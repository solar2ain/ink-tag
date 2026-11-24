/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_gui.h"
#include "inktag_gui_fonts.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pages;
    uint16_t y;
    uint32_t bytes;
    uint8_t* pixels;
    bool failed;
} capture_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pages;
    uint16_t y;
    uint8_t* black_pixels;
    uint8_t* color_pixels;
    bool failed;
} dual_plane_capture_t;

static void capture_frame(void* user_data, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                          uint16_t h) {
    capture_t* capture = user_data;
    if (!black || color != black || x != 0 || y != capture->y || w != capture->width || h == 0 ||
        y + h > capture->height) {
        fprintf(stderr,
                "capture mismatch black=%u shared=%u x=%u y=%u/%u w=%u/%u h=%u limit=%u\n",
                black != NULL, color == black, x, y, capture->y, w, capture->width,
                h, capture->height);
        capture->failed = true;
        return;
    }
    capture->pages++;
    if (capture->pixels) {
        memcpy(capture->pixels + (uint32_t)y * ((w + 3u) / 4u), black,
               (uint32_t)((w + 3u) / 4u) * h);
    }
    capture->y += h;
    capture->bytes += ((uint32_t)w + 3u) / 4u * h;
}

static void capture_dual_plane(void* user_data, uint8_t* black, uint8_t* color,
                               uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    dual_plane_capture_t* capture = user_data;
    uint16_t row_bytes = (uint16_t)((w + 7u) / 8u);
    if (!capture || !black || !color || color == black || x != 0u || y != capture->y ||
        w != capture->width || h == 0u || y + h > capture->height) {
        if (capture) capture->failed = true;
        return;
    }
    memcpy(capture->black_pixels + (uint32_t)y * row_bytes, black,
           (uint32_t)row_bytes * h);
    memcpy(capture->color_pixels + (uint32_t)y * row_bytes, color,
           (uint32_t)row_bytes * h);
    capture->pages++;
    capture->y += h;
}

static uint8_t calendar_full_black[15000];
static uint8_t calendar_full_color[15000];
static uint8_t calendar_paged_black[15000];
static uint8_t calendar_paged_color[15000];
static uint8_t calendar_full_render_buffer[30000];
static uint8_t calendar_gr_heap_buffer[7168];

static int render_400x300_calendar_like_gr551x(bool full_frame,
                                               uint8_t* black_output,
                                               uint8_t* color_output,
                                               uint16_t* pages) {
    gui_data_t data = {
        .mode = MODE_CALENDAR,
        .color = 2u,
        .width = 400u,
        .height = 300u,
        .visible_width = 400u,
        .visible_height = 300u,
        .force_rotation = true,
        .rotation = GFX_ROTATE_0,
        .native_ui_scale = 0u,
        .timestamp = 1789312751u,
        .week_start = 1u,
        .temperature = 25,
        .voltage = 3297u,
        .ssid = "InkTag_GR_3A57",
        .board_name = "gr551x_zkc42vm",
        .screen_info = "400x300 / BWR",
    };
    gui_options_t options = {
        .prefer_full_frame = full_frame,
        .render_buffer = full_frame ? calendar_full_render_buffer : calendar_gr_heap_buffer,
        .render_buffer_size = full_frame ? sizeof(calendar_full_render_buffer) : sizeof(calendar_gr_heap_buffer),
    };
    dual_plane_capture_t capture = {
        .width = 400u,
        .height = 300u,
        .black_pixels = black_output,
        .color_pixels = color_output,
    };
    memset(black_output, 0xff, sizeof(calendar_full_black));
    memset(color_output, 0xff, sizeof(calendar_full_color));
    if (!DrawGUIEx(&data, capture_dual_plane, &capture, &options) ||
        capture.failed || capture.y != 300u) {
        fprintf(stderr,
                "400x300 calendar plane render failed: full=%u pages=%u rows=%u failed=%u\n",
                full_frame, capture.pages, capture.y, capture.failed);
        return 1;
    }
    *pages = capture.pages;
    return 0;
}

