#include "inktag_epd_driver.h"
#include "inktag_board.h"
#include "inktag_epd_wait_policy.h"
#include "inktag_gr_health.h"

#include <string.h>
#include "app_io.h"
#include "app_spi.h"
#include "app_log.h"
#include "grx_sys.h"
#include "inktag_gr_app.h"
#include "inktag_gr_panel_diagnostic.h"
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
#include "inktag_gr_pin_routes.h"
#include "inktag_gr_pin_gpio.h"
static bool m_configured_busy, m_physical_busy_low, m_common_busy_high = true;
static bool m_enable_low;
#endif

static uint32_t epd_enable_level(bool active) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    return active != m_enable_low ? HIGH : LOW;
#else
    return active ? HIGH : LOW;
#endif
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BUFFER_SIZE 128
#define SPI_TIMEOUT_MS 1000
#define BUSY_ASSERT_TIMEOUT_MS 500
#define BUSY_POLL_INTERVAL_MS 1u

_Static_assert(BUSY_POLL_INTERVAL_MS <= INKTAG_EPD_WAIT_CANCEL_MAX_MS,
               "Goodix EPD cancel polling exceeds the common contract");

#ifndef INKTAG_BOARD_EPD_SPI_HZ
#define INKTAG_BOARD_EPD_SPI_HZ 1000000u
#endif

#ifndef INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW
#define INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW 0
#endif

#if defined(INKTAG_GOODIX_GR5515)
#define EPD_SPI_IO_TYPE APP_IO_TYPE_NORMAL
#define EPD_SPI_CS_MUX APP_IO_MUX_7
#define EPD_SPI_CLK_MUX APP_IO_MUX_2
#define EPD_SPI_MOSI_MUX APP_IO_MUX_2
#else
#define EPD_SPI_IO_TYPE APP_IO_TYPE_GPIOA
#define EPD_SPI_CS_MUX APP_IO_MUX_43
#define EPD_SPI_CLK_MUX APP_IO_MUX_42
#define EPD_SPI_MOSI_MUX APP_IO_MUX_46
#endif

extern void app_feed_wdt(void);

static uint32_t EPD_MOSI_PIN = 5;
static uint32_t EPD_SCLK_PIN = 8;
static uint32_t EPD_CS_PIN = 9;
static uint32_t EPD_DC_PIN = 10;
static uint32_t EPD_RST_PIN = 11;
static uint32_t EPD_BUSY_PIN = 12;
static uint32_t EPD_BS_PIN = 13;
static uint32_t EPD_WAKEUP_PIN = 0xFF;
static uint32_t EPD_EN_PIN = 0xFF;
static uint32_t EPD_LED_PIN = 0xFF;
static epd_abort_callback_t m_abort_callback = NULL;
static uint16_t m_driver_refs = 0;
static bool m_spi_ready = false;
static bool m_spi_hardware = false;
static uint16_t m_spi_init_status = APP_DRV_SUCCESS;
static uint16_t m_spi_last_status = APP_DRV_SUCCESS;
static uint32_t m_spi_error_count = 0;
static uint8_t m_last_command = 0xffu;
static uint8_t m_panel_read_command = 0xffu;
static uint8_t m_panel_read_raw[2];
static uint8_t m_panel_read_length = 0u;
/* app_spi keeps pointers to both spi_env and pin_cfg, so this configuration
 * must outlive EPD_GPIO_Init rather than being allocated on its stack. */
static app_spi_params_t m_spi_params;
static bool m_busy_diag_asserted = false;
static uint32_t m_busy_diag_assert_delay_ms = 0;
static uint32_t m_busy_diag_active_ms = 0;

static bool goodix_pin_valid(uint32_t pin) { return pin != 0xFF && pin < 0xC0u && (pin & 0x3Fu) < 32; }

static uint32_t goodix_pin_index(uint32_t pin) { return pin & 0x3Fu; }

static uint32_t pin_mask(uint32_t pin) {
    if (!goodix_pin_valid(pin)) return 0;
    return 1u << goodix_pin_index(pin);
}

static app_io_type_t goodix_pin_type(uint32_t pin) {
    switch ((pin >> 6) & 0x03u) {
        case 1:
            return APP_IO_TYPE_AON;
        case 2:
            return APP_IO_TYPE_MSIO;
        default:
#if defined(INKTAG_GOODIX_GR5515)
            /*
             * GR551x numbers its regular package pins through the NORMAL IO
             * bank.  Treating these values as GPIOA (the GR533x encoding)
             * silently addresses the wrong GPIO registers: panel EN/RST/DC
             * do not move and BUSY appears permanently low.
             */
            return APP_IO_TYPE_NORMAL;
#else
            return APP_IO_TYPE_GPIOA;
#endif
    }
}

