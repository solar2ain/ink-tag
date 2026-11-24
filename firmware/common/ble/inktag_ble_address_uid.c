#include "inktag_ble_address.h"

#include <stddef.h>
#include <string.h>
#include "inktag_sha256.h"

bool inktag_ble_address_derive_uid(uint16_t address_namespace,
                                 const uint8_t *uid, uint8_t uid_length,
                                 inktag_ble_address_t *output) {
    static const uint8_t domain[] = "INKTAG-BLE-ADDR-V1";
    uint8_t input[sizeof(domain) + 3u + INKTAG_BLE_ADDRESS_UID_MAX_BYTES];
    uint8_t digest[32];
    inktag_ble_address_t candidate;
    uint8_t any = 0u, every = 0xFFu;
    uint8_t i;
    if (output == NULL || uid == NULL || address_namespace == 0u ||
        uid_length == 0u || uid_length > INKTAG_BLE_ADDRESS_UID_MAX_BYTES)
        return false;
    for (i = 0u; i < uid_length; ++i) {
        any |= uid[i];
        every &= uid[i];
    }
    if (any == 0u || every == 0xFFu) return false;
    memcpy(input, domain, sizeof(domain));
    input[sizeof(domain)] = (uint8_t)(address_namespace >> 8);
    input[sizeof(domain) + 1u] = (uint8_t)address_namespace;
    input[sizeof(domain) + 2u] = uid_length;
    memcpy(input + sizeof(domain) + 3u, uid, uid_length);
    inktag_sha256(input, sizeof(domain) + 3u + uid_length, digest);
    memcpy(candidate.bytes, digest, INKTAG_BLE_ADDRESS_BYTES);
    candidate.bytes[0] = (uint8_t)((candidate.bytes[0] & 0x3Fu) | 0xC0u);
    candidate.type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    if (!inktag_ble_address_valid(&candidate)) return false;
    *output = candidate;
    return true;
}
