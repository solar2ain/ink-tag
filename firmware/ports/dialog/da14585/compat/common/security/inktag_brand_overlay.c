/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
#include "inktag_brand_overlay.h"

#include <stddef.h>

static const uint8_t s_word[][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, /* G */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, /* I */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}, /* N */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
};

static int8_t glyph_index(char character) {
    static const char alphabet[] =
        "AGIKNT";
    uint8_t index;
    for (index = 0u; alphabet[index] != '\0'; ++index) {
        if (alphabet[index] == character) return (int8_t)index;
    }
    return -1;
}

static bool word_pixel(const char *text, uint8_t scale,
                       uint16_t x, uint16_t y) {
    uint16_t cursor = 0u;
    uint8_t index;
    if (y >= 7u * scale) return false;
    for (index = 0u; text[index] != '\0'; ++index) {
        uint16_t advance = (uint16_t)(5u * scale + 1u);
        if (
            x >= cursor &&
            x < cursor + 5u * scale) {
            int8_t glyph = glyph_index(text[index]);
            uint8_t column = (uint8_t)((x - cursor) / scale);
            uint8_t row = (uint8_t)(y / scale);
            return glyph >= 0 &&
                   (s_word[(uint8_t)glyph][row] &
                    (uint8_t)(1u << (4u - column))) != 0u;
        }
        cursor = (uint16_t)(cursor + advance);
        if (x < cursor) return false;
    }
    return false;
}

static bool badge_pixel_ink(uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height,
                            uint8_t scale, const char *text) {
    uint8_t padding = (uint8_t)(scale + 1u);
    bool border =
        ((y == 0u || y + 1u == height) && x >= 2u && x + 2u < width) ||
        ((x == 0u || x + 1u == width) && y >= 2u && y + 2u < height) ||
        ((x == 1u || x + 2u == width) &&
         (y == 1u || y + 2u == height));
    if (border) return true;
    if (x < padding || y < padding ||
        x >= width - padding || y >= height - padding) return false;
    return word_pixel(text, scale, (uint16_t)(x - padding),
                      (uint16_t)(y - padding));
}

/* These coordinate/packing transforms serve both the chunk reject and
 * pixel loop. DA retains one copy; all formats and transforms stay present. */
#if defined(INKTAG_DIALOG) && defined(__clang__)
__attribute__((noinline))
#endif
static void logical_to_physical(const epd_screen_config_t *screen,
                                uint8_t rotation,
                                uint16_t logical_width,
                                uint16_t logical_height,
                                uint16_t x, uint16_t y,
                                uint16_t *physical_x,
                                uint16_t *physical_y) {
    uint16_t tx = x;
    uint16_t ty = y;
    if (rotation == EPD_SCREEN_TRANSFORM_ROTATE_90) {
        tx = (uint16_t)(logical_height - 1u - y);
        ty = x;
    } else if (rotation == EPD_SCREEN_TRANSFORM_ROTATE_180) {
        tx = (uint16_t)(logical_width - 1u - x);
        ty = (uint16_t)(logical_height - 1u - y);
    } else if (rotation == EPD_SCREEN_TRANSFORM_ROTATE_270) {
        tx = y;
        ty = (uint16_t)(logical_width - 1u - x);
    }
    if ((screen->transform & EPD_SCREEN_TRANSFORM_MIRROR_X) != 0u)
        tx = (uint16_t)(screen->visible_width - 1u - tx);
    if ((screen->transform & EPD_SCREEN_TRANSFORM_MIRROR_Y) != 0u)
        ty = (uint16_t)(screen->visible_height - 1u - ty);
    *physical_x = (uint16_t)(screen->visible_x + tx);
    *physical_y = (uint16_t)(screen->visible_y + ty);
}

/* The caller validates the packing once and constrains the complete logical
 * badge to the visible rectangle before rotation/mirroring. Every mapped
 * pixel is therefore inside physical RAM, including visible offsets. */
/* These coordinate/packing transforms serve both the chunk reject and
 * pixel loop. DA retains one copy; all formats and transforms stay present. */
