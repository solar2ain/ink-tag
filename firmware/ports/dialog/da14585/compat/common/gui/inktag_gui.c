#if defined(INKTAG_GUI_RENDERER_LEGACY)
#error INKTAG_GUI_RENDERER_LEGACY_has_been_retired
#endif

#include "inktag_gui.h"

#include <string.h>

#include "inktag_display_settings.h"
#include "inktag_gui_assets.h"
#include "inktag_gui_calendar.inc"

const char* GUI_EpdColorName(uint16_t color) {
    if (color == 1u) return "BW";
    if (color == 2u) return "BWR";
    if (color == 3u) return "BWRY";
    if (color == 10u) return "E6";
    return "COLOR";
}

/* Only the current renderer is compiled; historical snapshots remain in Git. */
#define INKTAG_V1_HOLIDAY_LOOKUP GetHoliday
#include "inktag_gui_default.inc"
#undef INKTAG_V1_HOLIDAY_LOOKUP
#undef GFX_getUTF8Width

const char* GUI_BoardDisplayName(Adafruit_GFX* gfx, const gui_data_t* data,
                               int width, int height, uint16_t scale,
                               uint16_t minimum) {
    return v1_board_display_name(gfx, data, width, height, scale, minimum);
}

bool GUI_CountdownNameSupported(const char* name, uint8_t length) {
    char bounded[GUI_COUNTDOWN_NAME_MAX_BYTES + 1u];
    const unsigned char* cursor;
    const unsigned char* end;
    uint8_t codepoints = 0u;
    u8g2_font_t font12 = {0};
    u8g2_font_t font9 = {0};
    if (name == NULL || length == 0u || length > GUI_COUNTDOWN_NAME_MAX_BYTES)
        return false;
    memcpy(bounded, name, length);
    bounded[length] = '\0';
    cursor = (const unsigned char*)bounded;
    end = cursor + length;
    u8g2_SetFont(&font12, u8g2_font_wqy12_t_lunar);
    u8g2_SetFont(&font9, u8g2_font_wqy9_t_lunar);
    while (cursor < end) {
        uint16_t glyph = v1_next_glyph(&cursor);
        if (cursor > end || ++codepoints > 4u ||
            (!u8g2_IsGlyph(&font12, glyph) && !u8g2_IsGlyph(&font9, glyph)))
            return false;
    }
    return cursor == end;
}

static void gui_begin_canvas(Adafruit_GFX* gfx,int16_t width,int16_t height,
                             int16_t page_height,uint16_t color,
                             uint8_t* buffer,uint32_t buffer_size) {
    bool two_planes = color == 2u || color == 3u;
    if (two_planes) {
        if (page_height < 2) page_height = 2;
        page_height &= ~1;
    }
    GFX_begin_with_buffer(gfx,width,height,page_height,buffer,buffer_size);
    if (two_planes) {
        if (!gfx->buffer || gfx->page_height < 2) {
            GFX_end(gfx);
            memset(gfx,0,sizeof(*gfx));
            return;
        }
        gfx->page_height /= 2;
        gfx->color = gfx->buffer;
        if (color == 2u) gfx->color += ((gfx->WIDTH+7)/8)*gfx->page_height;
        gfx->total_pages = gfx->HEIGHT/gfx->page_height + (gfx->HEIGHT%gfx->page_height > 0);
    }
}

