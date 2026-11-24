#include "image_verify.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

bool inktag_tlsr_boot_ram_prefix(const uint8_t h[32], uint32_t capacity,
                                uint32_t *prefix) {
    uint32_t size = ((uint32_t)h[12] | (uint32_t)h[13]<<8)*16u;
    /* Do not clamp a corrupt header as the vendor sample does. The complete
     * initial load image includes vectors, RAM code and retained data. */
    if (capacity < 36u || le32(h+8) != 0x544C4E4Bu || h[14] != 0x88u || h[15] != 0u ||
        size < 32u || size > 0x8000u || size > capacity ||
        le32(h+24) < size || le32(h+24) > capacity - 4u) return false;
    *prefix = size;
    return true;
}

