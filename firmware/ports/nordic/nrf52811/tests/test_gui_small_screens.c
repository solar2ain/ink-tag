#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_gui.h"

#if defined(TEST_GUI_STATIC_BUFFER)
static uint8_t render_buffer[7168U];
#define TEST_RENDER_BUFFER_FIELDS \
    .render_buffer = render_buffer, \
    .render_buffer_size = sizeof(render_buffer),
#else
#define TEST_RENDER_BUFFER_FIELDS
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t next_y;
    uint32_t bytes;
    uint16_t pages;
    uint8_t* pixels;
    bool failed;
} capture_t;

static void capture_page(void* user_data, uint8_t* black, uint8_t* color, uint16_t x, uint16_t y, uint16_t w,
                         uint16_t h) {
    capture_t* capture = user_data;
    uint32_t page_bytes = ((uint32_t)w + 7U) / 8U * h;

    if (black == NULL || color != NULL || x != 0 || w != capture->width || y != capture->next_y || h == 0 ||
        y + h > capture->height) {
        capture->failed = true;
        return;
    }

    if (capture->pixels != NULL) {
        memcpy(capture->pixels + (uint32_t)y * (((uint32_t)w + 7U) / 8U), black, page_bytes);
    }
    capture->next_y += h;
    capture->bytes += page_bytes;
    capture->pages++;
}

static bool pixel_is_black(const uint8_t* frame, uint16_t width, uint16_t x, uint16_t y) {
    uint8_t byte = frame[(uint32_t)y * (((uint32_t)width + 7U) / 8U) + x / 8U];
    return (byte & (uint8_t)(0x80U >> (x & 7U))) == 0U;
}






static int render_and_check(uint16_t width, uint16_t height, display_mode_t mode, GFX_ColorPlane plane) {
    gui_data_t data = {
        .mode = mode,
        .color = 2,
        .width = width,
        .height = height,
        .timestamp = 1786891447U,
        .week_start = 1,
        .temperature = 25,
        .voltage = 3000,
    };
    gui_options_t options = {
        .color_plane = plane,
        .force_buffer_color = true,
        .buffer_color = 1,
        TEST_RENDER_BUFFER_FIELDS
    };
    capture_t capture = {.width = width, .height = height};
    uint32_t expected_bytes = ((uint32_t)width + 7U) / 8U * height;

    bool rendered = DrawGUIEx(&data, capture_page, &capture, &options);
    if (!rendered || capture.failed || capture.next_y != height ||
        capture.bytes != expected_bytes || capture.pages == 0) {
        fprintf(stderr,
                "%ux%u mode=%u plane=%u: pages=%u rows=%u/%u bytes=%lu/%lu failed=%u\n",
                width, height, mode, plane, capture.pages, capture.next_y, height, (unsigned long)capture.bytes,
                (unsigned long)expected_bytes, capture.failed);
        return 1;
    }
    return 0;
}

static int check_board_display_names(void) {
    Adafruit_GFX gfx;
    gui_data_t data = {.board_name = "nrf52832_priceetag", .board_name_short = "priceetag"};
    GFX_begin(&gfx, 152, 152, 1);
    const char* selected = GUI_BoardDisplayName(&gfx, &data, 200, 24, 256u, 256u);
    int width = 0;
    for (const char* letter = data.board_name; *letter; ++letter)
        width += u8g2_GetGlyphWidth(&gfx.u8g2, (uint8_t)*letter);
    int height = GFX_getFontHeight(&gfx);
    bool failed = strcmp(selected, data.board_name) != 0 ||
        strcmp(GUI_BoardDisplayName(&gfx, &data, width, height, 256u, 256u), data.board_name) != 0 ||
        strcmp(GUI_BoardDisplayName(&gfx, &data, width - 1, height, 256u, 256u), data.board_name_short) != 0 ||
        strcmp(GUI_BoardDisplayName(&gfx, &data, width, height - 1, 256u, 256u), data.board_name_short) != 0;
    /* Same physical panel, different available row width: no 152px shortcut. */
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, 64, 24, 256u, 256u), data.board_name_short) != 0;
    data.board_name_short = NULL;
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, 1, 1, 256u, 256u), data.board_name) != 0;
    data.board_name = NULL;
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, 200, 24, 256u, 256u), "BOARD") != 0;
    GFX_end(&gfx);
    GFX_begin(&gfx, 800, 480, 1);
    data.board_name = "nrf52832_priceetag";
    data.board_name_short = "priceetag";
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, 400, 32, 384u, 160u), data.board_name) != 0;
    width = 0;
    for (const char* letter = data.board_name; *letter; ++letter)
        width += (int32_t)u8g2_GetGlyphWidth(&gfx.u8g2, (uint8_t)*letter) * gfx.font_scale_q8 / 256;
    height = GFX_getFontHeight(&gfx);
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, width, height, 384u, 160u), data.board_name) != 0;
    failed |= strcmp(GUI_BoardDisplayName(&gfx, &data, width - 1, height, 384u, 160u), data.board_name_short) != 0;
    GFX_end(&gfx);
    if (failed) {
        fprintf(stderr, "board display name fit/fallback contract failed (width=%d height=%d)\n", width, height);
        return 1;
    }
    return 0;
}

int main(void) {
    static const uint16_t sizes[][2] = {{104, 212}, {128, 250}, {152, 296}, {400, 300}};
    static const display_mode_t modes[] = {MODE_CALENDAR, MODE_CLOCK, MODE_DEVICE_INFO, MODE_LOW_BATTERY};
    static const GFX_ColorPlane planes[] = {GFX_COLOR_PLANE_BLACK, GFX_COLOR_PLANE_RED};

    if (!GUI_CountdownNameSupported("国庆", 6u) ||
        GUI_CountdownNameSupported("龘", 3u) ||
        GUI_CountdownNameSupported("ABCDE", 5u)) {
        fprintf(stderr, "countdown glyph validation mismatch\n");
        return 1;
    }

    for (uint8_t repeat = 0; repeat < 20; repeat++) {
        for (uint8_t size = 0; size < sizeof(sizes) / sizeof(sizes[0]); size++) {
            for (uint8_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); mode++) {
                for (uint8_t plane = 0; plane < sizeof(planes) / sizeof(planes[0]); plane++) {
                    if (render_and_check(sizes[size][0], sizes[size][1], modes[mode], planes[plane]) != 0) return 1;
                }
            }
        }
    }

    if (check_board_display_names() != 0) return 1;

    puts("GUI validation passed for retained2.13/2.66/4.2-inch rectangular native layouts and board display names.");
    return 0;
}
