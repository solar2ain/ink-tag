#include "inktag_ble_address.h"

#include <stddef.h>
#include <string.h>
#include "inktag_protocol_core.h"

bool inktag_ble_address_record_encode(const inktag_ble_address_t *address,
                                     uint8_t output[INKTAG_BLE_ADDRESS_RECORD_BYTES]) {
    uint32_t crc;
    uint8_t i;
    if (output == NULL || !inktag_ble_address_valid(address)) return false;
    output[0] = (uint8_t)(0x10u | address->type);
    memcpy(output + 1u, address->bytes, INKTAG_BLE_ADDRESS_BYTES);
    crc = inktag_crc32_update(0xFFFFFFFFu, output, 7u) ^ 0xFFFFFFFFu;
    for (i = 0u; i < 4u; ++i) output[7u + i] = (uint8_t)(crc >> (i * 8u));
    return true;
}

bool inktag_ble_address_record_decode(const uint8_t *record, uint16_t length,
                                     inktag_ble_address_t *output) {
    inktag_ble_address_t candidate;
    uint32_t crc;
    uint8_t i;
    if (record == NULL || output == NULL ||
        length != INKTAG_BLE_ADDRESS_RECORD_BYTES ||
        (record[0] != 0x10u && record[0] != 0x11u)) return false;
    crc = inktag_crc32_update(0xFFFFFFFFu, record, 7u) ^ 0xFFFFFFFFu;
    for (i = 0u; i < 4u; ++i)
        if (record[7u + i] != (uint8_t)(crc >> (i * 8u))) return false;
    candidate.type = record[0] & 1u;
    memcpy(candidate.bytes, record + 1u, INKTAG_BLE_ADDRESS_BYTES);
    if (!inktag_ble_address_valid(&candidate)) return false;
    *output = candidate;
    return true;
}
