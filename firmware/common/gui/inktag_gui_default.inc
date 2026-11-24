/*
 * GUI_default: InkTag's default native-page renderer (formerly V1).
 *
 * This file is included by inktag_gui.c so the accepted geometry executes inside
 * the existing bounded page/strip renderer.  The host oracle includes the
 * same file and supplies only its allocation/capture wrapper; do not fork the
 * layout formulas back into tools/emulator.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_gui_device_qr.h"
#include "inktag_calendar.h"
#include "inktag_gui_fonts.h"
#include "inktag_version.h"

#define V1_BLACK GFX_BLACK
#define V1_RED GFX_RED
#define V1_YELLOW GFX_YELLOW
#define V1_WHITE GFX_WHITE
#define V1_MIN_META_SCALE 160u
#define V1_NATIVE_TEXT_SCALE 256u
#define V1_CENTER_HANZI_BASELINE (-2)
#define V1_PAGE_TEXT_GUARD 32

#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
#include "inktag_gui_large_digits.h"
typedef struct {
    inktag_gui_role_frame_t source;
    inktag_gui_large_style_t style;
    inktag_gui_fixed_field_t field;
    bool use_large;
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
    bool layout_failed;
#endif
} v1_role_render_t;
static v1_role_render_t *v1_role_render;
#endif

static inline bool v1_native_user_fonts(void) {
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
    /* A validated factory package is a built-in layout source. The CRC is
     * only a layout hint; native admission and frame identity stay intact.
     * Frame admission has already restricted schema to zero or one. */
    return v1_role_render &&
        (v1_role_render->source.source.schema &
         (v1_role_render->source.source.crc32!=0xa309b12au));
#else
    return v1_role_render && v1_role_render->source.source.schema==INKTAG_GUI_NATIVE_PACKAGE_SCHEMA;
#endif
#else
    return false;
#endif
}

/* Native packages carry final pixel sizes. Legacy/default scaling is unchanged. */
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
static inline void v1_font_scale(Adafruit_GFX *gfx,uint16_t scale) {
    GFX_setFontScale(gfx,v1_native_user_fonts() ? 256u:scale);
}
#define GFX_setFontScale v1_font_scale
#endif

/* The atomic role frame keeps its schema immutable through a draw call.
 * nRF reuses this fact locally; no state survives the owning function. */
#if defined(NRF52832_XXAA)
#define V1_FRAME_FONT_SNAPSHOT const bool frame_native_fonts=v1_native_user_fonts()
#define V1_FRAME_NATIVE_FONTS() frame_native_fonts
#else
#define V1_FRAME_FONT_SNAPSHOT
#define V1_FRAME_NATIVE_FONTS() v1_native_user_fonts()
#endif

static inline bool v1_role_digits(Adafruit_GFX *gfx,uint8_t left,uint8_t right,uint16_t color) {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
    if(v1_role_render && v1_role_render->use_large) {
        inktag_gui_large_draw(gfx,&v1_role_render->style,&v1_role_render->field,left,right,color);
        return true;
    }
#else
    (void)gfx;(void)left;(void)right;(void)color;
#endif
    return false;
}

static const char* v1_advertised_name(const gui_data_t* data) {
    return data->bluetooth_name ? data->bluetooth_name : data->ssid;
}

/* Retain one copy of bounded text primitives on the size-qualified ports,
 * including TLSR Pro. No font/layout/UTF-8 behavior changes; Base and other
 * targets keep their existing compiler decisions. */
#if (defined(NRF52811_XXAA) || defined(INKTAG_GOODIX)) && defined(__GNUC__)
#define V1_TEXT_SHARED __attribute__((noinline, noclone))
#elif (defined(NRF52832_XXAA) || (defined(INKTAG_TELINK) && INKTAG_BUILD_PRO)) && defined(__GNUC__)
#define V1_TEXT_SHARED __attribute__((noinline, noclone))
#else
#define V1_TEXT_SHARED
#endif

/* Goodix shares the native-page dispatch and immutable calendar query.
 * Layout, font coverage and framebuffer/stack budgets remain unchanged. */
#if defined(INKTAG_GOODIX) && defined(__GNUC__)
#define V1_GR_PAGE_SHARED __attribute__((noinline,noclone))
#else
#define V1_GR_PAGE_SHARED
#endif

/* Candidate-only raster policy. Half-open pixel edges never dilate a stroke.
 * The production GFX and frozen V0 remain byte-for-byte unchanged. */
/* All supported ARM and host compilers have arithmetic signed right shift.
 * Keep a build-time guard instead of silently changing negative pixel edges. */
#if ((-1 >> 1) != -1)
#error "GUI edge rounding requires arithmetic signed right shift"
#endif
static inline int16_t v1_edge(int16_t value, uint16_t scale) {
    int32_t product = (int32_t)value * scale;
    return (int16_t)((product + 255) >> 8);
}

static inline int16_t v1_round_edge(int16_t value, uint16_t scale) {
    int32_t product = (int32_t)value * scale;
    return (int16_t)((product + 128) >> 8);
}

static void v1_font_run_impl(u8g2_font_t* font, int16_t x, int16_t y,
                        int16_t length, uint8_t direction, uint16_t color, uint8_t coverage) {
    if (length <= 0) return;
    Adafruit_GFX* gfx = (Adafruit_GFX*)((char*)font - offsetof(Adafruit_GFX, u8g2));
    int16_t x0 = x, y0 = y, x1 = x + 1, y1 = y + 1;
    if (direction == 0) x1 = x + length;
    else if (direction == 1) y1 = y + length;
    else if (direction == 2) x0 = x - length + 1;
    else y0 = y - length + 1;
    uint16_t scale = gfx->font_scale_q8; /* GFX_begin/setFontScale already normalize zero to 256. */
    /* Callers use matching X/Y edge policies: ceil0, covered3, compact4. */
    int start = coverage == 3u ? 0 : 255;
    int end = 255;
    if (coverage == 4u) start = end = 128;
    x0 = gfx->font_origin_x + (((int32_t)(x0-gfx->font_origin_x)*scale+start)>>8);
    x1 = gfx->font_origin_x + (((int32_t)(x1-gfx->font_origin_x)*scale+end)>>8);
    y0 = gfx->font_origin_y + (((int32_t)(y0-gfx->font_origin_y)*scale+start)>>8);
    y1 = gfx->font_origin_y + (((int32_t)(y1-gfx->font_origin_y)*scale+end)>>8);
    int width = x1-x0, height = y1-y0;
    if (coverage == 4u) {
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
    if (width > 0 && height > 0) GFX_fillRect(gfx,x0,y0,width,height,color);
}

static void v1_font_run(u8g2_font_t* font, int16_t x, int16_t y,
                        int16_t length, uint8_t direction, uint16_t color) {
    v1_font_run_impl(font, x, y, length, direction, color, 0u);
}

static void v1_font_run_covered(u8g2_font_t* font, int16_t x, int16_t y,
                        int16_t length, uint8_t direction, uint16_t color) {
    v1_font_run_impl(font, x, y, length, direction, color, 3u);
}

static void v1_font_run_compact(u8g2_font_t* font, int16_t x, int16_t y,
                        int16_t length, uint8_t direction, uint16_t color) {
    v1_font_run_impl(font, x, y, length, direction, color, 4u);
}

/* The same bounded UTF-8 walk serves measurement and font coverage checks. */
V1_TEXT_SHARED
static uint16_t v1_next_glyph(const unsigned char** cursor) {
    const unsigned char* p = *cursor;
    uint16_t c = *p++;
    if (c >= 0xF0) { for (uint8_t i = 0; i < 3u && *p; i++) p++; c = 0xFFFDu; }
    else if (c >= 0xE0 && *p) { c = (uint16_t)((c & 15u) << 6) | (*p++ & 63u); if (*p) c = (uint16_t)(c << 6) | (*p++ & 63u); }
    else if (c >= 0xC0 && *p) c = (uint16_t)((c & 31u) << 6) | (*p++ & 63u);
    *cursor = p;
    return c;
}
/* Measure the same per-glyph advances used by GFX_print, not a scaled sum. */
V1_TEXT_SHARED
static int v1_text_width(Adafruit_GFX* gfx, const char* text) {
    int width = 0;
    const unsigned char* p = (const unsigned char*)text;
    while (*p) {
        uint16_t c = v1_next_glyph(&p);
        width += (int32_t)u8g2_GetGlyphWidth(&gfx->u8g2, c) * gfx->font_scale_q8 / 256;
    }
    return width;
}
#define GFX_getUTF8Width v1_text_width

/* Every V1 text run is transparent; changing a u8g2 master resets this flag. */
V1_TEXT_SHARED
static void v1_set_font(Adafruit_GFX* gfx, const uint8_t* font, uint16_t scale) {
    GFX_setFont(gfx, font);
    GFX_setFontScale(gfx, scale);
    GFX_setFontMode(gfx, 1u);
}

/* Strings need no format parser. Preserve GFX_printf's 63-byte text bound. */
V1_TEXT_SHARED
static void v1_print(Adafruit_GFX* gfx, const char* text) {
    size_t length = strlen(text);
    GFX_write(gfx, text, length < 63u ? length : 63u);
}

V1_TEXT_SHARED
static void v1_print_at(Adafruit_GFX* gfx, int x, int y, const char* text) {
    GFX_setCursor(gfx, x, y);
    v1_print(gfx, text);
}

/* Crop metadata at UTF-8 boundaries using the already selected native face. */
V1_TEXT_SHARED
static void v1_fit_text(Adafruit_GFX* gfx, char* text, int width) {
    size_t length = strlen(text);
    while (length && GFX_getUTF8Width(gfx, text) > width) {
        do { length--; } while (length && ((uint8_t)text[length] & 0xC0u) == 0x80u);
        text[length] = '\0';
    }
}

/*
 * V1 typography contract: compact text stays at the native bitmap size,
 * WQY carries prose; wide calendar numerals reuse V0's Helvetica Bold master.
 * Shorten metadata before shrinking it; compact footer BLE stays native too.
 */

typedef enum { V1_ALIGN_LEFT, V1_ALIGN_CENTER, V1_ALIGN_RIGHT } v1_align_t;

typedef struct {
    const char* text;
    uint16_t color;
} v1_span_t;

typedef enum {
    V1_LAYOUT_COMPACT_SQUARE,
    V1_LAYOUT_COMPACT_RECT,
    V1_LAYOUT_NORMAL,
    V1_LAYOUT_LARGE,
} v1_layout_family_t;

typedef struct {
    uint16_t geometry_scale_q8;
    /* Four-byte value return avoids a hidden stack result on ARM. */
    uint8_t family : 2;
    /* Rectangular detail follows the 152px short-edge tier. Only the exact
     * 152/200 squares omit lunar cells; 240px reuses the lunar square path. */
    uint8_t calendar_lunar_cells : 1;
    uint8_t compact_qr : 1;
    uint8_t show_voltage : 1;
} v1_layout_preset_t;

static int v1_min(int a, int b) { return a < b ? a : b; }
static int v1_max(int a, int b) { return a > b ? a : b; }
static int V1_TEXT_SHARED v1_clamp(int value, int minimum, int maximum) {
    return v1_min(v1_max(value, minimum), maximum);
}

static v1_layout_preset_t v1_layout_preset(const gui_data_t* data) {
    int long_side = v1_max(data->width, data->height);
    int short_side = v1_min(data->width, data->height);
    static const struct {
        uint16_t width, height;
        v1_layout_preset_t layout;
    } screens[] = {
#if !defined(INKTAG_GUI_EXCLUDE_154_LAYOUTS)
        {152, 152, {256u, V1_LAYOUT_COMPACT_SQUARE, false, true, false}},
        {200, 200, {337u, V1_LAYOUT_COMPACT_SQUARE, false, true, true}},
#endif
        {212, 104, {256u, V1_LAYOUT_COMPACT_RECT, false, true, false}},
        {250, 128, {302u, V1_LAYOUT_COMPACT_RECT, false, false, true}},
        {296, 152, {256u, V1_LAYOUT_COMPACT_RECT, true, false, true}},
    };
    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++)
        if (long_side == screens[i].width && short_side == screens[i].height)
            return screens[i].layout;
    if (small_layout(data))
        return (v1_layout_preset_t){
            256u,
#if defined(INKTAG_GUI_EXCLUDE_154_LAYOUTS)
            V1_LAYOUT_COMPACT_RECT,
#else
            long_side * 4 <= short_side * 5
                ? V1_LAYOUT_COMPACT_SQUARE : V1_LAYOUT_COMPACT_RECT,
#endif
            short_side >= 152, false, long_side >= 200,
        };
    if (long_side <= 500)
        return (v1_layout_preset_t){256u, V1_LAYOUT_NORMAL, true, false, true};
    return (v1_layout_preset_t){256u, V1_LAYOUT_LARGE, true, false, true};
}