static bool epd_hardware_spi_supported(void) {
#if defined(INKTAG_GOODIX_GR5515)
    /* GR5513 ZKC42V/VM routes P3/P4 to SPIM CLK/MOSI. The qualified GR5515
     * PCB instead fixes CLK on P16, which has no SPIM-CLK mux, so it must keep
     * the GPIO implementation. CS is software-controlled by app_spi. */
    return goodix_pin_type(EPD_SCLK_PIN) == APP_IO_TYPE_NORMAL &&
           goodix_pin_type(EPD_MOSI_PIN) == APP_IO_TYPE_NORMAL &&
           goodix_pin_type(EPD_CS_PIN) == APP_IO_TYPE_NORMAL &&
           goodix_pin_index(EPD_CS_PIN) == 2u &&
           goodix_pin_index(EPD_SCLK_PIN) == 3u &&
           goodix_pin_index(EPD_MOSI_PIN) == 4u;
#else
    /* The factory ZKC21V bus is GPIOA8 MOSI / GPIOA9 CLK. */
    return goodix_pin_type(EPD_SCLK_PIN) == APP_IO_TYPE_GPIOA &&
           goodix_pin_type(EPD_MOSI_PIN) == APP_IO_TYPE_GPIOA &&
           goodix_pin_type(EPD_CS_PIN) == APP_IO_TYPE_GPIOA &&
           goodix_pin_index(EPD_CS_PIN) == 10u &&
           goodix_pin_index(EPD_SCLK_PIN) == 9u &&
           goodix_pin_index(EPD_MOSI_PIN) == 8u;
#endif
}

static void epd_soft_spi_init(void) {
    pinMode(EPD_MOSI_PIN, OUTPUT);
    pinMode(EPD_SCLK_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);
    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCLK_PIN, HIGH);
    digitalWrite(EPD_MOSI_PIN, HIGH);
    m_spi_hardware = false;
    m_spi_ready = true;
}

void delay(uint32_t ms) {
    /* Panel reset recipes and BUSY waits run inside the synchronous display
     * transaction. Bound each blocking slice so the 120 ms status timer can
     * reach GPIO promptly instead of being quantized by a 100+ ms wait. */
    while (ms != 0u) {
        uint32_t slice_ms = ms > 5u ? 5u : ms;
        delay_ms(slice_ms);
        inktag_gr_cooperative_maintenance();
        ms -= slice_ms;
    }
}

void pinMode(uint32_t pin, uint32_t mode) {
    if (!goodix_pin_valid(pin)) return;
    app_io_type_t type = goodix_pin_type(pin);
    uint32_t mask = pin_mask(pin);
    app_io_init_t init;
    memset(&init, 0, sizeof(init));
    init.pin = mask;
#if defined(INKTAG_GOODIX_GR5515)
    /* APP_IO_MUX is APP_IO_MUX_7 for GR551x GPIO operation. */
    init.mux = APP_IO_MUX;
#else
    init.mux = APP_IO_MUX_0;
#endif
    init.pull = APP_IO_NOPULL;
    switch (mode) {
        case INPUT: init.mode = APP_IO_MODE_INPUT; break;
        case INPUT_PULLUP: init.mode = APP_IO_MODE_INPUT; init.pull = APP_IO_PULLUP; break;
        case INPUT_PULLDOWN: init.mode = APP_IO_MODE_INPUT; init.pull = APP_IO_PULLDOWN; break;
        case OUTPUT: init.mode = APP_IO_MODE_OUTPUT; break;
        case DEFAULT:
        default:
            app_io_deinit(type, mask);
            return;
    }
    app_io_init(type, &init);
}

