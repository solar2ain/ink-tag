#ifndef INKTAG_TLSR_BOOT_IMAGE_VERIFY_H
#define INKTAG_TLSR_BOOT_IMAGE_VERIFY_H
#include <stdint.h>
#include <stdbool.h>
/* RAM prefix must be checked before copying over the running lower SRAM. */
bool inktag_tlsr_boot_ram_prefix(const uint8_t header[32], uint32_t capacity,
                                uint32_t *prefix);
#endif