static uint16_t v1_scale_q8(const gui_data_t* data) {
    switch (data->native_ui_scale) {
        case 2u: return 384u;
        case 3u: return 512u;
        case 4u: return 320u;
        case 5u: return 448u;
        default: return 256u;
    }
}

static uint16_t v1_accent(const gui_data_t* data) {
    return data->color <= 1u ? V1_BLACK : V1_RED;
}

static bool v1_has_red(const gui_data_t* data) {
    return data->color <= 11u && (data->color & 0xf6u) == 2u;
}

static bool v1_compact(const gui_data_t* data) { return small_layout(data); }

static int v1_square_text_scale(const gui_data_t* data) {
    if (data->width != data->height ||
        (data->mode != MODE_CALENDAR && data->mode != MODE_CLOCK)) return 256;
    return data->width == 200u ? 320 : data->width == 240u ? 384 : 256;
}
static bool v1_show_voltage(const gui_data_t* data) {
    return v1_layout_preset(data).show_voltage;
}
static int v1_compact_footer_height(const gui_data_t* data) {
    return data->width == 200u && v1_square_text_scale(data) == 320 ? 23 : 18;
}

static int v1_power_inset(const gui_data_t* data) {
    return v1_compact(data) ? 2
        : v1_clamp(v1_min(data->width, data->height) / 24, 10, 28);
}

static int v1_power_width(const gui_data_t* data) {
    if (!v1_compact(data)) return v1_clamp(data->width / 4, 100, 160);
    /* gui_data_t.temperature is int8_t: count its digits without division. */
    int value = data->temperature, digits = 1;
    if (value < 0) { value = -value; digits++; }
    digits += value >= 10;
    digits += value >= 100;
    if (v1_square_text_scale(data)>256 && data->width!=200u)
        return digits * 8 + (v1_show_voltage(data) ? 73 : 41);
    return digits * 7 + (v1_show_voltage(data) ? data->width <= 250u ? 59 : 61 : 29);
}

#if defined(INKTAG_TELINK) && defined(__GNUC__)
#define V1_HEADER_QUERY_SHARED __attribute__((noinline,noclone))
#elif defined(NRF52832_XXAA)
#define V1_HEADER_QUERY_SHARED V1_TEXT_SHARED
#else
#define V1_HEADER_QUERY_SHARED V1_GR_PAGE_SHARED
#endif
#if defined(INKTAG_GOODIX) || defined(NRF52832_XXAA) || defined(INKTAG_TELINK)
/* Share only the immutable built-in calendar/clock header geometry query. */
static uint32_t V1_HEADER_QUERY_SHARED v1_builtin_header_geometry(const gui_data_t* data) {
    if ((data->mode != MODE_CALENDAR && data->mode != MODE_CLOCK) || v1_native_user_fonts()) return 0u;
    return (uint32_t)data->width | ((uint32_t)data->height << 16);
}
#define V1_BUILTIN_HEADER(data,w,h) (v1_builtin_header_geometry(data) == ((uint32_t)(w) | ((uint32_t)(h)<<16)))
#else
#define V1_BUILTIN_HEADER(data,w,h) (((data)->mode == MODE_CALENDAR || (data)->mode == MODE_CLOCK) && (data)->width == (w) && (data)->height == (h) && !v1_native_user_fonts())
#endif

static int v1_power_height(const gui_data_t* data) {
#if defined(INKTAG_GOODIX) || defined(NRF52832_XXAA) || defined(INKTAG_TELINK)
    uint32_t geometry=v1_builtin_header_geometry(data);
    if (geometry == (200u | (200u<<16))) return 26;
    if (geometry == (400u | (300u<<16))) return 23;
#else
    if ((data->mode == MODE_CALENDAR || data->mode == MODE_CLOCK) && !v1_native_user_fonts()) {
        if (data->width == 200u && data->height == 200u) return 26;
        if (data->width == 400u && data->height == 300u) return 23;
    }
#endif
    return v1_compact(data) ? (18*v1_square_text_scale(data)+128)/256
        : v1_clamp(data->height / 12, 25, 48);
}

static int v1_power_x(const gui_data_t* data) {
    return data->width - v1_power_inset(data) - v1_power_width(data);
}

static int v1_header_baseline(const gui_data_t* data) {
    int height = v1_power_height(data), scale = v1_min(v1_scale_q8(data), 384);
    return data->width > 500u ? v1_max((height - 1 + 10 * scale / 256) / 2, 2 + 16 * scale / 256) : height - (5*v1_square_text_scale(data)+128)/256;
}

static bool v1_font_supports(Adafruit_GFX* gfx, const v1_span_t* spans, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const unsigned char* p = (const unsigned char*)spans[i].text;
        if (p[0]==' ' && p[1]==' ' && !p[2]) continue;
        while (*p) {
            uint16_t c = v1_next_glyph(&p);
            if (u8g2_GetGlyphWidth(&gfx->u8g2, c) <= 0) return false;
        }
    }
    return true;
}

static int V1_TEXT_SHARED v1_span_width(Adafruit_GFX* gfx, const char* text) {
    /* Group separators must not depend on an optional ASCII space glyph. */
    if (text[0]==' ' && text[1]==' ' && text[2]=='\0')
        return v1_max(8, gfx->font_scale_q8 * 8 / 256);
    return text[0] == ' ' && text[1] == '\0' ? v1_max(gfx->_width < 350 ? 4 : 2, gfx->font_scale_q8 * 4 / 256 - 2)
                                 : GFX_getUTF8Width(gfx, text);
}

static int V1_TEXT_SHARED v1_spans_width(Adafruit_GFX* gfx, const v1_span_t* spans, size_t count) {
    int width = 0;
    for (size_t i = 0; i < count; i++) width += v1_span_width(gfx, spans[i].text);
    return width;
}

static void v1_text_face(Adafruit_GFX* gfx, const v1_span_t* spans, size_t count,
                          const uint8_t** font, uint16_t* scale, uint16_t* minimum,
                          int width, int height) {
    if(v1_native_user_fonts()) {
        *scale=*minimum=256u;
        return;
    }
    if (*font == u8g2_font_helvB14_tn || *font == u8g2_font_helvB18_tn) return;
    /* The 200px date uses a reduced version of the existing 12px master. Keep this
     * explicit size instead of snapping it down to the 9px native face. */
    if (gfx->_width == 200 && gfx->_height == 200 &&
        *font == u8g2_font_wqy12_t_lunar && (*scale == 224u || *scale == 240u)) {
        v1_set_font(gfx, *font, *scale);
        if (v1_spans_width(gfx, spans, count) + 1 <= width &&
            GFX_getFontHeight(gfx) <= height) {
            *minimum = *scale;
            return;
        }
    }
    if (gfx->_width <= 500) {
        v1_set_font(gfx, u8g2_font_wqy12_t_lunar, 256u);
        bool heading = (*font == u8g2_font_wqy12_t_lunar || *scale >= 320u)
            && v1_font_supports(gfx, spans, count)
            && v1_spans_width(gfx, spans, count) <= width && GFX_getFontHeight(gfx) <= height;
        *font = heading ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar;
        v1_set_font(gfx, *font, 256u);
        *scale = 256u;
        if (v1_spans_width(gfx, spans, count) <= width && GFX_getFontHeight(gfx) <= height) *minimum = 256u;
        if (*minimum > 256u) *minimum = 256u;
        return;
    }
    if (*font == u8g2_font_wqy9_t_lunar && *scale >= 320u) {
        GFX_setFont(gfx, u8g2_font_wqy12_t_lunar);
        if (!v1_font_supports(gfx, spans, count)) return;
        *font = u8g2_font_wqy12_t_lunar;
        *scale = (uint16_t)((*scale * 3u + 2u) / 4u);
        if (*scale >= 248u && *scale <= 264u) *scale = 256u;
        *minimum = (uint16_t)(*minimum * 3u / 4u);
    }
}

static int v1_inline_width(Adafruit_GFX* gfx,
                               const v1_span_t* spans, size_t count) {
    return v1_spans_width(gfx, spans, count);
}

#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
typedef struct {int width,left,top,bottom;} v1_native_run_t;
static bool v1_native_run_metrics(Adafruit_GFX *gfx,const v1_span_t *spans,
                                  size_t count,int gap,v1_native_run_t *out) {
    int cursor=0,right=0;bool ink=false;
    *out=(v1_native_run_t){0,0,0,0};
    for(size_t i=0;i<count;++i) {
        const unsigned char *p=(const unsigned char*)spans[i].text;
        if(p[0]==' ' && (!p[1] || (p[1]==' ' && !p[2])))
            cursor+=v1_span_width(gfx,(const char*)p);
        else while(*p) {
            u8g2_glyph_metrics_t g;
            if(!u8g2_GetGlyphMetrics(&gfx->u8g2,v1_next_glyph(&p),&g))return false;
            if(g.width && g.height) {
                int top=-g.height-g.y,bottom=-g.y;
                out->left=v1_min(out->left,cursor+g.x);
                right=v1_max(right,cursor+g.x+g.width);
                out->top=ink ? v1_min(out->top,top):top;
                out->bottom=ink ? v1_max(out->bottom,bottom):bottom;ink=true;
            }
            cursor+=g.advance;
        }
        if(i+1u<count)cursor+=gap;
    }
    out->width=v1_max(cursor,right)-out->left;
    return true;
}
#endif

