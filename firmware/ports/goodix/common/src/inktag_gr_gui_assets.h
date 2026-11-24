#ifndef INKTAG_GR_GUI_ASSETS_H
#define INKTAG_GR_GUI_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_gui_asset_package.h"
#include "inktag_gui_font_roles.h"
#include "inktag_gr_flash_layout.h"

typedef enum {
    INKTAG_GR_GUI_ASSET_OK = 0,
    INKTAG_GR_GUI_ASSET_INVALID,
    INKTAG_GR_GUI_ASSET_IO_ERROR,
    INKTAG_GR_GUI_ASSET_STALE_GENERATION,
    INKTAG_GR_GUI_ASSET_BUSY,
} inktag_gr_gui_asset_result_t;

typedef struct {
    uint8_t active_bank, schema;
    uint32_t generation;
    uint32_t total_size;
    uint32_t package_crc32;
} inktag_gr_gui_asset_info_t;

bool inktag_gr_gui_asset_backend_available(void);
bool inktag_gr_gui_asset_bootstrap(void);
bool inktag_gr_gui_asset_transfer_active(void);
bool inktag_gr_gui_asset_descriptor_valid(const inktag_transfer_descriptor_t *);
bool inktag_gr_gui_asset_info(inktag_gr_gui_asset_info_t *info);
inktag_gr_gui_asset_result_t inktag_gr_gui_asset_begin(
    const inktag_transfer_descriptor_t *descriptor);
inktag_gr_gui_asset_result_t inktag_gr_gui_asset_append(
    uint32_t offset, const uint8_t *data, uint16_t length);
inktag_gr_gui_asset_result_t inktag_gr_gui_asset_commit(void);
void inktag_gr_gui_asset_abort(void);

#endif
