#ifndef INKTAG_OM_BRAND_H
#define INKTAG_OM_BRAND_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_epd_screen.h"
#include "inktag_activation.h"

/* Apply the shared product-edition badge to one final writable EPD chunk.
 * The OnMicro layer only supplies the verified activation state; packing,
 * placement, transform and color policy remain owned by firmware/common. */
bool inktag_om_brand_apply(
    const epd_screen_config_t *screen,
    const inktag_activation_t *activation,
    bool hidden_requested,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length);

/* Image reading orientation is metadata, not a panel-wiring transform. */
bool inktag_om_brand_apply_oriented(
    const epd_screen_config_t *screen,
    const inktag_activation_t *activation,
    bool hidden_requested,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length,
    uint8_t reading_rotation);

#endif
