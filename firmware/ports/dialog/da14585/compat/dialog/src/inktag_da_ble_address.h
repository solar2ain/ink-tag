#ifndef INKTAG_DA_BLE_ADDRESS_H
#define INKTAG_DA_BLE_ADDRESS_H

#include <stddef.h>
#include "inktag_ble_address.h"

/* SDK6 boot copies OTP into dev_bdaddr, LSB first, then PUBLIC GAP setup
 * reads it through NVDS. No separate installed address record exists in this
 * port. Never classify a void OTP read, invalid bytes or an SDK sample as
 * confirmed ABSENT: UID provisioning is deliberately unavailable here.
 * A valid legacy OTP public identity is preserved, not rewritten on OTA.
 * Output is unchanged on refusal; input/output buffers must not overlap. */
static inline bool inktag_da_ble_factory_address(
    const uint8_t otp_le[INKTAG_BLE_ADDRESS_BYTES],
    const uint8_t active_le[INKTAG_BLE_ADDRESS_BYTES],
    uint8_t active_type, inktag_ble_address_t *output)
{
    inktag_ble_address_t factory;
    uint8_t i;
    if (otp_le == NULL || active_le == NULL || output == NULL ||
        active_type != INKTAG_BLE_ADDRESS_PUBLIC) return false;

    /* Both role-specific vendor defaults are syntax-valid public addresses,
     * but neither is proof of a device identity, even if copied into OTP. */
    if ((otp_le[0] == 0x0Au || otp_le[0] == 0x0Bu) &&
        otp_le[1] == 0x00u && otp_le[2] == 0xF4u &&
        otp_le[3] == 0x35u && otp_le[4] == 0x23u &&
        otp_le[5] == 0x48u) return false;

    factory.type = INKTAG_BLE_ADDRESS_PUBLIC;
    for (i = 0u; i < INKTAG_BLE_ADDRESS_BYTES; ++i) {
        if (otp_le[i] != active_le[i]) return false;
        factory.bytes[INKTAG_BLE_ADDRESS_BYTES - 1u - i] = otp_le[i];
    }
    /* ABSENT here refers only to the structurally nonexistent port record,
     * never to factory OTP. The common selector rejects zero/FF as invalid. */
    return inktag_ble_address_select(INKTAG_BLE_ADDRESS_ABSENT, NULL,
        INKTAG_BLE_ADDRESS_PRESENT, &factory, output) ==
        INKTAG_BLE_ADDRESS_FACTORY;
}

#endif
