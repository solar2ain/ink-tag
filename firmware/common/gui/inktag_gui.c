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

static bool gui_prepare_canvas(const gui_data_t* data,gui_data_t* draw_data,
                                inktag_gui_region_transform_t* view) {
    if(!data || !draw_data || !view || !data->width || !data->height ||
        data->width>INT16_MAX || data->height>INT16_MAX ||
        (data->force_rotation && (unsigned)data->rotation>3u))return false;
    *draw_data=*data;
    if (draw_data->timestamp == 0u)
        draw_data->timestamp = GUI_UNSET_DISPLAY_TIMESTAMP;
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    uint16_t x=data->visible_x,y=data->visible_y;
    if(x>=data->width || y>=data->height)return false;
    uint16_t width=data->visible_width ? data->visible_width:(uint16_t)(data->width-x);
    uint16_t height=data->visible_height ? data->visible_height:(uint16_t)(data->height-y);
    if(width>data->width-x || height>data->height-y)return false;
    *view=(inktag_gui_region_transform_t){data->width,data->height,
        {(int16_t)x,(int16_t)y,width,height},0u,0u};
#else
    uint16_t width=data->visible_width ? data->visible_width:data->width;
    uint16_t height=data->visible_height ? data->visible_height:data->height;
    if(width>data->width || height>data->height)return false;
    *view=(inktag_gui_region_transform_t){width,height,{0,0,width,height},0u,0u};
#endif
    draw_data->width=width;draw_data->height=height;
    view->rotation=data->force_rotation ? (uint8_t)data->rotation:
        (small_layout(draw_data) && width<height ? GFX_ROTATE_90:GFX_ROTATE_0);
    view->mirrors=(data->mirror_x ? 1u:0u)|(data->mirror_y ? 2u:0u);
    if(view->rotation&1u){draw_data->width=height;draw_data->height=width;}
    return true;
}

/* DrawGUIEx already owns the validated logical canvas. Do not copy and
 * normalize the caller's complete GUI data a second time for this query. */
static bool gui_default_fixed_region(const gui_data_t* logical,
                                    const inktag_gui_region_transform_t* transform,
                                    gui_fixed_region_kind_t kind,gui_fixed_region_t* out) {
    memset(out,0,sizeof(*out));
    int size=kind==GUI_FIXED_CLOCK && logical->mode==MODE_CLOCK
        ? v1_clock_field(logical,&out->field):
        kind==GUI_FIXED_CALENDAR_DATE && logical->mode==MODE_CALENDAR
        ? v1_calendar_date_field(logical,&out->field):0;
    if(!size || !inktag_gui_region_map(transform,&out->field.rectangle,&out->physical))return false;
    out->key.layout_revision=1u;
    out->key.font_tier=(uint16_t)size;
    out->key.font_scale_q8=256u;
    out->key.color_mode=logical->color;
    out->key.logical_width=logical->width;out->key.logical_height=logical->height;
    out->key.page=(uint8_t)logical->mode;out->key.format=(uint8_t)kind;
    out->key.native_ui_scale=logical->native_ui_scale;
    out->key.logical_rectangle=out->field.rectangle;
    out->key.metrics=v1_seven_metrics(size);
    out->key.metrics.height=out->field.rectangle.height;
    out->key.transform=*transform;
    return true;
}

bool GUI_GetDefaultFixedRegion(const gui_data_t* data,gui_fixed_region_kind_t kind,
                               gui_fixed_region_t* out) {
    gui_data_t logical;inktag_gui_region_transform_t transform;
    return out && gui_prepare_canvas(data,&logical,&transform) &&
        gui_default_fixed_region(&logical,&transform,kind,out);
}

/* Keep the common GFX initialization single-copy. The remaining plane setup
 * matches the public3c/4c APIs, including allocation and undersized buffers. */
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

#if defined(INKTAG_GUI_ROLE_FONT_ASSETS) && defined(INKTAG_GOODIX)
/* End temporary snapshot/region lifetimes before the paged draw call. */
static bool V1_GR_PAGE_SHARED gui_prepare_role(v1_role_render_t* role,
        const gui_data_t* data,const inktag_gui_region_transform_t* view,
        const gui_options_t* options) {
    inktag_gui_role_snapshot_t snapshot={0};
    gui_fixed_region_t region={0};
    if(!inktag_gui_assets_role_snapshot(&snapshot) ||
       !inktag_gui_role_frame_prepare(&role->source,&snapshot) ||
       !inktag_gui_assets_healthy()) return false;
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
    /* Four-face geometry uses the draw policy; large digits retain their
     * existing font-neutral fitting area. The snapshot identity is intact. */
    if (!role->source.has_large) v1_role_render=role;
#endif
    gui_fixed_region_kind_t kind=data->mode==MODE_CLOCK ? GUI_FIXED_CLOCK:GUI_FIXED_CALENDAR_DATE;
    bool has_region=gui_default_fixed_region(data,view,kind,&region);
    if(has_region) {
        region.key.font_schema=snapshot.schema;region.key.font_generation=snapshot.generation;
        region.key.font_crc32=snapshot.crc32;
        if(role->source.has_large) {
            if(!inktag_gui_large_fit(&role->source,region.field.digit_count,
                &region.field.rectangle,&role->style,&role->field)) return false;
            region.field=role->field;role->use_large=true;
            region.key.metrics=role->style.scaled;region.key.font_tier=role->style.scaled.height;
            region.key.font_scale_q8=256u;
            region.key.logical_rectangle=role->field.rectangle;
            if(!inktag_gui_region_map(view,&role->field.rectangle,&region.physical)) return false;
        }
    }
    if(options && options->fixed_region)*options->fixed_region=region;

    return true;
}
#endif

