#include "inktag_nrf_build_profile.h"
#include "inktag_nrf_internal_assets.h"

#include <stdint.h>
#include <string.h>

#include "inktag_gui_assets.h"
#include "inktag_gui_fonts.h"
#if defined(INKTAG_NRF_GUI_ASSET_HOST_TEST)
#include <nrf_gui_asset_test_platform.h>
#else
#include "inktag_board.h"
#include "inktag_nrf_health.h"
#include "nrf_error.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "nrf_soc.h"
#endif

#define BANK_NONE (-1)
#define BANK_UNKNOWN (-2)
#define USER_FONT_VIRTUAL_BASE UINT32_C(0x60000000)
#define USER_FONT_VIRTUAL_END UINT32_C(0x61000000)

_Static_assert(INKTAG_BOARD_USER_FONT_SIZE == 32768u,
               "Single native user-font package must have 32 KiB");
_Static_assert(INKTAG_BOARD_USER_FONT_START + INKTAG_BOARD_USER_FONT_SIZE ==
                   INKTAG_BOARD_SETTINGS_START,
               "User-font data must end at Settings");

static int8_t s_active_bank = BANK_UNKNOWN;
static inktag_gui_asset_package_header_t s_active_header;
static inktag_gui_asset_entry_t s_faces[INKTAG_GUI_ROLE_FACE_CAPACITY];
static const uint8_t s_unavailable_font = 0xFFu;
static inktag_gui_asset_render_session_t s_session;
static bool s_read_failed;

#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_font_assets_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static uint32_t bank_address(uint8_t bank) {
    (void)bank;
    return INKTAG_BOARD_USER_FONT_START;
}

static bool read_bank(void *context, uint32_t offset, uint8_t *output,
                      uint16_t length) {
    uint32_t address = (uint32_t)(uintptr_t)context;
    if (output == NULL || length == 0u ||
        offset >= INKTAG_BOARD_USER_FONT_SIZE ||
        length > INKTAG_BOARD_USER_FONT_SIZE - offset)
        return false;
    memcpy(output, (const void *)(uintptr_t)(address + offset), length);
    return true;
}

#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_font_assets_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static bool select_active(void) {
    inktag_gui_asset_package_header_t header;
    inktag_gui_asset_entry_t entries[INKTAG_GUI_ROLE_FACE_CAPACITY];
    if (s_active_bank != BANK_UNKNOWN) return s_active_bank >= 0;
    if (!inktag_gui_native_validate(read_bank,
            (void *)(uintptr_t)bank_address(0u), &header, entries,
            INKTAG_GUI_ROLE_FACE_CAPACITY, INKTAG_BOARD_USER_FONT_SIZE)) {
        s_active_bank = BANK_NONE;
        return false;
    }
    s_active_header = header;
    memset(s_faces, 0, sizeof(s_faces));
    for (uint8_t index = 0u; index < header.entry_count; ++index)
        s_faces[entries[index].id] = entries[index];
    s_active_bank = 0;
    return true;
}

bool inktag_nrf_gui_asset_available(void) {
    (void)select_active();
    return true; /* Erased/invalid user data uses embedded defaults. */
}

bool inktag_nrf_gui_asset_info(inktag_nrf_gui_asset_info_t *info) {
    if (info == NULL) return false;
    if (!select_active()) {
        memset(info, 0, sizeof(*info));
        info->active_bank = 0xFFu;
        return true;
    }
    info->active_bank = (uint8_t)s_active_bank;
    info->schema = INKTAG_GUI_NATIVE_PACKAGE_SCHEMA;
    info->generation = s_active_header.generation;
    info->total_size = s_active_header.total_size;
    info->package_crc32 = s_active_header.package_crc32;
    return true;
}

#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_font_assets_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
bool inktag_nrf_gui_asset_transfer_active(void) { return false; }
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_begin(
    const inktag_transfer_descriptor_t *descriptor) {
    (void)descriptor;
    return INKTAG_NRF_GUI_ASSET_INVALID;
}
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_append(
    uint32_t offset, const uint8_t *data, uint16_t length) {
    (void)offset; (void)data; (void)length;
    return INKTAG_NRF_GUI_ASSET_INVALID;
}
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_commit(void) {
    return INKTAG_NRF_GUI_ASSET_INVALID;
}
void inktag_nrf_gui_asset_abort(void) {}
#endif
bool inktag_gui_assets_begin(void) {
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_font_assets_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
    if (!inktag_gui_asset_render_begin(&s_session, select_active()))
        return false;
#endif
    s_read_failed = false;
    return true;
}
bool inktag_gui_assets_healthy(void) { return inktag_gui_asset_render_healthy(&s_session); }
void inktag_gui_assets_end(void) { inktag_gui_asset_render_end(&s_session); }
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
    if (out->entry_count == 6u) for (uint8_t i = 0u; i < 2u; ++i) {
        out->large[i] = (const uint8_t *)(uintptr_t)(USER_FONT_VIRTUAL_BASE + s_faces[4u+i].offset);
        out->length[i] = s_faces[4u+i].length;
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
        s_read_failed = true;
        return &s_unavailable_font;
    }
    if (!(s_session.active ? s_session.user_selected : select_active()))
        return fallback[face_id];
    return (const uint8_t *)(uintptr_t)(
        USER_FONT_VIRTUAL_BASE + s_faces[face_id].offset);
}

uint8_t inktag_gui_asset_read_byte(const uint8_t *address) {
    uintptr_t value = (uintptr_t)address;
    bool user = value >= USER_FONT_VIRTUAL_BASE && value < USER_FONT_VIRTUAL_END;
    uint32_t offset = (uint32_t)(value - USER_FONT_VIRTUAL_BASE);
    if (address == NULL || (user &&
        (s_active_bank != 0 || offset >= INKTAG_BOARD_USER_FONT_SIZE ||
         offset >= s_active_header.total_size || (s_session.active && s_session.failed)))) {
        inktag_gui_asset_render_fail(&s_session);
        s_read_failed = true;
        return 0xFFu;
    }
    if (user) return *(const uint8_t *)(uintptr_t)(bank_address(0u) + offset);
    return *address;
}
