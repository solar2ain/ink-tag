#include "inktag_nrf_ble_address.h"

#include <stddef.h>
#include "inktag_ble_address.h"
#include "nrf_error.h"

uint32_t inktag_nrf_ble_address_read(ble_gap_addr_t *output) {
    ble_gap_addr_t sdk;
    inktag_ble_address_t installed, selected;
    uint32_t result;
    if (output == NULL) return NRF_ERROR_INVALID_ADDR;
    result = sd_ble_gap_addr_get(&sdk);
    if (result != NRF_SUCCESS) return result;
    if (sdk.addr_type == BLE_GAP_ADDR_TYPE_PUBLIC)
        installed.type = INKTAG_BLE_ADDRESS_PUBLIC;
    else if (sdk.addr_type == BLE_GAP_ADDR_TYPE_RANDOM_STATIC)
        installed.type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    else return NRF_ERROR_INVALID_DATA;
    for (uint8_t i = 0u; i < INKTAG_BLE_ADDRESS_BYTES; ++i)
        installed.bytes[i] = sdk.addr[INKTAG_BLE_ADDRESS_BYTES - 1u - i];
    /* The SoftDevice's legacy/factory identity is already installed even
     * without an InkTag address record. Neither a read error nor bad value
     * permits rekeying. The unconsulted second source is unknown, not proven
     * absent. Do not consult it or derive/write an identity here. */
    if (inktag_ble_address_select(INKTAG_BLE_ADDRESS_PRESENT, &installed,
            INKTAG_BLE_ADDRESS_ERROR, NULL, &selected) !=
            INKTAG_BLE_ADDRESS_INSTALLED) return NRF_ERROR_INVALID_DATA;
    *output = sdk; /* Preserve all SDK bytes and its authoritative type. */
    return NRF_SUCCESS;
}
