#include "inktag_nrf_build_profile.h"
#line 1 "firmware/ports/nordic/common/src/inktag_nrf_epd_io.c"
#include "inktag_nrf_epd_io.h"

#include <string.h>
#include "nrf_gpio.h"
#include "nrf_spi.h"

ret_code_t inktag_nrf_epd_command_read(const nrf_drv_spi_t *bus,
                                      NRF_SPI_Type *instance,
                                      uint32_t clock_pin, uint32_t data_pin,
                                      uint32_t cs_pin, uint32_t dc_pin,
                                      uint8_t command, uint8_t *value,
                                      uint8_t length,
                                      bool (*read_ready)(uint8_t command)) {
    ret_code_t result;
    uint32_t saved_config, saved_frequency;
    if (bus == NULL || instance == NULL || value == NULL || length == 0u)
        return NRF_ERROR_INVALID_PARAM;
    saved_config = instance->CONFIG;
    saved_frequency = instance->FREQUENCY;
    memset(value, 0xff, length);
    nrf_gpio_pin_set(cs_pin);
    nrf_gpio_cfg_output(data_pin);
    nrf_spi_pins_set(instance, clock_pin, data_pin, NRF_SPI_PIN_NOT_CONNECTED);
    nrf_gpio_pin_clear(dc_pin);
    nrf_gpio_pin_clear(cs_pin);
    result = nrf_drv_spi_transfer(bus, &command, 1u, NULL, 0u);
    if (result == NRF_SUCCESS) {
        nrf_gpio_pin_set(dc_pin);
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_epd_e6_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
        nrf_gpio_cfg_input(data_pin, NRF_GPIO_PIN_NOPULL);
#endif
        nrf_spi_pins_set(instance, clock_pin, NRF_SPI_PIN_NOT_CONNECTED, data_pin);
        /* SSD1675B shifts SDA on falling edges; sample on the following
         * rising edge, retaining the board's native idle-high polarity.
         * R1B reads are specified at <=2.5 MHz, unlike native 8 MHz writes. */
        bool mode0 =
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 6
#include "inktag_nrf_epd_e6_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
            command == 0x40u;
        nrf_spi_configure(instance, mode0 ? NRF_SPI_MODE_0 : NRF_SPI_MODE_3,
            NRF_SPI_BIT_ORDER_MSB_FIRST);
        nrf_spi_frequency_set(instance, mode0 ? NRF_SPI_FREQ_1M : NRF_SPI_FREQ_2M);
        result = read_ready != NULL && !read_ready(command) ? NRF_ERROR_TIMEOUT :
            nrf_drv_spi_transfer(bus, NULL, 0u, value, length);
    }
    /* CS rises before restoring the half-duplex data pad, so neither a failed
     * command nor a partial read can leave the controller driving our output.
     * Always restore native write mode/frequency. No panel RESET/power or
     * SPI driver ownership changes occur here. */
    nrf_gpio_pin_set(cs_pin);
    instance->CONFIG = saved_config;
    instance->FREQUENCY = saved_frequency;
    nrf_gpio_cfg_output(data_pin);
    nrf_spi_pins_set(instance, clock_pin, data_pin, NRF_SPI_PIN_NOT_CONNECTED);
    nrf_gpio_pin_set(dc_pin);
    if (result != NRF_SUCCESS) memset(value, 0xff, length);
    return result;
}

ret_code_t inktag_nrf_epd_trim_read(const nrf_drv_spi_t *bus,
    NRF_SPI_Type *instance, uint32_t clock_pin, uint32_t data_pin,
    uint32_t cs_pin, uint32_t dc_pin, uint8_t command, uint16_t skip,
    uint8_t *value, uint8_t length, bool (*abort_requested)(void),
    void (*maintenance)(void)) {
    uint8_t discard[64];
    if (!bus || !instance || !value || length != 7u || command != 0x92u ||
        (skip != 3945u && skip != 5993u) || !abort_requested || !maintenance)
        return NRF_ERROR_INVALID_PARAM;
    if (abort_requested()) return NRF_ERROR_INVALID_STATE;
    uint32_t saved_config = instance->CONFIG, saved_frequency = instance->FREQUENCY;
    memset(value, 0xff, length);
    nrf_gpio_pin_set(cs_pin);
    nrf_gpio_cfg_output(data_pin);
    nrf_spi_pins_set(instance, clock_pin, data_pin, NRF_SPI_PIN_NOT_CONNECTED);
    nrf_gpio_pin_clear(dc_pin);
    nrf_gpio_pin_clear(cs_pin);
    ret_code_t result = nrf_drv_spi_transfer(bus, &command, 1u, NULL, 0u);
    if (result == NRF_SUCCESS) {
        nrf_gpio_pin_set(dc_pin);
        nrf_gpio_cfg_input(data_pin, NRF_GPIO_PIN_NOPULL);
        nrf_spi_pins_set(instance, clock_pin, NRF_SPI_PIN_NOT_CONNECTED, data_pin);
        nrf_spi_frequency_set(instance, NRF_SPI_FREQ_1M);
        uint16_t total = (uint16_t)(skip + length + 1u), offset = 0u;
        while (offset < total) {
            maintenance();
            if (abort_requested()) { result = NRF_ERROR_INVALID_STATE; break; }
            uint16_t remaining = (uint16_t)(total - offset);
            uint8_t count = remaining > (uint16_t)sizeof(discard) ? (uint8_t)sizeof(discard) : (uint8_t)remaining;
            result = nrf_drv_spi_transfer(bus, NULL, 0u, discard, count);
            if (result != NRF_SUCCESS) break;
            for (uint8_t i = 0u; i < count; ++i) {
                uint16_t index = (uint16_t)(offset + i);
                if (index >= skip && index < skip + length) value[index - skip] = discard[i];
            }
            offset = (uint16_t)(offset + count);
        }
        if (result == NRF_SUCCESS && abort_requested()) result = NRF_ERROR_INVALID_STATE;
    }
    nrf_gpio_pin_set(cs_pin);
    instance->CONFIG = saved_config;
    instance->FREQUENCY = saved_frequency;
    nrf_gpio_cfg_output(data_pin);
    nrf_spi_pins_set(instance, clock_pin, data_pin, NRF_SPI_PIN_NOT_CONNECTED);
    nrf_gpio_pin_set(dc_pin);
    if (result != NRF_SUCCESS) memset(value, 0xff, length);
    return result;
}

#if INKTAG_BUILD_PRO
#include "inktag_nrf_epd_clock_read_pro.inc"
#endif
