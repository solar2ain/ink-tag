#include "inktag_nrf_external_flash.h"

#include <stddef.h>

#include "inktag_board.h"
#include "inktag_nrf_health.h"
#include "nrf_delay.h"
#include "nrf_error.h"
#include "nrf_gpio.h"

#if !defined(INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID) ||  !defined(INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY)
#error "external Flash transport requires exact-board identity and capacity"
#endif

#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_STATUS_BUSY 0x01u
#define FLASH_STATUS_WEL 0x02u
#define FLASH_PROGRAM_POLL_LIMIT 2000u
#define FLASH_ERASE_POLL_LIMIT 20000u

static uint8_t s_session_depth;
static uint32_t s_last_error = NRF_SUCCESS;

static inline void delay_half_period(void) {
    for (volatile uint32_t i = 0u; i < 2u; i++) __asm volatile ("nop");
}

static void delay_wake(void) {
    nrf_delay_us(50u);
}

static inline void chip_select(bool active) {
    if (active) nrf_gpio_pin_clear(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    else nrf_gpio_pin_set(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    delay_half_period();
}

static uint8_t spi_byte(uint8_t tx) {
    uint8_t rx = 0u;
    for (uint8_t bit = 0u; bit < 8u; bit++) {
        if ((tx & 0x80u) != 0u)
            nrf_gpio_pin_set(INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN);
        else
            nrf_gpio_pin_clear(INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN);
        tx <<= 1;
        delay_half_period();
        nrf_gpio_pin_set(INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN);
        delay_half_period();
        rx = (uint8_t)(rx << 1);
        if (nrf_gpio_pin_read(INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN) != 0u)
            rx |= 1u;
        nrf_gpio_pin_clear(INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN);
    }
    return rx;
}

static void bus_open(void) {
    nrf_gpio_pin_set(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    nrf_gpio_pin_clear(INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN);
    nrf_gpio_pin_clear(INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN);
    nrf_gpio_cfg_output(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    nrf_gpio_cfg_output(INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN);
    nrf_gpio_cfg_output(INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN);
    nrf_gpio_cfg_input(INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN,
                       NRF_GPIO_PIN_NOPULL);
}

static void bus_close(void) {
    nrf_gpio_pin_set(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    nrf_gpio_cfg_default(INKTAG_BOARD_EXTERNAL_FLASH_CS_PIN);
    nrf_gpio_cfg_default(INKTAG_BOARD_EXTERNAL_FLASH_MISO_PIN);
    nrf_gpio_cfg_default(INKTAG_BOARD_EXTERNAL_FLASH_SCLK_PIN);
    nrf_gpio_cfg_default(INKTAG_BOARD_EXTERNAL_FLASH_MOSI_PIN);
}

static void simple_command(uint8_t command) {
    chip_select(true);
    (void)spi_byte(command);
    chip_select(false);
}

static uint8_t read_status(void) {
    uint8_t status;
    chip_select(true);
    (void)spi_byte(0x05u);
    status = spi_byte(0u);
    chip_select(false);
    return status;
}

static uint32_t read_jedec_id(void) {
    uint32_t id;
    chip_select(true);
    (void)spi_byte(0x9Fu);
    id = (uint32_t)spi_byte(0u) << 16;
    id |= (uint32_t)spi_byte(0u) << 8;
    id |= spi_byte(0u);
    chip_select(false);
    return id;
}

static void send_address(uint8_t command, uint32_t address) {
    (void)spi_byte(command);
    (void)spi_byte((uint8_t)(address >> 16));
    (void)spi_byte((uint8_t)(address >> 8));
    (void)spi_byte((uint8_t)address);
}

static bool range_valid(uint32_t address, uint32_t length) {
    return length != 0u && address < INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY &&
           length <= INKTAG_BOARD_EXTERNAL_FLASH_CAPACITY - address;
}

static bool wait_ready(uint32_t poll_limit) {
    while (poll_limit-- != 0u) {
        if ((read_status() & FLASH_STATUS_BUSY) == 0u) return true;
        nrf_delay_us(100u);
    }
    s_last_error = NRF_ERROR_TIMEOUT;
    return false;
}

static bool write_enable(void) {
    simple_command(0x06u);
    if ((read_status() & FLASH_STATUS_WEL) != 0u) return true;
    s_last_error = NRF_ERROR_INVALID_STATE;
    return false;
}

bool inktag_nrf_external_flash_begin(void) {
    if (s_session_depth != 0u) {
        if (s_session_depth == UINT8_MAX) {
            s_last_error = NRF_ERROR_NO_MEM;
            return false;
        }
        s_session_depth++;
        s_last_error = NRF_SUCCESS;
        return true;
    }
    bus_open();
    simple_command(0xABu);
    delay_wake();
    if (read_jedec_id() != INKTAG_BOARD_EXTERNAL_FLASH_JEDEC_ID) {
        simple_command(0xB9u);
        bus_close();
        s_last_error = NRF_ERROR_NOT_FOUND;
        return false;
    }
    s_session_depth = 1u;
    s_last_error = NRF_SUCCESS;
    return true;
}

void inktag_nrf_external_flash_end(void) {
    if (s_session_depth == 0u) return;
    s_session_depth--;
    if (s_session_depth != 0u) return;
    simple_command(0xB9u);
    bus_close();
}

bool inktag_nrf_external_flash_read(uint32_t address, void *data,
                                    uint32_t length) {
    uint8_t *output = (uint8_t *)data;
    if (s_session_depth == 0u || data == NULL ||
        !range_valid(address, length)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    chip_select(true);
    send_address(0x03u, address);
    while (length-- != 0u) *output++ = spi_byte(0u);
    chip_select(false);
    s_last_error = NRF_SUCCESS;
    return true;
}

bool inktag_nrf_external_flash_program(uint32_t address, const void *data,
                                       uint16_t length) {
    const uint8_t *input = (const uint8_t *)data;
    if (s_session_depth == 0u || data == NULL ||
        !range_valid(address, length) || length > FLASH_PAGE_SIZE ||
        (address / FLASH_PAGE_SIZE) !=
            ((address + length - 1u) / FLASH_PAGE_SIZE)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    /* This is the real Page Program boundary. Re-sample here even when the
     * command ingress previously passed its voltage check. */
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        s_last_error = NRF_ERROR_FORBIDDEN;
        return false;
    }
    if (!wait_ready(FLASH_PROGRAM_POLL_LIMIT) || !write_enable()) return false;
    chip_select(true);
    send_address(0x02u, address);
    while (length-- != 0u) (void)spi_byte(*input++);
    chip_select(false);
    if (!wait_ready(FLASH_PROGRAM_POLL_LIMIT)) return false;
    s_last_error = NRF_SUCCESS;
    return true;
}

bool inktag_nrf_external_flash_erase_sector(uint32_t address) {
    if (s_session_depth == 0u ||
        (address & (FLASH_SECTOR_SIZE - 1u)) != 0u ||
        !range_valid(address, FLASH_SECTOR_SIZE)) {
        s_last_error = NRF_ERROR_INVALID_PARAM;
        return false;
    }
    /* This real Sector Erase boundary owns the final software voltage check
     * for journal compaction, including the unavailable-sample policy. */
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        s_last_error = NRF_ERROR_FORBIDDEN;
        return false;
    }
    if (!wait_ready(FLASH_PROGRAM_POLL_LIMIT) || !write_enable()) return false;
    chip_select(true);
    send_address(0x20u, address);
    chip_select(false);
    if (!wait_ready(FLASH_ERASE_POLL_LIMIT)) return false;
    s_last_error = NRF_SUCCESS;
    return true;
}

uint32_t inktag_nrf_external_flash_last_error(void) {
    return s_last_error;
}
