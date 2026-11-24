#ifndef INKTAG_SPI_NOR_SFDP_H
#define INKTAG_SPI_NOR_SFDP_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Small standard-SPI subset, not a vendor/device-ID catalogue. The mandatory
 * first BFPT describes density, address width and uniform 4 KiB erase. Read
 * only the first 11 DWORDs; no quad/status/OTP configuration is changed. */
#define INKTAG_SPI_NOR_BFPT_MAX 44u
static inline uint32_t inktag_spi_nor_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint8_t inktag_spi_nor_bfpt_location(
    const uint8_t header[16], uint32_t *address) {
    if (memcmp(header, "SFDP", 4) || header[5] != 1u ||
        header[8] != 0u || header[10] != 1u || header[15] != 0xffu ||
        header[11] < 9u) return 0;
    *address = (uint32_t)header[12] | (uint32_t)header[13] << 8 |
               (uint32_t)header[14] << 16;
    if (*address < 16u || *address > 0xffffffu - INKTAG_SPI_NOR_BFPT_MAX)
        return 0;
    return header[11] < 11u ? (uint8_t)(header[11] * 4u) :
                             INKTAG_SPI_NOR_BFPT_MAX;
}
static inline bool inktag_spi_nor_bfpt_geometry(
    const uint8_t *table, uint8_t length, uint32_t minimum_bytes,
    uint32_t *capacity, uint16_t *program_chunk) {
    if (length < 36u) return false;
    uint32_t first = inktag_spi_nor_le32(table);
    /* DWORD1: uniform 4 KiB erase, opcode20h, three-byte addressing usable. */
    if ((first & 3u) != 1u || ((first >> 8) & 0xffu) != 0x20u ||
        ((first >> 17) & 3u) > 1u) return false;
    uint32_t density = inktag_spi_nor_le32(table + 4);
    uint32_t bytes;
    if (density & 0x80000000u) {
        uint32_t exponent = density & 0x7fffffffu;
        if (exponent < 3u || exponent > 27u) return false;
        bytes = UINT32_C(1) << (exponent - 3u);
    } else {
        if ((density & 7u) != 7u) return false;
        bytes = (density >> 3) + 1u;
    }
    if (bytes < minimum_bytes || bytes > 0x1000000u) return false;
    /* Original JESD216 has the conventional256-byte program buffer; revA
     * supplies a page exponent in DWORD11. Smaller chunks also safely fit a
     * larger buffer, without changing its configuration register. */
    uint16_t chunk = 256u;
    if (length >= 44u) {
        uint8_t exponent = (uint8_t)((table[40] >> 4) & 15u);
        if (exponent < 8u) chunk = (uint16_t)(1u << exponent);
    }
    *capacity = bytes; *program_chunk = chunk;
    return true;
}
#endif
