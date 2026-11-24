/* IO behavior: docs/research/2026-11-04-epd-io-rewrite-spec.md.
 * GPIO lifecycle, transfer and pulse primitives are independently structured
 * from that contract. Existing InkTag board/read/wait/SAADC extensions retain
 * their source provenance; this is not a file-wide relicensing declaration. */
#include "inktag_epd_driver.h"
#include <string.h>
#include <stddef.h>
#include "inktag_epd_wait_policy.h"

#if defined(INKTAG_DIALOG)
extern void app_epd_trace(const char* tag);
#define EPD_BUSY_TRACE(_tag) app_epd_trace(_tag)
#else
#define EPD_BUSY_TRACE(_tag) ((void)0)
#endif

#include "app_error.h"
#include "nrf_drv_spi.h"
#if defined(INKTAG_NRF_SDK17)
#include "inktag_board.h"
#include "inktag_nrf_epd_io.h"
#include "inktag_nrf_io_config.h"
#endif

#define BUFFER_SIZE 128
#define EPD_WAIT_SERVICE_MASK 0x3fu

_Static_assert((EPD_WAIT_SERVICE_MASK + 1u) <=
                   INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS,
               "EPD maintenance cadence exceeds product policy");
_Static_assert((EPD_WAIT_SERVICE_MASK + 1u) <=
                   INKTAG_EPD_WAIT_CANCEL_MAX_MS,
               "EPD cancellation cadence exceeds product policy");

static epd_abort_callback_t m_abort_callback = NULL;
static epd_maintenance_callback_t m_maintenance_callback = NULL;
static bool m_busy_diag_asserted = false;
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
static ret_code_t m_spi_last_error = NRF_SUCCESS;
static uint8_t m_last_command;
static uint16_t m_busy_first_failure = UINT16_MAX;
#define EPD_RECORD_BUSY_FAILURE(reason) do { \
    if (m_busy_first_failure == UINT16_MAX) \
        m_busy_first_failure = ((uint16_t)m_last_command << 8u) | (reason); \
} while (0)
#else
#define EPD_RECORD_BUSY_FAILURE(reason) ((void)0)
#endif
static uint32_t m_busy_diag_assert_delay_ms = 0;
static uint32_t m_busy_diag_active_ms = 0;
#ifndef INKTAG_EPD_SPI_FREQUENCY
#define INKTAG_EPD_SPI_FREQUENCY NRF_DRV_SPI_FREQ_4M
#endif
#ifndef INKTAG_EPD_SPI_SOFTWARE_CS
#define INKTAG_EPD_SPI_SOFTWARE_CS 0
#endif
#ifndef INKTAG_EPD_BUSY_INPUT_MODE
#define INKTAG_EPD_BUSY_INPUT_MODE INPUT
#endif

extern void app_feed_wdt(void);  // Feed the watchdog timer (main.c)

/* The pin vector follows teardown order. Values are runtime board data;
 * only the optional final three entries use255 as absence. */
enum epd_pad { PAD_MOSI, PAD_SCLK, PAD_CS, PAD_DC, PAD_RESET, PAD_BUSY,
               PAD_BS, PAD_ENABLE, PAD_LED, PAD_COUNT };
static uint32_t epd_pads[PAD_COUNT] = {5, 8, 9, 10, 11, 12, 13, 255, 255};
static bool EPD_LED_ENABLED = true;
#define EPD_MOSI_PIN epd_pads[PAD_MOSI]
#define EPD_SCLK_PIN epd_pads[PAD_SCLK]
#define EPD_CS_PIN epd_pads[PAD_CS]
#define EPD_DC_PIN epd_pads[PAD_DC]
#define EPD_RST_PIN epd_pads[PAD_RESET]
#define EPD_BUSY_PIN epd_pads[PAD_BUSY]
#define EPD_BS_PIN epd_pads[PAD_BS]
#define EPD_EN_PIN epd_pads[PAD_ENABLE]
#define EPD_LED_PIN epd_pads[PAD_LED]

