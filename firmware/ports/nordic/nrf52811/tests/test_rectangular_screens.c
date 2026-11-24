/* Square geometry keeps ordinary admission and uses the rectangular template. */
#include <assert.h>
#include <string.h>
#include "inktag_epd_screen.h"
#include "inktag_gui.c"
static uint8_t page_buffer[8000],full_buffer[32768],images[2][32768];
typedef struct {unsigned frames,rows,height;uint8_t *pixels;} capture_t;
static void capture(void *ctx,uint8_t *black,uint8_t *red,uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    capture_t *c=ctx;unsigned row=(w+7u)/8u;
    assert(!x&&y==c->rows);c->rows+=h;++c->frames;
    assert(row*c->height*2u<=32768u);
    if(black==red)memcpy(c->pixels+row*y*2u,black,row*h*2u);
    else {memcpy(c->pixels+row*y,black,row*h);if(red)memcpy(c->pixels+row*c->height+row*y,red,row*h);}
}
int main(void) {
    const unsigned sides[]={152,200,240,320};
    for(unsigned n=0;n<4;++n)for(unsigned padded=0;padded<2;++padded) {
        unsigned side=sides[n],width=side+(padded?8:0);
        epd_screen_config_t c={.schema_version=1,.driver_id=n<3?EPD_SCREEN_DRIVER_SSD1619:EPD_SCREEN_DRIVER_UC8176,
            .color_id=EPD_SCREEN_COLOR_BWR,.width=width,.height=side,.visible_width=side,.visible_height=side};
        epd_screen_t out;assert(epd_screen_resolve(&c,&out)==EPD_SCREEN_STATUS_OK);
        assert(out.config.width==width&&out.config.height==side);
        uint8_t wire[EPD_SCREEN_WIRE_SIZE];
        assert(epd_screen_config_encode(&c,wire)==EPD_SCREEN_STATUS_OK);
        epd_screen_config_t decoded={0};
        assert(epd_screen_config_decode_exact(wire,sizeof(wire),&decoded)==EPD_SCREEN_STATUS_OK);
        assert(decoded.width==width&&decoded.height==side&&decoded.visible_width==side);
        for(unsigned mode=1;mode<=3;++mode)for(unsigned rotation=0;rotation<4;++rotation) {
            gui_data_t data={.width=width,.height=side,.visible_width=side,.visible_height=side,
                .force_rotation=true,.rotation=rotation,.mode=mode,.color=2,
                .timestamp=1790346480u,.temperature=24,.voltage=3010};
            gui_data_t logical=data;logical.width=side;
            assert(v1_layout_preset(&logical).family==(side<300?V1_LAYOUT_COMPACT_RECT:V1_LAYOUT_NORMAL));
            gui_options_t options={.render_buffer=page_buffer,.render_buffer_size=sizeof(page_buffer),
                .prefer_full_frame=true,.force_buffer_color=true,.buffer_color=3};
            capture_t a={.height=side,.pixels=images[0]},b={.height=side,.pixels=images[1]};
            memset(images,0,sizeof(images));
            assert(DrawGUIEx(&data,capture,&a,&options));assert(a.frames&&a.rows==side);
            options.render_buffer=full_buffer;options.render_buffer_size=sizeof(full_buffer);
            assert(DrawGUIEx(&data,capture,&b,&options));assert(b.frames==1&&b.rows==side);
            assert(!memcmp(images[0],images[1],sizeof(images[0])));
        }
    }
    epd_screen_config_t bad={.schema_version=1,.driver_id=0xff,.color_id=2,
        .width=200,.height=200,.visible_width=200,.visible_height=200};
    epd_screen_t out;assert(epd_screen_resolve(&bad,&out)==EPD_SCREEN_STATUS_UNSUPPORTED_DRIVER);
    bad.driver_id=EPD_SCREEN_DRIVER_SSD1619;bad.width=199;
    assert(epd_screen_resolve(&bad,&out)==EPD_SCREEN_STATUS_INVALID_GEOMETRY);
    bad.width=200;bad.color_id=0xff;
    assert(epd_screen_resolve(&bad,&out)==EPD_SCREEN_STATUS_UNSUPPORTED_COLOR);
    return 0;
}