/* Test private layout helpers without adding a firmware API or wire field. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/common/gui/inktag_gui.c"

static uint8_t pixels[800u * 568u / 4u];
static uint8_t reference[sizeof(pixels)];
static uint8_t full_frame[sizeof(pixels)];
static uint8_t render_buffer[sizeof(pixels)];
static unsigned rows;
static void capture(void* context, uint8_t* black, uint8_t* color,
                    uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    unsigned width = *(unsigned*)context;
    if (!(x == 0 && y == rows && w == width && color == black))
        fprintf(stderr,"capture x=%u y=%u rows=%u w=%u expected=%u packed=%u\n",x,y,rows,w,width,color==black);
    assert(x == 0 && y == rows && w == width && color == black);
    memcpy(pixels + y * (w / 4u), black, h * (w / 4u));
    rows += h;
}
static unsigned pixel(unsigned width, unsigned x, unsigned y) {
    return (pixels[y * (width / 4u) + x / 4u] >> (6u - (x % 4u) * 2u)) & 3u;
}
static void check_names(void) {
    Adafruit_GFX gfx = {0};
    char label[24], title[24];
    const char* platforms[] = {"GR", "OM", "TL", "NRF", "DA"};
    for (unsigned i = 0; i < 5; ++i) {
        char name[20];
        snprintf(name, sizeof(name), "IKT_%s_6775", platforms[i]);
        assert(strcmp(v1_ble_label(&gfx, name, 200, 256, label, sizeof(label), false, false), name) == 0);
        /* V1 draws per-glyph scaled advances, not GFX's final-ink bound. */
        int full_width = v1_text_width(&gfx, label);
        assert(strcmp(v1_ble_label(&gfx, name, full_width, 256, label, sizeof(label), false, false), name) == 0);
        v1_ble_label(&gfx, name, full_width-1, 256, label, sizeof(label), false, false);
        assert(strncmp(label, "IKT_", 4) == 0 && v1_text_width(&gfx, label) < full_width);
        assert(strcmp(v1_ble_label(&gfx, name, 200, 256, label, sizeof(label), false, true), "6775") == 0);
        assert(strcmp(v1_ble_label(&gfx, name, 200, 256, label, sizeof(label), true, true), "[6775]") == 0);
        v1_device_display_name(title, sizeof(title), name, false);
        assert(strcmp(title, "InkTag 6775") == 0);
        v1_device_display_name(title, sizeof(title), name, true);
        assert(strcmp(title, "6775") == 0);
        snprintf(name, sizeof(name), "InkTag_%s_6775", platforms[i]);
        assert(strcmp(v1_ble_label(&gfx, name, 200, 256, label, sizeof(label), false, false), name) == 0);
        assert(strcmp(v1_ble_label(&gfx, name, 200, 256, label, sizeof(label), false, true), "6775") == 0);
        for (unsigned scale=256; scale<=448; scale+=64) {
            char wrapped[24]; snprintf(wrapped,sizeof(wrapped),"[%s]",name);
            assert(strcmp(v1_ble_label(&gfx,name,300,scale,label,sizeof(label),true,false),wrapped)==0);
            assert(v1_text_width(&gfx,label)<=300);
            v1_ble_label(&gfx,name,15,scale,label,sizeof(label),true,true);
            assert(v1_text_width(&gfx,label)<=15);
        }
    }
    assert(strcmp(v1_ble_label(&gfx, "IKT_NRF_6775", 30, 256, label, sizeof(label), false, true), "6775") == 0);
    assert(strcmp(v1_ble_label(&gfx, "IKT_NRF_6775", 200, 256, label, sizeof(label), true, false), "[IKT_NRF_6775]")==0);
    assert(strcmp(v1_ble_label(&gfx, "", 200, 256, label, sizeof(label), false, true), "InkTag")==0);
    const char* custom[] = {"IKT_Home_6775", "IKT_NRF_desk", "InkTag_Home_A1", "IKT_NRF_6775_OTA", "IKT_NRF_677", "IKT_NRF_677Z", "ikt_NRF_6775", "InkTag kitchen"};
    for (unsigned i = 0; i < sizeof(custom) / sizeof(custom[0]); ++i) {
        assert(v1_factory_ble_name(custom[i]) == NULL);
        assert(strcmp(v1_ble_label(&gfx, custom[i], 200, 256, label, sizeof(label), false, true), custom[i]) == 0);
        v1_device_display_name(title, sizeof(title), custom[i], true);
        assert(strcmp(title, custom[i]) == 0);
    }
    const char* cjk="IKT_日月";
    v1_ble_label(&gfx,"IKT_日",200,384,label,sizeof(label),false,false);
    int cjk_width=v1_text_width(&gfx,label);
    assert(strcmp(v1_ble_label(&gfx,cjk,cjk_width,384,label,sizeof(label),false,false),"IKT_日")==0);
    assert(strcmp(v1_ble_label(&gfx,cjk,cjk_width-1,384,label,sizeof(label),false,false),"IKT_")==0);
    v1_ble_label(&gfx,"[IKT_日]",200,384,label,sizeof(label),false,false);
    cjk_width=v1_text_width(&gfx,label);
    assert(strcmp(v1_ble_label(&gfx,cjk,cjk_width,384,label,sizeof(label),true,false),"[IKT_日]")==0);
    assert(strcmp(v1_ble_label(&gfx,cjk,cjk_width-1,384,label,sizeof(label),true,false),"[IKT_]")==0);
}
static void check_platform_qr_region(void) {
    /* Same logical canvas and matrix must paint the same QR pixels regardless
     * of vendor marker, panel colour or render-page budget. Use every supported
     * QR version, not only the factory and managed-URL examples. */
    unsigned width = 296;
    uint8_t qr[407]; memset(qr, 0xff, sizeof(qr));
    const char* platforms[] = {"GR", "OM", "TL", "NRF", "DA"};
    gui_data_t data = {.mode=MODE_DEVICE_INFO, .color=2, .width=296, .height=128,
        .visible_width=296, .visible_height=128, .force_rotation=true,
        .rotation=GFX_ROTATE_0, .timestamp=1790346480, .week_start=1,
        .temperature=24, .voltage=3010, .native_ui_scale=1,
        .board_name="same-board", .screen_info="296x128 / BWR"};
    data.device_info_qr_data[0]=qr;
    unsigned header=v1_power_height(&data), bottom=data.height-18;
    for (unsigned modules=21; modules<=57; modules+=4) {
        data.device_info_qr_modules[0]=modules;
        for (unsigned vendor=0; vendor<5; ++vendor) {
            snprintf(data.ssid,sizeof(data.ssid),"IKT_%s_6775",platforms[vendor]);
            data.color=vendor%2 ? 1 : 2;
            gui_options_t options={.color_plane=GFX_COLOR_PLANE_ALL,
                .force_buffer_color=true,.buffer_color=3,
                .prefer_full_frame=false,.render_buffer=render_buffer,
                .render_buffer_size=2048};
            rows=0; assert(DrawGUIEx(&data,capture,&width,&options));
            assert(rows==data.height);
            if (!vendor) memcpy(reference,pixels,sizeof(pixels));
            for (unsigned y=header; y<bottom; ++y)
                for (unsigned x=0; x<=76; ++x)
                    assert(pixel(width,x,y)==((reference[y*(width/4)+x/4]>>(6-(x%4)*2))&3));
        }
    }
}
static void check_geometry_boundaries(void) {
    /* Retained rectangular detail starts at logical short edge152. */
    static const struct {
        uint16_t width, height;
        v1_layout_family_t family;
        bool lunar;
    } cases[] = {
        {296, 128, V1_LAYOUT_COMPACT_RECT, false},
        {296, 149, V1_LAYOUT_COMPACT_RECT, false},
        {296, 150, V1_LAYOUT_COMPACT_RECT, false},
        {296, 151, V1_LAYOUT_COMPACT_RECT, false},
        {296, 152, V1_LAYOUT_COMPACT_RECT, true},
        {400, 128, V1_LAYOUT_COMPACT_RECT, false},
        {600, 128, V1_LAYOUT_COMPACT_RECT, false},
        {128, 600, V1_LAYOUT_COMPACT_RECT, false},
        {400, 151, V1_LAYOUT_COMPACT_RECT, false},
        {400, 152, V1_LAYOUT_COMPACT_RECT, true},
        {400, 300, V1_LAYOUT_NORMAL, true},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        gui_data_t data = {.width=cases[i].width, .height=cases[i].height};
        v1_layout_preset_t preset = v1_layout_preset(&data);
        if (preset.family != cases[i].family || preset.calendar_lunar_cells != cases[i].lunar)
            fprintf(stderr, "layout %ux%u family=%u lunar=%u\n", data.width, data.height,
                    preset.family, preset.calendar_lunar_cells);
        assert(preset.family == cases[i].family);
        assert(preset.calendar_lunar_cells == cases[i].lunar);
        assert(v1_compact(&data) == (preset.family <= V1_LAYOUT_COMPACT_RECT));
    }
}

