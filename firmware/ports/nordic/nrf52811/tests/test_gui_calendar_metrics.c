/* Default InkTag layout font/size contract, shared native geometry. */

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "inktag_gui.c"
static unsigned digits,units,celsius,volts;
static unsigned expected_repeat;
static const uint8_t *expected_temperature_font,*expected_date_font;
int16_t __real_u8g2_DrawGlyph(u8g2_font_t *,int16_t,int16_t,uint16_t);
int16_t __wrap_u8g2_DrawGlyph(u8g2_font_t *font,int16_t x,int16_t y,uint16_t cp) {
    Adafruit_GFX *gfx=(Adafruit_GFX*)((char*)font - offsetof(Adafruit_GFX,u8g2));
    if(expected_repeat) {
        if(cp>='0'&&cp<='9') { digits++;assert(font->font_decode.fg_color==V1_RED);assert(font->font==expected_date_font); }
        if(cp==0x5E74u||cp==0x6708u||cp==0x65E5u) { units++;assert(font->font_decode.fg_color==V1_BLACK); }
    }
    if(cp==0x2103u) {
        celsius++;assert(font->font==expected_temperature_font);
        assert(gfx->font_scale_q8==256u);
    }
    if(cp=='V') { volts++;assert(font->font==u8g2_font_wqy9_t_lunar);assert(gfx->font_scale_q8==256u); }
    return __real_u8g2_DrawGlyph(font,x,y,cp);
}

static void check_free_fixed_fields(void) {
    for(unsigned size=1;size<=64;size++)for(unsigned count=2;count<=4;count+=2) {
        int padding=v1_max(1,size/2);
        inktag_gui_digit_metrics_t m={11*size+2,9*size+2,4*size+2*padding,padding,20*size+2,0};
        const inktag_gui_rect_t areas[]={{0,0,32767,32767},{3,18,500,300},{0,0,40,40},{0,0,0,0},{-1,0,500,500}};
        for(unsigned a=0;a<sizeof areas/sizeof areas[0];a++) {
            inktag_gui_fixed_field_t x={0},y={0};
            bool ok=inktag_gui_fixed_center(&m,count,&areas[a],&x);
            assert(ok==v1_seven_center(size,20*size+2,count,&areas[a],&y));
            if(ok) {
                assert(x.rectangle.x==y.rectangle.x && x.rectangle.y==y.rectangle.y);
                assert(x.rectangle.width==y.rectangle.width && x.rectangle.height==y.rectangle.height);
                assert(x.digit_count==y.digit_count && x.colon_x==y.colon_x);
                for(unsigned i=0;i<4;i++)assert(x.digit_x[i]==y.digit_x[i]);
            }
        }
    }
}

int main(void) {
    check_free_fixed_fields();
    static uint8_t buffer[18000];
    const unsigned sizes[][2]={{212,104},{250,128},{296,152},{400,300},{600,448},{300,400},{768,552},{552,768}};
    tm_t time={0};transformTime(1790346480u,&time);
    for(unsigned i=0;i<8;i++) {
        gui_data_t data={.width=sizes[i][0],.height=sizes[i][1],.mode=MODE_CALENDAR,
            .color=2,.temperature=24,.voltage=3300,.native_ui_scale=1};
        Adafruit_GFX gfx;GFX_begin_3c_with_buffer(&gfx,data.width,data.height,128,buffer,sizeof buffer);
        assert(gfx.buffer&&gfx.color);
        assert(v1_show_voltage(&data)==(i>=1));
        if(i<3) {
            digits=units=0;expected_repeat=1u;
            expected_date_font=u8g2_font_wqy9_t_lunar;
            v1_compact_header(&gfx,&data,&time);
            char text[24];GFX_format(text,sizeof text,"%u%u%u",time.tm_year+YEAR0,time.tm_mon+1,time.tm_mday);
            assert(digits==strlen(text)*expected_repeat&&units==3u);
            expected_repeat=0;
        }
        for(unsigned mode=0;mode<2;mode++) {
            data.mode=mode==0?MODE_CALENDAR:MODE_CLOCK;
            celsius=volts=0;
            expected_temperature_font=u8g2_font_wqy9_t_lunar;
            v1_power_status(&gfx,&data);
            assert(celsius==1u&&volts==(i>=1?1u:0u));
        }
        assert(v1_span_width(&gfx,"  ")==8);
        if(i<3) {
            data.mode=MODE_CLOCK;
            inktag_gui_fixed_field_t field;
            int size=v1_clock_field(&data,&field);
            assert(size==(i==0?2:i==1?3:4));
            assert(field.rectangle.height==20u*(unsigned)size+2u);
            if(i==2) {
                assert(field.rectangle.x>=20);
                assert(field.rectangle.x+field.rectangle.width<=data.width-20);
            }
        }
        GFX_end(&gfx);
    }
    return 0;
}
