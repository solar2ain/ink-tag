#include "inktag_gr_ble_address.h"

#include <stddef.h>
#include <string.h>
#include "ble.h"
#include "grx_sys.h"
#include "inktag_gr_nvds_guard.h"
#if defined(BOOTLOADER_ENABLE)
#include "grx_hal.h"
#include "inktag_gr_security_random.inc"
#else
extern bool inktag_gr_security_random(void *, uint8_t *, uint16_t);
#endif
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_nvds.h"
#elif defined(INKTAG_GOODIX_GR5331)
#include "gr533x_nvds.h"
#else
#error "Exact Goodix SDK required"
#endif

/* Host clean-BIN admission binds both actual App and Common Recovery bytes. */
static const uint8_t inktag_gr_clean_identity_info[8]
    __attribute__((used, retain)) = {'I','K','G','A',1,0x12,0x52,0};
#if defined(BOOTLOADER_ENABLE)
/* Reuse Common Recovery's existing fresh FLASH-floor sample. Its ordinary
 * DFU role guard is false in App, whose HAL already uses the health gate. */
extern bool inktag_dfu_programming_allowed(void);
#endif

static bool usable(const inktag_ble_address_t *address) {
    static const uint8_t sample[] = {0xea,0xcb,0x3e,0xcf,0x00,0x0f};
    return inktag_ble_address_valid(address) &&
           memcmp(address->bytes, sample, sizeof(sample)) != 0;
}

static bool read_uid(uint8_t uid[SYS_CHIP_UID_LEN]) {
    uint8_t again[SYS_CHIP_UID_LEN], any = 0u, every = 0xffu;
    if (sys_device_uid_get(uid) != 0 || sys_device_uid_get(again) != 0 ||
        memcmp(uid, again, sizeof(again))) return false;
    for (unsigned i = 0u; i < sizeof(again); ++i) { any |= uid[i]; every &= uid[i]; }
    return any != 0u && every != 0xffu;
}

/* Compatibility check only: this fold never creates a new identity. */
static bool legacy_sdk_matches(const inktag_ble_address_t *address,
                               const uint8_t uid[SYS_CHIP_UID_LEN]) {
    inktag_ble_address_t old = {.type=INKTAG_BLE_ADDRESS_STATIC_RANDOM};
    if (SYS_CHIP_UID_LEN != 16u || address->type != old.type) return false;
    for (unsigned i = 0u; i < 6u; ++i)
        old.bytes[5u-i] = uid[i] ^ uid[i+6u] ^ uid[(i+11u)%16u];
#if defined(INKTAG_GOODIX_GR5331)
    old.bytes[5] ^= 4u;
#else
    old.bytes[5] ^= 3u;
#endif
    old.bytes[0] = (old.bytes[0] & 0x3fu) | 0xc0u;
    return memcmp(address->bytes, old.bytes, sizeof(old.bytes)) == 0;
}

static bool sdk_decode(const ble_gap_bdaddr_t *sdk, inktag_ble_address_t *output) {
    inktag_ble_address_t candidate;
    if (sdk->addr_type == BLE_GAP_ADDR_TYPE_PUBLIC)
        candidate.type = INKTAG_BLE_ADDRESS_PUBLIC;
    else if (sdk->addr_type == BLE_GAP_ADDR_TYPE_RANDOM_STATIC)
        candidate.type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    else return false;
    for (unsigned i = 0u; i < 6u; ++i) candidate.bytes[i] = sdk->gap_addr.addr[5u-i];
    if (!inktag_ble_address_valid(&candidate)) return false;
    *output = candidate;
    return true;
}

