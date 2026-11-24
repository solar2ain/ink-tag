#ifndef INKTAG_TLSR_IMAGE_FLASH_H
#define INKTAG_TLSR_IMAGE_FLASH_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_tlsr_flash_layout.h"

/* Physical binding only. The common fixed store owns record grammar, bitmap,
 * transactions and allocation. high_extent is supplied by the qualified board
 * profile; an accessible address window must never enable it by itself. */
#define INKTAG_TLSR_IMAGE_ERASE_BYTES 4096u
#define INKTAG_TLSR_IMAGE_PRIMARY_SECTORS \
    (INKTAG_IMAGE_STORE_PRIMARY_SIZE / INKTAG_TLSR_IMAGE_ERASE_BYTES)
#define INKTAG_TLSR_IMAGE_HIGH_SECTORS \
    (INKTAG_IMAGE_STORE_NEBULAR_HIGH_SIZE / INKTAG_TLSR_IMAGE_ERASE_BYTES)

static inline uint16_t inktag_tlsr_image_flash_capacity(uint16_t stride,
                                                      bool high_extent) {
    if (!stride) return 0u;
    return (uint16_t)(INKTAG_TLSR_IMAGE_PRIMARY_SECTORS / stride +
        (high_extent ? INKTAG_TLSR_IMAGE_HIGH_SECTORS / stride : 0u));
}

static inline uint32_t inktag_tlsr_image_flash_address(uint8_t id,
                                                     uint16_t stride,
                                                     bool high_extent) {
    uint16_t first;
    if (!stride || id >= inktag_tlsr_image_flash_capacity(stride, high_extent))
        return UINT32_MAX;
    first = (uint16_t)(INKTAG_TLSR_IMAGE_PRIMARY_SECTORS / stride);
    if (id < first)
        return INKTAG_IMAGE_STORE_PRIMARY_START +
            (uint32_t)id * stride * INKTAG_TLSR_IMAGE_ERASE_BYTES;
    return INKTAG_IMAGE_STORE_NEBULAR_HIGH_START +
        (uint32_t)(id - first) * stride * INKTAG_TLSR_IMAGE_ERASE_BYTES;
}

static inline bool inktag_tlsr_image_flash_span(uint32_t address,
                                               uint32_t length,
                                               bool high_extent) {
    /* Subtract only after checking the lower/upper bound; never rely on an
     * address+length sum that an invalid request could wrap. Zero-byte IO is
     * not a successful probe, and neither accepted extent includes Factory. */
    const uint32_t base_end = INKTAG_IMAGE_STORE_PRIMARY_START +
                             INKTAG_IMAGE_STORE_PRIMARY_SIZE;
    const uint32_t high_end = INKTAG_IMAGE_STORE_NEBULAR_HIGH_START +
                             INKTAG_IMAGE_STORE_NEBULAR_HIGH_SIZE;
    return length &&
        ((address >= INKTAG_IMAGE_STORE_PRIMARY_START && address < base_end &&
          length <= base_end - address) ||
         (high_extent && address >= INKTAG_IMAGE_STORE_NEBULAR_HIGH_START &&
          address < high_end && length <= high_end - address));
}

#endif