#define SPI_INSTANCE 0                                               /**< SPI instance index. */
static const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE); /**< SPI instance. */

#if defined(INKTAG_NRF_SDK17)
#define HAL_SPI_INSTANCE spi.u.spi.p_reg
#else
#define HAL_SPI_INSTANCE spi.p_registers
nrf_gpio_pin_dir_t nrf_gpio_pin_dir_get(uint32_t pin) {
    NRF_GPIO_Type* reg = nrf_gpio_pin_port_decode(&pin);
    return (nrf_gpio_pin_dir_t)((reg->PIN_CNF[pin] & GPIO_PIN_CNF_DIR_Msk) >> GPIO_PIN_CNF_DIR_Pos);
}
#endif

/* SDK GPIO mode adapter. */
void pinMode(uint32_t pin, uint32_t mode) {
    if (mode == OUTPUT) {
        nrf_gpio_cfg_output(pin);
    } else if (mode <= INPUT_PULLDOWN) {
        nrf_gpio_cfg_input(pin, mode == INPUT_PULLUP ? NRF_GPIO_PIN_PULLUP :
                               mode == INPUT_PULLDOWN ? NRF_GPIO_PIN_PULLDOWN :
                               NRF_GPIO_PIN_NOPULL);
    } else {
        nrf_gpio_cfg_default(pin);
    }
}

/* Display bus ownership. */
static uint16_t m_driver_refs = 0;
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
static bool m_board_prepared = false;
#endif

void EPD_GPIO_Load(epd_config_t* cfg) {
    if (!cfg) return;
    static const uint8_t offsets[PAD_COUNT] = {
        offsetof(epd_config_t, mosi_pin), offsetof(epd_config_t, sclk_pin),
        offsetof(epd_config_t, cs_pin), offsetof(epd_config_t, dc_pin),
        offsetof(epd_config_t, rst_pin), offsetof(epd_config_t, busy_pin),
        offsetof(epd_config_t, bs_pin), offsetof(epd_config_t, en_pin),
        offsetof(epd_config_t, led_pin)
    };
    const uint8_t* record = (const uint8_t*)cfg;
    for (unsigned pad = 0; pad != PAD_COUNT; ++pad)
        epd_pads[pad] = record[offsets[pad]];
#if defined(INKTAG_BOARD_EPD_PREPARE_PIN)
    if (EPD_EN_PIN == INKTAG_BOARD_EPD_PREPARE_PIN) EPD_EN_PIN = 255;
#endif
}

#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
/* Board-only preparation belongs to the lifetime of one physical display
 * operation, not to a BLE connection or a cached SPI reference. PriceETag's
 * stock image runs this sequence immediately before initializing its display
 * bus, holds P0.07 low for the complete transfer/refresh, then drives it high
 * again. Keeping it separate from profile EN also avoids exposing an
 * unverified external circuit identity to the host. */
void EPD_BoardPrepare(void) {
    if (m_board_prepared) return;
#if defined(INKTAG_BOARD_EPD_PREPARE_PIN)
    /* Match the stock order exactly: first let P0.08-P0.13 settle for 50 ms,
     * then configure P0.07 as an output and begin with the 1 ms low phase.
     * On the first boot its OUT latch is still zero; on later operations the
     * preceding release has left it high during this guard interval. */
    delay(INKTAG_BOARD_EPD_PREPARE_DELAY_MS);
    pinMode(INKTAG_BOARD_EPD_PREPARE_PIN, OUTPUT);
    for (uint8_t i = 0; i < INKTAG_BOARD_EPD_PREPARE_PULSE_COUNT; i++) {
        digitalWrite(INKTAG_BOARD_EPD_PREPARE_PIN, LOW);
        delay(INKTAG_BOARD_EPD_PREPARE_LOW_MS);
        digitalWrite(INKTAG_BOARD_EPD_PREPARE_PIN, HIGH);
        delay(INKTAG_BOARD_EPD_PREPARE_HIGH_MS);
    }
    digitalWrite(INKTAG_BOARD_EPD_PREPARE_PIN, LOW);
#endif
    m_board_prepared = true;
}

