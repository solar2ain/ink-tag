#include <stdbool.h>

#include "nrf_dfu_ble_svci_bond_sharing.h"
#include "nrf_error.h"
#include "nrf.h"

#define INKTAG_NRF_DFU_NAME_LENGTH 16u

static uint8_t hex_digit(uint8_t value) {
    value &= 0x0Fu;
    if (value > 9u) value += 7u;
    return (uint8_t)('0' + value);
}

bool nrf_dfu_settings_adv_name_is_valid(void) {
    /* Recovery owns its name.  Never trust a partially written settings-page
     * handoff or a Pro application alias as the stable device identity. */
    return true;
}

ret_code_t nrf_dfu_settings_adv_name_copy(nrf_dfu_adv_name_t *name) {
    uint32_t *words = (uint32_t *)name;
    uint16_t id4;
    uint8_t digit0;
    uint8_t digit1;
    uint8_t digit2;
    uint8_t digit3;

    /* Nordic's DFU transport always passes its static m_adv_name object here. */
    id4 = (uint16_t)NRF_FICR->DEVICEADDR[0];
    digit0 = hex_digit((uint8_t)(id4 >> 12));
    digit1 = hex_digit((uint8_t)(id4 >> 8));
    digit2 = hex_digit((uint8_t)(id4 >> 4));
    digit3 = hex_digit((uint8_t)id4);

    /* nrf_dfu_adv_name_t is word-aligned. Packing the fixed text into its
     * seven ABI words keeps the 16 KiB nRF52811 Recovery within bounds. */
    /* m_adv_name is zero-initialized BSS; its CRC field is not consumed after
     * this provider returns, so do not spend code bytes storing the same zero. */
    words[1] = 0x5F544B49u; /* IKT_ */
    words[2] = 0x5F46524Eu; /* NRF_ */
    words[3] = (uint32_t)digit0 | ((uint32_t)digit1 << 8) |
               ((uint32_t)digit2 << 16) | ((uint32_t)digit3 << 24);
    words[4] = 0x41544F5Fu; /* _OTA */
    words[5] = 0u;
    words[6] = INKTAG_NRF_DFU_NAME_LENGTH;
    return NRF_SUCCESS;
}
