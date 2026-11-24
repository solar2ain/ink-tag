#include "inktag_image_quota.h"

uint16_t inktag_access_default_image_record_limit(
    uint16_t visible_width,
    uint16_t visible_height) {
    (void)visible_width;
    (void)visible_height;
    /* Compatibility sentinel, not a buffer length or a physical allocation. */
    return UINT16_MAX;
}
