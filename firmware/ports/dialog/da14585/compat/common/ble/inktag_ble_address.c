#include "inktag_ble_address.h"

#include <stddef.h>

bool inktag_ble_address_valid(const inktag_ble_address_t *address) {
    uint8_t any = 0u;
    uint8_t any_clear = 0u;
    uint8_t i;
    if (address == NULL ||
        (address->type != INKTAG_BLE_ADDRESS_PUBLIC &&
         address->type != INKTAG_BLE_ADDRESS_STATIC_RANDOM)) return false;
    if (address->type == INKTAG_BLE_ADDRESS_STATIC_RANDOM &&
        (address->bytes[0] & 0xC0u) != 0xC0u) return false;
    for (i = 0u; i < INKTAG_BLE_ADDRESS_BYTES; ++i) {
        uint8_t mask = i == 0u &&
            address->type == INKTAG_BLE_ADDRESS_STATIC_RANDOM ? 0x3Fu : 0xFFu;
        any |= address->bytes[i] & mask;
        any_clear |= (uint8_t)~address->bytes[i] & mask;
    }
    return any != 0u && any_clear != 0u;
}

inktag_ble_address_result_t inktag_ble_address_select(
    inktag_ble_address_state_t installed_state,
    const inktag_ble_address_t *installed,
    inktag_ble_address_state_t factory_state,
    const inktag_ble_address_t *factory,
    inktag_ble_address_t *output) {
    if (output == NULL) return INKTAG_BLE_ADDRESS_REFUSED;
    if (installed_state == INKTAG_BLE_ADDRESS_PRESENT) {
        if (!inktag_ble_address_valid(installed))
            return INKTAG_BLE_ADDRESS_REFUSED;
        *output = *installed;
        return INKTAG_BLE_ADDRESS_INSTALLED;
    }
    if (installed_state != INKTAG_BLE_ADDRESS_ABSENT)
        return INKTAG_BLE_ADDRESS_REFUSED;
    if (factory_state == INKTAG_BLE_ADDRESS_PRESENT) {
        if (!inktag_ble_address_valid(factory))
            return INKTAG_BLE_ADDRESS_REFUSED;
        *output = *factory;
        return INKTAG_BLE_ADDRESS_FACTORY;
    }
    return factory_state == INKTAG_BLE_ADDRESS_ABSENT
        ? INKTAG_BLE_ADDRESS_MISSING : INKTAG_BLE_ADDRESS_REFUSED;
}
