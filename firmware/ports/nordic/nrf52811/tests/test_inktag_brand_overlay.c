#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_brand_overlay.h"

static uint32_t segment_bytes(const epd_screen_config_t *screen,
                              uint8_t format) {
    uint32_t pixels = (uint32_t)screen->width * screen->height;
    if (format == EPD_SCREEN_TRANSFER_MONO_1BPP ||
        format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
        format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP) {
        return (uint32_t)((screen->width + 7u) / 8u) * screen->height;
    }
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
        format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) return (pixels + 3u) / 4u;
    return (pixels + 1u) / 2u;
}

static uint8_t packed(const uint8_t *segment,
                      const epd_screen_config_t *screen,
                      uint8_t format, uint16_t x, uint16_t y) {
    uint32_t pixel = (uint32_t)y * screen->width + x;
    if (format == EPD_SCREEN_TRANSFER_MONO_1BPP ||
        format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
        format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP) {
        uint32_t offset = (uint32_t)y * ((screen->width + 7u) / 8u) + x / 8u;
        return (uint8_t)((segment[offset] >> (7u - (x & 7u))) & 1u);
    }
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
        format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
        return (uint8_t)((segment[pixel / 4u] >>
                          (6u - (pixel & 3u) * 2u)) & 3u);
    }
    return (uint8_t)((segment[pixel / 2u] >>
                      ((pixel & 1u) != 0u ? 0u : 4u)) & 15u);
}

static bool pixel_black(uint8_t **segments,
                        const epd_screen_config_t *screen,
                        uint8_t format, uint16_t x, uint16_t y) {
    uint8_t first = packed(segments[0], screen, format, x, y);
    if (format == EPD_SCREEN_TRANSFER_MONO_1BPP) return first == 0u;
    if (format == EPD_SCREEN_TRANSFER_DUAL_1BPP)
        return first == 0u && packed(segments[1], screen, format, x, y) == 1u;
    if (format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP)
        return first == 1u && packed(segments[1], screen, format, x, y) == 1u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
        format == EPD_SCREEN_TRANSFER_INDEXED_4BPP) return first == 0u;
    return first == 1u && packed(segments[1], screen, format, x, y) == 0u;
}

static bool pixel_white(uint8_t **segments,
                        const epd_screen_config_t *screen,
                        uint8_t format, uint16_t x, uint16_t y) {
    uint8_t first = packed(segments[0], screen, format, x, y);
    if (format == EPD_SCREEN_TRANSFER_MONO_1BPP) return first == 1u;
    if (format == EPD_SCREEN_TRANSFER_DUAL_1BPP)
        return first == 1u && packed(segments[1], screen, format, x, y) == 1u;
    if (format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP)
        return first == 0u && packed(segments[1], screen, format, x, y) == 0u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2BPP) return first == 1u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_4BPP) return first == 3u;
    return first == 1u && packed(segments[1], screen, format, x, y) == 1u;
}

static bool pixel_red(uint8_t **segments,
                      const epd_screen_config_t *screen,
                      uint8_t format, uint16_t x, uint16_t y) {
    uint8_t first = packed(segments[0], screen, format, x, y);
    if (format == EPD_SCREEN_TRANSFER_DUAL_1BPP)
        return first == 1u && packed(segments[1], screen, format, x, y) == 0u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2BPP) return first == 3u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_4BPP) return first == 4u;
    if (format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP)
        return first == 3u && packed(segments[1], screen, format, x, y) == 1u;
    return false;
}

static void apply_chunked_edition(const epd_screen_config_t *screen,
                                  uint8_t format, uint8_t **segments,
                                  uint8_t count, uint32_t bytes,
                                  inktag_brand_edition_t edition) {
    uint8_t segment;
    for (segment = 0u; segment < count; ++segment) {
        uint32_t offset;
        for (offset = 0u; offset < bytes; offset += 17u) {
            uint16_t length = (uint16_t)(bytes - offset > 17u ?
                                         17u : bytes - offset);
            assert(inktag_brand_overlay_apply(
                screen, format, segment, offset, segments[segment] + offset,
                length, edition));
        }
    }
}

