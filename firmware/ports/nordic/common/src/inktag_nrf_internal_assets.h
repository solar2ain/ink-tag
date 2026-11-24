#ifndef INKTAG_NRF_INTERNAL_ASSETS_H
#define INKTAG_NRF_INTERNAL_ASSETS_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_gui_asset_package.h"
#include "inktag_gui_font_roles.h"

typedef struct {
    uint8_t active_bank;
    uint8_t schema;
    uint32_t generation;
    uint32_t total_size;
    uint32_t package_crc32;
} inktag_nrf_gui_asset_info_t;

typedef enum {
    INKTAG_NRF_GUI_ASSET_OK = 0,
    INKTAG_NRF_GUI_ASSET_INVALID,
    INKTAG_NRF_GUI_ASSET_IO_ERROR,
    INKTAG_NRF_GUI_ASSET_STALE_GENERATION,
    INKTAG_NRF_GUI_ASSET_BUSY,
} inktag_nrf_gui_asset_result_t;

bool inktag_nrf_gui_asset_available(void);
bool inktag_nrf_gui_asset_info(inktag_nrf_gui_asset_info_t *info);
bool inktag_nrf_gui_asset_transfer_active(void);
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_begin(
    const inktag_transfer_descriptor_t *descriptor);
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_append(
    uint32_t offset, const uint8_t *data, uint16_t length);
inktag_nrf_gui_asset_result_t inktag_nrf_gui_asset_commit(void);
void inktag_nrf_gui_asset_abort(void);

#endif
