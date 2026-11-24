#include "inktag_nrf_image_physical.h"

#include <stddef.h>
#include <string.h>

#include "inktag_board.h"
#include "inktag_nrf_health.h"
#include "inktag_protocol_core.h"
#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
#include "inktag_nrf_external_flash.h"
#include "nrf_error.h"
#else
#include "nrf_fstorage.h"
#include "nrf_fstorage_sd.h"
#endif

#if !defined(INKTAG_BOARD_IMAGE_STORE_START) || !defined(INKTAG_BOARD_IMAGE_STORE_SIZE)
#error "Nordic image store requires a board-owned Flash region"
#endif

#define IMAGE_RECORD_MAGIC 0x49475449u
#define IMAGE_RECORD_VERSION (INKTAG_NRF_SIMPLE_IMAGE_METADATA ? 4u : 2u)
#define IMAGE_STORE_START INKTAG_BOARD_IMAGE_STORE_START
#define IMAGE_STORE_SIZE INKTAG_BOARD_IMAGE_STORE_SIZE
#define IMAGE_STORE_END (IMAGE_STORE_START + IMAGE_STORE_SIZE)
#define IMAGE_STORE_SECTORS (IMAGE_STORE_SIZE / INKTAG_IMAGE_STORE_SECTOR_SIZE)
#define CLEANUP_MASK_WORDS ((IMAGE_STORE_SECTORS + 31u) / 32u)
#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
#define INKTAG_NRF_EXTERNAL_IMAGE_STORE 1
#else
#define INKTAG_NRF_EXTERNAL_IMAGE_STORE 0
#endif
#ifndef INKTAG_NRF_IMAGE_WRITE_BLOCK_SIZE
#define INKTAG_NRF_IMAGE_WRITE_BLOCK_SIZE 1024u
#endif
#ifndef INKTAG_NRF_IMAGE_WRITE_BLOCK_COUNT
#define INKTAG_NRF_IMAGE_WRITE_BLOCK_COUNT 4u
#endif
#define IMAGE_WRITE_BLOCK_SIZE INKTAG_NRF_IMAGE_WRITE_BLOCK_SIZE
#define IMAGE_WRITE_BLOCK_COUNT INKTAG_NRF_IMAGE_WRITE_BLOCK_COUNT
#define EXTERNAL_FLASH_PAGE_SIZE 256u

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
typedef enum {
    FLASH_STAGE_NONE = 0,
    FLASH_STAGE_BEGIN_ERASE,
    FLASH_STAGE_PAYLOAD_WRITE,
    FLASH_STAGE_HEADER_BODY,
    FLASH_STAGE_HEADER_MAGIC,
    FLASH_STAGE_CLEANUP_ERASE,
} flash_stage_t;
#endif

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
typedef enum {
    BLOCK_FREE = 0,
    BLOCK_FILLING,
    BLOCK_READY,
    BLOCK_IN_FLIGHT,
} block_state_t;

typedef struct {
    uint8_t data[IMAGE_WRITE_BLOCK_SIZE] __attribute__((aligned(4)));
    uint16_t length;
    uint16_t sequence;
    uint8_t state;
} image_write_block_t;
#endif

typedef struct {
    bool active;
    bool ready;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    bool commit_requested;
    bool drop_requested;
#endif
    bool abort_requested;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    bool write_failed;
#endif
    uint8_t id;
    uint8_t erase_next;
    uint8_t erase_remaining;
    uint32_t received;
    uint32_t crc;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    uint16_t next_sequence;
#endif
    inktag_image_store_header_t header __attribute__((aligned(4)));
} image_store_transaction_t;

#if !defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) || !INKTAG_BOARD_IMAGE_STORE_EXTERNAL
static void flash_event_handler(nrf_fstorage_evt_t *event);

NRF_FSTORAGE_DEF(nrf_fstorage_t s_image_flash) = {
    .evt_handler = flash_event_handler,
    .start_addr = IMAGE_STORE_START,
    .end_addr = IMAGE_STORE_END - 1u,
};
#endif

static image_store_transaction_t s_transaction;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static image_write_block_t s_blocks[IMAGE_WRITE_BLOCK_COUNT];
#endif
static bool s_initialized;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static volatile bool s_flash_pending;
static volatile bool s_flash_done;
static volatile uint32_t s_flash_result;
static flash_stage_t s_flash_stage;
static uint8_t s_inflight_block;
#endif
static uint32_t s_cleanup_mask[CLEANUP_MASK_WORDS];
static uint16_t s_cleanup_removed;
static uint8_t s_cleanup_id;
static inktag_image_store_event_type_t s_cleanup_event;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static uint8_t s_cleanup_run_start;
#endif
static bool s_event_pending;
static inktag_image_store_event_t s_event;
static bool s_flash_power_rejected;
static bool s_header_read_error;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
static uint8_t s_simple_target = UINT8_MAX;
#endif

static bool flash_io_busy(void) {
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    return false;
#else
    return s_flash_pending || s_flash_done;
#endif
}

_Static_assert(sizeof(inktag_image_store_header_t) == INKTAG_IMAGE_STORE_HEADER_SIZE,
               "image record header matches the selected format");
_Static_assert((IMAGE_STORE_START & (INKTAG_IMAGE_STORE_SECTOR_SIZE - 1u)) == 0u,
               "image store start must be page aligned");
_Static_assert((IMAGE_STORE_SIZE % INKTAG_IMAGE_STORE_SECTOR_SIZE) == 0u,
               "image store size must contain complete pages");
_Static_assert(IMAGE_STORE_SECTORS <= UINT8_MAX,
               "image record IDs and cleanup cursor must fit uint8_t");
