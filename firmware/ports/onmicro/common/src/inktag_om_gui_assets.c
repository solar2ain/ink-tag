#ifdef INKTAG_OM_GUI_ASSET_HOST_TEST
#include <om_gui_asset_test_platform.h>
#else
#include "om.h"
#include "om_driver.h"
#include "inktag_om_flash_layout.h"
#include "inktag_om_health.h"
#endif

#include <limits.h>
#include <string.h>

#include "inktag_gui_assets.h"
#include "inktag_gui_font_cache.h"
#include "inktag_om_gui_assets.h"
#if defined(INKTAG_GUI_EMBEDDED_FONT_FALLBACK)
#include "inktag_gui_fonts.h"
#endif

/* Read-only base IKFA1 provider. Upload/clear transactions live in the Pro provider.
 * Native IKFA1 only; one complete source per render. Physical reads
 * retain the qualified OM RAM-code, IRQ and fresh-voltage discipline below. */
#define GUI_ASSET_VIRTUAL_BASE UINT32_C(0x60000000)
#define GUI_ASSET_VIRTUAL_END UINT32_C(0x61000000)
#define GUI_ASSET_CACHE_BYTES 256u
/* Minimum multi-line budget: retain an index page beside its glyph page. */
#define GUI_ASSET_CACHE_LINES 2u
#define GUI_ASSET_HEADER_BYTES INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE
#define GUI_ASSET_ENTRY_CAPACITY INKTAG_GUI_ROLE_FACE_CAPACITY
#if !defined(INKTAG_OM_GUI_ASSET_HOST_TEST) && defined(__GNUC__)
#define OM_GUI_ASSET_RAM_CODE __attribute__((section(".ram_code"), noinline))
#else
#define OM_GUI_ASSET_RAM_CODE
#endif
_Static_assert(INKTAG_OM_GUI_ASSET_BANK_SIZE == 32768u, "Native user region is 32 KiB");
typedef struct { const uint8_t *pending_header; } gui_asset_reader_t;
static int8_t s_active_bank = -2;
static inktag_gui_asset_package_header_t s_active_header;
static inktag_gui_asset_entry_t s_active_entries[GUI_ASSET_ENTRY_CAPACITY];
static uint8_t s_cache[GUI_ASSET_CACHE_LINES][GUI_ASSET_CACHE_BYTES];
static uint32_t s_cache_offset[GUI_ASSET_CACHE_LINES];
static uint16_t s_cache_length[GUI_ASSET_CACHE_LINES];
static uint8_t s_cache_age[GUI_ASSET_CACHE_LINES], s_cache_current;
static inktag_gui_asset_render_session_t s_session;
static bool s_read_failed, s_default_after_render;

#if defined(INKTAG_OM_GUI_ASSET_HOST_TEST)
static bool flash_read(uint32_t address, void *data, uint32_t length) {
    return om_gui_asset_test_read(address, data, length);
}

