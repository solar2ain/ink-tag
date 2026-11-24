#ifndef INKTAG_BOARD_RECORD_H
#define INKTAG_BOARD_RECORD_H

#include <stdbool.h>
#include <stdint.h>

/* Bounded immutable-selector envelope; family/board interpretation is owned
 * by the port and exact board. This is corruption detection, not authority. */
#define INKTAG_BOARD_RECORD_SIZE 12u

static inline uint32_t inktag_board_record_checksum(const uint8_t *bytes) {
    uint32_t hash = 2166136261u;
    for (uint8_t i = 0u; i < 8u; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline void inktag_board_record_encode(uint8_t family, uint8_t board,
                                              uint8_t record[12]) {
    const uint8_t prefix[8] = {'I', 'B', 'D', '1', 1u, family, board, 1u};
    for (uint8_t i = 0u; i < 8u; ++i) record[i] = prefix[i];
    uint32_t hash = inktag_board_record_checksum(record);
    for (uint8_t i = 0u; i < 4u; ++i) record[8u + i] = (uint8_t)(hash >> (8u * i));
}

static inline bool inktag_board_record_decode(const uint8_t *record,
                                              uint16_t length, uint8_t family,
                                              uint8_t *board) {
    if (record == 0 || board == 0 || length != INKTAG_BOARD_RECORD_SIZE ||
        record[0] != 'I' || record[1] != 'B' || record[2] != 'D' ||
        record[3] != '1' || record[4] != 1u || record[5] != family ||
        record[6] == 0u || record[7] != 1u) return false;
    uint32_t hash = inktag_board_record_checksum(record);
    for (uint8_t i = 0u; i < 4u; ++i)
        if (record[8u + i] != (uint8_t)(hash >> (8u * i))) return false;
    *board = record[6];
    return true;
}

#endif