_Static_assert((IMAGE_WRITE_BLOCK_SIZE & 3u) == 0u &&
                   IMAGE_WRITE_BLOCK_SIZE <= UINT16_MAX,
               "image write blocks must be aligned and fit the public API");
_Static_assert(IMAGE_WRITE_BLOCK_COUNT > 0u &&
                   IMAGE_WRITE_BLOCK_COUNT <= UINT8_MAX,
               "image write block count must fit the state machine");
#if (!defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) ||  !INKTAG_BOARD_IMAGE_STORE_EXTERNAL) &&  defined(INKTAG_BOARD_APP_START) && defined(INKTAG_BOARD_APP_LIMIT)
_Static_assert(INKTAG_BOARD_APP_START + INKTAG_BOARD_APP_LIMIT ==
                   INKTAG_BOARD_IMAGE_STORE_START,
               "application and image-store boundaries must be contiguous");
#endif
#if (!defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) ||  !INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && defined(INKTAG_BOARD_FIND_KEYRING_START)
_Static_assert(IMAGE_STORE_END == INKTAG_BOARD_FIND_KEYRING_START,
               "image store must end at the Find-keyring boundary");
#elif (!defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) ||  !INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && defined(INKTAG_BOARD_SETTINGS_START)
_Static_assert(IMAGE_STORE_END == INKTAG_BOARD_SETTINGS_START,
               "image store must end at the settings-journal boundary");
#endif

#if !defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) || !INKTAG_BOARD_IMAGE_STORE_EXTERNAL
static void flash_event_handler(nrf_fstorage_evt_t *event) {
    if (!event) return;
    s_flash_result = event->result;
    s_flash_pending = false;
    s_flash_done = true;
}
#endif

static void emit_event(inktag_image_store_event_type_t type,
                       inktag_image_store_result_t result,
                       uint8_t id, uint16_t removed) {
    if (s_event_pending) return;
    s_event.type = type;
    s_event.result = result;
    s_event.id = id;
    s_event.removed = removed;
    s_event_pending = true;
}

static uint32_t sector_address(uint16_t sector) {
    return IMAGE_STORE_START + (uint32_t)sector * INKTAG_IMAGE_STORE_SECTOR_SIZE;
}

static void cleanup_mask_reset(void) {
    memset(s_cleanup_mask, 0, sizeof(s_cleanup_mask));
}

static bool cleanup_mask_test(uint16_t sector) {
    return (s_cleanup_mask[sector >> 5] &
            (UINT32_C(1) << (sector & 31u))) != 0u;
}

static void cleanup_mask_set(uint16_t sector) {
    s_cleanup_mask[sector >> 5] |= UINT32_C(1) << (sector & 31u);
}

static void cleanup_mask_clear(uint16_t sector) {
    s_cleanup_mask[sector >> 5] &= ~(UINT32_C(1) << (sector & 31u));
}

static void cleanup_mask_set_range(uint16_t first, uint16_t count) {
    while (count-- != 0u) cleanup_mask_set(first++);
}

static bool cleanup_mask_any(void) {
    uint8_t word;
    for (word = 0u; word < CLEANUP_MASK_WORDS; ++word)
        if (s_cleanup_mask[word] != 0u) return true;
    return false;
}

static uint32_t payload_address(uint8_t id) {
    return sector_address(id) + INKTAG_IMAGE_STORE_HEADER_SIZE;
}

static bool flash_read(uint32_t address, void *data, uint32_t length) {
#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
    bool result;
#endif
    if (!s_initialized || !data || length == 0u ||
        address >= IMAGE_STORE_END || length > IMAGE_STORE_END - address)
        return false;
#if IMAGE_STORE_START != 0u
    if (address < IMAGE_STORE_START) return false;
#endif
#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
    if (!inktag_nrf_external_flash_begin()) return false;
    result = inktag_nrf_external_flash_read(address, data, length);
    inktag_nrf_external_flash_end();
    return result;
#else
    return nrf_fstorage_read(&s_image_flash, address, data, length) == NRF_SUCCESS;
#endif
}

#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
static bool external_flash_write(uint32_t address, const uint8_t *data,
                                 uint32_t length) {
    bool result = true;
    if (!inktag_nrf_external_flash_begin()) return false;
    while (length != 0u) {
        uint16_t chunk = (uint16_t)(EXTERNAL_FLASH_PAGE_SIZE -
                                    (address & (EXTERNAL_FLASH_PAGE_SIZE - 1u)));
        if (chunk > length) chunk = (uint16_t)length;
        if (!inktag_nrf_external_flash_program(address, data, chunk)) {
            result = false;
            break;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    if (!result && inktag_nrf_external_flash_last_error() == NRF_ERROR_FORBIDDEN)
        s_flash_power_rejected = true;
    inktag_nrf_external_flash_end();
    return result;
}

static bool external_flash_erase(uint16_t first, uint16_t count) {
    bool result = true;
    if (!inktag_nrf_external_flash_begin()) return false;
    while (count-- != 0u) {
        if (!inktag_nrf_external_flash_erase_sector(
                IMAGE_STORE_START + (uint32_t)first *
                                      INKTAG_IMAGE_STORE_SECTOR_SIZE)) {
            result = false;
            break;
        }
        first++;
    }
    if (!result && inktag_nrf_external_flash_last_error() == NRF_ERROR_FORBIDDEN)
        s_flash_power_rejected = true;
    inktag_nrf_external_flash_end();
    return result;
}
#endif

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static bool start_write(uint32_t address, const void *data, uint32_t length,
                        flash_stage_t stage) {
    uint32_t result;
    s_flash_power_rejected = false;
    if (!s_initialized || s_flash_pending || s_flash_done || !data ||
        (address & 3u) != 0u || (length & 3u) != 0u || length == 0u) return false;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        s_flash_power_rejected = true;
        return false;
    }
    s_flash_stage = stage;
    s_flash_pending = true;
    result = nrf_fstorage_write(&s_image_flash, address, data, length, NULL);
    if (result != NRF_SUCCESS) {
        s_flash_pending = false;
        s_flash_stage = FLASH_STAGE_NONE;
        return false;
    }
    return true;
}
#endif

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static bool start_erase(uint16_t first, uint16_t count, flash_stage_t stage) {
    uint32_t result;
    s_flash_power_rejected = false;
    if (!s_initialized || s_flash_pending || s_flash_done || count == 0u ||
        first >= IMAGE_STORE_SECTORS || count > IMAGE_STORE_SECTORS - first) return false;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        s_flash_power_rejected = true;
        return false;
    }
    s_flash_stage = stage;
    s_flash_pending = true;
    result = nrf_fstorage_erase(&s_image_flash, sector_address(first), count, NULL);
    if (result != NRF_SUCCESS) {
        s_flash_pending = false;
        s_flash_stage = FLASH_STAGE_NONE;
        return false;
    }
    return true;
}
#endif

