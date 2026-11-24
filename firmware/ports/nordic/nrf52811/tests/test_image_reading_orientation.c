/* Compare shipping overlay against an independently composed reading->logical
 * ->physical pixel map, including mirrors, offsets, chunking and every packing. */
#define main original_brand_matrix
#include "test_inktag_brand_overlay.c"
#undef main
#include <string.h>
#include <stdio.h>
#include "inktag_protocol_core.h"

static uint32_t base_pixel_hash = 2166136261u;

static void turn(uint8_t q, unsigned w, unsigned h, unsigned *x, unsigned *y) {
    unsigned ox = *x, oy = *y;
    if (q == 1) { *x = h - 1 - oy; *y = ox; }
    if (q == 2) { *x = w - 1 - ox; *y = h - 1 - oy; }
    if (q == 3) { *x = oy; *y = w - 1 - ox; }
}
static void put(uint8_t *data, const epd_screen_config_t *s, uint8_t f,
                unsigned x, unsigned y, uint8_t v) {
    unsigned bits = f == 3 || f == 6 ? 2 : f == 5 ? 4 : 1;
    unsigned pixel = bits == 1 ? y * ((s->width + 7) / 8) * 8 + x : y * s->width + x;
    unsigned shift = 8 - bits - (pixel % (8 / bits)) * bits;
    unsigned i = pixel / (8 / bits), mask = ((1u << bits) - 1) << shift;
    data[i] = (data[i] & ~mask) | (v << shift);
}
static void matrix(void) {
    for (uint8_t t = 0; t < 16; t++) for (uint8_t q = 0; q < 4; q++)
    for (uint8_t f = 1; f <= 6; f++) for (uint8_t edition = 0; edition < 3; edition++) {
        epd_screen_config_t s = {.width=224,.height=168,.visible_x=8,.visible_y=8,
            .visible_width=200,.visible_height=144,.transform=t,.color_id=2};
        unsigned lw = t & 1 ? 144 : 200, lh = t & 1 ? 200 : 144;
        unsigned rw = q & 1 ? lh : lw, rh = q & 1 ? lw : lh;
        epd_screen_config_t r = {.width=rw,.height=rh,.visible_width=rw,.visible_height=rh,.color_id=2};
        unsigned bytes = segment_bytes(&s, f), refbytes = segment_bytes(&r, f);
        for (uint8_t seg = 0; seg < (f == 2 || f == 4 || f == 6 ? 2 : 1); seg++) {
            uint8_t *actual = malloc(bytes), *expected = malloc(bytes), *ref = malloc(refbytes), *before = malloc(refbytes);
            assert(actual && expected && ref && before);
            memset(actual, 0xa5, bytes); memset(expected, 0xa5, bytes);
            memset(ref, 0xa5, refbytes); memset(before, 0xa5, refbytes);
            assert(inktag_brand_overlay_apply(&r,f,seg,0,ref,refbytes,edition));
            for (unsigned y = 0; y < rh; y++) for (unsigned x = 0; x < rw; x++) {
                uint8_t value = packed(ref,&r,f,x,y);
                if (value == packed(before,&r,f,x,y)) continue;
                unsigned px=x, py=y;
                turn(q,rw,rh,&px,&py); turn(t & 3,lw,lh,&px,&py);
                if (t & 4) px = 199 - px;
                if (t & 8) py = 143 - py;
                put(expected,&s,f,px+8,py+8,value);
            }
            /* A byte equal in reference can still need a write at a rotated
             * packing offset; use badge bounds to include its entire white box. */
            for (unsigned y=0;y<rh;y++) for(unsigned x=0;x<rw;x++) {
                unsigned effective_edition=edition;
#if INKTAG_PROTOCOL_BASE_ONLY_TARGET
                effective_edition=0;
#endif
                unsigned bw=effective_edition==1?59:39, bx=rw-bw-3, by=rh-14;
                if (effective_edition==2 || x<bx || x>=bx+bw || y<by || y>=by+11) continue;
                unsigned px=x,py=y; turn(q,rw,rh,&px,&py);turn(t&3,lw,lh,&px,&py);
                if(t&4)px=199-px;
                if(t&8)py=143-py;
                put(expected,&s,f,px+8,py+8,packed(ref,&r,f,x,y));
            }
            for(unsigned off=0;off<bytes;off+=701) {
                uint16_t n=bytes-off>701?701:bytes-off;
                assert(inktag_brand_overlay_apply_oriented(&s,f,seg,off,actual+off,n,edition,q));
            }
            assert(!memcmp(actual,expected,bytes));
            if(edition==0)for(unsigned n=0;n<bytes;n++)base_pixel_hash=(base_pixel_hash^actual[n])*16777619u;
            free(actual);free(expected);free(ref);free(before);
        }
    }
}
static void descriptors(void) {
    uint8_t payload[20]={INKTAG_TRANSFER_OBJECT_DIRECT_FRAME,1,1,0};
    inktag_transfer_descriptor_t d;
    inktag_write_u32_le(payload+4,8);inktag_write_u32_le(payload+12,8);
    for(unsigned q=0;q<4;q++) for(unsigned compressed=0;compressed<2;compressed++) {
        payload[3]=1|(q<<2)|(compressed?128:0);
        assert(inktag_transfer_descriptor_decode(payload,20,&d)==INKTAG_STATUS_OK);
        assert(inktag_image_reading_rotation(d.black_mask)==q);
    }
    for(unsigned bit=16;bit<=64;bit<<=1) {
        payload[3]=bit;
        assert(inktag_transfer_descriptor_decode(payload,20,&d)==
               (bit==64 ? (INKTAG_TRANSFER_LZO1X_ENABLED ? INKTAG_STATUS_OK :
                            INKTAG_STATUS_UNSUPPORTED) : INKTAG_STATUS_INVALID_REQUEST));
    }
    payload[0]=INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR;payload[3]=16;
    assert(inktag_transfer_descriptor_decode(payload,20,&d)==INKTAG_STATUS_OK);
    epd_screen_config_t s={.width=200,.height=200,.visible_width=200,.visible_height=200};
    uint8_t untouched[4]={1,2,3,4};
    assert(!inktag_brand_overlay_apply_oriented(&s,1,0,0,untouched,4,0,4));
    assert(untouched[0]==1&&untouched[3]==4);
}
int main(void) {
#if !INKTAG_PROTOCOL_BASE_ONLY_TARGET
    original_brand_matrix();
#else
    test_nrf52811_hema_free_badge_is_red();
    test_invalid_metadata_is_non_mutating();
    test_invalid_metadata_is_not_written();
#endif
    matrix();descriptors();printf("InkTag pixels %08x\n",base_pixel_hash);return 0;
}
