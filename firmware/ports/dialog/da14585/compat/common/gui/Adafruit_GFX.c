/* DA normal profile: E6 implementations are not included. */
#if INKTAG_EPD_ENABLE_JD79665CA || INKTAG_EPD_ENABLE_OSPREY_E6
#error DA_source_source_requires_E6_disabled
#endif
/*
This is the core graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.).  It needs to be
paired with a hardware-specific library for each display device we carry
(to handle the lower-level functions).

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
 */

#include "Adafruit_GFX.h"

#if defined(INKTAG_GUI_COOPERATIVE_YIELD)
extern void inktag_gui_cooperative_yield(void);
#if !defined(INKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY)
static uint16_t s_cooperative_yield_pixels;
#endif
#elif defined(INKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY)
#error "INKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY requires INKTAG_GUI_COOPERATIVE_YIELD"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef ABS
#define ABS(x) ((x) > 0 ? (x) : -(x))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef SWAP
#define SWAP(a, b, T) \
    do {              \
        T t = a;      \
        a = b;        \
        b = t;        \
    } while (0)
#endif
#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, member) (type*)((char*)ptr - offsetof(type, member))
#endif

static int16_t GFX_scale_floor(int16_t delta, uint16_t scale) {
    int32_t value = (int32_t)delta * scale;
#if defined(INKTAG_DIALOG)
    _Static_assert((-1 >> 1) == -1, "DA raster needs arithmetic signed shift");
    return (int16_t)(value >> 8);
#else
    return value >= 0 ? (int16_t)(value / 256) : (int16_t)-((-value + 255) / 256);
#endif
}

static int16_t GFX_scale_ceil(int16_t delta, uint16_t scale) {
    int32_t value = (int32_t)delta * scale;
#if defined(INKTAG_DIALOG)
    /* int16*uint16 +255 remains within int32; this is signed ceil /256. */
    return (int16_t)((value + 255) >> 8);
#else
    return value >= 0 ? (int16_t)((value + 255) / 256) : (int16_t)-((-value) / 256);
#endif
}

static void GFX_u8g2_draw_hv_line(u8g2_font_t* u8g2, int16_t x, int16_t y, int16_t len, uint8_t dir, uint16_t color) {
    Adafruit_GFX* gfx = CONTAINER_OF(u8g2, Adafruit_GFX, u8g2);
    uint16_t scale = gfx->font_scale_q8 ? gfx->font_scale_q8 : 256u;
    if (scale != 256u) {
        int16_t x0 = x;
        int16_t y0 = y;
        int16_t x1 = x + 1;
        int16_t y1 = y + 1;
        if (dir == 0u) x1 = x + len;
        else if (dir == 1u) y1 = y + len;
        else if (dir == 2u) x0 = x - len + 1;
        else y0 = y - len + 1;
        x0 = gfx->font_origin_x + GFX_scale_floor(x0 - gfx->font_origin_x, scale);
        y0 = gfx->font_origin_y + GFX_scale_floor(y0 - gfx->font_origin_y, scale);
        x1 = gfx->font_origin_x + GFX_scale_ceil(x1 - gfx->font_origin_x, scale);
        y1 = gfx->font_origin_y + GFX_scale_ceil(y1 - gfx->font_origin_y, scale);
        GFX_fillRect(gfx, x0, y0, x1 - x0, y1 - y0, color);
        return;
    }
    switch (dir) {
        case 0:
            GFX_drawFastHLine(gfx, x, y, len, color);
            break;
        case 1:
            GFX_drawFastVLine(gfx, x, y, len, color);
            break;
        case 2:
            GFX_drawFastHLine(gfx, x - len + 1, y, len, color);
            break;
        case 3:
            GFX_drawFastVLine(gfx, x, y - len + 1, len, color);
            break;
    }
}

/**************************************************************************/
/*!
   @brief    Instatiate a GFX context for graphics
   @param    w   Display width, in pixels
   @param    h   Display height, in pixels
   @param    buffer_height Page buffer height
*/
/**************************************************************************/
static void GFX_begin_internal(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height,
                               uint8_t* external_buffer, uint32_t external_buffer_size) {
    memset(gfx, 0, sizeof(Adafruit_GFX));
    memset(&gfx->u8g2, 0, sizeof(gfx->u8g2));
    gfx->WIDTH = gfx->_width = w;
    gfx->HEIGHT = gfx->_height = h;
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    gfx->viewport_width = w;
    gfx->viewport_height = h;
#endif
    gfx->u8g2.draw_hv_line = GFX_u8g2_draw_hv_line;
    gfx->font_scale_q8 = 256u;
    uint16_t row_bytes = (gfx->WIDTH + 7) / 8;
    if (external_buffer) {
        uint32_t available_height = external_buffer_size / row_bytes;
        if (available_height > INT16_MAX) available_height = INT16_MAX;
        if ((uint32_t)buffer_height > available_height) buffer_height = (int16_t)available_height;
        if (buffer_height > 0) gfx->buffer = external_buffer;
    }
#if !defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
    else {
        while (buffer_height > 0) {
            gfx->buffer = malloc(row_bytes * buffer_height);
            if (gfx->buffer) {
                gfx->buffer_owned = true;
                break;
            }
            buffer_height /= 2;
        }
    }
#endif
    gfx->page_height = gfx->buffer ? buffer_height : 0;
    gfx->total_pages = gfx->page_height > 0 ? (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0) : 0;
    GFX_setWindow(gfx, 0, 0, gfx->WIDTH, gfx->HEIGHT);
}

void GFX_begin(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    GFX_begin_internal(gfx, w, h, buffer_height, NULL, 0);
}

void GFX_begin_with_buffer(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height,
                           uint8_t* buffer, uint32_t buffer_size) {
    GFX_begin_internal(gfx, w, h, buffer_height, buffer, buffer_size);
}

/**************************************************************************/
/*!
   @brief    Instatiate a 3-color GFX context for graphics
   @param    w   Display width, in pixels
   @param    h   Display height, in pixels
   @param    buffer_height Page buffer height, should be multiple of 2
*/
/**************************************************************************/
void GFX_begin_3c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    if (buffer_height < 2) buffer_height = 2;
    if (buffer_height & 1) buffer_height--;
    GFX_begin(gfx, w, h, buffer_height);
    if (gfx->buffer == NULL || gfx->page_height < 2) {
        GFX_end(gfx);
        memset(gfx, 0, sizeof(Adafruit_GFX));
        return;
    }
    gfx->page_height /= 2;
    gfx->color = gfx->buffer + ((gfx->WIDTH + 7) / 8) * gfx->page_height;
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