void EPD_BoardRelease(void) {
    if (!m_board_prepared) return;
#if defined(INKTAG_BOARD_EPD_PREPARE_PIN)
    /* The factory image leaves P0.07 configured as an output and returns it
     * high after the panel/SPI teardown. */
    pinMode(INKTAG_BOARD_EPD_PREPARE_PIN, OUTPUT);
    digitalWrite(INKTAG_BOARD_EPD_PREPARE_PIN, HIGH);
#endif
    m_board_prepared = false;
}

bool EPD_BoardIsPrepared(void) { return m_board_prepared; }
#endif

static void epd_gpio_init(bool refresh_led) {
    if (m_driver_refs++) return;
    for (unsigned pad = PAD_DC; pad <= PAD_RESET; ++pad)
        pinMode(epd_pads[pad], OUTPUT);
    pinMode(EPD_BUSY_PIN, INKTAG_EPD_BUSY_INPUT_MODE);

    nrf_drv_spi_config_t bus = NRF_DRV_SPI_DEFAULT_CONFIG;
    bus.sck_pin = EPD_SCLK_PIN;
    bus.mosi_pin = EPD_MOSI_PIN;
#if INKTAG_EPD_SPI_SOFTWARE_CS
    pinMode(EPD_CS_PIN, OUTPUT);
    digitalWrite(EPD_CS_PIN, HIGH);
    bus.ss_pin = NRF_DRV_SPI_PIN_NOT_USED;
#else
    bus.ss_pin = EPD_CS_PIN;
#endif
    bus.frequency = INKTAG_EPD_SPI_FREQUENCY;
#if defined(INKTAG_EPD_SPI_MODE)
    bus.mode = INKTAG_EPD_SPI_MODE;
#endif
#if defined(INKTAG_EPD_NATIVE_SPI_DRIVER_ID)
    if (epd_runtime_driver_id() == INKTAG_EPD_NATIVE_SPI_DRIVER_ID) {
        bus.frequency = INKTAG_EPD_NATIVE_SPI_FREQUENCY;
        bus.mode = INKTAG_EPD_NATIVE_SPI_MODE;
    }
#endif
#if defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &bus, NULL, NULL));
#else
    APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &bus, NULL));
#endif
    for (unsigned pad = PAD_BS; pad <= PAD_ENABLE; ++pad) {
        if (epd_pads[pad] == 255) continue;
        pinMode(epd_pads[pad], OUTPUT);
        digitalWrite(epd_pads[pad], pad == PAD_ENABLE ? HIGH : LOW);
    }
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_RST_PIN, HIGH);
    if (EPD_LED_PIN != 255) {
#if !defined(INKTAG_NRF_INDICATOR_ENABLED) || !INKTAG_NRF_INDICATOR_ENABLED
        /* Preload inactive before driving the pad: cold OUT can be low. */
        if (!refresh_led) digitalWrite(EPD_LED_PIN, HIGH);
#endif
        pinMode(EPD_LED_PIN, OUTPUT);
    }
#if defined(INKTAG_NRF_INDICATOR_ENABLED) && INKTAG_NRF_INDICATOR_ENABLED
    (void)refresh_led; /* The status owner preserves other active scenes. */
    if (!EPD_LED_ENABLED) EPD_LED_OFF();
#else
    if (refresh_led && EPD_LED_ENABLED) EPD_LED_ON(); else EPD_LED_OFF();
#endif
}

void EPD_GPIO_Init(void) { epd_gpio_init(true); }

#if defined(INKTAG_NRF_CLOCK_PARTIAL_400X300) && INKTAG_NRF_CLOCK_PARTIAL_400X300
void EPD_NrfClockGPIOInit(void) { epd_gpio_init(false); }
#endif