#else
static OM_GUI_ASSET_RAM_CODE bool flash_read(uint32_t address, void *data,
                                              uint32_t length) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_read(address, data, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

#endif

static void cache_reset(void) {
    inktag_gui_font_cache_reset(s_cache_offset,s_cache_length,s_cache_age,
        GUI_ASSET_CACHE_LINES,&s_cache_current);
}
static void select_default(void) {
    s_active_bank=-1;
    memset(&s_active_header,0,sizeof(s_active_header));
    cache_reset();
}
#if defined(INKTAG_OM_GUI_ASSET_HOST_TEST)
void inktag_om_gui_asset_test_cold_reset(void) {
    s_active_bank=-2;
    memset(&s_active_header,0,sizeof(s_active_header));
    memset(s_active_entries,0,sizeof(s_active_entries));
    memset(&s_session,0,sizeof(s_session));
    s_read_failed=s_default_after_render=false; cache_reset();
}
#endif
static bool package_read(void *context,uint32_t offset,uint8_t *out,uint16_t length) {
    gui_asset_reader_t *reader=context;
    if(!reader || !out || offset>INKTAG_OM_GUI_ASSET_BANK_SIZE ||
       length>INKTAG_OM_GUI_ASSET_BANK_SIZE-offset) return false;
    if(reader->pending_header && offset<GUI_ASSET_HEADER_BYTES) {
        uint16_t n=(uint16_t)(GUI_ASSET_HEADER_BYTES-offset);
        if(n>length)n=length;
        memcpy(out,reader->pending_header+offset,n);
        offset+=n;out+=n;length=(uint16_t)(length-n);
    }
    return !length || flash_read(INKTAG_OM_GUI_ASSET_BANK_A_START+offset,out,length);
}
static bool validate_user(const uint8_t *pending,inktag_gui_asset_package_header_t *header,
                          inktag_gui_asset_entry_t *entries) {
    gui_asset_reader_t reader={pending};
    return inktag_gui_native_validate(package_read,&reader,header,entries,
        GUI_ASSET_ENTRY_CAPACITY,INKTAG_OM_GUI_ASSET_BANK_SIZE);
}
static bool select_active(void) {
    inktag_gui_asset_package_header_t header;
    inktag_gui_asset_entry_t entries[GUI_ASSET_ENTRY_CAPACITY];
    if(s_active_bank>=-1)return s_active_bank==0;
    if(!validate_user(NULL,&header,entries)){select_default();return false;}
    s_active_header=header;
    memset(s_active_entries,0,sizeof(s_active_entries));
    for(uint8_t i=0;i<header.entry_count;++i)s_active_entries[entries[i].id]=entries[i];
    s_active_bank=0;cache_reset();return true;
}

bool inktag_om_gui_asset_available(void) { return select_active(); }
bool inktag_om_gui_asset_transfer_active(void) { return false; }
bool inktag_om_gui_asset_descriptor_valid(const inktag_transfer_descriptor_t *d) {
    return inktag_gui_native_transfer_valid(d,INKTAG_OM_GUI_ASSET_BANK_SIZE);
}
bool inktag_om_gui_asset_info(inktag_om_gui_asset_info_t *out) {
    if(!out)return false;
    bool user=select_active();
    out->active_bank=user?0u:255u;
    out->schema=user?INKTAG_GUI_NATIVE_PACKAGE_SCHEMA:0u;
    out->generation=user?s_active_header.generation:0u;
    out->total_size=user?s_active_header.total_size:0u;
    out->package_crc32=user?s_active_header.package_crc32:0u;
    return true;
}
inktag_om_gui_asset_result_t inktag_om_gui_asset_begin(const inktag_transfer_descriptor_t *d) {
    (void)d; return INKTAG_OM_GUI_ASSET_INVALID;
}
inktag_om_gui_asset_result_t inktag_om_gui_asset_append(uint32_t offset,const uint8_t *data,uint16_t length) {
    (void)offset; (void)data; (void)length; return INKTAG_OM_GUI_ASSET_INVALID;
}
inktag_om_gui_asset_result_t inktag_om_gui_asset_commit(void) {
    return INKTAG_OM_GUI_ASSET_INVALID;
}
void inktag_om_gui_asset_abort(void) {

}
inktag_om_gui_asset_result_t inktag_om_gui_asset_clear(void) {
    return INKTAG_OM_GUI_ASSET_INVALID;
}
bool inktag_gui_assets_begin(void) {
    if(s_session.active)return false;
    if(!inktag_gui_asset_render_begin(&s_session,select_active()))return false;
    s_read_failed=s_default_after_render=false;cache_reset();return true;
}
bool inktag_gui_assets_healthy(void) { return inktag_gui_asset_render_healthy(&s_session); }
bool inktag_gui_asset_font_read_failed(void) { return s_read_failed; }
void inktag_gui_assets_end(void) {
    inktag_gui_asset_render_end(&s_session);cache_reset();
    if(s_default_after_render)select_default();
}
bool inktag_gui_assets_role_snapshot(inktag_gui_role_snapshot_t *out) {
    if(!out || !inktag_gui_assets_healthy())return false;
    memset(out,0,sizeof(*out));out->entry_count=4u;
    if(!s_session.user_selected)return true;
    if(s_active_bank!=0)return false;
    out->schema=INKTAG_GUI_NATIVE_PACKAGE_SCHEMA;out->entry_count=s_active_header.entry_count;
    out->generation=s_active_header.generation;out->crc32=s_active_header.package_crc32;
    if(out->entry_count==6u)for(uint8_t i=0;i<2u;++i) {
        out->large[i]=(const uint8_t *)(uintptr_t)(GUI_ASSET_VIRTUAL_BASE+s_active_entries[4u+i].offset);
        out->length[i]=s_active_entries[4u+i].length;
    }
    return true;
}
const uint8_t *inktag_gui_asset_font(uint8_t id) {
    static const uint8_t *const defaults[4]={
        inktag_gui_fallback_font_wqy9,inktag_gui_fallback_font_wqy12,
        inktag_gui_fallback_font_helvB14,inktag_gui_fallback_font_helvB18};
    if(id>=4u){s_read_failed=true;inktag_gui_asset_render_fail(&s_session);return (const uint8_t *)(uintptr_t)GUI_ASSET_VIRTUAL_BASE;}
    bool user=s_session.active?s_session.user_selected:select_active();
    return user?(const uint8_t *)(uintptr_t)(GUI_ASSET_VIRTUAL_BASE+s_active_entries[id].offset):defaults[id];
}
static uint8_t read_failed(void) {
    s_read_failed=true;inktag_gui_asset_render_fail(&s_session);
    if(s_session.active)s_default_after_render=true;else select_default();
    cache_reset();return 255u;
}
uint8_t inktag_gui_asset_read_byte(const uint8_t *address) {
    uintptr_t value=(uintptr_t)address;
    if(value<GUI_ASSET_VIRTUAL_BASE || value>=GUI_ASSET_VIRTUAL_END)return *address;
    uint32_t off=(uint32_t)value-GUI_ASSET_VIRTUAL_BASE;
    if(s_active_bank!=0 || off>=INKTAG_OM_GUI_ASSET_BANK_SIZE || off>=s_active_header.total_size ||
       (s_session.active && s_session.failed))return read_failed();
    uint32_t aligned=off&~(GUI_ASSET_CACHE_BYTES-1u);
    uint8_t slot=s_cache_current;
    if(s_cache_offset[slot]!=aligned) {
        slot=inktag_gui_font_cache_choose(s_cache_offset,s_cache_age,
            GUI_ASSET_CACHE_LINES,s_cache_current,aligned);
        if(s_cache_offset[slot]!=aligned) {
            uint32_t remaining=s_active_header.total_size-aligned;
            uint16_t n=remaining<GUI_ASSET_CACHE_BYTES?(uint16_t)remaining:GUI_ASSET_CACHE_BYTES;
            if(!flash_read(INKTAG_OM_GUI_ASSET_BANK_A_START+aligned,s_cache[slot],n))return read_failed();
            s_cache_offset[slot]=aligned;
            s_cache_length[slot]=n;
        }
        inktag_gui_font_cache_touch(s_cache_age,GUI_ASSET_CACHE_LINES,slot,
            &s_cache_current);
    }
    if(off-aligned>=s_cache_length[slot])return read_failed();
    return s_cache[slot][off-aligned];
}
