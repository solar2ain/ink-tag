/* Host/WebAssembly adapter only. Layout and U8g2 come from shipping common. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "inktag_gui.h"
#include "inktag_gui_fonts.h"
#include "inktag_gui_assets.h"
#include "inktag_gui_asset_package.h"

static uint8_t package[32768];
static uint32_t package_size;
static inktag_gui_asset_package_header_t header;
static inktag_gui_asset_entry_t entries[6];
static bool active;
static gui_data_t data;
static char strings[5][128];
static uint8_t *pixels;
static uint32_t pixel_capacity;
static uint16_t rows;
static bool capture_failed;
static int32_t region_values[12];

static bool read_package(void *ctx,uint32_t off,uint8_t *out,uint16_t n) {
    (void)ctx;
    if(off>package_size || n>package_size-off)return false;
    memcpy(out,package+off,n);return true;
}
int nf_package(const uint8_t *bytes,uint32_t size) {
    if(active)return 0;
    package_size=0;
    if(!size)return 1;
    if(!bytes || size>sizeof package)return 0;
    memcpy(package,bytes,size);package_size=size;
    if(!inktag_gui_native_validate(read_package,NULL,&header,entries,6,sizeof package)) {
        package_size=0;return 0;
    }
    return 1;
}
bool inktag_gui_assets_begin(void) {if(active)return false;active=true;return true;}
void inktag_gui_assets_end(void) {active=false;}
bool inktag_gui_assets_healthy(void) {return active;}
bool inktag_gui_asset_font_read_failed(void) {return false;}
uint8_t inktag_gui_asset_read_byte(const uint8_t *p) {return *p;}
const uint8_t *inktag_gui_asset_font(uint8_t id) {
    static const uint8_t * const defaults[]={inktag_gui_fallback_font_wqy9,
        inktag_gui_fallback_font_wqy12,inktag_gui_fallback_font_helvB14,inktag_gui_fallback_font_helvB18};
    if(id>=4)return NULL;
    return package_size ? package+entries[id].offset:defaults[id];
}
bool inktag_gui_assets_role_snapshot(inktag_gui_role_snapshot_t *out) {
    memset(out,0,sizeof *out);out->entry_count=4;
    if(!package_size)return true;
    out->schema=INKTAG_GUI_NATIVE_PACKAGE_SCHEMA;out->entry_count=header.entry_count;
    out->generation=header.generation;out->crc32=header.package_crc32;
    if(header.entry_count==6)for(uint8_t i=0;i<2;i++) {
        out->large[i]=package+entries[i+4].offset;out->length[i]=entries[i+4].length;
    }
    return true;
}
/* ABI v1: 18 signed i32 values; no pointer/layout assumptions in callers. */
int nf_config(const int32_t *v) {
    if(!v || v[0]<1 || v[1]<1 || v[0]>2048 || v[1]>2048 ||
       (uint32_t)v[0]*(uint32_t)v[1]>2097152u || v[2]<1 || v[2]>4 ||
       v[3]<0 || v[3]>3 || v[4]<0 || v[4]>3 || v[5]<0 || v[5]>5 ||
       v[6]<0 || v[7]<0 || v[8]<1 || v[9]<1 || v[6]>=v[0] || v[7]>=v[1] ||
       v[8]>v[0]-v[6] || v[9]>v[1]-v[7] || v[11]<0 || v[11]>1)return 0;
    memset(&data,0,sizeof data);
    data.width=(uint16_t)v[0];data.height=(uint16_t)v[1];
    data.mode=(display_mode_t)v[2];data.force_rotation=true;data.rotation=(GFX_Rotate)v[3];
    data.mirror_x=(v[4]&1)!=0;data.mirror_y=(v[4]&2)!=0;data.native_ui_scale=(uint8_t)v[5];
    data.visible_x=(uint16_t)v[6];data.visible_y=(uint16_t)v[7];
    data.visible_width=(uint16_t)v[8];data.visible_height=(uint16_t)v[9];
    data.timestamp=(uint32_t)v[10];data.week_start=(uint8_t)v[11];data.color=(uint16_t)v[12];
    data.activated=v[13]!=0;data.brand_hidden=v[14]!=0;data.temperature=(int8_t)v[15];
    data.voltage=(uint16_t)v[16];
    memcpy(data.ssid,strings[0],sizeof data.ssid-1);
    data.bluetooth_name=strings[1];data.board_name=strings[2];data.mcu_name=strings[3];
    data.device_name=strings[0];data.screen_info=strings[4];
    return 1;
}
int nf_string(uint32_t id,const char *value,uint32_t length) {
    if(id>=5 || !value || length>=sizeof strings[0])return 0;
    memcpy(strings[id],value,length);strings[id][length]=0;return 1;
}
const int32_t *nf_region(uint32_t kind) {
    gui_fixed_region_t r;
    if(!GUI_GetDefaultFixedRegion(&data,(gui_fixed_region_kind_t)kind,&r))return NULL;
    const inktag_gui_rect_t *a=&r.field.rectangle,*b=&r.physical;
    int32_t v[]={a->x,a->y,a->width,a->height,b->x,b->y,b->width,b->height,
        r.field.digit_count,r.key.logical_width,r.key.logical_height,0};
    memcpy(region_values,v,sizeof v);return region_values;
}
static void capture(void *ctx,uint8_t *black,uint8_t *red,uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    (void)ctx;
    uint16_t stride=(data.width+7u)/8u;
    if(!black || x || y!=rows || w!=stride*8u || y+h>data.height) {capture_failed=true;return;}
    for(uint16_t yy=0;yy<h;yy++)for(uint16_t xx=0;xx<data.width;xx++) {
        uint8_t mask=(uint8_t)(128u>>(xx&7u));uint32_t off=(uint32_t)yy*stride+xx/8u;
        pixels[(uint32_t)(y+yy)*data.width+xx]=red && !(red[off]&mask) ? 2u:!(black[off]&mask) ? 1u:0u;
    }
    rows=(uint16_t)(y+h);
}
const uint8_t *nf_render(void) {
    uint32_t count=(uint32_t)data.width*data.height;
    if(!count)return NULL;
    if(count>pixel_capacity) {
        uint8_t *next=realloc(pixels,count);if(!next)return NULL;
        pixels=next;pixel_capacity=count;
    }
    memset(pixels,0,count);rows=0;capture_failed=false;
    uint32_t buffer_size=(uint32_t)((data.width+7u)/8u)*data.height*2u;
    uint8_t *buffer=malloc(buffer_size);if(!buffer)return NULL;
    gui_options_t options={.color_plane=GFX_COLOR_PLANE_ALL,.force_buffer_color=true,
        .buffer_color=2,.prefer_full_frame=true,.render_buffer=buffer,.render_buffer_size=buffer_size};
    bool ok=DrawGUIEx(&data,capture,NULL,&options) && !capture_failed && rows==data.height;
    free(buffer);return ok ? pixels:NULL;
}
