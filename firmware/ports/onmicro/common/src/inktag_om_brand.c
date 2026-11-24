#include "inktag_om_brand.h"

#include "inktag_access.h"
#include "inktag_brand_overlay.h"

bool inktag_om_brand_apply_oriented(
    const epd_screen_config_t *screen,
    const inktag_activation_t *activation,
    bool hidden_requested,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length,
    uint8_t reading_rotation) {
    (void)activation; (void)hidden_requested;
    return inktag_brand_overlay_apply_oriented(
        screen, transfer_format, segment_index, segment_offset,
        data, length, INKTAG_BRAND_EDITION_DEFAULT, reading_rotation);
}

bool inktag_om_brand_apply(
    const epd_screen_config_t *screen,
    const inktag_activation_t *activation,
    bool hidden_requested,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length) {
    return inktag_om_brand_apply_oriented(
        screen, activation, hidden_requested, transfer_format, segment_index,
        segment_offset, data, length, 0u);
}
