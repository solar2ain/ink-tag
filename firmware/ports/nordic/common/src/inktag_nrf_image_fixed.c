/* Nordic thin IO for the reviewed common fixed-image engine. Product state,
 * names, geometry, CRC and publication live only in common/storage. */
#include "inktag_nrf_image_store.h"
#include "inktag_board.h"
#include "inktag_nrf_health.h"
#include "inktag_epd_config.h"
#include "inktag_nrf_factory_defaults.h"
#include "nrf_error.h"
#include <string.h>

#define IO_START INKTAG_BOARD_IMAGE_STORE_START
#define IO_SIZE INKTAG_BOARD_IMAGE_STORE_SIZE
#define IO_END (IO_START + IO_SIZE)
#define IO_SECTORS (IO_SIZE / 4096u)
typedef char io_extent_aligned[!(IO_START & 4095u) && !(IO_SIZE & 4095u) ? 1 : -1];
typedef char io_after_recovery[INKTAG_BOARD_DFU_SETTINGS_START + 4096u == IO_START ? 1 : -1];
typedef char io_physical_end[IO_END == INKTAG_BOARD_FLASH_SIZE ? 1 : -1];

#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#include "nrf_sdh.h"
#include "app_timer.h"
static void io_completed(nrf_fstorage_evt_t *event);
NRF_FSTORAGE_DEF(nrf_fstorage_t io_flash) = {
    .evt_handler = io_completed, .start_addr = IO_START, .end_addr = IO_END - 1u,
};
static bool io_ready;
static volatile bool io_active, io_pending, io_cancelled;
static volatile uint32_t io_result;
/* Static source survives a timeout until the SDK actually releases it. */
static uint32_t io_words[64];
#define IO_WAIT_TICKS APP_TIMER_TICKS(2000u)
#ifndef INKTAG_NRF_IMAGE_WAIT_POLLS
#define INKTAG_NRF_IMAGE_WAIT_POLLS 2000000u
#endif

static void io_completed(nrf_fstorage_evt_t *event) {
    if (event && io_pending) { io_result = event->result; io_pending = false; }
}
static bool io_range(uint32_t addr, uint32_t length) {
    return addr >= IO_START && addr < IO_END && length && length <= IO_END - addr;
}
static bool io_read(uint32_t addr, void *out, uint32_t length) {
    if (!io_ready || io_pending || !out || !io_range(addr, length)) return false;
    /* nrf_fstorage_read requires aligned SOURCE, not aligned destination/length.
     * Product reads/download/replay accept arbitrary byte offsets. */
    unsigned skip = addr & 3u;
    if (skip) {
        uint32_t word;
        if (nrf_fstorage_read(&io_flash, addr & ~3u, &word, 4u) != NRF_SUCCESS)
            return false;
        unsigned head = 4u - skip;
        if (head > length) head = length;
        memcpy(out, (const uint8_t *)&word + skip, head);
        addr += head; out = (uint8_t *)out + head; length -= head;
    }
    return !length || nrf_fstorage_read(&io_flash, addr, out, length) == NRF_SUCCESS;
}
static bool io_expired(uint32_t started, unsigned polls) {
    return polls >= INKTAG_NRF_IMAGE_WAIT_POLLS ||
        app_timer_cnt_diff_compute(app_timer_cnt_get(), started) >= IO_WAIT_TICKS;
}
static inktag_image_store_result_t io_submit(uint32_t addr, uint32_t length, bool erase) {
    uint32_t started = app_timer_cnt_get(), result;
    unsigned polls = 0;
    do {
        /* Poll SDK events, never the application scheduler: RX's execution
         * lease cannot be overwritten, and disconnect/abort can latch cancel. */
        nrf_sdh_evts_poll();
        if (io_cancelled) return INKTAG_IMAGE_STORE_IO_ERROR;
        if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
            return INKTAG_IMAGE_STORE_LOW_POWER;
        io_result = NRF_ERROR_BUSY; io_pending = true;
        result = erase ? nrf_fstorage_erase(&io_flash, addr, 1u, NULL) :
            nrf_fstorage_write(&io_flash, addr, io_words, length, NULL);
        if (result != NRF_SUCCESS) io_pending = false;
        if (result != NRF_ERROR_BUSY) break;
        if (io_expired(started, ++polls)) return INKTAG_IMAGE_STORE_IO_ERROR;
    } while (true);
    if (result != NRF_SUCCESS) return INKTAG_IMAGE_STORE_IO_ERROR;
    while (io_pending) {
        nrf_sdh_evts_poll();
        if (io_expired(started, ++polls) && io_pending)
            return INKTAG_IMAGE_STORE_IO_ERROR; /* source/pending stay owned */
    }
    return io_result == NRF_SUCCESS && !io_cancelled ? INKTAG_IMAGE_STORE_OK :
        INKTAG_IMAGE_STORE_IO_ERROR;
}
static inktag_image_store_result_t io_mutate(uint32_t addr, const void *data, uint16_t length) {
    if (!io_ready) return INKTAG_IMAGE_STORE_IO_ERROR;
    if (io_active || io_pending) return INKTAG_IMAGE_STORE_BUSY;
    if (io_cancelled) return INKTAG_IMAGE_STORE_IO_ERROR;
    if (!io_range(addr, data ? length : 4096u) || (!data && (addr & 4095u)))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    io_active = true;
    inktag_image_store_result_t result = INKTAG_IMAGE_STORE_OK;
    if (!data) result = io_submit(addr, 4096u, true);
    else while (length && result == INKTAG_IMAGE_STORE_OK) {
        uint32_t aligned = addr & ~3u;
        unsigned skip = addr & 3u;
        unsigned chunk = sizeof(io_words) - skip;
        if (chunk > length) chunk = length;
        unsigned bytes = (skip + chunk + 3u) & ~3u;
        if (!io_range(aligned, bytes) || !io_read(aligned, io_words, bytes)) {
            result = INKTAG_IMAGE_STORE_IO_ERROR; break;
        }
        uint8_t *words = (uint8_t *)io_words;
        const uint8_t *input = data;
        for (unsigned n = 0; n < chunk; n++) {
            if ((words[skip + n] & input[n]) != input[n]) {
                result = INKTAG_IMAGE_STORE_IO_ERROR; break;
            }
            words[skip + n] = input[n];
        }
        if (result != INKTAG_IMAGE_STORE_OK) break;
        result = io_submit(aligned, bytes, false);
        /* SDK success is followed by actual readback, not caller-source CRC. */
        if (result == INKTAG_IMAGE_STORE_OK) {
            uint32_t check[64];
            if (!io_read(aligned, check, bytes) || memcmp(check, io_words, bytes))
                result = INKTAG_IMAGE_STORE_IO_ERROR;
        }
        addr += chunk; data = input + chunk; length -= chunk;
    }
    io_active = false; return result;
}
static bool io_config(epd_screen_config_t *out) {
    return inktag_nrf_boot_screen(out);
}