static int check_400x300_calendar_gr551x_pages_match_full_frame(void) {
    uint16_t full_pages = 0u;
    uint16_t paged_pages = 0u;
    if (render_400x300_calendar_like_gr551x(true, calendar_full_black,
                                            calendar_full_color, &full_pages) != 0 ||
        render_400x300_calendar_like_gr551x(false, calendar_paged_black,
                                            calendar_paged_color, &paged_pages) != 0 ||
        full_pages != 1u || paged_pages != 10u ||
        memcmp(calendar_full_black, calendar_paged_black,
               sizeof(calendar_full_black)) != 0 ||
        memcmp(calendar_full_color, calendar_paged_color,
               sizeof(calendar_full_color)) != 0) {
        const uint8_t* full = calendar_full_black;
        const uint8_t* paged = calendar_paged_black;
        const char* plane = "black";
        if (memcmp(full, paged, sizeof(calendar_full_black)) == 0) {
            full = calendar_full_color;
            paged = calendar_paged_color;
            plane = "color";
        }
        uint32_t mismatch = 0u;
        while (mismatch < sizeof(calendar_full_black) && full[mismatch] == paged[mismatch])
            mismatch++;
        fprintf(stderr,
                "400x300 GR551x calendar paging changed %s: pages=%u/%u byte=%lu row=%lu\n",
                plane, full_pages, paged_pages,
                (unsigned long)mismatch, (unsigned long)(mismatch / 50u));
        return 1;
    }
    return 0;
}

static int render_and_check(uint16_t width, uint16_t height, bool rotate) {
    uint8_t render_buffer[30000];
    gui_data_t data = {
        .mode = MODE_CALENDAR,
        .color = 3,
        .width = width,
        .height = height,
        .force_rotation = rotate,
        .rotation = GFX_ROTATE_270,
        .timestamp = 1786891447U,
        .week_start = 1,
        .temperature = 25,
        .voltage = 3000,
    };
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .prefer_full_frame = true,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = width, .height = height};
    uint32_t expected_bytes = ((uint32_t)width + 3u) / 4u * height;

    DrawGUIEx(&data, capture_frame, &capture, &options);
    if (capture.failed || capture.pages != 1 || capture.y != height || capture.bytes != expected_bytes) {
        fprintf(stderr, "%ux%u full-frame render failed: pages=%u rows=%u bytes=%lu/%lu failed=%u\n",
                width, height,
                capture.pages, capture.y, (unsigned long)capture.bytes, (unsigned long)expected_bytes,
                capture.failed);
        return 1;
    }
    return 0;
}

static int check_largest_bounded_page(void) {
    uint8_t render_buffer[8192];
    gui_data_t data = {
        .mode = MODE_CALENDAR,
        .color = 3,
        .width = 400,
        .height = 300,
        .timestamp = 1786891447U,
        .week_start = 1,
        .temperature = 25,
        .voltage = 3000,
    };
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .prefer_largest_page = true,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = 400, .height = 300};

    if (!DrawGUIEx(&data, capture_frame, &capture, &options) ||
        capture.failed || capture.pages != 4u || capture.y != 300u ||
        capture.bytes != 30000u) {
        fprintf(stderr,
                "400x300 bounded largest-page render failed: pages=%u rows=%u bytes=%lu failed=%u\n",
                capture.pages, capture.y, (unsigned long)capture.bytes,
                capture.failed);
        return 1;
    }
    return 0;
}

static uint8_t packed_pixel(const uint8_t* frame, uint16_t width, uint16_t x, uint16_t y) {
    uint8_t value = frame[(uint32_t)y * ((width + 3u) / 4u) + x / 4u];
    return (value >> (6u - 2u * (x & 3u))) & 0x03u;
}


static int check_one_bit_native_palette_fallback(void) {
    static const uint16_t colors[] = {
        GFX_RED, GFX_YELLOW, GFX_BLUE, GFX_GREEN,
        GFX_ORANGE, GFX_BLACK, GFX_WHITE,
    };
    uint8_t frame = 0u;
    Adafruit_GFX gfx;

    GFX_begin_with_buffer(&gfx, 8, 1, 1, &frame, sizeof(frame));
    if (!gfx.buffer || gfx.page_height != 1) return 1;
    GFX_setColorPlane(&gfx, GFX_COLOR_PLANE_ALL);
    GFX_firstPage(&gfx);
    for (uint8_t x = 0u; x < 7u; ++x) GFX_drawPixel(&gfx, x, 0, colors[x]);
    if (frame != 0x03u) {
        fprintf(stderr, "one-bit native palette fallback failed: %02x\n", frame);
        GFX_end(&gfx);
        return 1;
    }
    GFX_end(&gfx);
    return 0;
}