static inktag_image_store_result_t start_failure_result(void) {
    return s_flash_power_rejected ? INKTAG_IMAGE_STORE_LOW_POWER :
                                    INKTAG_IMAGE_STORE_IO_ERROR;
}

static uint32_t header_crc(const inktag_image_store_header_t *header) {
    const uint8_t *start = (const uint8_t *)&header->version;
    uint16_t length = (uint16_t)(
        offsetof(inktag_image_store_header_t, header_crc) -
        offsetof(inktag_image_store_header_t, version));
    return inktag_crc32_update(0xFFFFFFFFu, start, length) ^ 0xFFFFFFFFu;
}

static uint16_t sectors_for_payload(uint32_t payload_size) {
    uint32_t sectors = (payload_size + INKTAG_IMAGE_STORE_HEADER_SIZE +
                        INKTAG_IMAGE_STORE_SECTOR_SIZE - 1u) /
                       INKTAG_IMAGE_STORE_SECTOR_SIZE;
    return sectors > UINT16_MAX ? 0u : (uint16_t)sectors;
}

static bool header_area_is_erased(const inktag_image_store_header_t *header,
                                  uint16_t sector) {
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    (void)sector;
    /* Reuse the record marker already read by status instead of scanning all
     * 4 KiB of every free external sector. Payload-only interrupted writes
     * remain invisible and are erased transactionally when the run is reused. */
    return header->magic == UINT32_MAX;
#else
    const uint32_t *words = (const uint32_t *)sector_address(sector);
    uint16_t i;
    for (i = 0u; i < INKTAG_IMAGE_STORE_SECTOR_SIZE / sizeof(uint32_t); ++i) {
        if (words[i] != 0xFFFFFFFFu) return false;
    }
    return true;
#endif
}

static bool header_is_valid(const inktag_image_store_header_t *header,
                            uint16_t sector) {
    epd_screen_config_t screen;
    uint32_t record_capacity;
    uint32_t segment_total;
    if (header->magic != IMAGE_RECORD_MAGIC ||
        !((header->version == IMAGE_RECORD_VERSION &&
           header->header_size == INKTAG_IMAGE_STORE_HEADER_SIZE)
        ) ||
        header->record_sectors == 0u ||
        sector + header->record_sectors > IMAGE_STORE_SECTORS ||
        header->segment_count == 0u ||
        header->segment_count > INKTAG_IMAGE_STORE_MAX_SEGMENTS ||
        header->content_type > INKTAG_IMAGE_CONTENT_MAX ||
        epd_screen_config_decode(header->screen_config, EPD_SCREEN_WIRE_SIZE,
                                 &screen) != EPD_SCREEN_STATUS_OK) return false;
    record_capacity = (uint32_t)header->record_sectors * INKTAG_IMAGE_STORE_SECTOR_SIZE;
    if (header->payload_size == 0u ||
        header->payload_size > record_capacity - header->header_size ||
        header->segment_size[0] > header->payload_size) return false;
    segment_total = header->segment_size[0];
    if (header->segment_count > 1u) {
        if (header->segment_size[1] > header->payload_size - segment_total) return false;
        segment_total += header->segment_size[1];
    } else if (header->segment_size[1] != 0u) return false;
    return segment_total == header->payload_size;
}

static bool read_valid_header(uint16_t sector,
                              inktag_image_store_header_t *header) {
    s_header_read_error = false;
    if (!header || sector >= IMAGE_STORE_SECTORS) return false;
    if (!flash_read(sector_address(sector), header, sizeof(*header))) {
        s_header_read_error = true;
        return false;
    }
    if (header_crc(header) != header->header_crc) return false;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    if (memchr(header->name, 0, sizeof(header->name)) == NULL) return false;
#endif
    return header_is_valid(header, sector);
}

static bool compatible(const inktag_image_store_header_t *header,
                       const epd_screen_config_t *screen) {
    uint8_t encoded[EPD_SCREEN_WIRE_SIZE];
    return screen && epd_screen_config_encode(screen, encoded) == EPD_SCREEN_STATUS_OK &&
           epd_screen_wire_image_compatible(header->screen_config, encoded);
}

#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
static inktag_image_store_result_t find_free_run(uint16_t required, uint8_t *id);
#include "inktag_nrf_image_simple.inc"
#endif

