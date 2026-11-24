#include "inktag_gui_asset_package.h"
#include "inktag_gui_font_roles.h"

#include <limits.h>
#include <string.h>

static bool add_u32(uint32_t left, uint32_t right, uint32_t *result) {
    if (UINT32_MAX - left < right) return false;
    *result = left + right;
    return true;
}

static bool limit_valid(uint32_t limit) {
    return limit >= 88u && limit <= INKTAG_GUI_ASSET_PACKAGE_ABSOLUTE_MAX_BYTES;
}

static bool transfer_bounds_valid(
    const inktag_transfer_descriptor_t *descriptor, uint32_t maximum_bytes) {
    return limit_valid(maximum_bytes) && descriptor != NULL &&
           descriptor->object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE &&
           descriptor->segment_count == 1u && descriptor->black_mask == 0u &&
           descriptor->total_size >=
               INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE +
                   INKTAG_GUI_ASSET_FONT_COUNT * INKTAG_GUI_ASSET_ENTRY_SIZE &&
           descriptor->total_size <= maximum_bytes &&
           descriptor->segment_size[0] == descriptor->total_size &&
           descriptor->segment_size[1] == 0u;
}

static bool read_crc32(inktag_gui_asset_read_t read, void *context,
                       uint32_t offset, uint32_t length, uint32_t expected,
                       bool package) {
    uint8_t chunk[64];
    uint32_t crc = UINT32_C(0xffffffff);
    while (length != 0u) {
        uint16_t amount = length > sizeof(chunk) ? sizeof(chunk) : (uint16_t)length;
        if (!read(context, offset, chunk, amount)) return false;
        /* The validated package is at least its 88-byte header, so the first
         * 64-byte read always contains the complete CRC field at 16..19. */
        if (package && offset == 0u) memset(chunk + 16u, 0, 4u);
        crc = inktag_crc32_update(crc, chunk, amount);
        offset += amount;
        length -= amount;
    }
    return (crc ^ UINT32_C(0xffffffff)) == expected;
}

uint32_t inktag_gui_role_info_limit(const inktag_gui_role_info_t *info) {
    const inktag_gui_asset_info_t *b;
    if (!info) return 0u;
    b=&info->base;
    if (b->version!=INKTAG_RELEASE_FONT_INFO_VERSION || info->supported_schemas!=INKTAG_RELEASE_FONT_SCHEMA_MASK || info->role_version!=INKTAG_RELEASE_FONT_ROLE_VERSION || info->flags!=INKTAG_RELEASE_FONT_FLAGS ||
        (b->schema && b->schema!=INKTAG_GUI_NATIVE_PACKAGE_SCHEMA) || b->storage_mode!=1u ||
        !limit_valid(b->maximum_size)) return 0u;
    if (b->active_bank==255u) {
        if (b->schema || b->generation || b->active_size || b->active_crc32) return 0u;
    } else if (b->active_bank || !b->schema || b->active_size<88u ||
               b->active_size>b->maximum_size) return 0u;
    return b->maximum_size;
}

bool inktag_gui_role_decode_info(const uint8_t *p,uint16_t n,inktag_gui_role_info_t *info) {
    if (!p || !info || n!=INKTAG_GUI_ROLE_INFO_SIZE) return false;
    info->base=(inktag_gui_asset_info_t){p[0],p[1],p[2],p[3],
        inktag_read_u32_le(p+4),inktag_read_u32_le(p+8),
        inktag_read_u32_le(p+12),inktag_read_u32_le(p+16)};
    info->supported_schemas=inktag_read_u16_le(p+20);
    info->role_version=p[22];info->flags=p[23];
    return inktag_gui_role_info_limit(info)!=0u;
}

bool inktag_gui_role_encode_info(const inktag_gui_role_info_t *info,uint8_t *p,uint16_t n) {
    const inktag_gui_asset_info_t *b;
    if (!p || n<INKTAG_GUI_ROLE_INFO_SIZE || !inktag_gui_role_info_limit(info)) return false;
    b=&info->base;
    p[0]=b->version;p[1]=b->schema;p[2]=b->active_bank;p[3]=b->storage_mode;
    inktag_write_u32_le(p+4,b->generation);inktag_write_u32_le(p+8,b->active_size);
    inktag_write_u32_le(p+12,b->maximum_size);inktag_write_u32_le(p+16,b->active_crc32);
    inktag_write_u16_le(p+20,info->supported_schemas);p[22]=info->role_version;p[23]=info->flags;
    return true;
}

bool inktag_gui_native_transfer_valid(const inktag_transfer_descriptor_t *d,uint32_t maximum) {
    return d && d->format==INKTAG_GUI_NATIVE_PACKAGE_SCHEMA && transfer_bounds_valid(d,maximum);
}


