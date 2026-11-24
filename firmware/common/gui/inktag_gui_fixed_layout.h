#ifndef INKTAG_GUI_FIXED_LAYOUT_H
#define INKTAG_GUI_FIXED_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

/* Format cells, not the ink bbox of a particular number. This module does not
 * authorize partial refresh or keep previous-frame/panel state. */
typedef struct { int16_t x, y; uint16_t width, height; } inktag_gui_rect_t;
typedef struct {
    uint16_t digit_advance, digit_cell_width, colon_advance, colon_offset;
    uint16_t height, baseline;
} inktag_gui_digit_metrics_t;
typedef struct {
    inktag_gui_rect_t rectangle;
    int16_t digit_x[4], colon_x;
    uint8_t digit_count;
} inktag_gui_fixed_field_t;
typedef struct {
    uint16_t canvas_width, canvas_height;
    inktag_gui_rect_t viewport;
    uint8_t rotation, mirrors; /* rotation0..3 first, physical mirrors bits0/1 */
} inktag_gui_region_transform_t;
typedef struct {
    uint32_t font_generation, font_crc32, layout_revision;
    uint16_t font_schema, font_tier, font_scale_q8, color_mode, logical_width, logical_height;
    uint8_t page, format, native_ui_scale;
    inktag_gui_rect_t logical_rectangle;
    inktag_gui_digit_metrics_t metrics;
    inktag_gui_region_transform_t transform;
} inktag_gui_region_key_t;

static inline uint16_t inktag_gui_fixed_width(
    const inktag_gui_digit_metrics_t *m, uint8_t count) {
    uint32_t width;
    if (!m || (count != 2u && count != 4u) || !m->digit_advance ||
        !m->digit_cell_width || !m->height || m->baseline > m->height ||
        (count == 4u && (!m->colon_advance || m->colon_offset >= m->colon_advance))) return 0u;
    width=(uint32_t)(count-1u)*m->digit_advance+m->digit_cell_width;
    if (count == 4u) width+=m->colon_advance;
    return width<=INT16_MAX && m->height<=INT16_MAX ? (uint16_t)width:0u;
}

static inline bool inktag_gui_fixed_center(
    const inktag_gui_digit_metrics_t *m, uint8_t count,
    const inktag_gui_rect_t *area, inktag_gui_fixed_field_t *out) {
    uint16_t width=inktag_gui_fixed_width(m,count);
    if (!area || !out || !width || area->x<0 || area->y<0 ||
        width>area->width || m->height>area->height ||
        (uint32_t)area->x+area->width>INT16_MAX ||
        (uint32_t)area->y+area->height>INT16_MAX) return false;
    out->rectangle=(inktag_gui_rect_t){
        (int16_t)(area->x+(area->width-width)/2u),
        (int16_t)(area->y+(area->height-m->height)/2u),width,m->height};
    out->digit_count=count;
    out->colon_x=count==4u ? (int16_t)(out->rectangle.x+2u*m->digit_advance+m->colon_offset):-1;
    for (uint8_t i=0u;i<4u;++i)
        out->digit_x[i]=i<count ? (int16_t)(out->rectangle.x+i*m->digit_advance+
                            (count==4u && i>=2u ? m->colon_advance:0u)):-1;
    return true;
}

static inline bool inktag_gui_region_map(
    const inktag_gui_region_transform_t *t, const inktag_gui_rect_t *logical,
    inktag_gui_rect_t *physical) {
    int32_t x,y,w,h,vw,vh,lw,lh;
    if (!t || !logical || !physical || t->rotation>3u || t->mirrors>3u ||
        !t->canvas_width || !t->canvas_height || t->canvas_width>INT16_MAX ||
        t->canvas_height>INT16_MAX || t->viewport.x<0 || t->viewport.y<0 ||
        !t->viewport.width || !t->viewport.height ||
        (uint32_t)t->viewport.x+t->viewport.width>t->canvas_width ||
        (uint32_t)t->viewport.y+t->viewport.height>t->canvas_height) return false;
    vw=t->viewport.width;vh=t->viewport.height;
    lw=(t->rotation&1u) ? vh:vw;lh=(t->rotation&1u) ? vw:vh;
    x=logical->x;y=logical->y;w=logical->width;h=logical->height;
    if (x<0 || y<0 || !w || !h || x+w>lw || y+h>lh) return false;
    if(t->rotation==1u){int32_t old=x;x=vw-y-h;y=old;old=w;w=h;h=old;}
    else if(t->rotation==2u){x=vw-x-w;y=vh-y-h;}
    else if(t->rotation==3u){int32_t old=x;x=y;y=vh-old-w;old=w;w=h;h=old;}
    if(t->mirrors&1u)x=vw-x-w;
    if(t->mirrors&2u)y=vh-y-h;
    *physical=(inktag_gui_rect_t){(int16_t)(x+t->viewport.x),
        (int16_t)(y+t->viewport.y),(uint16_t)w,(uint16_t)h};
    return true;
}