static void v1_inline_text_style_at(Adafruit_GFX* gfx, int x, int y,
                           int width, int height,
                           const v1_span_t* spans, size_t count,
                           const uint8_t* font, uint16_t preferred_scale,
                           uint16_t minimum_scale, uint16_t background,
                           v1_align_t align, int fixed_baseline, int gap, bool bold_numbers) {
#if !defined(INKTAG_GUI_FACTORY_FONT_LAYOUT) || defined(NRF52832_XXAA)
    V1_FRAME_FONT_SNAPSHOT;
#endif
    if (!spans || count == 0u || width <= 0 || height <= 0) return;
    /* Fixed baselines, scaled ink and bold overprint can escape the nominal
     * layout box. Keep visibility conservative while still preserving the
     * cursor/baseline side effects required by adjacent paged layout. */
    bool page_visible = GFX_pageIntersectsLogicalY(
        gfx, y - V1_PAGE_TEXT_GUARD,
        height + V1_PAGE_TEXT_GUARD * 2);
    /* Negative gap is the 152px header's 2px month join after span3.
     * Other callers keep nonnegative spacing and the original interface. */
    bool tighten_month = gap < 0;
    if (tighten_month) { gap = -gap - 1; width += 2; }
    if (!font) font = u8g2_font_wqy9_t_lunar;
    v1_text_face(gfx, spans, count, &font, &preferred_scale, &minimum_scale, width - ((int)count - 1) * gap, height);
    unsigned scale = preferred_scale < minimum_scale
        ? minimum_scale : preferred_scale;
    v1_set_font(gfx, font, scale);
    while (scale > minimum_scale &&
           (v1_spans_width(gfx, spans, count) + ((int)count - 1) * gap > width ||
            GFX_getFontHeight(gfx) > height)) {
        scale = scale > minimum_scale + 16u
            ? (uint16_t)(scale - 16u) : minimum_scale;
        GFX_setFontScale(gfx, scale);
    }
    int text_width;
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
    /* Keep width, ink offset and baseline under the same native decision. */
    int baseline,ink_left=0;
#endif
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
    v1_native_run_t run={0,0,0,0};
    if(V1_FRAME_NATIVE_FONTS()) {
        if(!v1_native_run_metrics(gfx,spans,count,gap,&run) ||
            run.width>width || run.bottom-run.top>height) {
            v1_role_render->layout_failed=true;
            return;
        }
        text_width=run.width;
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
        ink_left=run.left;
        baseline=y+(height-(run.bottom-run.top))/2-run.top;
        if(fixed_baseline>=0 && fixed_baseline+run.top>=y &&
            fixed_baseline+run.bottom<=y+height)baseline=fixed_baseline;
#endif
    } else
#endif
    {
        text_width=v1_spans_width(gfx,spans,count)+((int)count-1)*gap;
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
        if (fixed_baseline>=0) baseline=fixed_baseline;
        else if (fixed_baseline==V1_CENTER_HANZI_BASELINE) {
            int ascent=(font==u8g2_font_wqy12_t_lunar ? 14:10)*scale/256;
            int ink_height=ascent+(scale+255)/256;
            baseline=y+(height-ink_height)/2+ascent;
        } else baseline=y+(height-GFX_getFontHeight(gfx))/2+GFX_getFontAscent(gfx);
#endif
    }
    int cursor_x = x;
    if (align == V1_ALIGN_CENTER) cursor_x = x + (width - text_width) / 2;
    if (align == V1_ALIGN_RIGHT) cursor_x = x + width - text_width;
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
    cursor_x-=ink_left;
#else
    int baseline = y + (height - GFX_getFontHeight(gfx)) / 2 +
        GFX_getFontAscent(gfx);
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
    if(V1_FRAME_NATIVE_FONTS()) {
        cursor_x-=run.left;
        baseline=y+(height-(run.bottom-run.top))/2-run.top;
        /* Fixed baselines are layout hints, not font-size requirements. Use
         * one only when this run's real glyph extents fit the row unchanged. */
        if(fixed_baseline>=0 && fixed_baseline+run.top>=y &&
            fixed_baseline+run.bottom<=y+height)baseline=fixed_baseline;
    }
#endif
    if (!V1_FRAME_NATIVE_FONTS() && fixed_baseline == V1_CENTER_HANZI_BASELINE) {
        /* Native weekday ink, not the font's shorter ASCII ascent. */
        int ascent = (font == u8g2_font_wqy12_t_lunar ? 14 : 10) * scale / 256;
        int ink_height = ascent + (scale + 255) / 256;
        baseline = y + (height - ink_height) / 2 + ascent;
    }
    if (!V1_FRAME_NATIVE_FONTS() && fixed_baseline >= 0) baseline = fixed_baseline;
#endif

    for (size_t i = 0u; i < count; i++) {
        if (tighten_month && i == 3u) cursor_x -= 2;
        GFX_setTextColor(gfx, spans[i].color, background);
        if (page_visible) {
            const char* text = spans[i].text;
            v1_print_at(gfx, cursor_x, baseline,
                        text[0] == ' ' && (text[1] == '\0' || (text[1] == ' ' && text[2] == '\0'))
                            ? "" : text);
            if (bold_numbers && spans[i].text[0]>='0' && spans[i].text[0]<='9')
                v1_print_at(gfx, cursor_x+1, baseline, spans[i].text);
        }
        cursor_x += v1_span_width(gfx, spans[i].text) + gap;
    }
    /* A paged render repeats the layout while clipping drawing to the active
     * strip.  Preserve the cursor/baseline side effects even when this text
     * box is outside that strip: callers use them to place adjacent markers
     * and lunar text.  Returning before layout made rows cut by a page
     * boundary reuse stale coordinates from an unrelated cell. */
    if (!page_visible) GFX_setCursor(gfx, cursor_x - gap, baseline);
}

#define v1_inline_text_at(g,x,y,w,h,s,n,f,p,m,b,a,l,gap) v1_inline_text_style_at(g,x,y,w,h,s,n,f,p,m,b,a,l,gap,false)

/* Color spans concatenate; actual spaces and explicit layout gaps stay content. */
#define v1_inline_text(g,x,y,w,h,s,n,f,p,m,b,a) v1_inline_text_at(g,x,y,w,h,s,n,f,p,m,b,a,-1,0)

/* Single-colour text is one span, with the compact ASCII ink baseline retained. */
static void v1_text(Adafruit_GFX* gfx, int x, int y, int width,
                    int height, const char* text, const uint8_t* font,
                    uint16_t scale, uint16_t minimum, uint16_t foreground,
                    uint16_t background, v1_align_t align) {
    if (!text) return;
    bool ascii = gfx->_width < 350;
    for (const unsigned char* p = (const unsigned char*)text; ascii && *p; p++)
        ascii = *p >= 32u && *p <= 126u;
    v1_span_t span = {text, foreground};
    v1_inline_text_at(gfx, x, y, width, height, &span, 1u, font, scale, minimum,
                      background, align, ascii ? y + (height - 8) / 2 + 8 : -1, 1);
}

static const char* const v1_date_units[] = {"年", "月", "日"};

/* Same mixed-colour date construction for the header and the calendar rail. */
static void v1_date_spans(const tm_t* time, uint16_t accent, v1_span_t* spans,
                           char numbers[][8], unsigned count) {
    const unsigned values[] = {time->tm_year + YEAR0, time->tm_mon + 1u, time->tm_mday};
    for (unsigned i = 0; i < count; i++) {
        GFX_format(numbers[i], 8u, "%u", values[i]);
        spans[i * 2] = (v1_span_t){numbers[i], accent};
        spans[i * 2 + 1] = (v1_span_t){v1_date_units[i], V1_BLACK};
    }
}

/* Square calendars and every compact clock share exactly one top row. */
static void v1_compact_header(Adafruit_GFX* gfx, const gui_data_t* data, const tm_t* time) {
    int width = v1_power_x(data) - 4;
    int height = v1_power_height(data);
    if (!GFX_pageIntersectsLogicalY(gfx, 0, height)) return;
    char numbers[3][8];
    uint16_t accent = v1_accent(data);
    v1_span_t spans[9];
    v1_date_spans(time, accent, spans, numbers, 3u);
    spans[6] = (v1_span_t){" ", V1_BLACK};
    spans[7] = (v1_span_t){"星期", V1_BLACK};
    spans[8] = (v1_span_t){Lunar_DayString[time->tm_wday], accent};
    size_t count = data->width == data->height ? 6u : 9u;
    v1_set_font(gfx, u8g2_font_wqy9_t_lunar, 256u);
    int gap = v1_inline_width(gfx, spans, count) + (int)count - 1 > width ? 0 : 1;
    uint16_t date_scale = v1_square_text_scale(data);
    const uint8_t* date_font = u8g2_font_wqy9_t_lunar;
    bool bold_date = date_scale > 256u;
    /* Only the 200px square header: keep room beside the temperature. */
    if (date_scale == 320u && !v1_native_user_fonts()) {
        date_font = u8g2_font_wqy12_t_lunar;
        date_scale = 224u;
    }
    if (V1_BUILTIN_HEADER(data,200u,200u)) {
        /* Built-in WQY9 advances: digits/minus7, dot6, Celsius13, V9.
         * The external native-font path never enters this header branch. */
        int power_width = v1_power_width(data);
        int temperature_width = data->temperature == GUI_TEMPERATURE_UNAVAILABLE
            ? 0 : power_width - 46;
        unsigned tenths = (data->voltage + 50u) / 100u;
        int text_left = data->width - v1_power_inset(data)
            - v1_clamp(power_width / 4,16,24) - 6
            - (tenths >= 100u ? 36 : 29) - temperature_width;
        width = v1_min(width,text_left - 4);
        v1_set_font(gfx,date_font,date_scale);
        int text_width=v1_spans_width(gfx,spans,count)+1;
        gap=v1_clamp((width-text_width)/5,1,3);
    }
    if (V1_BUILTIN_HEADER(data,152u,152u))
        gap = -gap - 1;
    bool covered = date_scale == 224u && !v1_native_user_fonts();
    if (covered) gfx->u8g2.draw_hv_line = v1_font_run_compact;
    v1_inline_text_style_at(gfx, 2, 0, width, height, spans, count,
                       date_font,
                       date_scale, 224u, V1_WHITE, V1_ALIGN_LEFT,
                       v1_header_baseline(data), gap, bold_date);
    if (covered) gfx->u8g2.draw_hv_line = v1_font_run;
    GFX_drawFastHLine(gfx, 2, height - 1, data->width - 4, V1_BLACK);
}

static const char* v1_ble_label(Adafruit_GFX* gfx, const char* name,
                                int width, uint16_t scale,
                                char* target, size_t target_size, bool brackets, bool compact);

/* Some vendor SDKs intentionally ship a tiny libc without strrchr().  Keep
 * this renderer self-contained rather than pulling in a larger libc solely
 * for BLE/board suffix formatting. */
#if defined(NRF52832_XXAA)
/* The factory validator accepts only NRF_ID4 or GR/OM/TL/DA_ID4 here. */
#define V1_FACTORY_SEPARATOR(factory) ((factory)+((factory)[0]=='N' ? 3u:2u))
#else
static const char* v1_last_char(const char* value, char needle) {
    const char* result = NULL;
    if (!value) return NULL;
    while (*value) {
        if (*value == needle) result = value;
        value++;
    }
    return result;
}

#define V1_FACTORY_SEPARATOR(factory) v1_last_char(factory,'_')
#endif

/* All callers keep a leading label: weekday, or BLE replacing that weekday. */
/* Rendering is serialized on every port. Reuse this short metadata buffer so
 * high-PPI paged rendering does not spend another 24 bytes on the deepest
 * font/Flash call chain. */
static char v1_lunar_ble[24];

static void v1_lunar_line(Adafruit_GFX* gfx, const gui_data_t* data,
                          int x, int y, int width, int height,
                          const tm_t* time, const struct Lunar_Date* lunar,
                          bool bluetooth, uint16_t preferred_scale,
                          uint16_t minimum_scale, v1_align_t align) {
    if (!GFX_pageIntersectsLogicalY(gfx, y - V1_PAGE_TEXT_GUARD,
                                   height + V1_PAGE_TEXT_GUARD * 2)) return;
    char stem_branch[16], month[20];
    uint8_t branch = LUNAR_GetBranch(lunar); /* Zodiac and branch share the same twelve-year index. */
    GFX_format(stem_branch, sizeof(stem_branch), "%s%s",
             Lunar_StemStrig[LUNAR_GetStem(lunar)], Lunar_BranchStrig[branch]);
    GFX_format(month, sizeof(month), "%s%s", Lunar_MonthLeapString[lunar->IsLeap], Lunar_MonthString[lunar->Month]);
    if (month[0] == ' ') memmove(month, month + 1, strlen(month));
    size_t length = strlen(month);
    if (length >= 3u && memcmp(month + length - 3u, "月", 3u) == 0)
        month[length - 3u] = '\0';
    uint16_t accent = v1_accent(data);
    const char* separation = !v1_compact(data) ? "  " : " ";
    v1_span_t full[] = {
        {"星期", V1_BLACK}, {Lunar_DayString[time->tm_wday], accent}, {separation, V1_BLACK}, {stem_branch, V1_BLACK},
        {Lunar_ZodiacString[branch], accent}, {"年", V1_BLACK}, {separation, V1_BLACK},
        {month, accent}, {"月", V1_BLACK}, {Lunar_DateString[lunar->Date], accent},
    };
    const v1_span_t* selected = full;
    size_t count = 10u;
    const uint8_t* font = data->width > 500u ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar;
    v1_set_font(gfx, font, preferred_scale);
    if (v1_inline_width(gfx, selected, count) > width) {
        /* Reuse the prefix buffer instead of materializing all fallback rows. */
        memmove(full + 3, full + 7, 3u * sizeof(*full));
        count = 6u;
        if (v1_inline_width(gfx, selected, count) > width) {
            full[3] = full[9]; count = 4u;
            if (v1_inline_width(gfx, selected, count) > width) count = 2u;
        }
    }
    if (bluetooth) {
        /* Keep the existing lunar detail level; shorten BLE before dropping lunar text. */
        int space = count > 2u ? v1_inline_width(gfx, full + 3, count - 3u) + 6 : 0;
        full[1] = (v1_span_t){v1_ble_label(gfx, v1_advertised_name(data), width - space, 256u,
                                          v1_lunar_ble, sizeof(v1_lunar_ble), true, v1_compact(data)), V1_BLACK};
        selected = full + 1;
        count--;
    }
    v1_inline_text(gfx, x, y, width, height, selected, count,
                   font, preferred_scale, minimum_scale, V1_WHITE, align);
}

static uint8_t v1_battery_percent(uint16_t millivolts) {
    uint16_t sample = (uint16_t)(((uint32_t)millivolts * 2047u) / 3600u);
    if (sample > 1705u) return 100u;
    if (sample > 1584u) {
        return (uint8_t)(28u + (sample - 1584u) * 72u / 121u);
    }
    if (sample > 1360u) {
        return (uint8_t)(4u + (sample - 1360u) * 3u / 28u);
    }
    if (sample > 1136u) {
        return (uint8_t)((sample - 1136u) / 56u);
    }
    return 0u;
}