bool DrawGUIEx(gui_data_t* data, buffer_callback callback, void* callback_data, const gui_options_t* options) {
    /* Retired page4 must not allocate, fetch assets or emit a blank frame. */
    if (data == NULL || data->mode == MODE_LOW_BATTERY) return false;
    if (data->week_start > 1)
        data->week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;

    tm_t tm = {0};
    struct Lunar_Date Lunar;
    gui_data_t draw_data = *data;
    if (draw_data.timestamp == 0u)
        draw_data.timestamp = GUI_UNSET_DISPLAY_TIMESTAMP;
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    uint16_t visible_x = data->visible_x < data->width ? data->visible_x : 0;
    uint16_t visible_y = data->visible_y < data->height ? data->visible_y : 0;
    uint16_t visible_width =
        data->visible_width && data->visible_width <= data->width - visible_x ? data->visible_width : data->width;
    uint16_t visible_height =
        data->visible_height && data->visible_height <= data->height - visible_y ? data->visible_height : data->height;
    draw_data.width = visible_width;
    draw_data.height = visible_height;
    /* Keep allocation, paging and callbacks on the controller RAM geometry.
     * The logical layout may be smaller when the glass exposes fewer pixels
     * than the controller stride (for example 122 visible columns in a
     * 128-column RAM frame). */
    uint16_t canvas_width = data->width;
    uint16_t canvas_height = data->height;
#else
    draw_data.width = data->visible_width && data->visible_width <= data->width ? data->visible_width : data->width;
    draw_data.height =
        data->visible_height && data->visible_height <= data->height ? data->visible_height : data->height;
    uint16_t canvas_width = draw_data.width;
    uint16_t canvas_height = draw_data.height;
#endif

    GFX_Rotate rotation = GFX_ROTATE_0;
    if (data->force_rotation) {
        rotation = data->rotation;
    } else if (small_layout(&draw_data) && draw_data.width < draw_data.height) {
        rotation = GFX_ROTATE_90;
    }
    if (rotation == GFX_ROTATE_90 || rotation == GFX_ROTATE_270) {
        uint16_t physical_width = draw_data.width;
        draw_data.width = draw_data.height;
        draw_data.height = physical_width;
    }

    transformTime(draw_data.timestamp, &tm);

    Adafruit_GFX gfx;
    int16_t row_bytes = (canvas_width + 7) / 8;
#if defined(__HEAP_SIZE)
    uint32_t available_bytes = (__HEAP_SIZE > 1024) ? (__HEAP_SIZE - 1024u) : 0u;
#else
    uint32_t available_bytes = 0;
#endif
    if (options && options->render_buffer) available_bytes = options->render_buffer_size;
    uint32_t available_height = available_bytes / row_bytes;
    if (available_height > INT16_MAX) available_height = INT16_MAX;
    int16_t ph = (int16_t)available_height;
    uint16_t buffer_color = data->color;
    if (options && options->force_buffer_color) buffer_color = options->buffer_color;

    bool full_frame = false;
    if (options && options->prefer_full_frame) {
        uint32_t plane_count = (buffer_color == 2 || buffer_color == 3) ? 2u : 1u;
        uint32_t required_ph = (uint32_t)canvas_height * plane_count;
        uint32_t required_bytes = (uint32_t)row_bytes * required_ph;
        if (required_ph <= INT16_MAX && required_bytes <= available_bytes) {
            ph = (int16_t)required_ph;
            full_frame = true;
        }
    }
    if (!full_frame && (!options || !options->prefer_largest_page) && ph > 64) ph = 64;
    if (ph < 2) ph = 2;

    uint8_t* render_buffer = options ? options->render_buffer : NULL;
    uint32_t render_buffer_size = options ? options->render_buffer_size : 0u;
    gui_begin_canvas(&gfx,canvas_width,canvas_height,ph,buffer_color,
                     render_buffer,render_buffer_size);

    if (!gfx.buffer || gfx.page_height <= 0) return false;
    if (!inktag_gui_assets_begin()) {
        GFX_end(&gfx);
        return false;
    }

    if (options) GFX_setColorPlane(&gfx, options->color_plane);
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    GFX_setRotation(&gfx, rotation);
    GFX_setRawViewport(&gfx, visible_x, visible_y, visible_width, visible_height);
#else
    if (rotation != GFX_ROTATE_0) {
        GFX_setRotation(&gfx, rotation);
        GFX_setWindow(&gfx, 0, 0, draw_data.width, draw_data.height);
    }
#endif
    GFX_setMirrorX(&gfx, draw_data.mirror_x);
    GFX_setMirrorY(&gfx, draw_data.mirror_y);

    /* Calendar metadata is invariant across framebuffer pages. Small-RAM
     * targets may redraw the layout dozens of times, so calculating lunar
     * data inside the page loop turns one conversion into a large delay. */
    LUNAR_SolarToLunar(&Lunar, tm.tm_year + YEAR0, tm.tm_mon + 1, tm.tm_mday);

    bool assets_ok = inktag_gui_assets_healthy();
    if (!assets_ok) goto finish;

    GFX_firstPage(&gfx);
    do {
        GFX_fillScreen(&gfx, GFX_WHITE);

        switch (draw_data.mode) {
            case MODE_CALENDAR:
            case MODE_CLOCK:
            case MODE_DEVICE_INFO: {
                /* One runtime entry keeps LTO from cloning the shared page
                 * prelude into three constant-page call sites. All page and
                 * layout choices, entitlement and buffers remain unchanged. */
                uint8_t page = draw_data.mode == MODE_CALENDAR ? 0u :
                    draw_data.mode == MODE_CLOCK ? 1u : 2u;
                DrawFreeLayoutV1Page(&gfx, &draw_data, page, &tm, &Lunar,
                    draw_data.mode == MODE_DEVICE_INFO ? NULL : draw_data.countdown);
                break;
            }
            default:
                break;
        }
        assets_ok = inktag_gui_assets_healthy();
        if (!assets_ok) break;
    } while (GFX_nextPage(&gfx, callback, callback_data));

finish:
    GFX_end(&gfx);
    inktag_gui_assets_end();
    return assets_ok;
}

bool DrawGUI(gui_data_t* data, buffer_callback callback, void* callback_data) {
    return DrawGUIEx(data, callback, callback_data, NULL);
}