void EPD_GPIO_Uninit(void) {
    if (!m_driver_refs) return;
    if (--m_driver_refs) return;
    EPD_LED_OFF();
    EPD_BoardRelease();
    nrf_drv_spi_uninit(&spi);
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
    if (EPD_EN_PIN != 255) digitalWrite(EPD_EN_PIN, LOW);
    for (unsigned pad = 0; pad != PAD_COUNT; ++pad) {
        if (pad >= PAD_BS && epd_pads[pad] == 255) continue;
        pinMode(epd_pads[pad], DEFAULT);
    }
}

/* One synchronous half-duplex transaction. Direction changes own both the
 * pad and peripheral connection; CS is balanced before propagating errors. */
static void epd_transfer(uint8_t* data, uint8_t length, bool receive) {
    nrf_gpio_pin_dir_t wanted = receive ? NRF_GPIO_PIN_DIR_INPUT : NRF_GPIO_PIN_DIR_OUTPUT;
    if (nrf_gpio_pin_dir_get(EPD_MOSI_PIN) != wanted) {
        pinMode(EPD_MOSI_PIN, receive ? INPUT : OUTPUT);
        nrf_spi_pins_set(HAL_SPI_INSTANCE, EPD_SCLK_PIN,
            receive ? NRF_SPI_PIN_NOT_CONNECTED : EPD_MOSI_PIN,
            receive ? EPD_MOSI_PIN : NRF_SPI_PIN_NOT_CONNECTED);
    }
#if INKTAG_EPD_SPI_SOFTWARE_CS
    digitalWrite(EPD_CS_PIN, LOW);
#endif
    ret_code_t result = nrf_drv_spi_transfer(&spi,
        receive ? NULL : data, receive ? 0 : length,
        receive ? data : NULL, receive ? length : 0);
#if INKTAG_EPD_SPI_SOFTWARE_CS
    digitalWrite(EPD_CS_PIN, HIGH);
#endif
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    if (m_spi_last_error == NRF_SUCCESS) m_spi_last_error = result;
#else
    APP_ERROR_CHECK(result);
#endif
}

void EPD_SPI_Write(uint8_t* value, uint8_t len) { epd_transfer(value, len, false); }
void EPD_SPI_Read(uint8_t* value, uint8_t len) { epd_transfer(value, len, true); }

#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
uint32_t EPD_SPI_LastError(void) { return m_spi_last_error; }
void EPD_SPI_ClearError(void) { m_spi_last_error = NRF_SUCCESS; }
#endif

/* Controller bus phases. */
void EPD_WriteCmd(uint8_t cmd) {
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    m_last_command = cmd;
#endif
    digitalWrite(EPD_DC_PIN, LOW);
    EPD_SPI_Write(&cmd, 1);
}

void EPD_WriteData(uint8_t* value, uint8_t len) {
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Write(value, len);
}

void EPD_ReadData(uint8_t* value, uint8_t len) {
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Read(value, len);
}

#if defined(INKTAG_NRF_SDK17) && INKTAG_EPD_SPI_SOFTWARE_CS && \
    defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
static bool EPD_TemperatureReadLevel(bool idle, uint16_t remaining) {
    for (;;) {
        if (EPD_AbortRequested()) return false;
        if (EPD_ReadBusy() == idle) return true;
        if (!remaining--) return false;
        delay(1u);
        app_feed_wdt();
        if (m_maintenance_callback != NULL) m_maintenance_callback();
    }
}

static bool EPD_OsptekReadReady(uint8_t command) {
    nrf_delay_us(4u);
    /* Do not reuse the refresh diagnostic collector for a sensor conversion.
     * Bounds match the accepted E6 transaction, not its PON policy. */
    return command == 0x70u ? !EPD_AbortRequested() :
        EPD_TemperatureReadLevel(false, 100u) &&
        EPD_TemperatureReadLevel(true, 2000u);
}
#endif

