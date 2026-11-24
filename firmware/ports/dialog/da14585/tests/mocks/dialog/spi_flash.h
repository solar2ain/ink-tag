#ifndef TEST_DIALOG_SPI_FLASH_H
#define TEST_DIALOG_SPI_FLASH_H

#include <stdint.h>

#define SPI_FLASH_ERR_OK 0
#define SPI_FLASH_ERR_FAILED 1
#define SPI_FLASH_OP_SE 0

int spi_flash_release_from_power_down(void);
int spi_flash_power_down(void);
int spi_flash_read_data(uint8_t *data, uint32_t address, uint32_t length,
                        uint32_t *actual);
int spi_flash_write_data(uint8_t *data, uint32_t address, uint32_t length,
                         uint32_t *actual);
int spi_flash_block_erase(uint32_t address, int operation);

#endif