static int check_122_in_128_viewport(void) {
    uint8_t frame[8000];
    Adafruit_GFX gfx;
    GFX_begin_4c_with_buffer(&gfx, 128, 250, 500, frame, sizeof(frame));
    if (!gfx.buffer || gfx.page_height != 250) return 1;

    GFX_setRotation(&gfx, GFX_ROTATE_90);
    GFX_setRawViewport(&gfx, 0, 0, 122, 250);
    GFX_setMirrorX(&gfx, true);
    GFX_firstPage(&gfx);
    GFX_drawPixel(&gfx, 0, 0, GFX_BLACK);
    GFX_drawPixel(&gfx, 249, 121, GFX_RED);

    /* Protocol order is rotation first, then mirror X in rotated raw
     * coordinates. Logical corners therefore map to raw (0,0) and (121,249);
     * the hidden controller columns 122..127 remain white. */
    if (packed_pixel(frame, 128, 0, 0) != 0 || packed_pixel(frame, 128, 121, 249) != 3 ||
        packed_pixel(frame, 128, 127, 249) != 1) {
        fprintf(stderr, "122-in-128 viewport mapping failed\n");
        GFX_end(&gfx);
        return 1;
    }
    GFX_end(&gfx);
    return 0;
}

static int check_400x300_board_transform(void) {
    uint8_t frame[30000];
    Adafruit_GFX gfx;
    GFX_begin_4c_with_buffer(&gfx, 400, 300, 600, frame, sizeof(frame));
    if (!gfx.buffer || gfx.page_height != 300) return 1;

    GFX_setRotation(&gfx, GFX_ROTATE_180);
    GFX_setMirrorX(&gfx, true);
    GFX_firstPage(&gfx);
    GFX_drawPixel(&gfx, 0, 0, GFX_BLACK);
    GFX_drawPixel(&gfx, 399, 299, GFX_RED);

    /* Rotation 180 plus logical-X mirroring corrects the reported upside-down
     * and left/right-mirrored board presentation. */
    if (packed_pixel(frame, 400, 0, 299) != 0 || packed_pixel(frame, 400, 399, 0) != 3) {
        fprintf(stderr, "400x300 board transform mapping failed\n");
        GFX_end(&gfx);
        return 1;
    }
    GFX_end(&gfx);
    return 0;
}

static int render_rotated_paged_scene(uint8_t* output, uint8_t* buffer,
                                      size_t buffer_size, uint16_t* pages) {
    Adafruit_GFX gfx;
    capture_t capture = {
        .width = 64,
        .height = 48,
        .pixels = output,
    };

    GFX_begin_4c_with_buffer(&gfx, 64, 48, 96, buffer, buffer_size);
    if (!gfx.buffer || gfx.page_height <= 0) return 1;
    GFX_setRotation(&gfx, GFX_ROTATE_180);
    GFX_setFont(&gfx, u8g2_font_wqy9_t_lunar);
    GFX_setTextColor(&gfx, GFX_BLACK, GFX_WHITE);
    GFX_firstPage(&gfx);
    do {
        GFX_fillScreen(&gfx, GFX_WHITE);
        GFX_fillRect(&gfx, 3, 2, 21, 13, GFX_BLACK);
        GFX_fillRect(&gfx, 35, 29, 32, 22, GFX_RED);
        GFX_drawFastHLine(&gfx, -4, 24, 72, GFX_YELLOW);
        GFX_drawStr(&gfx, 5, 44, "InkTag");
    } while (GFX_nextPage(&gfx, capture_frame, &capture));
    GFX_end(&gfx);
    *pages = capture.pages;
    return capture.failed || capture.y != 48u || capture.bytes != sizeof(uint8_t) * 768u;
}

static int check_rotated_small_pages_match_full_frame(void) {
    uint8_t full_buffer[768];
    uint8_t page_buffer[64];
    uint8_t full_frame[768];
    uint8_t paged_frame[768];
    uint16_t full_pages = 0;
    uint16_t paged_pages = 0;

    memset(full_frame, 0, sizeof(full_frame));
    memset(paged_frame, 0, sizeof(paged_frame));
    if (render_rotated_paged_scene(full_frame, full_buffer,
                                   sizeof(full_buffer), &full_pages) != 0 ||
        render_rotated_paged_scene(paged_frame, page_buffer,
                                   sizeof(page_buffer), &paged_pages) != 0 ||
        full_pages != 1u || paged_pages != 12u ||
        memcmp(full_frame, paged_frame, sizeof(full_frame)) != 0) {
        fprintf(stderr,
                "rotated small-page clipping changed output: full=%u paged=%u\n",
                full_pages, paged_pages);
        return 1;
    }
    return 0;
}