static int v1_battery(Adafruit_GFX* gfx, int right, int y,
                          int icon_width, uint16_t millivolts,
                          int height) {
    int x = right - icon_width - 2;
    uint8_t level = v1_battery_percent(millivolts);
    uint16_t color = millivolts && millivolts <= GUI_BATTERY_RED_MAX_MV ? V1_RED : V1_BLACK;
    GFX_fillRect(gfx, x, y, icon_width + 2, height, V1_WHITE);
    GFX_drawRect(gfx, x, y, icon_width, height, color);
    GFX_fillRect(gfx, x + icon_width, y + (height - 2) / 2, 2, 2, color);
    GFX_fillRect(gfx, x + 2, y + 2, (icon_width - 4) * level / 100,
                 height - 4, color);
    return x;
}

static void V1_TEXT_SHARED v1_power_status(Adafruit_GFX* gfx, const gui_data_t* data) {
    int width = v1_power_width(data), height = v1_power_height(data);
    int x = data->width - v1_power_inset(data) - width;
    int baseline = v1_header_baseline(data);
    GFX_fillRect(gfx, x, 0, width, height, V1_WHITE);
    bool compact = height <= 20;
    int icon_width = compact ? 12 : height >= 36 ? 32 : v1_clamp(width / 4, 16, 24);
    int icon_height = compact ? 8 : height >= 36 ? 14 : 10;
    int icon_x = x + width - icon_width - 2;
    char voltage[12] = {0};
    char temperature[12] = {0};
    uint16_t voltage_tenths = (uint16_t)((data->voltage + 50u) / 100u);
    GFX_format(voltage, sizeof(voltage), "%u.%uV", voltage_tenths / 10u,
             voltage_tenths % 10u);
    if (data->temperature != GUI_TEMPERATURE_UNAVAILABLE)
        GFX_format(temperature, sizeof(temperature), "%d℃", data->temperature);
    uint16_t minimum = height < 36 ? V1_NATIVE_TEXT_SCALE
                                   : V1_MIN_META_SCALE;
    unsigned scale = 256u;
    /* Temperature and voltage share the qualified WQY9 metrics at every
     * geometry. WQY12 is a different face, not a larger version of WQY9. */
    const uint8_t* temperature_font=u8g2_font_wqy9_t_lunar;
    int temperature_width=0,voltage_width=0;
    bool show_voltage = v1_show_voltage(data);
    int battery_gap = 2;
    int label_gap = 4;
#if defined(NRF52832_XXAA)
    /* scale starts at256 and only decreases: these gaps are invariant. */
    label_gap=data->width<=250u ? 2:4;
#endif
    for (;;) {
#if !defined(NRF52832_XXAA)
        /* scale <= 384 already bounds these gaps below their old maxima. */
        battery_gap = v1_max((scale * 2u + 128u) / 256u, 2);
        label_gap = data->width <= 250u ? 2 : v1_max((scale * 4u + 128u) / 256u, 4);
#endif
        v1_set_font(gfx, temperature_font, scale);
        temperature_width=GFX_getUTF8Width(gfx, temperature);
#if !defined(NRF52832_XXAA)
        v1_set_font(gfx, u8g2_font_wqy9_t_lunar, scale);
#endif
        voltage_width=show_voltage ? GFX_getUTF8Width(gfx,voltage) : 0;
#if !defined(NRF52832_XXAA)
        v1_set_font(gfx,temperature_font,scale);
#endif
        int required = temperature_width +
            (show_voltage ? label_gap + voltage_width : 0) +
            battery_gap + x + width - icon_x;
        if ((required <= width && GFX_getFontHeight(gfx) <= height) || scale <= minimum) break;
        scale = scale > minimum + 16u ? (uint16_t)(scale - 16u) : minimum;
#if !defined(NRF52832_XXAA)
        GFX_setFontScale(gfx, scale);
#endif
    }
    int right=x+width;
    v1_battery(gfx, right, baseline + 1 - (GFX_getFontAscent(gfx) + icon_height) / 2 - 2,
               icon_width, data->voltage, icon_height);
    int text_right = icon_x - battery_gap;
    int voltage_x = text_right - voltage_width;
    GFX_setTextColor(gfx, V1_BLACK, V1_WHITE);

    if (show_voltage) {
#if !defined(NRF52832_XXAA)
        v1_set_font(gfx,u8g2_font_wqy9_t_lunar,scale);
#endif
        v1_print_at(gfx, voltage_x, baseline, voltage);
    }
#if !defined(NRF52832_XXAA)
    v1_set_font(gfx,temperature_font,scale);
#endif
    v1_print_at(gfx, show_voltage ? voltage_x - label_gap - temperature_width : x, baseline, temperature);
}

#if defined(INKTAG_GUI_HOST_PREVIEW) && !INKTAG_PROTOCOL_BASE_ONLY_TARGET
#include "../../../tools/emulator/inktag_gui_badge_preview.inc"
#elif defined(INKTAG_GUI_DEVICE_BADGE_PROVIDER) && !INKTAG_PROTOCOL_BASE_ONLY_TARGET
#include INKTAG_GUI_DEVICE_BADGE_PROVIDER
#else
#include "inktag_gui_edition_badge.inc"
#endif
static void v1_brand_mark(Adafruit_GFX* gfx, int x, int y,
                          int width, int height,
                          uint8_t color_mode) {
    int padding = v1_clamp(height / 5, 3, 8);
    int gap = height >= 20 ? 2 : 1;
    int inner_width = width - padding * 2;
    int inner_height = height - padding * 2;
    GFX_fillRect(gfx, x, y, width, height, V1_WHITE);
    GFX_drawRect(gfx, x, y, width, height, V1_BLACK);
    int columns = color_mode != 1u && color_mode != 2u && color_mode != 5u ? 3 : 2;
    int usable_width = inner_width-gap*(columns-1), previous = 0;
    for (int i = 0; i < columns; i++) {
        int edge = usable_width*(i+1)/columns;
        uint16_t color = i == 0 ? V1_BLACK : i == 2 || color_mode == 5u ? V1_YELLOW
            : color_mode <= 1u ? V1_BLACK : V1_RED;
        GFX_fillRect(gfx,x+padding+previous+gap*i,y+padding,edge-previous,inner_height,color);
        previous = edge;
    }
}

static void v1_brand_title(Adafruit_GFX* gfx, const gui_data_t* data,
                           int x, int y, int width,
                           int height) {
    if (!GFX_pageIntersectsLogicalY(gfx, y, height)) return;
    int mark_height = v1_clamp(height * 2 / 3, 10, 28);
    int mark_width = mark_height * 6 / 5;
    int mark_y = y + (height - mark_height) / 2;
    int scale = height >= 34 && width >= 120 ? 2 : 1;
    v1_brand_mark(gfx, x, mark_y, mark_width, mark_height, data->color);
    v1_block_word(gfx, x + mark_width + scale * 2,
                  y + (height - 7 * scale) / 2, scale, V1_BLACK, false);
}

static bool v1_holiday(uint16_t year, uint8_t month, uint8_t day,
                       bool* work) {
#if defined(INKTAG_V1_HOLIDAY_LOOKUP)
    return INKTAG_V1_HOLIDAY_LOOKUP(year, month, day, work);
#else
    static const uint16_t holidays[] = {
        0x0101, 0x0102, 0x0103, 0x1104, 0x120E, 0x020F, 0x0210,
        0x0211, 0x0212, 0x0213, 0x0214, 0x0215, 0x0216, 0x0217,
        0x121C, 0x0404, 0x0405, 0x0406, 0x0501, 0x0502, 0x0503,
        0x0504, 0x0505, 0x1509, 0x0613, 0x0614, 0x0615, 0x0919,
        0x091A, 0x091B, 0x1914, 0x0A01, 0x0A02, 0x0A03, 0x0A04,
        0x0A05, 0x0A06, 0x0A07, 0x1A0A,
    };
    if (year != 2026u) return false;
    for (size_t i = 0; i < sizeof(holidays) / sizeof(holidays[0]); i++) {
        if (((holidays[i] >> 8) & 0xFu) == month &&
            (holidays[i] & 0xFFu) == day) {
            *work = ((holidays[i] >> 12) & 0xFu) != 0u;
            return true;
        }
    }
    return false;
#endif
}

typedef struct {
    uint8_t month;
    uint8_t day;
    const char* name;
} v1_festival_t;

static const char* v1_find_festival(const v1_festival_t* days, size_t count, uint8_t month, uint8_t day) {
    for (size_t i = 0; i < count; i++)
        if (days[i].month == month && days[i].day == day) return days[i].name;
    return NULL;
}

static const char* v1_festival(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t weekday,
                               const struct Lunar_Date* lunar) {
    static const v1_festival_t solar[] = {
        {1, 1, "元旦节"}, {2, 14, "情人节"}, {3, 8, "妇女节"},
        {3, 12, "植树节"}, {4, 1, "愚人节"}, {5, 1, "劳动节"},
        {5, 4, "青年节"}, {6, 1, "儿童节"}, {7, 1, "建党节"},
        {8, 1, "建军节"}, {9, 10, "教师节"}, {10, 1, "国庆节"},
        {11, 1, "万圣节"}, {12, 24, "平安夜"}, {12, 25, "圣诞节"},
    };
    static const v1_festival_t lunar_days[] = {
        {1, 1, "春节"}, {1, 15, "元宵节"}, {2, 2, "龙抬头"},
        {5, 5, "端午节"}, {7, 7, "七夕节"}, {7, 15, "中元节"},
        {8, 15, "中秋节"}, {9, 9, "重阳节"}, {10, 1, "寒衣节"},
        {12, 8, "腊八节"}, {12, 30, "除夕"},
    };
    const char* festival = v1_find_festival(lunar_days, sizeof(lunar_days) / sizeof(lunar_days[0]), lunar->Month, lunar->Date);
    if (festival) return festival;
    if (lunar->Month == 12u && lunar->Date == 29u) {
        uint16_t next_year = year;
        uint8_t next_month = month;
        uint8_t next_day = (uint8_t)(day + 1u);
        if (next_day > thisMonthMaxDays((uint8_t)(year - YEAR0), month)) {
            next_day = 1u;
            if (++next_month > 12u) {
                next_month = 1u;
                next_year++;
            }
        }
        struct Lunar_Date next_lunar;
        LUNAR_SolarToLunar(&next_lunar, next_year, next_month, next_day);
        if (next_lunar.Month == 1u && next_lunar.Date == 1u) return "除夕";
    }
    if (month == 5u && weekday == 0u && day >= 8u && day <= 14u)
        return "母亲节";
    if (month == 6u && weekday == 0u && day >= 15u && day <= 21u)
        return "父亲节";
    if (month == 11u && weekday == 4u && day >= 22u && day <= 28u)
        return "感恩节";
    festival = v1_find_festival(solar, sizeof(solar) / sizeof(solar[0]), month, day);
    if (festival) return festival;
    uint8_t solar_term_day = 0u;
    if (GetJieQi(year, month, day, &solar_term_day) && solar_term_day == day) {
        uint8_t index = (uint8_t)((month - 1u) * 2u + (day >= 15u ? 1u : 0u));
        return index == 6u ? "清明节" : JieQiStr[index];
    }
    return NULL;
}

/* Round shared boundaries, spreading remainder pixels symmetrically instead
 * of assigning every remainder pixel to Sunday/the final column. */
static int v1_calendar_column(int width, uint8_t boundary) {
    return ((int32_t)width * boundary + 3) / 7;
}