static inktag_image_store_result_t find_free_run(uint16_t required,
                                                  uint8_t *id) {
    uint16_t run_start = 0u, run_length = 0u, sector = 0u;
    inktag_image_store_header_t header;
    while (sector < IMAGE_STORE_SECTORS) {
        /* A failed physical read is conservatively reported as dirty. */
        header.magic = 0u;
        if (read_valid_header(sector, &header)) {
            sector = (uint16_t)(sector + header.record_sectors);
            run_length = 0u;
            continue;
        }
        if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
        if (run_length == 0u) run_start = sector;
        run_length++;
        if (run_length >= required) {
            *id = (uint8_t)run_start;
            return INKTAG_IMAGE_STORE_OK;
        }
        sector++;
    }
    return INKTAG_IMAGE_STORE_NO_SPACE;
}

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static void reset_blocks(void) {
    memset(s_blocks, 0, sizeof(s_blocks));
}

static image_write_block_t *filling_block(void) {
    uint8_t i;
    for (i = 0u; i < IMAGE_WRITE_BLOCK_COUNT; ++i)
        if (s_blocks[i].state == BLOCK_FILLING) return &s_blocks[i];
    return NULL;
}

static image_write_block_t *allocate_filling_block(void) {
    uint8_t i;
    for (i = 0u; i < IMAGE_WRITE_BLOCK_COUNT; ++i) {
        if (s_blocks[i].state == BLOCK_FREE) {
            s_blocks[i].state = BLOCK_FILLING;
            s_blocks[i].length = 0u;
            s_blocks[i].sequence = s_transaction.next_sequence++;
            return &s_blocks[i];
        }
    }
    return NULL;
}

static image_write_block_t *next_ready_block(void) {
    image_write_block_t *selected = NULL;
    uint8_t i;
    for (i = 0u; i < IMAGE_WRITE_BLOCK_COUNT; ++i) {
        if (s_blocks[i].state == BLOCK_READY &&
            (!selected || s_blocks[i].sequence < selected->sequence)) selected = &s_blocks[i];
    }
    return selected;
}

static bool blocks_pending(void) {
    uint8_t i;
    for (i = 0u; i < IMAGE_WRITE_BLOCK_COUNT; ++i)
        if (s_blocks[i].state != BLOCK_FREE) return true;
    return false;
}
#endif

static void clear_transaction(void) {
    memset(&s_transaction, 0, sizeof(s_transaction));
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    reset_blocks();
#endif
}

#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
static void finish_external_cleanup(inktag_image_store_result_t result) {
    cleanup_mask_reset();
    emit_event(s_cleanup_event, result, s_cleanup_id, s_cleanup_removed);
    if (s_cleanup_event == INKTAG_IMAGE_STORE_EVENT_ABORT) clear_transaction();
    s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
}
#endif

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
static bool cleanup_start_next(void) {
    uint8_t first = 0u;
    while (first < IMAGE_STORE_SECTORS &&
           !cleanup_mask_test(first)) first++;
    if (first >= IMAGE_STORE_SECTORS) return false;
    s_cleanup_run_start = first;
    /* Submit one page per operation so the supply is re-sampled before every
     * physical erase rather than once for a multi-page fstorage request. */
    return start_erase(first, 1u, FLASH_STAGE_CLEANUP_ERASE);
}
#endif

#ifdef INKTAG_IMAGE_PHYSICAL_TEST
/* Host-only cold-init seam; retained Flash bytes represent a normal reboot. */
void inktag_image_physical_test_reboot(void) {
    s_initialized = false;
    s_event_pending = false;
    s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
    memset(&s_transaction, 0, sizeof(s_transaction));
}
#endif

bool inktag_image_physical_init(void) {
    if (s_initialized) return true;
#if defined(INKTAG_BOARD_IMAGE_STORE_EXTERNAL) && INKTAG_BOARD_IMAGE_STORE_EXTERNAL
    if (!inktag_nrf_external_flash_begin()) return false;
    inktag_nrf_external_flash_end();
#else
    if (nrf_fstorage_init(&s_image_flash, &nrf_fstorage_sd, NULL) != NRF_SUCCESS)
        return false;
#endif
    s_initialized = true;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    s_simple_target = UINT8_MAX;
#endif
    clear_transaction();
    return true;
}