bool EPD_ReadCommand(uint8_t cmd, uint8_t* value, uint8_t len) {
#if defined(INKTAG_NRF_SDK17) && INKTAG_EPD_SPI_SOFTWARE_CS && \
    defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    uint8_t driver = epd_runtime_driver_id();
    bool ssd = driver == EPD_SCREEN_DRIVER_SSD1675 &&
        cmd == SSD1xxx_TSENSOR_READ && len == 2u;
    bool osprey = (driver == EPD_SCREEN_DRIVER_IST7513 ||
                   driver == EPD_SCREEN_DRIVER_IST7601) &&
        ((cmd == 0x70u && len == 7u) || (cmd == 0x40u && len == 1u));
    bool jd = (driver == EPD_SCREEN_DRIVER_JD79665AA ||
               driver == EPD_SCREEN_DRIVER_JD79665CA) && cmd == 0x40u && len == 2u;
    /* No arbitrary register reader or unreviewed UC transaction. The
     * common E6 callback additionally validates exact profile/state/R70. */
    if ((!ssd && !osprey && !jd) || value == NULL || m_driver_refs == 0u ||
        !EPD_BoardIsPrepared() || EPD_AbortRequested() ||
        m_spi_last_error != NRF_SUCCESS) return false;
    m_last_command = cmd;
    ret_code_t result = inktag_nrf_epd_command_read(&spi, HAL_SPI_INSTANCE,
        EPD_SCLK_PIN, EPD_MOSI_PIN, EPD_CS_PIN, EPD_DC_PIN, cmd, value, len,
        osprey ? EPD_OsptekReadReady : NULL);
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    if (result != NRF_SUCCESS && m_spi_last_error == NRF_SUCCESS)
        m_spi_last_error = result;
#endif
    return result == NRF_SUCCESS;
#else
    (void)cmd;
    (void)value;
    (void)len;
    /* This hardware-SPI adapter cannot change DC while retaining CS. */
    return false;
#endif
}

#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
#if defined(INKTAG_NRF_SDK17) && INKTAG_EPD_SPI_SOFTWARE_CS && defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
static void EPD_TrimMaintenance(void) {
    app_feed_wdt();
    if (m_maintenance_callback != NULL) m_maintenance_callback();
}
#endif
bool EPD_ReadCommandWindow(uint8_t cmd, uint16_t skip, uint8_t *value, uint8_t len) {
#if defined(INKTAG_NRF_SDK17) && INKTAG_EPD_SPI_SOFTWARE_CS && defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    if (m_driver_refs == 0u || !EPD_BoardIsPrepared() ||
        epd_runtime_driver_id() != EPD_SCREEN_DRIVER_JD79XXX) return false;
    ret_code_t result = inktag_nrf_epd_trim_read(&spi, HAL_SPI_INSTANCE,
        EPD_SCLK_PIN, EPD_MOSI_PIN, EPD_CS_PIN, EPD_DC_PIN,
        cmd, skip, value, len, EPD_AbortRequested, EPD_TrimMaintenance);
    if (result != NRF_SUCCESS && m_spi_last_error == NRF_SUCCESS) m_spi_last_error = result;
    return result == NRF_SUCCESS;
#else
    (void)cmd; (void)skip; (void)value; (void)len;
    return false;
#endif
}
#endif