static int check_compact_formatter(void) {
    char buffer[64];
    char truncated[5];

    if (GFX_format(buffer, sizeof(buffer), "%s %d %u %02X %%", "T", -12, 34u, 0xAFu) != 13 ||
        strcmp(buffer, "T -12 34 AF %") != 0) {
        fprintf(stderr, "compact GUI formatter output mismatch: %s\n", buffer);
        return 1;
    }
    if (GFX_format(buffer, sizeof(buffer), "%010d", INT32_MIN) != 11 ||
        strcmp(buffer, "-2147483648") != 0) {
        fprintf(stderr, "compact GUI formatter INT32_MIN mismatch: %s\n", buffer);
        return 1;
    }
    if (GFX_format(truncated, sizeof(truncated), "123456") != 4 || strcmp(truncated, "1234") != 0) {
        fprintf(stderr, "compact GUI formatter truncation mismatch: %s\n", truncated);
        return 1;
    }
    return 0;
}

static int check_device_info_ascii_font(void) {
    static const char required[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-./ |";
    u8g2_font_t font = {0};
    u8g2_SetFont(&font, u8g2_font_wqy9_t_lunar);
    for (const char* c = required; *c != '\0'; c++) {
        if (!u8g2_IsGlyph(&font, (uint8_t)*c)) {
            fprintf(stderr, "device_info ASCII font is missing glyph 0x%02X\n", (uint8_t)*c);
            return 1;
        }
    }
    return 0;
}

/* 768x552 packed 2-bpp is 105,984 bytes. Keep the exact full frame and its
 * backing buffer separate so page-culling changes are compared byte-for-byte,
 * not only through a hash of two differently paged renders. */
static uint8_t high_ppi_reference[106000];
static uint8_t high_ppi_frame[106000];
static uint8_t high_ppi_full_buffer[106000];
static uint8_t device_info_frame[106000];
static uint8_t a0_full_render_buffer[106000];

static uint32_t frame_hash(const uint8_t* frame, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t index = 0u; index < length; ++index) {
        hash ^= frame[index];
        hash *= 16777619u;
    }
    return hash;
}

static int render_a0_device_info(const uint8_t* qr, uint8_t* render_buffer,
                                 size_t render_buffer_size,
                                 bool prefer_full_frame, uint16_t* pages,
                                 uint32_t* hash) {
    gui_data_t data = {
        .mode = MODE_DEVICE_INFO,
        .color = 3,
        .width = 768,
        .height = 552,
        .board_name = "huawei_398_a0",
        .screen_info = "768x552 / BWRY",
        .device_info_qr_data = {qr},
        .device_info_qr_modules = {33},
    };
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .force_buffer_color = true,
        .buffer_color = 3,
        .prefer_full_frame = prefer_full_frame,
        .prefer_largest_page = !prefer_full_frame,
        .render_buffer = render_buffer,
        .render_buffer_size = render_buffer_size,
    };
    capture_t capture = {
        .width = 768,
        .height = 552,
        .pixels = device_info_frame,
    };
    strcpy(data.ssid, "InkTag_GR_A0F0");
    memset(device_info_frame, 0, sizeof(device_info_frame));
    if (!DrawGUIEx(&data, capture_frame, &capture, &options) || capture.failed ||
        capture.y != 552u || capture.bytes != 105984u) return 1;
    *pages = capture.pages;
    *hash = frame_hash(device_info_frame, 105984u);
    return 0;
}

