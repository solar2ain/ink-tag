# DA normal source projection v1
#!/usr/bin/env python3
"""Materialize the Dialog/Renesas DA14585 InkTag port inside SDK6."""

from __future__ import annotations

import argparse
import json
import re
import runpy
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


source_path = runpy.run_path(str(Path(__file__).parents[1] / 'build/source_scope.py'))['source_path']

DA14585_APPLICATION_RAW_LIMIT = 0x17600

def specialize_da_fixed_header(text: str) -> str:
    """Keep v5 bytes unchanged, expose DA's aligned RAM copies to the compiler."""
    anchor = "typedef struct __attribute__((packed)) {"
    if text.count(anchor) != 1:
        raise RuntimeError("fixed-slot header alignment anchor changed")
    text = text.replace(anchor, "typedef struct __attribute__((packed, aligned(4))) {")
    checks = """
/* DA only: SDK SPI copies records into aligned RAM objects, never casts NOR
 * bytes to this type. Preserve all frozen v5 offsets despite stronger alignment. */
#include <stddef.h>
typedef char da_fixed_header_layout_check[
    sizeof(inktag_image_store_header_t) == 96 &&
    __alignof__(inktag_image_store_header_t) == 4 &&
    offsetof(inktag_image_store_header_t, magic) == 0 &&
    offsetof(inktag_image_store_header_t, segment_size) == 32 &&
    offsetof(inktag_image_store_header_t, payload_size) == 40 &&
    offsetof(inktag_image_store_header_t, payload_crc) == 44 &&
    offsetof(inktag_image_store_header_t, created_at) == 48 &&
    offsetof(inktag_image_store_header_t, name) == 52 &&
    offsetof(inktag_image_store_header_t, header_crc) == 92 ? 1 : -1];
"""
    if text.count("\n#endif") != 1:
        raise RuntimeError("fixed-slot header guard anchor changed")
    return text.replace("\n#endif", checks + "\n#endif")


PRUNE_SOURCE_FILES = {
    "diss.c",
    "diss_task.c",
    "app_security.c",
    "app_security_task.c",
    "app_bass.c",
    "app_bass_task.c",
    "app_findme.c",
    "app_findme_task.c",
    "app_proxr.c",
    "app_proxr_task.c",
    "app_diss.c",
    "app_diss_task.c",
    "app_bond_db.c",
    "app_easy_crypto.c",
    "app_easy_security.c",
    "app_easy_storage.c",
    "app_easy_timer.c",
    "app_easy_whitelist.c",
    "app_suotar.c",
    "app_suotar_task.c",
    "battery.c",
    "fpga.c",
    "i2c.c",
    "i2c_eeprom.c",
    "uart.c",
}

BOARD_HEADER_FILES = {
    "da14585_aesl": Path("boards/da14585_aesl.h"),
}

BOARD_ARTIFACTS = {
    "da14585_aesl": "InkTag-da14585_aesl",
}

APPLICATION_BOARD_IDS = {
    "da14585_aesl": ("aesl29_da", "aesl42_da"),
}

LOW_FREQUENCY_CLOCK_MACROS = {
    "lfxo": "LP_CLK_XTAL32",
    "internal-rc": "LP_CLK_RCX20",
}

BOARD_DRIVER_SOURCES = {
    "da14585_aesl": ["inktag_epd_uc.c", "inktag_epd_ssd.c", "inktag_epd_jd.c"],
}

BOARD_DRIVER_DEFINES = {
    # DA keeps the complete shared UC8xxx/SSD1xxx/JD79xxx controller and
    # recipe catalog. Compiled availability does not qualify a panel on an
    # exact board; runtime capability and release claims still require the
    # corresponding physical evidence.
    "da14585_aesl": "-DINKTAG_EPD_ENABLE_UC8XXX=1 -DINKTAG_EPD_ENABLE_UC8151=0 -DINKTAG_EPD_ENABLE_SSD1XXX=1 -DINKTAG_EPD_ENABLE_JD79XXX=1 -DINKTAG_EPD_ENABLE_SSD1675=0 -DINKTAG_EXCLUDE_SSD1675_154=1 -DINKTAG_EPD_ENABLE_JD79665CA=0 -DINKTAG_EPD_ENABLE_SSD1677_GRAY4=0 -DINKTAG_DEVICE_INFO_QR_STORAGE_ENABLED=0",
}

DA14585_RECOVERY_PROJECT = "ble_app_inktag_recovery"
DA14585_RECOVERY_MAX_IMAGE_SIZE = 0x8000

GUI_SOURCES = ["Adafruit_GFX.c", "u8g2_font.c", "inktag_gui_fonts.c", "inktag_gui.c", "inktag_calendar.c"]
GUI_HEADERS = [
    "Adafruit_GFX.h",
    "u8g2_font.h",
    "inktag_gui_fonts.h",
    "inktag_gui.h",
    "inktag_gui_assets.h",
    "inktag_gui_device_qr.h",
    "inktag_calendar.h",
]
GUI_INCLUDES = ["inktag_gui_default.inc", "inktag_gui_calendar.inc", "inktag_solar_terms.inc"]


def resolve_low_frequency_clock(repo: Path, board: str) -> str:
    if board == "common":
        return LOW_FREQUENCY_CLOCK_MACROS["internal-rc"]

    board_ids = APPLICATION_BOARD_IDS.get(board)
    if board_ids is None:
        raise RuntimeError(f"{board}: no exact-board low-frequency clock group")
    registry = json.loads(
        source_path(repo, "boards/boards.json").read_text(encoding="utf-8")
    )["boards"]
    sources = set()
    for board_id in board_ids:
        record = registry.get(board_id)
        clock = record.get("lowFrequencyClock") if record is not None else None
        source = clock.get("source") if clock is not None else None
        if source not in LOW_FREQUENCY_CLOCK_MACROS:
            raise RuntimeError(
                f"{board_id}: missing or unsupported exact-board low-frequency clock source"
            )
        sources.add(source)
    if len(sources) != 1:
        raise RuntimeError(
            f"{board}: shared application members disagree on low-frequency clock: "
            + ", ".join(sorted(sources))
        )
    return LOW_FREQUENCY_CLOCK_MACROS[sources.pop()]

BOARD_GPIO_RESERVATIONS = {
    "common": """    RESERVE_GPIO(INKTAG_EPD_MOSI, GPIO_PORT_0, GPIO_PIN_0, PID_SPI_DO);
    RESERVE_GPIO(INKTAG_EPD_SCLK, GPIO_PORT_0, GPIO_PIN_4, PID_SPI_CLK);
    RESERVE_GPIO(INKTAG_EPD_CS, GPIO_PORT_0, GPIO_PIN_1, PID_SPI_EN);
    RESERVE_GPIO(INKTAG_EPD_DC, GPIO_PORT_0, GPIO_PIN_2, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_RST, GPIO_PORT_0, GPIO_PIN_5, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_BUSY, GPIO_PORT_0, GPIO_PIN_6, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_BS, GPIO_PORT_0, GPIO_PIN_7, PID_GPIO);""",
    "da14585_aesl": """    RESERVE_GPIO(INKTAG_EPD_MOSI, GPIO_PORT_2, GPIO_PIN_0, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_SCLK, GPIO_PORT_0, GPIO_PIN_1, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_CS, GPIO_PORT_2, GPIO_PIN_1, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_DC, GPIO_PORT_0, GPIO_PIN_7, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_RST, GPIO_PORT_1, GPIO_PIN_0, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_BUSY, GPIO_PORT_1, GPIO_PIN_1, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_BS, GPIO_PORT_2, GPIO_PIN_2, PID_GPIO);
    RESERVE_GPIO(INKTAG_EPD_EN, GPIO_PORT_2, GPIO_PIN_3, PID_GPIO);

    /* External code/image Flash bus. These are storage pins, not panel pins. */
    RESERVE_GPIO(INKTAG_FLASH_CLK, GPIO_PORT_0, GPIO_PIN_0, PID_SPI_CLK);
    RESERVE_GPIO(INKTAG_FLASH_CS, GPIO_PORT_0, GPIO_PIN_3, PID_SPI_EN);
    RESERVE_GPIO(INKTAG_FLASH_MOSI, GPIO_PORT_0, GPIO_PIN_6, PID_SPI_DO);
    RESERVE_GPIO(INKTAG_FLASH_MISO, GPIO_PORT_0, GPIO_PIN_5, PID_SPI_DI);""",
}

BOARD_STORAGE_PAD_CONFIGURATION = {
    "common": "",
    "da14585_aesl": """    /* External code/image Flash bus; independent of the panel wiring. */
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_3, OUTPUT, PID_SPI_EN, true);
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_0, OUTPUT, PID_SPI_CLK, false);
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_6, OUTPUT, PID_SPI_DO, false);
    GPIO_ConfigurePin(GPIO_PORT_0, GPIO_PIN_5, INPUT, PID_SPI_DI, false);""",
}