void digitalWrite(uint32_t pin, uint32_t value) {
    if (!goodix_pin_valid(pin)) return;
    app_io_write_pin(goodix_pin_type(pin), pin_mask(pin), value ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

uint32_t digitalRead(uint32_t pin) {
    if (!goodix_pin_valid(pin)) return 0;
    return app_io_read_pin(goodix_pin_type(pin), pin_mask(pin)) == APP_IO_PIN_SET ? HIGH : LOW;
}

void EPD_GPIO_Load(epd_config_t* cfg) {
    if (cfg == NULL) return;
    m_panel_read_length = 0u;
    m_last_command = 0xffu;
    EPD_MOSI_PIN = cfg->mosi_pin;
    EPD_SCLK_PIN = cfg->sclk_pin;
    EPD_CS_PIN = cfg->cs_pin;
    EPD_DC_PIN = cfg->dc_pin;
    EPD_RST_PIN = cfg->rst_pin;
    EPD_BUSY_PIN = cfg->busy_pin;
    EPD_BS_PIN = cfg->bs_pin;
    EPD_WAKEUP_PIN = cfg->wakeup_pin;
    EPD_EN_PIN = cfg->en_pin;
    EPD_LED_PIN = cfg->led_pin;
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    const inktag_pin_config_t *pins = inktag_gr_pin_routes_peek();
    m_configured_busy = pins != NULL;
    bool busy_high;
    m_physical_busy_low = inktag_gr_pin_routes_busy_high(&busy_high) && !busy_high;
    m_enable_low = pins && (pins->active_low & (1u << INKTAG_PIN_ENABLE));
#endif
#if INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW || defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (goodix_pin_valid(EPD_EN_PIN)) {
        digitalWrite(EPD_EN_PIN, epd_enable_level(false));
#if INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW
        pinMode(EPD_EN_PIN, OUTPUT);
#else
        /* A polarity change must not retain the old inactive latch, which is
         * active under the new setting. Preserve the high-active board's
         * existing released-pad mode; low-active uses its inactive pull-up. */
        pinMode(EPD_EN_PIN, m_enable_low ? INPUT_PULLUP : DEFAULT);
#endif
    }
#endif
}

#if defined(INKTAG_GR_PIN_CONFIG_PORT)
bool inktag_gr_epd_pins_idle(void) { return m_driver_refs == 0u && !m_spi_ready; }
bool inktag_gr_epd_pins_park(void) {
    if (!inktag_gr_epd_pins_idle()) return false;
    /* Called only for a previously validated configured route. Keep the
     * immutable rail owned/off even when the new configuration disables it. */
    if (goodix_pin_valid(EPD_EN_PIN)) {
        digitalWrite(EPD_EN_PIN, epd_enable_level(false));
        pinMode(EPD_EN_PIN, OUTPUT);
    }
    const uint32_t pins[] = {EPD_MOSI_PIN, EPD_SCLK_PIN, EPD_CS_PIN, EPD_DC_PIN,
                            EPD_RST_PIN, EPD_BUSY_PIN, EPD_BS_PIN, EPD_WAKEUP_PIN};
    for (unsigned i = 0u; i < sizeof(pins)/sizeof(pins[0]); ++i) pinMode(pins[i], DEFAULT);
    return true;
}
void inktag_gr_epd_pin_screen(const epd_screen_config_t *screen) {
    /* A screen save publishes new effective BUSY without reloading GPIOs.
     * Refresh this memory-only bus cache too, before the next EPD_Init. */
    const inktag_pin_config_t *pins = inktag_gr_pin_routes_peek();
    m_configured_busy = pins != NULL;
    bool busy_high;
    m_physical_busy_low = inktag_gr_pin_routes_busy_high(&busy_high) && !busy_high;
    /* SSD1680_WaitBusy uses low for BW in both registered recipes; other
     * SSD paths wait high. Physical polarity stays an independent pin fact. */
    m_common_busy_high = screen && (screen->driver_id & 0xF0u) == 0x20u &&
        !(screen->driver_id == EPD_SCREEN_DRIVER_SSD1680 &&
          screen->color_id == EPD_SCREEN_COLOR_BW);
}
#endif

void EPD_GPIO_Init(void) {
    if (m_driver_refs++ > 0) return;

    m_panel_read_length = 0u;
    m_last_command = 0xffu;
    m_spi_init_status = APP_DRV_SUCCESS;
    m_spi_last_status = APP_DRV_SUCCESS;
    m_spi_error_count = 0;

    // Match the stock ZKC21V GR5331 bring-up order:
    //   1) enable the EPD power domain (AON6 on stock boards)
    //   2) hold panel reset low for a short guard time
    //   3) idle the SPI/control pins high before the panel init sequence
    //
    // Some ESL boards gate the panel supply aggressively. Letting RST float/high
    // while the EPD rail is rising can leave the controller unresponsive even
    // though the BLE side and GPIO writes are working.
    if (EPD_EN_PIN != 0xFF) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
        /* Preserve the qualified high-active sequence; the newly supported
         * low-active setting must preload inactive before enabling output. */
        if (m_enable_low) digitalWrite(EPD_EN_PIN, epd_enable_level(false));
#endif
        pinMode(EPD_EN_PIN, OUTPUT);
        digitalWrite(EPD_EN_PIN, epd_enable_level(true));
        delay(10);
    }
    if (EPD_WAKEUP_PIN != 0xFF) {
        pinMode(EPD_WAKEUP_PIN, OUTPUT);
        digitalWrite(EPD_WAKEUP_PIN, HIGH);
        delay(10);
    }

    pinMode(EPD_RST_PIN, OUTPUT);
    digitalWrite(EPD_RST_PIN, LOW);
    delay(10);

    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_BUSY_PIN, INPUT);
    /* CS stays owned by GPIO across the small bidirectional read and SPIM
     * reconfiguration. The SDK must not release it during app_spi_deinit. */
    digitalWrite(EPD_CS_PIN, HIGH);
    pinMode(EPD_CS_PIN, OUTPUT);

    m_spi_ready = false;
    m_spi_hardware = epd_hardware_spi_supported();
    if (m_spi_hardware) {
        memset(&m_spi_params, 0, sizeof(m_spi_params));
        m_spi_params.id = APP_SPI_ID_MASTER;
        m_spi_params.is_soft_cs = true;
        m_spi_params.pin_cfg.cs.enable = APP_SPI_PIN_DISABLE;
        m_spi_params.pin_cfg.clk.type = EPD_SPI_IO_TYPE;
        m_spi_params.pin_cfg.clk.mux = EPD_SPI_CLK_MUX;
        m_spi_params.pin_cfg.clk.pin = pin_mask(EPD_SCLK_PIN);
        m_spi_params.pin_cfg.clk.mode = APP_IO_MODE_MUX;
        m_spi_params.pin_cfg.clk.pull = APP_IO_NOPULL;
        m_spi_params.pin_cfg.clk.enable = APP_SPI_PIN_ENABLE;
        m_spi_params.pin_cfg.mosi.type = EPD_SPI_IO_TYPE;
        m_spi_params.pin_cfg.mosi.mux = EPD_SPI_MOSI_MUX;
        m_spi_params.pin_cfg.mosi.pin = pin_mask(EPD_MOSI_PIN);
        m_spi_params.pin_cfg.mosi.mode = APP_IO_MODE_MUX;
        m_spi_params.pin_cfg.mosi.pull = APP_IO_NOPULL;
        m_spi_params.pin_cfg.mosi.enable = APP_SPI_PIN_ENABLE;
        m_spi_params.pin_cfg.miso.enable = APP_SPI_PIN_DISABLE;
        m_spi_params.init.data_size = SPI_DATASIZE_8BIT;
        m_spi_params.init.clock_polarity = SPI_POLARITY_LOW;
        m_spi_params.init.clock_phase = SPI_PHASE_1EDGE;
        m_spi_params.init.baudrate_prescaler = SystemCoreClock / INKTAG_BOARD_EPD_SPI_HZ;
        m_spi_params.init.ti_mode = SPI_TIMODE_DISABLE;
        m_spi_params.init.slave_select = SPI_SLAVE_SELECT_0;

        m_spi_init_status = app_spi_init(&m_spi_params, NULL);
        if (m_spi_init_status == APP_DRV_SUCCESS) {
            m_spi_ready = true;
        } else {
            app_spi_deinit(APP_SPI_ID_MASTER);
            epd_soft_spi_init();
        }
    } else {
        epd_soft_spi_init();
    }

    if (EPD_BS_PIN != 0xFF) { pinMode(EPD_BS_PIN, OUTPUT); digitalWrite(EPD_BS_PIN, LOW); }
    if (EPD_LED_PIN != 0xFF) pinMode(EPD_LED_PIN, OUTPUT);
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_LED_ON();
}

