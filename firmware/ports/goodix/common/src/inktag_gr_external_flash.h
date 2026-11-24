#ifndef INKTAG_GR_EXTERNAL_FLASH_H
#define INKTAG_GR_EXTERNAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>


bool inktag_gr_external_flash_available(void);
uint32_t inktag_gr_external_flash_capacity(void);
uint32_t inktag_gr_external_flash_read(
    uint32_t offset, uint8_t *data, uint32_t length);
uint32_t inktag_gr_external_flash_write(
    uint32_t offset, const uint8_t *data, uint32_t length);
bool inktag_gr_external_flash_erase(uint32_t offset, uint32_t length);

#endif