EPD_DIALOG_PREFIX = r'''#include "gpio.h"
#include "spi.h"
#include "systick.h"
#include "syscntl.h"
#include "inktag_epd_driver.h"
#include "inktag_epd_wait_policy.h"
#include "inktag_da_health.h"
#include "app_entry_point.h"
#include "app_easy_timer.h"

#include <stdbool.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BUFFER_SIZE 128
#define EPD_WAIT_SERVICE_MASK 0x3fu

_Static_assert((EPD_WAIT_SERVICE_MASK + 1u) <=
                   INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS,
               "EPD maintenance cadence exceeds product policy");
_Static_assert((EPD_WAIT_SERVICE_MASK + 1u) <=
                   INKTAG_EPD_WAIT_CANCEL_MAX_MS,
               "EPD cancellation cadence exceeds product policy");

extern void app_feed_wdt(void);
extern void app_service_ble(void);
extern void app_epd_trace(const char* tag);
#define EPD_BUSY_TRACE(_tag) app_epd_trace(_tag)

static uint32_t EPD_MOSI_PIN = 0x00;
static uint32_t EPD_SCLK_PIN = 0x04;
static uint32_t EPD_CS_PIN = 0x01;
static uint32_t EPD_DC_PIN = 0x02;
static uint32_t EPD_RST_PIN = 0x05;
static uint32_t EPD_BUSY_PIN = 0x06;
static uint32_t EPD_BS_PIN = 0x07;
static uint32_t EPD_EN_PIN = 0xFF;
static uint32_t EPD_LED_PIN = 0xFF;
static bool EPD_LED_ENABLED = false;
static epd_abort_callback_t m_abort_callback = NULL;
static epd_maintenance_callback_t m_maintenance_callback = NULL;
static bool m_busy_diag_asserted = false;
static uint32_t m_busy_diag_assert_delay_ms = 0;
static uint32_t m_busy_diag_active_ms = 0;
static uint16_t m_driver_refs = 0;
static bool m_spi_ready = false;
static bool m_enable_active_low;
static bool m_spi_hold_cs = false;
static int8_t m_spi_last_error = SPI_STATUS_ERR_OK;
static uint16_t m_diag_spi_transfers = 0;

static GPIO_PORT da_port(uint32_t pin) { return (GPIO_PORT)((pin >> 4) & 0x0F); }
static GPIO_PIN da_pin(uint32_t pin) { return (GPIO_PIN)(pin & 0x0F); }
static bool pin_valid(uint32_t pin) { return pin != 0xFF && da_port(pin) <= GPIO_PORT_3 && da_pin(pin) <= GPIO_PIN_9; }

static uint32_t da_gpio_data_reg(uint32_t pin)
{
    uint32_t port = (uint32_t)da_port(pin);
    if (port == (uint32_t)GPIO_PORT_3) port = (uint32_t)GPIO_PORT_3_REMAP;
    return GPIO_BASE + (port << 5);
}

static uint16_t da_gpio_mask(uint32_t pin) { return (uint16_t)(1u << (uint32_t)da_pin(pin)); }

void delay(uint32_t ms)
{
    while (ms--) {
        systick_wait(1000);
        /* Release SysTick before servicing BLE. Reset pulse delays otherwise
         * block the scheduler for 100 ms per edge despite cooperative BUSY
         * waits. The port's callback guard prevents nested SDK scheduling. */
        app_service_ble();
    }
}

void pinMode(uint32_t pin, uint32_t mode)
{
    if (!pin_valid(pin)) return;

    GPIO_PUPD pupd = (GPIO_PUPD)0;
    switch (mode) {
        case INPUT_PULLUP:
            pupd = (GPIO_PUPD)0x100;
            break;
        case INPUT_PULLDOWN:
            pupd = (GPIO_PUPD)0x200;
            break;
        case OUTPUT:
            pupd = (GPIO_PUPD)0x300;
            break;
        case INPUT:
        default:
            pupd = (GPIO_PUPD)0x000;
            break;
    }
    GPIO_ConfigurePin(da_port(pin), da_pin(pin), pupd, PID_GPIO, false);
}

void digitalWrite(uint32_t pin, uint32_t value)
{
    if (!pin_valid(pin)) return;
    if (value) {
        GPIO_SetActive(da_port(pin), da_pin(pin));
    } else {
        GPIO_SetInactive(da_port(pin), da_pin(pin));
    }
}

uint32_t digitalRead(uint32_t pin)
{
    if (!pin_valid(pin)) return 0;
    return GPIO_GetPinStatus(da_port(pin), da_pin(pin)) ? 1u : 0u;
}

void EPD_DialogDiagReset(void)
{
    m_diag_spi_transfers = 0;
    m_spi_last_error = SPI_STATUS_ERR_OK;
    EPD_BusyDiagReset();
}

bool EPD_DialogSpiReady(void) { return m_spi_ready; }
int8_t EPD_DialogSpiLastError(void) { return m_spi_last_error; }
uint32_t EPD_DialogSpiBytes(void) { return 0u; }
uint16_t EPD_DialogSpiTransfers(void) { return m_diag_spi_transfers; }
uint16_t EPD_DialogResetTransitions(void) { return 0u; }
uint16_t EPD_DialogDcTransitions(void) { return 0u; }
uint16_t EPD_DialogBusyTransitions(void) { return 0u; }

uint8_t EPD_DialogPinLevels(void)
{
    uint8_t levels = 0;
    if (m_driver_refs == 0) return levels;
    if (pin_valid(EPD_BUSY_PIN) && GPIO_GetPinStatus(da_port(EPD_BUSY_PIN), da_pin(EPD_BUSY_PIN))) levels |= 0x01;
    if (pin_valid(EPD_RST_PIN) && GPIO_GetPinStatus(da_port(EPD_RST_PIN), da_pin(EPD_RST_PIN))) levels |= 0x02;
    if (pin_valid(EPD_DC_PIN) && GPIO_GetPinStatus(da_port(EPD_DC_PIN), da_pin(EPD_DC_PIN))) levels |= 0x04;
    if (!pin_valid(EPD_EN_PIN) || GPIO_GetPinStatus(da_port(EPD_EN_PIN), da_pin(EPD_EN_PIN))) levels |= 0x08;
    if (pin_valid(EPD_CS_PIN) && GPIO_GetPinStatus(da_port(EPD_CS_PIN), da_pin(EPD_CS_PIN))) levels |= 0x10;
    if (pin_valid(EPD_SCLK_PIN) && GPIO_GetPinStatus(da_port(EPD_SCLK_PIN), da_pin(EPD_SCLK_PIN))) levels |= 0x20;
    if (pin_valid(EPD_MOSI_PIN) && GPIO_GetPinStatus(da_port(EPD_MOSI_PIN), da_pin(EPD_MOSI_PIN))) levels |= 0x40;
    return levels;
}

void EPD_GPIO_Load(epd_config_t *cfg)
{
    if (cfg == NULL) return;
    EPD_MOSI_PIN = cfg->mosi_pin;
    EPD_SCLK_PIN = cfg->sclk_pin;
    EPD_CS_PIN = cfg->cs_pin;
    EPD_DC_PIN = cfg->dc_pin;
    EPD_RST_PIN = cfg->rst_pin;
    EPD_BUSY_PIN = cfg->busy_pin;
    EPD_BS_PIN = cfg->bs_pin;
    EPD_EN_PIN = cfg->en_pin;
    m_enable_active_low = cfg->enable_active_low != 0u;
    EPD_LED_PIN = cfg->led_pin;
}

void EPD_GPIO_Init(void)
{
    if (EPD_MOSI_PIN == 0xFF) return; /* Common disabled-display configuration. */
    if (m_driver_refs++ > 0) return;

    SetBits16(PMU_CTRL_REG, PERIPH_SLEEP, 0);
    while (!(GetWord16(SYS_STAT_REG) & PER_IS_UP));
    SetBits16(CLK_16M_REG, XTAL16_BIAS_SH_ENABLE, 1);

    /* The stock AESL display transport is GPIO-driven. Keep DA on the same
     * electrical path and use the same mode-0 edges as the GR adapter instead
     * of sharing the hardware SPI block used during boot. */
    if (EPD_EN_PIN != 0xFF) {
        pinMode(EPD_EN_PIN, OUTPUT);
        digitalWrite(EPD_EN_PIN, !m_enable_active_low);
        delay(10);
    }

    pinMode(EPD_RST_PIN, OUTPUT);
    digitalWrite(EPD_RST_PIN, LOW);
    delay(10);

    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_SCLK_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);
    digitalWrite(EPD_CS_PIN, HIGH);
    /* Every registered panel uses SPI mode 0 on this GPIO transport. Keep
     * SCK low not only before the first byte but also between CS-framed
     * command/data transfers; leaving it high changes the leading edge and
     * made the SES UC8151 ignore PON/DRF without ever asserting BUSY_N. */
    digitalWrite(EPD_SCLK_PIN, LOW);
    digitalWrite(EPD_MOSI_PIN, LOW);
    m_spi_last_error = SPI_STATUS_ERR_OK;
    m_spi_ready = true;

    if (EPD_BS_PIN != 0xFF) {
        pinMode(EPD_BS_PIN, OUTPUT);
        digitalWrite(EPD_BS_PIN, LOW);
    }
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_RST_PIN, HIGH);
    if (EPD_LED_PIN != 0xFF) pinMode(EPD_LED_PIN, OUTPUT);
    EPD_LED_ON();
    GPIO_set_pad_latch_en(true);
}

#if defined(INKTAG_DA_A33_PANEL_CANDIDATE) && INKTAG_DA_A33_PANEL_CANDIDATE
void EPD_AeslStockBusSelect(bool high)
{
    digitalWrite(EPD_BS_PIN, high ? HIGH : LOW);
}
#endif

void EPD_GPIO_Uninit(void)
{
    if (m_driver_refs == 0 || --m_driver_refs > 0) return;

    EPD_LED_OFF();
    m_spi_ready = false;
    /* Park every driven panel input before cutting its supply. The final
     * deep-sleep byte (0xA5) otherwise leaves MOSI high and can back-power an
     * unpowered panel through its input protection path. Nested users must
     * retain their pins until the last balanced close above. */
    digitalWrite(EPD_SCLK_PIN, LOW);
    digitalWrite(EPD_MOSI_PIN, LOW);
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
#if defined(INKTAG_DA_A33_PANEL_CANDIDATE) && INKTAG_DA_A33_PANEL_CANDIDATE
    /* Use the existing GPIO writer directly for parking, so the stock-HIGH
     * helper has one call site and LTO can specialize it without a wrapper. */
    digitalWrite(EPD_BS_PIN, LOW);
#endif
    if (EPD_EN_PIN != 0xFF) digitalWrite(EPD_EN_PIN, m_enable_active_low);
}

bool EPD_DialogPinsRelease(void)
{
    const uint8_t pads[] = {EPD_MOSI_PIN, EPD_SCLK_PIN, EPD_CS_PIN,
        EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN, EPD_EN_PIN};
    unsigned i;
    /* Do not steal pads from a nested driver or active transfer. Normal
     * Uninit has already parked outputs and cut panel power. */
    if (m_driver_refs != 0u || m_spi_ready) return false;
    if (EPD_EN_PIN != 0xFF) digitalWrite(EPD_EN_PIN, m_enable_active_low);
    for (i = 0u; i < sizeof(pads); ++i)
        pinMode(pads[i], pads[i] == EPD_EN_PIN && m_enable_active_low ? INPUT_PULLUP : INPUT_PULLDOWN);
    return true;
}

void EPD_SPI_Write(uint8_t *value, uint8_t len)
{
    if (m_spi_ready && value && len) {
        const uint32_t cs_data_reg = da_gpio_data_reg(EPD_CS_PIN);
        const uint32_t clk_data_reg = da_gpio_data_reg(EPD_SCLK_PIN);
        const uint32_t mosi_data_reg = da_gpio_data_reg(EPD_MOSI_PIN);
        const uint16_t cs_mask = da_gpio_mask(EPD_CS_PIN);
        const uint16_t clk_mask = da_gpio_mask(EPD_SCLK_PIN);
        const uint16_t mosi_mask = da_gpio_mask(EPD_MOSI_PIN);
        m_diag_spi_transfers++;
        if ((m_diag_spi_transfers & 0x3fu) == 0u) app_service_ble();
        SetWord16(cs_data_reg + 4u, cs_mask);
        for (uint8_t i = 0; i < len; i++) {
            uint8_t byte = value[i];
            for (uint8_t bit = 0; bit < 8; bit++) {
                SetWord16(clk_data_reg + 4u, clk_mask);
                SetWord16(mosi_data_reg +
                              ((byte & 0x80u) ? 2u : 4u),
                          mosi_mask);
                byte <<= 1;
                SetWord16(clk_data_reg + 2u, clk_mask);
            }
        }
        SetWord16(clk_data_reg + 4u, clk_mask);
        if (!m_spi_hold_cs) SetWord16(cs_data_reg + 2u, cs_mask);
    }
}

void EPD_SPI_Read(uint8_t *value, uint8_t len)
{
    if (m_spi_ready && value && len) {
        pinMode(EPD_MOSI_PIN, INPUT_PULLUP);
        digitalWrite(EPD_CS_PIN, LOW);
        for (uint8_t i = 0; i < len; i++) {
            uint8_t byte = 0u;
            for (uint8_t bit = 0; bit < 8; bit++) {
                systick_wait(4u);
                byte = (uint8_t)((byte << 1u) |
                                 (digitalRead(EPD_MOSI_PIN) ? 1u : 0u));
                digitalWrite(EPD_SCLK_PIN, HIGH);
                systick_wait(4u);
                digitalWrite(EPD_SCLK_PIN, LOW);
            }
            value[i] = byte;
        }
        if (!m_spi_hold_cs) digitalWrite(EPD_CS_PIN, HIGH);
        pinMode(EPD_MOSI_PIN, OUTPUT);
        digitalWrite(EPD_MOSI_PIN, LOW);
    }
}

void EPD_WriteCmd(uint8_t cmd)
{
    digitalWrite(EPD_DC_PIN, LOW);
    EPD_SPI_Write(&cmd, 1);
}

void EPD_WriteData(uint8_t *value, uint8_t len)
{
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Write(value, len);
}

void EPD_ReadData(uint8_t *value, uint8_t len)
{
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Read(value, len);
}

bool EPD_ReadCommand(uint8_t command, uint8_t *value, uint8_t len)
{
    if (!m_spi_ready || value == NULL || len == 0u) return false;

    /* JD79665AA uses one half-duplex SDA line and requires CS to remain low
     * across the command-to-read turnaround. Its qualified lab transaction
     * samples while SCK is low, then advances the controller on the rising
     * edge. A pull-up makes a disconnected/floating panel fail as FF FF. */
    m_spi_hold_cs = true;
    EPD_WriteCmd(command);
    EPD_ReadData(value, len);
    m_spi_hold_cs = false;
    digitalWrite(EPD_CS_PIN, HIGH);
    return m_spi_last_error == SPI_STATUS_ERR_OK;
}

void EPD_WriteByte(uint8_t value)
{
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Write(&value, 1);
}

uint8_t EPD_ReadByte(void)
{
    uint8_t value = 0;
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Read(&value, 1);
    return value;
}

void I2C_Handler(void)
{
}

void UART2_Handler(void)
{
}

enum process_event_response app_timer_api_process_handler(ke_msg_id_t const msgid,
                                                          void const *param,
                                                          ke_task_id_t const dest_id,
                                                          ke_task_id_t const src_id,
                                                          enum ke_msg_status_tag *msg_ret)
{
    (void)msgid;
    (void)param;
    (void)dest_id;
    (void)src_id;
    (void)msg_ret;
    return PR_EVENT_UNHANDLED;
}

timer_hnd app_easy_timer(const uint32_t delay, timer_callback fn)
{
    (void)delay;
    (void)fn;
    return EASY_TIMER_INVALID_TIMER;
}

void app_easy_timer_cancel(const timer_hnd timer_id)
{
    (void)timer_id;
}

timer_hnd app_easy_timer_modify(const timer_hnd timer_id, const uint32_t delay)
{
    (void)timer_id;
    (void)delay;
    return EASY_TIMER_INVALID_TIMER;
}

void app_easy_timer_cancel_all(void)
{
}
'''


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and src.read_bytes() == dst.read_bytes():
        return
    # Changed bytes need a fresh destination mtime for incremental compilers.
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)


def add_file(group: ET.Element, name: str, path: str) -> None:
    files = group.find("Files")
    if files is None:
        files = ET.SubElement(group, "Files")
    for file_node in files.findall("File"):
        if file_node.findtext("FilePath") == path:
            return
    file_node = ET.SubElement(files, "File")
    ET.SubElement(file_node, "FileName").text = name
    ET.SubElement(file_node, "FileType").text = "1"
    ET.SubElement(file_node, "FilePath").text = path


def prune_file_nodes(root: ET.Element) -> int:
    removed = 0
    for files in root.findall(".//Files"):
        for file_node in list(files.findall("File")):
            name = file_node.findtext("FileName")
            if name in PRUNE_SOURCE_FILES:
                files.remove(file_node)
                removed += 1
    return removed


def patch_uvprojx(path: Path, project_sources: list[str], driver_defines: str) -> None:
    text = path.read_text(encoding="utf-8")
    text = text.replace("ble_app_peripheral", "ble_app_inktag")
    text = text.replace("ble_app_peripheral_585", "InkTag-DA14585")
    text = text.replace("ble_app_peripheral_586", "InkTag-DA14586")
    text = text.replace(
        "-mthumb -c -include",
        f"-mthumb -c -DINKTAG_DIALOG -DINKTAG_DA_SINGLE_APP_SLOT=1 "
        f"-DINKTAG_DA_APPLICATION_MEMORY_MAP=1 "
        f"-DINKTAG_EPD_JD79XXX_A0_STREAM_STATE_SYMBOL=inktag_da_gui_frame_buffer "
        f"-DINKTAG_EPD_JD79XXX_A0_STREAM_STATE_BYTES=2304 "
        f"-DINKTAG_EPD_JD79XXX_A0_STREAM_STATE_OFFSET=2108 "
        f"-DINKTAG_PROTOCOL_FREE_ONLY_TARGET=1 "
        f"-DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1 "
        f"-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1 "
        f"-DINKTAG_GUI_STATIC_BUFFER_ONLY "
        # Keep the full shared GUI implementation. Only its page buffer and
        # cooperative-yield mechanics and the runtime-override slot are
        # DA-specific memory adaptations. DA does not expose SET_HOLIDAYS;
        # the complete built-in 2026 schedule remains in shared read-only data.
        f"-DINKTAG_GUI_RAW_VIEWPORT -DINKTAG_GUI_COOPERATIVE_YIELD "
        f"-DINKTAG_GUI_COOPERATIVE_YIELD_PAGE_ONLY "
        f"-DINKTAG_GUI_MAX_RUNTIME_HOLIDAYS=1 "
        # Preserve the frozen 2,144-byte App stack reservation. The historical
        # 1,736-byte peak plus 384-byte margin is a floor, not permission to
        # shrink this boundary; each candidate still needs its own stack proof.
        f"-DSTACK_SIZE=0x860 {driver_defines} -include",
    )
    text = text.replace("</IncludePath>", ";..\\src\\inktag_common</IncludePath>")
    text = text.replace(
        "..\\..\\..\\..\\..\\sdk\\common_project_files\\scatterfiles\\DA14585_586_armclang.sct",
        "inktag_da14585.sct",
    )
    path.write_text(text, encoding="utf-8")

    tree = ET.parse(path)
    root = tree.getroot()
    # Some SDK6 groups use value 2 (inherit/enable) even when the target-level
    # ArmClang LTO sees the complete application while vendor ROM entry points
    # remain explicit SDK objects. This keeps all features and removes duplicate
    # cross-module helpers in DA14585's code-in-SRAM image.
    for option in root.findall(".//v6Lto"):
        option.text = "1"
    for option in root.findall(".//AdsLtoi"):
        option.text = "1"
    for option in root.findall(".//Optim"):
        if option.text == "0":
            option.text = "7"
    # DA executes the complete application from its 96 KiB RAM window. Keep
    # the SDK's highest optimization level, but consistently favor code size
    # instead of inheriting per-file speed bias from the sample project.
    for option in root.findall(".//oTime"):
        option.text = "0"
    removed = prune_file_nodes(root)
    # Only this App uses the validated, allocation-free RX-to-queue bridge.
    # Never patch the shared SDK source or Recovery's profile/receive path.
    for file_node in root.findall(".//File"):
        if file_node.findtext("FileName") == "custs1_task.c":
            file_node.find("FilePath").text = "..\\src\\custom_profile\\custs1_task.c"
    for target in root.findall(".//Target"):
        groups = target.find("Groups")
        if groups is None:
            continue
        group = ET.SubElement(groups, "Group")
        ET.SubElement(group, "GroupName").text = "inktag_common"
        add_file(group, "systick.c", "..\\..\\..\\..\\..\\sdk\\platform\\driver\\systick\\systick.c")
        add_file(group, "epd_driver_dialog.c", "..\\src\\epd_driver_dialog.c")
        add_file(group, "inktag_epd_config.c", "..\\src\\inktag_common\\inktag_epd_config.c")
        for source in project_sources:
            add_file(group, source, f"..\\src\\inktag_common\\{source}")
    tree.write(path, encoding="utf-8", xml_declaration=True)
    print(f"Pruned {removed} unused Dialog sample source entries")


def specialize_da_custs1_task(text: str) -> str:
    """Remove only the duplicate CUSTS1 receive allocation, after SDK validation.

    At 220-byte values SDK6 can retain the original GATTC request/reassembly
    while allocating a second 226-byte CUSTS1 indication. An exact-board trap
    found RESET_MEM_ALLOC_FAIL there, with no free message block above152B.
    Keep CCC/attribute/permission validation and ATT confirmation byte-exact;
    the App copies borrowed bytes into its existing one-packet queue and
    processes them later, outside the scheduler. No payload/heap size change.
    """
    anchor = """            // Inform APP
            struct custs1_val_write_ind *req_id = KE_MSG_ALLOC_DYN(CUSTS1_VAL_WRITE_IND,
                                                                   prf_dst_task_get(&(custs1_env->prf_env), KE_IDX_GET(src_id)),
                                                                   dest_id,
                                                                   custs1_val_write_ind,
                                                                   param->length);
            memcpy(req_id->value, param->value, param->length);
            req_id->handle = att_idx;
            req_id->length = param->length;
            req_id->conidx = conidx;

            KE_MSG_SEND(req_id);"""
    if text.count(anchor) != 1:
        raise RuntimeError("SDK6 CUSTS1 validated RX allocation anchor changed")
    return '#include "user_custs1_impl.h"\n' + text.replace(anchor, """            /* Borrow only until this SDK callback returns. The App copies
             * into its owned queue; no nested scheduler or display work. */
            inktag_da_write_value(conidx, att_idx, param->value, param->length);""", 1)


