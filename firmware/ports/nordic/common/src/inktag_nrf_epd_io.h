#ifndef INKTAG_NRF_EPD_IO_H
#define INKTAG_NRF_EPD_IO_H
#include <stdbool.h>
#include "inktag_nrf_clock_io.h"

#include "nrf_drv_spi.h"
#include "sdk_errors.h"

/* Call only while the display owner holds an initialized non-DMA SPI bus
 * configured with software CS. The exact board supplies the pins; this helper
 * neither opens/powers a panel nor decides whether its sensor is supported.
 * SSD1675B uses Mode3/2MHz RX; checked R70/R40 readers use Mode0/1MHz.
 * The optional owner-supplied ready callback runs with CS held and SDA input,
 * before clocks. Always restore native CONFIG/FREQUENCY on every exit.
 * Electrical qualification is separate from this bounded transaction. */
ret_code_t inktag_nrf_epd_command_read(const nrf_drv_spi_t *bus,
                                      NRF_SPI_Type *instance,
                                      uint32_t clock_pin, uint32_t data_pin,
                                      uint32_t cs_pin, uint32_t dc_pin,
                                      uint8_t command, uint8_t *value,
                                      uint8_t length,
                                      bool (*read_ready)(uint8_t command));

/* Only the frozen read-only Hanshow trim windows. Keeps the selected panel
 * SPI mode and CS across skip+7 bytes+one closing byte, caps RX at1MHz and
 * restores the native bus on error/cancel. Electrical panel evidence is open. */
ret_code_t inktag_nrf_epd_trim_read(const nrf_drv_spi_t *bus,
    NRF_SPI_Type *instance, uint32_t clock_pin, uint32_t data_pin,
    uint32_t cs_pin, uint32_t dc_pin, uint8_t command, uint16_t skip,
    uint8_t *value, uint8_t length, bool (*abort_requested)(void),
    void (*maintenance)(void));

#if defined(INKTAG_NRF_CLOCK_PARTIAL_400X300) && INKTAG_NRF_CLOCK_PARTIAL_400X300
/* Fixed SSD1619/SSD1683/UC8276 clock read set only. Owner supplies controller family
 * after exact profile admission; no power, metadata writer or generic reader. */
ret_code_t inktag_nrf_epd_clock_read(const nrf_drv_spi_t *bus,
    NRF_SPI_Type *instance,uint32_t clock_pin,uint32_t data_pin,
    uint32_t cs_pin,uint32_t dc_pin,bool uc,uint8_t command,uint16_t skip,
    uint8_t *value,uint8_t length,bool (*abort_requested)(void),
    void (*maintenance)(void));
#endif

#endif