static bool initial_address(inktag_ble_address_t *address) {
    ble_gap_bdaddr_t sdk;
    inktag_ble_address_t current;
    uint8_t uid[SYS_CHIP_UID_LEN];
    if (ble_gap_addr_get(&sdk) != BLE_SUCCESS) return false;
    bool current_valid = sdk_decode(&sdk, &current) && usable(&current);
    /* A non-sample public SDK identity is the existing ordinary public
     * identity (factory or vendor NVDS), not the SDK's temporary random one. */
    if (current_valid && current.type == INKTAG_BLE_ADDRESS_PUBLIC) {
        *address = current;
        return true;
    }
    bool uid_valid = read_uid(uid);
    if (current_valid && uid_valid && legacy_sdk_matches(&current, uid)) {
        *address = current;
        return true;
    }
    /* SDK startup's public-address backend loads C001 or real efuse factory
     * bytes. A temporary/random live identity is not a usable factory source.
     * PUBLIC setters have unchecked NVDS + asynchronous controller effects;
     * do not add such a setter or block initialization on a mismatching efuse
     * value. Continue to complete UID, then the existing hardware RNG. */
    if (uid_valid) {
#if defined(INKTAG_GOODIX_GR5331)
        const uint16_t ns = INKTAG_BLE_ADDRESS_NS_GR5331;
#else
        const uint16_t ns = INKTAG_BLE_ADDRESS_NS_GR551X;
#endif
        return inktag_ble_address_derive_uid_compact(ns, uid, sizeof(uid), address) &&
               usable(address);
    }
    if (!inktag_gr_security_random(NULL, address->bytes, sizeof(address->bytes)))
        return false;
    address->bytes[0] = (address->bytes[0] & 0x3fu) | 0xc0u;
    address->type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    return usable(address);
}

bool inktag_gr_ble_address_init(void) {
    uint8_t raw[INKTAG_BLE_ADDRESS_RECORD_BYTES], again[sizeof(raw)];
    uint16_t length = sizeof(raw);
    inktag_ble_address_t address;
    if (!inktag_gr_nvds_ready()) return false;
    uint8_t status = nvds_get(INKTAG_GR_BLE_ADDRESS_NVDS_TAG, &length, raw);
    if (status == NVDS_SUCCESS &&
        inktag_ble_address_record_decode(raw, length, &address) && usable(&address))
        return inktag_gr_ble_address_apply(&address);
    /* Missing/unsupported/corrupt optional identity is initialized once. A
     * real storage-access failure is still a failure, never an erase/retry. */
    if (status != NVDS_SUCCESS && status != NVDS_TAG_NOT_EXISTED &&
        status != NVDS_LENGTH_OUT_OF_RANGE) return false;
    if (!initial_address(&address)) return false;
#if defined(BOOTLOADER_ENABLE)
    if (!inktag_dfu_programming_allowed()) return false;
#endif
    if (!inktag_ble_address_record_encode(&address, raw) ||
        nvds_put(INKTAG_GR_BLE_ADDRESS_NVDS_TAG, sizeof(raw), raw) != NVDS_SUCCESS)
        return false;
    length = sizeof(again);
    if (nvds_get(INKTAG_GR_BLE_ADDRESS_NVDS_TAG, &length, again) != NVDS_SUCCESS ||
        length != sizeof(raw) || memcmp(raw, again, sizeof(raw))) return false;
    /* App and Common Recovery consume this same verified durable record. */
    return inktag_gr_ble_address_apply(&address);
}

bool inktag_gr_ble_address_read_current(inktag_ble_address_t *output) {
    ble_gap_bdaddr_t sdk;
    return output != NULL && ble_gap_addr_get(&sdk) == BLE_SUCCESS &&
           sdk_decode(&sdk, output);
}

bool inktag_gr_ble_address_apply(const inktag_ble_address_t *address) {
    inktag_ble_address_t current;
    ble_gap_bdaddr_t sdk = {0}, live;
    if (!inktag_ble_address_valid(address) ||
        ble_gap_addr_get(&live) != BLE_SUCCESS) return false;
    sdk.addr_type = address->type == INKTAG_BLE_ADDRESS_PUBLIC ?
        BLE_GAP_ADDR_TYPE_PUBLIC : BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    for (unsigned i = 0u; i < INKTAG_BLE_ADDRESS_BYTES; ++i)
        sdk.gap_addr.addr[i] = address->bytes[INKTAG_BLE_ADDRESS_BYTES - 1u - i];
    if (sdk.addr_type == live.addr_type &&
        memcmp(sdk.gap_addr.addr, live.gap_addr.addr, INKTAG_BLE_ADDRESS_BYTES) == 0)
        return true;
    if (address->type != INKTAG_BLE_ADDRESS_STATIC_RANDOM) return false;
    if (ble_gap_addr_set(&sdk) != BLE_SUCCESS ||
        !inktag_gr_ble_address_read_current(&current)) return false;
    return current.type == address->type &&
        memcmp(address->bytes, current.bytes, INKTAG_BLE_ADDRESS_BYTES) == 0;
}
