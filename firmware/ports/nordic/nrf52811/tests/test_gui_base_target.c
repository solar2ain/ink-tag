/* Compile the shipping GUI twice to compare generic InkTag pixels with the
 * nRF52811 base-only build and reject stale Pro/hide flags on that target. */
#define main reference_device_info_main
#include "test_gui_device_info.c"
#undef main

int main(void) {
    static const unsigned sizes[][2]={{128,250},{400,300},{768,552}};
    gui_options_t options={.color_plane=GFX_COLOR_PLANE_ALL,
        .force_buffer_color=true,.buffer_color=3,.prefer_full_frame=true,
        .render_buffer=render_buffer,.render_buffer_size=sizeof(render_buffer)};
    for(unsigned i=0;i<3;i++) for(unsigned mode=1;mode<=3;mode++) {
        unsigned width=sizes[i][0],height=sizes[i][1];
        gui_data_t data={.mode=mode,.color=2,.width=width,.height=height,
            .visible_width=i?width:122,.visible_height=height,.force_rotation=true,
            .rotation=i?GFX_ROTATE_0:GFX_ROTATE_270,.timestamp=1790346480,
            .week_start=1,.temperature=24,.voltage=3010,.native_ui_scale=1,
            .bluetooth_name="IKT_NRF_37DE",.mcu_name="nRF52811",.screen_info="SSD1619"};
        assert(GUI_SetDeviceName(&data,NULL,0,0x37de));
        rows=0;assert(DrawGUIEx(&data,capture,&width,&options));assert(rows==height);
        unsigned bytes=width*height/4;memcpy(full_frame,pixels,bytes);
        uint32_t hash=2166136261u;
        for(unsigned n=0;n<bytes;n++)hash=(hash^pixels[n])*16777619u;
        printf("%u %u %u %08x\n",width,height,mode,hash);
#if defined(NRF52811_XXAA)
        for(unsigned hidden=0;hidden<2;hidden++) {
            data.activated=true;data.brand_hidden=hidden;rows=0;
            assert(DrawGUIEx(&data,capture,&width,&options));
            assert(rows==height&&!memcmp(full_frame,pixels,bytes));
        }
#endif
    }
    return 0;
}