static inline bool inktag_gui_region_key_equal(
    const inktag_gui_region_key_t *a,const inktag_gui_region_key_t *b) {
    if(!a || !b)return false;
    /* Compare only value bytes. Never compare inter-member or tail padding.
     * The compile-time layout predicate preserves the fieldwise fallback on
     * an ABI that inserts padding inside these two ranges. All fields use
     * fixed-width integer types, with no padding bits or floating values. */
    if (offsetof(inktag_gui_region_key_t,native_ui_scale)+sizeof(a->native_ui_scale)==27u &&
        sizeof(a->logical_rectangle)==8u && sizeof(a->metrics)==12u &&
        sizeof(a->transform)==14u &&
        offsetof(inktag_gui_region_key_t,metrics)==offsetof(inktag_gui_region_key_t,logical_rectangle)+8u &&
        offsetof(inktag_gui_region_key_t,transform)==offsetof(inktag_gui_region_key_t,logical_rectangle)+20u) {
        return !memcmp(a,b,27u) &&
            !memcmp(&a->logical_rectangle,&b->logical_rectangle,34u);
    }
#define INKTAG_GUI_KEY_SAME_(member) (a->member==b->member)
    return INKTAG_GUI_KEY_SAME_(font_generation)&&INKTAG_GUI_KEY_SAME_(font_crc32)&&INKTAG_GUI_KEY_SAME_(layout_revision)&&INKTAG_GUI_KEY_SAME_(font_schema)&&
        INKTAG_GUI_KEY_SAME_(font_tier)&&INKTAG_GUI_KEY_SAME_(font_scale_q8)&&INKTAG_GUI_KEY_SAME_(color_mode)&&INKTAG_GUI_KEY_SAME_(logical_width)&&INKTAG_GUI_KEY_SAME_(logical_height)&&INKTAG_GUI_KEY_SAME_(page)&&INKTAG_GUI_KEY_SAME_(format)&&INKTAG_GUI_KEY_SAME_(native_ui_scale)&&
        INKTAG_GUI_KEY_SAME_(logical_rectangle.x)&&INKTAG_GUI_KEY_SAME_(logical_rectangle.y)&&INKTAG_GUI_KEY_SAME_(logical_rectangle.width)&&INKTAG_GUI_KEY_SAME_(logical_rectangle.height)&&
        INKTAG_GUI_KEY_SAME_(metrics.digit_advance)&&INKTAG_GUI_KEY_SAME_(metrics.digit_cell_width)&&INKTAG_GUI_KEY_SAME_(metrics.colon_advance)&&
        INKTAG_GUI_KEY_SAME_(metrics.colon_offset)&&INKTAG_GUI_KEY_SAME_(metrics.height)&&INKTAG_GUI_KEY_SAME_(metrics.baseline)&&
        INKTAG_GUI_KEY_SAME_(transform.canvas_width)&&INKTAG_GUI_KEY_SAME_(transform.canvas_height)&&
        INKTAG_GUI_KEY_SAME_(transform.viewport.x)&&INKTAG_GUI_KEY_SAME_(transform.viewport.y)&&INKTAG_GUI_KEY_SAME_(transform.viewport.width)&&
        INKTAG_GUI_KEY_SAME_(transform.viewport.height)&&INKTAG_GUI_KEY_SAME_(transform.rotation)&&INKTAG_GUI_KEY_SAME_(transform.mirrors);
#undef INKTAG_GUI_KEY_SAME_
}
#endif