static int check_a0_ram_qr_paged_matches_full_frame(void) {
    static const uint8_t expected_qr[137] = {
        0xFE, 0x48, 0xBF, 0xBF, 0xC1, 0x38, 0xD2, 0x50, 0x6E, 0xA0, 0xC3, 0xEB,
        0xB7, 0x55, 0x04, 0x95, 0xDB, 0xAB, 0x7C, 0xE2, 0xEC, 0x15, 0x71, 0x8D,
        0x07, 0xFA, 0xAA, 0xAA, 0xFE, 0x01, 0xC1, 0x7A, 0x00, 0xBE, 0x4E, 0x5B,
        0x3E, 0x7C, 0x46, 0x6C, 0x9B, 0x7E, 0xFF, 0xF2, 0x82, 0xD9, 0x1F, 0x9C,
        0x19, 0xCF, 0xFC, 0xA9, 0xE1, 0x50, 0xCA, 0xA9, 0x26, 0x5F, 0xCB, 0x0E,
        0x31, 0xFC, 0xFA, 0x7E, 0x0D, 0xDC, 0x73, 0x05, 0x42, 0xD8, 0xC0, 0x58,
        0x1E, 0xBB, 0xC6, 0x8E, 0xEB, 0x3E, 0xD6, 0x0D, 0x93, 0xC7, 0xE7, 0x67,
        0xC2, 0x5E, 0x5C, 0xAB, 0x1C, 0x50, 0xB6, 0x59, 0x69, 0xD6, 0x15, 0x33,
        0x8D, 0x7E, 0xE7, 0x82, 0x9E, 0x73, 0xFA, 0x00, 0x4D, 0x7C, 0x45, 0x7F,
        0x8F, 0x70, 0xEA, 0x90, 0x53, 0x6F, 0x31, 0xFB, 0xAE, 0xA9, 0xEF, 0xCD,
        0xD7, 0x6B, 0x0A, 0x66, 0xEA, 0x8F, 0x2C, 0xC1, 0x04, 0xD6, 0x4B, 0x18,
        0xFE, 0x9D, 0x40, 0xD3, 0x00,
    };
    uint8_t qr[sizeof(expected_qr)];
    uint8_t page_buffer[8192];
    uint16_t full_pages = 0u;
    uint16_t paged_pages = 0u;
    uint16_t fallback_pages = 0u;
    uint32_t full_hash = 0u;
    uint32_t paged_hash = 0u;
    uint32_t fallback_hash = 0u;
    memcpy(qr, expected_qr, sizeof(qr));
    if (render_a0_device_info(qr, a0_full_render_buffer,
                              sizeof(a0_full_render_buffer), true,
                              &full_pages, &full_hash) != 0 ||
        render_a0_device_info(qr, page_buffer, sizeof(page_buffer), false,
                              &paged_pages, &paged_hash) != 0 ||
        render_a0_device_info(qr, page_buffer, sizeof(page_buffer), true,
                              &fallback_pages, &fallback_hash) != 0 ||
        full_pages != 1u || paged_pages != 14u || full_hash != paged_hash) {
        fprintf(stderr,
                "Huawei A0 RAM QR paging changed output: pages=%u/%u/%u hash=%08lx/%08lx/%08lx\n",
                full_pages, paged_pages, fallback_pages,
                (unsigned long)full_hash, (unsigned long)paged_hash,
                (unsigned long)fallback_hash);
        return 1;
    }
    if (fallback_pages != 18u || fallback_hash != full_hash) {
        fprintf(stderr,
                "Huawei A0 full-frame request did not safely fall back: pages=%u hash=%08lx/%08lx\n",
                fallback_pages, (unsigned long)full_hash,
                (unsigned long)fallback_hash);
        return 1;
    }
    return 0;
}

static int check_high_ppi_page_parity(void) {
    uint8_t da_render_buffer[8192];
    uint32_t device_info_hashes[2] = {0u, 0u};
    static const struct {
        uint8_t mode;
        uint8_t scale;
    } cases[] = {
        {MODE_CALENDAR, 1u},
        {MODE_CLOCK, 1u},
        {MODE_DEVICE_INFO, 1u},
        {MODE_DEVICE_INFO, 2u}, /* preset-default 150% */
    };
    for (uint8_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        gui_data_t data = {
            .mode = cases[index].mode,
            .color = 3,
            .width = 768,
            .height = 552,
            .force_rotation = true,
            .rotation = GFX_ROTATE_180,
            .timestamp = 1786891447U,
            .week_start = 1u,
            .temperature = 25,
            .voltage = 3000u,
            .native_ui_scale = cases[index].scale,
            .board_name = "huawei_398_a1",
            .screen_info = "768x552 / BWRY",
        };
        strcpy(data.ssid, "InkTag_GR_3A57");
        gui_options_t options = {
            .color_plane = GFX_COLOR_PLANE_ALL,
            .force_buffer_color = true,
            .buffer_color = 3,
            .prefer_full_frame = true,
            .render_buffer = high_ppi_full_buffer,
            .render_buffer_size = sizeof(high_ppi_full_buffer),
        };
        memset(high_ppi_reference, 0, sizeof(high_ppi_reference));
        capture_t capture = {
            .width = 768,
            .height = 552,
            .pixels = high_ppi_reference,
        };
        if (!DrawGUIEx(&data, capture_frame, &capture, &options) || capture.failed ||
            capture.pages != 1u || capture.y != 552u ||
            capture.bytes != 105984u) {
            fprintf(stderr,
                    "768x552 mode %u scale %u did not render one full frame\n",
                    cases[index].mode, cases[index].scale);
            return 1;
        }
        if (cases[index].mode == MODE_DEVICE_INFO)
            device_info_hashes[cases[index].scale - 1u] =
                frame_hash(high_ppi_reference, 105984u);

        options.prefer_full_frame = false;
        options.prefer_largest_page = true;
        options.render_buffer = da_render_buffer;
        options.render_buffer_size = sizeof(da_render_buffer);
        memset(high_ppi_frame, 0, sizeof(high_ppi_frame));
        capture = (capture_t){
            .width = 768,
            .height = 552,
            .pixels = high_ppi_frame,
        };
        if (!DrawGUIEx(&data, capture_frame, &capture, &options) ||
            capture.failed || capture.pages != 14u || capture.y != 552u ||
            capture.bytes != 105984u ||
            memcmp(high_ppi_frame, high_ppi_reference, 105984u) != 0) {
            uint32_t different = 0u;
            uint32_t last_different = 0u;
            uint32_t difference_count = 0u;
            uint16_t row_differences[552] = {0u};
            while (different < 105984u &&
                   high_ppi_frame[different] == high_ppi_reference[different])
                different++;
            for (uint32_t probe = different; probe < 105984u; ++probe) {
                if (high_ppi_frame[probe] != high_ppi_reference[probe]) {
                    last_different = probe;
                    difference_count++;
                    row_differences[probe / 192u]++;
                }
            }
            fprintf(stderr,
                    "768x552 mode %u scale %u changed with the DA 8 KiB page: "
                    "pages=%u first-byte=%lu (%u,%lu) last=(%u,%lu) count=%lu %02x/%02x\n",
                    cases[index].mode, cases[index].scale, capture.pages,
                    (unsigned long)different, (unsigned)(different % 192u),
                    (unsigned long)(different / 192u),
                    (unsigned)(last_different % 192u),
                    (unsigned long)(last_different / 192u),
                    (unsigned long)difference_count,
                    different < 105984u ? high_ppi_reference[different] : 0u,
                    different < 105984u ? high_ppi_frame[different] : 0u);
            for (uint16_t row = 0u; row < 552u; ++row) {
                if (row_differences[row] != 0u)
                    fprintf(stderr, " row %u: %u differing bytes\n",
                            row, row_differences[row]);
            }
            return 1;
        }
    }
    if (device_info_hashes[0] == device_info_hashes[1]) {
        fprintf(stderr, "device_info page ignored the configured native text scale\n");
        return 1;
    }
    return 0;
}

