/*
 * Headless native-page renderer for the WebUI layout design gallery.
 *
 * This executable deliberately calls the same DrawGUIEx(), Adafruit_GFX,
 * Lunar and font sources as application firmware.  Its PPM output is the
 * firmware framebuffer converted to RGB without reimplementing layout math
 * in JavaScript.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_gui.h"
#include "inktag_gui_device_qr.h"
#include "inktag_gui_fonts.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t next_y;
    uint16_t row_bytes;
    uint8_t* black;
    uint8_t* red;
    bool failed;
} capture_t;

static void capture_frame(void* user_data, uint8_t* black, uint8_t* red,
                          uint16_t x, uint16_t y, uint16_t width,
                          uint16_t height) {
    capture_t* capture = user_data;
    uint16_t transfer_width = capture ? (uint16_t)(capture->row_bytes * 8u) : 0u;
    if (!capture || !black || !red || x != 0u || y != capture->next_y ||
        width != transfer_width || height == 0u ||
        y + height > capture->height) {
        fprintf(stderr,
                "callback mismatch capture=%u black=%u red=%u x=%u y=%u/%u width=%u/%u height=%u limit=%u\n",
                capture != NULL, black != NULL, red != NULL, x, y,
                capture ? capture->next_y : 0u, width, transfer_width, height,
                capture ? capture->height : 0u);
        if (capture) capture->failed = true;
        return;
    }
    uint32_t bytes = (uint32_t)capture->row_bytes * height;
    memcpy(capture->black + (uint32_t)y * capture->row_bytes, black, bytes);
    memcpy(capture->red + (uint32_t)y * capture->row_bytes, red, bytes);
    capture->next_y += height;
}

static uint8_t plane_bit(const uint8_t* plane, uint16_t row_bytes,
                         uint16_t x, uint16_t y) {
    return (plane[(uint32_t)y * row_bytes + x / 8u] &
            (uint8_t)(0x80u >> (x & 7u))) != 0u;
}

static void set_plane_bit(uint8_t* plane, uint16_t row_bytes,
                          uint16_t x, uint16_t y, uint8_t value) {
    uint8_t* byte = &plane[(uint32_t)y * row_bytes + x / 8u];
    uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
    if (value)
        *byte |= mask;
    else
        *byte &= (uint8_t)~mask;
}

static uint16_t native_scale_q8(uint8_t scale) {
    switch (scale) {
        case 2u: return 384u;
        case 3u: return 512u;
        case 4u: return 320u;
        case 5u: return 448u;
        case 1u:
        case 0u:
        default: return 256u;
    }
}

static bool render_native(capture_t* capture, display_mode_t mode,
                          uint8_t native_scale, uint32_t timestamp) {
    uint32_t plane_bytes = (uint32_t)capture->row_bytes * capture->height;
    uint8_t* render_buffer = malloc(plane_bytes * 2u);
    if (!render_buffer) return false;

    char screen_info[32] = {0};
    snprintf(screen_info, sizeof(screen_info), "%ux%u / BWR",
             capture->width, capture->height);
    gui_data_t data = {
        .mode = mode,
        .color = 2u,
        .width = capture->width,
        .height = capture->height,
        .visible_width = capture->width,
        .visible_height = capture->height,
        .native_ui_scale = native_scale,
        .timestamp = timestamp,
        .week_start = 1u,
        .temperature = 24,
        .voltage = 3010u,
        .ssid = "InkTag_3A57",
        .activated = false,
        .board_name = "gr551x_zkc42v",
        .screen_info = screen_info,
        .device_info_qr_data = {inktag_default_device_qr},
        .device_info_qr_modules = {INKTAG_DEFAULT_DEVICE_QR_MODULES},
    };
    gui_options_t options = {
        .color_plane = GFX_COLOR_PLANE_ALL,
        .prefer_full_frame = true,
        .render_buffer = render_buffer,
        .render_buffer_size = plane_bytes * 2u,
    };
    capture->next_y = 0u;
    capture->failed = false;
    bool ok = DrawGUIEx(&data, capture_frame, capture, &options) &&
              !capture->failed && capture->next_y == capture->height;
    if (!ok) {
        fprintf(stderr, "native capture failed: failed=%u rows=%u/%u bytes=%u buffer=%lu\n",
                capture->failed, capture->next_y, capture->height,
                capture->row_bytes, (unsigned long)(plane_bytes * 2u));
    }
    free(render_buffer);
    return ok;
}

static bool render_default_mark(capture_t* target, uint8_t native_scale,
                             display_mode_t mode,
                             int16_t* mark_x, int16_t* mark_y,
                             int16_t* mark_width, int16_t* mark_height,
                             int16_t* changed_x, int16_t* changed_y,
                             int16_t* changed_width, int16_t* changed_height) {
    uint32_t plane_bytes = (uint32_t)target->row_bytes * target->height;
    uint8_t* mark_black = malloc(plane_bytes);
    uint8_t* mark_red = malloc(plane_bytes);
    uint8_t* render_buffer = malloc(plane_bytes * 2u);
    if (!mark_black || !mark_red || !render_buffer) {
        free(mark_black);
        free(mark_red);
        free(render_buffer);
        return false;
    }
    memset(mark_black, 0xff, plane_bytes);
    memset(mark_red, 0xff, plane_bytes);
    capture_t mark = {
        .width = target->width,
        .height = target->height,
        .row_bytes = target->row_bytes,
        .black = mark_black,
        .red = mark_red,
    };

    Adafruit_GFX gfx;
    GFX_begin_3c_with_buffer(&gfx, target->width, target->height,
                             (int16_t)(target->height * 2u), render_buffer,
                             plane_bytes * 2u);
    if (!gfx.buffer) {
        free(mark_black);
        free(mark_red);
        free(render_buffer);
        return false;
    }
    GFX_firstPage(&gfx);
    GFX_fillScreen(&gfx, GFX_WHITE);
    GFX_setFont(&gfx, u8g2_font_wqy9_t_lunar);
    uint16_t mark_scale_q8 = native_scale_q8(native_scale);
    GFX_setFontScale(&gfx, mark_scale_q8);
    GFX_setFontMode(&gfx, 1u);
    GFX_setTextColor(&gfx, GFX_BLACK, GFX_WHITE);

    bool compact = target->width <= 384u && target->height <= 200u;
    bool compact_clock = compact && mode == MODE_CLOCK;
    int16_t margin = compact ? 1 : (mark_scale_q8 >= 384u ? 6 : 3);
    int16_t horizontal_padding = compact ? 3 : 4;
    int16_t vertical_padding = compact ? 1 : 3;
    int16_t text_width = GFX_getUTF8Width(&gfx, "InkTag base");
    int16_t font_height = GFX_getFontHeight(&gfx);
    int16_t box_width = text_width + horizontal_padding * 2;
    int16_t box_height = font_height + vertical_padding * 2;
    int16_t clock_pill_height = target->height <= 112u ? 17 : 20;
    int16_t maximum_box_height = compact_clock ? clock_pill_height : target->height;
    while ((box_width + margin * 2 > target->width ||
            box_height + margin * 2 > target->height ||
            box_height > maximum_box_height) &&
           mark_scale_q8 > 128u) {
        mark_scale_q8 -= 16u;
        GFX_setFontScale(&gfx, mark_scale_q8);
        margin = compact ? 1 : (mark_scale_q8 >= 384u ? 6 : 3);
        text_width = GFX_getUTF8Width(&gfx, "InkTag base");
        font_height = GFX_getFontHeight(&gfx);
        box_width = text_width + horizontal_padding * 2;
        box_height = font_height + vertical_padding * 2;
    }
    int16_t x = (int16_t)target->width - box_width - margin;
    int16_t y = (int16_t)target->height - box_height - margin;
    if (x < 1) x = 1;
    if (y < 1) y = 1;

    int16_t affected_x = x;
    int16_t affected_y = y;
    int16_t affected_width = box_width;
    int16_t affected_height = box_height;
    if (compact_clock) {
        int16_t footer_y = (int16_t)target->height -
                           (target->height <= 112u ? 24 : 30);
        int16_t rule_y = footer_y - 3;
        int16_t pill_y = footer_y +
                         ((int16_t)target->height - footer_y -
                          clock_pill_height) /
                             2;
        y = pill_y + (clock_pill_height - box_height) / 2;
        x = (int16_t)target->width - box_width - margin;

        GFX_fillRect(&gfx, 0, rule_y, target->width,
                     (int16_t)target->height - rule_y, GFX_WHITE);
        GFX_drawFastHLine(&gfx, 0, rule_y, target->width, GFX_BLACK);
        int16_t pill_width = x - 9;
        if (pill_width > 16) {
            GFX_fillRoundRect(&gfx, 6, pill_y, pill_width,
                              clock_pill_height, 5, GFX_RED);
            GFX_setFontScale(&gfx, mark_scale_q8);
            GFX_setTextColor(&gfx, GFX_WHITE, GFX_RED);
            const char* footer = "星期一 七月廿六";
            if (GFX_getUTF8Width(&gfx, footer) > pill_width - 4)
                footer = "星期一";
            if (GFX_getUTF8Width(&gfx, footer) > pill_width - 4)
                footer = "周一";
            GFX_setCursor(&gfx,
                          6 + (pill_width - GFX_getUTF8Width(&gfx, footer)) / 2,
                          pill_y + (clock_pill_height - font_height) / 2 +
                              GFX_getFontAscent(&gfx));
            GFX_printf(&gfx, "%s", footer);
        }
        affected_x = 0;
        affected_y = rule_y;
        affected_width = target->width;
        affected_height = (int16_t)target->height - rule_y;
    }

    GFX_fillRect(&gfx, x, y, box_width, box_height, GFX_WHITE);
    GFX_drawRect(&gfx, x, y, box_width, box_height, GFX_BLACK);
    GFX_setTextColor(&gfx, GFX_BLACK, GFX_WHITE);
    GFX_setCursor(&gfx, x + horizontal_padding,
                  y + vertical_padding + GFX_getFontAscent(&gfx));
    GFX_printf(&gfx, "InkTag base");
    (void)GFX_nextPage(&gfx, capture_frame, &mark);
    GFX_end(&gfx);

    if (mark.failed || mark.next_y != target->height) {
        free(mark_black);
        free(mark_red);
        free(render_buffer);
        return false;
    }
    for (int16_t py = affected_y;
         py < affected_y + affected_height && py < target->height; py++) {
        for (int16_t px = affected_x;
             px < affected_x + affected_width && px < target->width; px++) {
            set_plane_bit(target->black, target->row_bytes, (uint16_t)px,
                          (uint16_t)py,
                          plane_bit(mark.black, mark.row_bytes, (uint16_t)px,
                                    (uint16_t)py));
            set_plane_bit(target->red, target->row_bytes, (uint16_t)px,
                          (uint16_t)py,
                          plane_bit(mark.red, mark.row_bytes, (uint16_t)px,
                                    (uint16_t)py));
        }
    }
    *mark_x = x;
    *mark_y = y;
    *mark_width = box_width;
    *mark_height = box_height;
    *changed_x = affected_x;
    *changed_y = affected_y;
    *changed_width = affected_width;
    *changed_height = affected_height;
    free(mark_black);
    free(mark_red);
    free(render_buffer);
    return true;
}

static bool write_ppm(const char* path, const capture_t* capture) {
    FILE* output = fopen(path, "wb");
    if (!output) return false;
    fprintf(output, "P6\n%u %u\n255\n", capture->width, capture->height);
    for (uint16_t y = 0u; y < capture->height; y++) {
        for (uint16_t x = 0u; x < capture->width; x++) {
            uint8_t pixel[3] = {255u, 255u, 255u};
            if (!plane_bit(capture->red, capture->row_bytes, x, y)) {
                pixel[0] = 200u;
                pixel[1] = 45u;
                pixel[2] = 40u;
            } else if (!plane_bit(capture->black, capture->row_bytes, x, y)) {
                pixel[0] = 23u;
                pixel[1] = 26u;
                pixel[2] = 24u;
            }
            if (fwrite(pixel, sizeof(pixel), 1u, output) != 1u) {
                fclose(output);
                return false;
            }
        }
    }
    return fclose(output) == 0;
}

static display_mode_t parse_mode(const char* value) {
    if (strcmp(value, "calendar") == 0) return MODE_CALENDAR;
    if (strcmp(value, "clock") == 0) return MODE_CLOCK;
    if (strcmp(value, "device-info") == 0) return MODE_DEVICE_INFO;
    if (strcmp(value, "low-battery") == 0) return MODE_LOW_BATTERY;
    return MODE_PICTURE;
}

int main(int argc, char** argv) {
    if (argc != 7 && argc != 8) {
        fprintf(stderr,
                "usage: %s WIDTH HEIGHT device_info|calendar|clock|low-battery SCALE_ENUM "
                "FREE_MARK OUTPUT.ppm [TIMESTAMP]\n",
                argv[0]);
        return 2;
    }
    long width = strtol(argv[1], NULL, 10);
    long height = strtol(argv[2], NULL, 10);
    display_mode_t mode = parse_mode(argv[3]);
    long native_scale = strtol(argv[4], NULL, 10);
    bool default_mark = strtol(argv[5], NULL, 10) != 0;
    if (width < 32 || width > 2048 || height < 32 || height > 2048 ||
        mode == MODE_PICTURE || native_scale < 0 || native_scale > 5) {
        fprintf(stderr, "invalid render arguments\n");
        return 2;
    }

    capture_t capture = {
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .row_bytes = (uint16_t)((width + 7) / 8),
    };
    uint32_t plane_bytes = (uint32_t)capture.row_bytes * capture.height;
    capture.black = malloc(plane_bytes);
    capture.red = malloc(plane_bytes);
    if (!capture.black || !capture.red) {
        fprintf(stderr, "frame allocation failed\n");
        free(capture.black);
        free(capture.red);
        return 1;
    }
    memset(capture.black, 0xff, plane_bytes);
    memset(capture.red, 0xff, plane_bytes);
    uint32_t timestamp = argc == 8 ? (uint32_t)strtoul(argv[7], NULL, 10) : 1788791280u;
    if (!render_native(&capture, mode, (uint8_t)native_scale, timestamp)) {
        fprintf(stderr, "DrawGUIEx failed\n");
        free(capture.black);
        free(capture.red);
        return 1;
    }

    int16_t mark_x = 0;
    int16_t mark_y = 0;
    int16_t mark_width = 0;
    int16_t mark_height = 0;
    int16_t changed_x = 0;
    int16_t changed_y = 0;
    int16_t changed_width = 0;
    int16_t changed_height = 0;
    if (default_mark &&
        !render_default_mark(&capture, (uint8_t)native_scale, mode, &mark_x,
                          &mark_y, &mark_width, &mark_height, &changed_x,
                          &changed_y, &changed_width, &changed_height)) {
        fprintf(stderr, "base mark render failed\n");
        free(capture.black);
        free(capture.red);
        return 1;
    }
    if (!write_ppm(argv[6], &capture)) {
        fprintf(stderr, "failed to write %s\n", argv[6]);
        free(capture.black);
        free(capture.red);
        return 1;
    }
    printf("{\"mark\":[%d,%d,%d,%d],\"changed\":[%d,%d,%d,%d]}\n",
           mark_x, mark_y, mark_width, mark_height, changed_x, changed_y,
           changed_width, changed_height);
    free(capture.black);
    free(capture.red);
    return 0;
}