void GFX_begin_3c_with_buffer(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height,
                              uint8_t* buffer, uint32_t buffer_size) {
    if (buffer_height < 2) buffer_height = 2;
    if (buffer_height & 1) buffer_height--;
    GFX_begin_with_buffer(gfx, w, h, buffer_height, buffer, buffer_size);
    if (gfx->buffer == NULL || gfx->page_height < 2) {
        GFX_end(gfx);
        memset(gfx, 0, sizeof(Adafruit_GFX));
        return;
    }
    gfx->page_height /= 2;
    gfx->color = gfx->buffer + ((gfx->WIDTH + 7) / 8) * gfx->page_height;
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

/**************************************************************************/
/*!
   @brief    Instatiate a 4-color GFX context for graphics
   @param    w   Display width, in pixels
   @param    h   Display height, in pixels
   @param    buffer_height Page buffer height, should be multiple of 2
*/
/**************************************************************************/
void GFX_begin_4c(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height) {
    if (buffer_height < 2) buffer_height = 2;
    if (buffer_height & 1) buffer_height--;
    GFX_begin(gfx, w, h, buffer_height);
    if (gfx->buffer == NULL || gfx->page_height < 2) {
        GFX_end(gfx);
        memset(gfx, 0, sizeof(Adafruit_GFX));
        return;
    }
    gfx->page_height /= 2;
    gfx->color = gfx->buffer;
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

void GFX_begin_4c_with_buffer(Adafruit_GFX* gfx, int16_t w, int16_t h, int16_t buffer_height,
                              uint8_t* buffer, uint32_t buffer_size) {
    if (buffer_height < 2) buffer_height = 2;
    if (buffer_height & 1) buffer_height--;
    GFX_begin_with_buffer(gfx, w, h, buffer_height, buffer, buffer_size);
    if (gfx->buffer == NULL || gfx->page_height < 2) {
        GFX_end(gfx);
        memset(gfx, 0, sizeof(Adafruit_GFX));
        return;
    }
    gfx->page_height /= 2;
    gfx->color = gfx->buffer;
    gfx->total_pages = (gfx->HEIGHT / gfx->page_height) + (gfx->HEIGHT % gfx->page_height > 0);
}

void GFX_end(Adafruit_GFX* gfx) {
#if !defined(INKTAG_GUI_STATIC_BUFFER_ONLY)
    if (gfx->buffer && gfx->buffer_owned) free(gfx->buffer);
#endif
    gfx->buffer = NULL;
    gfx->color = NULL;
    gfx->buffer_owned = false;
}

void GFX_firstPage(Adafruit_GFX* gfx) {
    if (gfx->buffer == NULL || gfx->page_height <= 0) return;
    GFX_fillScreen(gfx, GFX_WHITE);
    gfx->current_page = 0;
}

bool GFX_nextPage(Adafruit_GFX* gfx, buffer_callback callback, void* user_data) {
    if (gfx->buffer == NULL || gfx->page_height <= 0 || gfx->total_pages <= 0) return false;
    if (callback) {
        int16_t page_ys = gfx->current_page * gfx->page_height;
        if (gfx->px != 0 || gfx->py != 0 || gfx->pw != gfx->_width || gfx->ph != gfx->_height) {
            int16_t page_ye = gfx->current_page < gfx->total_pages - 1 ? page_ys + gfx->page_height : gfx->HEIGHT;
            uint16_t dest_ys = gfx->py + page_ys;  // transposed
            uint16_t dest_ye = MIN(gfx->py + gfx->ph, gfx->py + page_ye);
            if (dest_ye > dest_ys)
                callback(user_data, gfx->buffer, gfx->color, gfx->px, dest_ys, gfx->pw, dest_ye - dest_ys);
        } else {
            int16_t height = MIN(gfx->page_height, gfx->HEIGHT - page_ys);
            callback(user_data, gfx->buffer, gfx->color, 0, page_ys, gfx->WIDTH, height);
        }
    }

#if defined(INKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY)
    /* A sparse page can contain fewer than the pixel-period service budget.
     * Tiny-page targets opt in to one BLE/watchdog opportunity after every
     * emitted page instead of perturbing the established full-frame cadence. */
    inktag_gui_cooperative_yield();
#endif

    gfx->current_page++;
    GFX_fillScreen(gfx, GFX_WHITE);

    return gfx->current_page < gfx->total_pages;
}

/**************************************************************************/
/*!
    @brief      Set rotation setting for display
    @param  r   0 thru 3 corresponding to 4 cardinal rotations
*/
/**************************************************************************/
void GFX_setRotation(Adafruit_GFX* gfx, GFX_Rotate r) {
    gfx->rotation = r;
    switch (gfx->rotation) {
        case GFX_ROTATE_0:
        case GFX_ROTATE_180:
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            gfx->_width = gfx->viewport_width;
            gfx->_height = gfx->viewport_height;
#else
            gfx->_width = gfx->WIDTH;
            gfx->_height = gfx->HEIGHT;
#endif
            break;
        case GFX_ROTATE_90:
        case GFX_ROTATE_270:
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            gfx->_width = gfx->viewport_height;
            gfx->_height = gfx->viewport_width;
#else
            gfx->_width = gfx->HEIGHT;
            gfx->_height = gfx->WIDTH;
#endif
            break;
    }
}

#if defined(INKTAG_GUI_RAW_VIEWPORT)
void GFX_setRawViewport(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x > gfx->WIDTH) x = gfx->WIDTH;
    if (y > gfx->HEIGHT) y = gfx->HEIGHT;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (x + w > gfx->WIDTH) w = gfx->WIDTH - x;
    if (y + h > gfx->HEIGHT) h = gfx->HEIGHT - y;

    gfx->viewport_x = x;
    gfx->viewport_y = y;
    gfx->viewport_width = w;
    gfx->viewport_height = h;
    GFX_setRotation(gfx, gfx->rotation);
}
#endif

void GFX_setMirrorX(Adafruit_GFX* gfx, bool mirror) { gfx->mirror_x = mirror; }
void GFX_setMirrorY(Adafruit_GFX* gfx, bool mirror) { gfx->mirror_y = mirror; }

void GFX_setColorPlane(Adafruit_GFX* gfx, GFX_ColorPlane plane) { gfx->color_plane = plane; }

/**************************************************************************/
/*!
    setWindow, use parameters according to actual rotation.
    x and w should be multiple of 8, for rotation 0 or 2,
    y and h should be multiple of 8, for rotation 1 or 3,
    else window is increased as needed,
    this is an addressing limitation of the e-paper controllers
*/
/**************************************************************************/
void GFX_setWindow(Adafruit_GFX* gfx, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    gfx->px = MIN(x, gfx->_width);
    gfx->py = MIN(y, gfx->_height);
    gfx->pw = MIN(w, gfx->_width - gfx->px);
    gfx->ph = MIN(h, gfx->_height - gfx->py);

    switch (gfx->rotation) {
        case GFX_ROTATE_0:
            break;
        case GFX_ROTATE_90:
            SWAP(gfx->px, gfx->py, uint16_t);
            SWAP(gfx->pw, gfx->ph, uint16_t);
            gfx->px = gfx->WIDTH - gfx->px - gfx->pw;
            break;
        case GFX_ROTATE_180:
            gfx->px = gfx->WIDTH - gfx->px - gfx->pw;
            gfx->py = gfx->HEIGHT - gfx->py - gfx->ph;
            break;
        case GFX_ROTATE_270:
            SWAP(gfx->px, gfx->py, uint16_t);
            SWAP(gfx->pw, gfx->ph, uint16_t);
            gfx->py = gfx->HEIGHT - gfx->py - gfx->ph;
            break;
    }

    gfx->pw += gfx->px % 8;
    if (gfx->pw % 8 > 0) gfx->pw += 8 - (gfx->pw % 8);
    gfx->px -= gfx->px % 8;
}

static uint8_t color4(const Adafruit_GFX* gfx, uint16_t color) {
    (void)gfx;

    static uint16_t _prev_color = GFX_BLACK;
    static uint8_t _prev_color4 = 0x00;  // black
    if (color == _prev_color) return _prev_color4;
    uint8_t cv4 = 0x00;
    switch (color) {
        case GFX_BLACK:
            cv4 = 0x00;
            break;
        case GFX_WHITE:
            cv4 = 0x01;
            break;
        case GFX_GREEN:
            cv4 = 0x02;
            break;  // use yellow?
        case GFX_BLUE:
            cv4 = 0x00;
            break;  // use black
        case GFX_RED:
            cv4 = 0x03;
            break;
        case GFX_YELLOW:
            cv4 = 0x02;
            break;
        case GFX_ORANGE:
            cv4 = 0x02;
            break;  // use yellow?
        default: {
            uint16_t red = color & 0xF800;
            uint16_t green = (color & 0x07E0) << 5;
            uint16_t blue = (color & 0x001F) << 11;
            if ((red < 0x8000) && (green < 0x8000) && (blue < 0x8000))
                cv4 = 0x00;  // black
            else if ((red >= 0x8000) && (green >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x01;  // white
            else if ((red >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x03;  //  red, blue > red
            else if ((green >= 0x8000) && (blue >= 0x8000))
                cv4 = 0x01;  //  green, blue > white
            else if ((red >= 0x8000) && (green >= 0xC000))
                cv4 = 0x02;  // yellow
            else if ((red >= 0x8000) && (green >= 0x4000))
                cv4 = 0x03;  // orange > red
            else if (red >= 0x8000)
                cv4 = 0x03;  // red
            else if (green >= 0x8000)
                cv4 = 0x00;  // green > black
            else
                cv4 = 0x03;  // blue
        } break;
    }
    _prev_color = color;
    _prev_color4 = cv4;
    return cv4;
}
/**************************************************************************/
/*!
   @brief    Draw a pixel
    @param   x   x coordinate
    @param   y   y coordinate
   @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_drawPixel(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t color) {
    if (gfx->buffer == NULL || gfx->page_height <= 0) return;
    if (x < 0 || x >= gfx->_width || y < 0 || y >= gfx->_height) return;

    switch (gfx->rotation) {
        case GFX_ROTATE_0:
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            x += gfx->viewport_x;
            y += gfx->viewport_y;
#endif
            break;
        case GFX_ROTATE_90:
            SWAP(x, y, int16_t);
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            x = gfx->viewport_x + gfx->viewport_width - x - 1;
            y += gfx->viewport_y;
#else
            x = gfx->WIDTH - x - 1;
#endif
            break;
        case GFX_ROTATE_180:
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            x = gfx->viewport_x + gfx->viewport_width - x - 1;
            y = gfx->viewport_y + gfx->viewport_height - y - 1;
#else
            x = gfx->WIDTH - x - 1;
            y = gfx->HEIGHT - y - 1;
#endif
            break;
        case GFX_ROTATE_270:
            SWAP(x, y, int16_t);
#if defined(INKTAG_GUI_RAW_VIEWPORT)
            x += gfx->viewport_x;
            y = gfx->viewport_y + gfx->viewport_height - y - 1;
#else
            y = gfx->HEIGHT - y - 1;
#endif
            break;
    }

    /* Protocol order is rotation first, then X/Y mirrors in the rotated
     * raw-panel coordinates. Preserve both bits independently. */
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    if (gfx->mirror_x) x = gfx->viewport_x + gfx->viewport_width - (x - gfx->viewport_x) - 1;
    if (gfx->mirror_y) y = gfx->viewport_y + gfx->viewport_height - (y - gfx->viewport_y) - 1;
#else
    if (gfx->mirror_x) x = gfx->WIDTH - x - 1;
    if (gfx->mirror_y) y = gfx->HEIGHT - y - 1;
#endif

    // transpose partial window to 0,0
    x -= gfx->px;
    y -= gfx->py;
    // clip to (partial) window
    if (x < 0 || x >= gfx->pw || y < 0 || y >= gfx->ph) return;
    // adjust for current page
    y -= gfx->current_page * gfx->page_height;
    // check if in current page
    if (y < 0 || y >= gfx->page_height) return;

#if defined(INKTAG_GUI_COOPERATIVE_YIELD) && \
    !defined(INKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY)
    /* Count pixels that actually land in this page. Full-frame and ordinary
     * paged targets retain the established 512-pixel radio/watchdog cadence;
     * only an explicitly tiny-page target services once per emitted page. */
    if ((++s_cooperative_yield_pixels & 0x01FFu) == 0u) {
        inktag_gui_cooperative_yield();
    }
#endif

    if (gfx->color == gfx->buffer) {  // 4c
        uint32_t i = x / 4 + ((uint32_t)y) * (gfx->pw / 4);
        uint8_t pv = color4(gfx, color);
#if defined(INKTAG_DIALOG)
        /* Four packed positions differ only by a two-bit shift. Preserve all
         * four colours without duplicating mask/store code in SRAM. */
        uint8_t shift = (uint8_t)(6u - (uint8_t)(x & 3) * 2u);
        gfx->buffer[i] = (uint8_t)((gfx->buffer[i] & (uint8_t)~(3u << shift)) |
                                   (pv << shift));
#else
        switch (x % 4) {
            case 0:
                gfx->buffer[i] = (gfx->buffer[i] & 0x3F) | (pv << 6);
                break;
            case 1:
                gfx->buffer[i] = (gfx->buffer[i] & 0xCF) | (pv << 4);
                break;
            case 2:
                gfx->buffer[i] = (gfx->buffer[i] & 0xF3) | (pv << 2);
                break;
            case 3:
                gfx->buffer[i] = (gfx->buffer[i] & 0xFC) | pv;
                break;
        }
#endif
    } else if (gfx->color != NULL) {  // 3c
        uint16_t i = x / 8 + y * (gfx->pw / 8);
        gfx->buffer[i] |= 0x80 >> (x & 7);  // white
        gfx->color[i] |= 0x80 >> (x & 7);
        if (color == GFX_BLACK)
            gfx->buffer[i] &= ~(0x80 >> (x & 7));
        else if (color != GFX_WHITE)
            gfx->color[i] &= ~(0x80 >> (x & 7));
    } else {
        uint16_t i = x / 8 + y * (gfx->pw / 8);
        if (gfx->color_plane == GFX_COLOR_PLANE_BLACK) {
            color = color == GFX_BLACK ? GFX_BLACK : GFX_WHITE;
        } else if (gfx->color_plane == GFX_COLOR_PLANE_RED) {
            color = color == GFX_RED ? GFX_BLACK : GFX_WHITE;
        }
        if (color == GFX_WHITE)
            gfx->buffer[i] |= 0x80 >> (x & 7);
        else
            gfx->buffer[i] &= ~(0x80 >> (x & 7));
    }
}

/* Return the logical Y span that maps into the current raw framebuffer page.
 * For 0/180-degree transforms raw Y depends only on logical Y, so callers can
 * reject or trim primitives before walking pixels that cannot land in the
 * small page buffer.  The 90/270-degree cases depend on logical X instead and
 * deliberately keep the conservative per-pixel clipping path. */
static uint32_t GFX_currentPageLogicalY(const Adafruit_GFX* gfx) {
    int32_t raw_top;
    int32_t raw_bottom;
    int32_t raw_limit;
    int32_t base;
    int32_t extent;
    int16_t logical_top;
    int16_t logical_bottom;
    bool inverted;

    if (gfx->buffer == NULL || gfx->page_height <= 0 ||
        (gfx->rotation != GFX_ROTATE_0 && gfx->rotation != GFX_ROTATE_180))
        return UINT32_MAX;

    raw_top = (int32_t)gfx->py +
        (int32_t)gfx->current_page * gfx->page_height;
    raw_bottom = raw_top + gfx->page_height;
    raw_limit = (int32_t)gfx->py + gfx->ph;
    if (raw_bottom > raw_limit) raw_bottom = raw_limit;
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    base = gfx->viewport_y;
    extent = gfx->viewport_height;
#else
    base = 0;
    extent = gfx->HEIGHT;
#endif
    inverted = (gfx->rotation == GFX_ROTATE_180) != gfx->mirror_y;
    if (inverted) {
        logical_top = (int16_t)(extent - (raw_bottom - base));
        logical_bottom = (int16_t)(extent - (raw_top - base));
    } else {
        logical_top = (int16_t)(raw_top - base);
        logical_bottom = (int16_t)(raw_bottom - base);
    }
    if (logical_top < 0) logical_top = 0;
    if (logical_bottom > gfx->_height) logical_bottom = gfx->_height;
    if (logical_bottom < logical_top) logical_bottom = logical_top;
    return ((uint32_t)(uint16_t)logical_top << 16) |
           (uint16_t)logical_bottom;
}

bool GFX_pageIntersectsLogicalY(const Adafruit_GFX* gfx,
                                int16_t y, int16_t height) {
    uint32_t span = GFX_currentPageLogicalY(gfx);
    if (span == UINT32_MAX) return true;
    int16_t page_top = (int16_t)(span >> 16);
    int16_t page_bottom = (int16_t)span;
    if (height <= 0 || y >= page_bottom) return false;
    return y + height > page_top;
}

/**************************************************************************/
/*!
   @brief    Draw a line.  Bresenham's algorithm - thx wikpedia
    @param    x0  Start point x coordinate
    @param    y0  Start point y coordinate
    @param    x1  End point x coordinate
    @param    y1  End point y coordinate
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawLine(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        SWAP(x0, y0, int16_t);
        SWAP(x1, y1, int16_t);
    }

    if (x0 > x1) {
        SWAP(x0, x1, int16_t);
        SWAP(y0, y1, int16_t);
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0 <= x1; x0++) {
        if (steep) {
            GFX_drawPixel(gfx, y0, x0, color);
        } else {
            GFX_drawPixel(gfx, x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/**************************************************************************/
/*!
   @brief    Draw a dotted line.  Bresenham's algorithm - thx wikpedia
    @param    x0  Start point x coordinate
    @param    y0  Start point y coordinate
    @param    x1  End point x coordinate
    @param    y1  End point y coordinate
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawDottedLine(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color,
                        uint8_t dot_len, uint8_t space_len) {
    int16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        SWAP(x0, y0, int16_t);
        SWAP(x1, y1, int16_t);
    }

    if (x0 > x1) {
        SWAP(x0, x1, int16_t);
        SWAP(y0, y1, int16_t);
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    uint8_t draw = 1;
    uint8_t len = 0;
    for (; x0 <= x1; x0++) {
        if (draw) {
            if (steep) {
                GFX_drawPixel(gfx, y0, x0, color);
            } else {
                GFX_drawPixel(gfx, x0, y0, color);
            }
            len++;
            if (len >= dot_len) {
                len = 0;
                draw = 0;
            }
        } else {
            len++;
            if (len >= space_len) {
                len = 0;
                draw = 1;
            }
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/**************************************************************************/
/*!
   @brief    Draw a perfectly vertical line
    @param    x   Top-most x coordinate
    @param    y   Top-most y coordinate
    @param    h   Height in pixels
   @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_drawFastVLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t h, uint16_t color) {
    int16_t page_top;
    int16_t page_bottom;
    uint32_t page_span;

    if (h <= 0) return;

    // Boundary check
    if (x < 0 || x >= gfx->_width) return;
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (y + h > gfx->_height) h = gfx->_height - y;
    if (h <= 0) return;

    page_span = GFX_currentPageLogicalY(gfx);
    if (page_span != UINT32_MAX) {
        page_top = (int16_t)(page_span >> 16);
        page_bottom = (int16_t)page_span;
        if (y < page_top) {
            h -= page_top - y;
            y = page_top;
        }
        if (y + h > page_bottom) h = page_bottom - y;
        if (h <= 0) return;
    }

    for (int16_t i = 0; i < h; i++) {
        GFX_drawPixel(gfx, x, y + i, color);
    }
}

/**************************************************************************/
/*!
   @brief    Draw a perfectly horizontal line
    @param    x   Left-most x coordinate
    @param    y   Left-most y coordinate
    @param    w   Width in pixels
   @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_drawFastHLine(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, uint16_t color) {
    int16_t page_top;
    int16_t page_bottom;
    uint32_t page_span;

    if (w <= 0) return;

    // Boundary check
    if (y < 0 || y >= gfx->_height) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > gfx->_width) w = gfx->_width - x;
    if (w <= 0) return;
    page_span = GFX_currentPageLogicalY(gfx);
    if (page_span != UINT32_MAX) {
        page_top = (int16_t)(page_span >> 16);
        page_bottom = (int16_t)page_span;
        if (y < page_top || y >= page_bottom) return;
    }

    for (int16_t i = 0; i < w; i++) {
        GFX_drawPixel(gfx, x + i, y, color);
    }
}

/**************************************************************************/
/*!
   @brief    Fill a rectangle completely with one color.
    @param    x   Top left corner x coordinate
    @param    y   Top left corner y coordinate
    @param    w   Width in pixels
    @param    h   Height in pixels
   @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_fillRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = x; i < x + w; i++) {
        GFX_drawFastVLine(gfx, i, y, h, color);
    }
}

/**************************************************************************/
/*!
   @brief    Fill the screen completely with one color.
    @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_fillScreen(Adafruit_GFX* gfx, uint16_t color) {
    if (gfx->buffer == NULL || gfx->page_height <= 0) return;
    uint32_t size = ((gfx->WIDTH + 7) / 8) * gfx->page_height;
    if (gfx->color == gfx->buffer) {        // 4c
        uint8_t pv = color4(gfx, color) * 0x55;  // 0b01010101
        memset(gfx->buffer, pv, size * 2);
    } else {
        memset(gfx->buffer, color == GFX_WHITE ? 0xFF : 0x00, size);
        if (gfx->color != NULL) memset(gfx->color, color == GFX_RED ? 0x00 : 0xFF, size);
    }
}

/**************************************************************************/
/*!
   @brief    Draw a circle outline
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    r   Radius of circle
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    GFX_drawPixel(gfx, x0, y0 + r, color);
    GFX_drawPixel(gfx, x0, y0 - r, color);
    GFX_drawPixel(gfx, x0 + r, y0, color);
    GFX_drawPixel(gfx, x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        GFX_drawPixel(gfx, x0 + y, y0 + x, color);
        GFX_drawPixel(gfx, x0 - y, y0 + x, color);
        GFX_drawPixel(gfx, x0 + y, y0 - x, color);
        GFX_drawPixel(gfx, x0 - y, y0 - x, color);
    }
}

/**************************************************************************/
/*!
    @brief    Quarter-circle drawer, used to do circles and roundrects
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    r   Radius of circle
    @param    cornername  Mask bit #1, #2, #4, and #8 to indicate which quarters
              of the circle we're doing
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t cornername, uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        if (cornername & 0x4) {
            GFX_drawPixel(gfx, x0 + x, y0 + y, color);
            GFX_drawPixel(gfx, x0 + y, y0 + x, color);
        }
        if (cornername & 0x2) {
            GFX_drawPixel(gfx, x0 + x, y0 - y, color);
            GFX_drawPixel(gfx, x0 + y, y0 - x, color);
        }
        if (cornername & 0x8) {
            GFX_drawPixel(gfx, x0 - y, y0 + x, color);
            GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        }
        if (cornername & 0x1) {
            GFX_drawPixel(gfx, x0 - y, y0 - x, color);
            GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        }
    }
}

/**************************************************************************/
/*!
   @brief    Draw a circle with filled color
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    r   Radius of circle
    @param    color 16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_fillCircle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    GFX_drawFastVLine(gfx, x0, y0 - r, 2 * r + 1, color);
    GFX_fillCircleHelper(gfx, x0, y0, r, 3, 0, color);
}

/**************************************************************************/
/*!
    @brief  Half-circle drawer with fill, used for circles and roundrects
    @param  x0       Center-point x coordinate
    @param  y0       Center-point y coordinate
    @param  r        Radius of circle
    @param  corners  Mask bits indicating which sides of the circle we are
                     doing, left (1) and/or right (2)
    @param  delta    Offset from center-point, used for round-rects
    @param  color    16-bit 5-6-5 Color to fill with
*/
/**************************************************************************/
void GFX_fillCircleHelper(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t r, uint8_t corners, int16_t delta,
                          uint16_t color) {
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;
    int16_t px = x;
    int16_t py = y;

    delta++;  // Avoid some +1's in the loop

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        // These checks avoid double-drawing certain lines, important
        // for the SSD1306 library which has an INVERT drawing mode.
        if (x < (y + 1)) {
            if (corners & 1) GFX_drawFastVLine(gfx, x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2) GFX_drawFastVLine(gfx, x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {
            if (corners & 1) GFX_drawFastVLine(gfx, x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2) GFX_drawFastVLine(gfx, x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}

/**************************************************************************/
/*!
   @brief    Draw an ellipse outline
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    rw   Horizontal radius of ellipse
    @param    rh   Vertical radius of ellipse
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color) {
    // Bresenham's ellipse algorithm
    int16_t x = 0, y = rh;
    int32_t rw2 = rw * rw, rh2 = rh * rh;
    int32_t twoRw2 = 2 * rw2, twoRh2 = 2 * rh2;

    int32_t decision = rh2 - (rw2 * rh) + (rw2 / 4);

    // region 1
    while ((twoRh2 * x) < (twoRw2 * y)) {
        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        x++;
        if (decision < 0) {
            decision += rh2 + (twoRh2 * x);
        } else {
            decision += rh2 + (twoRh2 * x) - (twoRw2 * y);
            y--;
        }
    }

    // region 2
    decision = ((rh2 * (2 * x + 1) * (2 * x + 1)) >> 2) + (rw2 * (y - 1) * (y - 1)) - (rw2 * rh2);
    while (y >= 0) {
        GFX_drawPixel(gfx, x0 + x, y0 + y, color);
        GFX_drawPixel(gfx, x0 - x, y0 + y, color);
        GFX_drawPixel(gfx, x0 + x, y0 - y, color);
        GFX_drawPixel(gfx, x0 - x, y0 - y, color);
        y--;
        if (decision > 0) {
            decision += rw2 - (twoRw2 * y);
        } else {
            decision += rw2 + (twoRh2 * x) - (twoRw2 * y);
            x++;
        }
    }
}

/**************************************************************************/
/*!
   @brief    Draw an ellipse with filled colour
    @param    x0   Center-point x coordinate
    @param    y0   Center-point y coordinate
    @param    rw   Horizontal radius of ellipse
    @param    rh   Vertical radius of ellipse
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_fillEllipse(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t rw, int16_t rh, uint16_t color) {
    // Bresenham's ellipse algorithm
    int16_t x = 0, y = rh;
    int32_t rw2 = rw * rw, rh2 = rh * rh;
    int32_t twoRw2 = 2 * rw2, twoRh2 = 2 * rh2;

    int32_t decision = rh2 - (rw2 * rh) + (rw2 / 4);

    // region 1
    while ((twoRh2 * x) < (twoRw2 * y)) {
        x++;
        if (decision < 0) {
            decision += rh2 + (twoRh2 * x);
        } else {
            decision += rh2 + (twoRh2 * x) - (twoRw2 * y);
            GFX_drawFastHLine(gfx, x0 - (x - 1), y0 + y, 2 * (x - 1) + 1, color);
            GFX_drawFastHLine(gfx, x0 - (x - 1), y0 - y, 2 * (x - 1) + 1, color);
            y--;
        }
    }

    // region 2
    decision = ((rh2 * (2 * x + 1) * (2 * x + 1)) >> 2) + (rw2 * (y - 1) * (y - 1)) - (rw2 * rh2);
    while (y >= 0) {
        GFX_drawFastHLine(gfx, x0 - x, y0 + y, 2 * x + 1, color);
        GFX_drawFastHLine(gfx, x0 - x, y0 - y, 2 * x + 1, color);
        y--;
        if (decision > 0) {
            decision += rw2 - (twoRw2 * y);
        } else {
            decision += rw2 + (twoRh2 * x) - (twoRw2 * y);
            x++;
        }
    }
}

/**************************************************************************/
/*!
   @brief   Draw a rectangle with no fill color
    @param    x   Top left corner x coordinate
    @param    y   Top left corner y coordinate
    @param    w   Width in pixels
    @param    h   Height in pixels
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    GFX_drawFastHLine(gfx, x, y, w, color);
    GFX_drawFastHLine(gfx, x, y + h - 1, w, color);
    GFX_drawFastVLine(gfx, x, y, h, color);
    GFX_drawFastVLine(gfx, x + w - 1, y, h, color);
}

/**************************************************************************/
/*!
   @brief   Draw a rounded rectangle with no fill color
    @param    x   Top left corner x coordinate
    @param    y   Top left corner y coordinate
    @param    w   Width in pixels
    @param    h   Height in pixels
    @param    r   Radius of corner rounding
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawRoundRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    int16_t max_radius = ((w < h) ? w : h) / 2;  // 1/2 minor axis
    if (r > max_radius) r = max_radius;
    // smarter version
    GFX_drawFastHLine(gfx, x + r, y, w - 2 * r, color);          // Top
    GFX_drawFastHLine(gfx, x + r, y + h - 1, w - 2 * r, color);  // Bottom
    GFX_drawFastVLine(gfx, x, y + r, h - 2 * r, color);          // Left
    GFX_drawFastVLine(gfx, x + w - 1, y + r, h - 2 * r, color);  // Right
    // draw four corners
    GFX_drawCircleHelper(gfx, x + r, y + r, r, 1, color);
    GFX_drawCircleHelper(gfx, x + w - r - 1, y + r, r, 2, color);
    GFX_drawCircleHelper(gfx, x + w - r - 1, y + h - r - 1, r, 4, color);
    GFX_drawCircleHelper(gfx, x + r, y + h - r - 1, r, 8, color);
}

/**************************************************************************/
/*!
   @brief   Draw a rounded rectangle with fill color
    @param    x   Top left corner x coordinate
    @param    y   Top left corner y coordinate
    @param    w   Width in pixels
    @param    h   Height in pixels
    @param    r   Radius of corner rounding
    @param    color 16-bit 5-6-5 Color to draw/fill with
*/
/**************************************************************************/
void GFX_fillRoundRect(Adafruit_GFX* gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
    int16_t max_radius = ((w < h) ? w : h) / 2;  // 1/2 minor axis
    if (r > max_radius) r = max_radius;
    // smarter version
    GFX_fillRect(gfx, x + r, y, w - 2 * r, h, color);
    // draw four corners
    GFX_fillCircleHelper(gfx, x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
    GFX_fillCircleHelper(gfx, x + r, y + r, r, 2, h - 2 * r - 1, color);
}

/**************************************************************************/
/*!
   @brief   Draw a triangle with no fill color
    @param    x0  Vertex #0 x coordinate
    @param    y0  Vertex #0 y coordinate
    @param    x1  Vertex #1 x coordinate
    @param    y1  Vertex #1 y coordinate
    @param    x2  Vertex #2 x coordinate
    @param    y2  Vertex #2 y coordinate
    @param    color 16-bit 5-6-5 Color to draw with
*/
/**************************************************************************/
void GFX_drawTriangle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint16_t color) {
    GFX_drawLine(gfx, x0, y0, x1, y1, color);
    GFX_drawLine(gfx, x1, y1, x2, y2, color);
    GFX_drawLine(gfx, x2, y2, x0, y0, color);
}

/**************************************************************************/
/*!
   @brief     Draw a triangle with color-fill
    @param    x0  Vertex #0 x coordinate
    @param    y0  Vertex #0 y coordinate
    @param    x1  Vertex #1 x coordinate
    @param    y1  Vertex #1 y coordinate
    @param    x2  Vertex #2 x coordinate
    @param    y2  Vertex #2 y coordinate
    @param    color 16-bit 5-6-5 Color to fill/draw with
*/
/**************************************************************************/
void GFX_fillTriangle(Adafruit_GFX* gfx, int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint16_t color) {
    int16_t a, b, y, last;

    // Sort coordinates by Y order (y2 >= y1 >= y0)
    if (y0 > y1) {
        SWAP(y0, y1, int16_t);
        SWAP(x0, x1, int16_t);
    }
    if (y1 > y2) {
        SWAP(y2, y1, int16_t);
        SWAP(x2, x1, int16_t);
    }
    if (y0 > y1) {
        SWAP(y0, y1, int16_t);
        SWAP(x0, x1, int16_t);
    }

    if (y0 == y2) {  // Handle awkward all-on-same-line case as its own thing
        a = b = x0;
        if (x1 < a)
            a = x1;
        else if (x1 > b)
            b = x1;
        if (x2 < a)
            a = x2;
        else if (x2 > b)
            b = x2;
        GFX_drawFastHLine(gfx, a, y0, b - a + 1, color);
        return;
    }

    int16_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    // For upper part of triangle, find scanline crossings for segments
    // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
    // is included here (and second loop will be skipped, avoiding a /0
    // error there), otherwise scanline y1 is skipped here and handled
    // in the second loop...which also avoids a /0 error here if y0=y1
    // (flat-topped triangle).
    if (y1 == y2)
        last = y1;  // Include y1 scanline
    else
        last = y1 - 1;  // Skip it

    for (y = y0; y <= last; y++) {
        a = x0 + sa / dy01;
        b = x0 + sb / dy02;
        sa += dx01;
        sb += dx02;
        /* longhand:
        a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        */
        if (a > b) SWAP(a, b, int16_t);
        GFX_drawFastHLine(gfx, a, y, b - a + 1, color);
    }

    // For lower part of triangle, find scanline crossings for segments
    // 0-2 and 1-2.  This loop is skipped if y1=y2.
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        a = x1 + sa / dy12;
        b = x0 + sb / dy02;
        sa += dx12;
        sb += dx02;
        /* longhand:
        a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        */
        if (a > b) SWAP(a, b, int16_t);
        GFX_drawFastHLine(gfx, a, y, b - a + 1, color);
    }
}

/**************************************************************************/
/*!
   @brief      Draw a RAM-resident 1-bit image at the specified (x,y) position,
   using the specified foreground color (unset bits are transparent).
    @param    x   Top left corner x coordinate
    @param    y   Top left corner y coordinate
    @param    bitmap  byte array with monochrome bitmap
    @param    w   Width of bitmap in pixels
    @param    h   Height of bitmap in pixels
    @param    color 16-bit 5-6-5 Color to draw with
    @param    invert When true, will invert the bitmap
*/
/**************************************************************************/
void GFX_drawBitmap(Adafruit_GFX* gfx, int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h,
                    uint16_t color, bool invert) {
    int16_t byteWidth = (w + 7) / 8;  // Bitmap scanline pad = whole byte
    uint8_t byte = 0;

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (i & 7)
                byte <<= 1;
            else
                byte = bitmap[j * byteWidth + i / 8];
            if (((byte & 0x80) == 0x80) ^ invert) GFX_drawPixel(gfx, x + i, y + j, color);
        }
    }
}

/*

  U8g2_for_Adafruit_GFX.cpp

  Add unicode support and U8g2 fonts to Adafruit GFX libraries.

  U8g2 for Adafruit GFX Lib (https://github.com/olikraus/U8g2_for_Adafruit_GFX)

  Copyright (c) 2018, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list
    of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice, this
    list of conditions and the following disclaimer in the documentation and/or other
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

void GFX_setCursor(Adafruit_GFX* gfx, int16_t x, int16_t y) {
    gfx->tx = x;
    gfx->ty = y;
    gfx->utf8_state = 0;
}

void GFX_setFont(Adafruit_GFX* gfx, const uint8_t* font) { u8g2_SetFont(&gfx->u8g2, font); }

void GFX_setFontScale(Adafruit_GFX* gfx, uint16_t scale_q8) {
    gfx->font_scale_q8 = scale_q8 ? scale_q8 : 256u;
}

void GFX_setFontMode(Adafruit_GFX* gfx, uint8_t is_transparent) { u8g2_SetFontMode(&gfx->u8g2, is_transparent); }

void GFX_setFontDirection(Adafruit_GFX* gfx, GFX_Rotate d) { u8g2_SetFontDirection(&gfx->u8g2, (uint8_t)d); }

void GFX_setTextColor(Adafruit_GFX* gfx, uint16_t fg, uint16_t bg) {
    u8g2_SetForegroundColor(&gfx->u8g2, fg);
    u8g2_SetBackgroundColor(&gfx->u8g2, bg);
}

static int16_t GFX_scaleFontMetric(const Adafruit_GFX* gfx, int16_t value) {
    uint16_t scale = gfx->font_scale_q8 ? gfx->font_scale_q8 : 256u;
    return (int16_t)(((int32_t)value * scale) / 256);
}

int16_t GFX_getFontAscent(Adafruit_GFX* gfx) { return GFX_scaleFontMetric(gfx, gfx->u8g2.font_info.ascent_A); }

int16_t GFX_getFontDescent(Adafruit_GFX* gfx) { return GFX_scaleFontMetric(gfx, gfx->u8g2.font_info.descent_g); }

int16_t GFX_getFontHeight(Adafruit_GFX* gfx) {
    return GFX_scaleFontMetric(gfx, gfx->u8g2.font_info.ascent_A - gfx->u8g2.font_info.descent_g);
}

static bool GFX_glyphOutsideCurrentPage(const Adafruit_GFX* gfx, int16_t y) {
    int16_t glyph_top;
    int16_t glyph_bottom;
    int16_t page_top;
    int16_t page_bottom;
    uint32_t page_span;

    if (gfx->buffer == NULL || gfx->page_height <= 0 || gfx->u8g2.font_info.max_char_height <= 0) return false;
    page_span = GFX_currentPageLogicalY(gfx);
    if (gfx->u8g2.font_decode.dir != 0 || page_span == UINT32_MAX) return false;
    page_top = (int16_t)(page_span >> 16);
    page_bottom = (int16_t)page_span;

    /* u8g2 places a direction-0 glyph at y - height - y_offset. Use the
     * font-wide bounding box so the inexpensive rejection remains
     * conservative for every glyph in the active font. */
    glyph_top = y - GFX_scaleFontMetric(gfx, gfx->u8g2.font_info.max_char_height + gfx->u8g2.font_info.y_offset);
    glyph_bottom = y - GFX_scaleFontMetric(gfx, gfx->u8g2.font_info.y_offset);
    /* Metric scaling truncates, but raster spans round outward. A bound on
     * the page edge can still contribute its final pixel; keep that glyph
     * and let the ordinary pixel clip decide. No layout/advance changes. */
    return glyph_bottom < page_top || glyph_top > page_bottom;
}

static int16_t GFX_drawGlyphPaged(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t e) {
    int16_t width = u8g2_GetGlyphWidth(&gfx->u8g2, e);
    if (!GFX_glyphOutsideCurrentPage(gfx, y)) {
        gfx->font_origin_x = x;
        gfx->font_origin_y = y;
        (void)u8g2_DrawGlyph(&gfx->u8g2, x, y, e);
    }
    return GFX_scaleFontMetric(gfx, width);
}

int16_t GFX_drawGlyph(Adafruit_GFX* gfx, int16_t x, int16_t y, uint16_t e) {
    return GFX_drawGlyphPaged(gfx, x, y, e);
}

int16_t GFX_drawStr(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* s) {
    int16_t sum = 0;
    while (*s != '\0') {
        int16_t delta = GFX_drawGlyphPaged(gfx, x, y, (uint8_t)*s++);
        switch (gfx->u8g2.font_decode.dir) {
            case 0:
                x += delta;
                break;
            case 1:
                y += delta;
                break;
            case 2:
                x -= delta;
                break;
            case 3:
                y -= delta;
                break;
        }
        sum += delta;
    }
    return sum;
}

static uint16_t utf8_next(Adafruit_GFX* gfx, uint8_t b) {
    if (b == 0)         /* '\n' terminates the string to support the string list procedures */
        return 0x0ffff; /* end of string detected, pending UTF8 is discarded */
    if (gfx->utf8_state == 0) {
        if (b >= 0xfc) /* 6 byte sequence */
        {
            gfx->utf8_state = 5;
            b &= 1;
        } else if (b >= 0xf8) {
            gfx->utf8_state = 4;
            b &= 3;
        } else if (b >= 0xf0) {
            gfx->utf8_state = 3;
            b &= 7;
        } else if (b >= 0xe0) {
            gfx->utf8_state = 2;
            b &= 15;
        } else if (b >= 0xc0) {
            gfx->utf8_state = 1;
            b &= 0x01f;
        } else {
            /* do nothing, just use the value as encoding */
            return b;
        }
        gfx->encoding = b;
        return 0x0fffe;
    } else {
        gfx->utf8_state--;
        /* The case b < 0x080 (an illegal UTF8 encoding) is not checked here. */
        gfx->encoding <<= 6;
        b &= 0x03f;
        gfx->encoding |= b;
        if (gfx->utf8_state != 0) return 0x0fffe; /* nothing to do yet */
    }
    return gfx->encoding;
}

int16_t GFX_drawUTF8(Adafruit_GFX* gfx, int16_t x, int16_t y, const char* str) {
    uint16_t e;
    int16_t delta, sum;

    gfx->utf8_state = 0;
    sum = 0;
    for (;;) {
        e = utf8_next(gfx, (uint8_t)*str);
        if (e == 0x0ffff) break;
        str++;
        if (e != 0x0fffe) {
            delta = GFX_drawGlyphPaged(gfx, x, y, e);

            switch (gfx->u8g2.font_decode.dir) {
                case 0:
                    x += delta;
                    break;
                case 1:
                    y += delta;
                    break;
                case 2:
                    x -= delta;
                    break;
                case 3:
                    y -= delta;
                    break;
            }

            sum += delta;
        }
    }
    return sum;
}

int16_t GFX_getUTF8Width(Adafruit_GFX* gfx, const char* str) {
    uint16_t e;
    int16_t dx, w;

    gfx->u8g2.font_decode.glyph_width = 0;
    gfx->utf8_state = 0;
    w = 0;
    dx = 0;
    for (;;) {
        e = utf8_next(gfx, (uint8_t)*str);
        if (e == 0x0ffff) break;
        str++;
        if (e != 0x0fffe) {
            dx = u8g2_GetGlyphWidth(&gfx->u8g2, e);
            w += dx;
        }
    }
    /* adjust the last glyph, check for issue #16: do not adjust if width is 0 */
    if (gfx->u8g2.font_decode.glyph_width != 0) {
        w -= dx;
        w += gfx->u8g2.font_decode.glyph_width; /* the real pixel width of the glyph, sideeffect of GetGlyphWidth */
        /* issue #46: we have to add the x offset also */
        w += gfx->u8g2.glyph_x_offset; /* this value is set as a side effect of u8g2_GetGlyphWidth() */
    }

    return GFX_scaleFontMetric(gfx, w);
}

size_t GFX_print(Adafruit_GFX* gfx, const char c) {
    int16_t delta;
    uint16_t e = utf8_next(gfx, (uint8_t)c);
    if (e == '\n') {
        gfx->tx = 0;
        gfx->ty += GFX_scaleFontMetric(
            gfx, gfx->u8g2.font_info.ascent_para - gfx->u8g2.font_info.descent_para);
    } else if (e == '\r') {
        gfx->tx = 0;
    } else if (e < 0x0fffe) {
        delta = GFX_drawGlyphPaged(gfx, gfx->tx, gfx->ty, e);
        switch (gfx->u8g2.font_decode.dir) {
            case 0:
                gfx->tx += delta;
                break;
            case 1:
                gfx->ty += delta;
                break;
            case 2:
                gfx->tx -= delta;
                break;
            case 3:
                gfx->ty -= delta;
                break;
        }
    }
    return 1;
}

size_t GFX_write(Adafruit_GFX* gfx, const char* buffer, size_t size) {
    size_t cnt = 0;
    while (size > 0) {
        cnt += GFX_print(gfx, *buffer++);
        size--;
    }
    return cnt;
}

static void format_char(char* buffer, size_t size, size_t* length, char value) {
    if (*length + 1 < size) buffer[(*length)++] = value;
}

static void format_string(char* buffer, size_t size, size_t* length, const char* value) {
    if (value == NULL) value = "(null)";
    while (*value != '\0') format_char(buffer, size, length, *value++);
}

static void format_unsigned(char* buffer, size_t size, size_t* length, uint32_t value, uint8_t base,
                            bool upper, uint8_t width, char padding, bool negative) {
    char digits[10];
    uint8_t count = 0;
    do {
        uint8_t digit = (uint8_t)(value % base);
        digits[count++] = (char)(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
        value /= base;
    } while (value != 0 && count < sizeof(digits));

    uint8_t printed = (uint8_t)(count + (negative ? 1 : 0));
    if (padding == ' ') {
        while (printed < width) {
            format_char(buffer, size, length, ' ');
            printed++;
        }
    }
    if (negative) format_char(buffer, size, length, '-');
    if (padding == '0') {
        while (printed < width) {
            format_char(buffer, size, length, '0');
            printed++;
        }
    }
    while (count > 0) format_char(buffer, size, length, digits[--count]);
}

static size_t GFX_vformat(char* buffer, size_t size, const char* format, va_list args) {
    size_t length = 0;
    if (buffer == NULL || size == 0 || format == NULL) return 0;

    while (*format != '\0' && length + 1 < size) {
        if (*format != '%') {
            format_char(buffer, size, &length, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            format_char(buffer, size, &length, *format++);
            continue;
        }

        char padding = ' ';
        uint8_t width = 0;
        if (*format == '0') {
            padding = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            uint16_t next = (uint16_t)width * 10u + (uint8_t)(*format++ - '0');
            width = next > UINT8_MAX ? UINT8_MAX : (uint8_t)next;
        }
        if (*format == 'l') format++;

        switch (*format) {
            case 's':
                format_string(buffer, size, &length, va_arg(args, const char*));
                break;
            case 'c':
                format_char(buffer, size, &length, (char)va_arg(args, int));
                break;
            case 'd':
            case 'i': {
                int32_t value = va_arg(args, int32_t);
                bool negative = value < 0;
                uint32_t magnitude = negative ? 0u - (uint32_t)value : (uint32_t)value;
                format_unsigned(buffer, size, &length, magnitude, 10, false, width, padding, negative);
                break;
            }
            case 'u':
                format_unsigned(buffer, size, &length, va_arg(args, uint32_t), 10, false, width, padding, false);
                break;
            case 'x':
            case 'X': {
                bool upper = *format == 'X';
                format_unsigned(buffer, size, &length, va_arg(args, uint32_t), 16, upper, width, padding, false);
                break;
            }
            case '\0':
                format_char(buffer, size, &length, '%');
                continue;
            default:
                format_char(buffer, size, &length, '%');
                format_char(buffer, size, &length, *format);
                break;
        }
        format++;
    }
    buffer[length] = '\0';
    return length;
}

size_t GFX_format(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t length = GFX_vformat(buffer, size, format, args);
    va_end(args);
    return length;
}

size_t GFX_printf(Adafruit_GFX* gfx, const char* format, ...) {
    char buffer[64];
    va_list args;
    va_start(args, format);
    size_t length = GFX_vformat(buffer, sizeof(buffer), format, args);
    va_end(args);
    return GFX_write(gfx, buffer, length);
}