void EPD_GPIO_Uninit(void) {
    if (m_driver_refs == 0 || --m_driver_refs > 0) return;
    /* The last reference releases the powered controller.  Retaining the
     * diagnostic bytes is harmless, but they are no longer a current sensor
     * sample; ordinary status queries must not wake it or revive this cache. */
    m_panel_read_length = 0u;
    EPD_LED_OFF();
    if (m_spi_ready && m_spi_hardware) app_spi_deinit(APP_SPI_ID_MASTER);
    m_spi_ready = false;
    m_spi_hardware = false;
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
    if (EPD_EN_PIN != 0xFF) digitalWrite(EPD_EN_PIN, epd_enable_level(false));
    if (EPD_WAKEUP_PIN != 0xFF) digitalWrite(EPD_WAKEUP_PIN, LOW);
    pinMode(EPD_MOSI_PIN, DEFAULT);
    pinMode(EPD_SCLK_PIN, DEFAULT);
    pinMode(EPD_CS_PIN, DEFAULT);
    pinMode(EPD_DC_PIN, DEFAULT);
    pinMode(EPD_RST_PIN, DEFAULT);
    pinMode(EPD_BUSY_PIN, DEFAULT);
    pinMode(EPD_BS_PIN, DEFAULT);
    pinMode(EPD_WAKEUP_PIN, DEFAULT);
#if !INKTAG_BOARD_EPD_IDLE_HOLD_ENABLE_LOW
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    /* Only the new low-active route needs a different released-pad level.
     * Existing high-active board parking remains unchanged. */
    if (m_enable_low) pinMode(EPD_EN_PIN, INPUT_PULLUP);
    else
#endif
    pinMode(EPD_EN_PIN, DEFAULT);
#endif
    pinMode(EPD_LED_PIN, DEFAULT);
}

void EPD_SPI_Write(uint8_t* value, uint8_t len) {
    if (!m_spi_ready || value == NULL || len == 0) return;
    digitalWrite(EPD_CS_PIN, LOW);
    if (!m_spi_hardware) {
        for (uint8_t i = 0; i < len; i++) {
            uint8_t byte = value[i];
            for (uint8_t bit = 0; bit < 8; bit++) {
                digitalWrite(EPD_SCLK_PIN, LOW);
                digitalWrite(EPD_MOSI_PIN, (byte & 0x80) ? HIGH : LOW);
                byte <<= 1;
                digitalWrite(EPD_SCLK_PIN, HIGH);
            }
        }
        digitalWrite(EPD_SCLK_PIN, LOW);
    } else {
        uint16_t status = app_spi_transmit_sync(APP_SPI_ID_MASTER, value, len,
                                                SPI_TIMEOUT_MS);
        if (status != APP_DRV_SUCCESS) {
            m_spi_last_status = status;
            m_spi_error_count++;
        }
    }
    digitalWrite(EPD_CS_PIN, HIGH);
    /* The display path is synchronous and can keep the application dispatcher
     * occupied for several LED periods. Service a pending status edge only
     * after the transfer completes (and bit-bang CS is high), so refresh/OTA
     * feedback keeps its product cadence without disturbing an SPI frame. */
    inktag_gr_cooperative_maintenance();
}

void inktag_gr_epd_spi_diag(uint8_t *hardware, uint16_t *init_status,
                            uint32_t *error_count, uint16_t *last_status,
                            uint32_t *core_hz, uint32_t *baud_divider) {
    if (hardware != NULL) *hardware = m_spi_hardware ? 1u : 0u;
    if (init_status != NULL) *init_status = m_spi_init_status;
    if (error_count != NULL) *error_count = m_spi_error_count;
    if (last_status != NULL) *last_status = m_spi_last_status;
    if (core_hz != NULL) *core_hz = SystemCoreClock;
#if defined(INKTAG_GOODIX_GR5515)
    if (baud_divider != NULL) *baud_divider = m_spi_ready && m_spi_hardware
                                                 ? (SPIM->BAUD & SSI_BAUD_SCKDIV_Msk)
                                                 : 0u;
#elif defined(INKTAG_GOODIX_GR5331)
    if (baud_divider != NULL) *baud_divider = m_spi_ready && m_spi_hardware
                                                 ? (SPIM->BAUD & SPI_BAUD_CLK_DIV_Msk)
                                                 : 0u;
#else
    if (baud_divider != NULL) *baud_divider = 0u;
#endif
}