static void render_geometry_case(gui_data_t* data) {
    unsigned width = data->width;
    size_t bytes = (size_t)width * data->height / 4u;
    gui_options_t options={.color_plane=GFX_COLOR_PLANE_ALL,.force_buffer_color=true,.buffer_color=3,
        .prefer_full_frame=true,.render_buffer=render_buffer,.render_buffer_size=sizeof(render_buffer)};
    rows=0;
    assert(DrawGUIEx(data,capture,&width,&options));
    assert(rows == data->height);
    memcpy(full_frame,pixels,bytes);
    rows=0; options.prefer_full_frame=false; options.render_buffer_size=2048;
    assert(DrawGUIEx(data,capture,&width,&options));
    assert(rows == data->height);
    assert(memcmp(full_frame,pixels,bytes) == 0);
}

static void check_name_separation(void) {
    const char* platforms[]={"GR","OM","TL","NRF","DA"};
    const unsigned sizes[][2]={{212,104},{250,128},{296,128},{296,152},{400,300}};
    bool first=true;
    for (unsigned vendor=0; vendor<5; ++vendor) {
        char gap[20]; snprintf(gap,sizeof(gap),"IKT_%s_6775",platforms[vendor]);
        for (unsigned n=0; n<sizeof(sizes)/sizeof(sizes[0]); ++n) {
            unsigned width=(sizes[n][0]+7u)&~7u;
            char screen[24]; snprintf(screen,sizeof(screen),"%ux%u / BWR",sizes[n][0],sizes[n][1]);
            gui_data_t data={.mode=MODE_DEVICE_INFO,.color=2,.width=width,.height=sizes[n][1],
                .visible_width=sizes[n][0],.visible_height=sizes[n][1],.force_rotation=true,.rotation=GFX_ROTATE_0,
                .timestamp=1790346480,.week_start=1,.temperature=24,.voltage=3010,.native_ui_scale=1,
                .board_name="nrf52832_priceetag",.board_name_short="priceetag",.screen_info=screen,
                .bluetooth_name=gap};
            strcpy(data.ssid,"Kitchen");
            assert(strcmp(v1_advertised_name(&data),gap)==0);
            render_geometry_case(&data);
            size_t bytes=(size_t)width*data.height/4u;
            memcpy(reference,pixels,bytes);
            uint32_t hash=2166136261u;
            for (unsigned y=0;y<data.height;++y) for(unsigned x=0;x<sizes[n][0];++x) {
                unsigned p=pixel(width,x,y); hash=(hash^(p==1 ? 0 : p==0 ? 1 : 2))*16777619u;
            }
            strcpy(data.ssid,"Office");
            render_geometry_case(&data);
            gui_data_t logical=data; logical.width=sizes[n][0];
            v1_layout_preset_t preset=v1_layout_preset(&logical);
            /* New device rows depend on rotated logical height, not square
             * shape or the calendar's lunar-detail selector. */
            bool short_rows=logical.height<150u;
            if (short_rows) assert(memcmp(reference,pixels,bytes)==0);
            else {
                assert(memcmp(reference,pixels,bytes)!=0); /* NAME changed. */
                unsigned header=v1_power_height(&logical);
                unsigned footer=preset.family<=V1_LAYOUT_COMPACT_RECT ? 18 : 21;
                unsigned row_height=(data.height-header-footer)/6u;
                unsigned ble_y=header+row_height; /* NAME, BLE, MCU, EPD, FW, TIER */
                /* BLE did not change when the independent Name changed. */
                assert(memcmp(reference+ble_y*(width/4u),pixels+ble_y*(width/4u),row_height*(width/4u))==0);
            }
            printf("%s{\"width\":%u,\"height\":%u,\"bluetoothName\":\"%s\",\"deviceName\":\"Kitchen\",\"fnv\":%u}",
                first?"":",\n",sizes[n][0],sizes[n][1],gap,hash);
            first=false;
            if (!short_rows) {
                /* A valid independent English label may resemble a GAP name.
                 * It must remain literal NAME, not become "InkTag ABCD". */
                strcpy(data.ssid,"IKT_GR_ABCD");
                render_geometry_case(&data);
                hash=2166136261u;
                for (unsigned y=0;y<data.height;++y) for(unsigned x=0;x<sizes[n][0];++x) {
                    unsigned p=pixel(width,x,y); hash=(hash^(p==1 ? 0 : p==0 ? 1 : 2))*16777619u;
                }
                printf(",\n{\"width\":%u,\"height\":%u,\"bluetoothName\":\"%s\",\"deviceName\":\"IKT_GR_ABCD\",\"fnv\":%u}",
                    sizes[n][0],sizes[n][1],gap,hash);
            }
            /* Legacy producers still carry their actual GAP name in ssid. */
            data.bluetooth_name=NULL; strcpy(data.ssid,gap);
            assert(strcmp(v1_advertised_name(&data),gap)==0);
        }
    }
}

