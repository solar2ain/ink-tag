#ifndef INKTAG_OM_RECOVERY_PREFLIGHT_H
#define INKTAG_OM_RECOVERY_PREFLIGHT_H

#if defined(INKTAG_OM_FIXED_RECOVERY)
#include "omble.h"
#include "inktag_om_ota_policy.h"
#include "inktag_om_swap.h"
#include "inktag_firmware_signature.h"
#include "inktag_recovery_version.h"
#include "inktag_recovery_integrity_policy.h"

#define INKTAG_OM_PREFLIGHT_LE32(value) \
    (uint8_t)(value), (uint8_t)((value) >> 8), \
    (uint8_t)((value) >> 16), (uint8_t)((value) >> 24)
static const uint8_t s_om_recovery_preflight[] = {
    'I', 'K', 'R', 'P', 1u, 0u, 20u, 0u,
    INKTAG_OM_PREFLIGHT_LE32(INKTAG_OM_OTA_LAYOUT_SIGNATURE),
    INKTAG_OM_PREFLIGHT_LE32(INKTAG_OM_SLOT_SIZE -
                            INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES),
    INKTAG_OM_PREFLIGHT_LE32(INKTAG_OM_SLOT_SIZE),
    'I', 'K', 'S', 'P', 1u, INKTAG_RECOVERY_POLICY_CRC_ONLY,
};
#undef INKTAG_OM_PREFLIGHT_LE32
typedef char inktag_om_preflight_size_check[
    sizeof(s_om_recovery_preflight) == 26u ? 1 : -1];
typedef char inktag_om_preflight_profile_check[
#if defined(INKTAG_OM_NEXT_LAYOUT)
    INKTAG_OM_OTA_LAYOUT_SIGNATURE == 0x31565A4Fu &&
#else
    INKTAG_OM_OTA_LAYOUT_SIGNATURE == 0x32524D4Fu &&
#endif
    INKTAG_OM_OTA_BOARD_SIGNATURE == 0x42584D4Fu &&
    INKTAG_OM_OTA_VERIFIER_SIGNATURE == 0x314B4D4Fu &&
    INKTAG_OM_OTA_COMPATIBILITY_EPOCH == 1u &&
#if defined(INKTAG_OM_NEXT_LAYOUT)
    INKTAG_OM_SLOT_A == 0x2A000u && INKTAG_OM_SLOT_SIZE == 319488u &&
#else
    INKTAG_OM_SLOT_A == 0x2B000u && INKTAG_OM_SLOT_SIZE == 303104u &&
#endif
    INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES == 96u ? 1 : -1];

/* Software Revision is ASCII without the C string terminator. */
static const uint8_t s_om_recovery_software[] = INKTAG_RECOVERY_VERSION_STRING;
static bool inktag_om_recovery_software_read(uint16_t evt_id,
    const omble_evt_t *evt, uint16_t software_handle) {
    if (evt_id != OB_GATTS_EVT_READ_REQ || !evt ||
        evt->gatt.read_req.att_hdl != software_handle) return false;
    uint16_t offset = evt->gatt.read_req.offset;
    if (offset > sizeof(s_om_recovery_software) - 1u) {
        ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_INVALID_OFFSET,
                               NULL, 0u);
    } else {
        ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR,
            s_om_recovery_software + offset,
            (uint16_t)(sizeof(s_om_recovery_software) - 1u - offset));
    }
    return true;
}

/* Only SDK READ/handle/offset/response; no transfer initialization or probing. */
static bool inktag_om_recovery_preflight_read(uint16_t evt_id,
    const omble_evt_t *evt, uint16_t version_handle) {
    if (evt_id != OB_GATTS_EVT_READ_REQ || !evt ||
        evt->gatt.read_req.att_hdl != version_handle) return false;
    uint16_t offset = evt->gatt.read_req.offset;
    if (offset > sizeof(s_om_recovery_preflight)) {
        ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_INVALID_OFFSET,
                               NULL, 0u);
    } else {
        ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR,
            s_om_recovery_preflight + offset,
            (uint16_t)(sizeof(s_om_recovery_preflight) - offset));
    }
    return true;
}
#endif
#endif