/* The panel uses one bidirectional SDA pad, not a separate MISO trace. Keep
 * CS low across an optional command and the entire response. Hardware SPIM
 * cannot remap its MOSI output to MISO on that same pad, so only the small
 * read transaction temporarily borrows the pins as GPIO. Bulk writes keep
 * the qualified hardware-SPI path and baud rate. */
static void epd_gpio_read_command(uint8_t cmd) {
    for (uint8_t bit = 0u; bit < 8u; bit++) {
        digitalWrite(EPD_MOSI_PIN, (cmd & 0x80u) ? HIGH : LOW);
        delay_us(2u);
        digitalWrite(EPD_SCLK_PIN, HIGH);
        delay_us(2u);
        digitalWrite(EPD_SCLK_PIN, LOW);
        cmd <<= 1u;
    }
}

static uint8_t epd_gpio_read_byte(void) {
    uint8_t value = 0u;
    for (uint8_t bit = 0u; bit < 8u; bit++) {
        value <<= 1u;
        delay_us(2u);
        if (digitalRead(EPD_MOSI_PIN)) value |= 1u;
        digitalWrite(EPD_SCLK_PIN, HIGH);
        delay_us(2u);
        digitalWrite(EPD_SCLK_PIN, LOW);
    }
    return value;
}

static bool epd_gpio_read_transaction_core(bool command, uint8_t cmd,
    uint8_t *value, uint8_t len, uint16_t skip, bool closing_byte) {
    if (!m_spi_ready || value == NULL || len == 0u) return false;
    if (skip > 5993u || (uint32_t)skip + len + (closing_byte ? 1u : 0u) > 6001u)
        return false;
    bool restore_spim = m_spi_hardware;
    bool ok = true;
    memset(value, 0xff, len);
    if (restore_spim) {
        uint16_t status = app_spi_deinit(APP_SPI_ID_MASTER);
        if (status != APP_DRV_SUCCESS) {
            m_spi_last_status = status;
            m_spi_error_count++;
            m_spi_ready = false;
            return false;
        }
    }
    pinMode(EPD_CS_PIN, OUTPUT);
    pinMode(EPD_SCLK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCLK_PIN, LOW);
    if (command) {
        digitalWrite(EPD_DC_PIN, LOW);
        digitalWrite(EPD_CS_PIN, LOW);
        epd_gpio_read_command(cmd);
    }
    digitalWrite(EPD_DC_PIN, HIGH);
    pinMode(EPD_MOSI_PIN, INPUT);
    /* A data-only read drives its MSB on CS falling. Release SDA and set DC
     * before selecting the controller, not while both ends may drive it. */
    if (!command) digitalWrite(EPD_CS_PIN, LOW);
    uint16_t total = (uint16_t)(skip + len + (closing_byte ? 1u : 0u));
    for (uint16_t i = 0u; i < total; i++) {
        uint8_t byte = epd_gpio_read_byte();
        if (i >= skip && i - skip < len) value[i - skip] = byte;
        /* Long stock trim reads retain one CS interval, a seven-byte output
         * and bounded foreground maintenance; no full OTP buffer or writer. */
        if (skip != 0u && (i & 63u) == 63u) {
            inktag_gr_cooperative_maintenance();
            if (EPD_AbortRequested()) { ok = false; break; }
        }
    }
    if (closing_byte && EPD_AbortRequested()) ok = false;
    digitalWrite(EPD_CS_PIN, HIGH);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    digitalWrite(EPD_MOSI_PIN, HIGH);
    if (restore_spim) {
        pinMode(EPD_MOSI_PIN, DEFAULT);
        pinMode(EPD_SCLK_PIN, DEFAULT);
        uint16_t status = app_spi_init(&m_spi_params, NULL);
        if (status != APP_DRV_SUCCESS) {
            m_spi_last_status = status;
            m_spi_error_count++;
            /* A failed restoration is a real I/O failure, not permission to
             * continue this frame at an unqualified GPIO clock. */
            (void)app_spi_deinit(APP_SPI_ID_MASTER);
            m_spi_ready = false;
            return false;
        }
    }
    return ok;
}

static bool epd_gpio_read_transaction(bool command, uint8_t cmd,
                                      uint8_t *value, uint8_t len) {
    return epd_gpio_read_transaction_core(command, cmd, value, len, 0u, false);
}

#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
static uint16_t m_diag_skip;
static uint8_t m_diag_left;
static bool m_diag_read_open, m_diag_bus_failed;

