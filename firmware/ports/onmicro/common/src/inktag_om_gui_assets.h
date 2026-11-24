#ifndef INKTAG_OM_GUI_ASSETS_H
#define INKTAG_OM_GUI_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_gui_asset_package.h"

typedef enum {
    INKTAG_OM_GUI_ASSET_OK = 0,
    INKTAG_OM_GUI_ASSET_INVALID,
    INKTAG_OM_GUI_ASSET_IO_ERROR,
    INKTAG_OM_GUI_ASSET_STALE_GENERATION,
    INKTAG_OM_GUI_ASSET_BUSY,
} inktag_om_gui_asset_result_t;

typedef struct {
    uint8_t active_bank;
    uint8_t schema;
    uint32_t generation;
    uint32_t total_size;
    uint32_t package_crc32;
} inktag_om_gui_asset_info_t;

bool inktag_om_gui_asset_available(void);
bool inktag_om_gui_asset_transfer_active(void);
bool inktag_om_gui_asset_descriptor_valid(const inktag_transfer_descriptor_t *descriptor);
bool inktag_om_gui_asset_info(inktag_om_gui_asset_info_t *info);
inktag_om_gui_asset_result_t inktag_om_gui_asset_begin(
    const inktag_transfer_descriptor_t *descriptor);
inktag_om_gui_asset_result_t inktag_om_gui_asset_append(
    uint32_t offset, const uint8_t *data, uint16_t length);
inktag_om_gui_asset_result_t inktag_om_gui_asset_commit(void);
void inktag_om_gui_asset_abort(void);
/* Explicit content clear only; never used by preference reset or OTA. */
inktag_om_gui_asset_result_t inktag_om_gui_asset_clear(void);

#endif