def materialize_da14585_scatter(sdk: Path, project: Path) -> None:
    source = sdk / "sdk/common_project_files/scatterfiles/DA14585_586_armclang.sct"
    target = project / "Keil_5/inktag_da14585.sct"
    text = source.read_text(encoding="utf-8")
    selectors = ["free_area_0", "free_area_1", "free_area_2", "free_area_3"]
    for selector in selectors:
        # +FIRST keeps these exact sections from being consumed by the earlier
        # catch-all .ANY(+RW) region when a 222-byte queue exactly fills one of
        # SDK6's 222-byte exchange-memory gaps.
        text = text.replace(".ANY(free_area)", f".ANY({selector}, +FIRST)", 1)
    # Use a specific BSS selector for the generic application scratch section.
    # A final .ANY(free_area) still loses to ER_IROM3's earlier .ANY(+RW) after
    # LTO merges several contributors, while a non-BSS custom section also
    # emits hundreds of zero bytes and pins the OTA payload to this high
    # execution address. Keep this region entirely zero-initialized.
    # SDK6's bounded 140-byte non-retained boot heap also fits here after the
    # DA page-buffer adjustment. Its address remains visible through the ROM
    # configuration table, and the SDK derives the required RAM-bank retention
    # mode from that address at runtime.
    text = text.replace(
        ".ANY(free_area)",
        "*(.bss.inktag_free_area)\n        *(.bss.heap_mem_area_not_ret)\n"
        # Exact pure-ZI sections only: retain SDK RNG and the common runtime
        # model in the already-retained final bank; no payload tail or overlap.
        "        *(.bss.chacha20_state)\n        *(.bss.trng_bits)\n"
        "        *(.bss.s_runtime_model)",
        1,
    )
    text = text.replace(
        """    ER_NZI +0 UNINIT {
        *(.bss.heap_mem_area_not_ret)                   ; not retained HEAP
    }
""",
        "",
        1,
    )
    trace_anchor = "    ER_FREE_AREA FREE_AREA_BASE_ADDR FREE_AREA_SIZE {"
    if text.count(trace_anchor) != 1:
        raise RuntimeError("DA14585 reset-trace high-RAM anchor changed")
    text = text.replace(trace_anchor, """    ER_RESET_TRACE FREE_AREA_BASE_ADDR UNINIT 32 {
        *(.bss.inktag_reset_trace, +FIRST)
    }
    ER_FREE_AREA (FREE_AREA_BASE_ADDR + 32) (FREE_AREA_SIZE - 32) {""", 1)
    text += "\nScatterAssert(ImageLength(ER_RESET_TRACE) == 32)\n"
    target.write_text(text, encoding="utf-8")


def replace_once_or_verify(
    path: Path, old: str, new: str, *, previous: str | None = None
) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    if previous is not None and previous in text:
        if text.count(previous) != 1:
            raise RuntimeError(f"{path}: expected one previous DA14585 placement")
        path.write_text(text.replace(previous, new, 1), encoding="utf-8")
        return
    if text.count(old) != 1:
        raise RuntimeError(f"{path}: expected one DA14585 scratch placement anchor")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def restore_once_or_verify(path: Path, original: str, *patched_variants: str) -> None:
    """Restore an SDK declaration even when it appears inside the old patch."""
    text = path.read_text(encoding="utf-8")
    for patched in patched_variants:
        if patched in text:
            if text.count(patched) != 1:
                raise RuntimeError(f"{path}: expected one legacy DA14585 patch")
            path.write_text(text.replace(patched, original, 1), encoding="utf-8")
            return
    if original not in text:
        raise RuntimeError(f"{path}: expected canonical DA14585 declaration")


def mark_functions_noinline(path: Path, names) -> None:
    """Keep required DA frame boundaries for raw and already projected sources."""
    original = path.read_text(encoding="utf-8")
    text = original
    for name in names:
        pattern = (r"(?m)^(?:__attribute__\(\(noinline\)\) )?"
                   r"(static [^\n;{]*\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{)")
        text, count = re.subn(pattern, r"__attribute__((noinline)) \1", text)
        if count != 1:
            raise RuntimeError(f"{path}: expected one {name} definition")
    if text != original:
        path.write_text(text, encoding="utf-8")


def patch_da14585_scratch_placement(sdk: Path, project: Path) -> None:
    """Move bounded DA-only data without forking common feature code."""
    common = project / "src" / "inktag_common"
    scratch16 = ' __attribute__((section(".bss.inktag_free_area"), aligned(2)))'
    previous_scratch = ' __attribute__((section("free_area"), aligned(4)))'
    # With legacy 27-byte link-layer PDUs the BLE/retained boundary moves up
    # one RAM bank. Keep the 300-byte driver registry in the ordinary code
    # region so the smaller post-exchange free area remains available to the
    # non-retained boot heap and application scratch buffers.
    replace_once_or_verify(
        common / "inktag_gui_calendar.inc",
        "static uint16_t runtime_holidays[INKTAG_GUI_MAX_RUNTIME_HOLIDAYS];",
        "static uint16_t runtime_holidays[INKTAG_GUI_MAX_RUNTIME_HOLIDAYS]"
        f"{scratch16};",
    )
    # Keep the accepted shared pixels byte-identical while preventing the
    # device-information branch from inflating DrawFreeLayoutV1Page's single
    # worst-case stack frame under Arm Compiler 6 LTO.
    replace_once_or_verify(
        common / "inktag_gui_default.inc",
        "static void v1_device_info(Adafruit_GFX* gfx, const gui_data_t* data) {",
        "__attribute__((noinline)) static void v1_device_info("
        "Adafruit_GFX* gfx, const gui_data_t* data) {",
    )
    replace_once_or_verify(
        common / "inktag_gui_default.inc",
        "static void v1_clock(Adafruit_GFX* gfx, tm_t* time,",
        "__attribute__((noinline)) static void v1_clock(Adafruit_GFX* gfx, tm_t* time,",
    )
    # Share repeated font-metric and native-page helpers under ArmClang LTO.
    # Only function boundaries change; glyph/layout operations stay common.
    mark_functions_noinline(common / "inktag_gui_default.inc",
                            ("v1_festival", "v1_power_status"))
    replace_once_or_verify(
        common / "Adafruit_GFX.c",
        "static int16_t GFX_scaleFontMetric(",
        "__attribute__((noinline)) static int16_t GFX_scaleFontMetric(",
    )
    # The shared low-battery page is called from one mode arm. Keeping it out
    # of DrawGUIEx avoids ArmClang LTO duplicating its large layout frame into
    # the dispatcher while preserving the exact common drawing operations.
    replace_once_or_verify(
        common / "inktag_gui_default.inc",
        "static void v1_low_battery_text(Adafruit_GFX* gfx, int x, int baseline,",
        "__attribute__((noinline)) static void v1_low_battery_text("
        "Adafruit_GFX* gfx, int x, int baseline,",
    )
    replace_once_or_verify(
        common / "inktag_gui_default.inc",
        "static void v1_low_battery(Adafruit_GFX* gfx, const gui_data_t* data) {",
        "__attribute__((noinline)) static void v1_low_battery("
        "Adafruit_GFX* gfx, const gui_data_t* data) {",
    )
    # The independent renderer contains only Free brand behavior.
    # Use the current common factory-name parser. It validates IKT_/legacy
    # names and is also used by the full BLE label, so a second DA-only
    # replacement is neither necessary nor compatible with upstream updates.
    chacha_original = (
        'static chacha20_state_t chacha20_state_val '
        '__SECTION_ZERO("chacha20_state");'
    )
    restore_once_or_verify(
        sdk / "third_party" / "rand" / "chacha20.c",
        chacha_original,
        "#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n"
        'static chacha20_state_t chacha20_state_val '
        '__attribute__((section(".bss.inktag_free_area"), aligned(4)));\n'
        "#else\n"
        f"{chacha_original}\n"
        "#endif",
        (
            "#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n"
            f"static chacha20_state_t chacha20_state_val{previous_scratch};\n"
            "#else\n"
            f"{chacha_original}\n"
            "#endif"
        ),
    )
    spi_original = "static spi_flash_cfg_t spi_flash_cfg_env;"
    restore_once_or_verify(
        sdk / "sdk" / "platform" / "driver" / "spi_flash" / "spi_flash.c",
        spi_original,
        "#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n"
        'static spi_flash_cfg_t spi_flash_cfg_env '
        '__attribute__((section(".bss.inktag_free_area"), aligned(4)));\n'
        "#else\n"
        f"{spi_original}\n"
        "#endif",
        (
            "#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n"
            f"static spi_flash_cfg_t spi_flash_cfg_env{previous_scratch};\n"
            "#else\n"
            f"{spi_original}\n"
            "#endif"
        ),
    )
    # Keep the SDK's live non-retained heap at its configured 128 bytes plus
    # 12-byte allocator header. Older InkTag materializations enlarged this
    # array and incorrectly reused its live prefix as protocol scratch.
    non_ret_original = (
        'uint32_t rwip_heap_non_ret[RWIP_CALC_HEAP_LEN(RWIP_HEAP_NON_RET_SIZE_JT)]   '
        '__SECTION_ZERO("heap_mem_area_not_ret");'
    )
    restore_once_or_verify(
        sdk / "sdk" / "platform" / "arch" / "main" / "jump_table.c",
        non_ret_original,
        '#if defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n'
        'uint32_t rwip_heap_non_ret[105]   '
        '__SECTION_ZERO("heap_mem_area_not_ret");\n'
        '#else\n'
        'uint32_t rwip_heap_non_ret[RWIP_CALC_HEAP_LEN(RWIP_HEAP_NON_RET_SIZE_JT)]   '
        '__SECTION_ZERO("heap_mem_area_not_ret");\n'
        '#endif',
    )


def materialize_da14585_secondary_bootloader(repo: Path, sdk: Path) -> Path:
    bootloader = sdk / "utilities/secondary_bootloader"
    config = bootloader / "includes/da14585_config_basic.h"
    text = config.read_text(encoding="utf-8")
    text, uart_count = re.subn(
        r"#(?:define|undef) UART_SUPPORTED",
        "#undef UART_SUPPORTED",
        text,
        count=1,
    )
    text, dma_count = re.subn(
        r"#(?:define|undef) CFG_SPI_DMA_SUPPORT",
        "#undef CFG_SPI_DMA_SUPPORT",
        text,
        count=1,
    )
    if uart_count != 1 or dma_count != 1:
        raise SystemExit(f"Unable to configure the DA14585 secondary bootloader: {config}")
    config.write_text(text, encoding="utf-8")

    boot_source = bootloader / "src/bootloader.c"
    text = boot_source.read_text(encoding="utf-8")
    marker_anchor = """    imageOffsets[0] = ProductHeader->offset1;
    imageOffsets[1] = ProductHeader->offset2;

    // Get the active image index"""
    marker_patch = """    imageOffsets[0] = ProductHeader->offset1;
    imageOffsets[1] = ProductHeader->offset2;
#if defined(__DA14585__)
    /* A zero request byte selects fixed Recovery without damaging the current
     * application header. Recovery clears this sector only after safe resume
     * or after a signed candidate has been verified and durably armed. */
    FlashRead((unsigned long)flashbuffer, 0x00035008u, 1u);
    if (flashbuffer[0] == 0u) imageOffsets[0] = imageOffsets[1];
#endif

    // Get the active image index"""
    if marker_anchor not in text and marker_patch not in text:
        raise SystemExit(
            f"Unable to add DA14585 Recovery request selector: {boot_source}"
        )
    boot_source.write_text(text.replace(marker_anchor, marker_patch), encoding="utf-8")

    copy_file(
        repo / "app/inktag_secondary_crc32.c",
        bootloader / "src/inktag_secondary_crc32.c",
    )
    # uVision also caches the shared file list in .uvoptx. If only .uvprojx is
    # changed it compiles both the old table implementation and this one, even
    # during a rebuild. Keep the display/object name as crc32.c but redirect
    # the cached source path as well.
    replace_once_or_verify(
        bootloader / "secondary_bootloader.uvoptx",
        "..\\..\\third_party\\crc32\\crc32.c",
        ".\\src\\inktag_secondary_crc32.c",
    )

    scatter_name = "inktag_secondary_bootloader_585_armclang.sct"
    copy_file(
        repo / "app" / f"{scatter_name}.in",
        bootloader / "startup" / scatter_name,
    )
    project = bootloader / "secondary_bootloader.uvprojx"
    tree = ET.parse(project)
    root = tree.getroot()
    target = next(
        (item for item in root.findall(".//Target")
         if item.findtext("TargetName") == "DA14585"),
        None,
    )
    if target is None:
        raise SystemExit(f"DA14585 secondary-boot target is missing: {project}")
    defines = target.find(".//Cads/VariousControls/Define")
    if defines is None:
        raise SystemExit(f"DA14585 secondary-boot defines are missing: {project}")
    define_tokens = (defines.text or "").split()
    if "__DA14585__" not in define_tokens:
        define_tokens.append("__DA14585__")
        defines.text = " ".join(define_tokens)
    scatter = target.find(".//ScatterFile")
    if scatter is None:
        raise SystemExit(f"DA14585 secondary-boot scatter setting is missing: {project}")
    scatter.text = f".\\startup\\{scatter_name}"
    crc_sources = [
        item for item in root.findall(".//File")
        if item.findtext("FileName") in {"crc32.c", "inktag_secondary_crc32.c"}
    ]
    if not crc_sources:
        raise SystemExit(f"DA14585 secondary-boot CRC32 source is missing: {project}")
    # The .uvoptx file list is shared by every target in this vendor project.
    # Redirect every matching .uvprojx entry too; otherwise uVision combines
    # the cached DA14585 source with a sibling target's old path and compiles
    # two crc32 objects into the selected target.
    for crc_source in crc_sources:
        # Keep the original project file name so Keil reuses one crc32.o
        # identity across incremental materializations.
        crc_source.find("FileName").text = "crc32.c"
        crc_source.find("FilePath").text = ".\\src\\inktag_secondary_crc32.c"
    for option in target.findall(".//v6Lto"):
        option.text = "1"
    for option in target.findall(".//AdsLtoi"):
        option.text = "1"
    for option in target.findall(".//Optim"):
        option.text = "7"
    for option in target.findall(".//oTime"):
        option.text = "0"
    tree.write(project, encoding="utf-8", xml_declaration=True)
    return project


