#include "inktag_gr_app_integrity.h"
#include "inktag_code_guard.h"

#include <stddef.h>

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t update(uint32_t crc, const uint8_t *data, uint32_t length) {
    while (length--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

INKTAG_GUARD_A bool inktag_gr_app_integrity_verify(inktag_gr_integrity_read_t read,
    uint32_t app_base, uint32_t native_bytes, uint32_t payload_limit, uint32_t native_checksum,
    uint32_t board_context, uint32_t layout_context, uint32_t epoch) {
    uint8_t record[INKTAG_GR_APP_INTEGRITY_BYTES];
    uint8_t block[128];
    uint32_t crc = UINT32_MAX;
    uint32_t offset = 0u;
    uint32_t body;
    uint32_t sum = 0u;

    if (read == NULL || native_bytes <= sizeof(record) ||
        native_bytes > payload_limit || app_base > UINT32_MAX - native_bytes)
        return false;

    body = native_bytes - sizeof(record);
    /* A single exact native extent fixes the location. Never scan arbitrary
     * Flash for magic or derive an extent from an unchecked record. */
    if (!read(app_base + body, record, sizeof(record)) ||
        word(record) != 0x49434b49u || record[4] != 1u ||
        record[5] != sizeof(record) || record[6] != 0u || record[7] != 0u ||
        word(record + 8) != body || word(record + 16) != board_context ||
        word(record + 20) != layout_context || word(record + 24) != epoch ||
        word(record + 28) == 0u || word(record + 32) != 0u ||
        (update(UINT32_MAX, record, 36u) ^ UINT32_MAX) != word(record + 36))
        return false;

    for (unsigned i = 0; i < sizeof(record); ++i) sum += record[i];

    while (offset < body) {
        uint32_t length = body - offset;
        if (length > sizeof(block)) length = sizeof(block);
        if (!read(app_base + offset, block, length)) return false;
        for (unsigned i = 0; i < length; ++i) sum += block[i];
        crc = update(crc, block, length);
        offset += length;
    }
    /* Compare against the persisted expected CRC, never a freshly generated
     * expected value. Release version is metadata, not an anti-rollback floor. */
    return (crc ^ UINT32_MAX) == word(record + 12) && sum == native_checksum;
}
