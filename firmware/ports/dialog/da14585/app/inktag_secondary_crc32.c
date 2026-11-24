/*
 * Table-free IEEE CRC-32 for the DA14585 SPI secondary bootloader.
 *
 * The SDK implementation keeps a 1 KiB lookup table in the loader's high-RAM
 * execution region. Boot happens only once and reads external Flash far more
 * slowly than this bitwise loop, so the smaller implementation is the better
 * trade here. The application and Recovery continue to use their normal SDK
 * implementations.
 */

#include <stddef.h>
#include <stdint.h>

uint32_t crc32(uint32_t crc, const void *buffer, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)buffer;

    crc ^= UINT32_MAX;
    while (size-- != 0u) {
        uint8_t bit;

        crc ^= *cursor++;
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t reflected_polynomial =
                (0u - (crc & 1u)) & UINT32_C(0xEDB88320);
            crc = (crc >> 1) ^ reflected_polynomial;
        }
    }
    return crc ^ UINT32_MAX;
}