#if defined(INKTAG_NRF_CLOCK_PARTIAL_400X300) && INKTAG_NRF_CLOCK_PARTIAL_400X300
static void EPD_NrfClockMaintenance(void) {
    app_feed_wdt();if(m_maintenance_callback)m_maintenance_callback();
}
bool EPD_NrfClockRead(uint8_t command,uint16_t skip,uint8_t *value,uint8_t length) {
#if defined(INKTAG_NRF_SDK17) && INKTAG_EPD_SPI_SOFTWARE_CS && defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    uint8_t driver=epd_runtime_driver_id();
    if ((driver!=EPD_SCREEN_DRIVER_SSD1619 && driver!=EPD_SCREEN_DRIVER_SSD1683 &&
         driver!=EPD_SCREEN_DRIVER_UC8276) ||
        m_driver_refs!=1u || !EPD_BoardIsPrepared() || EPD_AbortRequested() ||
        m_spi_last_error!=NRF_SUCCESS) return false;
    ret_code_t result=inktag_nrf_epd_clock_read(&spi,HAL_SPI_INSTANCE,
        EPD_SCLK_PIN,EPD_MOSI_PIN,EPD_CS_PIN,EPD_DC_PIN,
        driver==EPD_SCREEN_DRIVER_UC8276,command,skip,value,length,
        EPD_AbortRequested,EPD_NrfClockMaintenance);
    if(result!=NRF_SUCCESS && m_spi_last_error==NRF_SUCCESS)m_spi_last_error=result;
    return result==NRF_SUCCESS;
#else
    (void)command;(void)skip;(void)value;(void)length;return false;
#endif
}
#endif
void EPD_WriteByte(uint8_t value) { EPD_WriteData(&value, 1); }

uint8_t EPD_ReadByte(void) {
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
    uint8_t value = 0xFF;
#else
    uint8_t value;
#endif
    digitalWrite(EPD_DC_PIN, HIGH);
    EPD_SPI_Read(&value, 1);
    return value;
}

void EPD_FillRAM(uint8_t cmd, uint8_t value, uint32_t len) {
    uint8_t repeated[BUFFER_SIZE];
    memset(repeated, value, sizeof repeated);
    EPD_WriteCmd(cmd);
    while (len) {
        uint8_t count = len < sizeof repeated ? (uint8_t)len : sizeof repeated;
        EPD_WriteData(repeated, count);
        len -= count;
    }
}

static void epd_reset_phase(bool level, uint16_t duration) {
    digitalWrite(EPD_RST_PIN, level);
    delay(duration);
}

void EPD_Reset(bool status, uint16_t duration) {
    epd_reset_phase(status, duration);
    epd_reset_phase(!status, duration);
    epd_reset_phase(status, duration);
}

void EPD_ResetPulse(uint16_t high_before_ms, uint16_t low_ms,
                    uint16_t high_after_ms) {
    epd_reset_phase(HIGH, high_before_ms);
    epd_reset_phase(LOW, low_ms);
    epd_reset_phase(HIGH, high_after_ms);
}

void EPD_ResetDualPulse(uint16_t duration) {
    for (unsigned pulse = 0; pulse != 2; ++pulse) {
        epd_reset_phase(LOW, duration);
        epd_reset_phase(HIGH, duration);
    }
}

bool EPD_ReadBusy(void) {
#if defined(INKTAG_NRF_SDK17)
    return inktag_nrf_io_read_busy(digitalRead(EPD_BUSY_PIN));
#else
    return digitalRead(EPD_BUSY_PIN);
#endif
}

void EPD_SetAbortCallback(epd_abort_callback_t callback) { m_abort_callback = callback; }

void EPD_SetMaintenanceCallback(epd_maintenance_callback_t callback) {
    m_maintenance_callback = callback;
}

bool EPD_AbortRequested(void) { return m_abort_callback != NULL && m_abort_callback(); }

bool EPD_BusyDiagAsserted(void) { return m_busy_diag_asserted; }
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
void EPD_BusyFailureReset(void) { m_busy_first_failure = UINT16_MAX; }
uint16_t EPD_BusyFirstFailure(void) { return m_busy_first_failure; }
#endif
uint32_t EPD_BusyDiagAssertDelayMs(void) { return m_busy_diag_assert_delay_ms; }
uint32_t EPD_BusyDiagActiveMs(void) { return m_busy_diag_active_ms; }

void EPD_BusyDiagReset(void) {
    m_busy_diag_asserted = false;
    m_busy_diag_assert_delay_ms = 0;
    m_busy_diag_active_ms = 0;
}