static void v1_month_grid(Adafruit_GFX* gfx, tm_t* time,
                          const gui_data_t* data, int x, int y,
                          int width, int height, bool show_lunar) {
    if (!GFX_pageIntersectsLogicalY(gfx, y, height)) return;
    uint16_t year = (uint16_t)(time->tm_year + YEAR0);
    uint8_t month = (uint8_t)(time->tm_mon + 1u);
    uint8_t first_weekday = get_first_day_week(year, month);
    uint8_t first_index = (uint8_t)((first_weekday - data->week_start + 7u) % 7u);
    uint8_t maximum_days = thisMonthMaxDays((uint8_t)(year - YEAR0), month);
    uint8_t rows = (uint8_t)((first_index + maximum_days + 6u) / 7u);
    if (rows < 5u) rows = 5u;
    if (rows > 6u) rows = 6u;
    bool compact = v1_compact(data);
    bool narrow = compact || data->width < 350u;
    bool large = data->height >= 400u;
    int header_height = narrow ? (14*v1_square_text_scale(data)+128)/256 : v1_clamp(height / (show_lunar ? 6 : 7), 15,
                                     large ? 42 : 26);
    bool tight_lunar = data->width == 296u && data->height == 152u && rows == 6u && show_lunar && !v1_native_user_fonts();
    if (tight_lunar) header_height = 12;
    if (data->width == 400u && data->height == 300u && rows == 6u && !v1_native_user_fonts()) header_height = 22;
    int days_y = header_height + (narrow && show_lunar && !tight_lunar ? 2 : 0);
    int days_height = height - days_y;
    int cell_height = days_height / rows;
    uint16_t accent = v1_accent(data);
    uint16_t weekday_scale = narrow ?
        (data->width == 200u && data->height == 200u ? 288u : 256u) :
        (uint16_t)v1_min((int)v1_scale_q8(data), 512);
    if (GFX_pageIntersectsLogicalY(gfx, y, header_height)) {
        GFX_fillRoundRect(gfx, x, y, width, header_height, header_height / 2,
                          V1_BLACK);
        for (uint8_t column = 0; column < 7u; column++) {
            uint8_t weekday = (uint8_t)((data->week_start + column) % 7u);
            bool weekend = weekday == 0u || weekday == 6u;
            int column_x = v1_calendar_column(width, column);
            int cell_x = x + column_x;
            int cell_w = v1_calendar_column(width, column + 1u) - column_x;
            uint16_t background = weekend ? accent : V1_BLACK;
            if (weekend) {
                int fill_x=cell_x,fill_w=cell_w;
                if(column==0u || column==6u) {
                    GFX_fillRoundRect(gfx,cell_x,y,cell_w,header_height,
                        header_height/2,background);
                    int half=cell_w/2;
                    if(column==0u){fill_x+=half;fill_w-=half;}
                    else fill_w=half;
                }
                GFX_fillRect(gfx,fill_x,y,fill_w,header_height,background);
            }
            v1_span_t weekday_span = {Lunar_DayString[weekday], V1_WHITE};
            /* WQY9 counters close under 1px overprint; bold only the promoted face. */
            bool bold = weekday_scale >= 320u;
            v1_inline_text_at(gfx, cell_x, y, cell_w - bold, header_height - bold,
                    &weekday_span, 1u, u8g2_font_wqy9_t_lunar,
                    weekday_scale, V1_MIN_META_SCALE, background,
                    V1_ALIGN_CENTER, V1_CENTER_HANZI_BASELINE, 1);
            /* Thicken both axes by one output pixel without another font asset. */
            if (bold) {
                int left = gfx->tx - GFX_getUTF8Width(gfx, weekday_span.text), top = gfx->ty;
                for (int offset = 1; offset < 4; offset++)
                    v1_print_at(gfx, left + (offset & 1), top + (offset >> 1), weekday_span.text);
            }
        }
    }

    if (data->width >= 350u) {
        for (uint8_t column = 1u; column < 7u; column++)
            GFX_drawDottedLine(gfx, x + v1_calendar_column(width, column),
                               y + header_height,
                               x + v1_calendar_column(width, column), y + height - 1,
                               V1_BLACK, 1u, 5u);
        for (uint8_t row = 1u; row < rows; row++) {
            int line_y = y + header_height + row * cell_height;
            if (GFX_pageIntersectsLogicalY(gfx, line_y, 1))
                GFX_drawDottedLine(gfx, x, line_y, x + width - 1,
                                   line_y, V1_BLACK, 1u, 5u);
        }
    }

    for (uint8_t day = 1u; day <= maximum_days; day++) {
        uint8_t index = (uint8_t)(first_index + day - 1u);
        uint8_t column = (uint8_t)(index % 7u);
        uint8_t row = (uint8_t)(index / 7u);
        uint8_t actual_weekday = (uint8_t)((first_weekday + day - 1u) % 7u);
        int row_top = row * cell_height;
        int row_bottom = row_top + cell_height;
        int column_x = v1_calendar_column(width, column);
        int cell_x = x + column_x;
        int cell_y = y + days_y + row_top;
        int cell_w = v1_calendar_column(width, column + 1u) - column_x;
        int cell_h = row_bottom - row_top;
        if (!GFX_pageIntersectsLogicalY(gfx, cell_y, cell_h)) continue;
        int row_gap = narrow ? show_lunar ? 1 : 0 : v1_clamp(cell_height / 10, 2, 5);
        int content_h = v1_max(7, cell_height - row_gap);
        /* One compact date/lunar block, not two rows spread across a tall
         * cell. Preserve supplied native-font package geometry unchanged. */
        if (compact && show_lunar && !v1_native_user_fonts()) content_h=v1_min(content_h,21);
        int content_y = cell_y + (cell_h - content_h) / 2;
        bool highlighted = day == time->tm_mday;
        bool weekend = actual_weekday == 0u || actual_weekday == 6u;
        uint16_t foreground = highlighted ? V1_WHITE : weekend ? accent : V1_BLACK;
        uint16_t background = highlighted ? accent : V1_WHITE;
        int inset_x = narrow && show_lunar ? 1 : v1_clamp(cell_w / 10, 1, 5);
        bool compact_cell = compact;
        int inset_y = compact_cell ? 0
                                       : v1_clamp(cell_h / 12, 1, 4);
        int marker_size = compact_cell && cell_h < 14 ? 2 : 3;
        int marker_band = compact_cell ? show_lunar ? 1 : marker_size + 1 : 0;
        int numeral_h = tight_lunar ? 8 : 9;
        int lunar_h = show_lunar ? compact_cell ? content_h - numeral_h - marker_band : v1_clamp(content_h / 3, 7, 18) : 0;
        int number_h = compact_cell ? show_lunar ? numeral_h : v1_max(8, content_h - marker_band) : v1_max(7, content_h - lunar_h);
        char number[4] = {0};
        GFX_format(number, sizeof(number), "%u", day);
        uint16_t number_scale = compact_cell
            ? v1_min((int)v1_scale_q8(data), show_lunar && content_h < 28 ? 256 : 512)
            : v1_min(v1_scale_q8(data), 384);
        bool large_square = data->width == 200u && data->height == 200u && !show_lunar;
        bool reduced_square = large_square && !v1_native_user_fonts();
        if (large_square) number_scale=reduced_square ? 240u : 256u;
        const uint8_t* number_font = large_square ? u8g2_font_wqy12_t_lunar : compact_cell ? u8g2_font_wqy9_t_lunar : u8g2_font_helvB14_tn;
        v1_span_t number_span = {number, foreground};
        bool work = false;
        bool holiday = v1_holiday(year, month, day, &work);
        int digit_ink_height = reduced_square ? 10 : 12;
        int fixed_baseline=large_square ? content_y+(number_h-digit_ink_height)/2+digit_ink_height:
            compact_cell ? content_y+(number_h-8)/2+8 : -1;
        int selected_height=cell_h-inset_y*2;
        int selected_inset_y=inset_y;
        if (compact_cell && !v1_native_user_fonts()) {
            int ink_end=fixed_baseline-cell_y;
            if (show_lunar) {
                int ink_h=v1_min(11,v1_max(1,lunar_h));
                ink_end=content_y-cell_y+number_h+marker_band+(lunar_h-ink_h)/2+ink_h;
                selected_inset_y=v1_max(0,content_y-cell_y-2);
            } else if (holiday) ink_end+= (cell_h<(large_square?20:12)?1:2)+marker_size;
            selected_height=v1_min(cell_h,v1_max(1,ink_end+2))-selected_inset_y;
        }
        if (highlighted)
            GFX_fillRoundRect(gfx,cell_x+inset_x,cell_y+selected_inset_y,cell_w-inset_x*2,
                selected_height,compact_cell?2:v1_clamp(v1_min(cell_w,cell_h)/5,2,5),accent);
        if (reduced_square) gfx->u8g2.draw_hv_line = v1_font_run_compact;
        v1_inline_text_at(gfx, cell_x + 1, content_y, cell_w - 2, number_h, &number_span, 1u,
                number_font, number_scale, reduced_square ? 240u : compact_cell ? 256u : 112u,
                background, V1_ALIGN_CENTER,
                fixed_baseline, 1);
        if (reduced_square) gfx->u8g2.draw_hv_line = v1_font_run;
        int number_baseline = gfx->ty;
        int number_right = gfx->tx;
        if (holiday) {
            uint16_t marker_color = highlighted ? V1_WHITE : work ? V1_BLACK : accent;
            int number_width = GFX_getUTF8Width(gfx, number);
            int number_x = cell_x + 1 + (cell_w - 2 - number_width) / 2;
            if (compact_cell) {
                int baseline = number_baseline;
                int marker_width = marker_size == 2 ? 2 : 4;
                int mx = show_lunar ? cell_x + cell_w - inset_x - marker_width - 1 : number_x + (number_width - marker_width) / 2;
                if (show_lunar && ((data->width == 296u && data->height == 152u) ||
                    (data->width == 240u && data->height == 240u))) mx--;
                /* An 11px six-row cell cannot hold another pixel below the dot. */
                int my = show_lunar ? baseline - 6 : baseline + (cell_h < (large_square ? 20:12) ? 1 : 2);
                if (marker_size == 2) GFX_fillRect(gfx, mx, my, 2, 2, marker_color);
                else GFX_fillRoundRect(gfx, mx, my, marker_width, marker_size, 1, marker_color);
            } else {
                int mark_x = v1_min(cell_x + cell_w - 15, number_right + 2);
                int mark_baseline = v1_max(cell_y + 14, number_baseline - GFX_getFontAscent(gfx) + 10);
                if (highlighted) {
                    GFX_fillCircle(gfx, mark_x + 6, mark_baseline - 5, 8, V1_WHITE);
                    GFX_drawCircle(gfx, mark_x + 6, mark_baseline - 5, 8, accent);
                }
                v1_span_t mark = {work ? "班" : "休", work ? V1_BLACK : accent};
                v1_inline_text_at(gfx, mark_x, cell_y + inset_y + 2, 14, 15, &mark, 1u,
                                 u8g2_font_wqy9_t_lunar, 256u, 256u, V1_WHITE, V1_ALIGN_LEFT, mark_baseline, 1);
            }
        }
        if (show_lunar && GFX_pageIntersectsLogicalY(
                /* Wide cells place lunar ink relative to the number's
                 * actual baseline, not below its tall nominal layout box. */
                gfx, (compact_cell ? content_y + number_h + marker_band : number_baseline) - V1_PAGE_TEXT_GUARD,
                lunar_h + V1_PAGE_TEXT_GUARD * 2)) {
            struct Lunar_Date lunar;
            LUNAR_SolarToLunar(&lunar, year, month, day);
            const char* festival = v1_festival(year, month, day,
                                                actual_weekday, &lunar);
            char month_text[12] = {0};
            if (lunar.Date == 1u)
                GFX_format(month_text, sizeof(month_text), "%s%s",
                         Lunar_MonthLeapString[lunar.IsLeap],
                         Lunar_MonthString[lunar.Month]);
            const char* lunar_text = festival ? festival : lunar.Date == 1u
                ? month_text : Lunar_DateString[lunar.Date];
            while (*lunar_text == ' ') lunar_text++;
            char short_lunar[7];
            if (compact_cell && strlen(lunar_text) > 6u) {
                GFX_format(short_lunar, sizeof(short_lunar), "%s", lunar_text);
                lunar_text = short_lunar;
            }
            int lunar_y = content_y + number_h + marker_band;
            v1_span_t lunar_span = {lunar_text, highlighted ? V1_WHITE : festival ? accent : foreground};
            int lunar_inset = 1;
            int lunar_baseline;
            if (compact_cell) {
                /* Native calendar Hanzi occupy [-10, 1); shrink only with coverage. */
                unsigned scale = 256u;
                v1_set_font(gfx, u8g2_font_wqy9_t_lunar, scale);
                int ink_height = 11;
                while (scale > 128u && (ink_height > lunar_h || GFX_getUTF8Width(gfx, lunar_text) > cell_w - lunar_inset * 2)) {
                    scale -= 16u; GFX_setFontScale(gfx, scale);
                    ink_height = (10 * scale + 255) / 256 + (scale + 255) / 256;
                }
                lunar_baseline = lunar_y + (lunar_h - ink_height) / 2 + (10 * scale + 255) / 256;
                if (scale < 256u) gfx->u8g2.draw_hv_line = tight_lunar ? v1_font_run_compact : v1_font_run_covered;
            } else {
                v1_set_font(gfx, data->width > 500u ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar, 256u);
                lunar_baseline = v1_min(number_baseline + (data->width > 500u ? 19 : 13), cell_y + cell_h - inset_y - 3);
            }
            GFX_setTextColor(gfx, lunar_span.color, background);
            v1_print_at(gfx, cell_x + (cell_w - GFX_getUTF8Width(gfx, lunar_text)) / 2, lunar_baseline, lunar_text);
            gfx->u8g2.draw_hv_line = v1_font_run;
        }
    }
}