static int check_v1_device_info_logo_palette(uint8_t color,
                                              uint8_t expected_middle,
                                              uint8_t expected_right,
                                              bool three_bars) {
    uint8_t render_buffer[30000];
    gui_data_t data = {
        .mode = MODE_DEVICE_INFO,
        .color = color,
        .width = 400,
        .height = 300,
        .board_name = "om6626_zkc42b",
        .screen_info = "screen",
    };
    strcpy(data.ssid, "InkTag_OM_BB92");
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .force_buffer_color = true,
        .buffer_color = 3,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = 400, .height = 300, .pixels = device_info_frame};
    DrawGUIEx(&data, capture_frame, &capture, &options);

    /* The 400x300 header uses a 15x13px mark at (12, 8). BWRY uses the
     * white backing plus black/red/yellow pigment bars; BW/BWR stay two-bar. */
    uint8_t backing = packed_pixel(device_info_frame, 400, 14, 15);
    uint8_t left = packed_pixel(device_info_frame, 400, 15, 15);
    uint8_t middle = packed_pixel(device_info_frame, 400, three_bars ? 18 : 20, 15);
    uint8_t right = three_bars ? packed_pixel(device_info_frame, 400, 22, 15) : middle;
    if (capture.failed || capture.y != 300 || backing != 1u || left != 0u ||
        middle != expected_middle || right != expected_right) {
        fprintf(stderr,
                "V1 device_info logo color=%u backing=%u bars=%u/%u/%u\n",
                color, backing, left, middle, right);
        return 1;
    }
    return 0;
}

/* Frozen V0 device_info-page probes remain available for archaeology, but the
 * production MODE_DEVICE_INFO value now renders the V1 device-information page.
 * Pixel parity for that page is enforced by the 360-case V1 oracle. */
#if defined(INKTAG_GUI_LEGACY_DEVICE_INFO_TESTS)
static void write_le16(FILE* output, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    fwrite(bytes, 1u, sizeof(bytes), output);
}

static void write_le32(FILE* output, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24),
    };
    fwrite(bytes, 1u, sizeof(bytes), output);
}

