#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "inktag_da_health.h"
#include "inktag_da_image_store.h"
#include "inktag_image_upload_v5.h"
#include "inktag_protocol_core.h"
#include "spi_flash.h"

#define FLASH_SIZE 0x40000u
#define START 0x24000u
#define END 0x35000u
static uint8_t flash[FLASH_SIZE], saved[FLASH_SIZE], payload[30000], readback[30000];
static unsigned depth, operations, fail_at, mutations, ble_calls, reads;
static bool allow = true, read_fail, read_short, abrupt, cut_latched, cancel_once;
static jmp_buf cut;
static epd_screen_config_t configured;

static bool operation_fails(void) {
    operations++;
    if (cut_latched) return true;
    if (operations != fail_at) return false;
    cut_latched = true; return true;
}
int spi_flash_release_from_power_down(void) { assert(depth == 0); depth = 1; return 0; }
int spi_flash_power_down(void) { assert(depth == 1); depth = 0; return 0; }
int spi_flash_read_data(uint8_t *out, uint32_t addr, uint32_t size, uint32_t *actual) {
    assert(depth == 1 && addr >= START && addr <= END && size <= END - addr);
    reads++; if (read_fail) return SPI_FLASH_ERR_FAILED;
    memcpy(out, flash + addr, size); *actual = size - (read_short ? 1u : 0u); return 0;
}
int spi_flash_write_data(uint8_t *data, uint32_t addr, uint32_t size, uint32_t *actual) {
    assert(depth == 1 && addr >= START && addr <= END && size <= END - addr);
    *actual = 0;
    /* SDK write_data splits the caller's range into NOR pages. Inject at each
     * physical program boundary, including a partially programmed page. */
    while (size) {
        uint32_t count = 256u - (addr & 255u);
        if (count > size) count = size;
        bool failed = operation_fails();
        uint32_t prefix = failed ? (count + 1u) / 2u : count;
        for (uint32_t i = 0; i < prefix; i++) flash[addr + i] &= data[i];
        *actual += prefix;
        if (failed) { if (abrupt) longjmp(cut, 1); return SPI_FLASH_ERR_FAILED; }
        addr += count; data += count; size -= count;
    }
    return 0;
}
int spi_flash_block_erase(uint32_t addr, int mode) {
    assert(depth == 1 && addr >= START && addr <= END - 4096 && !(addr & 4095) && mode == SPI_FLASH_OP_SE);
    bool failed = operation_fails();
    memset(flash + addr, 255, failed ? 512u : 4096u);
    if (failed) { if (abrupt) longjmp(cut, 1); return SPI_FLASH_ERR_FAILED; }
    return 0;
}
bool inktag_da_health_operation_allowed(inktag_power_operation_t op, bool force) {
    assert(op == INKTAG_POWER_OPERATION_FLASH && !force); mutations++; return allow;
}
void app_service_ble(void) {
    assert(depth == 0); ble_calls++;
    if (cancel_once) { cancel_once=false; inktag_da_store_abort(); }
}
epd_screen_status_t epd_screen_config_encode(const epd_screen_config_t *s, uint8_t wire[EPD_SCREEN_WIRE_SIZE]) {
    if (!s || !wire) return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(wire, 0, EPD_SCREEN_WIRE_SIZE);
    wire[0]=s->schema_version; wire[1]=s->driver_id; wire[2]=s->panel_recipe;
    wire[3]=s->color_id; wire[4]=s->transform;
    wire[5]=s->width; wire[6]=s->width>>8; wire[7]=s->height; wire[8]=s->height>>8;
    wire[9]=s->visible_width; wire[10]=s->visible_width>>8;
    wire[11]=s->visible_height; wire[12]=s->visible_height>>8;
    return EPD_SCREEN_STATUS_OK;
}
epd_screen_status_t epd_screen_config_decode_exact(const uint8_t *wire, size_t length, epd_screen_config_t *s) {
    if (!wire || !s || length != EPD_SCREEN_WIRE_SIZE || wire[0] != EPD_SCREEN_SCHEMA_VERSION)
        return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(s,0,sizeof(*s)); s->schema_version=wire[0]; s->driver_id=wire[1];
    s->panel_recipe=wire[2]; s->color_id=wire[3]; s->transform=wire[4];
    s->width=wire[5]|wire[6]<<8; s->height=wire[7]|wire[8]<<8;
    s->visible_width=wire[9]|wire[10]<<8; s->visible_height=wire[11]|wire[12]<<8;
    return s->width && s->height ? EPD_SCREEN_STATUS_OK : EPD_SCREEN_STATUS_INVALID_GEOMETRY;
}
bool epd_screen_wire_image_compatible(const uint8_t a[EPD_SCREEN_WIRE_SIZE], const uint8_t b[EPD_SCREEN_WIRE_SIZE]) {
    return !memcmp(a,b,EPD_SCREEN_WIRE_SIZE);
}
bool inktag_da_active_screen_config(epd_screen_config_t *s) { *s = configured; return true; }
epd_screen_status_t epd_screen_resolve(const epd_screen_config_t *s, epd_screen_t *out) {
    if (!s || !out || !s->width || !s->height) return EPD_SCREEN_STATUS_INVALID_GEOMETRY;
    memset(out,0,sizeof(*out)); out->config=*s;
    out->frame_bytes=((s->width+7u)/8u)*s->height*2u; return EPD_SCREEN_STATUS_OK;
}
static epd_screen_config_t profile(uint16_t width,uint16_t height) {
    epd_screen_config_t s={0}; s.schema_version=EPD_SCREEN_SCHEMA_VERSION;
    s.driver_id=EPD_SCREEN_DRIVER_SSD1619; s.panel_recipe=1; s.color_id=EPD_SCREEN_COLOR_BWR;
    s.width=s.visible_width=width; s.height=s.visible_height=height; return s;
}
static void reboot(void) {
    depth=0; fail_at=0; operations=0; cut_latched=false; read_fail=read_short=false; allow=true;
    assert(inktag_image_store_init());
}
static uint8_t upload_flags=1u;
static inktag_image_store_result_t upload(const char *name,uint8_t requested,uint8_t type,uint8_t *id) {
    epd_screen_t resolved; epd_screen_resolve(&configured,&resolved);
    uint32_t size=resolved.frame_bytes;
    uint32_t segments[2]={size/2,size-size/2};
    inktag_image_store_result_t r=inktag_da_store_begin(&configured,1,2,upload_flags,type,segments,
        size,inktag_crc32_update(UINT32_MAX,payload,size)^UINT32_MAX,1,name,requested,id);
    if(r!=INKTAG_IMAGE_STORE_OK) return r;
    for(uint32_t off=0;off<size;) {
        uint16_t n=size-off>220?220:size-off;
        r=inktag_image_store_append(payload+off,n);
        if(r!=INKTAG_IMAGE_STORE_OK) { inktag_da_store_abort(); return r; }
        off+=n;
    }
    r=inktag_da_store_commit(id);
    if(r!=INKTAG_IMAGE_STORE_OK) inktag_da_store_abort();
    return r;
}
static void descriptor_cases(void) {
    uint8_t wire[80]={INKTAG_TRANSFER_OBJECT_STORED_IMAGE,1,2,1};
    inktag_stored_image_v5_descriptor_t d;
    wire[4]=0x40; wire[5]=0x1f; /* 8000 bytes, two4000-byte planes. */
    wire[12]=wire[16]=0xa0; wire[13]=wire[17]=0x0f;
    const char *utf8="中文图片-1234567890123456789012";
    assert(strlen(utf8)==35); wire[38]=35; memcpy(wire+39,utf8,35);
    assert(inktag_stored_image_v5_descriptor_decode(wire,74,&d)==INKTAG_STATUS_OK && d.replace_slot==255 && !strcmp(d.name,utf8));
    wire[74]=7;
    assert(inktag_stored_image_v5_descriptor_decode(wire,75,&d)==INKTAG_STATUS_OK && d.replace_slot==7);
    wire[74]=255;
    assert(inktag_stored_image_v5_descriptor_decode(wire,75,&d)==INKTAG_STATUS_NOT_FOUND);
    wire[38]=36;
    assert(inktag_stored_image_v5_descriptor_decode(wire,75,&d)==INKTAG_STATUS_INVALID_REQUEST);
    wire[38]=35; wire[39]=0;
    assert(inktag_stored_image_v5_descriptor_decode(wire,74,&d)==INKTAG_STATUS_INVALID_REQUEST);
    wire[38]=1; wire[39]=0xc0;
    assert(inktag_stored_image_v5_descriptor_decode(wire,40,&d)==INKTAG_STATUS_INVALID_REQUEST);
    wire[38]=0;
    assert(inktag_stored_image_v5_descriptor_decode(wire,39,&d)==INKTAG_STATUS_OK);
    assert(inktag_stored_image_v5_descriptor_decode(wire,38,&d)==INKTAG_STATUS_INVALID_REQUEST);
}
static void orientation_roundtrip(void) {
    configured=profile(128,250); memset(payload,0x34,sizeof(payload));
    for(unsigned rotation=0;rotation<4;++rotation) {
        uint8_t id; inktag_image_store_header_t header;
        memset(flash,0xa5,sizeof(flash)); reboot(); upload_flags=1u|(rotation<<2);
        assert(upload("orientation",255,0,&id)==INKTAG_IMAGE_STORE_OK);
        reboot(); assert(operations==0);
        assert(inktag_image_store_get(id,&header)==INKTAG_IMAGE_STORE_OK);
        assert(header.black_mask==upload_flags);
        assert(inktag_image_store_read(id,0,readback,8000)==INKTAG_IMAGE_STORE_OK);
        assert(!memcmp(readback,payload,8000));
    }
    upload_flags=1u;
}