bool inktag_gr_panel_read_start(uint8_t command, uint16_t skip, uint8_t length) {
    if (!m_spi_ready || m_driver_refs != 1u || m_diag_read_open || !length ||
        length > 64u || (command != 0x70u && command != 0x71u && command != 0xa2u &&
                        command != 0x2du && command != 0x2fu &&
                        command != 0x40u && command != 0x92u) ||
        (command != 0xa2u && command != 0x2du && command != 0x92u && skip) ||
        (command == 0x70u && length != 1u && length != 3u && length != 5u && length != 7u) ||
        (command == 0x71u && length != 1u) ||
        (command == 0xa2u && (!skip || (uint32_t)skip + length > 6145u)) ||
        (command == 0x92u && (!skip || (uint32_t)skip + length > 4097u)) ||
        (command == 0x40u && length != 2u) ||
        (command == 0x2du && (uint32_t)skip + length > 11u) ||
        (command == 0x2fu && length != 1u)) return false;
    m_diag_bus_failed = false;
    if (m_spi_hardware) {
        uint16_t status = app_spi_deinit(APP_SPI_ID_MASTER);
        if (status != APP_DRV_SUCCESS) {
            m_spi_last_status = status;
            m_spi_error_count++;
            m_diag_bus_failed = true;
            return false;
        }
        m_spi_hardware = false;
    }
    pinMode(EPD_CS_PIN, OUTPUT);
    pinMode(EPD_SCLK_PIN, OUTPUT);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    digitalWrite(EPD_CS_PIN, HIGH);
    digitalWrite(EPD_SCLK_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    digitalWrite(EPD_DC_PIN, LOW);
    epd_gpio_read_command(command);
    digitalWrite(EPD_DC_PIN, HIGH);
    pinMode(EPD_MOSI_PIN, INPUT);
    m_diag_skip = skip;
    m_diag_left = length;
    m_diag_read_open = true;
    return true;
}

bool inktag_gr_panel_read_poll(uint8_t *output, uint8_t capacity,
                              uint8_t *written, bool *done) {
    if (!m_diag_read_open || !output || !written || !done || capacity < m_diag_left)
        return false;
    *written = 0u;
    *done = false;
    /* At most 64 bytes/2.1ms of deliberate bit delays per scheduler turn.
     * Retain one CS transaction while discarding the documented OTP prefix;
     * never invent random-access OTP addresses or allocate a full OTP buffer. */
    for (uint8_t count = 0u; count < 64u && (m_diag_skip || m_diag_left); count++) {
        uint8_t value = epd_gpio_read_byte();
        if (m_diag_skip) m_diag_skip--;
        else { output[(*written)++] = value; m_diag_left--; }
    }
    if (!m_diag_skip && !m_diag_left) {
        digitalWrite(EPD_CS_PIN, HIGH);
        pinMode(EPD_MOSI_PIN, OUTPUT);
        digitalWrite(EPD_MOSI_PIN, HIGH);
        m_diag_read_open = false;
        *done = true;
    }
    return true;
}

bool inktag_gr_panel_read_power_off(void) {
    /* Read operations never issue PON, DRF, programming or voltage commands.
     * The fitted board supply switch must be present. Check its driven level
     * before releasing GPIOs; failed cleanup remains latched by common. */
    if (m_driver_refs != 1u || !goodix_pin_valid(EPD_EN_PIN)) return false;
    digitalWrite(EPD_CS_PIN, HIGH);
    pinMode(EPD_MOSI_PIN, OUTPUT);
    digitalWrite(EPD_MOSI_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
    digitalWrite(EPD_EN_PIN, epd_enable_level(false));
    if (goodix_pin_valid(EPD_WAKEUP_PIN)) digitalWrite(EPD_WAKEUP_PIN, LOW);
    m_diag_read_open = false;
    m_diag_skip = 0u;
    m_diag_left = 0u;
    return !m_diag_bus_failed && digitalRead(EPD_EN_PIN) == epd_enable_level(false) &&
        (!goodix_pin_valid(EPD_WAKEUP_PIN) || digitalRead(EPD_WAKEUP_PIN) == LOW);
}

bool inktag_gr_panel_diag_write(uint8_t command, const uint8_t *data, uint8_t length) {
    /* Private primitive for common's fixed A0 sequence, not a raw BLE command.
     * Fail closed even if called incorrectly by a future adapter. */
    bool allowed = (command==0x00u && length==1u && data && data[0]==0x0bu) ||
        (command==0x61u && length==4u && data &&
            !memcmp(data,(const uint8_t[]){3u,0u,2u,0x28u},4u)) ||
        (command==0x04u && !length) ||
        (command==0x02u && length==1u && data && !data[0]) ||
        (command==0x83u && length==9u && data &&
            !memcmp(data,(const uint8_t[]){0,0,2,255,0,0,0,0,1},9u)) ||
        (command==0x10u && length==192u && data);
    if (!allowed || !m_spi_ready || m_driver_refs!=1u || m_diag_read_open)
        return false;
    if (command==0x10u)
        for (uint8_t i=0u;i<length;i++) if(data[i]!=0x55u) return false;
    uint32_t before=m_spi_error_count;
    EPD_WriteCmd(command);
    if(length) EPD_WriteData((uint8_t *)data,length);
    return m_spi_ready && m_spi_error_count==before;
}
#endif

static void epd_cache_panel_read(uint8_t command, const uint8_t *value,
                                 uint8_t len) {
    if ((command != 0x40u && command != 0x1bu) || value == NULL || len == 0u)
        return;
    m_panel_read_command = command;
    m_panel_read_length = len > sizeof(m_panel_read_raw)
                              ? (uint8_t)sizeof(m_panel_read_raw) : len;
    memcpy(m_panel_read_raw, value, m_panel_read_length);
}

bool inktag_gr_epd_panel_temperature_raw(uint8_t *command, uint8_t raw[2],
                                          uint8_t *length) {
    if (!m_spi_ready || m_driver_refs == 0u ||
        command == NULL || raw == NULL || length == NULL ||
        m_panel_read_length == 0u) return false;
    *command = m_panel_read_command;
    *length = m_panel_read_length;
    memcpy(raw, m_panel_read_raw, m_panel_read_length);
    return true;
}

void inktag_gr_epd_panel_temperature_invalidate(void) {
    m_panel_read_length = 0u;
}

void EPD_SPI_Read(uint8_t* value, uint8_t len) {
    if (value == NULL || len == 0u) return;
    memset(value, 0xff, len);
    m_panel_read_length = 0u;
    if (epd_gpio_read_transaction(false, 0u, value, len))
        epd_cache_panel_read(m_last_command, value, len);
}

void EPD_WriteCmd(uint8_t cmd) {
    m_last_command = cmd;
    digitalWrite(EPD_DC_PIN, LOW);
    EPD_SPI_Write(&cmd, 1);
}
void EPD_WriteData(uint8_t* value, uint8_t len) { digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Write(value, len); }
void EPD_ReadData(uint8_t* value, uint8_t len) { digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Read(value, len); }
bool EPD_ReadCommand(uint8_t cmd, uint8_t* value, uint8_t len) {
    m_panel_read_length = 0u;
    m_last_command = cmd;
    bool ok = epd_gpio_read_transaction(true, cmd, value, len);
    if (ok) epd_cache_panel_read(cmd, value, len);
    return ok;
}
#if defined(INKTAG_GR_CLOCK_PARTIAL_SSD1683) && INKTAG_GR_CLOCK_PARTIAL_SSD1683
#include "inktag_uc8276_clock.h"
bool inktag_gr_uc8276_clock_native_settings(int8_t temperature,uint8_t *psr2,uint8_t settings[9]) {
    uint8_t header[40],again[9],bin;
    if (!psr2 || !settings || m_driver_refs!=1u || EPD_AbortRequested() ||
        !epd_gpio_read_transaction_core(true,0xa2u,header,sizeof header,1u,false) ||
        !inktag_uc8276_clock_native_bin(header,temperature,&bin)) return false;
    uint16_t skip=(uint16_t)(1u+0x28u+(uint16_t)bin*233u);
    if (!epd_gpio_read_transaction_core(true,0xa2u,settings,9u,skip,false) ||
        !epd_gpio_read_transaction_core(true,0xa2u,again,9u,skip,false) ||
        memcmp(settings,again,9u) || !inktag_uc8276_clock_native_settings_valid(settings)) return false;
    *psr2=header[3];
    return !EPD_AbortRequested();
}
bool inktag_gr_uc8276_clock_status(uint8_t *status) {
    return status && m_driver_refs==1u && !EPD_AbortRequested() &&
        epd_gpio_read_transaction_core(true,0x71u,status,1u,0u,false);
}
static bool gr_clock_ssd_options(uint8_t *status,uint8_t *metadata,uint8_t length) {
    /* Keep the native R1B temperature cache intact. EPD_ReadCommand deliberately
     * invalidates it before a general read, so use this fixed private read set. */
    if (!status || !metadata || m_driver_refs!=1u || EPD_AbortRequested()) return false;
    return epd_gpio_read_transaction_core(true,0x2fu,status,1u,0u,false) &&
        epd_gpio_read_transaction_core(true,0x2du,metadata,length,0u,false);
}
bool inktag_gr_ssd1683_clock_metadata(uint8_t *status,uint8_t metadata[21]) {
    return gr_clock_ssd_options(status,metadata,11u) &&
        epd_gpio_read_transaction_core(true,0x2eu,metadata+11u,10u,0u,false);
}
bool inktag_gr_ssd1619_clock_metadata(uint8_t *status,uint8_t metadata[8]) {
    /* SSD1619A R2D has8 documented bytes. Do not overread it as the
     * SSD1683's11-byte options or require an unrelated OTP User ID. */
    return gr_clock_ssd_options(status,metadata,8u);
}
#endif
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
bool inktag_gr_clock_partial_read_ram(uint8_t *value,uint8_t length) {
    /* Private qualified clock RAM read, one dummy, one selected row. The
     * standard SSD1683 backend needs50B; small/internal retain their16B bound. */
#if defined(INKTAG_GR_CLOCK_PARTIAL_SSD1683) && INKTAG_GR_CLOCK_PARTIAL_SSD1683
    const uint8_t row_limit=50u;
#else
    const uint8_t row_limit=16u;
#endif
    if (!value || !length || length>row_limit || m_driver_refs!=1u ||
        EPD_AbortRequested()) return false;
    return epd_gpio_read_transaction_core(true,0x27u,value,length,1u,false);
}
#endif
#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
bool EPD_ReadCommandWindow(uint8_t cmd, uint16_t skip, uint8_t *value, uint8_t len) {
    /* These are the two delivered Small-Q stock trim-presence reads only.
     * A valid software transaction does not qualify a panel/PCB electrically. */
    if (cmd != 0x92u || (skip != 3945u && skip != 5993u) || !value ||
        len != 7u || m_driver_refs != 1u || EPD_AbortRequested()) return false;
    m_panel_read_length = 0u;
    m_last_command = cmd;
    return epd_gpio_read_transaction_core(true, cmd, value, len, skip, true);
}
#endif
void EPD_WriteByte(uint8_t value) { digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Write(&value, 1); }
uint8_t EPD_ReadByte(void) { uint8_t value = 0; digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Read(&value, 1); return value; }

void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len) {
    uint8_t buffer[BUFFER_SIZE];
    memset(buffer, value, sizeof(buffer));
    EPD_WriteCmd(cmd);
    while (len > 0) {
        uint8_t chunk = (len > BUFFER_SIZE) ? BUFFER_SIZE : (uint8_t)len;
        EPD_WriteData(buffer, chunk);
        len -= chunk;
    }
}

void EPD_Reset(bool status, uint16_t duration) {
    digitalWrite(EPD_RST_PIN, status);
    delay(duration);
    digitalWrite(EPD_RST_PIN, status ? LOW : HIGH);
    delay(duration);
    digitalWrite(EPD_RST_PIN, status);
    delay(duration);
}

void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms) {
    digitalWrite(EPD_RST_PIN, HIGH);
    delay(high_before_ms);
    digitalWrite(EPD_RST_PIN, LOW);
    delay(low_ms);
    digitalWrite(EPD_RST_PIN, HIGH);
    delay(high_after_ms);
}