static void dump_device_info_preview(uint16_t width, uint16_t height) {
    const char* directory = getenv("INKTAG_GUI_DUMP_DIR");
    if (!directory || !directory[0]) return;
    bool rotate = width < height && width <= 200u;
    uint16_t output_width = rotate ? height : width;
    uint16_t output_height = rotate ? width : height;
    char path[512];
    snprintf(path, sizeof(path), "%s/device_info-%ux%u.bmp", directory,
             output_width, output_height);
    FILE* output = fopen(path, "wb");
    if (!output) return;
    uint32_t row_bytes = ((uint32_t)output_width * 3u + 3u) & ~3u;
    uint32_t image_bytes = row_bytes * output_height;
    fwrite("BM", 1u, 2u, output);
    write_le32(output, 54u + image_bytes);
    write_le32(output, 0u);
    write_le32(output, 54u);
    write_le32(output, 40u);
    write_le32(output, output_width);
    write_le32(output, output_height);
    write_le16(output, 1u);
    write_le16(output, 24u);
    write_le32(output, 0u);
    write_le32(output, image_bytes);
    write_le32(output, 2835u);
    write_le32(output, 2835u);
    write_le32(output, 0u);
    write_le32(output, 0u);
    static const uint8_t palette[4][3] = {
        {17u, 23u, 20u}, {255u, 255u, 255u},
        {235u, 194u, 25u}, {220u, 48u, 38u},
    };
    uint8_t padding[3] = {0u, 0u, 0u};
    for (int32_t y = (int32_t)output_height - 1; y >= 0; y--) {
        for (uint16_t x = 0u; x < output_width; x++) {
            uint16_t source_x = rotate ? (uint16_t)(width - 1u - (uint16_t)y) : x;
            uint16_t source_y = rotate ? x : (uint16_t)y;
            const uint8_t* rgb = palette[packed_pixel(device_info_frame, width, source_x, source_y)];
            uint8_t bgr[3] = {rgb[2], rgb[1], rgb[0]};
            fwrite(bgr, 1u, 3u, output);
        }
        fwrite(padding, 1u, row_bytes - (uint32_t)output_width * 3u, output);
    }
    fclose(output);
}

static int check_device_info_layout_pixels(uint16_t width, uint16_t height,
                                       uint16_t visible_width, uint16_t visible_height,
                                       uint16_t header_x, uint16_t header_y,
                                       uint16_t qr1_x, uint16_t qr1_y,
                                       uint16_t qr2_x, uint16_t qr2_y,
                                       uint16_t margin_x, uint16_t margin_y) {
    uint8_t render_buffer[30000];
    uint8_t qr[137];
    memset(device_info_frame, 0, sizeof(device_info_frame));
    memset(qr, 0xFF, sizeof(qr));
    gui_data_t data = {
        .mode = MODE_DEVICE_INFO,
        .color = 3,
        .width = width,
        .height = height,
        .visible_width = visible_width,
        .visible_height = visible_height,
        .board_name = "om6626_zkc42b",
        .screen_info = "screen / BWRY",
        .device_info_qr_data = {qr, qr},
        .device_info_qr_modules = {33, 33},
    };
    strcpy(data.ssid, "InkTag_OM_BB92");
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .force_buffer_color = true,
        .buffer_color = 3,
        .prefer_full_frame = true,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = width, .height = height, .pixels = device_info_frame};
    DrawGUIEx(&data, capture_frame, &capture, &options);
    dump_device_info_preview(width, height);
    uint8_t header = packed_pixel(device_info_frame, width, header_x, header_y);
    uint8_t qr1 = packed_pixel(device_info_frame, width, qr1_x, qr1_y);
    uint8_t qr2 = packed_pixel(device_info_frame, width, qr2_x, qr2_y);
    uint8_t margin = packed_pixel(device_info_frame, width, margin_x, margin_y);
    if (capture.failed || capture.pages == 0 || capture.y != height ||
        header != 0 || qr1 != 0 || qr2 != 0 || margin != 1) {
        fprintf(stderr,
                "%ux%u device_info pixels: header=%u qr1=%u qr2=%u margin=%u pages=%u failed=%u\n",
                width, height, header, qr1, qr2, margin, capture.pages, capture.failed);
        return 1;
    }
    return 0;
}

