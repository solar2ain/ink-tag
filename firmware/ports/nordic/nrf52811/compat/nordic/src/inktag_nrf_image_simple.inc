/* Pilot based on original physical sector IDs. No index, generations or COW.
 * Replacing a target may lose it on interruption; all other extents stay intact.
 */
#include "inktag_resource_name.h"

bool inktag_image_store_replace_slot(uint8_t id) {
    if (!s_initialized || inktag_image_physical_busy() || id >= IMAGE_STORE_SECTORS)
        return false;
    s_simple_target = id;
    return true;
}

static inktag_image_store_result_t simple_select_target(
    uint16_t required, const char *name, uint8_t *id) {
    uint8_t target = s_simple_target;
    s_simple_target = UINT8_MAX;
    size_t length = name ? strlen(name) : 0u;
    if (length >= INKTAG_IMAGE_STORE_NAME_SIZE ||
        (length && !inktag_resource_name_valid((const uint8_t *)name, length)))
        return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (target != UINT8_MAX && target + required > IMAGE_STORE_SECTORS)
        return INKTAG_IMAGE_STORE_NO_SPACE;
    for (uint16_t sector = 0u; sector < IMAGE_STORE_SECTORS;) {
        inktag_image_store_header_t header;
        if (!read_valid_header(sector, &header)) {
            if (s_header_read_error) return INKTAG_IMAGE_STORE_IO_ERROR;
            sector++;
            continue;
        }
        if (sector != target && length &&
            inktag_resource_name_matches((const uint8_t *)name, length,
                (const uint8_t *)header.name, strlen(header.name)))
            return INKTAG_IMAGE_STORE_BAD_ARGUMENT;
        if (target != UINT8_MAX && sector != target &&
            sector < target + required && sector + header.record_sectors > target)
            return INKTAG_IMAGE_STORE_NO_SPACE;
        sector += header.record_sectors;
    }
    if (target == UINT8_MAX) return find_free_run(required, id);
    *id = target;
    return INKTAG_IMAGE_STORE_OK;
}