static void apply_chunked(const epd_screen_config_t *screen,
                          uint8_t format, uint8_t **segments,
                          uint8_t count, uint32_t bytes) {
    apply_chunked_edition(screen, format, segments, count, bytes,
                          INKTAG_BRAND_EDITION_DEFAULT);
}

static void test_all_packings(void) {
    static const uint8_t formats[] = {
        EPD_SCREEN_TRANSFER_MONO_1BPP,
        EPD_SCREEN_TRANSFER_DUAL_1BPP,
        EPD_SCREEN_TRANSFER_INDEXED_2BPP,
        EPD_SCREEN_TRANSFER_PLANAR_2X1BPP,
        EPD_SCREEN_TRANSFER_INDEXED_4BPP,
        EPD_SCREEN_TRANSFER_INDEXED_2X2BPP,
    };
    epd_screen_config_t screen = {
        .color_id = EPD_SCREEN_COLOR_BWR,
        .width = 400u, .height = 300u,
        .visible_width = 400u, .visible_height = 300u,
    };
    uint8_t format_index;
    for (format_index = 0u;
         format_index < sizeof(formats) / sizeof(formats[0]);
         ++format_index) {
        uint8_t format = formats[format_index];
        uint8_t count = (format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
                         format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
                         format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) ? 2u : 1u;
        uint32_t bytes = segment_bytes(&screen, format);
        uint8_t *segments[2] = {calloc(bytes, 1u), calloc(bytes, 1u)};
        assert(segments[0] != NULL && segments[1] != NULL);
        apply_chunked(&screen, format, segments, count, bytes);
        /* Match the 400x300 calendar footer: 1x INKTAG badge at x=351,
         * y=284, size=39x11, with red ink when the packing supports it. */
        if (format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
            format == EPD_SCREEN_TRANSFER_INDEXED_2BPP ||
            format == EPD_SCREEN_TRANSFER_INDEXED_4BPP ||
            format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
            assert(pixel_red(segments, &screen, format, 353u, 284u));
            assert(pixel_red(segments, &screen, format, 355u, 287u));
        } else {
            assert(pixel_black(segments, &screen, format, 353u, 284u));
            assert(pixel_black(segments, &screen, format, 355u, 287u));
        }
        assert(pixel_white(segments, &screen, format, 352u, 289u));
        free(segments[0]);
        free(segments[1]);
    }
}

static void logical_to_physical(const epd_screen_config_t *screen,
                                uint16_t logical_width,
                                uint16_t logical_height,
                                uint16_t x, uint16_t y,
                                uint16_t *physical_x,
                                uint16_t *physical_y) {
    uint8_t rotation = screen->transform & 3u;
    uint16_t tx = x;
    uint16_t ty = y;
    if (rotation == 1u) {
        tx = (uint16_t)(logical_height - 1u - y); ty = x;
    } else if (rotation == 2u) {
        tx = (uint16_t)(logical_width - 1u - x);
        ty = (uint16_t)(logical_height - 1u - y);
    } else if (rotation == 3u) {
        tx = y; ty = (uint16_t)(logical_width - 1u - x);
    }
    if ((screen->transform & 4u) != 0u)
        tx = (uint16_t)(screen->visible_width - 1u - tx);
    if ((screen->transform & 8u) != 0u)
        ty = (uint16_t)(screen->visible_height - 1u - ty);
    *physical_x = (uint16_t)(screen->visible_x + tx);
    *physical_y = (uint16_t)(screen->visible_y + ty);
}

