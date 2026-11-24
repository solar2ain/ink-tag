#ifndef INKTAG_GUI_FONT_ROLES_H
#define INKTAG_GUI_FONT_ROLES_H

#include "inktag_gui_asset_package.h"
#include "inktag_gui_fixed_layout.h"
#include "inktag_release_v1.h"

#define INKTAG_GUI_NATIVE_PACKAGE_SCHEMA INKTAG_RELEASE_FONT_VERSION
#define INKTAG_GUI_ROLE_FACE_CAPACITY 6u
#define INKTAG_GUI_ROLE_INFO_SIZE 24u

typedef struct {
    inktag_gui_asset_info_t base;
    uint16_t supported_schemas;
    uint8_t role_version, flags;
} inktag_gui_role_info_t;

bool inktag_gui_role_decode_info(const uint8_t *, uint16_t, inktag_gui_role_info_t *);
bool inktag_gui_role_encode_info(const inktag_gui_role_info_t *, uint8_t *, uint16_t);
uint32_t inktag_gui_role_info_limit(const inktag_gui_role_info_t *);
/* Native-only ports declare the generated release-v1 INFO contract. */
bool inktag_gui_native_validate(inktag_gui_asset_read_t, void *,
    inktag_gui_asset_package_header_t *, inktag_gui_asset_entry_t *,
    uint8_t entry_capacity, uint32_t maximum_bytes);
bool inktag_gui_native_transfer_valid(const inktag_transfer_descriptor_t *, uint32_t);
#endif
