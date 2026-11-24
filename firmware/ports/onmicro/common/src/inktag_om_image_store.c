#include "inktag_om_image_store.h"
#include <stddef.h>
#include "om.h"
#include "om_driver.h"
#include "inktag_om_external_flash.h"
#include "inktag_om_flash_layout.h"
#include "inktag_om_health.h"
#include "inktag_epd_config.h"

#define IMAGE_STORE_PRIMARY_START INKTAG_OM_IMAGE_STORE_PRIMARY_START
#define IMAGE_STORE_PRIMARY_SIZE INKTAG_OM_IMAGE_STORE_PRIMARY_SIZE
#define IMAGE_STORE_MAIN_SIZE IMAGE_STORE_PRIMARY_SIZE
#define IMAGE_STORE_MAIN_SECTORS (IMAGE_STORE_MAIN_SIZE / INKTAG_IMAGE_STORE_SECTOR_SIZE)
#define OM_FLASH_REQUIRED_SIZE 0x00100000u
#if defined(__GNUC__)
#define OM_IMAGE_STORE_RAM_CODE __attribute__((section(".ram_code"), noinline))
#else
#define OM_IMAGE_STORE_RAM_CODE
#endif

_Static_assert(IMAGE_STORE_PRIMARY_START ==
#if defined(INKTAG_OM_NEXT_LAYOUT)
               0x00084000u &&
#else
               0x00082000u &&
#endif
#if defined(INKTAG_OM_NEXT_LAYOUT)
               IMAGE_STORE_PRIMARY_START + IMAGE_STORE_PRIMARY_SIZE == 0x000FD000u,
               "ZKC reclaimed store must preserve NVDS and one CONF sector");
#else
               IMAGE_STORE_PRIMARY_START + IMAGE_STORE_PRIMARY_SIZE == 0x000FC000u,
               "qualified internal image extent must not move");
#endif
#include "inktag_om_image_io.inc"

/* NULL means one sector erase. Every physical mutation resamples power;
 * the common engine owns erase progress and magic-last publication. */
static inktag_image_store_result_t store_mutate(
    uint32_t offset, const void *data, uint32_t length) {
    image_store_span_t span;
    uint32_t bytes = data ? length : INKTAG_IMAGE_STORE_SECTOR_SIZE;
    if ((!data && (length || offset % INKTAG_IMAGE_STORE_SECTOR_SIZE)) ||
        !store_resolve_span(offset, bytes, &span))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_IMAGE_STORE_LOW_POWER;
    bool ok = data ? store_write(offset, data, length) : span.external ?
        inktag_om_external_flash_erase(span.address, bytes) :
        flash_erase_sector(span.address);
    inktag_om_health_watchdog_feed();
    if (ok) return INKTAG_IMAGE_STORE_OK;
    return inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false) ?
        INKTAG_IMAGE_STORE_IO_ERROR : INKTAG_IMAGE_STORE_LOW_POWER;
}

static bool store_config_read(epd_screen_config_t *config) {
    return drv_sfs_capacity() >= OM_FLASH_REQUIRED_SIZE &&
           epd_screen_config_read(config);
}

/* One shared App has a second physical extent only on qualified 42B hardware.
 * The 634-sector bound sizes RAM; actual count follows the selected layout
 * and the exact-board-qualified auxiliary capacity, never this upper bound. */
#define INKTAG_FIXED_STORE_SECTORS 634u
#define INKTAG_FIXED_STORE_RUNTIME_SECTORS store_sector_count()
#define INKTAG_FIXED_STORE_CAPACITY(step) store_fixed_capacity(step)
#define INKTAG_FIXED_STORE_ADDRESS(id, step) store_fixed_offset(id, step)
#define INKTAG_FIXED_STORE_READ(offset, data, length) store_read(offset, data, length)
#define INKTAG_FIXED_STORE_MUTATE(offset, data, length) store_mutate(offset, data, length)
#define INKTAG_FIXED_STORE_POWER_ALLOWED() \
    inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)
#define INKTAG_FIXED_STORE_CONFIG_READ(config) store_config_read(config)
#include "inktag_image_fixed_store.inc"