static void test_transform_and_visible_offset(void) {
    uint8_t transform;
    for (transform = 0u; transform < 16u; ++transform) {
        epd_screen_config_t screen = {
            .width = 136u, .height = 128u,
            .visible_x = 4u, .visible_y = 3u,
            .visible_width = 120u, .visible_height = 80u,
            .transform = transform,
        };
        uint16_t logical_width = (transform & 1u) != 0u ? 80u : 120u;
        uint16_t logical_height = (transform & 1u) != 0u ? 120u : 80u;
        uint16_t badge_x = (uint16_t)(logical_width - 39u - 3u);
        uint16_t badge_y = (uint16_t)(logical_height - 11u - 3u);
        uint16_t black_x;
        uint16_t black_y;
        uint16_t white_x;
        uint16_t white_y;
        uint32_t bytes = segment_bytes(&screen, EPD_SCREEN_TRANSFER_MONO_1BPP);
        uint8_t *segment = calloc(bytes, 1u);
        uint8_t *segments[2] = {segment, NULL};
        assert(segment != NULL);
        assert(inktag_brand_overlay_apply(
            &screen, EPD_SCREEN_TRANSFER_MONO_1BPP, 0u, 0u, segment,
            (uint16_t)bytes, INKTAG_BRAND_EDITION_DEFAULT));
        logical_to_physical(&screen, logical_width, logical_height,
                            (uint16_t)(badge_x + 2u), badge_y,
                            &black_x, &black_y);
        logical_to_physical(&screen, logical_width, logical_height,
                            (uint16_t)(badge_x + 1u),
                            (uint16_t)(badge_y + 5u),
                            &white_x, &white_y);
        assert(pixel_black(segments, &screen,
                           EPD_SCREEN_TRANSFER_MONO_1BPP,
                           black_x, black_y));
        assert(pixel_white(segments, &screen,
                           EPD_SCREEN_TRANSFER_MONO_1BPP,
                           white_x, white_y));
        free(segment);
    }
}

static void test_edition_color_and_large_geometry(void) {
    epd_screen_config_t normal = {
        .color_id = EPD_SCREEN_COLOR_BWR,
        .width = 400u, .height = 300u,
        .visible_width = 400u, .visible_height = 300u,
    };
    uint32_t normal_bytes = segment_bytes(&normal,
                                          EPD_SCREEN_TRANSFER_DUAL_1BPP);
    uint8_t *normal_segments[2] = {
        calloc(normal_bytes, 1u), calloc(normal_bytes, 1u),
    };
    assert(normal_segments[0] != NULL && normal_segments[1] != NULL);
    apply_chunked_edition(&normal, EPD_SCREEN_TRANSFER_DUAL_1BPP,
                          normal_segments, 2u, normal_bytes,
                          INKTAG_BRAND_EDITION_PRO);
    /* Reserved edition input must match every byte of permanent InkTag. */
    uint8_t *base_segments[2] = {calloc(normal_bytes, 1u), calloc(normal_bytes, 1u)};
    assert(base_segments[0] && base_segments[1]);
    apply_chunked(&normal, EPD_SCREEN_TRANSFER_DUAL_1BPP,
                  base_segments, 2u, normal_bytes);
    assert(memcmp(normal_segments[0], base_segments[0], normal_bytes) == 0);
    assert(memcmp(normal_segments[1], base_segments[1], normal_bytes) == 0);
    free(base_segments[0]); free(base_segments[1]);
    free(normal_segments[0]);
    free(normal_segments[1]);

    {
        epd_screen_config_t large = {
            .color_id = EPD_SCREEN_COLOR_BWR,
            .width = 600u, .height = 448u,
            .visible_width = 600u, .visible_height = 448u,
        };
        uint32_t bytes = segment_bytes(&large,
                                       EPD_SCREEN_TRANSFER_DUAL_1BPP);
        uint8_t *segments[2] = {calloc(bytes, 1u), calloc(bytes, 1u)};
        assert(segments[0] != NULL && segments[1] != NULL);
        apply_chunked(&large, EPD_SCREEN_TRANSFER_DUAL_1BPP,
                      segments, 2u, bytes);
        /* 600x448 uses the V1 large footer: 2x INKTAG at x=514, y=423. */
        assert(pixel_red(segments, &large,
                         EPD_SCREEN_TRANSFER_DUAL_1BPP, 516u, 423u));
        assert(pixel_white(segments, &large,
                           EPD_SCREEN_TRANSFER_DUAL_1BPP, 515u, 433u));
        free(segments[0]);
        free(segments[1]);
    }
}