#if defined(INKTAG_DIALOG) && defined(__clang__)
__attribute__((noinline))
#endif
static void packed_location(const epd_screen_config_t *screen,
                            uint8_t transfer_format,
                            uint16_t x, uint16_t y,
                            uint32_t *byte_offset, uint8_t *shift,
                            uint8_t *mask) {
    uint32_t pixel;
    if (transfer_format == EPD_SCREEN_TRANSFER_MONO_1BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP) {
        uint16_t row_bytes = (uint16_t)((screen->width + 7u) / 8u);
        *byte_offset = (uint32_t)y * row_bytes + x / 8u;
        *shift = (uint8_t)(7u - (x & 7u));
        *mask = 1u;
        return;
    }
    pixel = (uint32_t)y * screen->width + x;
    if (transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
        transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
        *byte_offset = pixel / 4u;
        *shift = (uint8_t)(6u - (pixel & 3u) * 2u);
        *mask = 3u;
        return;
    }
    /* packed_value() admitted the only remaining packing: indexed 4 bpp. */
    *byte_offset = pixel / 2u;
    *shift = (uint8_t)((pixel & 1u) != 0u ? 0u : 4u);
    *mask = 15u;
}

typedef enum {
    BADGE_PIXEL_WHITE = 0,
    BADGE_PIXEL_BLACK = 1,
    BADGE_PIXEL_RED = 2,
} badge_pixel_t;

static bool screen_has_red(const epd_screen_config_t *screen) {
    uint8_t color = screen->color_id;
    /* Registered red-capable ids are the pairs 2/3 and 10/11. */
    return color <= EPD_SCREEN_COLOR_BWRYGBO && (color & 0xf6u) == 2u;
}

static bool packed_value(uint8_t transfer_format, uint8_t segment_index,
                         badge_pixel_t pixel, uint8_t *value) {
    static const uint8_t values[6u * 2u * 3u] = {
        1u, 0u, 0u, 0xffu, 0xffu, 0xffu, /* mono */
        1u, 0u, 1u, 1u, 1u, 0u,          /* dual planes */
        1u, 0u, 3u, 0xffu, 0xffu, 0xffu, /* indexed 2 bpp */
        0u, 1u, 1u, 0u, 1u, 1u,          /* planar 2 x 1 bpp */
        3u, 0u, 4u, 0xffu, 0xffu, 0xffu, /* indexed 4 bpp */
        1u, 1u, 3u, 1u, 0u, 1u,          /* indexed 2 x 2 bpp */
    };
    uint8_t encoded;
    if (transfer_format < EPD_SCREEN_TRANSFER_MONO_1BPP ||
        transfer_format > EPD_SCREEN_TRANSFER_INDEXED_2X2BPP ||
        segment_index > 1u || pixel > BADGE_PIXEL_RED) return false;
    encoded = values[((transfer_format - 1u) * 2u + segment_index) * 3u + pixel];
    if (encoded == 0xffu) return false;
    *value = encoded;
    return true;
}