def patch_recovery_uvprojx(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = text.replace("ble_app_ota_585", "InkTag-DA14585-Recovery")
    text = text.replace(
        "-mthumb -c -include",
        "-mthumb -c -DINKTAG_DA_SINGLE_APP_SLOT=1 "
        "-DINKTAG_DA_FIXED_RECOVERY=1 -include",
    )
    text = text.replace("</IncludePath>", ";..\\src\\inktag_common</IncludePath>")
    path.write_text(text, encoding="utf-8")

    tree = ET.parse(path)
    root = tree.getroot()
    for option in root.findall(".//v6Lto"):
        option.text = "1"
    for option in root.findall(".//AdsLtoi"):
        option.text = "1"
    for option in root.findall(".//Optim"):
        if option.text == "0":
            option.text = "7"
    for file_node in root.findall(".//File"):
        if file_node.findtext("FileName") in (
            "app_suotar.c", "app_suotar_task.c"
        ):
            file_path = file_node.find("FilePath")
            if file_path is not None:
                file_path.text = "..\\src\\" + file_node.findtext("FileName")
    for target in root.findall(".//Target"):
        groups = target.find("Groups")
        if groups is None:
            continue
        group = ET.SubElement(groups, "Group")
        ET.SubElement(group, "GroupName").text = "inktag_recovery_common"
        add_file(group, "inktag_recovery.c", "..\\src\\inktag_common\\inktag_recovery.c")
        add_file(group, "inktag_da_recovery.c", "..\\src\\inktag_common\\inktag_da_recovery.c")
        add_file(group, "inktag_da_ota_security.c", "..\\src\\inktag_common\\inktag_da_ota_security.c")
        add_file(group, "inktag_power.c", "..\\src\\inktag_common\\inktag_power.c")
        add_file(group, "inktag_da_health.c", "..\\src\\inktag_common\\inktag_da_health.c")
        add_file(group, "inktag_ble_address.c", "..\\src\\inktag_common\\inktag_ble_address.c")
    for files in root.findall(".//Files"):
        for node in list(files):
            if node.findtext("FileName") in ("app_easy_crypto.c", "sha2.c"):
                files.remove(node)
    tree.write(path, encoding="utf-8", xml_declaration=True)


def patch_single_app_suotar(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    release_anchor = """    if( suota_state.mem_dev == SUOTAR_MEM_SPI_FLASH ) {
        spi_release();
    }"""
    release_patch = """    if( suota_state.mem_dev == SUOTAR_MEM_SPI_FLASH ) {
#if !INKTAG_DA_SINGLE_APP_SLOT
        spi_release();
#endif
        /* Fixed single-slot Recovery still needs external Flash after a
         * rejected/closed session for resume-app and the pP handoff. */
    }"""
    if release_anchor not in text and release_patch not in text:
        raise SystemExit(f"Unable to retain DA14585 Recovery SPI state: {path}")
    text = text.replace(release_anchor, release_patch, 1)

    reset_anchor = """        // Reboot now since peer has already disconnected
        platform_reset(RESET_AFTER_SUOTA_UPDATE);"""
    reset_previous = """        /* DA14585 may preserve the current SRAM remap across a
         * software reset. Reload the packaged pP selector so the durable
         * marker deterministically selects application or fixed Recovery.
         * If its bounded Flash preflight fails, stay in connectable Recovery;
         * platform_reset() is unsafe while address zero maps this SRAM image. */
        (void)inktag_da_restart_via_secondary_bootloader();"""
    reset_patch = """        /* Keep the established post-disconnect pP handoff for
         * native SUOTA finalization. Its bounded loader preflight can fail
         * back to connectable Recovery. Safe resume separately uses the SDK
         * ROM-mapped reset after its ACK-confirmed main-loop validation. */
        (void)inktag_da_restart_via_secondary_bootloader();"""
    if reset_anchor not in text and reset_patch not in text and reset_previous not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA pP handoff: {path}")
    text = text.replace(reset_anchor, reset_patch, 1)
    text = text.replace(reset_previous, reset_patch, 1)

    size_check = """    // verify image positions
    // the code size must be less than the space between images
    if (!codesize || codesize > (imageposition[1] - imageposition[0] - sizeof(image_header_t)))
    {
        return SUOTAR_INVAL_IMG_SIZE;
    }"""
    single_size_check = """    // Fixed Recovery occupies bank 2. Bank 1 is the only writable
    // application. The external 96 KiB pQ extent is larger than the safe
    // boot-time copy span because the secondary loader must coexist in SRAM.
#if INKTAG_DA_SINGLE_APP_SLOT
    if (!codesize || codesize > INKTAG_DA_APPLICATION_RAW_LIMIT)
#else
    if (!codesize || codesize > (imageposition[1] - imageposition[0] - sizeof(image_header_t)))
#endif
    {
        return SUOTAR_INVAL_IMG_SIZE;
    }"""
    if size_check not in text and single_size_check not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA image-size check: {path}")
    text = text.replace(size_check, single_size_check)

    product_positions = """    // read product header
    pProductHeader = (product_header_t*) mem_data_buff;

    ret = app_read_ext_mem( (uint8_t*)mem_data_buff, (unsigned long)PRODUCT_HEADER_POSITION, (unsigned long)sizeof(product_header_t) );
    if(ret < 0) return SUOTAR_EXT_MEM_READ_ERR;

    // verify product header
    if( (pProductHeader->signature[0] != PRODUCT_HEADER_SIGNATURE1) ||
        (pProductHeader->signature[1] != PRODUCT_HEADER_SIGNATURE2)    )
    {
        return SUOTAR_INVAL_PRODUCT_HDR;
    }

    // Store the start addresses of both images positions
    imageposition[0] = pProductHeader->offset1;
    imageposition[1] = pProductHeader->offset2;"""
    fixed_product_positions = """#if INKTAG_DA_SINGLE_APP_SLOT
    /* The factory product header is not an OTA trust boundary. Never derive a
     * destructive address from its mutable offsets: this Recovery can write
     * only the immutable application window compiled into the image. */
    (void)pProductHeader;
    imageposition[0] = INKTAG_DA_APPLICATION_START;
    imageposition[1] = INKTAG_DA_RECOVERY_START;
#else
""" + product_positions + """
#endif"""
    if product_positions not in text and fixed_product_positions not in text:
        raise SystemExit(f"Unable to fix DA14585 SUOTA application positions: {path}")
    text = text.replace(product_positions, fixed_product_positions)

    selection = """#if SUOTAR_ALLOW_BANK_SELECTION
    if (image_bank == ANY_IMAGE_BANK ||  image_bank > SECOND_IMAGE_BANK)
#endif
    {
        new_bank = oldest_img_idx + 1;
    }
#if SUOTAR_ALLOW_BANK_SELECTION
    else
    {
        new_bank = image_bank;
    }
#endif"""
    single_selection = """#if INKTAG_DA_SINGLE_APP_SLOT
    // Bank 2 is immutable Recovery. Only Recovery exposes SUOTA and it may
    // replace bank 1; the normal application only invalidates bank 1 and
    // resets so the stock secondary Boot selects Recovery.
    (void)image_bank;
    new_bank = 1; /* SDK6 bank number for product-header offset1. */
#else
#if SUOTAR_ALLOW_BANK_SELECTION
    if (image_bank == ANY_IMAGE_BANK ||  image_bank > SECOND_IMAGE_BANK)
#endif
    {
        new_bank = oldest_img_idx + 1;
    }
#if SUOTAR_ALLOW_BANK_SELECTION
    else
    {
        new_bank = image_bank;
    }
#endif
#endif"""
    if selection not in text and single_selection not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA bank selection: {path}")
    text = text.replace(selection, single_selection)

    same_image_check = """#if SUOTAR_DO_NOT_ALLOW_SAME_IMAGE
    if (is_invalid_image[img_idx]==IMAGE_HEADER_SAME_VERSION) return SUOTAR_SAME_IMG_ERR;
#endif"""
    single_same_image_check = """#if SUOTAR_DO_NOT_ALLOW_SAME_IMAGE && !INKTAG_DA_SINGLE_APP_SLOT
    if (is_invalid_image[img_idx]==IMAGE_HEADER_SAME_VERSION) return SUOTAR_SAME_IMG_ERR;
#endif
    /* A signed single-slot application may be reinstalled for recovery. The
     * authenticated compatibility epoch, not the SDK version string, is the
     * device-side rollback boundary. */"""
    if same_image_check not in text and single_same_image_check not in text:
        raise SystemExit(f"Unable to allow DA14585 signed same-image recovery: {path}")
    text = text.replace(same_image_check, single_same_image_check, 1)

    version_check = """    if (user_app_suotar_cb.on_suotar_version_check != NULL) {
        SUOTAR_IMG_VERSION_CHECK version_check_stat;
""" + (" " * 8) + """
        // Get the newest flash image's header in order to get the version
        ret = app_read_ext_mem( (uint8_t*)pImageHeader, (unsigned long)imageposition[newest_img_idx], (unsigned long)sizeof(image_header_t) );
        if(ret < 0) return SUOTAR_EXT_MEM_READ_ERR;

        // Call the user callback with the active image version string and the incoming image's version string
        version_check_stat = user_app_suotar_cb.on_suotar_version_check(pImageHeader->version, pfwHeader->version);

        // If the version string checking status is not OK, return
        if (version_check_stat != SUOTAR_IMG_VERSION_CHECK_OK) {
            return version_check_stat;
        }
    }"""
    single_version_check = """#if INKTAG_DA_SINGLE_APP_SLOT
    // Recovery is a permanent transport, not an application generation. Its
    // version cannot be used for application anti-rollback, and accepting a
    // previously saved valid application is required for interrupted recovery.
    (void)newest_img_idx;
#else
""" + version_check + """
#endif"""
    if version_check not in text and single_version_check not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA version policy: {path}")
    text = text.replace(version_check, single_version_check)

    variable_mem_base = "    suota_state.mem_base_add = imageposition[img_idx];"
    fixed_mem_base = """#if INKTAG_DA_SINGLE_APP_SLOT
    suota_state.mem_base_add = INKTAG_DA_APPLICATION_START;
#else
    suota_state.mem_base_add = imageposition[img_idx];
#endif"""
    if variable_mem_base not in text and fixed_mem_base not in text:
        raise SystemExit(f"Unable to fix DA14585 SUOTA write base: {path}")
    text = text.replace(variable_mem_base, fixed_mem_base)

    image_id = """    if (is_invalid_image[!img_idx] == IMAGE_HEADER_OK || is_invalid_image[!img_idx] == IMAGE_HEADER_SAME_VERSION)
        imageid = app_get_image_id(imageid_x[!img_idx]);
    else
        imageid = IMAGE_ID_1;"""
    single_image_id = """#if INKTAG_DA_SINGLE_APP_SLOT
    // Recovery is generation 0 and the application is always generation 1,
    // so the stock secondary Boot selects the application whenever it is valid.
    imageid = IMAGE_ID_1;
#else
    if (is_invalid_image[!img_idx] == IMAGE_HEADER_OK || is_invalid_image[!img_idx] == IMAGE_HEADER_SAME_VERSION)
        imageid = app_get_image_id(imageid_x[!img_idx]);
    else
        imageid = IMAGE_ID_1;
#endif"""
    if image_id not in text and single_image_id not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA image generation: {path}")
    text = text.replace(image_id, single_image_id)
    if '#include "inktag_da_health.h"' not in text:
        first_include = text.index("#include")
        line_end = text.index("\n", first_include)
        text = text[:line_end + 1] + '#include "inktag_da_health.h"\n' + text[line_end + 1:]
    if '#include "inktag_da_ota_security.h"' not in text:
        first_include = text.index("#include")
        line_end = text.index("\n", first_include)
        text = text[:line_end + 1] + '#include "inktag_da_ota_security.h"\n' + text[line_end + 1:]

    header_check = """    // Get image size
    codesize = pfwHeader->code_size;
    suota_state.suota_image_len = pfwHeader->code_size+sizeof(image_header_t);"""
    signed_header_check = """    // Get image size and reject missing/wrong publisher metadata or
    // a retired compatibility epoch before the first single-slot erase.
    codesize = pfwHeader->code_size;
    suota_state.suota_image_len = pfwHeader->code_size+sizeof(image_header_t);
#if INKTAG_DA_SINGLE_APP_SLOT
    if (!inktag_da_ota_preflight(data, data_len))
        return SUOTAR_INVAL_IMG_HDR;
#endif"""
    if header_check not in text and signed_header_check not in text:
        raise SystemExit(f"Unable to add DA14585 signed preflight: {path}")
    text = text.replace(header_check, signed_header_check)

    entry_marker = """    uint8_t newest_img_idx, oldest_img_idx;

    if( data_len < sizeof(image_header_t) )"""
    guarded_entry = """    uint8_t newest_img_idx, oldest_img_idx;

    // Refuse the transaction before any application-sector erase or write.
    if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, true)) {
        inktag_da_ota_reject_power(
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
            INKTAG_DA_APPLICATION_START,
            INKTAG_POWER_POLICY_OTA_MIN_MV, true);
        return SUOTAR_EXT_MEM_WRITE_ERR;
    }

    if( data_len < sizeof(image_header_t) )"""
    if entry_marker not in text and guarded_entry not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA power entry gate: {path}")
    text = text.replace(entry_marker, guarded_entry)

    mutation_anchor = """    // Erase header and image
    if( suota_state.mem_dev == SUOTAR_IMG_SPI_FLASH )
    {
#if (!SUOTAR_SPI_DISABLE)
"""
    mutation_patch = """    // Erase header and image
    if( suota_state.mem_dev == SUOTAR_IMG_SPI_FLASH )
    {
#if (!SUOTAR_SPI_DISABLE)
#if INKTAG_DA_SINGLE_APP_SLOT
    /* This durable marker is the safe-resume boundary. It must reach Flash
     * before the first application-sector erase. */
    if (!inktag_da_recovery_mark_mutated()) {
        inktag_da_ota_reject_power(
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_DA_APPLICATION_START,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        return SUOTAR_EXT_MEM_WRITE_ERR;
    }
    (void)spi_flash_release_from_power_down();
#endif
"""
    if mutation_anchor not in text and mutation_patch not in text:
        raise SystemExit(f"Unable to add DA14585 durable mutation marker: {path}")
    text = text.replace(mutation_anchor, mutation_patch)

    erase_marker = """    for (i = 0; i < sector; i++)
    {
        if (scheduler_en)"""
    guarded_erase = """    for (i = 0; i < sector; i++)
    {
        // Keep erase-load samples out of the stable policy value.
        if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
            inktag_da_ota_reject_power(
                INKTAG_OTA_REJECT_STAGE_ERASE, starting_sector,
                INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
            return -1;
        }
        if (scheduler_en)"""
    if erase_marker not in text and guarded_erase not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA erase power gate: {path}")
    text = text.replace(erase_marker, guarded_erase)

    erase_function = """    uint32_t timeout_cnt;

    // Calculate the starting sector offset"""
    guarded_erase_function = """    uint32_t timeout_cnt;

#if INKTAG_DA_SINGLE_APP_SLOT
    if (!size || starting_address < INKTAG_DA_APPLICATION_START ||
        starting_address >= INKTAG_DA_RECOVERY_START ||
        size > INKTAG_DA_RECOVERY_START - starting_address) {
        inktag_da_ota_reject_set_default(
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            starting_address);
        return -1;
    }
#endif

    // Calculate the starting sector offset"""
    if erase_function not in text and guarded_erase_function not in text:
        raise SystemExit(f"Unable to bound DA14585 SUOTA erase window: {path}")
    text = text.replace(erase_function, guarded_erase_function)

    write_marker = """    return spi_flash_write_data(data, address, size, &actual_size);"""
    guarded_write = """    // Keep program-load samples out of the stable policy value.
    if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        inktag_da_ota_reject_power(
            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE, address,
            INKTAG_POWER_POLICY_FLASH_MIN_MV, true);
        return -1;
    }
    return spi_flash_write_data(data, address, size, &actual_size);"""
    if write_marker not in text and guarded_write not in text:
        raise SystemExit(f"Unable to patch DA14585 SUOTA write power gate: {path}")
    text = text.replace(write_marker, guarded_write)

    write_function = """    uint32_t actual_size;

#if (SUOTAR_SPI_DELATE_SECTOR_ERASE)"""
    guarded_write_function = """    uint32_t actual_size;

#if INKTAG_DA_SINGLE_APP_SLOT
    if (address < INKTAG_DA_APPLICATION_START ||
        address >= INKTAG_DA_RECOVERY_START ||
        size > INKTAG_DA_RECOVERY_START - address) {
        inktag_da_ota_reject_set_default(
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, address);
        return -1;
    }
#endif

#if (SUOTAR_SPI_DELATE_SECTOR_ERASE)"""
    if write_function not in text and guarded_write_function not in text:
        raise SystemExit(f"Unable to bound DA14585 SUOTA program window: {path}")
    text = text.replace(write_function, guarded_write_function)

    erase_result = """    if( ret != SPI_FLASH_ERR_OK) return SUOTAR_EXT_MEM_WRITE_ERR;"""
    diagnosed_erase_result = """    if( ret != SPI_FLASH_ERR_OK) {
        inktag_da_ota_reject_set_default(
            INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            suota_state.mem_base_add);
        return SUOTAR_EXT_MEM_WRITE_ERR;
    }"""
    if erase_result not in text and diagnosed_erase_result not in text:
        raise SystemExit(f"Unable to diagnose DA14585 SUOTA erase failure: {path}")
    text = text.replace(erase_result, diagnosed_erase_result, 1)

    block_write_result = """                    if(ret < 0){
                        status = SUOTAR_EXT_MEM_WRITE_ERR;
                    }"""
    diagnosed_block_write_result = """                    if(ret < 0){
                        inktag_da_ota_reject_set_default(
                            INKTAG_OTA_REJECT_REASON_WRITE_FAILED,
                            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
                            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                            suota_state.mem_base_add + suota_state.suota_img_idx);
                        status = SUOTAR_EXT_MEM_WRITE_ERR;
                    }"""
    if block_write_result not in text and diagnosed_block_write_result not in text:
        raise SystemExit(f"Unable to diagnose DA14585 SUOTA block write: {path}")
    text = text.replace(block_write_result, diagnosed_block_write_result, 1)

    header_write_result = """        if(ret < 0) return SUOTAR_EXT_MEM_WRITE_ERR;

        ret = app_flash_write_data((uint8_t*) &data[CODE_OFFSET], suota_state.mem_base_add + CODE_OFFSET, data_len - CODE_OFFSET);
        if(ret < 0) return SUOTAR_EXT_MEM_WRITE_ERR;"""
    diagnosed_header_write_result = """        if(ret < 0) {
            inktag_da_ota_reject_set_default(
                INKTAG_OTA_REJECT_REASON_WRITE_FAILED,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                suota_state.mem_base_add);
            return SUOTAR_EXT_MEM_WRITE_ERR;
        }

        ret = app_flash_write_data((uint8_t*) &data[CODE_OFFSET], suota_state.mem_base_add + CODE_OFFSET, data_len - CODE_OFFSET);
        if(ret < 0) {
            inktag_da_ota_reject_set_default(
                INKTAG_OTA_REJECT_REASON_WRITE_FAILED,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                suota_state.mem_base_add + CODE_OFFSET);
            return SUOTAR_EXT_MEM_WRITE_ERR;
        }"""
    if header_write_result not in text and diagnosed_header_write_result not in text:
        raise SystemExit(f"Unable to diagnose DA14585 SUOTA header write: {path}")
    text = text.replace(header_write_result, diagnosed_header_write_result, 1)

    if '#include "inktag_da_recovery.h"' not in text:
        first_include = text.index("#include")
        line_end = text.index("\n", first_include)
        text = text[:line_end + 1] + '#include "inktag_da_recovery.h"\n' + text[line_end + 1:]
    valid_flag_marker = """        app_suotar_spi_config(&spi_conf);
        app_spi_flash_init(&spi_conf.cs);
        ret = app_flash_write_data((uint8_t*) &flag, suota_state.mem_base_add + 2, sizeof(uint8_t));
        if (ret == 0)
            ret = SUOTAR_CMP_OK;"""
    guarded_valid_flag = """        app_suotar_spi_config(&spi_conf);
        app_spi_flash_init(&spi_conf.cs);
        /* Arm only after the complete payload and XOR check have passed, but
         * before making the pQ header bootable. The SUOTA final CMP_OK is
         * therefore a verified/committed handoff, never product success. */
        if (!inktag_da_ota_verify_and_arm())
            return SUOTAR_EXT_MEM_WRITE_ERR;
        /* The journal adapter powers the external Flash down when it closes
         * its transaction. Re-open it before programming the pQ valid flag. */
        app_spi_flash_init(&spi_conf.cs);
        ret = app_flash_write_data((uint8_t*) &flag, suota_state.mem_base_add + 2, sizeof(uint8_t));
        if (ret == 0)
            ret = SUOTAR_CMP_OK;
        else
            inktag_da_ota_reject_set_default(
                INKTAG_OTA_REJECT_REASON_WRITE_FAILED,
                INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                suota_state.mem_base_add + 2u);"""
    if valid_flag_marker not in text and guarded_valid_flag not in text:
        raise SystemExit(f"Unable to patch DA14585 Recovery trial arm point: {path}")
    text = text.replace(valid_flag_marker, guarded_valid_flag)

    resume_anchor = """        case SUOTAR_REBOOT:
        {
            suotar_conditional_reboot();"""
    resume_patch = """#if INKTAG_DA_SINGLE_APP_SLOT
        case 0xFC: /* InkTag safe resume-app command. */
        {
            /* Only the guarded application-task dispatch may accept resume.
             * Direct native reboot/legacy dispatch is never a substitute. */
            suotar_send_status_update_req((uint8_t)SUOTAR_INVAL_MEM_TYPE);
            break;
        }
#endif

        case SUOTAR_REBOOT:
        {
            suotar_conditional_reboot();"""
    if resume_anchor not in text and resume_patch not in text:
        raise SystemExit(f"Unable to add DA14585 safe resume command: {path}")
    text = text.replace(resume_anchor, resume_patch)

    status_anchor = """    // req->conhdl = active_conhdl;
    req->status = status;

    // Send the message"""
    status_previous = """    // req->conhdl = active_conhdl;
#if INKTAG_DA_SINGLE_APP_SLOT
    req->length = inktag_da_ota_status_encode(
        status, req->status, sizeof(req->status));
#else
    req->length = 1u;
    req->status[0] = status;
#endif

    // Send the message"""
    status_patch = status_previous.replace(
        "#if INKTAG_DA_SINGLE_APP_SLOT\n",
        "    req->inktag_resume_sequence = 0u;\n"
        "#if INKTAG_DA_SINGLE_APP_SLOT\n"
        "    req->inktag_resume_sequence = inktag_da_safe_resume_status_sequence(status);\n",
        1,
    )
    if status_anchor not in text and status_patch not in text and status_previous not in text:
        raise SystemExit(f"Unable to extend DA14585 SUOTA status payload: {path}")
    text = text.replace(status_anchor, status_patch)
    text = text.replace(status_previous, status_patch)
    declaration = "extern uint16_t inktag_da_safe_resume_status_sequence(uint8_t status);\n"
    if declaration not in text:
        text = text.replace('#include "inktag_da_ota_security.h"\n',
            '#include "inktag_da_ota_security.h"\n' + declaration, 1)

    status_function_anchor = """void suotar_send_status_update_req( uint8_t status )
{
    // Inform SUOTAR task."""
    status_function_patch = """void suotar_send_status_update_req( uint8_t status )
{
#if INKTAG_DA_SINGLE_APP_SLOT
    /* A disconnect clears the SDK status attribute. Keep the structured
     * rejection pending while offline; user_app_connection republishes it
     * after profile cleanup has completed on the next Recovery connection. */
    if (app_env[0].conidx == GAP_INVALID_CONIDX) return;
#endif
    // Inform SUOTAR task."""
    if status_function_anchor not in text and status_function_patch not in text:
        raise SystemExit(
            f"Unable to retain DA14585 offline rejection status: {path}"
        )
    text = text.replace(status_function_anchor, status_function_patch, 1)

    reboot_anchor = """#if SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS
                // Start the reboot timer
                if(suota_state.suota_reboot_timer_hnd != EASY_TIMER_INVALID_TIMER) {
                    app_easy_timer_cancel(suota_state.suota_reboot_timer_hnd);
                    suota_state.suota_reboot_timer_hnd = EASY_TIMER_INVALID_TIMER;
                }
                suota_state.suota_reboot_timer_hnd = app_easy_timer(MS_TO_TIMERUNITS(SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS), suotar_reboot_timer_callback);
#endif // SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS"""
    reboot_patch = """#if SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS
                /* Keep a rejected candidate in Recovery so the host can read
                 * the full rejection tail and retry without racing a reset. */
                if (ret == SUOTAR_CMP_OK) {
                    if(suota_state.suota_reboot_timer_hnd != EASY_TIMER_INVALID_TIMER) {
                        app_easy_timer_cancel(suota_state.suota_reboot_timer_hnd);
                        suota_state.suota_reboot_timer_hnd = EASY_TIMER_INVALID_TIMER;
                    }
                    suota_state.suota_reboot_timer_hnd = app_easy_timer(MS_TO_TIMERUNITS(SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS), suotar_reboot_timer_callback);
                }
#endif // SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS"""
    if reboot_anchor not in text and reboot_patch not in text:
        raise SystemExit(f"Unable to guard DA14585 failed-finalize reboot: {path}")
    text = text.replace(reboot_anchor, reboot_patch, 1)
    path.write_text(text, encoding="utf-8")


def patch_deferred_da14585_finalize(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    function_anchor = """/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

#if SUOTAR_SECURE_LOCK_FEATURE"""
    function_patch = """/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */

#if INKTAG_DA_SINGLE_APP_SLOT
#include "inktag_da_recovery.h"
extern bool inktag_da_safe_resume_busy(void);
extern void inktag_da_safe_resume_request(uint32_t command, bool finalize_pending);

static uint32_t s_inktag_finalize_command;

bool inktag_da_finalize_pending_after_disconnect(void)
{
    uint32_t raw_size;
    uint32_t crc32;
    uint32_t mem_info = 0u;
    uint32_t command = s_inktag_finalize_command;
    if (command == 0u) return false;
    s_inktag_finalize_command = 0u;
    app_suotar_read_mem(command, &mem_info);
    /* There is no peer for a memory-info notification now. The status update
     * queued by app_suotar_read_mem() still updates the Recovery attribute so
     * a reconnect can read the complete retained rejection diagnostic. */
    if (!inktag_da_application_info(&raw_size, &crc32))
        suota_state.reboot_requested = 0u;
    return true;
}

static void inktag_da_finalize_after_att_response(void)
{
    if (app_env[0].conidx == GAP_INVALID_CONIDX) {
        (void)inktag_da_finalize_pending_after_disconnect();
        return;
    }
    /* P-256 and the journal scan take seconds. Disconnect after the ATT write
     * response, before that blocking work, so controller events cannot exhaust
     * the tiny SDK6 kernel heap while finalization owns the Cortex-M0. */
    app_easy_gap_disconnect(app_env[0].conidx);
}
#endif

#if SUOTAR_SECURE_LOCK_FEATURE"""
    if function_anchor not in text and function_patch not in text:
        raise SystemExit(f"Unable to add deferred DA14585 finalize callback: {path}")
    text = text.replace(function_anchor, function_patch, 1)

    declaration_anchor = """    uint32_t mem_info = 0;
#if SUOTAR_SECURE_LOCK_FEATURE
    uint8_t mem_dev_cmd = (uint8_t) ((param->mem_dev & SUOTAR_READ_MEM_DEV_TYPE) >> 24);"""
    declaration_patch = """    uint32_t mem_info = 0;
#if INKTAG_DA_SINGLE_APP_SLOT || SUOTAR_SECURE_LOCK_FEATURE
    uint8_t mem_dev_cmd = (uint8_t) ((param->mem_dev & SUOTAR_READ_MEM_DEV_TYPE) >> 24);
#endif
#if INKTAG_DA_SINGLE_APP_SLOT
    if (param->char_code && mem_dev_cmd == 0xFCu) {
        inktag_da_safe_resume_request(param->mem_dev,
            s_inktag_finalize_command != 0u);
        return KE_MSG_CONSUMED;
    }
    if (inktag_da_safe_resume_busy()) {
        suotar_send_status_update_req((uint8_t)SUOTAR_LOCKED_ERR);
        return KE_MSG_CONSUMED;
    }
#endif
#if SUOTAR_SECURE_LOCK_FEATURE"""
    if declaration_anchor not in text and declaration_patch not in text:
        raise SystemExit(f"Unable to share DA14585 SUOTA command decode: {path}")
    text = text.replace(declaration_anchor, declaration_patch, 1)

    data_anchor = """    if( suota_state.mem_dev < SUOTAR_MEM_INVAL_DEV )"""
    data_patch = """#if INKTAG_DA_SINGLE_APP_SLOT
    if (inktag_da_safe_resume_busy()) {
        suotar_send_status_update_req((uint8_t)SUOTAR_LOCKED_ERR);
        return KE_MSG_CONSUMED;
    }
#endif
    if( suota_state.mem_dev < SUOTAR_MEM_INVAL_DEV )"""
    if data_anchor not in text and data_patch not in text:
        raise SystemExit(f"Unable to guard DA14585 resume/WRITE race: {path}")
    if data_patch not in text:
        text = text.replace(data_anchor, data_patch, 1)

    # GPIO/PATCH_LEN also mutate native transfer state. Freeze all four SDK
    # handlers while the accepted safe-resume owns the disconnect boundary.
    for name in ("suotar_gpio_map_ind_handler", "suotar_patch_len_ind_handler"):
        start = text.index("static int " + name)
        body = text.index("{", start) + 1
        guard = """
#if INKTAG_DA_SINGLE_APP_SLOT
    if (inktag_da_safe_resume_busy()) {
        suotar_send_status_update_req((uint8_t)SUOTAR_LOCKED_ERR);
        return KE_MSG_CONSUMED;
    }
#endif
"""
        if not text[body:].startswith(guard):
            text = text[:body] + guard + text[body:]

    dispatch_anchor = """    if (param->char_code)
    {
        app_suotar_read_mem(param->mem_dev, &mem_info);"""
    dispatch_patch = """    if (param->char_code)
    {
#if INKTAG_DA_SINGLE_APP_SLOT
        /* The profile has already queued the ATT write confirmation, but the
         * application task must yield once before the multi-second P-256 and
         * journal scan so the controller can actually transmit it. */
        if (mem_dev_cmd == SUOTAR_IMG_END) {
            if (s_inktag_finalize_command != 0u) {
                suotar_send_status_update_req((uint8_t) SUOTAR_LOCKED_ERR);
                return KE_MSG_CONSUMED;
            }
            s_inktag_finalize_command = param->mem_dev;
            (void)app_easy_timer(1u, inktag_da_finalize_after_att_response);
            return KE_MSG_CONSUMED;
        }
#endif
        app_suotar_read_mem(param->mem_dev, &mem_info);"""
    if dispatch_anchor not in text and dispatch_patch not in text:
        raise SystemExit(f"Unable to defer DA14585 SUOTA finalization: {path}")
    text = text.replace(dispatch_anchor, dispatch_patch, 1)
    path.write_text(text, encoding="utf-8")


def patch_da14585_suota_profile(sdk: Path) -> None:
    task_header = sdk / "sdk/ble_stack/profiles/suota/suotar/api/suotar_task.h"
    text = task_header.read_text(encoding="utf-8")
    old_struct = """    /// SUOTAR Status
    uint8_t status;
};"""
    previous_struct = """    /// SUOTAR status followed by an optional InkTag rejection tail.
    uint8_t status[15];
    uint8_t length;
};"""
    new_struct = previous_struct.replace("    uint8_t length;\n", "    uint8_t length;\n"
        "    /// Internal correlation only; never part of the status payload.\n"
        "    uint16_t inktag_resume_sequence;\n")
    if old_struct not in text and new_struct not in text and previous_struct not in text:
        raise SystemExit(f"Unable to extend DA14585 SUOTA status message: {task_header}")
    task_header.write_text(text.replace(old_struct, new_struct).replace(previous_struct, new_struct), encoding="utf-8")

    profile = sdk / "sdk/ble_stack/profiles/suota/suotar/src/suotar.c"
    text = profile.read_text(encoding="utf-8")
    version_anchor = """    uint8_t version = SUOTA_VERSION;
    uint16_t pd_char_size = SUOTA_PD_CHAR_SIZE;
    //Set SUOTA version into DB
    attmdb_att_set_value(suota_shdl + SUOTA_IDX_VERSION_VAL,
                         sizeof(uint8_t),
                         0,
                         &version);"""
    version_patch = """#if defined(INKTAG_DA_FIXED_RECOVERY)
    /* Native metadata binding of the public SAFE_RESUME_APP capability.
     * Keep SUOTA version13; do not pretend this is a newer vendor protocol. */
    #include "inktag_da_ota_policy.h"
    uint8_t version[] = {SUOTA_VERSION, 0x49u, 0x4Bu, 1u, 1u,
        0x53u, 0x50u, 1u, INKTAG_DA_OTA_POLICY_CODE};
#else
    uint8_t version[] = {SUOTA_VERSION};
#endif
    uint16_t pd_char_size = SUOTA_PD_CHAR_SIZE;
    //Set SUOTA version into DB
    attmdb_att_set_value(suota_shdl + SUOTA_IDX_VERSION_VAL,
                         sizeof(version),
                         0,
                         version);"""
    previous_version_patch = version_patch.replace(
        '#include "inktag_da_ota_policy.h"\n    uint8_t version[] = {SUOTA_VERSION, 0x49u, 0x4Bu, 1u, 1u,\n'
        '        0x53u, 0x50u, 1u, INKTAG_DA_OTA_POLICY_CODE};',
        'uint8_t version[] = {SUOTA_VERSION, 0x49u, 0x4Bu, 1u, 1u};')
    if not any(v in text for v in (version_anchor, previous_version_patch, version_patch)):
        raise SystemExit(f"Unable to bind read-only DA14585 resume capability: {profile}")
    text = text.replace(version_anchor, version_patch, 1).replace(previous_version_patch, version_patch, 1)
    version_max_anchor = """            .uuid = SUOTA_VERSION_UUID,
            .perm = PERM(RD, ENABLE) | PERM_VAL(UUID_LEN, PERM_UUID_128),
            .max_len = sizeof(uint8_t) };"""
    version_max_patch = """            .uuid = SUOTA_VERSION_UUID,
            .perm = PERM(RD, ENABLE) | PERM_VAL(UUID_LEN, PERM_UUID_128),
#if defined(INKTAG_DA_FIXED_RECOVERY)
            .max_len = 9u };
#else
            .max_len = sizeof(uint8_t) };
#endif"""
    previous_max_patch = version_max_patch.replace('.max_len = 9u', '.max_len = 5u')
    if not any(v in text for v in (version_max_anchor, previous_max_patch, version_max_patch)):
        raise SystemExit(f"Unable to size DA14585 resume metadata: {profile}")
    text = text.replace(version_max_anchor, version_max_patch, 1).replace(previous_max_patch, version_max_patch, 1)
    old_max = """            .perm = PERM(RD, ENABLE) | PERM(NTF, ENABLE) | \\
                    PERM_VAL(UUID_LEN, PERM_UUID_128),
            .max_len = sizeof(uint8_t) };"""
    new_max = """            .perm = PERM(RD, ENABLE) | PERM(NTF, ENABLE) | \\
                    PERM_VAL(UUID_LEN, PERM_UUID_128),
            .max_len = 15u };"""
    if old_max not in text and new_max not in text:
        raise SystemExit(f"Unable to extend DA14585 SUOTA status attribute: {profile}")
    profile.write_text(text.replace(old_max, new_max), encoding="utf-8")

    task = sdk / "sdk/ble_stack/profiles/suota/suotar/src/suotar_task.c"
    text = task.read_text(encoding="utf-8")
    old_handler = """    // Update the value in the attribute database
    uint8_t status = attmdb_att_set_value(suotar_env->suota_shdl + SUOTA_IDX_PATCH_STATUS_VAL,
                                  sizeof(uint8_t), 0, (uint8_t *) &param->status);
    ASSERT_ERROR(status == ATT_ERR_NO_ERROR);"""
    new_handler = """    // Update the value in the attribute database.
    uint8_t status = attmdb_att_set_value(suotar_env->suota_shdl + SUOTA_IDX_PATCH_STATUS_VAL,
                                  param->length, 0, (uint8_t *) param->status);
    ASSERT_ERROR(status == ATT_ERR_NO_ERROR);"""
    if old_handler not in text and new_handler not in text:
        raise SystemExit(f"Unable to extend DA14585 SUOTA status handler: {task}")
    text = text.replace(old_handler, new_handler)
    text = text.replace(
        "sizeof(param->status));\n\n        // Fill in the parameter structure",
        "param->length);\n\n        // Fill in the parameter structure",
        1,
    )
    text = text.replace(
        "req->length = sizeof(param->status);\n        memcpy(req->value, &param->status, req->length);",
        "req->length = param->length;\n        memcpy(req->value, param->status, req->length);",
        1,
    )
    text = text.replace("req->seq_num = 0;", "req->seq_num = param->inktag_resume_sequence;", 1)
    completion = """#if defined(INKTAG_DA_FIXED_RECOVERY)
extern void inktag_da_safe_resume_tx_complete(uint8_t connection_idx,
                                             uint16_t sequence, bool success);
static int inktag_resume_status_cmp_evt_handler(ke_msg_id_t const msgid,
    struct gattc_cmp_evt const *param, ke_task_id_t const dest_id,
    ke_task_id_t const src_id)
{
    (void)msgid;
    (void)dest_id;
    if (param->operation == GATTC_NOTIFY || param->operation == GATTC_INDICATE)
        inktag_da_safe_resume_tx_complete(KE_IDX_GET(src_id), param->seq_num,
                                          param->status == GAP_ERR_NO_ERROR);
    return KE_MSG_CONSUMED;
}
#endif

"""
    if completion not in text:
        anchor = "/// Default State handler definition."
        if anchor not in text:
            raise SystemExit(f"Unable to bind DA14585 status completion: {task}")
        text = text.replace(anchor, completion + anchor, 1)
    row = "#if defined(INKTAG_DA_FIXED_RECOVERY)\n" \
          "    {GATTC_CMP_EVT, (ke_msg_func_t) inktag_resume_status_cmp_evt_handler},\n#endif\n"
    if row not in text:
        anchor = "const struct ke_msg_handler suotar_default_state[] =\n{\n"
        if anchor not in text:
            raise SystemExit(f"Unable to register DA14585 status completion: {task}")
        text = text.replace(anchor, anchor + row, 1)
    task.write_text(text, encoding="utf-8")


def patch_spi_flash_wait_policy(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    include_anchor = '#include "spi_flash.h"\n'
    previous_include_patch = (
        '#include "spi_flash.h"\n'
        '#if defined(INKTAG_DA_FIXED_RECOVERY)\n'
        '#include "arch_wdg.h"\n'
        '#include "rwip.h"\n'
        '#endif\n'
    )
    include_patch = (
        '#include "spi_flash.h"\n'
        '#if defined(INKTAG_DA_FIXED_RECOVERY) || defined(INKTAG_DA_APPLICATION_MEMORY_MAP)\n'
        '#include "arch_wdg.h"\n'
        '#if defined(INKTAG_DA_FIXED_RECOVERY)\n'
        '#include "rwip.h"\n'
        '#endif\n'
        '#endif\n'
    )
    legacy_include_patch = previous_include_patch.replace(
        "INKTAG_DA_FIXED_RECOVERY", "INKTAG_DA_SINGLE_APP_SLOT"
    )
    text = text.replace(legacy_include_patch, include_patch, 1)
    text = text.replace(previous_include_patch, include_patch, 1)
    if include_patch not in text:
        if text.count(include_anchor) != 1:
            raise SystemExit(f"Unable to add DA14585 Recovery Flash wait includes: {path}")
        text = text.replace(include_anchor, include_patch, 1)

    wait_anchor = """        if ((spi_flash_read_status_reg() & SPI_FLASH_SR_BUSY) == 0)
        {
            return SPI_FLASH_ERR_OK;
        }
"""
    previous_wait_patch = wait_anchor + """#if defined(INKTAG_DA_FIXED_RECOVERY)
        /* Page program and all other synchronous Flash waits must preserve
         * the same BLE/watchdog liveness as the SDK's non-blocking erase. */
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
        if ((GetBits16(CLK_RADIO_REG, BLE_ENABLE) == 1) &&
            (GetBits32(BLE_DEEPSLCNTL_REG, DEEP_SLEEP_STAT) == 0))
            rwip_schedule();
#endif
"""
    wait_patch = wait_anchor + """#if defined(INKTAG_DA_FIXED_RECOVERY) || defined(INKTAG_DA_APPLICATION_MEMORY_MAP)
        /* App and Recovery use the SDK's two-second watchdog. Keep physical
         * Flash waits bounded by its existing timeout and service watchdog
         * liveness without changing protection or write admission. */
        wdg_reload(WATCHDOG_DEFAULT_PERIOD);
#if defined(INKTAG_DA_FIXED_RECOVERY)
        /* App journal/EPD callbacks must not reenter a Flash-owned SPI bus. */
        if ((GetBits16(CLK_RADIO_REG, BLE_ENABLE) == 1) &&
            (GetBits32(BLE_DEEPSLCNTL_REG, DEEP_SLEEP_STAT) == 0))
            rwip_schedule();
#endif
#endif
"""
    legacy_wait_patch = previous_wait_patch.replace(
        "INKTAG_DA_FIXED_RECOVERY", "INKTAG_DA_SINGLE_APP_SLOT"
    )
    text = text.replace(legacy_wait_patch, wait_patch, 1)
    text = text.replace(previous_wait_patch, wait_patch, 1)
    if wait_patch not in text:
        if text.count(wait_anchor) != 1:
            raise SystemExit(f"Unable to patch DA14585 Recovery Flash wait loop: {path}")
        text = text.replace(wait_anchor, wait_patch, 1)
    path.write_text(text, encoding="utf-8")


def materialize_recovery_project(ota_template: Path, recovery: Path,
                                 suotar_source: Path, repo: Path,
                                 low_frequency_clock: str) -> Path:
    if recovery.exists():
        shutil.rmtree(recovery)
    shutil.copytree(ota_template, recovery)

    config = recovery / "src" / "config"
    user_config = config / "user_config.h"
    text = user_config.read_text(encoding="utf-8")
    # Keep the SDK-generated complete-name field at the final 15-byte size.
    # Recovery replaces the four zeroes from the immutable controller address
    # immediately before advertising, yielding IKT_DA_<ID4>_OTA.
    text = text.replace('#define USER_DEVICE_NAME        "DLG-OTA"',
                        '#define USER_DEVICE_NAME        "IKT_DA_0000_OTA"')
    user_config.write_text(text, encoding="utf-8")
    patch_da14585_advanced_config(
        config / "da14585_config_advanced.h", low_frequency_clock,
        fixed_recovery=True)
    callback_config = config / "user_callback_config.h"
    text = callback_config.read_text(encoding="utf-8")
    callback_anchor = ".app_on_system_powered  = NULL,"
    if text.count(callback_anchor) != 1:
        raise SystemExit(
            f"Unable to install DA14585 Recovery deferred-resume callback "
            f"(deferred main-loop callback): "
            f"{callback_config}"
        )
    callback_config.write_text(
        text.replace(
            callback_anchor,
            ".app_on_system_powered  = user_app_on_system_powered,",
            1,
        ),
        encoding="utf-8",
    )

    profiles = config / "user_profiles_config.h"
    text = profiles.read_text(encoding="utf-8")
    custom_profile = "#define CFG_PRF_CUST1"
    if text.count(custom_profile) != 1:
        raise SystemExit(f"Unable to remove DA14585 Recovery sample custom profile: {profiles}")
    text = text.replace(
        custom_profile,
        "/* Recovery exposes only maintenance profiles; the SDK sample CUST1\n"
        " * LED/button/ADC service is intentionally not compiled. */",
    )
    revision_anchor = "#define APP_DIS_FIRM_REV_STR            SDK_VERSION"
    if text.count(revision_anchor) != 1:
        raise SystemExit(f"Unable to bind DA Recovery firmware revision: {profiles}")
    text = text.replace(revision_anchor,
        '#include "inktag_recovery_version.h"\n'
        '#define APP_DIS_FIRM_REV_STR            INKTAG_RECOVERY_VERSION_STRING', 1)
    text = text.replace(
        "/// @} APP_CONFIG",
        """/* Interrupted writes must reset into this fixed Recovery image. */
#define SUOTAR_SECURE_AUTO_REBOOT_TIMEOUT_MS (3000)

/// @} APP_CONFIG""",
    )
    profiles.write_text(text, encoding="utf-8")

    user_ota = recovery / "src" / "user_ota.c"
    text = user_ota.read_text(encoding="utf-8")
    # The SDK OTA sample rotates a private manufacturer counter once per
    # advertising interval. It is not part of the InkTag Recovery identity or
    # SUOTA protocol, and retaining it costs code plus retained RAM. Keep the
    # connection-parameter timer, but remove this sample-only advertising
    # state machine before adding the stable Recovery name.
    sample_blocks = (
        (
            "// Manufacturer Specific Data ADV structure type\n"
            "struct mnf_specific_data_ad_structure\n"
            "{\n"
            "    uint8_t ad_structure_size;\n"
            "    uint8_t ad_structure_type;\n"
            "    uint8_t company_id[APP_AD_MSD_COMPANY_ID_LEN];\n"
            "    uint8_t proprietary_data[APP_AD_MSD_DATA_LEN];\n"
            "};\n\n",
            "",
        ),
        (
            "timer_hnd app_adv_data_update_timer_used        "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n",
            "",
        ),
        (
            "// Retained variables\n"
            "struct mnf_specific_data_ad_structure mnf_data  "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n"
            "// Index of manufacturer data in advertising data or scan response data (when MSB is 1)\n"
            "uint8_t mnf_data_index                          "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n"
            "uint8_t stored_adv_data_len                     "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n"
            "uint8_t stored_scan_rsp_data_len                "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n"
            "uint8_t stored_adv_data[ADV_DATA_LEN]           "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n"
            "uint8_t stored_scan_rsp_data[SCAN_RSP_DATA_LEN] "
            "__SECTION_ZERO(\"retention_mem_area0\"); //@RETENTION MEMORY\n",
            "",
        ),
        (
            "    // Initialize Manufacturer Specific Data\n"
            "    mnf_data_init();\n\n"
            "    // Initialize Advertising and Scan Response Data\n"
            "    memcpy(stored_adv_data, USER_ADVERTISE_DATA, USER_ADVERTISE_DATA_LEN);\n"
            "    stored_adv_data_len = USER_ADVERTISE_DATA_LEN;\n"
            "    memcpy(stored_scan_rsp_data, USER_ADVERTISE_SCAN_RESPONSE_DATA, USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN);\n"
            "    stored_scan_rsp_data_len = USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN;\n\n",
            "",
        ),
        (
            "    // Schedule the next advertising data update\n"
            "    app_adv_data_update_timer_used = app_easy_timer(APP_ADV_DATA_UPDATE_TO, adv_data_update_timer_cb);\n\n",
            "",
        ),
        (
            "    // Add manufacturer data to initial advertising or scan response data, if there is enough space\n"
            "    app_add_ad_struct(cmd, &mnf_data, sizeof(struct mnf_specific_data_ad_structure), 1);\n\n",
            "",
        ),
        (
            "        // Stop the advertising data update timer\n"
            "        app_easy_timer_cancel(app_adv_data_update_timer_used);\n\n",
            "",
        ),
        (
            "    // Update manufacturer data for the next advertsing event\n"
            "    mnf_data_update();\n",
            "",
        ),
    )
    for old, new in sample_blocks:
        if text.count(old) != 1:
            raise SystemExit(
                f"Unable to remove DA14585 Recovery sample advertising path: {user_ota}"
            )
        text = text.replace(old, new, 1)
    text, removed_sample_helpers = re.subn(
        r"/\*\*\n \*{88}\n \* @brief Initialize Manufacturer Specific Data\n"
        r".*?(?=/\*\*\n \*{88}\n \* @brief Parameter update request timer callback)",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )
    if removed_sample_helpers != 1:
        raise SystemExit(
            f"Unable to remove DA14585 Recovery sample advertising helpers: {user_ota}"
        )
    text = text.replace(
        '#include "gap.h"\n',
        '#include "gap.h"\n#include "gapm.h"\n#include "inktag_da_recovery.h"\n'
        '#include "inktag_da_ota_security.h"\n',
        1,
    )
    suotar_include = """#if (BLE_SUOTA_RECEIVER)
#include "app_suotar.h"
#endif"""
    suotar_include_patch = """#if (BLE_SUOTA_RECEIVER)
#include "app_suotar.h"
extern bool inktag_da_finalize_pending_after_disconnect(void);
#endif"""
    if suotar_include not in text:
        raise SystemExit(
            f"Unable to declare DA14585 disconnected finalizer: {user_ota}"
        )
    text = text.replace(suotar_include, suotar_include_patch, 1)
    identity_anchor = """void user_app_adv_start(void)
{"""
    identity_patch = """#include "inktag_da_recovery_resume.inc"
#include "inktag_da_ble_sdk_address.h"
#include "rwip.h"

void inktag_da_recovery_service_ble(void)
{
    /* Called between completed Flash reads, with SUOTA ownership held.
     * Drain controller events throughout CRC, not in one burst at erase. */
    if ((GetBits16(CLK_RADIO_REG, BLE_ENABLE) == 1) &&
        (GetBits32(BLE_DEEPSLCNTL_REG, DEEP_SLEEP_STAT) == 0))
        rwip_schedule();
}

static void inktag_recovery_apply_device_name(
    struct gapm_start_advertise_cmd *cmd)
{
    static const char hex[] = "0123456789ABCDEF";
    char name[sizeof(USER_DEVICE_NAME)] = "IKT_DA_0000_OTA";
    const uint8_t name_len = (uint8_t)(sizeof(name) - 1u);
    const uint8_t id_offset = (uint8_t)(sizeof("IKT_DA_") - 1u);

    name[id_offset] = hex[gapm_env.addr.addr[1] >> 4];
    name[id_offset + 1u] = hex[gapm_env.addr.addr[1] & 0x0Fu];
    name[id_offset + 2u] = hex[gapm_env.addr.addr[0] >> 4];
    name[id_offset + 3u] = hex[gapm_env.addr.addr[0] & 0x0Fu];

    device_info.dev_name.length = name_len;
    memcpy(device_info.dev_name.name, name, name_len + 1u);

    /* Replace the generated complete-name field rather than deriving
     * Recovery identity from a mutable application alias. */
    if (cmd == NULL || cmd->info.host.scan_rsp_data_len < name_len + 2u ||
        cmd->info.host.scan_rsp_data[0] != name_len + 1u ||
        cmd->info.host.scan_rsp_data[1] != GAP_AD_TYPE_COMPLETE_NAME) {
        ASSERT_ERROR(0);
        return;
    }
    memcpy(&cmd->info.host.scan_rsp_data[2], name, name_len);
}

void user_app_adv_start(void)
{
    inktag_ble_address_t selected;
    /* Refuse unproven identity before allocating an advertising message. */
    if (!inktag_da_ble_sdk_address(&selected)) return;
"""
    if identity_anchor not in text:
        raise SystemExit(f"Unable to patch DA14585 Recovery identity: {user_ota}")
    text = text.replace(identity_anchor, identity_patch, 1)
    command_anchor = """    cmd = app_easy_gap_undirected_advertise_get_active();"""
    command_patch = """    cmd = app_easy_gap_undirected_advertise_get_active();
    inktag_recovery_apply_device_name(cmd);"""
    if command_anchor not in text:
        raise SystemExit(f"Unable to apply DA14585 Recovery identity: {user_ota}")
    text = text.replace(command_anchor, command_patch, 1)
    connection_anchor = """        app_connection_idx = connection_idx;

        // Check if the parameters"""
    connection_patch = """        app_connection_idx = connection_idx;

        /* The SDK resets the status attribute during disconnect cleanup.
         * Republish a retained structured rejection only after Recovery has
         * accepted the replacement connection. */
        if (inktag_da_ota_reject_pending())
            suotar_send_status_update_req((uint8_t) SUOTAR_EXT_MEM_WRITE_ERR);

        // Check if the parameters"""
    if connection_anchor not in text:
        raise SystemExit(
            f"Unable to republish DA14585 retained rejection: {user_ota}"
        )
    text = text.replace(connection_anchor, connection_patch, 1)
    text = text.replace(
        "    // Disable the SPI flash memory protection (unprotect all sectors)\n"
        "    spi_flash_configure_memory_protection(SPI_FLASH_MEM_PROT_NONE);\n\n",
        "",
    )
    reset_anchor = """        // Platform reset
        platform_reset(RESET_AFTER_SUOTA_UPDATE);"""
    reset_patch = """        // Re-enter the packaged pP selector; software reset alone may
        // preserve DA14585's current address-zero SRAM remap. A failed bounded
        // preflight leaves this already-restarted advertiser in Recovery.
        (void)inktag_da_restart_via_secondary_bootloader();"""
    if text.count(reset_anchor) != 1:
        raise SystemExit(f"Unable to patch DA14585 Recovery disconnect handoff: {user_ota}")
    text = text.replace(reset_anchor, reset_patch, 1)
    disconnect_anchor = """    // Restart Advertising
    user_app_adv_start();

#if (BLE_SUOTA_RECEIVER)
    // Issue a platform reset when it is requested by the suotar procedure
    if (suota_state.reboot_requested)
    {
        // Reboot request will be served
        suota_state.reboot_requested = 0;

        // Re-enter the packaged pP selector; software reset alone may
        // preserve DA14585's current address-zero SRAM remap. A failed bounded
        // preflight leaves this already-restarted advertiser in Recovery.
        (void)inktag_da_restart_via_secondary_bootloader();
    }
#endif"""
    disconnect_patch = """#if (BLE_SUOTA_RECEIVER)
    /* A peer disconnect cannot replace confirmed ACK delivery. Safe resume
     * itself is serviced from the system-powered main-loop callback. */
    inktag_da_safe_resume_on_disconnect();

    /* Final image verification is deliberately disconnected: the several-
     * second P-256/journal pass must not starve controller event handling. */
    (void)inktag_da_finalize_pending_after_disconnect();

    /* Do not allocate a fresh advertising command while the disconnected
     * role is already committed to a pP handoff. On SDK6's small kernel heap
     * that transient allocation can assert with RESET_MEM_ALLOC_FAIL. */
    if (suota_state.reboot_requested)
    {
        suota_state.reboot_requested = 0;
        if (!inktag_da_restart_via_secondary_bootloader())
            user_app_adv_start();
        return;
    }
#endif

    // Restart Advertising
    user_app_adv_start();"""
    if disconnect_anchor not in text:
        raise SystemExit(
            f"Unable to hook DA14585 disconnected finalizer: {user_ota}"
        )
    text = text.replace(disconnect_anchor, disconnect_patch, 1)
    user_ota.write_text(text, encoding="utf-8")

    user_ota_header = recovery / "src" / "user_ota.h"
    text = user_ota_header.read_text(encoding="utf-8")
    declaration_anchor = """void user_app_init(void);
"""
    declaration_patch = """void user_app_init(void);
arch_main_loop_callback_ret_t user_app_on_system_powered(void);
bool inktag_da_safe_resume_busy(void);
void inktag_da_safe_resume_request(uint32_t command, bool finalize_pending);
"""
    if text.count(declaration_anchor) != 1:
        raise SystemExit(
            f"Unable to declare DA14585 Recovery deferred resume: "
            f"{user_ota_header}"
        )
    user_ota_header.write_text(
        text.replace(declaration_anchor, declaration_patch, 1),
        encoding="utf-8",
    )

    local_suotar = recovery / "src" / "app_suotar.c"
    copy_file(suotar_source, local_suotar)
    patch_single_app_suotar(local_suotar)
    local_suotar_task = recovery / "src" / "app_suotar_task.c"
    copy_file(
        suotar_source.with_name("app_suotar_task.c"),
        local_suotar_task,
    )
    patch_deferred_da14585_finalize(local_suotar_task)

    recovery_common = recovery / "src" / "inktag_common"
    copy_file(repo / "compat/dialog/src/inktag_da_recovery_integrity_version.h",
              config / "inktag_recovery_version.h")
    for name in ["inktag_ble_address.c", "inktag_ble_address.h"]:
        copy_file(repo / "compat/common/ble" / name, recovery_common / name)
    for name in ["inktag_da_ble_address.h", "inktag_da_ble_sdk_address.h"]:
        copy_file(repo / "compat/dialog/src" / name,
                  recovery_common / name)
    copy_file(repo / "compat/common/ota/inktag_recovery.c",
              recovery_common / "inktag_recovery.c")
    copy_file(repo / "compat/common/ota/inktag_recovery.h",
              recovery_common / "inktag_recovery.h")
    copy_file(repo / "compat/common/ota/inktag_ota_reject.h",
              recovery_common / "inktag_ota_reject.h")
    copy_file(repo / "compat/common/security/inktag_firmware_signature.h",
              recovery_common / "inktag_firmware_signature.h")
    copy_file(repo / "compat/dialog/src/inktag_da_recovery.c",
              recovery_common / "inktag_da_recovery.c")
    copy_file(repo / "compat/dialog/src/inktag_da_recovery.h",
              recovery_common / "inktag_da_recovery.h")
    copy_file(repo / "compat/dialog/src/inktag_da_recovery_resume.inc",
              recovery_common / "inktag_da_recovery_resume.inc")
    copy_file(repo / "compat/dialog/src/inktag_da_ota_security.c",
              recovery_common / "inktag_da_ota_security.c")
    copy_file(repo / "compat/dialog/src/inktag_da_ota_security.h",
              recovery_common / "inktag_da_ota_security.h")
    copy_file(repo / "compat/dialog/src/inktag_da_ota_policy.h",
              recovery_common / "inktag_da_ota_policy.h")
    copy_file(repo / "compat/common/power/inktag_power.c",
              recovery_common / "inktag_power.c")
    copy_file(repo / "compat/common/power/inktag_power.h",
              recovery_common / "inktag_power.h")
    copy_file(repo / "compat/dialog/src/inktag_da_health.c",
              recovery_common / "inktag_da_health.c")
    copy_file(repo / "compat/dialog/src/inktag_da_health.h",
              recovery_common / "inktag_da_health.h")
    sdk = suotar_source.parents[4]
    project = recovery / "Keil_5" / "ble_app_ota.uvprojx"
    patch_da14585_suota_profile(sdk)
    patch_recovery_uvprojx(project)
    return project


def patch_user_profiles_config(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = text.replace("#define CFG_PRF_DISS\n#define CFG_PRF_CUST1",
                        "#define CFG_PRF_CUST1")
    path.write_text(text, encoding="utf-8")


def patch_da14585_advanced_config(path: Path, low_frequency_clock: str,
                                  *, fixed_recovery: bool = False) -> None:
    if low_frequency_clock not in LOW_FREQUENCY_CLOCK_MACROS.values():
        raise RuntimeError(f"Unsupported DA14585 low-frequency clock macro: {low_frequency_clock}")
    text = path.read_text(encoding="utf-8")
    text, count = re.subn(
        r"#define CFG_LP_CLK\s+LP_CLK_(?:XTAL32|RCX20)",
        f"#define CFG_LP_CLK              {low_frequency_clock}",
        text,
        count=1,
    )
    if count != 1:
        raise RuntimeError(f"{path}: missing supported CFG_LP_CLK definition")
    # Keep the SDK's TRNG-seeded ChaCha20 generator, but use the documented
    # small acquisition buffer already exercised by the DA build. This is a
    # startup-latency/RAM trade and does not reduce the application stack.
    text = text.replace("#define CFG_TRNG (1024)",
                        "#define CFG_TRNG (32)")
    # Keep the 244-byte ATT/GATT value contract while using the Bluetooth 4.x
    # 27-byte link-layer PDU. L2CAP fragmentation preserves the public packet
    # size; the legacy PDU layout moves the SDK's retained/BLE boundary and
    # returns one 4 KiB SRAM bank to DA14585's constrained code-in-SRAM
    # application. Hardware regression must cover sustained image/SUOTA
    # transfer throughput and reconnect behavior.
    text = text.replace("#define CFG_MAX_TX_PACKET_LENGTH        (251)",
                        "#define CFG_MAX_TX_PACKET_LENGTH        (27)")
    text = text.replace("#define CFG_MAX_RX_PACKET_LENGTH        (251)",
                        "#define CFG_MAX_RX_PACKET_LENGTH        (27)")
    # The application exposes one five-attribute custom service. SDK6 uses
    # the non-retained heap as a boot-time scratch allocation for its
    # 124-byte service descriptor; 128 payload bytes plus the SDK allocator's
    # separately calculated 12-byte heap overhead is the exact bounded need.
    # Connected Recovery also allocates controller messages during safe resume;
    # restore its original SDK heap. The App allocation remains unchanged.
    text = text.replace("// #define NON_RET_HEAP_SZ         2048",
                        "#define NON_RET_HEAP_SZ         " +
                        ("2048" if fixed_recovery else "128"))
    # The application has one five-attribute CUSTS1 database and no sample
    # DISS/SUOTA profile. Bound the retained attribute-database heap below the
    # SDK's generic 1 KiB default; exact-board BLE/profile stress remains a
    # release gate for this target-specific memory setting.
    text = text.replace("// #define DB_HEAP_SZ              1024",
                        "#define DB_HEAP_SZ              896")
    # With legacy 27-byte link-layer PDUs, InkTag retains at most one incoming
    # 244-byte command and one queued response/event at a time. The stock
    # auto-size leaves 1,372 of 1,392 bytes free on a connected qualified
    # AESL42. Keep 1,088 bytes so a 420-byte persisted QR record can be loaded
    # while connected with response/event margin, returning the unused 304
    # bytes to the code-in-SRAM application. Sustained V2 transfer,
    # cancellation and reconnect testing is required for this exact setting.
    text = text.replace("// #define MSG_HEAP_SZ             6880",
                        "#define MSG_HEAP_SZ             1088")
    # The Free-only DA image generates a fixed 14-byte advertising name and
    # reuses the SDK's retained GAP name. Keep a bounded 8-byte margin above
    # the linked 920-byte retention block while returning executable SRAM to
    # the product-floor lifecycle commands.
    text = text.replace("#define CFG_RET_DATA_SIZE    (2048)",
                        "#define CFG_RET_DATA_SIZE    (928)")
    path.write_text(text, encoding="utf-8")


def patch_user_callback_config(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    anchor = '#include "user_peripheral.h"'
    if text.count(anchor) != 1:
        raise RuntimeError(f"{path}: missing application callback include")
    text = text.replace(anchor, anchor + '\n#include "inktag_board.h"')
    text = text.replace(".app_on_system_powered  = NULL,",
                        ".app_on_system_powered  = user_app_on_system_powered,")
    text, sleep_count = re.subn(
        r"(\.app_validate_sleep\s*=\s*)NULL(\s*,)",
        r"\1user_app_validate_sleep\2",
        text,
        count=1,
    )
    if sleep_count != 1:
        raise RuntimeError(f"{path}: missing application sleep validation callback slot")
    path.write_text(text, encoding="utf-8")


def patch_user_periph_setup(path: Path, board: str) -> None:
    text = path.read_text(encoding="utf-8")
    reserve_marker = "#ifdef CFG_PRINTF_UART2\n    RESERVE_GPIO(UART2_TX, UART2_TX_PORT, UART2_TX_PIN, PID_UART2_TX);\n#endif"
    text = text.replace(
        reserve_marker,
        reserve_marker + "\n\n" + BOARD_GPIO_RESERVATIONS[board],
    )
    pad_marker = "#if defined (CFG_PRINTF_UART2)\n    GPIO_ConfigurePin(UART2_TX_PORT, UART2_TX_PIN, OUTPUT, PID_UART2_TX, false);\n#endif"
    text = text.replace(
        pad_marker,
        pad_marker + "\n\n" + BOARD_STORAGE_PAD_CONFIGURATION[board],
    )
    path.write_text(text, encoding="utf-8")


def patch_da14585_basic_config(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    text = text.replace("#undef CFG_SPI_FLASH_ENABLE", "#define CFG_SPI_FLASH_ENABLE")
    # SDK development mode adds OTP-copy timing emulation, GPIO reservation
    # checks and breakpoint-oriented fault handlers. It is explicitly not a
    # production setting and is unrelated to leaving SWD attach enabled.
    text = text.replace("#define CFG_DEVELOPMENT_DEBUG",
                        "#undef CFG_DEVELOPMENT_DEBUG")
    path.write_text(text, encoding="utf-8")


def patch_da14585_system_init(path: Path) -> None:
    """Keep the one-shot SDK initializer out of the perpetual main frame.

    SDK6 declares most of its main-loop helpers force-inline. With LTO it also
    folds system_init() into main(), so the initializer's peak stack remains
    reserved while InkTag later renders a page. A noinline boundary preserves
    behavior while making startup and runtime stack peaks mutually exclusive.
    """
    text = path.read_text(encoding="utf-8")
    pattern = r"(?m)^(?:__attribute__\(\(noinline\)\) )*void system_init\(void\)\n\{"
    patched = "__attribute__((noinline)) void system_init(void)\n{"
    text, count = re.subn(pattern, patched, text)
    if count != 1:
        raise RuntimeError(f"{path}: missing DA14585 system_init definition")
    if path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def patch_da14585_trng_stack(path: Path) -> None:
    """Unwind the unchanged 1024-byte RFADC sample before App/Flash init.

    LTO otherwise merges the startup-only TRNG sample array into system_init,
    keeping it live across unrelated configuration journal calls. This changes
    neither entropy collection nor radio/power settings, just frame lifetime.
    """
    text = path.read_text(encoding="utf-8")
    pattern = r"(?m)^(?:__attribute__\(\(noinline\)\) )*void init_rand_seed_from_trng\(void\)\n\{"
    text, count = re.subn(pattern,
        "__attribute__((noinline)) void init_rand_seed_from_trng(void)\n{", text)
    if count != 1:
        raise RuntimeError(f"{path}: missing DA14585 TRNG seed definition")
    if path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


def patch_user_config(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    old_adv = '''#define USER_ADVERTISE_DATA         ("\\x03"\\
                                    ADV_TYPE_COMPLETE_LIST_16BIT_SERVICE_IDS\\
                                    ADV_UUID_DEVICE_INFORMATION_SERVICE\\
                                    "\\x11"\\
                                    ADV_TYPE_COMPLETE_LIST_128BIT_SERVICE_IDS\\
                                    "\\x59\\x5A\\x08\\xE4\\x86\\x2A\\x9E\\x8F\\xE9\\x11\\xBC\\x7C\\x98\\x43\\x42\\x18")'''
    new_adv = '''#define USER_ADVERTISE_DATA         ("\\x11"\\
                                    ADV_TYPE_COMPLETE_LIST_128BIT_SERVICE_IDS\\
                                    "\\xEC\\x5A\\x67\\x1C\\xC1\\xB6\\x46\\xFB\\x8D\\x91\\x28\\xD8\\x01\\x00\\x75\\x62")'''
    text = text.replace(old_adv, new_adv)
    text = text.replace('#define USER_ADVERTISE_SCAN_RESPONSE_DATA ""',
                        '#define USER_ADVERTISE_SCAN_RESPONSE_DATA ""')
    text = text.replace('#define USER_DEVICE_NAME        "DLG-PRPH"',
                        '#define USER_DEVICE_NAME        ""')
    # The application requests the SDK sleep path, but the board validator
    # keeps unqualified AESL hardware in event-aware mode_idle. A future
    # exact-board-qualified extended-sleep build must still preserve the
    # 30-second SWD grace window. Recovery is materialized separately and
    # keeps ARCH_SLEEP_OFF permanently.
    text, sleep_count = re.subn(
        r"(app_default_sleep_mode\s*=\s*)ARCH_SLEEP_OFF(\s*;)",
        r"\1ARCH_EXT_SLEEP_ON\2",
        text,
        count=1,
    )
    if sleep_count != 1:
        raise RuntimeError(f"{path}: missing DA application sleep-mode setting")
    # The InkTag data characteristic carries up to 244 ATT payload bytes.
    text = text.replace("    .max_mtu = 23,", "    .max_mtu = 247,", 1)
    # The SDK sample's1.25s link supervision timeout is not an InkTag
    # e-paper policy. Match the Nordic App's30s request without weakening
    # panel deadlines or cooperative BLE service. Interval/latency and the
    # one-second disconnected advertising/sleep policy remain unchanged.
    text, supervision_count = re.subn(
        r"(\.time_out\s*=\s*MS_TO_TIMERUNITS\()1250(\)\s*,)",
        r"\g<1>30000\2",
        text,
        count=1,
    )
    if supervision_count != 1:
        raise RuntimeError(f"{path}: missing SDK App supervision-timeout setting")
    path.write_text(text, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--sdk-workspace", type=Path,
                        help="Owned isolated render directory, supplied by da_sdk_workspace.py")
    parser.add_argument("--board", choices=sorted(BOARD_ARTIFACTS), default="da14585_aesl")
    args = parser.parse_args()

    repo = args.repo.resolve()
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from da_sdk_workspace import isolated_render
    sdk = isolated_render(repo, args.sdk_workspace)
    subprocess.run(
        [sys.executable, str(repo / "build-support/version/sync-version.py"), "--repo", str(repo)],
        check=True,
    )
    template = sdk / "projects" / "target_apps" / "ble_examples" / "ble_app_peripheral"
    ota_template = sdk / "projects" / "target_apps" / "ble_examples" / "ble_app_ota"
    project = sdk / "projects" / "target_apps" / "ble_examples" / "ble_app_inktag"
    recovery = sdk / "projects" / "target_apps" / "ble_examples" / DA14585_RECOVERY_PROJECT
    common = repo / "compat/dialog/src"
    epd_common = repo / "compat/common" / "epd"
    input_common = repo / "compat/common" / "input"
    protocol_common = repo / "compat/common" / "protocol"
    lifecycle_common = repo / "compat/common" / "lifecycle"
    ota_common = repo / "compat/common" / "ota"
    power_common = repo / "compat/common" / "power"
    security_common = repo / "compat/common" / "security"
    storage_common = repo / "compat/common" / "storage"
    time_common = repo / "compat/common" / "time"
    low_frequency_clock = resolve_low_frequency_clock(repo, args.board)

    if not template.exists():
        raise SystemExit(f"Missing Dialog template: {template}")
    if not ota_template.exists():
        raise SystemExit(f"Missing Dialog OTA template: {ota_template}")
    if project.exists():
        shutil.rmtree(project)
    shutil.copytree(template, project)
    copy_file(ota_template / "src" / "platform" / "user_periph_setup.c",
              project / "src" / "platform" / "user_periph_setup.c")
    copy_file(ota_template / "src" / "config" / "user_periph_setup.h",
              project / "src" / "config" / "user_periph_setup.h")

    copy_file(common / "user_peripheral.c", project / "src" / "user_peripheral.c")
    copy_file(common / "user_peripheral.h", project / "src" / "user_peripheral.h")
    copy_file(common / "user_custs1_impl.c", project / "src" / "user_custs1_impl.c")
    copy_file(common / "user_custs1_impl.h", project / "src" / "user_custs1_impl.h")
    # A project-local SDK specialization avoids changing Recovery or any
    # other worktree's original vendor profile source.
    custs_task = sdk / "sdk/ble_stack/profiles/custom/custs/src/custs1_task.c"
    copied_custs_task = project / "src/custom_profile/custs1_task.c"
    copied_custs_task.parent.mkdir(parents=True, exist_ok=True)
    copied_custs_task.write_text(specialize_da_custs1_task(
        custs_task.read_text(encoding="utf-8")), encoding="utf-8")
    copy_file(common / "inktag_da_image_store.c",
              project / "src" / "inktag_common" / "inktag_da_image_store.c")
    copy_file(common / "inktag_da_image_store.h",
              project / "src" / "inktag_common" / "inktag_da_image_store.h")
    for name in ["inktag_da_fixed_slot_io.h", "inktag_da_clock.h", "inktag_da_default_qr.h",
                 "inktag_da_ble_address.h", "inktag_da_ble_sdk_address.h",
                 "inktag_da_pins.c", "inktag_da_pins.h"]:
        copy_file(common / name, project / "src" / "inktag_common" / name)
    copy_file(common / "inktag_da_runtime_scratch.h",
              project / "src" / "inktag_common" / "inktag_da_runtime_scratch.h")
    copy_file(common / "inktag_da_capabilities.h",
              project / "src" / "inktag_common" / "inktag_da_capabilities.h")
    copy_file(common / "inktag_da_external_assets.c",
              project / "src" / "inktag_common" / "inktag_da_external_assets.c")
    copy_file(common / "inktag_da_health.c",
              project / "src" / "inktag_common" / "inktag_da_health.c")
    copy_file(common / "inktag_da_health.h",
              project / "src" / "inktag_common" / "inktag_da_health.h")
    copy_file(common / "inktag_da_protocol_v2.inc",
              project / "src" / "inktag_da_protocol_v2.inc")
    for name in ["inktag_da_transfer.h", "inktag_da_transfer_write.inc", "inktag_da_ghost.inc", "inktag_da_reset_trace.inc", "inktag_da_tests.inc"]:
        copy_file(common / name, project / "src" / name)
    copy_file(common / "inktag_da_notifications.inc",
              project / "src" / "inktag_da_notifications.inc")
    copy_file(common / "inktag_da_recovery.c",
              project / "src" / "inktag_common" / "inktag_da_recovery.c")
    copy_file(common / "inktag_da_recovery.h",
              project / "src" / "inktag_common" / "inktag_da_recovery.h")
    copy_file(common / "inktag_da_ota_policy.h",
              project / "src" / "inktag_common" / "inktag_da_ota_policy.h")
    copy_file(common / "user_custs1_def.c", project / "src" / "custom_profile" / "user_custs1_def.c")
    copy_file(common / "user_custs1_def.h", project / "src" / "custom_profile" / "user_custs1_def.h")
    copy_file(common / "sdk_config.h", project / "src" / "config" / "sdk_config.h")
    driver_sources = BOARD_DRIVER_SOURCES[args.board]
    gui_common = repo / "compat/common" / "gui"
    for name in ["inktag_epd_config.c", "inktag_epd_config.h", "inktag_epd_driver.h", "inktag_epd_screen.c", "inktag_epd_screen.h", "inktag_epd_runtime.c", "inktag_epd_commands.c", "inktag_epd_temperature.h", "inktag_page_cycle.c", "inktag_page_cycle.h", "inktag_protocol_v2.h", "inktag_version.h", *driver_sources]:
        copy_file(epd_common / name, project / "src" / "inktag_common" / name)
    copy_file(epd_common / "inktag_epd_wait_policy.h",
              project / "src" / "inktag_common" / "inktag_epd_wait_policy.h")
    for name in ["inktag_input.c", "inktag_input.h",
                 "inktag_resource_name.c", "inktag_resource_name.h"]:
        copy_file(input_common / name, project / "src" / "inktag_common" / name)
    ble_common = repo / "compat/common" / "ble"
    for name in ["inktag_ble_identity.h", "inktag_ble_power.h",
                 "inktag_ble_address.c", "inktag_ble_address.h",
                 "inktag_device_display_name.h"]:
        copy_file(ble_common / name, project / "src" / "inktag_common" / name)
    copy_file(repo / "compat/common" / "status" / "inktag_temperature_wire.h",
              project / "src" / "inktag_common" / "inktag_temperature_wire.h")
    for name in ["inktag_protocol_core.c", "inktag_protocol_core.h", "inktag_release_v1.h",
                 "inktag_pin_config.c", "inktag_pin_config.h",
                 "inktag_image_upload_v5.c", "inktag_image_upload_v5.h",
                 "inktag_packbits.c", "inktag_packbits.h",
                 "inktag_display_settings.c", "inktag_display_settings.h"]:
        copy_file(source_path(repo, f"compat/common/protocol/{name}"),
                  project / "src" / "inktag_common" / name)
    for name in ["inktag_lifecycle.c", "inktag_lifecycle.h"]:
        copy_file(lifecycle_common / name, project / "src" / "inktag_common" / name)
    for name in ["inktag_recovery.c", "inktag_recovery.h"]:
        copy_file(ota_common / name, project / "src" / "inktag_common" / name)
    for name in ["inktag_power.c", "inktag_power.h"]:
        copy_file(power_common / name, project / "src" / "inktag_common" / name)
    for name in ["inktag_settings_records.h", "inktag_settings_journal.c",
                 "inktag_settings_journal.h", "inktag_image_fixed_store.h",
                 "inktag_image_fixed_store.inc"]:
        copy_file(storage_common / name,
                  project / "src" / "inktag_common" / name)
    # Preserve the byte-exact reviewed common source; specialize only its
    # generated SDK copy's capture placement. 96-byte header replaces the old
    # 96-byte DA transaction arena; initialized state must not enter pure ZI.
    fixed_copy = project / "src" / "inktag_common" / "inktag_image_fixed_store.inc"
    fixed_text = fixed_copy.read_text(encoding="utf-8")
    capture_anchor = "static INKTAG_FIXED_STORE_RAM inktag_image_store_header_t capture;"
    if fixed_text.count(capture_anchor) != 1:
        raise RuntimeError("fixed-slot capture placement anchor changed")
    fixed_text = fixed_text.replace(capture_anchor,
        'static __attribute__((section(".bss.inktag_free_area"), aligned(4))) '
        'inktag_image_store_header_t capture;')
    fixed_copy.write_text(fixed_text, encoding="utf-8")
    fixed_header = project / "src" / "inktag_common" / "inktag_image_fixed_store.h"
    fixed_header.write_text(specialize_da_fixed_header(
        fixed_header.read_text(encoding="utf-8")), encoding="utf-8")
    # Free-only DA exposes the complete qualified image extent and therefore
    # needs no commercial quota layer. Keep only the final-chunk Free overlay;
    # do not link activation, access, SHA-256 or entitlement storage. Import
    # only the neutral status header for the read-only permanent-Free wire
    # size; the credential engine remains absent from the project.
    for name in ["inktag_brand_overlay.c", "inktag_brand_overlay.h",
                 "inktag_device_status.h"]:
        copy_file((protocol_common if name == "inktag_device_status.h" else security_common) / name,
                  project / "src" / "inktag_common" / name)
    for name in ["inktag_wall_clock.c", "inktag_wall_clock.h"]:
        copy_file(time_common / name, project / "src" / "inktag_common" / name)
    # The frozen DA application copies the unmodified shared GUI, fonts and
    # lunar/calendar implementation; controller/profile support is not split.
    for name in [*GUI_SOURCES, *GUI_HEADERS, *GUI_INCLUDES]:
        copy_file(gui_common / name, project / "src" / "inktag_common" / name)
    generated_board_header = project / "src" / "inktag_common" / "inktag_board.h"
    copy_file(source_path(repo, BOARD_HEADER_FILES[args.board].as_posix()), generated_board_header)

    driver = (epd_common / "inktag_epd_driver.c").read_text(encoding="utf-8")
    tail = driver[driver.index("void EPD_FillRAM"):]
    tail = tail.replace("            app_feed_wdt();",
                        "            app_service_ble();")
    tail = tail.replace("nrf_gpio_pin_toggle(EPD_LED_PIN);",
                        "digitalWrite(EPD_LED_PIN, digitalRead(EPD_LED_PIN) ? LOW : HIGH);")
    read_voltage_start = tail.index("uint16_t EPD_ReadVoltage(void) {")
    tail = tail[:read_voltage_start] + (
        "uint16_t EPD_ReadVoltage(void) {\n"
        "    return inktag_da_read_battery_mv();\n"
        "}\n"
    )
    (project / "src" / "epd_driver_dialog.c").write_text(EPD_DIALOG_PREFIX + "\n\n" + tail, encoding="utf-8")
    patch_da14585_scratch_placement(sdk, project)

    patch_user_config(project / "src" / "config" / "user_config.h")
    patch_user_profiles_config(project / "src" / "config" / "user_profiles_config.h")
    patch_da14585_basic_config(project / "src" / "config" / "da14585_config_basic.h")
    patch_da14585_advanced_config(
        project / "src" / "config" / "da14585_config_advanced.h",
        low_frequency_clock,
    )
    patch_da14585_system_init(
        sdk / "sdk" / "platform" / "arch" / "main" / "arch_system.c"
    )
    patch_da14585_trng_stack(
        sdk / "sdk" / "platform" / "driver" / "trng" / "trng.c"
    )
    patch_user_callback_config(project / "src" / "config" / "user_callback_config.h")
    patch_user_periph_setup(project / "src" / "platform" / "user_periph_setup.c", args.board)

    old_uvprojx = project / "Keil_5" / "ble_app_peripheral.uvprojx"
    new_uvprojx = project / "Keil_5" / "inktag_da14585.uvprojx"
    old_uvprojx.rename(new_uvprojx)
    materialize_da14585_scatter(sdk, project)
    patch_uvprojx(new_uvprojx, ["inktag_epd_screen.c", "inktag_epd_runtime.c", "inktag_epd_commands.c", "inktag_page_cycle.c", "inktag_input.c",
                  "inktag_resource_name.c",
                  "inktag_protocol_core.c", "inktag_lifecycle.c",
                  "inktag_image_upload_v5.c",
                  "inktag_packbits.c",
                  "inktag_display_settings.c",
                  "inktag_pin_config.c", "inktag_da_pins.c",
                  "inktag_recovery.c", "inktag_da_recovery.c",
                  "inktag_power.c",
                  "inktag_ble_address.c",
                  "inktag_settings_journal.c",
                  "inktag_brand_overlay.c",
                  "inktag_da_image_store.c",
                  "inktag_da_external_assets.c",
                  "inktag_wall_clock.c",
                  "inktag_da_health.c",
                  *driver_sources, *GUI_SOURCES],
                  BOARD_DRIVER_DEFINES[args.board] + " -DINKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES=35 -DINKTAG_TRANSFER_PACKBITS_ENABLED=1 -DINKTAG_TRANSFER_LZO1X_ENABLED=0" +
                  "")
    patch_spi_flash_wait_policy(
        sdk / "sdk" / "platform" / "driver" / "spi_flash" / "spi_flash.c"
    )
    recovery_project = materialize_recovery_project(
        ota_template,
        recovery,
        sdk / "sdk" / "app_modules" / "src" / "app_suotar" / "app_suotar.c",
        repo,
        low_frequency_clock,
    )
    bootloader_project = materialize_da14585_secondary_bootloader(repo, sdk)

    print(f"{project} [{args.board}, low-frequency clock={low_frequency_clock}]")
    print(f"{recovery_project} [fixed Recovery, max image 0x{DA14585_RECOVERY_MAX_IMAGE_SIZE:X}]")
    print(
        f"{bootloader_project} [SPI-only secondary boot, "
        f"max application 0x{DA14585_APPLICATION_RAW_LIMIT:X}]"
    )


if __name__ == "__main__":
    main()
