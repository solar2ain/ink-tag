/* Built-in glyph advances and signed pixel-edge equivalence. */

#include <assert.h>
#include <limits.h>
#include "inktag_gui.c"
int main(void) {
    Adafruit_GFX gfx; uint8_t buffer[100];
    GFX_begin_3c_with_buffer(&gfx,200,200,2,buffer,sizeof buffer);
    v1_set_font(&gfx,u8g2_font_wqy9_t_lunar,256u);
    gui_data_t data={.width=200,.height=200,.mode=MODE_CALENDAR,.color=2};
    char label[16];
    for(int t=-128;t<=127;t++) {
        data.temperature=(int8_t)t;
        GFX_format(label,sizeof label,"%d℃",t);
        if(t!=GUI_TEMPERATURE_UNAVAILABLE)
            assert(v1_power_width(&data)-46==v1_text_width(&gfx,label));
    }
    for(unsigned v=0;v<=UINT16_MAX;v++) {
        unsigned tenths=(v+50u)/100u;
        GFX_format(label,sizeof label,"%u.%uV",tenths/10u,tenths%10u);
        assert(v1_text_width(&gfx,label)==(tenths>=100u?36:29));
    }
    for(int value=-1024;value<=1024;value++)for(unsigned scale=128;scale<=512;scale++) {
        int32_t product=value*(int32_t)scale;
        int16_t ceil_edge=product>=0?(product+255)/256:-((-product)/256);
        int32_t rounded=product+128;
        int16_t round_edge=rounded>=0?rounded/256:-((-rounded+255)/256);
        assert(v1_edge(value,scale)==ceil_edge);
        assert(v1_round_edge(value,scale)==round_edge);
    }
    GFX_end(&gfx);
}