static void test_nrf52811_hema_free_badge_is_red(void) {
    epd_screen_config_t screen = {
        .color_id = EPD_SCREEN_COLOR_BWR,
        .width = 128u, .height = 250u,
        .visible_width = 122u, .visible_height = 250u,
        .transform = EPD_SCREEN_TRANSFORM_ROTATE_270,
    };
    uint32_t bytes = segment_bytes(&screen,
                                   EPD_SCREEN_TRANSFER_DUAL_1BPP);
    uint8_t *segments[2] = {calloc(bytes, 1u), calloc(bytes, 1u)};
    assert(segments[0] != NULL && segments[1] != NULL);
    apply_chunked(&screen, EPD_SCREEN_TRANSFER_DUAL_1BPP,
                  segments, 2u, bytes);
    /* Logical INKTAG badge is x=208,y=108,size=39x11. Under the board's 270
     * degree transform its top border maps to physical (108,39). */
    assert(pixel_red(segments, &screen,
                     EPD_SCREEN_TRANSFER_DUAL_1BPP, 108u, 39u));
    assert(pixel_white(segments, &screen,
                       EPD_SCREEN_TRANSFER_DUAL_1BPP, 113u, 40u));
    free(segments[0]);
    free(segments[1]);
}

static void test_invalid_metadata_is_non_mutating(void) {
    epd_screen_config_t screen = {
        .width = 200u, .height = 200u,
        .visible_width = 200u, .visible_height = 200u,
    };
    uint8_t bytes[4] = {1u, 2u, 3u, 4u};
    assert(!inktag_brand_overlay_apply(
        &screen, 0xffu, 0u, 0u, bytes, sizeof(bytes),
        INKTAG_BRAND_EDITION_DEFAULT));
    assert(bytes[0] == 1u && bytes[1] == 2u &&
           bytes[2] == 3u && bytes[3] == 4u);
}

static uint8_t s_mock_write_count;

static bool guarded_mock_write(const epd_screen_config_t *screen,
                               uint8_t format, uint8_t *bytes,
                               uint16_t length) {
    if (!inktag_brand_overlay_apply(
            screen, format, 0u, 0u, bytes, length,
            INKTAG_BRAND_EDITION_DEFAULT)) return false;
    s_mock_write_count++;
    return true;
}

static void test_invalid_metadata_is_not_written(void) {
    epd_screen_config_t screen = {
        .width = 200u, .height = 200u,
        .visible_width = 200u, .visible_height = 200u,
    };
    uint8_t bytes[4] = {1u, 2u, 3u, 4u};
    s_mock_write_count = 0u;
    assert(!guarded_mock_write(&screen, 0xffu, bytes, sizeof(bytes)));
    assert(s_mock_write_count == 0u);
}

static void test_reserved_edition_cannot_hide_the_free_badge(void) {
    epd_screen_config_t screen = {
        .width = 200u, .height = 200u,
        .visible_width = 200u, .visible_height = 200u,
    };
    uint32_t length=segment_bytes(&screen,EPD_SCREEN_TRANSFER_MONO_1BPP);
    uint8_t *base_bytes=calloc(length,1), *reserved=calloc(length,1);
    assert(base_bytes && reserved);
    assert(inktag_brand_overlay_apply(&screen,EPD_SCREEN_TRANSFER_MONO_1BPP,
        0,0,base_bytes,length,INKTAG_BRAND_EDITION_DEFAULT));
    assert(inktag_brand_overlay_apply(&screen,EPD_SCREEN_TRANSFER_MONO_1BPP,
        0,0,reserved,length,INKTAG_BRAND_EDITION_HIDDEN));
    assert(!memcmp(base_bytes,reserved,length));
    free(base_bytes);free(reserved);
}

int main(void) {
    test_all_packings();
    test_transform_and_visible_offset();
    test_edition_color_and_large_geometry();
    test_nrf52811_hema_free_badge_is_red();
    test_invalid_metadata_is_non_mutating();
    test_invalid_metadata_is_not_written();
    test_reserved_edition_cannot_hide_the_free_badge();
    return 0;
}
