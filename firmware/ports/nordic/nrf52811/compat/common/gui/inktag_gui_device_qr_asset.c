#define INKTAG_DEVICE_INFO_QR_ASSET_DEFINITION 1
#include "inktag_gui_device_qr.h"

/* Keep the array alive until the asset linker script collects its
 * data section; the anchor itself is discarded from the raw payload. */
const uint8_t* const inktag_device_info_qr_asset_anchor =
    inktag_default_device_qr;