bool inktag_brand_overlay_apply_oriented(
    const epd_screen_config_t *screen,
    uint8_t transfer_format,
    uint8_t segment_index,
    uint32_t segment_offset,
    uint8_t *data,
    uint16_t length,
    inktag_brand_edition_t edition, uint8_t reading_rotation) {
    static const char free_text[] = "INKTAG";
    const char *text = free_text;
    uint8_t rotation;
    uint16_t logical_width;
    uint16_t logical_height;
    uint8_t scale;
    uint16_t badge_width;
    uint16_t badge_height;
    uint16_t badge_x;
    uint16_t badge_y;
    uint8_t corner;
    uint16_t x;
    uint16_t y;
    badge_pixel_t ink_pixel;
    uint8_t white_value;
    uint8_t ink_value;

    if (reading_rotation > 3u) return false;
    if (screen == NULL || data == NULL || length == 0u ||
        screen->width == 0u || screen->height == 0u ||
        screen->visible_width == 0u || screen->visible_height == 0u ||
        (uint32_t)screen->visible_x + screen->visible_width > screen->width ||
        (uint32_t)screen->visible_y + screen->visible_height > screen->height ||
        !packed_value(transfer_format, segment_index, BADGE_PIXEL_WHITE,
                      &white_value)) {
        return false;
    }
    (void)edition;
    ink_pixel = screen_has_red(screen) ? BADGE_PIXEL_RED : BADGE_PIXEL_BLACK;
    if (!packed_value(transfer_format, segment_index, ink_pixel, &ink_value))
        return false;
    /* Osprey E6 is not the generic seven-colour palette: 1=white, 3=red,
     * and 4 is invalid. Resolve once per chunk, never in the pixel loop. */
    rotation = (uint8_t)((screen->transform + reading_rotation) &
                         EPD_SCREEN_TRANSFORM_ROTATION_MASK);
    logical_width = (rotation == EPD_SCREEN_TRANSFORM_ROTATE_90 ||
                     rotation == EPD_SCREEN_TRANSFORM_ROTATE_270) ?
                        screen->visible_height : screen->visible_width;
    logical_height = (rotation == EPD_SCREEN_TRANSFORM_ROTATE_90 ||
                      rotation == EPD_SCREEN_TRANSFORM_ROTATE_270) ?
                         screen->visible_width : screen->visible_height;
    /* Match inktag_gui_default.inc exactly: compact/normal calendar footers use the
     * native badge, while only >500 px layouts use the doubled badge. */
    scale = logical_width > 500u ? 2u : 1u;
    badge_width = (uint16_t)(32u * scale + 7u);
    badge_height = (uint16_t)(9u * scale + 2u);
    if (logical_width < 300u && logical_height < 300u) {
        if (logical_width < badge_width + 3u || logical_height < 18u)
            return false;
        badge_x = (uint16_t)(logical_width - badge_width - 3u);
        /* Match the 200px native footer's 23px band, not the 18px default. */
        badge_y = (uint16_t)(logical_height -
            (logical_width == 200u && logical_height == 200u ? 17u : 14u));
    } else {
        uint32_t width_scale = (uint32_t)logical_width * 3u;
        uint32_t height_scale = (uint32_t)logical_height * 4u;
        uint16_t margin = (uint16_t)(
            ((width_scale < height_scale ? width_scale : height_scale) + 60u) /
            120u);
        if (margin < 10u) margin = 10u;
        if (margin > 20u) margin = 20u;
        if (logical_width < badge_width + margin ||
            logical_height < badge_height + 5u) return false;
        badge_x = (uint16_t)(logical_width - badge_width - margin);
        badge_y = (uint16_t)(logical_height - badge_height - 5u);
    }

    /* Most transfer chunks cannot overlap the fixed badge. Reject them before
     * walking every badge pixel: on large multi-pass panels the old loop ran
     * thousands of coordinate transforms for each 237-byte BLE chunk. An
     * orthogonal rotation/mirror maps the logical badge rectangle to another
     * physical rectangle, so its four packed corner offsets bound every byte
     * that the overlay can touch. */
    {
        uint32_t min_byte_offset = UINT32_MAX;
        uint32_t max_byte_offset = 0u;
        for (corner = 0u; corner < 4u; ++corner) {
            uint16_t physical_x;
            uint16_t physical_y;
            uint32_t byte_offset;
            uint8_t shift;
            uint8_t mask;
            logical_to_physical(
                screen, rotation, logical_width, logical_height,
                (uint16_t)(badge_x + ((corner & 1u) != 0u ? badge_width - 1u : 0u)),
                (uint16_t)(badge_y + ((corner & 2u) != 0u ? badge_height - 1u : 0u)),
                &physical_x, &physical_y);
            packed_location(screen, transfer_format, physical_x,
                            physical_y, &byte_offset, &shift, &mask);
            if (byte_offset < min_byte_offset) min_byte_offset = byte_offset;
            if (byte_offset > max_byte_offset) max_byte_offset = byte_offset;
        }
        if (segment_offset + length <= min_byte_offset ||
            segment_offset > max_byte_offset) {
            return true;
        }
    }

    for (y = 0u; y < badge_height; ++y) {
        for (x = 0u; x < badge_width; ++x) {
            uint16_t physical_x;
            uint16_t physical_y;
            uint32_t byte_offset;
            uint8_t shift;
            uint8_t mask;
            bool ink = badge_pixel_ink(x, y, badge_width, badge_height,
                                       scale, text);
            logical_to_physical(screen, rotation, logical_width, logical_height,
                                (uint16_t)(badge_x + x),
                                (uint16_t)(badge_y + y),
                                &physical_x, &physical_y);
            packed_location(screen, transfer_format, physical_x,
                            physical_y, &byte_offset, &shift, &mask);
            if (byte_offset < segment_offset ||
                byte_offset >= segment_offset + length) {
                continue;
            }
            data[byte_offset - segment_offset] = (uint8_t)(
                (data[byte_offset - segment_offset] &
                 (uint8_t)~(uint8_t)(mask << shift)) |
                (uint8_t)((ink ? ink_value : white_value) << shift));
        }
    }
    return true;
}

bool inktag_brand_overlay_apply(
    const epd_screen_config_t *screen, uint8_t format, uint8_t segment,
    uint32_t offset, uint8_t *data, uint16_t length,
    inktag_brand_edition_t edition) {
    return inktag_brand_overlay_apply_oriented(
        screen, format, segment, offset, data, length, edition, 0u);
}
