#include "inktag_gr_image_io.h"

#include <stdbool.h>
#include <string.h>
#if defined(INKTAG_GR_GUI_ASSET_HOST_TEST)
#include <gr_gui_asset_test_platform.h>
#else
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx.h"
#else
#include "gr533x.h"
#endif
#include "hal_flash.h"
#include "inktag_gr_health.h"
#endif
#include "inktag_gr_board_identity.h"
#include "inktag_gr_cooperative.h"
#include "inktag_gr_flash_layout.h"
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
#include "inktag_gr_external_flash.h"
#endif

/* Physical bus facts only; no image index, names or persistent metadata. */
static inktag_gr_image_io_extent_t s_extents[2];
static uint8_t s_count;
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
static bool s_auxiliary;
#endif

inktag_gr_image_io_result_t inktag_gr_image_io_open(void) {
    s_count = 0;
    memset(s_extents, 0, sizeof(s_extents));
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    s_auxiliary = false;
#endif
#if !defined(INKTAG_GOODIX_OTA)
    return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
#elif defined(INKTAG_GOODIX_GR5331)
    if (inktag_gr_board_identity() != INKTAG_GR_BOARD_ZKC21V)
        return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
    s_extents[0] = (inktag_gr_image_io_extent_t){
        INKTAG_GR_IMAGE_STORE_START, INKTAG_GR_IMAGE_STORE_SIZE};
    s_count = 1;
#elif defined(INKTAG_GOODIX_GR5515)
    uint8_t board = inktag_gr_board_identity();
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (board != INKTAG_GR_BOARD_ZKC21V)
        return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
#else
    if (board != INKTAG_GR_BOARD_ZKC21V &&
        board != INKTAG_GR_BOARD_ZKC42V && board != INKTAG_GR_BOARD_ZKC42VM)
        return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
#endif
    uint32_t id = 0, capacity = 0;
    hal_flash_get_info(&id, &capacity);
    uint8_t code = (uint8_t)id;
    if (code >= 0x10u && code < 0x20u && (1u << code) < capacity)
        capacity = 1u << code; /* Reject mirrored upper address windows. */
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (capacity != 0x80000u) return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
#else
    if (capacity < 0x80000u) return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
#endif
    s_extents[0] = (inktag_gr_image_io_extent_t){
        INKTAG_GR_IMAGE_STORE_START, INKTAG_GR_IMAGE_STORE_SIZE};
#if !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (capacity < 0x100000u) {
        /* v6 has no package-Flash image fallback. Both runtime selectors
         * require the exact fitted NOR to match before exposing slots. */
        if (!inktag_gr_external_flash_available()) {
            memset(s_extents, 0, sizeof(s_extents));
            return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
        }
        s_auxiliary = true;
    }
#endif
    s_count = 1;
#else
#error "GR image IO needs an exact supported target"
#endif
    return INKTAG_GR_IMAGE_IO_OK;
}

uint8_t inktag_gr_image_io_extents(inktag_gr_image_io_extent_t out[2]) {
    if (out) memcpy(out, s_extents, sizeof(s_extents));
    return s_count;
}

bool inktag_gr_image_io_power_allowed(void) {
    return inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true);
}

uint16_t inktag_gr_image_io_sector_count(void) {
    uint32_t total = 0;
    for (uint8_t i = 0; i < s_count; ++i)
        total += s_extents[i].bytes / INKTAG_GR_FLASH_SECTOR_SIZE;
    return (uint16_t)total;
}

uint16_t inktag_gr_image_io_slot_count(uint16_t stride) {
    if (!stride) return 0;
    uint16_t total = 0;
    for (uint8_t i = 0; i < s_count; ++i)
        total += s_extents[i].bytes / INKTAG_GR_FLASH_SECTOR_SIZE / stride;
    return total;
}

uint32_t inktag_gr_image_io_slot_address(uint8_t id, uint16_t stride) {
    if (!stride) return UINT32_MAX;
    for (uint8_t i = 0; i < s_count; ++i) {
        uint16_t count = s_extents[i].bytes / INKTAG_GR_FLASH_SECTOR_SIZE / stride;
        if (id < count) return s_extents[i].start +
            (uint32_t)id * stride * INKTAG_GR_FLASH_SECTOR_SIZE;
        id -= count;
    }
    return UINT32_MAX;
}