/* Both BUSY phases use the same remaining-budget clock. Sample before
 * testing the budget, and publish elapsed time before the next callback. */
static bool epd_poll_held(bool level, uint16_t budget, uint32_t* elapsed) {
    for (;;) {
        bool held = EPD_ReadBusy() == level;
        if (!held || !budget) return true;
        if (!(budget & EPD_WAIT_SERVICE_MASK)) {
            app_feed_wdt();
            if (m_maintenance_callback) m_maintenance_callback();
            if (EPD_AbortRequested()) return false;
        }
        delay(1);
        --budget;
        ++*elapsed;
    }
}

void EPD_WaitBusy(bool status, uint16_t timeout) {
#if !defined(INKTAG_COMPACT_BUSY_DIAG)
    uint32_t saved_led = EPD_LED_PIN == 255 ? HIGH : digitalRead(EPD_LED_PIN);
#endif
    (void)epd_poll_held(status, timeout, &m_busy_diag_active_ms);
#if !defined(INKTAG_COMPACT_BUSY_DIAG)
    if (EPD_LED_PIN != 255) {
        if (saved_led == LOW) EPD_LED_ON(); else EPD_LED_OFF();
    }
#endif
}

__attribute__((noinline)) bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout,
                                             uint16_t release_timeout) {
    EPD_BusyDiagReset();
    if (!epd_poll_held(!status, assert_timeout, &m_busy_diag_assert_delay_ms)) {
#if defined(INKTAG_COMPACT_BUSY_DIAG)
        EPD_RECORD_BUSY_FAILURE(3u);
        return false;
#else
        goto done;
#endif
    }
    m_busy_diag_asserted = EPD_ReadBusy() == status;
    EPD_BUSY_TRACE(m_busy_diag_asserted ? "busy:on" : "busy:no-assert");
#if defined(INKTAG_COMPACT_BUSY_DIAG)
    if (!m_busy_diag_asserted) { EPD_RECORD_BUSY_FAILURE(1u); return false; }
    EPD_WaitBusy(status, release_timeout);
    bool released = EPD_ReadBusy() != status;
    bool cancelled = EPD_AbortRequested();
    if (cancelled) EPD_RECORD_BUSY_FAILURE(3u);
    else if (!released) EPD_RECORD_BUSY_FAILURE(2u);
    EPD_BUSY_TRACE(released ? "busy:off" : "busy:held");
    return m_busy_diag_asserted && released && !cancelled;
#else
    if (!m_busy_diag_asserted || EPD_AbortRequested()) goto done;
    EPD_WaitBusy(status, release_timeout);
done:
    if (EPD_AbortRequested()) EPD_RECORD_BUSY_FAILURE(3u);
    else if (!m_busy_diag_asserted) EPD_RECORD_BUSY_FAILURE(1u);
    else if (EPD_ReadBusy() == status) EPD_RECORD_BUSY_FAILURE(2u);
    return !EPD_AbortRequested() && m_busy_diag_asserted && EPD_ReadBusy() != status;
#endif
}

static void epd_led_level(bool high) {
    if (EPD_LED_PIN == 255) return;
    pinMode(EPD_LED_PIN, OUTPUT);
    digitalWrite(EPD_LED_PIN, high);
}

void EPD_LED_ON(void) { if (EPD_LED_ENABLED) epd_led_level(LOW); }
void EPD_LED_OFF(void) { epd_led_level(HIGH); }
void EPD_LED_Toggle(void) {
    if (EPD_LED_ENABLED && EPD_LED_PIN != 255) nrf_gpio_pin_toggle(EPD_LED_PIN);
}
void EPD_LED_BLINK(void) {
    if (!EPD_LED_ENABLED || EPD_LED_PIN == 255) return;
    epd_led_level(LOW);
    delay(100);
    /* A blink holds output ownership until its final default transition. */
    digitalWrite(EPD_LED_PIN, HIGH);
    delay(100);
    pinMode(EPD_LED_PIN, DEFAULT);
}
void EPD_LED_SetEnabled(bool enabled) {
    EPD_LED_ENABLED = enabled;
    if (!enabled) EPD_LED_OFF();
}