void inktag_image_physical_process(void) {
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    uint8_t first;
    if (!s_initialized) return;

    if (s_cleanup_event != INKTAG_IMAGE_STORE_EVENT_NONE) {
        for (first = 0u; first < IMAGE_STORE_SECTORS; ++first)
            if (cleanup_mask_test(first)) break;
        if (first < IMAGE_STORE_SECTORS) {
            s_flash_power_rejected = false;
            if (!external_flash_erase(first, 1u)) {
                finish_external_cleanup(start_failure_result());
                return;
            }
            cleanup_mask_clear(first);
            return;
        }
        finish_external_cleanup(INKTAG_IMAGE_STORE_OK);
        return;
    }

    if (s_transaction.abort_requested) {
        uint16_t sectors = s_transaction.header.record_sectors;
        uint8_t id = s_transaction.id;
        cleanup_mask_reset();
        cleanup_mask_set_range(id, sectors);
        s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_ABORT;
        s_cleanup_id = id;
        s_cleanup_removed = 0u;
        s_transaction.abort_requested = false;
        return;
    }

    if (s_transaction.active && !s_transaction.ready &&
        s_transaction.erase_remaining != 0u) {
        uint8_t id = s_transaction.id;
        s_flash_power_rejected = false;
        if (!external_flash_erase(s_transaction.erase_next, 1u)) {
            inktag_image_store_result_t result = start_failure_result();
            clear_transaction();
            emit_event(INKTAG_IMAGE_STORE_EVENT_BEGIN, result, id, 0u);
            return;
        }
        s_transaction.erase_next++;
        s_transaction.erase_remaining--;
        if (s_transaction.erase_remaining == 0u) {
            s_transaction.ready = true;
            emit_event(INKTAG_IMAGE_STORE_EVENT_BEGIN,
                       INKTAG_IMAGE_STORE_OK, id, 0u);
        }
    }
#else
    flash_stage_t completed_stage;
    uint32_t completed_result;
    if (!s_initialized) return;

    if (s_flash_done) {
        completed_stage = s_flash_stage;
        completed_result = s_flash_result;
        s_flash_done = false;
        s_flash_stage = FLASH_STAGE_NONE;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
        if (s_transaction.drop_requested &&
            completed_stage != FLASH_STAGE_CLEANUP_ERASE) {
            /* The asynchronous backend has released its source buffer. A
             * low-power cancel may now abandon the incomplete record without
             * issuing the cleanup erase that a normal abort performs. */
            clear_transaction();
            return;
        }
#endif
        if (completed_result != NRF_SUCCESS) {
            inktag_image_store_result_t failure =
                s_flash_power_rejected ? INKTAG_IMAGE_STORE_LOW_POWER :
                                         INKTAG_IMAGE_STORE_IO_ERROR;
            if (completed_stage == FLASH_STAGE_BEGIN_ERASE) {
                uint8_t id = s_transaction.id;
                clear_transaction();
                emit_event(INKTAG_IMAGE_STORE_EVENT_BEGIN,
                           failure, id, 0u);
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
            } else if (completed_stage == FLASH_STAGE_PAYLOAD_WRITE) {
                uint8_t id = s_transaction.id;
                s_blocks[s_inflight_block].state = BLOCK_FREE;
                if (failure == INKTAG_IMAGE_STORE_LOW_POWER)
                    clear_transaction();
                else
                    s_transaction.write_failed = true;
                emit_event(INKTAG_IMAGE_STORE_EVENT_WRITE,
                           failure, id, 0u);
            } else if (completed_stage == FLASH_STAGE_HEADER_BODY ||
                       completed_stage == FLASH_STAGE_HEADER_MAGIC) {
                uint8_t id = s_transaction.id;
                clear_transaction();
                emit_event(INKTAG_IMAGE_STORE_EVENT_COMMIT,
                           failure, id, 0u);
#endif
            } else if (completed_stage == FLASH_STAGE_CLEANUP_ERASE) {
                emit_event(s_cleanup_event, failure,
                           s_cleanup_id, s_cleanup_removed);
                cleanup_mask_reset();
                s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
            }
            return;
        }

        if (completed_stage == FLASH_STAGE_BEGIN_ERASE) {
            if (s_transaction.erase_remaining != 0u) {
                uint8_t next = s_transaction.erase_next++;
                s_transaction.erase_remaining--;
                if (!start_erase(next, 1u, FLASH_STAGE_BEGIN_ERASE)) {
                    uint8_t id = s_transaction.id;
                    inktag_image_store_result_t result = start_failure_result();
                    clear_transaction();
                    emit_event(INKTAG_IMAGE_STORE_EVENT_BEGIN, result, id, 0u);
                }
                return;
            }
            s_transaction.ready = true;
            emit_event(INKTAG_IMAGE_STORE_EVENT_BEGIN, INKTAG_IMAGE_STORE_OK,
                       s_transaction.id, 0u);
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
        } else if (completed_stage == FLASH_STAGE_PAYLOAD_WRITE) {
            s_blocks[s_inflight_block].state = BLOCK_FREE;
        } else if (completed_stage == FLASH_STAGE_HEADER_BODY) {
            if (!start_write(sector_address(s_transaction.id),
                              &s_transaction.header.magic, sizeof(uint32_t),
                              FLASH_STAGE_HEADER_MAGIC)) {
                uint8_t id = s_transaction.id;
                inktag_image_store_result_t result = start_failure_result();
                clear_transaction();
                emit_event(INKTAG_IMAGE_STORE_EVENT_COMMIT,
                           result, id, 0u);
            }
            return;
        } else if (completed_stage == FLASH_STAGE_HEADER_MAGIC) {
            uint8_t id = s_transaction.id;
            clear_transaction();
            emit_event(INKTAG_IMAGE_STORE_EVENT_COMMIT, INKTAG_IMAGE_STORE_OK,
                       id, 0u);
#endif
        } else if (completed_stage == FLASH_STAGE_CLEANUP_ERASE) {
            cleanup_mask_clear(s_cleanup_run_start);
            if (!cleanup_mask_any()) {
                inktag_image_store_event_type_t event = s_cleanup_event;
                uint8_t id = s_cleanup_id;
                uint16_t removed = s_cleanup_removed;
                s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
                if (event == INKTAG_IMAGE_STORE_EVENT_ABORT) clear_transaction();
                emit_event(event, INKTAG_IMAGE_STORE_OK, id, removed);
            }
        }
    }

    if (s_flash_pending || s_flash_done) return;

    if (s_cleanup_event != INKTAG_IMAGE_STORE_EVENT_NONE && cleanup_mask_any()) {
        if (!cleanup_start_next()) {
            inktag_image_store_result_t result = start_failure_result();
            if (s_cleanup_event == INKTAG_IMAGE_STORE_EVENT_ABORT)
                clear_transaction();
            emit_event(s_cleanup_event, result,
                       s_cleanup_id, s_cleanup_removed);
            cleanup_mask_reset();
            s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
        }
        return;
    }

    if (s_transaction.abort_requested) {
        uint16_t sectors = s_transaction.header.record_sectors;
        uint8_t id = s_transaction.id;
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
        s_write_buffer_length = 0u;
#else
        reset_blocks();
        s_transaction.commit_requested = false;
#endif
        cleanup_mask_reset();
        cleanup_mask_set_range(id, sectors);
        s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_ABORT;
        s_cleanup_id = id;
        s_cleanup_removed = 0u;
        s_transaction.abort_requested = false;
        return;
    }

#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    if (s_transaction.active && s_transaction.ready) {
        image_write_block_t *block = next_ready_block();
        if (block) {
            uint32_t address = payload_address(s_transaction.id) +
                               (uint32_t)block->sequence * IMAGE_WRITE_BLOCK_SIZE;
            uint32_t write_length = (block->length + 3u) & ~3u;
            if (write_length > block->length)
                memset(&block->data[block->length], 0xFF, write_length - block->length);
            s_inflight_block = (uint8_t)(block - s_blocks);
            block->state = BLOCK_IN_FLIGHT;
            if (!start_write(address, block->data, write_length,
                              FLASH_STAGE_PAYLOAD_WRITE)) {
                inktag_image_store_result_t result = start_failure_result();
                block->state = BLOCK_FREE;
                if (result == INKTAG_IMAGE_STORE_LOW_POWER) {
                    uint8_t id = s_transaction.id;
                    clear_transaction();
                    emit_event(INKTAG_IMAGE_STORE_EVENT_WRITE, result, id, 0u);
                    return;
                }
                s_transaction.write_failed = true;
                emit_event(INKTAG_IMAGE_STORE_EVENT_WRITE,
                           result, s_transaction.id, 0u);
            }
            return;
        }
        if (s_transaction.commit_requested && !blocks_pending()) {
            const uint8_t *bytes = (const uint8_t *)&s_transaction.header;
            if (!start_write(sector_address(s_transaction.id) + sizeof(uint32_t),
                              bytes + sizeof(uint32_t),
                              sizeof(s_transaction.header) - sizeof(uint32_t),
                              FLASH_STAGE_HEADER_BODY)) {
                uint8_t id = s_transaction.id;
                inktag_image_store_result_t result = start_failure_result();
                clear_transaction();
                emit_event(INKTAG_IMAGE_STORE_EVENT_COMMIT,
                           result, id, 0u);
            }
        }
    }