int main(void) {
    orientation_roundtrip();
    uint8_t id; uint16_t removed;
    inktag_image_store_header_t h;
    inktag_image_store_status_t st;
    configured=profile(128,250);
    memset(flash,0xa5,sizeof(flash)); memset(payload,0x34,sizeof(payload));
    reboot();
    assert(inktag_image_store_sector_count()==17 && inktag_image_store_fixed_slot_count()==8);
    assert(operations==0); /* Obsolete factory/old format is never auto-erased. */
    descriptor_cases();
    assert(upload("中文图片-1234567890123456789012",255,0,&id)==INKTAG_IMAGE_STORE_OK && id==0);
    assert(upload("neighbor",255,0,&id)==INKTAG_IMAGE_STORE_OK && id==1);
    assert(upload("specified-empty",7,0,&id)==INKTAG_IMAGE_STORE_OK && id==7);
    assert(inktag_da_store_delete(7)==INKTAG_IMAGE_STORE_OK);
    reboot();
    assert(inktag_image_store_get(7,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_get(1,&h)==INKTAG_IMAGE_STORE_OK);
    assert(!strcmp(h.name,"neighbor")); /* Shared clients resolve names to stable IDs. */
    assert(inktag_image_store_read(1,0,readback,8000)==INKTAG_IMAGE_STORE_OK && !memcmp(readback,payload,8000));
    unsigned before=operations;
    assert(upload("NEIGHBOR",255,0,&id)==INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(upload("slot-1",255,0,&id)==INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(upload("中文图片-12345678901234567890123",255,0,&id)==INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(upload("bad-id",8,0,&id)==INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(before==operations);
    for(unsigned mode=0;mode<2;mode++) {
        read_fail=mode==0; read_short=mode==1;
        assert(inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_IO_ERROR);
        assert(inktag_da_store_next(255,&id,&h)==INKTAG_IMAGE_STORE_IO_ERROR);
        assert(inktag_image_store_status(&configured,8000,&st)==INKTAG_IMAGE_STORE_IO_ERROR);
        assert(inktag_da_store_clear(NULL,&removed)==INKTAG_IMAGE_STORE_IO_ERROR);
        assert(upload("read-failure",255,0,&id)==INKTAG_IMAGE_STORE_IO_ERROR);
        assert(before==operations && depth==0);
    }
    read_fail=read_short=false;
    allow=false;
    assert(upload("low",255,0,&id)==INKTAG_IMAGE_STORE_LOW_POWER);
    assert(inktag_da_store_delete(1)==INKTAG_IMAGE_STORE_LOW_POWER);
    assert(inktag_da_store_clear(NULL,&removed)==INKTAG_IMAGE_STORE_LOW_POWER);
    assert(before==operations); allow=true;
    memcpy(saved,flash,sizeof(saved));
    memset(payload,0x78,8000); operations=0;
    assert(upload("replacement",0,0,&id)==INKTAG_IMAGE_STORE_OK);
    unsigned total=operations,cases=0;
    for(unsigned mode=0;mode<2;mode++) for(unsigned point=1;point<=total;point++) {
        memcpy(flash,saved,sizeof(saved)); reboot();
        fail_at=point; abrupt=mode!=0;
        if(!setjmp(cut)) assert(upload("replacement",0,0,&id)==INKTAG_IMAGE_STORE_IO_ERROR);
        reboot();
        assert(!memcmp(flash,saved,START));
        assert(!memcmp(flash+START+8192,saved+START+8192,sizeof(flash)-START-8192));
        inktag_image_store_result_t r=inktag_image_store_get(0,&h);
        assert(r==INKTAG_IMAGE_STORE_OK || r==INKTAG_IMAGE_STORE_NOT_FOUND);
        if(r==INKTAG_IMAGE_STORE_OK) {
            assert(inktag_image_store_read(0,0,readback,8000)==INKTAG_IMAGE_STORE_OK);
            uint8_t value=!strcmp(h.name,"replacement")?0x78:0x34;
            for(unsigned n=0;n<8000;n++) assert(readback[n]==value);
        }
        cases++;
    }
    memcpy(flash,saved,sizeof(saved)); reboot(); abrupt=false;
    for(unsigned n=2;n<8;n++) { char name[16]; snprintf(name,sizeof(name),"image-%u",n);
        assert(upload(name,255,0,&id)==INKTAG_IMAGE_STORE_OK && id==n); }
    assert(upload("full",255,0,&id)==INKTAG_IMAGE_STORE_NO_SPACE);
    assert(upload("full-replace",7,4,&id)==INKTAG_IMAGE_STORE_OK && id==7);
    assert(inktag_image_store_get(7,&h)==INKTAG_IMAGE_STORE_OK && h.content_type==4);
    assert(inktag_image_store_status(&configured,8000,&st)==INKTAG_IMAGE_STORE_OK && st.saved_count==8 && st.free_sectors==0);
    epd_screen_config_t large=profile(400,300);
    assert(!inktag_image_store_profile_allowed(&large));
    assert(inktag_da_store_clear(NULL,&removed)==INKTAG_IMAGE_STORE_OK && removed==8);
    assert(inktag_image_store_profile_allowed(&large));
    /* Capacity comes only from the existing 17 sectors and whole-frame slot
     * alignment. Exercise every slot, including IDs above an old quota of two. */
    const unsigned widths[]={128,400},heights[]={296,300},counts[]={5,2};
    for(unsigned geometry=0;geometry<2;geometry++) {
        configured=profile(widths[geometry],heights[geometry]); reboot();
        assert(inktag_image_store_sector_count()==17);
        assert(inktag_image_store_fixed_slot_count()==counts[geometry]);
        assert(inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
        for(unsigned n=0;n<counts[geometry];n++) {
            char name[20];snprintf(name,sizeof(name),"physical-%u",n);
            assert(upload(name,255,0,&id)==INKTAG_IMAGE_STORE_OK && id==n);
        }
        assert(upload("beyond-physical",255,0,&id)==INKTAG_IMAGE_STORE_NO_SPACE);
        assert(upload("replace-last",counts[geometry]-1,0,&id)==INKTAG_IMAGE_STORE_OK);
        assert(id==counts[geometry]-1);
        assert(inktag_da_store_clear(NULL,&removed)==INKTAG_IMAGE_STORE_OK);
        assert(removed==counts[geometry]);
    }
    configured=profile(128,250); reboot();
    assert(inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
    read_fail=true; assert(!inktag_image_store_init());
    assert(inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_IO_ERROR);
    assert(inktag_da_store_next(255,&id,&h)==INKTAG_IMAGE_STORE_IO_ERROR);
    read_fail=false; reboot();
    cancel_once=true;
    assert(upload("disconnect-during-begin",255,0,&id)==INKTAG_IMAGE_STORE_BUSY);
    assert(!inktag_image_store_busy() && !inktag_image_store_capture_active());
    reboot();
    /* Cancel after partial data; CRC and size rejection never publish magic. */
    uint32_t sizes[2]={4000,4000};
    assert(inktag_da_store_begin(&configured,1,2,1,0,sizes,8000,0,1,"cancel",255,&id)==INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_append(payload,220)==INKTAG_IMAGE_STORE_OK);
    assert(inktag_da_store_commit(NULL)==INKTAG_IMAGE_STORE_SIZE_MISMATCH);
    assert(inktag_image_store_append(payload,8000)==INKTAG_IMAGE_STORE_SIZE_MISMATCH);
    inktag_da_store_abort(); reboot();
    assert(inktag_image_store_get(id,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
    sizes[0]=UINT32_MAX; sizes[1]=0;
    assert(inktag_da_store_begin(&configured,1,1,1,0,sizes,UINT32_MAX,0,0,"overflow",255,&id)==INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    assert(inktag_image_store_status(&configured,UINT32_MAX,&st)==INKTAG_IMAGE_STORE_BAD_ARGUMENT);
    /* A completed magic write followed by failed readback is still cleared,
     * and its failure event must not strand the next operation at BUSY. */
    sizes[0]=sizes[1]=4000;
    uint32_t checksum=inktag_crc32_update(UINT32_MAX,payload,8000)^UINT32_MAX;
    assert(inktag_da_store_begin(&configured,1,2,1,0,sizes,8000,checksum,1,"readback",255,&id)==INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_append(payload,8000)==INKTAG_IMAGE_STORE_OK);
    read_fail=true;
    assert(inktag_da_store_commit(&id)==INKTAG_IMAGE_STORE_IO_ERROR);
    inktag_image_store_event_t event;
    assert(!inktag_image_store_take_event(&event));
    read_fail=false;
    assert(inktag_da_store_clear(NULL,&removed)==INKTAG_IMAGE_STORE_OK);
    reboot(); assert(inktag_image_store_get(id,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_da_store_begin(&configured,1,2,1,0,sizes,8000,0,1,"crc",255,&id)==INKTAG_IMAGE_STORE_OK);
    assert(inktag_image_store_append(payload,8000)==INKTAG_IMAGE_STORE_OK);
    assert(inktag_da_store_commit(NULL)==INKTAG_IMAGE_STORE_CRC_MISMATCH);
    inktag_da_store_abort(); reboot();
    assert(inktag_image_store_get(id,&h)==INKTAG_IMAGE_STORE_NOT_FOUND);
    assert(inktag_image_store_read(1,UINT32_MAX,readback,1)==INKTAG_IMAGE_STORE_NOT_FOUND);
    epd_screen_config_t small=configured;
    configured=profile(1024,1024);
    before=reads;
    assert(!inktag_image_store_init() && inktag_image_store_fixed_slot_count()==0);
    assert(reads==before && inktag_image_store_get(0,&h)==INKTAG_IMAGE_STORE_NO_SPACE);
    assert(inktag_da_store_profile_allowed(&small));
    configured=small; reboot();
    assert(depth==0 && mutations && ble_calls && reads);
    printf("DA v5 actual backend: physical8/5/2 capacity, holes/reboot/name/35B/full replace, IO/low power/cancel/profile; %u interrupted physical boundaries; neighbors/reserved protected\n",cases);
    return 0;
}