static const char* v1_factory_ble_name(const char* name) {
    if (!name) return NULL;
    const char* suffix = name;
    if (strncmp(suffix, "IKT_", 4u) == 0) suffix += 4;
    else if (strncmp(suffix, "InkTag_", 7u) == 0) suffix += 7;
    else return NULL;
    unsigned platform = strncmp(suffix, "NRF_", 4u) == 0 ? 4u
        : (strncmp(suffix, "GR_", 3u) == 0 || strncmp(suffix, "OM_", 3u) == 0 ||
           strncmp(suffix, "TL_", 3u) == 0 || strncmp(suffix, "DA_", 3u) == 0) ? 3u : 0u;
    if (!platform || strlen(suffix + platform) != 4u) return NULL;
    for (unsigned i = platform; i < platform + 4u; ++i)
        if (!((suffix[i] >= '0' && suffix[i] <= '9') ||
              (suffix[i] >= 'A' && suffix[i] <= 'F'))) return NULL;
    return suffix;
}

static const char* v1_ble_label(Adafruit_GFX* gfx, const char* name,
                                int width, uint16_t scale,
                                char* target, size_t target_size, bool brackets, bool compact) {
    if (!name || !name[0]) name = "InkTag";
    v1_set_font(gfx, u8g2_font_wqy9_t_lunar, scale);
    const char* factory = v1_factory_ble_name(name);
    /* Layout, not available width, chooses the name: compact factory labels
     * always show ID4; wide labels retain the exact advertised name. */
    const char* candidate = compact && factory ? V1_FACTORY_SEPARATOR(factory) + 1 : name;
    if (target_size < 3u) { if (target_size) target[0] = '\0'; return target; }
    size_t copied = v1_min(strlen(candidate), target_size - 3u);
    if (brackets) target[0] = '[';
    memcpy(target + brackets, candidate, copied);
    if (brackets) target[copied + 1u] = ']';
    target[copied + brackets * 2u] = '\0';
#if defined(NRF52832_XXAA)
    /* This exact length was just written above, including optional brackets. */
    size_t length=copied + brackets * 2u;
#else
    size_t length = strlen(target);
#endif
    while (length && GFX_getUTF8Width(gfx, target) > width) {
        if (brackets && length == 2u) { target[0] = '\0'; break; }
        if (brackets) --length;
        do { --length; } while (length > brackets &&
                               ((uint8_t)target[length] & 0xC0u) == 0x80u);
        if (brackets) target[length++] = ']';
        target[length] = '\0';
    }
    return target;
}

static void v1_seven_number(Adafruit_GFX* gfx, uint8_t number, int x,
                            int y, int size, int height_size, uint16_t color);

/* Bracketed BLE labels always use WQY9: WQY12 has no square brackets. */
static void v1_ble_text(Adafruit_GFX* gfx, const gui_data_t* data,
                        int x, int baseline, int width, uint16_t scale, v1_align_t align) {
    if (width <= 0) return;
    char label[24];
    v1_ble_label(gfx, v1_advertised_name(data), width, scale, label, sizeof(label), true, v1_compact(data));
    GFX_setFontScale(gfx, scale);
    int spare = width - GFX_getUTF8Width(gfx, label);
    if (align == V1_ALIGN_CENTER) x += spare / 2;
    if (align == V1_ALIGN_RIGHT) x += spare;
    GFX_setTextColor(gfx, V1_BLACK, V1_WHITE);
    v1_print_at(gfx, x, baseline, label);
}

static void v1_compact_footer(Adafruit_GFX* gfx, tm_t* time,
                              struct Lunar_Date* lunar, const gui_data_t* data) {
    int margin = 3, height = v1_compact_footer_height(data), badge = 66;
    int y = data->height - height;
    if (!GFX_pageIntersectsLogicalY(gfx, y, height)) return;
    int width = data->width - margin * 3 - badge;
    GFX_drawFastHLine(gfx, margin, y, data->width - margin * 2, V1_BLACK);
    if (data->width == data->height) {
        char label[24];
        v1_span_t ble = {v1_ble_label(gfx, v1_advertised_name(data), width, 256u,
                                    label, sizeof(label), true, true), V1_BLACK};
        /* Reuse native-face selection and its existing coverage/fit checks. */
        v1_inline_text_at(gfx, margin, y + 1, width, height - 1, &ble, 1u,
                       v1_native_user_fonts() ? u8g2_font_wqy9_t_lunar : u8g2_font_wqy12_t_lunar,
                       256u, 256u, V1_WHITE, V1_ALIGN_LEFT, V1_CENTER_HANZI_BASELINE, 0);
    } else {
        /* Native runs use the existing footer's inner band, not the legacy
         * 10px ink hint. This neither enlarges the footer nor scales a font. */
        bool native=v1_native_user_fonts();
        v1_lunar_line(gfx, data, margin, y + (native ? 1:6), width, native ? height-2:10,
                      time, lunar, true, 256u, 256u, V1_ALIGN_LEFT);
    }
    v1_edition_badge(gfx, data, data->width - margin - badge, y + 3, badge, height - 5, V1_ALIGN_RIGHT);
}

typedef struct { int x, y, width, height; } v1_frame_t;

static inktag_gui_digit_metrics_t v1_seven_metrics(int size) {
    int padding=v1_max(1,size/2);
    return (inktag_gui_digit_metrics_t){(uint16_t)(11*size+2),
        (uint16_t)(9*size+2),(uint16_t)(4*size+2*padding),
        (uint16_t)padding,(uint16_t)(20*size+2),0u};
}


/* Native text masters share one baseline; only the missing WQY12 brackets scale WQY9. */
typedef struct { const char* text; const uint8_t* font; uint16_t color, scale; int8_t gap; } v1_run_t;
static int v1_native_runs(Adafruit_GFX* gfx, const v1_run_t* runs, size_t count,
                              int x, int baseline) {
    int start = x;
    for (size_t i = 0; i < count; i++) {
        x += runs[i].gap;
        v1_set_font(gfx, runs[i].font, runs[i].scale);
        if (baseline >= 0) {
            GFX_setTextColor(gfx, runs[i].color, V1_WHITE);
            v1_print_at(gfx, x, baseline, runs[i].text);
        }
        x += GFX_getUTF8Width(gfx, runs[i].text);
    }
    return x - start;
}


static void V1_TEXT_SHARED v1_wide_date(Adafruit_GFX* gfx, const gui_data_t* data, const tm_t* time, int x) {
    char number[8];
    const uint16_t values[] = {time->tm_year + YEAR0, time->tm_mon + 1u, time->tm_mday};
    const uint8_t* unit = data->width > 500u ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar;
    const uint8_t* digit = data->width > 500u ? u8g2_font_helvB18_tn : u8g2_font_helvB14_tn;
    uint8_t group_gap = data->width > 500u ? 4u : 3u;
    uint16_t accent = v1_accent(data);
    int baseline = v1_header_baseline(data);
    v1_run_t date[] = {{number, digit, accent, 256u, 0}, {NULL, unit, V1_BLACK, 256u, 2}};
    for (uint8_t i = 0; i < 3u; i++) {
        GFX_format(number, sizeof(number), "%u", values[i]);
        date[1].text = v1_date_units[i];
        x += v1_native_runs(gfx, date, 2u, x, baseline);
        date[0].gap = group_gap;
    }
    GFX_format(number, sizeof(number), "%u", iso_week_of_year(values[0], values[1], values[2]));
    v1_run_t week[] = {
        {"[", u8g2_font_wqy9_t_lunar, V1_BLACK, data->width > 500u ? 384u : 256u, 10},
        {number, data->width > 500u ? u8g2_font_helvB14_tn : u8g2_font_wqy9_t_lunar, accent, 256u, 1},
        {"周", unit, V1_BLACK, 256u, 1},
        {"]", u8g2_font_wqy9_t_lunar, V1_BLACK, data->width > 500u ? 384u : 256u, data->width > 500u ? -2 : 0},
    };
    v1_native_runs(gfx, week, 4u, x, baseline);
}

static bool v1_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int V1_TEXT_SHARED v1_countdown(Adafruit_GFX* gfx, const gui_data_t* data, const tm_t* time,
                            const gui_countdown_t* target, int maximum) {
    const char* name = target && target->name ? target->name : "";
    while (v1_space(*name)) name++;
    size_t length = strlen(name);
    while (length && v1_space(name[length - 1])) length--;
    uint32_t days = 0;
    if (length && target->target_day && target->target_day >= data->timestamp / SEC_PER_DY)
        days = target->target_day - data->timestamp / SEC_PER_DY;
    else {
        uint8_t distance = 0;
        uint8_t term = GetJieQiStr(time->tm_year + YEAR0, time->tm_mon + 1u, time->tm_mday, &distance);
        if (term == 0xFFu) return 0;
        if (term == 24u) {
            uint8_t next_day = 0;
            if (GetJieQi(time->tm_year + YEAR0 + 1u, 1u, 1u, &next_day)) distance = 31u - time->tm_mday + next_day;
        }
        name = JieQiStr[term % 24u]; length = strlen(name); days = distance;
    }
    char label[64], number[8];
    GFX_setFont(gfx, u8g2_font_wqy12_t_lunar);
    v1_span_t span = {name, V1_BLACK};
    const uint8_t* font = v1_font_supports(gfx, &span, 1u) ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar;
    GFX_setFont(gfx, font);
    size_t used = 0;
    const unsigned char* cursor = (const unsigned char*)name;
    while ((size_t)(cursor - (const unsigned char*)name) < length) {
        const unsigned char* start = cursor;
        uint16_t glyph = v1_next_glyph(&cursor);
        size_t next = (size_t)(cursor - (const unsigned char*)name);
        if (next > 63u) break;
        if (u8g2_GetGlyphWidth(&gfx->u8g2, glyph) <= 0) label[used++] = '?';
        else { memcpy(label + used, start, (size_t)(cursor - start)); used += (size_t)(cursor - start); }
    }
    label[used] = '\0';
    GFX_format(number, sizeof(number), "%u", days);
    const uint8_t* body = data->width > 500u ? u8g2_font_wqy12_t_lunar : u8g2_font_wqy9_t_lunar;
    v1_run_t runs[] = {
        {"离", body, V1_BLACK, 256u, 0}, {"", font, v1_accent(data), data->width > 500u ? 320u : 256u, 2},
        {number, body, V1_BLACK, 256u, 3}, {"天", body, V1_BLACK, 256u, 1},
    };
    int fixed_width = v1_native_runs(gfx, runs, 4u, 0, -1);
    v1_set_font(gfx, font, runs[1].scale);
    v1_fit_text(gfx, label, maximum - fixed_width);
    int width = fixed_width + GFX_getUTF8Width(gfx, label);
    runs[1].text = label;
    /* Only wide layouts call countdown: their general row height is >=25. */
    int power_height=v1_power_height(data);
    v1_native_runs(gfx,runs,4u,data->width-v1_power_inset(data)-width,
        power_height+(power_height==23 ? 16:19));
    return width;
}