#endif
#endif
}

bool inktag_image_physical_take_event(inktag_image_store_event_t *event) {
    if (!event || !s_event_pending) return false;
    *event = s_event;
    s_event_pending = false;
    memset(&s_event, 0, sizeof(s_event));
    return true;
}

bool inktag_image_physical_supported(void) { return s_initialized; }

bool inktag_image_physical_busy(void) {
    return flash_io_busy() || s_transaction.active ||
           s_cleanup_event != INKTAG_IMAGE_STORE_EVENT_NONE;
}

uint16_t inktag_image_physical_sector_count(void) {
    return (uint16_t)IMAGE_STORE_SECTORS;
}

inktag_image_store_result_t inktag_image_physical_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status) {
    uint16_t sector = 0u, free_run = 0u;
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!status) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (inktag_image_physical_busy()) return INKTAG_IMAGE_STORE_BUSY;
    memset(status, 0, sizeof(*status));
    status->total_sectors = (uint16_t)IMAGE_STORE_SECTORS;
    status->required_sectors = sectors_for_payload(payload_size);
    while (sector < IMAGE_STORE_SECTORS) {
        if (read_valid_header(sector, &header)) {
            status->saved_count++;
            if (compatible(&header, screen)) {
                status->compatible_count++;
                if (header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)
                    status->playable_count++;
            } else status->incompatible_count++;
            sector = (uint16_t)(sector + header.record_sectors);
            free_run = 0u;
            continue;
        }
        if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
        if (!header_area_is_erased(&header, sector)) status->dirty_sectors++;
        status->free_sectors++;
        free_run++;
        if (free_run > status->largest_free_run) status->largest_free_run = free_run;
        sector++;
    }
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_physical_begin(
    const epd_screen_config_t *screen, uint8_t format,
    uint8_t segment_count, uint8_t black_mask, uint8_t content_type,
    const uint32_t segment_size[2], uint32_t payload_size,
    uint32_t payload_crc, uint32_t created_at, const char *name, uint8_t *id) {
    uint32_t segment_total;
    uint16_t required;
    uint8_t record_id;
    inktag_image_store_result_t result;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    uint8_t requested_target = s_simple_target;
    s_simple_target = UINT8_MAX;
#endif
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_IMAGE_STORE_LOW_POWER;
    if (!screen || !segment_size || !id || payload_size == 0u ||
        segment_count == 0u || segment_count > INKTAG_IMAGE_STORE_MAX_SEGMENTS ||
        content_type > INKTAG_IMAGE_CONTENT_MAX) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (segment_size[0] > payload_size ||
        (segment_count > 1u && segment_size[1] > payload_size - segment_size[0]))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    segment_total = segment_size[0] + (segment_count > 1u ? segment_size[1] : 0u);
    required = sectors_for_payload(payload_size);
    if (segment_total != payload_size || required == 0u || required > IMAGE_STORE_SECTORS)
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    s_simple_target = requested_target;
    result = simple_select_target(required, name, &record_id);
#else
    result = find_free_run(required, &record_id);
#endif
    if (result != INKTAG_IMAGE_STORE_OK) return result;

    clear_transaction();
    s_transaction.active = true;
    s_transaction.id = record_id;
    s_transaction.crc = 0xFFFFFFFFu;
    s_transaction.header.magic = IMAGE_RECORD_MAGIC;
    s_transaction.header.version = IMAGE_RECORD_VERSION;
    s_transaction.header.header_size = INKTAG_IMAGE_STORE_HEADER_SIZE;
    s_transaction.header.record_sectors = required;
    s_transaction.header.format = format;
    if (epd_screen_config_encode(screen, s_transaction.header.screen_config) !=
        EPD_SCREEN_STATUS_OK) {
        clear_transaction();
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    }
    s_transaction.header.segment_count = segment_count;
    s_transaction.header.black_mask = black_mask;
    s_transaction.header.content_type = content_type;
    s_transaction.header.segment_size[0] = segment_size[0];
    s_transaction.header.segment_size[1] = segment_count > 1u ? segment_size[1] : 0u;
    s_transaction.header.payload_size = payload_size;
    s_transaction.header.payload_crc = payload_crc;
    s_transaction.header.created_at = created_at;
    if (name) {
        strncpy(s_transaction.header.name, name, INKTAG_IMAGE_STORE_NAME_SIZE - 1u);
        s_transaction.header.name[INKTAG_IMAGE_STORE_NAME_SIZE - 1u] = '\0';
    }
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    s_transaction.erase_next = record_id;
    s_transaction.erase_remaining = (uint8_t)required;
#else
    s_transaction.erase_next = (uint8_t)(record_id + 1u);
    s_transaction.erase_remaining = (uint8_t)(required - 1u);
    if (!start_erase(record_id, 1u, FLASH_STAGE_BEGIN_ERASE)) {
        inktag_image_store_result_t result = start_failure_result();
        clear_transaction();
        return result;
    }
#endif
    *id = record_id;
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_physical_append(const uint8_t *data,
                                                       uint16_t length) {
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    uint16_t remaining = length;
    uint16_t capacity = 0u;
    uint8_t i;
    image_write_block_t *block;
#endif
    if (!s_transaction.active || !s_transaction.ready || !data || length == 0u)
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, false))
        return INKTAG_IMAGE_STORE_LOW_POWER;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    if (s_transaction.write_failed) return INKTAG_IMAGE_STORE_IO_ERROR;
