"""Exercise the shipping half-duplex SPI transaction and every error exit."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/common/epd/inktag_epd_driver.c').read_text()

def function(name):
    match=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert match,name
    end=match.end();depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define INKTAG_EPD_SPI_SOFTWARE_CS 1
#define INKTAG_NRF_BOARD_NRF52811_AESL 1
#define NRF_SUCCESS 0u
#define NRF_SPI_PIN_NOT_CONNECTED 255u
#define INPUT_PULLUP 2u
#define INPUT 0u
#define OUTPUT 1u
#define LOW 0u
#define HIGH 1u
#define NRF_GPIO_PIN_DIR_OUTPUT OUTPUT
#define NRF_GPIO_PIN_DIR_INPUT INPUT
#define HAL_SPI_INSTANCE 0u
#define APP_ERROR_CHECK(x) assert((x) == 0u)
typedef uint32_t ret_code_t;
typedef unsigned nrf_gpio_pin_dir_t;
static unsigned spi, m_driver_refs = 1u;
static unsigned EPD_MOSI_PIN=20, EPD_SCLK_PIN=19, EPD_CS_PIN=6, EPD_DC_PIN=5;
static unsigned mode[32], level[32], mosi=20, miso=255, calls, fail_at, cs_edges;
static void pinMode(unsigned pin, unsigned value) { mode[pin]=value; }
static nrf_gpio_pin_dir_t nrf_gpio_pin_dir_get(unsigned pin) {
    return mode[pin] == OUTPUT ? NRF_GPIO_PIN_DIR_OUTPUT : NRF_GPIO_PIN_DIR_INPUT;
}
static void digitalWrite(unsigned pin, unsigned value) {
    if (pin == EPD_CS_PIN) cs_edges++;
    level[pin]=value;
}
static void nrf_spi_pins_set(unsigned reg, unsigned clk, unsigned out, unsigned in) {
    assert(reg == 0u && clk == EPD_SCLK_PIN); mosi=out; miso=in;
}
static ret_code_t nrf_drv_spi_transfer(const unsigned *instance,
        uint8_t *tx, uint8_t tx_len, uint8_t *rx, uint8_t rx_len) {
    assert(instance == &spi && !level[EPD_CS_PIN]);
    calls++;
    if (tx_len) {
        assert(tx && !rx && !rx_len && mosi == 20u && miso == 255u);
        assert(mode[20] == OUTPUT);
    } else {
        assert(!tx && rx && rx_len == 2u && level[EPD_DC_PIN]);
        assert(mosi == 255u && miso == 20u && mode[20] == INPUT_PULLUP);
        rx[0]=22u; rx[1]=0x80u;
    }
    return calls == fail_at ? 1u : NRF_SUCCESS;
}
'''

TEST = r'''
static void reset(void) {
    mode[20]=OUTPUT;level[6]=HIGH;calls=cs_edges=0u;mosi=20u;miso=255u;
}
static void restored(void) {
    assert(level[6] && mode[20] == OUTPUT && mosi == 20u && miso == 255u);
}
int main(void) {
    uint8_t value[2];
    reset();fail_at=0u;
    assert(EPD_ReadCommand(0x1bu, value, 2u));
    assert(value[0] == 22u && value[1] == 0x80u && calls == 2u && cs_edges == 2u);
    restored();
    for (fail_at=1u; fail_at<=2u; fail_at++) {
        reset();assert(!EPD_ReadCommand(0x1bu, value, 2u));
        assert(calls == fail_at && cs_edges == 2u);restored();
    }
    reset();fail_at=0u;
    assert(!EPD_ReadCommand(0x1bu, NULL, 2u));
    assert(!EPD_ReadCommand(0x1bu, value, 0u));
    m_driver_refs=0u;assert(!EPD_ReadCommand(0x1bu, value, 2u));
    assert(calls == 0u && cs_edges == 0u);restored();
    m_driver_refs=1u;
    assert(EPD_ReadCommand(0x40u, value, 2u));
    unsigned before=calls;
    EPD_SPI_Write(value, 2u);assert(calls == before + 1u);restored();
    puts("PASS continuous-CS hardware SPI: half-duplex read, both IO failures, parked/null/empty refusal, restored write pins");
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    file = out / 'read.c'
    binary = out / 'read'
    file.write_text(PREFIX + function('epd_select_direction') + '\n' + function('epd_transfer') + '\n' +
                    function('EPD_ReadCommand') + '\n' +
                    function('EPD_SPI_Write') + TEST)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
