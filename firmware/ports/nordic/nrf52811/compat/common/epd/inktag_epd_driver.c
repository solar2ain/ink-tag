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
#define EPD_RECORD_BUSY_FAILURE(reason) ((void)0)
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
static uint8_t epd_pads[PAD_COUNT] = {5, 8, 9, 10, 11, 12, 13, 255, 255};
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
__attribute__((noinline, noclone))
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

static bool m_gpio_configured;
bool EPD_GPIO_IsConfigured(void) { return m_gpio_configured; }

/* Display bus ownership. */
static uint16_t m_driver_refs = 0;

void EPD_GPIO_Load(epd_config_t* cfg) {
    if (!cfg) return;
    m_gpio_configured = cfg->mosi_pin != UINT8_MAX;
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


void EPD_GPIO_Init(void) {
    if (!EPD_GPIO_IsConfigured()) return;
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
    if (EPD_LED_PIN != 255) pinMode(EPD_LED_PIN, OUTPUT);
#if defined(INKTAG_NRF_INDICATOR_ENABLED) && INKTAG_NRF_INDICATOR_ENABLED
    if (!EPD_LED_ENABLED) EPD_LED_OFF();
#else
    if (EPD_LED_ENABLED) EPD_LED_ON(); else EPD_LED_OFF();
#endif
}

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
static __attribute__((noinline,noclone)) void epd_select_direction(bool receive) {
    nrf_gpio_pin_dir_t wanted = receive ? NRF_GPIO_PIN_DIR_INPUT : NRF_GPIO_PIN_DIR_OUTPUT;
    if (nrf_gpio_pin_dir_get(EPD_MOSI_PIN) != wanted) {
        pinMode(EPD_MOSI_PIN, receive ? INPUT_PULLUP : OUTPUT);
        nrf_spi_pins_set(HAL_SPI_INSTANCE, EPD_SCLK_PIN,
            receive ? NRF_SPI_PIN_NOT_CONNECTED : EPD_MOSI_PIN,
            receive ? EPD_MOSI_PIN : NRF_SPI_PIN_NOT_CONNECTED);
    }
}

static __attribute__((noinline,noclone)) void epd_transfer(uint8_t* data, uint8_t length, bool receive) {
    epd_select_direction(receive);
#if INKTAG_EPD_SPI_SOFTWARE_CS
    digitalWrite(EPD_CS_PIN, LOW);
#endif
    ret_code_t result = nrf_drv_spi_transfer(&spi,
        receive ? NULL : data, receive ? 0 : length,
        receive ? data : NULL, receive ? length : 0);
#if INKTAG_EPD_SPI_SOFTWARE_CS
    digitalWrite(EPD_CS_PIN, HIGH);
#endif
    APP_ERROR_CHECK(result);
}

void EPD_SPI_Write(uint8_t* value, uint8_t len) { epd_transfer(value, len, false); }
void EPD_SPI_Read(uint8_t* value, uint8_t len) { epd_transfer(value, len, true); }


/* Controller bus phases. */
void EPD_WriteCmd(uint8_t cmd) {
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


bool EPD_ReadCommand(uint8_t cmd, uint8_t* value, uint8_t len) {
#if INKTAG_EPD_SPI_SOFTWARE_CS
    if (!m_driver_refs || !value || !len) return false;
    epd_select_direction(false);
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    ret_code_t status = nrf_drv_spi_transfer(&spi, &cmd, 1, NULL, 0);
    if (status == NRF_SUCCESS) {
        digitalWrite(EPD_DC_PIN, HIGH);
        epd_select_direction(true);
        status = nrf_drv_spi_transfer(&spi, NULL, 0, value, len);
    }
    digitalWrite(EPD_CS_PIN, HIGH);
    epd_select_direction(false);
    return status == NRF_SUCCESS;
#else
    (void)cmd; (void)value; (void)len;
    return false;
#endif
}

#if INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q
bool EPD_ReadCommandWindow(uint8_t cmd, uint16_t skip, uint8_t *value, uint8_t len) {
    (void)cmd; (void)skip; (void)value; (void)len;
    return false;
}
#endif

void EPD_WriteByte(uint8_t value) { EPD_WriteData(&value, 1); }

uint8_t EPD_ReadByte(void) {
    uint8_t value;
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
    return digitalRead(EPD_BUSY_PIN);
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
