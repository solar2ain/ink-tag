#include "inktag_ble_address.h"

#include <stddef.h>

static uint64_t fnv_prime_multiply(uint64_t value) {
#if defined(INKTAG_TELINK)
    /* 0x100000001b3 = 2^40 + 435. Keep the carry from the low word
     * using two 16-bit products, without a 64-bit multiply dependency. */
    uint32_t low = (uint32_t)value;
    uint32_t product = (low & 0xffffu) * 435u;
    uint32_t middle = (low >> 16) * 435u + (product >> 16);
    uint32_t high = (uint32_t)(value >> 32) * 435u +
        (middle >> 16) + (low << 8);
    return ((uint64_t)high << 32) | (middle << 16) | (product & 0xffffu);
#else
    /* Retain the measured implementation on the other native compilers. */
    return (value << 40) + (value << 8) + (value << 7) +
           (value << 5) + (value << 4) + (value << 1) + value;
#endif
}

bool inktag_ble_address_derive_uid_compact(uint16_t address_namespace,
                                         const uint8_t *uid, uint8_t uid_length,
                                         inktag_ble_address_t *output) {
    /* FNV-1a64 offset basis after "INKTAG-BLE-ADDR-FNV1" including its NUL. */
    uint64_t hash = UINT64_C(0x6f9668559e352aed);
    inktag_ble_address_t candidate;
    uint8_t any = 0u, every = 0xFFu;
    uint8_t i;
    if (output == NULL || uid == NULL || address_namespace == 0u ||
        uid_length == 0u || uid_length > INKTAG_BLE_ADDRESS_UID_MAX_BYTES)
        return false;
    hash = fnv_prime_multiply(hash ^ (uint8_t)(address_namespace >> 8));
    hash = fnv_prime_multiply(hash ^ (uint8_t)address_namespace);
    hash = fnv_prime_multiply(hash ^ uid_length);
    for (i = 0u; i < uid_length; ++i) {
        any |= uid[i];
        every &= uid[i];
        hash = fnv_prime_multiply(hash ^ uid[i]);
    }
    if (any == 0u || every == 0xFFu) return false;
    hash ^= hash >> 46;
    for (i = INKTAG_BLE_ADDRESS_BYTES; i != 0u; --i) {
        candidate.bytes[i - 1u] = (uint8_t)hash;
        hash >>= 8;
    }
    candidate.bytes[0] = (uint8_t)((candidate.bytes[0] & 0x3Fu) | 0xC0u);
    candidate.type = INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    if (!inktag_ble_address_valid(&candidate)) return false;
    *output = candidate;
    return true;
}