static void check_compact_rotation_and_viewport(void) {
    static const unsigned sizes[][2]={{296,128},{296,151},{296,152},{400,128},{600,128},{400,151},{400,152}};
    static const display_mode_t modes[]={MODE_DEVICE_INFO,MODE_CALENDAR,MODE_CLOCK};
    gui_data_t data={.color=2,.force_rotation=true,.rotation=GFX_ROTATE_0,
        .timestamp=1790346480,.week_start=1,.temperature=24,.voltage=3010,
        .native_ui_scale=1,.board_name="da14585_asel29",.board_name_short="asel29"};
    strcpy(data.ssid,"IKT_DA_65CA");
    for (unsigned m=0; m<sizeof(modes)/sizeof(modes[0]); ++m) {
        data.mode=modes[m];
        for (unsigned n=0; n<sizeof(sizes)/sizeof(sizes[0]); ++n) {
            data.width=sizes[n][0]; data.height=sizes[n][1];
            render_geometry_case(&data);
        }
        data.width=296; data.height=128;
        render_geometry_case(&data);
        memcpy(reference,pixels,296u*128u/4u);
        data.width=128; data.height=296; data.rotation=GFX_ROTATE_90;
        render_geometry_case(&data);
        for (unsigned y=0; y<128; ++y) for (unsigned x=0; x<296; ++x)
            assert(pixel(128,127-y,x) == ((reference[y*74u+x/4u]>>(6u-(x%4u)*2u))&3u));
#if defined(INKTAG_GUI_RAW_VIEWPORT)
        data.height=304; data.visible_width=128; data.visible_height=296; data.visible_y=4;
        render_geometry_case(&data);
        for (unsigned y=0; y<304; ++y) for (unsigned x=0; x<128; ++x) {
            unsigned expected = y < 4 || y >= 300 ? 1u
                : ((reference[(127u-x)*74u+(y-4u)/4u]>>(6u-((y-4u)%4u)*2u))&3u);
            assert(pixel(128,x,y) == expected);
        }
        data.visible_width=0; data.visible_height=0; data.visible_y=0;
#endif
        data.rotation=GFX_ROTATE_0;
    }
}