#endif
    if (length > s_transaction.header.payload_size - s_transaction.received)
        return INKTAG_IMAGE_STORE_SIZE_MISMATCH;
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    s_flash_power_rejected = false;
    if (!external_flash_write(payload_address(s_transaction.id) +
                                  s_transaction.received,
                              data, length)) {
        s_transaction.ready = false;
        return start_failure_result();
    }
#else
    for (i = 0u; i < IMAGE_WRITE_BLOCK_COUNT; ++i) {
        if (s_blocks[i].state == BLOCK_FREE) capacity += IMAGE_WRITE_BLOCK_SIZE;
        else if (s_blocks[i].state == BLOCK_FILLING)
            capacity += IMAGE_WRITE_BLOCK_SIZE - s_blocks[i].length;
    }
    if (capacity < length) return INKTAG_IMAGE_STORE_BUSY;

    while (remaining > 0u) {
        uint16_t chunk;
        block = filling_block();
        if (!block) block = allocate_filling_block();
        if (!block) return INKTAG_IMAGE_STORE_BUSY;
        chunk = (uint16_t)(IMAGE_WRITE_BLOCK_SIZE - block->length);
        if (chunk > remaining) chunk = remaining;
        memcpy(&block->data[block->length], &data[length - remaining], chunk);
        block->length = (uint16_t)(block->length + chunk);
        remaining = (uint16_t)(remaining - chunk);
        if (block->length == IMAGE_WRITE_BLOCK_SIZE) block->state = BLOCK_READY;
    }
#endif
    s_transaction.crc = inktag_crc32_update(s_transaction.crc, data, length);
    s_transaction.received += length;
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_physical_commit(void) {
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    image_write_block_t *block;
#endif
    if (!s_transaction.active || !s_transaction.ready)
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_IMAGE_STORE_LOW_POWER;
#if !INKTAG_NRF_EXTERNAL_IMAGE_STORE
    if (s_transaction.write_failed) return INKTAG_IMAGE_STORE_IO_ERROR;
#endif
    if (s_transaction.received != s_transaction.header.payload_size)
        return INKTAG_IMAGE_STORE_SIZE_MISMATCH;
    if ((s_transaction.crc ^ 0xFFFFFFFFu) != s_transaction.header.payload_crc)
        return INKTAG_IMAGE_STORE_CRC_MISMATCH;
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
    s_transaction.header.header_crc = header_crc(&s_transaction.header);
    {
        const uint8_t *bytes = (const uint8_t *)&s_transaction.header;
        uint8_t id = s_transaction.id;
        s_flash_power_rejected = false;
        if (!external_flash_write(sector_address(id) + sizeof(uint32_t),
                                  bytes + sizeof(uint32_t),
                                  sizeof(s_transaction.header) - sizeof(uint32_t)) ||
            !external_flash_write(sector_address(id), bytes,
                                  sizeof(uint32_t))) {
            s_transaction.ready = false;
            return start_failure_result();
        }
        clear_transaction();
        emit_event(INKTAG_IMAGE_STORE_EVENT_COMMIT, INKTAG_IMAGE_STORE_OK,
                   id, 0u);
    }
#else
    block = filling_block();
    if (block && block->length > 0u) block->state = BLOCK_READY;
    s_transaction.header.header_crc = header_crc(&s_transaction.header);
    s_transaction.commit_requested = true;
#endif
    return INKTAG_IMAGE_STORE_OK;
}

void inktag_image_physical_abort(void) {
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    s_simple_target = UINT8_MAX;
#endif
    if (!s_transaction.active) return;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, false)) {
        /* Do not turn a low-voltage abort into another unsafe erase. The
         * incomplete, uncommitted sectors remain dirty and are never exposed
         * as a record. Preserve an in-flight write's source buffer until its
         * asynchronous completion event, then release the transaction. */
#if INKTAG_NRF_EXTERNAL_IMAGE_STORE
        clear_transaction();
#else
        if (s_flash_pending || s_flash_done)
            s_transaction.drop_requested = true;
        else
            clear_transaction();
#endif
        return;
    }
    s_transaction.abort_requested = true;
}

