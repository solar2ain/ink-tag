#ifndef INKTAG_BLE_IDENTITY_H
#define INKTAG_BLE_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/* Normal application discovery contract:
 *   primary ADV = Flags + complete local name + 4-byte InkTag marker
 *   scan response = complete 128-bit InkTag service UUID
 *
 * Nineteen bytes deliberately preserve the published settings ABI and leave
 * one byte of headroom in a legacy 31-byte primary advertisement:
 * 3 (Flags AD) + 2 + 19 (name AD) + 6 (manufacturer AD) = 30 bytes. */
#define INKTAG_BLE_DEVICE_NAME_PREFIX "IKT_"
#define INKTAG_BLE_DEVICE_NAME_PREFIX_BYTES 4u
#define INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX "InkTag_"
#define INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX_BYTES 7u
#define INKTAG_BLE_DEVICE_NAME_MAX_BYTES 19u
#define INKTAG_BLE_LEGACY_ADV_MAX_BYTES 31u
#define INKTAG_BLE_FLAGS_AD_BYTES 3u
#define INKTAG_BLE_COMPLETE_NAME_AD_OVERHEAD 2u
#define INKTAG_BLE_MANUFACTURER_MARKER_AD_BYTES 6u
#define INKTAG_BLE_SERVICE_UUID128_AD_BYTES 18u

#if (INKTAG_BLE_FLAGS_AD_BYTES + INKTAG_BLE_COMPLETE_NAME_AD_OVERHEAD +  INKTAG_BLE_DEVICE_NAME_MAX_BYTES + INKTAG_BLE_MANUFACTURER_MARKER_AD_BYTES)  > INKTAG_BLE_LEGACY_ADV_MAX_BYTES
#error "InkTag normal discovery payload exceeds the legacy advertising limit"
#endif

/* Persisted-record/discovery compatibility keeps the small safety boundary:
 * exact product prefix,
 * non-empty suffix, no embedded control byte and the byte limit.
 * Historical names may contain high bytes. New writes must additionally use
 * inktag_ble_device_name_write_valid below; do not tighten record reads. */
static inline bool inktag_ble_device_name_valid(const uint8_t *name,
                                                 uint8_t length) {
    uint8_t i = 0u;
    uint8_t prefix_bytes = 0u;
    bool primary = true;
    if (name == 0 || length <= INKTAG_BLE_DEVICE_NAME_PREFIX_BYTES ||
        length > INKTAG_BLE_DEVICE_NAME_MAX_BYTES) return false;
    while (i < INKTAG_BLE_DEVICE_NAME_PREFIX_BYTES) {
        if (name[i] != (uint8_t)INKTAG_BLE_DEVICE_NAME_PREFIX[i]) {
            primary = false;
            break;
        }
        ++i;
    }
    if (primary) {
        prefix_bytes = INKTAG_BLE_DEVICE_NAME_PREFIX_BYTES;
    } else {
        i = 0u;
        if (length <= INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX_BYTES) return false;
        while (i < INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX_BYTES) {
            if (name[i] != (uint8_t)INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX[i])
                return false;
            ++i;
        }
        prefix_bytes = INKTAG_BLE_DEVICE_NAME_LEGACY_PREFIX_BYTES;
    }
    if (length <= prefix_bytes) return false;
    i = prefix_bytes;
    while (i < length) {
        uint8_t byte = name[i++];
        if (byte < 0x20u || byte == 0x7Fu) return false;
    }
    return true;
}

/* New ordinary GAP rename only; retained prefix/length gates still apply.
 * Default/factory advertising names are not rewritten by this function. */
static inline bool inktag_ble_device_name_write_valid(const uint8_t *name,
                                                       uint8_t length) {
    uint8_t i;
    if (!inktag_ble_device_name_valid(name, length)) return false;
    for (i = 0u; i < length; ++i) {
        if (name[i] > 0x7Eu) return false;
    }
    return true;
}

/* Optional shared implementation; the inline validator above is authoritative. */
bool inktag_ble_device_name_valid_shared(const uint8_t *name, uint8_t length);

#endif