static void check_independent_device_metadata(void) {
    gui_data_t data={.mode=MODE_DEVICE_INFO,.color=2,.width=400,.height=300,
        .force_rotation=true,.rotation=GFX_ROTATE_0,.native_ui_scale=1,
        .temperature=24,.voltage=3010,.timestamp=1790346480,
        .bluetooth_name="IKT_NRF_37DE",.mcu_name="nRF52811",
        .board_name="nrf52811_asel22",.screen_info="400x300 / BWR"};
    const uint8_t label[]="123456789012345678901234";
    assert(GUI_SetDeviceName(&data,label,24u,0x37deu));
    assert(strcmp(data.ssid,(const char*)label)==0);
    assert(strcmp(v1_advertised_name(&data),"IKT_NRF_37DE")==0);
    render_geometry_case(&data);
    const size_t bytes=400u*300u/4u;
    memcpy(reference,pixels,bytes);
    data.board_name="unrelated PCB"; data.board_name_short="other";
    render_geometry_case(&data);
    assert(memcmp(reference,pixels,bytes)==0); // PCB never substitutes for silicon
    data.mcu_name="nRF52832";
    render_geometry_case(&data);
    unsigned header=v1_power_height(&data), row=(300u-header-21u)/6u;
    unsigned changes=0;
    for(unsigned y=0;y<300u;++y) for(unsigned x=0;x<400u;++x) {
        unsigned before=(reference[y*100u+x/4u]>>(6u-(x%4u)*2u))&3u;
        if(pixel(400,x,y)!=before) {
            assert(y>=header+2u*row && y<header+3u*row);
            ++changes;
        }
    }
    assert(changes>0); // actual MCU glyphs, rather than only a renamed key
    data.mcu_name=NULL; render_geometry_case(&data); memcpy(reference,pixels,bytes);
    data.mcu_name="--"; render_geometry_case(&data);
    assert(memcmp(reference,pixels,bytes)==0);
    assert(GUI_SetDeviceName(&data,NULL,0u,0x37deu));
    assert(strcmp(data.ssid,"InkTag 37DE")==0);
    assert(strcmp(v1_advertised_name(&data),"IKT_NRF_37DE")==0);
    assert(!GUI_SetDeviceName(&data,label,25u,0x37deu));
}

