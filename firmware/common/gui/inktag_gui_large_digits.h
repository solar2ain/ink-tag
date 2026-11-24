#ifndef INKTAG_GUI_LARGE_DIGITS_H
#define INKTAG_GUI_LARGE_DIGITS_H

#include "Adafruit_GFX.h"
#include "inktag_gui_font_roles.h"
#include <string.h>

typedef struct {
    const uint8_t *large[2];
    uint32_t length[2], generation, crc32;
    uint8_t schema, entry_count;
} inktag_gui_role_snapshot_t;

typedef struct {
    const uint8_t *font;
    inktag_gui_digit_metrics_t scaled;
    uint8_t tier;
} inktag_gui_large_style_t;

typedef struct {
    inktag_gui_role_snapshot_t source;
    inktag_gui_digit_metrics_t metrics[2];
    bool has_large;
} inktag_gui_role_frame_t;

/* Called only after the existing atomic begin. The port must bind all faces,
 * generation and byte-reader to that same session; no per-glyph source choice.
 * Native ports render only embedded source0 or release-v1 IKFA user data. */
static inline bool inktag_gui_role_frame_prepare(inktag_gui_role_frame_t *frame,
    const inktag_gui_role_snapshot_t *source) {
    if (!frame || !source ||
        (source->schema && source->schema!=INKTAG_GUI_NATIVE_PACKAGE_SCHEMA) ||
        (source->entry_count!=4u && source->entry_count!=6u) ||
        (!source->schema && source->entry_count!=4u) ||
        (!source->schema && (source->generation || source->crc32))) return false;
    frame->source=*source;frame->has_large=source->large[0]!=NULL;
    if (frame->has_large!=(source->large[1]!=NULL) || frame->has_large!=(source->entry_count==6u) ||
        (!source->schema && frame->has_large)) return false;
    for(uint8_t i=0u;i<2u;++i) {
        if (!source->large[i]) {if(source->length[i])return false;continue;}
        if(source->length[i]<31u)return false;
        u8g2_font_t font={0};u8g2_SetFont(&font,source->large[i]);
        uint8_t width=(uint8_t)u8g2_GetGlyphWidth(&font,'0');
        uint8_t colon=(uint8_t)u8g2_GetGlyphWidth(&font,':');
        uint8_t height=(uint8_t)font.font_info.max_char_height;
        if(!width || width>127u || !colon || colon>width || !height || height>127u)return false;
        frame->metrics[i]=(inktag_gui_digit_metrics_t){width,width,colon,0u,height,height};
    }
    return true;
}


static inline bool inktag_gui_large_fit(const inktag_gui_role_frame_t *frame,
    uint8_t count,const inktag_gui_rect_t *area,inktag_gui_large_style_t *style,
    inktag_gui_fixed_field_t *field) {
    bool found=false;uint16_t best_height=0u;
    if(!frame || !frame->has_large || !area || !style || !field)return false;
    inktag_gui_rect_t bounds=*area;
    for(uint8_t i=0u;i<2u;++i) {
        const inktag_gui_digit_metrics_t *native=&frame->metrics[i];
        inktag_gui_fixed_field_t candidate;
        if(inktag_gui_fixed_center(native,count,&bounds,&candidate) && (!found || native->height>best_height)) {
            *style=(inktag_gui_large_style_t){frame->source.large[i],*native,i};
            best_height=native->height;found=true;
        }
    }
    if(!found)return false;
    memset(field,0,sizeof(*field));
    return inktag_gui_fixed_center(&style->scaled,count,&bounds,field);
}

static inline void inktag_gui_large_draw(Adafruit_GFX *gfx,
    const inktag_gui_large_style_t *style,const inktag_gui_fixed_field_t *field,
    uint8_t left,uint8_t right,uint16_t color) {
    if(!gfx || !style || !field || !style->font ||
       (field->digit_count!=2u && field->digit_count!=4u) || left>99u || right>99u)return;
    GFX_setFont(gfx,style->font);GFX_setFontScale(gfx,256u);GFX_setFontMode(gfx,1u);
    u8g2_SetForegroundColor(&gfx->u8g2,color);
    const uint8_t digits[4]={left/10u,left%10u,right/10u,right%10u};
    for(uint8_t place=0u;place<=field->digit_count;++place) {
        bool colon=place==field->digit_count;
        if(colon && field->digit_count!=4u)break;
        uint8_t digit=colon ? 10u:digits[place];
        if(digit>10u)return;
        int16_t origin=colon ? field->colon_x:field->digit_x[place];
        u8g2_DrawGlyph(&gfx->u8g2,origin,field->rectangle.y+style->scaled.height,(uint16_t)('0'+digit));
    }
}

#endif
