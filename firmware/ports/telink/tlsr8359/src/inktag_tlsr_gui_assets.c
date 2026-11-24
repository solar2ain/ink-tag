#include "../../common/src/inktag_tlsr_build_profile.h"
#ifdef INKTAG_TLSR_GUI_ASSET_HOST_TEST
#include <tlsr_gui_asset_test_platform.h>
#else
#include "main.h"
#endif
#include <limits.h>
#include <string.h>
#include "inktag_tlsr_gui_assets.h"
#include "inktag_tlsr_user_font_storage.h"
#include "inktag_tlsr_power.h"
#include "inktag_gui_assets.h"
#include "inktag_gui_font_cache.h"
#include "inktag_gui_fonts.h"

/* Native-only single-region adapter. Validation/rendering reuse frozen common
 * ed00a3c8; storage semantics follow GR b7eeffe3, with Telink power/readback IO.
 * Never migrate/erase an old package at boot. Header is programmed last. */
#define GUI_ASSET_VIRTUAL_BASE UINT32_C(0x60000000)
#define GUI_ASSET_VIRTUAL_END UINT32_C(0x61000000)
#define GUI_ASSET_CACHE_BYTES 256u
#define GUI_ASSET_CACHE_SLOTS 4u
#define GUI_ASSET_HEADER_BYTES INKTAG_GUI_ASSET_PACKAGE_HEADER_SIZE
#define GUI_ASSET_BANK_NONE 0xFFu
#define GUI_ASSET_ENTRY_CAPACITY INKTAG_GUI_ROLE_FACE_CAPACITY

/* Private RAM transaction; the explicit 24B package header stays unchanged. */
#if INKTAG_BUILD_PRO
#include "inktag_gui_upload_state_type_pro.inc"
#endif
typedef struct { const uint8_t *pending_header; } gui_asset_reader_t;

/* -2 unchecked, -1 embedded default, 0 complete user package. */
/* Source selection and atomic-session metadata survive LOW32K retention.
 * Losing the cached bank/header can select a zero-length user package after
 * wake. Only the per-render byte cache below is disposable scratch RAM. */
static RAM int8_t s_active_bank = -2;
static RAM inktag_gui_asset_package_header_t s_active_header;
static RAM inktag_gui_asset_entry_t s_active_entries[GUI_ASSET_ENTRY_CAPACITY];
#if INKTAG_BUILD_PRO
#include "inktag_gui_upload_state_pro.inc"
#endif
/* Small paged canvases repeatedly alternate glyph indices and bitmap data.
 * Keep four bounded SDK reads rather than evicting the index at every glyph.
 * This is ordinary scratch RAM, never part of the deep-retention state. */
static uint8_t s_cache[GUI_ASSET_CACHE_SLOTS][GUI_ASSET_CACHE_BYTES];
static uint32_t s_cache_offset[GUI_ASSET_CACHE_SLOTS];
static uint16_t s_cache_length[GUI_ASSET_CACHE_SLOTS];
static uint8_t s_cache_age[GUI_ASSET_CACHE_SLOTS], s_cache_current;
static RAM inktag_gui_asset_render_session_t s_session;
static RAM bool s_read_failed, s_default_after_render;