static void check_unset_display_reference(void) {
    const display_mode_t modes[]={MODE_CALENDAR,MODE_CLOCK,MODE_DEVICE_INFO};
    const gui_countdown_t target={.name="TEST",.target_day=GUI_UNSET_DISPLAY_TIMESTAMP/SEC_PER_DY+7u};
    gui_data_t data={.color=2,.width=400,.height=300,.force_rotation=true,
        .rotation=GFX_ROTATE_0,.native_ui_scale=1,.countdown=&target,
        .temperature=24,.voltage=3010,.mcu_name="nRF52811"};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i) {
        data.mode=modes[i];data.timestamp=0;
        render_geometry_case(&data);
        assert(data.timestamp==0); // display fallback never writes the caller's clock
        memcpy(reference,pixels,400u*300u/4u);
        data.timestamp=GUI_UNSET_DISPLAY_TIMESTAMP;
        render_geometry_case(&data);
        assert(memcmp(reference,pixels,400u*300u/4u)==0);
    }
}

int main(void) {
    check_independent_device_metadata();
    check_unset_display_reference();

    check_geometry_boundaries();
    check_compact_rotation_and_viewport();
    check_names();
    check_platform_qr_region();
    const unsigned sizes[][2] = {{212,104},{250,128},{296,152},{400,300},{600,448},{752,568},{768,552},{800,480}};
    bool first = true;
    puts("{\"cases\":[");
    for (unsigned n = 0; n < sizeof(sizes)/sizeof(sizes[0]); ++n) {
        unsigned width = (sizes[n][0] + 7u) & ~7u;
        gui_data_t data = {.mode=MODE_DEVICE_INFO, .color=2, .width=width,
            .height=sizes[n][1], .visible_width=sizes[n][0], .visible_height=sizes[n][1],
            .force_rotation=true,.rotation=GFX_ROTATE_0,
            .timestamp=1790346480, .week_start=1, .temperature=24, .voltage=3010,
            .native_ui_scale=1, .board_name="nrf52832_priceetag", .board_name_short="priceetag"};
        char screen[24];
        snprintf(screen, sizeof(screen), "%ux%u / BWR", sizes[n][0], sizes[n][1]);
        data.screen_info=screen;
        strcpy(data.ssid, "IKT_NRF_6775");
        gui_data_t logical=data; logical.width=sizes[n][0];
        unsigned header=v1_power_height(&logical);
        bool compact=v1_layout_preset(&logical).family <= V1_LAYOUT_COMPACT_RECT;
        unsigned footer=compact ? 18 : logical.width>500 ? 31 : 21;
        unsigned height=data.height-header-footer;
        unsigned limit=v1_min(v1_min(height, logical.width/2), compact ? 132 : 300);
        unsigned side=33u*v1_max(1,limit/33u);
        unsigned divider=compact ? v1_min(logical.width/2,side*5/4)
            : v1_max(side,v1_min(logical.width*47/100,side*6/5));
        const unsigned modules[]={21,25,29,33,37,41,45,49,53,57};
        for (unsigned m=0; m<sizeof(modules)/sizeof(modules[0]); ++m) {
            uint8_t qr[407]; memset(qr,0xff,sizeof(qr));
            data.device_info_qr_data[0]=qr; data.device_info_qr_modules[0]=modules[m];
            rows=0;
            gui_options_t options={.color_plane=GFX_COLOR_PLANE_ALL,.force_buffer_color=true,.buffer_color=3,
                .prefer_full_frame=true,.render_buffer=render_buffer,.render_buffer_size=sizeof(render_buffer)};
            assert(DrawGUIEx(&data,capture,&width,&options)); assert(rows==data.height);
            for (unsigned y=header; y<data.height-footer; ++y) {
                assert(pixel(width,divider,y)==0);
                if (m) for (unsigned x=divider; x<logical.width; ++x)
                    assert(pixel(width,x,y)==((reference[y*(width/4)+x/4]>>(6-(x%4)*2))&3));
            }
            if (!m) memcpy(reference,pixels,sizeof(pixels));
            unsigned scale=v1_max(1,v1_min(limit,divider)/(modules[m]+8));
            /* Retained rectangular layouts use integer module scale. */
            unsigned qr_side=(modules[m]+8)*scale;
            assert(qr_side<=divider);
            unsigned qx=(divider-qr_side)/2, qy=header+(height-qr_side)/2;
            unsigned inner0=4u*qr_side/(modules[m]+8u);
            unsigned inner1=(modules[m]+4u)*qr_side/(modules[m]+8u);
            for (unsigned y=0; y<qr_side; ++y) for (unsigned x=0; x<qr_side; ++x)
                assert(pixel(width,qx+x,qy+y)==((x<inner0 || y<inner0 || x>=inner1 || y>=inner1) ? 1u : 0u));
            uint32_t hash=2166136261u;
            for(unsigned y=0;y<data.height;++y) for(unsigned x=0;x<logical.width;++x) {
                unsigned p=pixel(width,x,y); unsigned normalized=p==1 ? 0 : p==0 ? 1 : 2;
                hash=(hash^normalized)*16777619u;
            }
            printf("%s{\"width\":%u,\"height\":%u,\"modules\":%u,\"divider\":%u,\"fnv\":%u}",first?"":",\n",logical.width,data.height,modules[m],divider,hash);
            first=false;
            memcpy(full_frame,pixels,width*data.height/4u);
            rows=0; options.prefer_full_frame=false; options.render_buffer_size=2048;
            assert(DrawGUIEx(&data,capture,&width,&options)); assert(rows==data.height);
            assert(memcmp(full_frame,pixels,width*data.height/4u)==0);
        }
    }
    puts("\n],\"metadataCases\":[");
    check_name_separation();
    puts("\n]}");
    return 0;
}