bool inktag_image_physical_capture_active(void) { return s_transaction.active; }

inktag_image_store_result_t inktag_image_physical_get(
    uint8_t id, inktag_image_store_header_t *header) {
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!header) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (inktag_image_physical_busy()) return INKTAG_IMAGE_STORE_BUSY;
    if (read_valid_header(id, header)) return INKTAG_IMAGE_STORE_OK;
    return s_header_read_error ? INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_NOT_FOUND;
}

static inktag_image_store_result_t next_record(
    uint8_t after_id, const epd_screen_config_t *screen, bool slideshow_only,
    uint8_t *id) {
    uint16_t first = UINT16_MAX, next = UINT16_MAX, sector = 0u;
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!id) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (inktag_image_physical_busy()) return INKTAG_IMAGE_STORE_BUSY;
    while (sector < IMAGE_STORE_SECTORS) {
        if (read_valid_header(sector, &header)) {
            if (compatible(&header, screen) &&
                (!slideshow_only || header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)) {
                if (first == UINT16_MAX) first = sector;
                if (after_id != UINT8_MAX && sector > after_id && next == UINT16_MAX)
                    next = sector;
            }
            sector = (uint16_t)(sector + header.record_sectors);
        } else {
            if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
            sector++;
        }
    }
    if (next == UINT16_MAX) next = first;
    if (next == UINT16_MAX || next > UINT8_MAX) return INKTAG_IMAGE_STORE_NOT_FOUND;
    *id = (uint8_t)next;
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_physical_next_compatible(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id) {
    return next_record(after_id, screen, true, id);
}

inktag_image_store_result_t inktag_image_physical_next_page(
    uint8_t after_id, const epd_screen_config_t *screen, uint8_t *id) {
    return next_record(after_id, screen, false, id);
}

inktag_image_store_result_t inktag_image_physical_read(
    uint8_t id, uint32_t offset, uint8_t *data, uint16_t length) {
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (!data) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (inktag_image_physical_busy()) return INKTAG_IMAGE_STORE_BUSY;
    if (!read_valid_header(id, &header))
        return s_header_read_error ? INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_NOT_FOUND;
    if (offset > header.payload_size || length > header.payload_size - offset)
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!flash_read(sector_address(id) + header.header_size + offset, data, length))
        return INKTAG_IMAGE_STORE_IO_ERROR;
    return INKTAG_IMAGE_STORE_OK;
}


static inktag_image_store_result_t begin_cleanup(
    uint16_t removed, uint8_t id, inktag_image_store_event_type_t event) {
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        cleanup_mask_reset();
        return INKTAG_IMAGE_STORE_LOW_POWER;
    }
    s_cleanup_removed = removed;
    s_cleanup_id = id;
    s_cleanup_event = event;
    if (!cleanup_mask_any()) {
        emit_event(event, INKTAG_IMAGE_STORE_OK, id, removed);
        s_cleanup_event = INKTAG_IMAGE_STORE_EVENT_NONE;
    }
    return INKTAG_IMAGE_STORE_OK;
}

inktag_image_store_result_t inktag_image_physical_delete(uint8_t id) {
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    if (!read_valid_header(id, &header))
        return s_header_read_error ? INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_NOT_FOUND;
    cleanup_mask_reset();
    cleanup_mask_set_range(id, header.record_sectors);
    return begin_cleanup(1u, id, INKTAG_IMAGE_STORE_EVENT_DELETE);
}

inktag_image_store_result_t inktag_image_physical_clear_incompatible(
    const epd_screen_config_t *screen) {
    uint16_t sector = 0u, removed = 0u;
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    cleanup_mask_reset();
    while (sector < IMAGE_STORE_SECTORS) {
        if (read_valid_header(sector, &header)) {
            if (!compatible(&header, screen)) {
                cleanup_mask_set_range(sector, header.record_sectors);
                removed++;
            }
            sector = (uint16_t)(sector + header.record_sectors);
        } else {
            if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
            sector++;
        }
    }
    return begin_cleanup(removed, 0u,
                         INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE);
}

inktag_image_store_result_t inktag_image_physical_clear_all(void) {
    uint16_t sector = 0u, removed = 0u;
    inktag_image_store_header_t header;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    while (sector < IMAGE_STORE_SECTORS) {
        if (read_valid_header(sector, &header)) {
            removed++;
            sector = (uint16_t)(sector + header.record_sectors);
        } else {
            if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
            sector++;
        }
    }
    cleanup_mask_reset();
    cleanup_mask_set_range(0u, IMAGE_STORE_SECTORS);
    return begin_cleanup(removed, 0u, INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL);
}

inktag_image_store_result_t inktag_image_physical_clear_selected(
    const uint8_t *ids, uint8_t count, bool incompatible_operation) {
    uint16_t removed = 0u;
    uint8_t i;
    if (!s_initialized) return INKTAG_IMAGE_STORE_UNSUPPORTED;
    if (inktag_image_physical_busy() || s_event_pending) return INKTAG_IMAGE_STORE_BUSY;
    if (count != 0u && ids == NULL) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    cleanup_mask_reset();
    for (i = 0u; i < count; ++i) {
        inktag_image_store_header_t header;
        if (!read_valid_header(ids[i], &header)) {
            cleanup_mask_reset();
            return INKTAG_IMAGE_STORE_NOT_FOUND;
        }
        cleanup_mask_set_range(ids[i],
            header.record_sectors
        );
        removed++;
    }
    return begin_cleanup(
        removed, 0u,
        incompatible_operation ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE :
                                 INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL);
}