void EPD_ResetDualPulse(uint16_t duration) {
    digitalWrite(EPD_RST_PIN, LOW);
    delay(duration);
    digitalWrite(EPD_RST_PIN, HIGH);
    delay(duration);
    digitalWrite(EPD_RST_PIN, LOW);
    delay(duration);
    digitalWrite(EPD_RST_PIN, HIGH);
    delay(duration);
}

bool EPD_ReadBusy(void) {
    bool raw = digitalRead(EPD_BUSY_PIN);
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (m_configured_busy) {
        bool active = m_physical_busy_low ? !raw : raw;
        return m_common_busy_high ? active : !active;
    }
#endif
    return raw;
}

void EPD_SetAbortCallback(epd_abort_callback_t callback) { m_abort_callback = callback; }

bool EPD_AbortRequested(void) { return m_abort_callback != NULL && m_abort_callback(); }

bool EPD_BusyDiagAsserted(void) { return m_busy_diag_asserted; }
uint32_t EPD_BusyDiagAssertDelayMs(void) { return m_busy_diag_assert_delay_ms; }
uint32_t EPD_BusyDiagActiveMs(void) { return m_busy_diag_active_ms; }

void EPD_BusyDiagReset(void) {
    m_busy_diag_asserted = false;
    m_busy_diag_assert_delay_ms = 0;
    m_busy_diag_active_ms = 0;
}

bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout, uint16_t release_timeout) {
    uint32_t led_status = digitalRead(EPD_LED_PIN);
    EPD_BusyDiagReset();

    /*
     * BUSY is not guaranteed to assert in the same instant that the final
     * command byte is clocked.  Sampling only once here races the controller:
     * an idle level is mistaken for an already-completed operation.  First
     * catch the transition into the requested busy state, then wait for the
     * controller to return to idle.  The assertion window is a ceiling, not a
     * fixed delay, and exits as soon as the edge is observed.
     */
    while (EPD_ReadBusy() != status && assert_timeout > 0) {
        if (EPD_AbortRequested()) break;
        if ((assert_timeout % INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS) == 0u)
            app_feed_wdt();
        /* Goodix BLE scheduling is interrupt-driven. delay() also services
         * pending status edges at a bounded cadence. */
        delay(BUSY_POLL_INTERVAL_MS);
        m_busy_diag_assert_delay_ms++;
        assert_timeout -= BUSY_POLL_INTERVAL_MS;
    }
    m_busy_diag_asserted = EPD_ReadBusy() == status;

    while (EPD_ReadBusy() == status && release_timeout > 0) {
        if (EPD_AbortRequested()) break;
        if ((release_timeout % INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS) == 0u) {
            app_feed_wdt();
            EPD_LED_Toggle();
        }
        delay(BUSY_POLL_INTERVAL_MS);
        m_busy_diag_active_ms++;
        release_timeout -= BUSY_POLL_INTERVAL_MS;
    }
    if (led_status == LOW) EPD_LED_ON(); else EPD_LED_OFF();
    return !EPD_AbortRequested() && m_busy_diag_asserted && EPD_ReadBusy() != status;
}

void EPD_WaitBusy(bool status, uint16_t timeout) {
    uint16_t assert_timeout = timeout < BUSY_ASSERT_TIMEOUT_MS ? timeout : BUSY_ASSERT_TIMEOUT_MS;
    (void)EPD_WaitBusyEdge(status, assert_timeout, timeout);
}

void EPD_LED_ON(void) { if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, LOW); }
void EPD_LED_OFF(void) { if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, HIGH); }
void EPD_LED_Toggle(void) { if (goodix_pin_valid(EPD_LED_PIN)) app_io_toggle_pin(goodix_pin_type(EPD_LED_PIN), pin_mask(EPD_LED_PIN)); }
void EPD_LED_BLINK(void) { if (EPD_LED_PIN != 0xFF) { pinMode(EPD_LED_PIN, OUTPUT); EPD_LED_ON(); delay(100); EPD_LED_OFF(); delay(100); pinMode(EPD_LED_PIN, DEFAULT); } }
uint16_t EPD_ReadVoltage(void) {
    return inktag_gr_health_voltage_mv(false);
}
