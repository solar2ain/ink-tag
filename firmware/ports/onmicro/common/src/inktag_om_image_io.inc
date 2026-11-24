/* OM-only Flash/extent binding for the common fixed-slot engine.
 * No image headers, ID allocation, names, journal
 * or product transaction state belong here. Preserve the qualified RAM-code
 * interrupt discipline and auxiliary driver's per-access park/deep sleep. */
static OM_IMAGE_STORE_RAM_CODE bool flash_read(uint32_t address, void *data, uint32_t length) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_read(address, data, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static OM_IMAGE_STORE_RAM_CODE bool flash_write(uint32_t address, const void *data,
                                                 uint32_t length) {
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                             true)) {
        return false;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_unlock_all();
    drv_sfs_write(address, data, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static OM_IMAGE_STORE_RAM_CODE bool flash_erase_sector(uint32_t address) {
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                             true)) {
        return false;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_unlock_all();
    drv_sfs_erase(address, INKTAG_IMAGE_STORE_SECTOR_SIZE);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static uint32_t store_size(void) {
    return IMAGE_STORE_MAIN_SIZE + inktag_om_external_flash_storage_capacity();
}

static uint16_t store_sector_count(void) {
    return (uint16_t)(store_size() / INKTAG_IMAGE_STORE_SECTOR_SIZE);
}

/* Arithmetic fixed-slot geometry: discard each device's remainder separately.
 * Offsets use the existing main-first logical IO namespace, not XIP addresses.
 * The common engine caps wire IDs to 0..254; 255 remains the auto-select marker. */
static inline uint16_t store_fixed_capacity(uint16_t stride) {
    if (!stride) return 0u;
    uint16_t auxiliary = (uint16_t)(inktag_om_external_flash_storage_capacity() /
                                    INKTAG_IMAGE_STORE_SECTOR_SIZE);
    return (uint16_t)(IMAGE_STORE_MAIN_SECTORS / stride + auxiliary / stride);
}

static inline uint32_t store_fixed_offset(uint16_t id, uint16_t stride) {
    if (!stride || id >= 255u || id >= store_fixed_capacity(stride))
        return UINT32_MAX;
    uint16_t primary_slots = IMAGE_STORE_MAIN_SECTORS / stride;
    if (id < primary_slots)
        return (uint32_t)id * stride * INKTAG_IMAGE_STORE_SECTOR_SIZE;
    return IMAGE_STORE_MAIN_SIZE +
        (uint32_t)(id - primary_slots) * stride * INKTAG_IMAGE_STORE_SECTOR_SIZE;
}

typedef struct {
    bool external;
    uint32_t address;
} image_store_span_t;

/* Resolve exactly one physical extent. Rejecting a cross-extent byte span is
 * deliberate: a record cannot turn discontiguous ownership ranges into one
 * pretend-contiguous Flash transaction. */
static bool store_resolve_span(uint32_t logical_offset, uint32_t length,
                               image_store_span_t *span) {
    uint32_t extent_offset;
    uint32_t extent_size;
    uint32_t physical_start;
    uint32_t total_size = store_size();
    bool external = false;

    if (!span || logical_offset > total_size ||
        length > total_size - logical_offset) {
        return false;
    }
    if (logical_offset < IMAGE_STORE_PRIMARY_SIZE) {
        extent_offset = logical_offset;
        extent_size = IMAGE_STORE_PRIMARY_SIZE;
        physical_start = IMAGE_STORE_PRIMARY_START;
    } else {
        extent_offset = logical_offset - IMAGE_STORE_MAIN_SIZE;
        extent_size = inktag_om_external_flash_storage_capacity();
        physical_start = INKTAG_OM_EXTERNAL_FLASH_STORAGE_OFFSET;
        external = true;
    }
    if (extent_offset > extent_size || length > extent_size - extent_offset) {
        return false;
    }
    span->external = external;
    span->address = physical_start + extent_offset;
    return true;
}

static bool store_read(uint32_t logical_offset, void *data, uint32_t length) {
    image_store_span_t span;
    if (!store_resolve_span(logical_offset, length, &span)) return false;
    if (span.external) {
        return inktag_om_external_flash_read(span.address, data, length) == length;
    }
    return flash_read(span.address, data, length);
}

static bool store_write(uint32_t logical_offset, const void *data, uint32_t length) {
    image_store_span_t span;
    if (!store_resolve_span(logical_offset, length, &span)) return false;
    if (span.external) {
        return inktag_om_external_flash_write(span.address, data, length) == length;
    }
    return flash_write(span.address, data, length);
}
