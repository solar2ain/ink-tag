#ifndef INKTAG_NRF_EXTERNAL_FLASH_H
#define INKTAG_NRF_EXTERNAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>

/* Shared exact-board SPI-NOR transport for the nRF52811 auxiliary Flash.
 * Callers may nest begin/end pairs; the outermost end deep-powers-down the
 * part and releases every bus pin. */
bool inktag_nrf_external_flash_begin(void);
void inktag_nrf_external_flash_end(void);
bool inktag_nrf_external_flash_read(uint32_t address, void *data,
                                    uint32_t length);
bool inktag_nrf_external_flash_program(uint32_t address, const void *data,
                                       uint16_t length);
bool inktag_nrf_external_flash_erase_sector(uint32_t address);
uint32_t inktag_nrf_external_flash_last_error(void);

#endif