static int check_device_info_logo_palette(uint8_t color, uint8_t expected_middle,
                                      uint8_t expected_right, bool three_bars) {
    uint8_t render_buffer[30000];
    gui_data_t data = {
        .mode = MODE_DEVICE_INFO,
        .color = color,
        .width = 400,
        .height = 300,
        .board_name = "om6626_zkc42b",
        .screen_info = "screen",
    };
    strcpy(data.ssid, "InkTag_OM_BB92");
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .force_buffer_color = true,
        .buffer_color = 3,
        .prefer_full_frame = true,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = 400, .height = 300, .pixels = device_info_frame};
    DrawGUIEx(&data, capture_frame, &capture, &options);

    /* The 400x300 header uses a 39x33px mark at (15, 8). These samples
     * lock its white backing and vertical bars without coupling the test to
     * text glyphs or QR payloads. */
    uint8_t backing = packed_pixel(device_info_frame, 400, 18, 20);
    uint8_t left = packed_pixel(device_info_frame, 400, three_bars ? 23 : 24, 20);
    uint8_t middle = packed_pixel(device_info_frame, 400, three_bars ? 31 : 35, 20);
    uint8_t right = three_bars ? packed_pixel(device_info_frame, 400, 43, 20) : middle;
    uint8_t vertical = packed_pixel(device_info_frame, 400, three_bars ? 23 : 24, 34);
    if (capture.failed || capture.y != 300 || backing != 1u || left != 0u ||
        middle != expected_middle || right != expected_right || vertical != 0u) {
        fprintf(stderr,
                "device_info logo color=%u backing=%u bars=%u/%u/%u vertical=%u\n",
                color, backing, left, middle, right, vertical);
        return 1;
    }
    return 0;
}

static int check_compact_device_info_uses_two_pixel_qr_modules(void) {
    uint8_t render_buffer[30000];
    uint8_t qr[137];
    memset(qr, 0xFF, sizeof(qr));
    gui_data_t data = {
        .mode = MODE_DEVICE_INFO,
        .color = 3,
        .width = 256,
        .height = 128,
        .visible_width = 250,
        .visible_height = 128,
        .board_name = "om6626_zkc21b",
        .screen_info = "128x250 / BWRY",
        .device_info_qr_data = {qr, qr},
        .device_info_qr_modules = {33, 33},
    };
    strcpy(data.ssid, "InkTag_OM_634F");
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .force_buffer_color = true,
        .buffer_color = 3,
        .prefer_full_frame = true,
        .render_buffer = render_buffer,
        .render_buffer_size = sizeof(render_buffer),
    };
    capture_t capture = {.width = 256, .height = 128, .pixels = device_info_frame};
    DrawGUIEx(&data, capture_frame, &capture, &options);

    /* A 76 px rail leaves two exact 82 px QR boxes. Their 8 px quiet zones
     * prove that each 33-module payload is rendered at 2x, not 1x. */
    if (capture.failed || capture.y != 128 ||
        packed_pixel(device_info_frame, 256, 75, 64) != 0u ||
        packed_pixel(device_info_frame, 256, 76, 64) != 1u ||
        packed_pixel(device_info_frame, 256, 88, 38) != 1u ||
        packed_pixel(device_info_frame, 256, 89, 38) != 0u ||
        packed_pixel(device_info_frame, 256, 170, 38) != 1u ||
        packed_pixel(device_info_frame, 256, 171, 38) != 0u) {
        fprintf(stderr, "250x128 device_info did not preserve a 76px rail and 2x QR modules\n");
        return 1;
    }
    return 0;
}
#endif

int main(void) {
    if (strcmp(GUI_EpdColorName(1u), "BW") != 0 ||
        strcmp(GUI_EpdColorName(2u), "BWR") != 0 ||
        strcmp(GUI_EpdColorName(3u), "BWRY") != 0 ||
        strcmp(GUI_EpdColorName(10u), "E6") != 0 ||
        strcmp(GUI_EpdColorName(11u), "COLOR") != 0) {
        fprintf(stderr, "device_info EPD color labels changed\n");
        return 1;
    }
    if (render_and_check(128, 250, true) != 0) return 1;
    if (render_and_check(400, 300, false) != 0) return 1;
    if (check_largest_bounded_page() != 0) return 1;
    if (check_122_in_128_viewport() != 0) return 1;
    if (check_400x300_board_transform() != 0) return 1;
    if (check_400x300_calendar_gr551x_pages_match_full_frame() != 0) return 1;
    if (check_rotated_small_pages_match_full_frame() != 0) return 1;
    if (check_one_bit_native_palette_fallback() != 0) return 1;
    if (check_compact_formatter() != 0) return 1;
    if (check_device_info_ascii_font() != 0) return 1;
    if (check_high_ppi_page_parity() != 0) return 1;
    if (check_v1_device_info_logo_palette(1u, 0u, 0u, false) != 0) return 1;
    if (check_v1_device_info_logo_palette(2u, 3u, 3u, false) != 0) return 1;
    if (check_v1_device_info_logo_palette(3u, 3u, 2u, true) != 0) return 1;
    if (check_v1_device_info_logo_palette(5u, 2u, 2u, false) != 0) return 1;
    if (check_a0_ram_qr_paged_matches_full_frame() != 0) return 1;
    puts("V1 native GUIs render complete frames with calibrated visible bounds.");
    return 0;
}