static void cache_reset(void) {
    inktag_gui_font_cache_reset(s_cache_offset,s_cache_length,s_cache_age,
        GUI_ASSET_CACHE_SLOTS,&s_cache_current);
}
static void select_default(void) {
    s_active_bank = -1;
    memset(&s_active_header, 0, sizeof(s_active_header));
    cache_reset();
}
static bool storage_read(uint32_t offset, uint8_t *data, uint32_t length) {
    return inktag_tlsr_user_font_read(offset, data, length);
}
#if INKTAG_BUILD_PRO
#include "inktag_gui_upload_io_pro.inc"
#endif
bool inktag_tlsr_gui_asset_available(void) {
#if defined(INKTAG_TLSR_GUI_ASSET_HOST_TEST)
    return true;
#else
    const inktag_tlsr_board_profile_t *profile =
        inktag_tlsr_board_profile(inktag_board_detect());
    return profile && profile->physical_flash_bytes >=
        INKTAG_USER_FONT_START + INKTAG_USER_FONT_SIZE;
#endif
}
static bool package_read(void *context, uint32_t offset, uint8_t *output, uint16_t length) {
    gui_asset_reader_t *reader = context;
    if (reader == NULL || output == NULL || offset > INKTAG_USER_FONT_SIZE ||
        length > INKTAG_USER_FONT_SIZE - offset) return false;
    if (reader->pending_header != NULL && offset < GUI_ASSET_HEADER_BYTES) {
        uint16_t count = (uint16_t)(GUI_ASSET_HEADER_BYTES - offset);
        if (count > length) count = length;
        memcpy(output, reader->pending_header + offset, count);
        output += count;
        offset += count;
        length = (uint16_t)(length - count);
    }
    return length == 0u || storage_read(offset, output, length);
}
static bool validate_user(const uint8_t *pending,
    inktag_gui_asset_package_header_t *header,
    inktag_gui_asset_entry_t entries[GUI_ASSET_ENTRY_CAPACITY]) {
    gui_asset_reader_t reader = {pending};
    return inktag_gui_native_validate(package_read, &reader, header, entries,
        GUI_ASSET_ENTRY_CAPACITY, INKTAG_USER_FONT_SIZE);
}
static bool select_active(void) {
    inktag_gui_asset_package_header_t header;
    inktag_gui_asset_entry_t entries[GUI_ASSET_ENTRY_CAPACITY];
    if (s_active_bank >= -1) return s_active_bank == 0;
    if (!inktag_tlsr_gui_asset_available() || !validate_user(NULL, &header, entries)) {
        select_default();
        return false;
    }
    s_active_header = header;
    memset(s_active_entries, 0, sizeof(s_active_entries));
    for (uint8_t i = 0u; i < header.entry_count; ++i)
        s_active_entries[entries[i].id] = entries[i];
    s_active_bank = 0;
    cache_reset();
    return true;
}
bool inktag_tlsr_gui_asset_bootstrap(void) {
    (void)select_active();
    return true; /* Empty user storage is normal; complete defaults are linked. */
}
#if INKTAG_BUILD_PRO
#include "inktag_gui_upload_admission_pro.inc"
#else
bool inktag_tlsr_gui_asset_transfer_active(void) { return false; }
bool inktag_tlsr_gui_asset_descriptor_valid(const inktag_transfer_descriptor_t *descriptor) { (void)descriptor; return false; }
#endif
bool inktag_tlsr_gui_asset_info(inktag_tlsr_gui_asset_info_t *info) {
    if (info == NULL || !inktag_tlsr_gui_asset_available()) return false;
    bool user = select_active();
    info->active_bank = user ? 0u : GUI_ASSET_BANK_NONE;
    info->schema = user ? INKTAG_GUI_NATIVE_PACKAGE_SCHEMA : 0u;
    info->generation = user ? s_active_header.generation : 0u;
    info->total_size = user ? s_active_header.total_size : 0u;
    info->package_crc32 = user ? s_active_header.package_crc32 : 0u;
    return true;
}
#if INKTAG_BUILD_PRO
#include "inktag_gui_upload_pro.inc"
#else
inktag_tlsr_gui_asset_result_t inktag_tlsr_gui_asset_begin(const inktag_transfer_descriptor_t *descriptor) { (void)descriptor; return INKTAG_TLSR_GUI_ASSET_INVALID; }
inktag_tlsr_gui_asset_result_t inktag_tlsr_gui_asset_append(uint32_t offset, const uint8_t *data, uint16_t length) { (void)offset; (void)data; (void)length; return INKTAG_TLSR_GUI_ASSET_INVALID; }
inktag_tlsr_gui_asset_result_t inktag_tlsr_gui_asset_commit(void) { return INKTAG_TLSR_GUI_ASSET_INVALID; }
void inktag_tlsr_gui_asset_abort(void) {}
#endif
bool inktag_gui_assets_begin(void) {
#if INKTAG_BUILD_PRO
    if (s_session.active || s_transaction.active) return false;
#else
    if (s_session.active) return false;
#endif
    if (!inktag_gui_asset_render_begin(&s_session, select_active())) return false;
    s_read_failed = false;
    s_default_after_render = false;
    cache_reset();
    return true;
}
bool inktag_gui_assets_healthy(void) { return inktag_gui_asset_render_healthy(&s_session); }
void inktag_gui_assets_end(void) {
    inktag_gui_asset_render_end(&s_session);
    cache_reset();
    if (s_default_after_render) select_default();
}
bool inktag_gui_asset_font_read_failed(void) { return s_read_failed; }
bool inktag_gui_assets_role_snapshot(inktag_gui_role_snapshot_t *out) {
    if (!out || !inktag_gui_assets_healthy()) return false;
    memset(out, 0, sizeof(*out));
    out->entry_count = INKTAG_GUI_ASSET_FONT_COUNT;
    if (!s_session.user_selected) return true;
    if (s_active_bank != 0) return false;
    out->schema = INKTAG_GUI_NATIVE_PACKAGE_SCHEMA;
    out->entry_count = s_active_header.entry_count;
    out->generation = s_active_header.generation;
    out->crc32 = s_active_header.package_crc32;
    if (out->entry_count == 6u) {
        for (uint8_t i = 0u; i < 2u; ++i) {
            out->large[i] = (const uint8_t *)(uintptr_t)
                (GUI_ASSET_VIRTUAL_BASE + s_active_entries[4u + i].offset);
            out->length[i] = s_active_entries[4u + i].length;
        }
    }
    return true;
}
const uint8_t *inktag_gui_asset_font(uint8_t face_id) {
    static const uint8_t *const fallback[INKTAG_GUI_ASSET_FONT_COUNT] = {
        inktag_gui_fallback_font_wqy9, inktag_gui_fallback_font_wqy12,
        inktag_gui_fallback_font_helvB14, inktag_gui_fallback_font_helvB18,
    };
    if (face_id >= INKTAG_GUI_ASSET_FONT_COUNT) {
        inktag_gui_asset_render_fail(&s_session);
        return (const uint8_t *)(uintptr_t)GUI_ASSET_VIRTUAL_BASE;
    }
    bool user = s_session.active ? s_session.user_selected : select_active();
    if (!user) return fallback[face_id];
    return (const uint8_t *)(uintptr_t)(GUI_ASSET_VIRTUAL_BASE + s_active_entries[face_id].offset);
}
uint8_t inktag_gui_asset_read_byte(const uint8_t *address) {
    uintptr_t value = (uintptr_t)address;
    if (value < GUI_ASSET_VIRTUAL_BASE || value >= GUI_ASSET_VIRTUAL_END)
        return *address;
    uint32_t offset = (uint32_t)value - GUI_ASSET_VIRTUAL_BASE;
    if (s_active_bank != 0 || offset >= INKTAG_USER_FONT_SIZE ||
        offset >= s_active_header.total_size ||
        (s_session.active && s_session.failed)) {
        inktag_gui_asset_render_fail(&s_session);
        s_read_failed = true;
        return 0xFFu;
    }
    uint32_t aligned = offset & ~(GUI_ASSET_CACHE_BYTES - 1u);
    uint8_t slot = s_cache_current;
    if (s_cache_offset[slot] != aligned) {
        slot = inktag_gui_font_cache_choose(s_cache_offset,s_cache_age,
            GUI_ASSET_CACHE_SLOTS,s_cache_current,aligned);
        if (s_cache_offset[slot] != aligned) {
            uint32_t remaining = s_active_header.total_size - aligned;
            uint16_t count = remaining < GUI_ASSET_CACHE_BYTES ? (uint16_t)remaining : GUI_ASSET_CACHE_BYTES;
            if (!storage_read(aligned, s_cache[slot], count)) {
                inktag_gui_asset_render_fail(&s_session);
                s_read_failed = true;
                if (s_session.active) s_default_after_render = true;
                else select_default();
                return 0xFFu;
            }
            s_cache_offset[slot] = aligned;
            s_cache_length[slot] = count;
        }
        inktag_gui_font_cache_touch(s_cache_age,GUI_ASSET_CACHE_SLOTS,slot,
            &s_cache_current);
    }
    if (offset - aligned >= s_cache_length[slot]) {
        inktag_gui_asset_render_fail(&s_session);
        s_read_failed = true;
        return 0xFFu;
    }
    return s_cache[slot][offset - aligned];
}