/* Validate native text faces before the existing U8g2 decoder can see them.
 * A bounded64-byte reader is shared by table and glyph checks; no full-font
 * RAM copy, arbitrary pointer walk, or new storage/recovery mechanism. */
typedef struct __attribute__((aligned(4))) {
    inktag_gui_asset_read_t read;void *context;
    uint32_t base,length,start;uint16_t cached;
    uint8_t data[64];
} role_font_reader_t;

static bool role_font_byte(role_font_reader_t *r,uint32_t offset,uint8_t *out) {
    if(offset>=r->length)return false;
    if(offset<r->start || offset-r->start>=r->cached) {
        uint32_t left=r->length-offset;
        r->cached=left>sizeof r->data ? sizeof r->data:(uint16_t)left;
        if(!r->read(r->context,r->base+offset,r->data,r->cached))return false;
        r->start=offset;
    }
    *out=r->data[offset-r->start];return true;
}

static bool role_font_word(role_font_reader_t *r,uint32_t offset,uint16_t *out) {
    uint8_t a,b;
    if(!role_font_byte(r,offset,&a) || !role_font_byte(r,offset+1u,&b))return false;
    *out=(uint16_t)((uint16_t)a*256u+b);return true;
}

static bool role_font_bits(role_font_reader_t *r,uint32_t *bit,uint32_t end,
                           uint8_t count,uint16_t *out) {
    uint8_t byte;uint16_t value=0u;
    if(!count || count>8u || *bit+count>end*8u)return false;
    for(uint8_t i=0u;i<count;++i,++*bit) {
        if(!role_font_byte(r,*bit/8u,&byte))return false;
        value|=(uint16_t)(((byte>>(*bit&7u))&1u)<<i);
    }
    *out=value;return true;
}

static bool role_font_glyph(role_font_reader_t *r,uint32_t *position,uint8_t prefix,
                            const uint8_t *h,uint8_t face,uint16_t code) {
    uint8_t length;uint16_t width,height,value,zero,one,repeat;
    int x,y,advance;
    uint32_t bit,end,pixels=0u;
    if(!role_font_byte(r,*position+prefix-1u,&length) || length<=prefix)return false;
    end=*position+length;bit=(*position+prefix)*8u;
    if(end>r->length || !role_font_bits(r,&bit,end,h[4],&width) ||
        !role_font_bits(r,&bit,end,h[5],&height) || width>h[9] || height>h[10] ||
        !role_font_bits(r,&bit,end,h[6],&value) || (width && !height))return false;
    x=(int)value-(int)(1u<<(h[6]-1u));
    if(!role_font_bits(r,&bit,end,h[7],&value))return false;
    y=(int)value-(int)(1u<<(h[7]-1u));
    if(!role_font_bits(r,&bit,end,h[8],&value))return false;
    advance=(int)value-(int)(1u<<(h[8]-1u));
    {
        int left=(int8_t)h[11],bottom=(int8_t)h[12];
        if(x<left || x+width>left+h[9] || y<bottom || y+height>bottom+h[10] || advance<0)return false;
        if(face>=4u && (code<48u || code>58u || x || y || height!=h[10] ||
            !width || width!=(unsigned)advance || (code!=58u && width!=h[9])))return false;
    }
    while(width && pixels<(uint32_t)width*height) {
        if(!role_font_bits(r,&bit,end,h[2],&zero) || !role_font_bits(r,&bit,end,h[3],&one) ||
            (!zero && !one))return false;
        do {
            pixels+=zero+one;
            if(pixels>(uint32_t)width*height)return false;
            if(!role_font_bits(r,&bit,end,1u,&repeat) ||
                (repeat && pixels>=(uint32_t)width*height))return false;
        } while(repeat);
    }
    *position=end;return true;
}

