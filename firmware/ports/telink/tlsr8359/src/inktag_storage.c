#include "main.h"
#include "inktag_protocol_core.h"
#if INKTAG_BUILD_PRO
#include "inktag_nfc_command.h"
#endif
#include "inktag_tlsr_image_flash.h"
#include <string.h>

#define INKTAG_FLASH_SCAN_CHUNK_SIZE 256u
void inktag_gui_cooperative_yield(void);
static RAM bool s_high_image_extent;

static bool bytes_are_erased(uint32_t address, uint32_t length) {
    uint8_t data[INKTAG_FLASH_SCAN_CHUNK_SIZE];
    while (length) {
        uint16_t count = length > sizeof(data) ? sizeof(data) : length;
        flash_read_page(address, count, data);
        for (uint16_t i = 0; i < count; ++i)
            if (data[i] != 0xFFu) return false;
        address += count;
        length -= count;
    }
    return true;
}

static bool flash_write_bytes(uint32_t address, const uint8_t *data,
                              uint32_t length) {
    while (length) {
        uint16_t room = (uint16_t)(256u - (address & 0xFFu));
        uint16_t count = length < room ? (uint16_t)length : room;
        uint8_t page[256];
        memcpy(page, data, count);
        if (!inktag_tlsr_flash_write_page(address, count, page,
                INKTAG_POWER_OPERATION_FLASH)) return false;
        /* SDK IO has no return code. This detects observed write mismatches,
         * not every possible electrical read failure. */
        flash_read_page(address, count, page);
        if (memcmp(page, data, count) != 0) return false;
        address += count;
        data += count;
        length -= count;
    }
    return true;
}

static bool image_flash_read(uint32_t address, void *data, uint16_t length) {
    if (!data || !inktag_tlsr_image_flash_span(address, length,
                                               s_high_image_extent)) return false;
    flash_read_page(address, length, data);
    return true;
}

static inktag_image_store_result_t image_flash_mutate(
    uint32_t address, const void *data, uint16_t length) {
    bool erase = data == NULL;
    if (!inktag_tlsr_image_flash_span(address,
            erase ? INKTAG_IMAGE_STORE_SECTOR_SIZE : length,
            s_high_image_extent) ||
        (erase && (length || address % INKTAG_IMAGE_STORE_SECTOR_SIZE)))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH))
        return INKTAG_IMAGE_STORE_LOW_POWER;
    bool ok = erase ?
        inktag_tlsr_flash_erase_sector(address, INKTAG_POWER_OPERATION_FLASH) &&
            bytes_are_erased(address, INKTAG_IMAGE_STORE_SECTOR_SIZE) :
        flash_write_bytes(address, data, length);
    if (ok) return INKTAG_IMAGE_STORE_OK;
    return inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH) ?
        INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_LOW_POWER;
}

static bool image_config_read(epd_screen_config_t *screen) {
    return inktag_settings_screen_valid() && epd_screen_config_decode(inktag_settings()->screen_config,
        EPD_SCREEN_WIRE_SIZE, screen) == EPD_SCREEN_STATUS_OK;
}

#define INKTAG_FIXED_STORE_SECTORS \
    (INKTAG_TLSR_IMAGE_PRIMARY_SECTORS + INKTAG_TLSR_IMAGE_HIGH_SECTORS)
#define INKTAG_FIXED_STORE_RUNTIME_SECTORS \
    (INKTAG_TLSR_IMAGE_PRIMARY_SECTORS + \
        (s_high_image_extent ? INKTAG_TLSR_IMAGE_HIGH_SECTORS : 0u))
#define INKTAG_FIXED_STORE_CAPACITY(step) \
    inktag_tlsr_image_flash_capacity(step, s_high_image_extent)
#define INKTAG_FIXED_STORE_ADDRESS(id, step) \
    inktag_tlsr_image_flash_address(id, step, s_high_image_extent)
#define INKTAG_FIXED_STORE_READ image_flash_read
#define INKTAG_FIXED_STORE_MUTATE image_flash_mutate
#define INKTAG_FIXED_STORE_CONFIG_READ image_config_read
#define INKTAG_FIXED_STORE_POWER_ALLOWED() \
    inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH)
#define INKTAG_FIXED_STORE_RAM RAM
#define inktag_image_store_begin inktag_fixed_store_begin
#define inktag_image_store_commit inktag_fixed_store_commit
#define inktag_image_store_abort inktag_fixed_store_abort
#define inktag_image_store_delete inktag_fixed_store_delete
#define inktag_image_store_clear_incompatible inktag_fixed_store_clear_incompatible
#define inktag_image_store_clear_all inktag_fixed_store_clear_all
#include "inktag_image_fixed_store.inc"
#undef inktag_image_store_begin
#undef inktag_image_store_commit
#undef inktag_image_store_abort
#undef inktag_image_store_delete
#undef inktag_image_store_clear_incompatible
#undef inktag_image_store_clear_all

