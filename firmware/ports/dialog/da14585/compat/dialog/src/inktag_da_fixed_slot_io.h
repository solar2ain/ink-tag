#ifndef INKTAG_DA_FIXED_SLOT_IO_H
#define INKTAG_DA_FIXED_SLOT_IO_H

/* I/O binding draft for the integration-reviewed fixed-slot common core.
 * No indexing/product state here.
 * Each operation owns one balanced SPI wake/sleep session; SDK write_data
 * owns page splitting. The existing physical-point voltage policy is retained.
 */
#include <stddef.h>
#include <stdint.h>
#include "inktag_da_health.h"
#include "inktag_protocol_v2.h"
#include "spi_flash.h"

#define INKTAG_DA_FIXED_STORE_START UINT32_C(0x24000)
#define INKTAG_DA_FIXED_STORE_SIZE UINT32_C(0x11000)
#define INKTAG_DA_FIXED_STORE_END UINT32_C(0x35000)
#define INKTAG_DA_FIXED_ERASE_BYTES UINT32_C(4096)

static inline bool inktag_da_fixed_io_range(uint32_t address, uint32_t length)
{
    return address >= INKTAG_DA_FIXED_STORE_START &&
           address <= INKTAG_DA_FIXED_STORE_END &&
           length <= INKTAG_DA_FIXED_STORE_END - address;
}

static inline inktag_status_v2_t inktag_da_fixed_io_read(
    uint32_t address, void *data, uint32_t length)
{
    uint32_t actual = 0u;
    int result;
    if (data == NULL || length == 0u ||
        !inktag_da_fixed_io_range(address, length))
        return INKTAG_STATUS_INVALID_REQUEST;
    if (spi_flash_release_from_power_down() != SPI_FLASH_ERR_OK)
        return INKTAG_STATUS_IO_ERROR;
    result = spi_flash_read_data((uint8_t *)data, address, length, &actual);
    if (spi_flash_power_down() != SPI_FLASH_ERR_OK)
        return INKTAG_STATUS_IO_ERROR;
    return result == SPI_FLASH_ERR_OK && actual == length ?
        INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}

static inline inktag_status_v2_t inktag_da_fixed_io_mutate(
    uint32_t address, const void *data, uint32_t length)
{
    uint32_t extent = data != NULL ? length : INKTAG_DA_FIXED_ERASE_BYTES;
    uint32_t actual = 0u;
    int result, sleep_result;
    if (!inktag_da_fixed_io_range(address, extent) ||
        (data != NULL ? length == 0u :
         (length != 0u || (address & (INKTAG_DA_FIXED_ERASE_BYTES - 1u)) != 0u)))
        return INKTAG_STATUS_INVALID_REQUEST;
    if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false))
        return INKTAG_STATUS_LOW_POWER;
    if (spi_flash_release_from_power_down() != SPI_FLASH_ERR_OK)
        return INKTAG_STATUS_IO_ERROR;
    if (data != NULL)
        result = spi_flash_write_data((uint8_t *)data, address, length, &actual);
    else
        result = spi_flash_block_erase(address, SPI_FLASH_OP_SE);
    sleep_result = spi_flash_power_down();
    /* Caller services BLE only after the engine publishes its new phase.
     * Re-entering abort inside MUTATE could otherwise resurrect a capture. */
    return result == SPI_FLASH_ERR_OK && sleep_result == SPI_FLASH_ERR_OK &&
           (data == NULL || actual == length) ?
        INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}

#endif