#define INKTAG_FIXED_STORE_SECTORS IO_SECTORS
#define INKTAG_FIXED_STORE_CAPACITY(step) (IO_SECTORS / (step))
#define INKTAG_FIXED_STORE_ADDRESS(id,step) (IO_START + (id) * (step) * 4096u)
#define INKTAG_FIXED_STORE_READ io_read
#define INKTAG_FIXED_STORE_MUTATE io_mutate
#define INKTAG_FIXED_STORE_POWER_ALLOWED() inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)
#define INKTAG_FIXED_STORE_CONFIG_READ io_config
/* Public wrappers expose outstanding SDK ownership and defer reentrant abort
 * until the current mutation has drained. They add no product state machine. */
#define inktag_image_store_init io_core_init
#define inktag_image_store_busy io_core_busy
#define inktag_image_store_abort io_core_abort
#define inktag_image_store_process io_core_process
#include "inktag_image_fixed_store.inc"
#undef inktag_image_store_init
#undef inktag_image_store_busy
#undef inktag_image_store_abort
#undef inktag_image_store_process

bool inktag_image_store_init(void) {
    if (io_active || io_pending || io_core_busy()) return false;
    io_cancelled = false;
    if (!io_ready) {
        if (nrf_fstorage_init(&io_flash, &nrf_fstorage_sd, NULL) != NRF_SUCCESS) return false;
        io_ready = true;
    }
    return io_core_init();
}
bool inktag_image_store_busy(void) { return io_active || io_pending || io_core_busy(); }
void inktag_image_store_abort(void) {
    /* IRQ never changes the common foreground phase, including the narrow
     * interval just before/after a physical MUTATE. Do not clear this latch
     * in MUTATE: that could erase a disconnect/cancel delivered between calls. */
    io_cancelled = true;
}
void inktag_image_store_process(void) {
    if (io_active) return;
    if (io_cancelled) {
        io_cancelled = false; io_core_abort(); return;
    }
    io_core_process();
    if (io_cancelled) {
        io_cancelled = false; io_core_abort();
    }
}