uint16_t EPD_ReadVoltage(void) {
#if defined(INKTAG_NRF_SDK17)
#define EPD_SAADC_EVENT_WAIT_LIMIT 1000000u
    volatile int16_t value = 0;
    uint32_t remaining;
    bool started;
    bool sampled;
    bool stopped;

    NRF_SAADC->EVENTS_STARTED = 0u;
    NRF_SAADC->EVENTS_END = 0u;
    NRF_SAADC->EVENTS_STOPPED = 0u;
    NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_10bit;
    NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);
    NRF_SAADC->CH[0].CONFIG =
        ((SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) & SAADC_CH_CONFIG_RESP_Msk) |
        ((SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos) & SAADC_CH_CONFIG_RESN_Msk) |
        ((SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) & SAADC_CH_CONFIG_GAIN_Msk) |
        ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) & SAADC_CH_CONFIG_REFSEL_Msk) |
        ((SAADC_CH_CONFIG_TACQ_3us << SAADC_CH_CONFIG_TACQ_Pos) & SAADC_CH_CONFIG_TACQ_Msk) |
        ((SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) & SAADC_CH_CONFIG_MODE_Msk);
    NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;
    NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_VDD;
    NRF_SAADC->RESULT.PTR = (uint32_t)&value;
    NRF_SAADC->RESULT.MAXCNT = 1;
    NRF_SAADC->TASKS_START = 0x01UL;
    remaining = EPD_SAADC_EVENT_WAIT_LIMIT;
    while (!NRF_SAADC->EVENTS_STARTED && remaining-- != 0u) { }
    started = NRF_SAADC->EVENTS_STARTED != 0u;
    NRF_SAADC->EVENTS_STARTED = 0x00UL;
    if (started) NRF_SAADC->TASKS_SAMPLE = 0x01UL;
    remaining = EPD_SAADC_EVENT_WAIT_LIMIT;
    while (started && !NRF_SAADC->EVENTS_END && remaining-- != 0u) { }
    sampled = started && NRF_SAADC->EVENTS_END != 0u;
    NRF_SAADC->EVENTS_END = 0x00UL;
    NRF_SAADC->TASKS_STOP = 0x01UL;
    remaining = EPD_SAADC_EVENT_WAIT_LIMIT;
    while (!NRF_SAADC->EVENTS_STOPPED && remaining-- != 0u) { }
    stopped = NRF_SAADC->EVENTS_STOPPED != 0u;
    NRF_SAADC->EVENTS_STOPPED = 0x00UL;
    NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);
    if (!sampled || !stopped || value < 0) value = 0;
#undef EPD_SAADC_EVENT_WAIT_LIMIT
#else
    /* Supply/3 against the internal bandgap,10-bit conversion. */
    const uint32_t configuration =
        (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) |
        (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos) |
        (ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
        (ADC_CONFIG_PSEL_Disabled << ADC_CONFIG_PSEL_Pos) |
        (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos);
    NRF_ADC->ENABLE = 1;
    NRF_ADC->CONFIG = configuration;
    NRF_ADC->TASKS_START = 1;
    for (;;) { if (NRF_ADC->EVENTS_END) break; }
    NRF_ADC->EVENTS_END = 0;
    uint16_t value = NRF_ADC->RESULT;
    NRF_ADC->TASKS_STOP = 1;
    NRF_ADC->ENABLE = 0;
#endif
    EPD_DEBUG("ADC value: %d", value);
    return ((uint32_t)value * 225u) / 64u;
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
bool EPD_ReadEffectiveTemperature(int8_t* temperature) {
    (void)temperature;
    return false;
}