bool DrawGUIEx(gui_data_t* data, buffer_callback callback, void* callback_data, const gui_options_t* options) {
    gui_data_t draw_data;inktag_gui_region_transform_t view;
    /* Retired page4 never allocates, reads font assets or emits a blank frame. */
    if(!data || data->mode==MODE_LOW_BATTERY ||
       !gui_prepare_canvas(data,&draw_data,&view))return false;
    if (data->week_start > 1)
        data->week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    draw_data.week_start=data->week_start;
    tm_t tm = {0};
    struct Lunar_Date Lunar;
    uint16_t canvas_width=view.canvas_width,canvas_height=view.canvas_height;
    GFX_Rotate rotation=(GFX_Rotate)view.rotation;

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
    /* E6 native layouts use the existing K/W/Y/R accents. Keep those in the
     * packed 2bpp page buffer; the controller expands them to its 4bpp wire.
     * Lenmory's explicit Area0/Area6 options remain authoritative. */
    if (buffer_color == 10u) buffer_color = 3u;

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

    bool assets_ok = inktag_gui_assets_healthy();
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
    v1_role_render_t role={0};
#if defined(INKTAG_GOODIX)
    v1_role_render=NULL;
    if(!gui_prepare_role(&role,&draw_data,&view,options)) {assets_ok=false;goto finish;}
    v1_role_render=&role;
#else
    inktag_gui_role_snapshot_t snapshot={0};
    gui_fixed_region_t region={0};
    v1_role_render=NULL;
    if(!inktag_gui_assets_role_snapshot(&snapshot) ||
       !inktag_gui_role_frame_prepare(&role.source,&snapshot) ||
       !inktag_gui_assets_healthy()) {assets_ok=false;goto finish;}
#if defined(INKTAG_GUI_FACTORY_FONT_LAYOUT)
    /* Match four-face query/draw policy without moving native large digits. */
    if (!role.source.has_large) v1_role_render=&role;
#endif
    gui_fixed_region_kind_t kind=draw_data.mode==MODE_CLOCK ? GUI_FIXED_CLOCK:GUI_FIXED_CALENDAR_DATE;
    bool has_region=gui_default_fixed_region(&draw_data,&view,kind,&region);
    if(has_region) {
        region.key.font_schema=snapshot.schema;region.key.font_generation=snapshot.generation;
        region.key.font_crc32=snapshot.crc32;
        if(role.source.has_large) {
            if(!inktag_gui_large_fit(&role.source,region.field.digit_count,
                &region.field.rectangle,&role.style,&role.field)) {assets_ok=false;goto finish;}
            region.field=role.field;role.use_large=true;
            region.key.metrics=role.style.scaled;region.key.font_tier=role.style.scaled.height;
            region.key.font_scale_q8=256u;
            region.key.logical_rectangle=role.field.rectangle;
            if(!inktag_gui_region_map(&view,&role.field.rectangle,&region.physical)) {assets_ok=false;goto finish;}
        }
    }
    if(options && options->fixed_region)*options->fixed_region=region;
    v1_role_render=&role;
#endif
#endif

    if (options) GFX_setColorPlane(&gfx, options->color_plane);
#if defined(INKTAG_GUI_RAW_VIEWPORT)
    GFX_setRotation(&gfx, rotation);
    GFX_setRawViewport(&gfx, view.viewport.x, view.viewport.y,
                       view.viewport.width, view.viewport.height);
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

    assets_ok = inktag_gui_assets_healthy();
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
                DrawLayoutV1Page(&gfx, &draw_data, page, &tm, &Lunar,
                    draw_data.mode == MODE_DEVICE_INFO ? NULL : draw_data.countdown);
                break;
            }
            default:
                break;
        }
        assets_ok = inktag_gui_assets_healthy();
#if defined(INKTAG_GUI_NATIVE_FONT_ASSETS)
        assets_ok=assets_ok && !role.layout_failed;
#endif
        if (!assets_ok) break;
    } while (GFX_nextPage(&gfx, callback, callback_data));

finish:
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
    v1_role_render=NULL;
#endif
    GFX_end(&gfx);
    inktag_gui_assets_end();
    return assets_ok;
}

bool DrawGUI(gui_data_t* data, buffer_callback callback, void* callback_data) {
    return DrawGUIEx(data, callback, callback_data, NULL);
}