static v1_frame_t v1_wide_chrome(Adafruit_GFX* gfx, tm_t* time,
                                 struct Lunar_Date* lunar, const gui_data_t* data, bool top_rule,
                                 const gui_countdown_t* countdown) {
    int layout = v1_clamp(v1_min((int)(data->width * 256u / 400u),
                                    (int)(data->height * 256u / 300u)), 256, 512);
#define V1PX(v) ((int)(((v) * layout + 128) / 256))
    int power_height = v1_power_height(data), power_width = v1_power_width(data);
    int power_right = data->width - v1_power_inset(data);
    /* Wide rows reach23 only through the built-in400x300 header above. */
    bool header_42=power_height==23;
    int margin = V1PX(10), header = power_height + (header_42 ? 22 : 26), badge = data->width > 500u ? 122 : 82;
    int footer = data->width > 500u ? 31 : 21, badge_height = footer - 5;
    int footer_y = data->height - footer;
    int right = data->width - margin;
    if (GFX_pageIntersectsLogicalY(gfx, 0, header)) {
        v1_wide_date(gfx, data, time, margin);
        int countdown_width = v1_countdown(
            gfx, data, time, countdown, (data->width - margin * 2) / 2);
        int lunar_width = v1_min(power_right - power_width - margin - V1PX(8),
            power_right - countdown_width - margin - 8);
        int row_y = power_height + 2;
        int row_height = header - power_height - 4;
        v1_lunar_line(gfx, data, margin, row_y, lunar_width, row_height,
                      time, lunar, false, 256u, 256u, V1_ALIGN_LEFT);
        if (top_rule)
            GFX_drawFastHLine(gfx, margin, header - 1,
                              data->width - margin * 2, V1_BLACK);
    }
    if (GFX_pageIntersectsLogicalY(gfx, footer_y, footer)) {
        int ble_width = right - margin - badge - V1PX(8);
        uint16_t footer_scale = data->width <= 500u ? 256u : (uint16_t)v1_min(v1_scale_q8(data), 384);
        int footer_ascent = 10 * footer_scale / 256;
        int footer_ink_height = footer_ascent + (footer_scale + 255u) / 256u;
        GFX_drawFastHLine(gfx, margin, footer_y,
                          data->width - margin * 2, V1_BLACK);
        v1_ble_text(gfx, data, margin,
                    footer_y + 1 + (footer - 1 - footer_ink_height) / 2 + footer_ascent,
                    ble_width, footer_scale, V1_ALIGN_LEFT);
        v1_edition_badge(gfx, data, right - badge, footer_y + 3,
                         badge, badge_height, V1_ALIGN_RIGHT);
    }
    return (v1_frame_t){margin, header, data->width - margin * 2, footer_y - header - 2};
#undef V1PX
}

static int v1_calendar_date_field(const gui_data_t* data, inktag_gui_fixed_field_t* field) {
    v1_layout_preset_t preset=v1_layout_preset(data);
    if(preset.family!=V1_LAYOUT_COMPACT_RECT)return 0;
    int margin=3,rail=preset.calendar_lunar_cells ? 90:v1_max(74,v1_power_width(data)+1);
    int rail_x=data->width-margin-rail,header=v1_power_height(data);
    int month_height=preset.calendar_lunar_cells ? 24:20;
    int day_height=data->height-18-(month_height-2)-month_height-header;
    int size=v1_max(1,v1_min(2,v1_min((day_height-4)/20,(rail-4)/20)));
    if(day_height<=0 || rail_x<0)return 0;
    inktag_gui_digit_metrics_t metrics=v1_seven_metrics(size);
    inktag_gui_rect_t area={(int16_t)rail_x,(int16_t)header,(uint16_t)rail,(uint16_t)day_height};
    return inktag_gui_fixed_center(&metrics,2u,&area,field) ? size:0;
}

static void v1_calendar(Adafruit_GFX* gfx, tm_t* time,
                         struct Lunar_Date* lunar, const gui_data_t* data, const gui_countdown_t* countdown) {
    v1_layout_preset_t preset = v1_layout_preset(data);
    if (preset.family == V1_LAYOUT_COMPACT_SQUARE) {
        int margin = 2, header = v1_power_height(data), footer = v1_compact_footer_height(data);
        v1_compact_header(gfx, data, time);
        v1_month_grid(gfx, time, data, margin, header + 2,
                      data->width - margin * 2, data->height - header - 2 - footer,
                      preset.calendar_lunar_cells);
        v1_compact_footer(gfx, time, lunar, data);
        return;
    }
    if (preset.family == V1_LAYOUT_COMPACT_RECT) {
        int margin = 3, gap = 4, footer = 18, header = v1_power_height(data);
        int rail = preset.calendar_lunar_cells ? 90 : v1_max(74, v1_power_width(data) + 1);
        int grid = data->width - margin * 2 - rail - gap;
        int rail_x = margin + grid + gap, bottom = data->height - footer;
        int grid_y = 2;
        v1_month_grid(gfx, time, data, margin, grid_y, grid, bottom - grid_y, preset.calendar_lunar_cells);
        GFX_drawFastVLine(gfx, rail_x - 2, 2, bottom - 1, V1_BLACK);
        GFX_drawFastHLine(gfx, rail_x - 2, header - 1, data->width - margin - (rail_x - 2) + 1, V1_BLACK);
        int month_height = preset.calendar_lunar_cells ? 24 : 20;
        int bluetooth_height = month_height - 2;
        int month_y = bottom - bluetooth_height - month_height;
        inktag_gui_fixed_field_t day;
        int day_size=v1_calendar_date_field(data,&day);
        if(day_size && !v1_role_digits(gfx,time->tm_mday,0u,v1_accent(data))) v1_seven_number(gfx,time->tm_mday,day.digit_x[0],
                                    day.rectangle.y,day_size,day_size,v1_accent(data));
        GFX_drawDottedLine(gfx, rail_x - 2, month_y, data->width - margin, month_y, V1_BLACK, 2u, 2u);
        GFX_drawDottedLine(gfx, rail_x - 2, bottom - bluetooth_height, data->width - margin, bottom - bluetooth_height, V1_BLACK, 2u, 2u);
        char numbers[2][8];
        v1_span_t spans[4];
        v1_date_spans(time, v1_accent(data), spans, numbers, 2u);
        v1_inline_text_at(gfx, rail_x + 2, month_y + 3, rail - 4, month_height - 5, spans, 4u,
                         u8g2_font_wqy9_t_lunar, 256u, 256u, V1_WHITE, V1_ALIGN_CENTER,
                         month_y + (month_height - 14) / 2 + 12, 1);
        v1_span_t weekday[] = {{"星期", V1_BLACK}, {Lunar_DayString[time->tm_wday], v1_accent(data)}};
        v1_inline_text_at(gfx, rail_x + 2, bottom - bluetooth_height, rail - 4, bluetooth_height,
                         weekday, 2u, u8g2_font_wqy9_t_lunar, 256u, 256u, V1_WHITE, V1_ALIGN_CENTER,
                         bottom - bluetooth_height + (bluetooth_height - 11) / 2 + 11, 1);
        v1_compact_footer(gfx, time, lunar, data);
        return;
    }
    v1_frame_t body = v1_wide_chrome(gfx, time, lunar, data, false, countdown);
    v1_month_grid(gfx, time, data, body.x, body.y, body.width, body.height, true);
}

static int v1_time_width(int size) {
    inktag_gui_digit_metrics_t metrics=v1_seven_metrics(size);
    return inktag_gui_fixed_width(&metrics,4u);
}