void inktag_storage_init(inktag_board_id_t board) {
    const inktag_tlsr_board_profile_t *profile = inktag_tlsr_board_profile(board);
    s_high_image_extent = profile && profile->high_image_extent &&
        inktag_tlsr_board_high_image_extent_allowed() &&
        profile->physical_flash_bytes >= INKTAG_IMAGE_STORE_NEBULAR_HIGH_START +
                                         INKTAG_IMAGE_STORE_NEBULAR_HIGH_SIZE;
    inktag_settings_init(board);
    (void)inktag_image_store_init();
}

/* Bounded event adapter: each common erase/head step yields to the existing
 * BLE/status/cancel service. No second store state machine or queued SDK IO. */
static inktag_image_store_result_t image_wait_event(
    inktag_image_store_event_type_t expected, uint8_t *id, uint16_t *removed) {
    inktag_image_store_event_t result;
    do {
        inktag_image_store_process();
        if (inktag_image_store_take_event(&result)) {
            if (result.type != expected) return INKTAG_IMAGE_STORE_BUSY;
            if (id) *id = result.id;
            if (removed) *removed = result.removed;
            return result.result;
        }
        inktag_gui_cooperative_yield();
    } while (inktag_image_store_busy());
    return INKTAG_IMAGE_STORE_BUSY;
}

inktag_image_store_result_t inktag_image_store_begin(
    const epd_screen_config_t *screen, uint8_t format, uint8_t segments,
    uint8_t black, uint8_t type, const uint32_t sizes[2], uint32_t payload,
    uint32_t checksum, uint32_t created, const char *name,
    uint8_t replace_id, uint8_t *id) {
    if (replace_id != UINT8_MAX && !inktag_image_store_replace_slot(replace_id))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    inktag_image_store_result_t result = inktag_fixed_store_begin(screen,
        format, segments, black, type, sizes, payload, checksum, created, name, id);
    return result == INKTAG_IMAGE_STORE_OK ?
        image_wait_event(INKTAG_IMAGE_STORE_EVENT_BEGIN, id, NULL) : result;
}

inktag_image_store_result_t inktag_image_store_commit(uint8_t *id) {
    if (!id) return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    inktag_image_store_result_t result = inktag_fixed_store_commit();
    /* Failed readback can also emit an event; drain it before another request. */
    if (result != INKTAG_IMAGE_STORE_OK) {
        inktag_image_store_event_t discarded;
        (void)inktag_image_store_take_event(&discarded);
        return result;
    }
    return image_wait_event(INKTAG_IMAGE_STORE_EVENT_COMMIT, id, NULL);
}

void inktag_image_store_abort(void) {
    inktag_image_store_event_t discarded;
    inktag_fixed_store_abort();
    (void)inktag_image_store_take_event(&discarded);
}

inktag_image_store_result_t inktag_image_store_delete(uint8_t id) {
    inktag_image_store_result_t result = inktag_fixed_store_delete(id);
    return result == INKTAG_IMAGE_STORE_OK ?
        image_wait_event(INKTAG_IMAGE_STORE_EVENT_DELETE, NULL, NULL) : result;
}

inktag_image_store_result_t inktag_image_store_clear_incompatible(
    const epd_screen_config_t *screen, uint16_t *removed) {
    if (removed) *removed = 0u;
    inktag_image_store_result_t result = inktag_fixed_store_clear_incompatible(screen);
    return result == INKTAG_IMAGE_STORE_OK ?
        image_wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE, NULL, removed) : result;
}

inktag_image_store_result_t inktag_image_store_clear_all(uint16_t *removed) {
    if (removed) *removed = 0u;
    inktag_image_store_result_t result = inktag_fixed_store_clear_all();
    return result == INKTAG_IMAGE_STORE_OK ?
        image_wait_event(INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, NULL, removed) : result;
}

/* Occupied ordinal is only a InkTag visibility rank, never a wire-ID remap. */
bool inktag_image_store_record_ordinal(uint8_t id, uint16_t *ordinal) {
    uint8_t rank;
    if (inktag_image_store_ordinal(id, &rank) != INKTAG_IMAGE_STORE_OK) return false;
    if (ordinal) *ordinal = rank;
    return true;
}


#if INKTAG_BUILD_PRO
#include "inktag_storage_nfc_name_pro.inc"
#endif
