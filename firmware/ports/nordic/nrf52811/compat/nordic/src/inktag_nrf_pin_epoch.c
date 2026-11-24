#include "inktag_nrf_pin_epoch.h"
#include "inktag_protocol_core.h"
#include "inktag_nrf_dfu_epoch.h"

bool inktag_nrf_pin_epoch_settings_ready(const void *settings, size_t length) {
    const uint8_t *p=settings;
    if (!p || length<92u) return false;
    uint32_t bytes=inktag_read_u32_le(p+24u);
    return inktag_read_u32_le(p+4u)==2u &&
        inktag_nrf_dfu_compatibility_epoch(inktag_read_u32_le(p+8u))>=1u &&
        (inktag_read_u32_le(p+12u) | inktag_read_u32_le(p+20u) |
         inktag_read_u32_le(p+44u))==INKTAG_NRF_DFU_NO_PENDING_VERSION &&
        bytes && bytes<=0x11000u &&
        inktag_read_u32_le(p+32u)==1u && /* valid Bank0 App */
        inktag_read_u32_le(p)==~inktag_crc32_update(UINT32_MAX,p+4u,88u);
}
