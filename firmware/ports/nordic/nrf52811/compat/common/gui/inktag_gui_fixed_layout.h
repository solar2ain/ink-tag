#ifndef INKTAG_GUI_FIXED_LAYOUT_H
#define INKTAG_GUI_FIXED_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

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

#endif
