#include "inktag_om_address.h"
#include "nvds.h"
#include "omble_gap.h"
#include "om_driver.h"
#include "inktag_om_voltage.h"
#include <string.h>

bool inktag_om_address_from_sdk(const uint8_t lsb[6], uint8_t sdk_type,
                                inktag_ble_address_t *output) {
    inktag_ble_address_t address;
    if (!lsb || !output) return false;
    if (sdk_type == OB_ADV_ADDR_TYPE_PUBLIC) address.type = INKTAG_BLE_ADDRESS_PUBLIC;
    else if (sdk_type == OB_ADV_ADDR_TYPE_RANDOM) address.type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    else return false;
    for (uint8_t i = 0; i < 6u; ++i) address.bytes[i] = lsb[5u-i];
    if (!inktag_ble_address_valid(&address)) return false;
    *output = address;
    return true;
}

bool inktag_om_address_to_sdk(const inktag_ble_address_t *address,
                              uint8_t lsb[6], uint8_t *sdk_type) {
    if (!lsb || !sdk_type || !inktag_ble_address_valid(address)) return false;
    uint8_t type = address->type == INKTAG_BLE_ADDRESS_PUBLIC ?
        OB_ADV_ADDR_TYPE_PUBLIC : OB_ADV_ADDR_TYPE_RANDOM;
    for (uint8_t i = 0; i < 6u; ++i) lsb[i] = address->bytes[5u-i];
    *sdk_type = type;
    return true;
}

__attribute__((used,section(".inktag_identity_init")))
const uint8_t inktag_om_identity_init_policy[8] = {'I','K','I','D',1u,1u,0u,0u};

/* The SDK's 32-bit UID convenience function XOR-folds the UID and can
 * fall back to JEDEC. Retain the full manufacturer UID in both consumers. */
#if defined(INKTAG_OM_FIXED_RECOVERY)
static
#endif
uint8_t inktag_om_flash_uid(uint8_t uid[19]) {
    uint32_t id = drv_sf_read_id(OM_SF, 0u);
    if (id != drv_sf_read_id(OM_SF, 0u)) return false;
    uint8_t mid = (uint8_t)(id >> 16u), length;
    if (mid == 0x85u || mid == 0xEBu || mid == 0xC8u) length = 16u;
    else if (mid == 0xE0u || mid == 0x68u) length = 8u;
    else return false;
    uint8_t check[16], any = 0u, every = 0xFFu;
    uid[0] = (uint8_t)(id >> 16u); uid[1] = (uint8_t)(id >> 8u); uid[2] = (uint8_t)id;
    drv_sf_read_uid_ex(OM_SF, 0u, uid + 3u, length);
    drv_sf_read_uid_ex(OM_SF, 0u, check, length);
    for (uint8_t i = 0u; i < length; ++i) {any |= uid[3u+i]; every &= uid[3u+i];}
    if (!any || every == 0xFFu || memcmp(uid + 3u, check, length)) return 0u;
    return (uint8_t)(length + 3u);
}

static bool initial_uid_address(uint8_t address[6]) {
    uint8_t uid[19], type;
    inktag_ble_address_t derived;
    const uint8_t length = inktag_om_flash_uid(uid);
    return length && inktag_ble_address_derive_uid_compact(INKTAG_BLE_ADDRESS_NS_OM6626,
        uid, length, &derived) &&
        inktag_om_address_to_sdk(&derived, address, &type);
}

uint8_t inktag_om_address_load(uint8_t nvds_init_status,
                              uint8_t lsb[6], uint8_t *sdk_type) {
    uint8_t stored[6];
    nvds_tag_len_t length = sizeof(stored);
    inktag_ble_address_t installed;
    uint8_t source = INKTAG_OM_ADDRESS_NVDS;
    if (!lsb || !sdk_type || nvds_init_status != NVDS_OK)
        return INKTAG_OM_ADDRESS_INIT_ERROR;
    uint8_t status = nvds_get(NVDS_TAG_BD_ADDRESS, &length, stored);
    if (status == NVDS_OK && length == sizeof(stored)) {
        stored[5] = (uint8_t)((stored[5] & 0x3Fu) | 0xC0u);
        if (inktag_om_address_from_sdk(stored, OB_ADV_ADDR_TYPE_RANDOM, &installed))
            goto ready;
    }
    /* Explicit clean initialization leaves tag1 absent. Both roles share this
     * SDK path, including voltage admission and persistent write/readback. */
    {
        bool generated = false;
        if (status == NVDS_TAG_NOT_DEFINED) generated = initial_uid_address(stored);
        for (uint8_t attempt = 0u; attempt < 4u; ++attempt) {
            if (!generated) {
                uint32_t a = drv_rng_get(), b = drv_rng_get();
                memcpy(stored, &a, 4u); memcpy(stored + 4u, &b, 2u);
            }
            stored[5] = (uint8_t)((stored[5] & 0x3Fu) | 0xC0u);
            if (inktag_om_address_from_sdk(stored, OB_ADV_ADDR_TYPE_RANDOM, &installed)) {
                generated = true; break;
            }
            generated = false;
        }
        if (!generated) return INKTAG_OM_ADDRESS_ROOT_ERROR;
        if (!inktag_om_voltage_operation_allowed(INKTAG_POWER_OPERATION_FLASH) ||
            nvds_put(NVDS_TAG_BD_ADDRESS, sizeof(stored), stored) != NVDS_OK)
            return INKTAG_OM_ADDRESS_WRITE_ERROR;
        uint8_t check[6]; length = sizeof(check);
        if (nvds_get(NVDS_TAG_BD_ADDRESS, &length, check) != NVDS_OK ||
            length != sizeof(check) || memcmp(check, stored, sizeof(check)))
            return INKTAG_OM_ADDRESS_WRITE_ERROR;
        source = INKTAG_OM_ADDRESS_GENERATED;
    }
ready:
    memcpy(lsb, stored, sizeof(stored));
    *sdk_type = OB_ADV_ADDR_TYPE_RANDOM;
    return source;
}