static void v1_seven_number(Adafruit_GFX* gfx, uint8_t number, int x,
                            int y, int size, int height_size, uint16_t color) {
    static const uint8_t digits[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    /* x/y coefficients and offsets, then horizontal flag. One stroke kernel. */
    static const uint8_t segments[7][5] = {
        {1,1,0,0,1}, {7,1,1,1,0}, {7,1,10,2,0}, {1,1,18,2,1},
        {0,0,10,2,0}, {0,0,1,1,0}, {1,1,9,1,1},
    };
    if (!GFX_pageIntersectsLogicalY(gfx, y, 18 * height_size + 2 * size + 2)) return;
    for (uint8_t place = 0; place < 2; place++) {
        uint8_t digit = place ? number % 10u : number / 10u;
        for (uint8_t s = 0; s < 7; s++) {
            if (!(digits[digit] & (1u << s))) continue;
            const uint8_t* segment = segments[s];
            int sx = x + place * (11 * size + 2) + segment[0] * size + segment[1];
            int sy = y + segment[2] * height_size + segment[3];
            for (int row = 0; row < 2 * size; row++) {
                int spread = row < size ? row : 2 * size - row;
                if (segment[4])
                    GFX_drawFastHLine(gfx, sx + size - spread, sy + row, 5 * size + 2 * spread, color);
                else
                    GFX_drawFastVLine(gfx, sx + row, sy + size - spread, 7 * height_size + 2 * spread, color);
            }
        }
    }
}

static int v1_clock_height_size(const gui_data_t* data, int size) {
    (void)data;
    return size;
}

static int v1_clock_field(const gui_data_t* data,inktag_gui_fixed_field_t* field) {
    v1_layout_preset_t preset = v1_layout_preset(data);
    v1_layout_family_t family = preset.family;
    bool compact = family <= V1_LAYOUT_COMPACT_RECT;
    int margin = compact ? 2 : v1_power_inset(data);
    int layout=v1_clamp(v1_min((int)(data->width*256u/400u),
                              (int)(data->height*256u/300u)),256,512);
    int wide_margin=(10*layout+128)/256;
    int power_height=v1_power_height(data);
    /* In compact layouts wide_header is unused; general wide rows are >=25. */
    int wide_header=power_height+(power_height==23 ? 22:26);
    int wide_footer=data->width>500u ? 31:21;
    v1_frame_t body = compact
        ? (v1_frame_t){margin, v1_power_height(data), data->width - margin * 2,
                      data->height - v1_power_height(data) - v1_compact_footer_height(data)}
        : (v1_frame_t){wide_margin,wide_header,data->width-wide_margin*2,
                      data->height-wide_footer-wide_header-2};
    int header = body.y, available = body.height;
    if(available<=0)return 0;
    /* Clock typography must not depend on calendar lunar visibility. */
    int size = family == V1_LAYOUT_COMPACT_SQUARE ? (data->width <= 160u ? 2 : data->width >= 240u ? 4 : 3)
        : compact ? (v1_min(data->width,data->height) >= 150 ? 4 : data->height <= 110u ? 2 : 3)
        : v1_max(4, (data->width - margin * 2 - 8) * 8 / 470);
    while (size > 1 &&
           (v1_time_width(size) > data->width - margin * 2 ||
            20 * size + 2 > available - 4)) size--;
    inktag_gui_digit_metrics_t metrics=v1_seven_metrics(size);
    metrics.height=(uint16_t)(18*v1_clock_height_size(data,size)+2*size+2);
    inktag_gui_rect_t area={0,(int16_t)header,data->width,(uint16_t)available};
    return inktag_gui_fixed_center(&metrics,4u,&area,field) ? size:0;
}

static void v1_clock(Adafruit_GFX* gfx, tm_t* time,
                     struct Lunar_Date* lunar, const gui_data_t* data, const gui_countdown_t* countdown) {
    v1_layout_family_t family=v1_layout_preset(data).family;
    bool compact=family<=V1_LAYOUT_COMPACT_RECT;
    bool square=family==V1_LAYOUT_COMPACT_SQUARE;
    if(compact)v1_compact_header(gfx,data,time);
    else (void)v1_wide_chrome(gfx,time,lunar,data,true,countdown);
    inktag_gui_fixed_field_t field;
    int size=v1_clock_field(data,&field);
    if(!size)return;
    int time_y=field.rectangle.y,colon_x=field.colon_x;
    int height_size=v1_clock_height_size(data,size);
    if(!v1_role_digits(gfx,time->tm_hour,time->tm_min,V1_BLACK)) {
        /* Keep digit cells fixed: a narrow leading 1 does not move HH:MM. */
        v1_seven_number(gfx, time->tm_hour, field.digit_x[0], time_y, size, height_size, V1_BLACK);
        GFX_fillRect(gfx, colon_x, time_y + 9 * height_size / 2 + 1,
                     2 * size, 2 * size, V1_BLACK);
        GFX_fillRect(gfx, colon_x, time_y + 27 * height_size / 2 + 3,
                     2 * size, 2 * size, V1_BLACK);
        v1_seven_number(gfx, time->tm_min, field.digit_x[2],
                        time_y, size, height_size, V1_BLACK);
    }
    if (compact) {
        if (square) {
            int lunar_width = v1_min(data->width - 6, 128);
            v1_lunar_line(gfx, data, (data->width - lunar_width) / 2,
                          data->height - v1_compact_footer_height(data) - 16,
                          lunar_width, 12, time, lunar, false, 256u, 256u, V1_ALIGN_CENTER);
        }
        v1_compact_footer(gfx, time, lunar, data);
    }
}

static void v1_qr(Adafruit_GFX* gfx, int x, int y,
                  int side, const uint8_t* bytes, uint8_t modules) {
    int outer_modules = modules + 8;
    if (!GFX_pageIntersectsLogicalY(gfx, y, side)) return;
    GFX_fillRect(gfx, x, y, side, side, V1_WHITE);
    for (uint8_t row = 0; row < modules; row++) {
        uint16_t bit = (uint16_t)row * modules;
        int top = (row + 4) * side / outer_modules;
        int bottom = (row + 5) * side / outer_modules;
        if (!GFX_pageIntersectsLogicalY(
                gfx, y + top, bottom - top)) continue;
        for (uint8_t column = 0; column < modules; column++) {
            uint8_t packed = u8x8_pgm_read(bytes + (bit >> 3));
            uint8_t mask = (uint8_t)(0x80u >> (bit & 7u));
            if ((packed & mask) != 0u) {
                int left = (column + 4) * side / outer_modules;
                int right = (column + 5) * side / outer_modules;
                GFX_fillRect(gfx, x + left, y + top,
                             right - left, bottom - top, V1_BLACK);
            }
            bit++;
        }
    }
}

static const char* v1_board_display_name(Adafruit_GFX* gfx, const gui_data_t* data,
                               int width, int height, uint16_t scale,
                               uint16_t minimum) {
    const char* name = data->board_name && data->board_name[0]
        ? data->board_name : "BOARD";
    if (data->board_name_short && data->board_name_short[0]) {
        const uint8_t* font = u8g2_font_wqy9_t_lunar;
        v1_span_t span = {name, V1_BLACK};
        v1_text_face(gfx, &span, 1u, &font, &scale, &minimum, width, height);
        v1_set_font(gfx, font, scale);
        if (v1_text_width(gfx, name) > width || GFX_getFontHeight(gfx) > height)
            return data->board_name_short;
    }
    return name;
}

static void v1_info_rows(Adafruit_GFX* gfx, const gui_data_t* data,
                         int x, int y, int width, int height,
                         const char* const keys[], const char* const values[],
                         uint8_t count, const uint8_t* order, uint16_t scale) {
    int row_height = height / count;
    bool narrow = v1_compact(data) || data->width < 350u;
    int key_width = keys ? v1_clamp(width / 4, 34, 92) : 0;
    int column_gap = !keys ? 0 : narrow ? 8 : v1_clamp(width / 24, 8, 16);
    int value_x = x + key_width + column_gap;
    int value_width = v1_max(1, width - key_width - column_gap);
    for (uint8_t i = 0; i < count; i++) {
        uint8_t field = order ? order[i] : i;
        int row_y = y + i * row_height;
        bool row_visible = GFX_pageIntersectsLogicalY(
            gfx, row_y, row_height);
        uint16_t minimum = narrow ? V1_NATIVE_TEXT_SCALE
                                              : V1_MIN_META_SCALE;
        if (row_visible) {
            if (keys) v1_text(gfx, x, row_y, key_width, row_height, keys[field],
                    u8g2_font_wqy9_t_lunar, scale, minimum,
                    v1_accent(data), V1_WHITE, V1_ALIGN_RIGHT);
            char fitted[96];
            GFX_format(fitted, sizeof(fitted), "%s", values[field]);
            v1_set_font(gfx, u8g2_font_wqy9_t_lunar, scale);
            v1_fit_text(gfx, fitted, value_width);
            v1_text(gfx, value_x, row_y, value_width, row_height,
                    fitted, u8g2_font_wqy9_t_lunar,
                    scale, minimum, V1_BLACK, V1_WHITE,
                    keys ? V1_ALIGN_LEFT : V1_ALIGN_CENTER);
        }
        if (i + 1u < count && GFX_pageIntersectsLogicalY(
                gfx, row_y + row_height - 1, 1))
            GFX_drawDottedLine(gfx, x, row_y + row_height - 1,
                               x + width - 1, row_y + row_height - 1,
                               V1_BLACK, 1u, 4u);
    }
}

static void v1_screen_token(char* target, size_t size, const char* value) {
    if (!value || !value[0]) value = "DISPLAY";
    size_t i = 0u;
    while (value[i] && value[i] != ' ' && value[i] != '/' && i < 11u &&
           i + 1u < size) {
        target[i] = value[i];
        i++;
    }
    target[i] = '\0';
}

static void v1_device_display_name(char* target, size_t size,
                                   const char* advertised_name,
                                   bool compact) {
    const char* value = advertised_name && advertised_name[0]
        ? advertised_name : "InkTag";
    const char* factory = v1_factory_ble_name(value);
    const char* suffix = factory ? V1_FACTORY_SEPARATOR(factory) : NULL;
    if (suffix) {
        GFX_format(target, size, compact ? "%s" : "InkTag %s", suffix + 1);
        return;
    }
    GFX_format(target, size, "%s", value);
}


static void v1_device_info(Adafruit_GFX* gfx, const gui_data_t* data) {
    v1_layout_preset_t preset = v1_layout_preset(data);
    v1_layout_family_t family = preset.family;
    bool compact = family == V1_LAYOUT_COMPACT_SQUARE ||
                   family == V1_LAYOUT_COMPACT_RECT;
    bool square = family == V1_LAYOUT_COMPACT_SQUARE;
    int margin = compact ? 3
        : v1_clamp(v1_min(data->width, data->height) / 24, 10, 28);
    int header = v1_power_height(data);
    int footer = compact ? 18 : data->width > 500u ? 31 : 21;
    int badge_width = compact ? 66 : family == V1_LAYOUT_LARGE ? 122 : 82;
    int badge_height = footer - 5;
    int status_x = v1_power_x(data);
    int brand_height = data->width > 500u ? header - 3 : v1_min(30, header - 5);
    int brand_y = v1_header_baseline(data) - (header>20 ? 7:5) - brand_height / 2;
    v1_brand_title(gfx, data, margin, brand_y, status_x - margin * 2, brand_height);
    GFX_drawFastHLine(gfx, margin, header - 1,
                      data->width - margin * 2, V1_BLACK);
    int content_y = header;
    int content_height = data->height - content_y - footer;
    uint8_t modules = data->device_info_qr_modules[0]
        ? data->device_info_qr_modules[0] : INKTAG_DEFAULT_DEVICE_QR_MODULES;
    const uint8_t* qr = data->device_info_qr_data[0]
        ? data->device_info_qr_data[0] : inktag_default_device_qr;
    int maximum_qr_section_width = data->width / 2;
    int qr_limit = compact
        ? v1_min(v1_min(content_height, maximum_qr_section_width), 132)
        : v1_min(v1_min(content_height, maximum_qr_section_width), 300);
    /* Freeze columns to the original default matrix, not the mutable URL or
     * credential. Preserve four quiet modules on every edge. */
    int reference_side = (INKTAG_DEFAULT_DEVICE_QR_MODULES + 8) *
        v1_max(1, qr_limit / (INKTAG_DEFAULT_DEVICE_QR_MODULES + 8));
    int qr_section_width = compact ? v1_min(maximum_qr_section_width, reference_side * 5 / 4)
        : v1_max(reference_side, v1_min(data->width * 47 / 100, reference_side * 6 / 5));
    int qr_scale = v1_max(1, v1_min(qr_limit, qr_section_width) / (modules + 8));
    int qr_side = (modules + 8) * qr_scale;
    /* Square 1.54" matrices use the original default outer box. Integer
     * boundary mapping keeps all modules and four quiet modules without
     * dropping a saved URL/credential from 66px to 41px. Other layouts retain
     * their original integer size and columns. */
    if (square && modules + 8 <= reference_side)
        qr_side = reference_side;
    int qr_x = (qr_section_width - qr_side) / 2;
    int qr_y = content_y + (content_height - qr_side) / 2;
    v1_qr(gfx, qr_x, qr_y, qr_side, qr, modules);
    GFX_drawFastVLine(gfx, qr_section_width, content_y, content_height,
                      V1_BLACK);
    int info_x = qr_section_width + margin;
    int info_width = data->width - info_x - margin;
    /* Every formatter below writes and terminates its destination before use;
     * avoid clearing 96 bytes on every one of this paged renderer's passes. */
    char ble[24];
    char device_name[24];
    char screen[20];
    char epd[28];
    v1_screen_token(screen, sizeof(screen), data->screen_info);
    GFX_format(epd, sizeof(epd), "%s %s", screen,
               GUI_EpdColorName(data->color));
    int key_width = v1_clamp(info_width / 4, 34, 92);
    int info_gap = compact || data->width < 350u ? 8
        : v1_clamp(info_width / 24, 8, 16);
    int value_width = square ? info_width : info_width - key_width - info_gap;
    uint16_t info_scale = compact || data->width < 350u ? 256u : (uint16_t)v1_min(
        (int)v1_scale_q8(data),
        data->width >= 600u ? 448 : data->width < 350u ? 384 : 320);
    static const char* const keys[] = {"NAME", "BLE", "MCU", "EPD", "FW", "TIER"};
    const char* advertised_name = v1_advertised_name(data);
    v1_device_display_name(device_name, sizeof(device_name), advertised_name, false);
    const char* display_name = data->device_name && data->device_name[0]
        ? data->device_name : data->bluetooth_name && data->ssid[0]
        ? data->ssid : device_name;
    const char* values[] = {
        display_name,
        v1_ble_label(gfx, advertised_name, value_width, info_scale,
                     ble, sizeof(ble), false, square),
        data->mcu_name && data->mcu_name[0] ? data->mcu_name : "--",
        epd, "v" INKTAG_VERSION_STRING,
#if defined(INKTAG_GUI_HOST_PREVIEW) && !INKTAG_PROTOCOL_BASE_ONLY_TARGET
#include "../../../tools/emulator/inktag_gui_edition_name_preview.inc"
#elif defined(INKTAG_GUI_DEVICE_NAME_PROVIDER) && !INKTAG_PROTOCOL_BASE_ONLY_TARGET
#include INKTAG_GUI_DEVICE_NAME_PROVIDER
#else
#include "inktag_gui_edition_name.inc"
#endif
    };
    static const uint8_t compact_order[] = {1, 2, 4, 5};
    /* The caller has already resolved the rotated, visible canvas. */
    bool short_rows = data->height < 150u;
    v1_info_rows(gfx, data, info_x, content_y, info_width, content_height,
                 square ? NULL : keys, values, short_rows ? 4u : 6u,
                 short_rows ? compact_order : NULL, info_scale);
    int footer_y = data->height - footer;
    GFX_drawFastHLine(gfx, margin, footer_y,
                      data->width - margin * 2, V1_BLACK);
    v1_span_t title[] = {{"DEVICE", V1_BLACK}, {"INFO", V1_BLACK}};
    uint16_t caption_scale = compact ? 256u : (uint16_t)v1_min((int)v1_scale_q8(data), 384);
    int cap_height = data->width <= 500u ? 8 : 8 * caption_scale / 256;
    v1_inline_text_at(gfx, margin, footer_y + 1,
            data->width - badge_width - margin * 3, footer - 1,
            title, 2u, u8g2_font_wqy9_t_lunar,
            caption_scale,
            compact ? V1_NATIVE_TEXT_SCALE : V1_MIN_META_SCALE,
            V1_WHITE, V1_ALIGN_LEFT, footer_y + 1 + (footer - 1 - cap_height) / 2 + cap_height,
            data->width > 500u ? (3 * caption_scale + 128) / 256 : 2);
    v1_edition_badge(gfx, data, data->width - margin - badge_width,
                     footer_y + 3, badge_width, badge_height, V1_ALIGN_RIGHT);
}

static void V1_GR_PAGE_SHARED DrawLayoutV1Page(Adafruit_GFX* gfx, gui_data_t* data,
                                 uint8_t page, tm_t* time,
                                 struct Lunar_Date* lunar,
                                 const gui_countdown_t* countdown) {
    gfx->u8g2.draw_hv_line = v1_font_run;
    if (GFX_pageIntersectsLogicalY(gfx, 0, v1_power_height(data)))
        v1_power_status(gfx, data);
    if (page == 0u)
        v1_calendar(gfx, time, lunar, data, countdown);
    else if (page == 1u)
        v1_clock(gfx, time, lunar, data, countdown);
    else
        v1_device_info(gfx, data);
}