static bool role_font_valid(inktag_gui_asset_read_t read,void *context,
                            const inktag_gui_asset_entry_t *entry) {
    role_font_reader_t r={read,context,entry->offset,entry->length,UINT32_MAX,0u,{0}};
    uint8_t h[23],cp,length;uint16_t offset,delta,end_code,previous_end=255u,code;
    uint32_t position=23u,unicode,table_bytes,block,next=0u;unsigned count=0u,index=0u,previous=0u;
    bool shortcut_a=false,shortcut_lower=false;
    if(entry->length<31u || !read(context,entry->offset,h,sizeof h))return false;
    {
        int left=(int8_t)h[11],bottom=(int8_t)h[12],top=bottom+h[10];
        if(!h[9] || !h[10] || left+h[9]>127 || top<0 || top>127 || bottom>0 ||
            h[13]!=top || (int8_t)h[14]!=bottom || h[15]!=top || (int8_t)h[16]!=bottom ||
            (entry->id>=4u && (left || bottom || h[0]!=11u)))return false;
    }
    for(uint8_t i=2u;i<9u;++i)if(!h[i] || h[i]>8u)return false;
    if(h[4]>7u || h[5]>7u || h[9]>127u || h[10]>127u || h[13]>127u || h[15]>127u)return false;
    unicode=23u+(uint16_t)((uint16_t)h[21]*256u+h[22]);
    uint16_t upper=(uint16_t)((uint16_t)h[17]*256u+h[18]);
    uint16_t lower=(uint16_t)((uint16_t)h[19]*256u+h[20]);
    if(unicode<25u || unicode+4u>r.length)return false;
    while(position+1u<unicode) {
        if(!role_font_byte(&r,position,&cp) || !role_font_byte(&r,position+1u,&length))return false;
        if(!length)break;
        if(!cp || cp<=previous)return false;
        if(position==23u+upper)shortcut_a=true;
        if(position==23u+lower)shortcut_lower=true;
        previous=cp;++count;
        if(!role_font_glyph(&r,&position,2u,h,entry->id,cp) || position>unicode)return false;
    }
    if(position+2u!=unicode || !role_font_byte(&r,position,&cp) || cp ||
        !role_font_byte(&r,position+1u,&length) || length ||
        (upper && !shortcut_a) || (lower && !shortcut_lower) ||
        !role_font_word(&r,unicode,&offset) || offset<4u || offset%4u)return false;
    table_bytes=offset;block=unicode+table_bytes;
    if(block+2u>r.length || !role_font_word(&r,unicode+2u,&end_code))return false;
    position=block;previous=255u;
    if(table_bytes>4u) {
        if(!role_font_word(&r,unicode+4u,&delta) || !delta)return false;
        next=block+delta;
    }
    for(;;) {
        if(next && position>next)return false;
        if(next && position==next) {
            previous_end=end_code;++index;
            if(!role_font_word(&r,unicode+index*4u+2u,&end_code) || end_code<=previous_end)return false;
            if((index+1u)*4u<table_bytes) {
                if(!role_font_word(&r,unicode+(index+1u)*4u,&delta) || !delta)return false;
                next=position+delta;
            } else next=0u;
        }
        if(!role_font_word(&r,position,&code))return false;
        if(!code)break;
        if(code<=previous || code<=previous_end || code>end_code || code==65535u)return false;
        previous=code;++count;
        if(!role_font_glyph(&r,&position,3u,h,entry->id,code))return false;
    }
    if(index*4u+4u!=table_bytes || end_code!=65535u || r.length-position>3u ||
        (uint8_t)count!=h[0] || (entry->id>=4u && count!=11u))return false;
    while(position<r.length)if(!role_font_byte(&r,position++,&cp) || cp)return false;
    return true;
}

bool inktag_gui_native_validate(inktag_gui_asset_read_t read,void *context,
    inktag_gui_asset_package_header_t *header,inktag_gui_asset_entry_t *entries,
    uint8_t capacity,uint32_t maximum) {
    uint8_t raw[24];uint32_t end;
    if (!read || !header || !entries || !limit_valid(maximum) ||
        !read(context,0u,raw,24u) || inktag_read_u32_le(raw)!=INKTAG_RELEASE_FONT_MAGIC ||
        inktag_read_u16_le(raw+4)!=INKTAG_GUI_NATIVE_PACKAGE_SCHEMA ||
        (raw[20]!=4u && raw[20]!=6u) || raw[20]>capacity || raw[21] || raw[22] || raw[23]) return false;
    header->header_size=inktag_read_u16_le(raw+6);
    header->generation=inktag_read_u32_le(raw+8);
    header->total_size=inktag_read_u32_le(raw+12);
    header->package_crc32=inktag_read_u32_le(raw+16);
    header->entry_count=raw[20];
    if (header->header_size!=24u+16u*header->entry_count ||
        header->total_size<header->header_size || header->total_size>maximum) return false;
    end=header->header_size;
    for (uint8_t i=0u;i<header->entry_count;++i) {
        inktag_gui_asset_entry_t *e=&entries[i];
        if (!read(context,24u+16u*i,raw,16u)) return false;
        e->kind=raw[0];e->id=raw[1];e->format=raw[2];e->flags=raw[3];
        e->offset=inktag_read_u32_le(raw+4);e->length=inktag_read_u32_le(raw+8);e->crc32=inktag_read_u32_le(raw+12);
        if (e->kind!=1u || e->id!=i || e->flags || e->format!=
            1u ||
            !e->length || e->offset!=end || !add_u32(e->offset,e->length,&end) ||
            end>header->total_size || !read_crc32(read,context,e->offset,e->length,e->crc32,false))return false;
        if(!role_font_valid(read,context,e))return false;
    }
    return end==header->total_size && read_crc32(read,context,0u,end,header->package_crc32,true);
}