inktag_gr_image_io_result_t inktag_gr_image_io_span(uint32_t address,
                                                 uint32_t length) {
    if (!s_count) return INKTAG_GR_IMAGE_IO_UNAVAILABLE;
    if (!length) return INKTAG_GR_IMAGE_IO_INVALID;
    for (uint8_t i = 0; i < s_count; ++i) {
        const inktag_gr_image_io_extent_t *extent = &s_extents[i];
        if (address < extent->start) continue;
        uint32_t offset = address - extent->start;
        if (offset >= extent->bytes || length > extent->bytes - offset) continue;
        return INKTAG_GR_IMAGE_IO_OK;
    }
    /* Never split one operation across physical chips or a protected tail. */
    return INKTAG_GR_IMAGE_IO_INVALID;
}

#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
static bool auxiliary(uint32_t address) {
    return s_auxiliary && s_count == 1 && address >= s_extents[0].start;
}
#endif

inktag_gr_image_io_result_t inktag_gr_image_io_read(uint32_t address,
    uint8_t *data, uint16_t length) {
    if (!data) return INKTAG_GR_IMAGE_IO_INVALID;
    inktag_gr_image_io_result_t result = inktag_gr_image_io_span(address, length);
    if (result != INKTAG_GR_IMAGE_IO_OK) return result;
    uint32_t read;
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (auxiliary(address)) read = inktag_gr_external_flash_read(
        address - INKTAG_GR5513_EXTERNAL_FLASH_BOUNDARY, data, length);
    else
#endif
        read = hal_flash_read(address, data, length);
    inktag_gr_cooperative_maintenance();
    return read == length ? INKTAG_GR_IMAGE_IO_OK : INKTAG_GR_IMAGE_IO_ERROR;
}

static uint32_t mutation_begin(bool *secure) {
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    *secure = hal_flash_get_security();
    if (*secure) hal_flash_set_security(false);
    return mask;
}
static void mutation_end(bool secure, uint32_t mask) {
    if (secure) hal_flash_set_security(true);
    if (!(mask & 1u)) __enable_irq();
}

inktag_gr_image_io_result_t inktag_gr_image_io_program(uint32_t address,
    const uint8_t *data, uint16_t length) {
    if (!data) return INKTAG_GR_IMAGE_IO_INVALID;
    inktag_gr_image_io_result_t result = inktag_gr_image_io_span(address, length);
    if (result != INKTAG_GR_IMAGE_IO_OK) return result;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_GR_IMAGE_IO_LOW_POWER;
    uint32_t written;
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (auxiliary(address)) written = inktag_gr_external_flash_write(
        address - INKTAG_GR5513_EXTERNAL_FLASH_BOUNDARY, data, length);
    else
#endif
    {
        bool secure;
        uint32_t mask = mutation_begin(&secure);
        written = hal_flash_write(address, data, length);
        mutation_end(secure, mask);
    }
    inktag_gr_cooperative_maintenance();
    return written == length ? INKTAG_GR_IMAGE_IO_OK : INKTAG_GR_IMAGE_IO_ERROR;
}

inktag_gr_image_io_result_t inktag_gr_image_io_erase(uint32_t address) {
    if (address & (INKTAG_GR_FLASH_SECTOR_SIZE - 1u))
        return INKTAG_GR_IMAGE_IO_INVALID;
    inktag_gr_image_io_result_t result = inktag_gr_image_io_span(
        address, INKTAG_GR_FLASH_SECTOR_SIZE);
    if (result != INKTAG_GR_IMAGE_IO_OK) return result;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_GR_IMAGE_IO_LOW_POWER;
    bool erased;
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if (auxiliary(address)) erased = inktag_gr_external_flash_erase(
        address - INKTAG_GR5513_EXTERNAL_FLASH_BOUNDARY, INKTAG_GR_FLASH_SECTOR_SIZE);
    else
#endif
    {
        bool secure;
        uint32_t mask = mutation_begin(&secure);
        erased = hal_flash_erase(address, INKTAG_GR_FLASH_SECTOR_SIZE);
        mutation_end(secure, mask);
    }
    inktag_gr_cooperative_maintenance();
    return erased ? INKTAG_GR_IMAGE_IO_OK : INKTAG_GR_IMAGE_IO_ERROR;
}
