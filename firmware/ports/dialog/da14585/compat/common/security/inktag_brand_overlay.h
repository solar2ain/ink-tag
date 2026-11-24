#ifndef INKTAG_BRAND_OVERLAY_H
#define INKTAG_BRAND_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_epd_screen.h"

typedef enum {
    INKTAG_BRAND_EDITION_FREE = 0,
    INKTAG_BRAND_EDITION_PRO = 1,
    /* Reserved ABI values; this Free implementation always draws its mark. */
    INKTAG_BRAND_EDITION_HIDDEN = 2,
} inktag_brand_edition_t;

/* Overlay the device-enforced edition badge on one device-ready image chunk.
 * Its geometry is the canonical V1 calendar footer badge at the same logical
 * bottom-right position. Free uses red on red-capable panels (black is the
 * physical fallback). Reserved edition arguments never suppress the Free mark.
 * segment_offset is relative to the start of segment_index, not to the whole
 * transfer. The original record remains untouched; callers apply this only to
 * the final bytes sent to the panel controller. Returns false for invalid or
 * unsupported packing metadata and leaves data unchanged in that case. */
bool inktag_brand_overlay_apply(
    const epd_screen_config_t *screen,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length,
    inktag_brand_edition_t edition);

/* reading_rotation = CW quarter turns from upright photo to logical screen.
 * Composition precedes profile rotation/mirrors. Does not alter that profile,
 * stored pixels, CRC, access checks, or the fixed Free badge placement rule. */
bool inktag_brand_overlay_apply_oriented(
    const epd_screen_config_t *screen, uint8_t transfer_format,
    uint8_t segment_index, uint32_t segment_offset, uint8_t *data,
    uint16_t length, inktag_brand_edition_t edition, uint8_t reading_rotation);

#endif
