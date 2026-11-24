#include "inktag_epd_driver.h"

#include <string.h>
#include "om.h"
#include "om_driver.h"
#include "evt.h"
#include "evt_timer.h"
#include "inktag_elapsed_clock.h"
#include "inktag_epd_wait_policy.h"
#include "inktag_board.h"
#include "inktag_om_app.h"
#include "inktag_om_board_identity.h"
#include "inktag_om_panel_idle.h"
#include "inktag_om_voltage.h"
#include "inktag_om_pins.h"

#define BUFFER_SIZE 128
#define SPI_TIMEOUT_MS 1000
#define BUSY_ASSERT_TIMEOUT_MS 500
#define DELAY_MAINTENANCE_CHUNK_MS 32u

_Static_assert(DELAY_MAINTENANCE_CHUNK_MS <=
                   INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS,
               "OM panel delays must service BLE within the common bound");
_Static_assert(DELAY_MAINTENANCE_CHUNK_MS <=
                   INKTAG_EPD_WAIT_CANCEL_MAX_MS,
               "OM panel delays must observe cancellation within the common bound");
_Static_assert(INKTAG_OM_EVENT_SERVICE_TIMER_MS <=
                   INKTAG_EPD_WAIT_MAINTENANCE_MAX_MS,
               "OM BUSY wait must service BLE within the common bound");
_Static_assert(INKTAG_OM_EVENT_SERVICE_TIMER_MS <=
                   INKTAG_EPD_WAIT_CANCEL_MAX_MS,
               "OM BUSY wait must observe cancellation within the common bound");

extern void app_feed_wdt(void);

static uint32_t EPD_MOSI_PIN = 17;
static uint32_t EPD_SCLK_PIN = 19;
static uint32_t EPD_CS_PIN = 18;
static uint32_t EPD_DC_PIN = 14;
static uint32_t EPD_RST_PIN = 15;
static uint32_t EPD_BUSY_PIN = 16;
static uint32_t EPD_BS_PIN = 20;
static uint32_t EPD_EN_PIN = 0xFF;
static uint32_t EPD_LED_PIN = 0xFF;
static epd_abort_callback_t m_abort_callback = NULL;
static uint16_t m_driver_refs = 0;
static bool m_spi_ready = false;
static int32_t m_spi_last_error = OM_ERROR_OK;
static bool m_busy_diag_asserted = false;
static uint32_t m_busy_diag_assert_delay_ms = 0;
static uint32_t m_busy_diag_active_ms = 0;
static evt_timer_t m_busy_wake_timer;
static bool m_busy_wake_timer_armed;

static void busy_wake_timer_handler(evt_timer_t *timer, void *context) {
    (void)timer;
    (void)context;
}

static void busy_wake_timer_start(void) {
    if (m_busy_wake_timer_armed) return;
    m_busy_wake_timer_armed = evt_timer_set(
        &m_busy_wake_timer, INKTAG_OM_EVENT_SERVICE_TIMER_MS,
        EVT_TIMER_REPEAT, busy_wake_timer_handler, NULL);
}

static void busy_wake_timer_stop(void) {
    if (!m_busy_wake_timer_armed) return;
    evt_timer_del(&m_busy_wake_timer);
    m_busy_wake_timer_armed = false;
}

/* The recovered ZKC21B/ZKC42B panel bus is routed to the OM6626 SPI1 pin
 * group (SCLK=15, DO=21, CS=22).  Using SPI0 here configures mux functions
 * which those pads do not expose, so transfers appear to complete while no
 * command reaches the panel.  Keep the instance board-owned for future
 * OM6626 PCBs with different routing. */
#if INKTAG_BOARD_EPD_SPI_INDEX == 1
#define EPD_SPI_INSTANCE OM_SPI1
#define EPD_SPI_CS_MUX PINMUX_SPI1_CS_CFG
#define EPD_SPI_SCLK_MUX PINMUX_SPI1_SCK_CFG
#define EPD_SPI_MOSI_MUX PINMUX_SPI1_DO_CFG
#elif INKTAG_BOARD_EPD_SPI_INDEX == 0
#define EPD_SPI_INSTANCE OM_SPI0
#define EPD_SPI_CS_MUX PINMUX_SPI0_CS_CFG
#define EPD_SPI_SCLK_MUX PINMUX_SPI0_SCK_CFG
#define EPD_SPI_MOSI_MUX PINMUX_SPI0_DO_CFG
#else
#error "Unsupported INKTAG_BOARD_EPD_SPI_INDEX"
#endif

static uint32_t pin_mask(uint32_t pin) { return (pin >= 32) ? 0 : (1u << pin); }
static bool pin_valid(uint32_t pin) { return pin != 0xFF && pin < 26; }

void delay(uint32_t ms) {
    /* OM's BLE host is cooperatively scheduled. Controller recipes include
     * reset/settle delays of several hundred milliseconds; one uninterrupted
     * DWT delay can make the radio stop sending while the vendor GAP state
     * continues to report STARTED. Service the SDK well inside the common
     * 100 ms maintenance/cancellation bound during every long panel delay. */
    while (ms > 0u) {
        uint32_t chunk = ms > DELAY_MAINTENANCE_CHUNK_MS ?
            DELAY_MAINTENANCE_CHUNK_MS : ms;
        drv_dwt_delay_ms(chunk);
        ms -= chunk;
        app_feed_wdt();
        evt_schedule();
        inktag_om_app_epd_maintenance();
    }
}

void pinMode(uint32_t pin, uint32_t mode) {
    if (!pin_valid(pin)) return;

    pin_config_t pin_cfg = { pin, { PINMUX_GPIO_MODE_CFG }, PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL };
    gpio_config_t gpio_cfg = { OM_GPIO0, (uint8_t)pin, GPIO_DIR_OUTPUT, GPIO_LEVEL_LOW, GPIO_TRIG_NONE };

    switch (mode) {
        case INPUT:
            pin_cfg.mode = PMU_PIN_MODE_FLOAT;
            gpio_cfg.dir = GPIO_DIR_INPUT;
            break;
        case INPUT_PULLUP:
            pin_cfg.mode = PMU_PIN_MODE_PU;
            gpio_cfg.dir = GPIO_DIR_INPUT;
            break;
        case INPUT_PULLDOWN:
            pin_cfg.mode = PMU_PIN_MODE_PD;
            gpio_cfg.dir = GPIO_DIR_INPUT;
            break;
        case OUTPUT:
            pin_cfg.mode = PMU_PIN_MODE_PP;
            gpio_cfg.dir = GPIO_DIR_OUTPUT;
            break;
        case DEFAULT:
        default:
            pin_cfg.mode = PMU_PIN_MODE_FLOAT;
            gpio_cfg.dir = GPIO_DIR_INPUT;
            break;
    }

    drv_pin_init(&pin_cfg, 1);
    drv_gpio_init(&gpio_cfg, 1);
}

void digitalWrite(uint32_t pin, uint32_t value) {
    if (!pin_valid(pin)) return;
    drv_gpio_write(OM_GPIO0, pin_mask(pin), value ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW);
}

uint32_t digitalRead(uint32_t pin) {
    if (!pin_valid(pin)) return 0;
    return drv_gpio_read(OM_GPIO0, pin_mask(pin)) ? HIGH : LOW;
}

static void spi_pin_init(void) {
    pin_config_t pins[] = {
        { (uint8_t)EPD_CS_PIN,   { EPD_SPI_CS_MUX },   PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL },
        { (uint8_t)EPD_SCLK_PIN, { EPD_SPI_SCLK_MUX }, PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL },
        { (uint8_t)EPD_MOSI_PIN, { EPD_SPI_MOSI_MUX }, PMU_PIN_MODE_PU, PMU_PIN_DRIVER_CURRENT_NORMAL },
    };
    drv_pin_init(pins, sizeof(pins) / sizeof(pins[0]));
}

void EPD_GPIO_Load(epd_config_t* cfg) {
    if (cfg == NULL) return;
    EPD_MOSI_PIN = cfg->mosi_pin;
    EPD_SCLK_PIN = cfg->sclk_pin;
    EPD_CS_PIN = cfg->cs_pin;
    EPD_DC_PIN = cfg->dc_pin;
    EPD_RST_PIN = cfg->rst_pin;
    EPD_BUSY_PIN = cfg->busy_pin;
    EPD_BS_PIN = cfg->bs_pin;
    EPD_EN_PIN = cfg->en_pin;
#if defined(INKTAG_BOARD_MANAGED_STATUS_LED)
    /* Managed RGB scenes own the board LEDs. A persisted legacy LED pin must
     * never bypass that policy from the panel BUSY waiter. */
    EPD_LED_PIN = 0xFF;
#else
    EPD_LED_PIN = cfg->led_pin;
#endif
}

void EPD_GPIO_Init(void) {
    if (!inktag_om_app_epd_control_allowed()) return;
    if (m_driver_refs++ > 0) return;

    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_BUSY_PIN, INPUT);

    spi_config_t spi_cfg;
    memset(&spi_cfg, 0, sizeof(spi_cfg));
#if defined(INKTAG_BOARD_EPD_SPI_HZ)
    spi_cfg.freq = INKTAG_BOARD_EPD_SPI_HZ;
#else
    spi_cfg.freq = 1000 * 1000;
#endif
    spi_cfg.role = SPI_ROLE_MASTER;
    spi_cfg.mode = SPI_MODE_0;
    spi_cfg.wire = SPI_WIRE_3;
    spi_cfg.first_bit = SPI_MSB_FIRST;
    spi_cfg.cs_valid = SPI_CS_LOW;

    spi_pin_init();
    m_spi_last_error = drv_spi_init(EPD_SPI_INSTANCE, &spi_cfg);
    if (m_spi_last_error == OM_ERROR_OK) {
        m_spi_ready = true;
    }

    if (EPD_BS_PIN != 0xFF) { pinMode(EPD_BS_PIN, OUTPUT); digitalWrite(EPD_BS_PIN, LOW); }
    if (EPD_EN_PIN != 0xFF) {
        /* Preload inactive before output, then enable the qualified rail. */
        bool low=inktag_om_pins_active_low(INKTAG_PIN_ENABLE);
        digitalWrite(EPD_EN_PIN,low?HIGH:LOW);
        pin_config_t pin={EPD_EN_PIN,{PINMUX_GPIO_MODE_CFG},PMU_PIN_MODE_PP,PMU_PIN_DRIVER_CURRENT_NORMAL};
        gpio_config_t gpio={OM_GPIO0,(uint8_t)EPD_EN_PIN,GPIO_DIR_OUTPUT,
                           low?GPIO_LEVEL_HIGH:GPIO_LEVEL_LOW,GPIO_TRIG_NONE};
        drv_pin_init(&pin,1u);drv_gpio_init(&gpio,1u);
        digitalWrite(EPD_EN_PIN,low?LOW:HIGH);
    }
    if (EPD_LED_PIN != 0xFF) pinMode(EPD_LED_PIN, OUTPUT);
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_RST_PIN, HIGH);
    EPD_LED_ON();
}

void EPD_GPIO_Uninit(void) {
    if (m_driver_refs == 0 || --m_driver_refs > 0) return;
    EPD_LED_OFF();
    /* drv_spi_init enables the peripheral clock, but the SDK has no SPI
     * deinit routine.  The panel bus is idle here; gate SPI1 just as the
     * external-Flash adapter gates SPI0 after each transaction.  A later
     * EPD_GPIO_Init resets/re-enables SPI1 through drv_spi_init. */
    (void)drv_spi_control(EPD_SPI_INSTANCE, SPI_CONTROL_CLK_DISABLE, NULL);
    m_spi_ready = false;
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    digitalWrite(EPD_RST_PIN, LOW);
    if (EPD_EN_PIN != 0xFF) digitalWrite(EPD_EN_PIN,
        inktag_om_pins_active_low(INKTAG_PIN_ENABLE)?HIGH:LOW);
    pinMode(EPD_MOSI_PIN, DEFAULT);
    pinMode(EPD_SCLK_PIN, DEFAULT);
    pinMode(EPD_CS_PIN, DEFAULT);
    pinMode(EPD_DC_PIN, DEFAULT);
    pinMode(EPD_RST_PIN, DEFAULT);
    pinMode(EPD_BUSY_PIN, DEFAULT);
    pinMode(EPD_BS_PIN, DEFAULT);
    if (EPD_EN_PIN != UINT8_MAX &&
        EPD_EN_PIN == inktag_om_panel_idle_en_pin()) {
        /* Retain the exact-board EN-off level without a GPIO-init rail pulse. */
        pin_config_t pin={EPD_EN_PIN,{PINMUX_GPIO_MODE_CFG},PMU_PIN_MODE_PP,PMU_PIN_DRIVER_CURRENT_NORMAL};
        gpio_config_t gpio={OM_GPIO0,(uint8_t)EPD_EN_PIN,GPIO_DIR_OUTPUT,
            inktag_om_pins_active_low(INKTAG_PIN_ENABLE)?GPIO_LEVEL_HIGH:GPIO_LEVEL_LOW,GPIO_TRIG_NONE};
        drv_pin_init(&pin,1u);drv_gpio_init(&gpio,1u);
    } else {
        pinMode(EPD_EN_PIN, DEFAULT);
    }
    pinMode(EPD_LED_PIN, DEFAULT);
}

void EPD_SPI_Write(uint8_t* value, uint8_t len) {
    if (!m_spi_ready || value == NULL || len == 0) return;
    int32_t result = drv_spi_transfer(EPD_SPI_INSTANCE, value, len, NULL, 0, SPI_TIMEOUT_MS);
    /* Keep the first error until the existing GPIO/SPI initialization starts
     * a new bus lifetime. Still perform later best-effort cleanup transfers. */
    if (m_spi_last_error == OM_ERROR_OK) m_spi_last_error = result;
}

void EPD_SPI_Read(uint8_t* value, uint8_t len) {
    if (!m_spi_ready || value == NULL || len == 0) return;
    int32_t result = drv_spi_transfer(EPD_SPI_INSTANCE, NULL, 0, value, len, SPI_TIMEOUT_MS);
    if (m_spi_last_error == OM_ERROR_OK) m_spi_last_error = result;
}

void EPD_WriteCmd(uint8_t cmd) { digitalWrite(EPD_DC_PIN, LOW); EPD_SPI_Write(&cmd, 1); }
void EPD_WriteData(uint8_t* value, uint8_t len) { digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Write(value, len); }
void EPD_ReadData(uint8_t* value, uint8_t len) { digitalWrite(EPD_DC_PIN, HIGH); EPD_SPI_Read(value, len); }
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
/* Sensor conversion has its own bounded level wait. Do not overwrite the
 * display BUSY-edge diagnostics or convert an optional sensor miss into a
 * display failure. delay() retains OM's existing WDT/radio/cancel service. */
static bool epd_sensor_level(bool idle, uint16_t remaining) {
    for (;;) {
        if (EPD_AbortRequested()) return false;
        if (EPD_ReadBusy() == idle) return true;
        if (!remaining--) return false;
        delay(1u);
    }
}

static bool epd_sensor_ready(void) {
    drv_dwt_delay_us(4u);
    return epd_sensor_level(false, 100u) &&
           epd_sensor_level(true, 2000u);
}
#endif

static bool epd_read_window(uint8_t cmd,uint16_t skip,uint8_t *value,uint8_t len,bool closing_byte) {
    if (!m_spi_ready || value == NULL || len == 0u ||
        !pin_valid(EPD_CS_PIN) || !pin_valid(EPD_SCLK_PIN) ||
        !pin_valid(EPD_MOSI_PIN) || !pin_valid(EPD_DC_PIN)) return false;
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    bool e6_read = !skip && !closing_byte &&
        ((cmd == 0x70u && len == 7u) || (cmd == 0x40u && len == 1u));
    bool conversion = e6_read && cmd == 0x40u;
    if (e6_read && EPD_AbortRequested()) return false;
#else
    bool e6_read = false;
#endif

    /* The fitted panels share their write and read data on the SPI1 DO pad.
     * The SDK's split transfers release CS between command and response,
     * whereas TSC reads require one CS-low interval with D/C changing phase.
     * This bounded GPIO transaction runs only while the panel bus is open;
     * restore the qualified hardware-SPI pinmux before the next RAM write. */
    const uint32_t pins[] = {EPD_CS_PIN, EPD_SCLK_PIN, EPD_MOSI_PIN};
    const gpio_level_t levels[] = {GPIO_LEVEL_HIGH, GPIO_LEVEL_LOW,
                                   GPIO_LEVEL_LOW};
    for (uint8_t i = 0u; i < 3u; ++i) {
        pin_config_t pin_cfg = {
            (uint8_t)pins[i], {PINMUX_GPIO_MODE_CFG}, PMU_PIN_MODE_PP,
            PMU_PIN_DRIVER_CURRENT_NORMAL
        };
        gpio_config_t gpio_cfg = {
            OM_GPIO0, (uint8_t)pins[i], GPIO_DIR_OUTPUT, levels[i],
            GPIO_TRIG_NONE
        };
        drv_pin_init(&pin_cfg, 1);
        drv_gpio_init(&gpio_cfg, 1);
    }
    digitalWrite(EPD_DC_PIN, LOW);
    digitalWrite(EPD_CS_PIN, LOW);
    for (uint8_t bit = 0u; bit < 8u; ++bit) {
        digitalWrite(EPD_SCLK_PIN, LOW);
        digitalWrite(EPD_MOSI_PIN, (cmd & 0x80u) != 0u);
        drv_dwt_delay_us(2u);
        digitalWrite(EPD_SCLK_PIN, HIGH);
        drv_dwt_delay_us(2u);
        cmd <<= 1u;
    }
    if (!e6_read) digitalWrite(EPD_SCLK_PIN, LOW);
    digitalWrite(EPD_DC_PIN, HIGH);
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    pinMode(EPD_MOSI_PIN, e6_read ? INPUT_PULLUP : INPUT);
    bool ok = !e6_read || (conversion ? epd_sensor_ready() : true);
    if (e6_read && !conversion) drv_dwt_delay_us(4u);
#else
    pinMode(EPD_MOSI_PIN, INPUT);
    bool ok=true;
#endif
    uint16_t total=(uint16_t)(skip+len+(closing_byte?1u:0u));
    for (uint16_t i = 0u; ok && i < total; ++i) {
        uint8_t byte = 0u;
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            byte <<= 1u;
            if (e6_read) digitalWrite(EPD_SCLK_PIN, LOW);
            drv_dwt_delay_us(2u);
            if (e6_read) {
                digitalWrite(EPD_SCLK_PIN, HIGH);
                drv_dwt_delay_us(2u);
            }
            if (digitalRead(EPD_MOSI_PIN)) byte |= 1u;
            if (!e6_read) {
                digitalWrite(EPD_SCLK_PIN, HIGH);
                drv_dwt_delay_us(2u);
                digitalWrite(EPD_SCLK_PIN, LOW);
            }
        }
        if(i>=skip && i<(uint16_t)(skip+len))value[i-skip]=byte;
        if((i&63u)==63u) {
            app_feed_wdt();evt_schedule();inktag_om_app_epd_maintenance();
            if(EPD_AbortRequested()){ok=false;break;}
        }
    }
    digitalWrite(EPD_SCLK_PIN, LOW);
    digitalWrite(EPD_CS_PIN, HIGH);
    spi_pin_init();
    return ok;
}
bool EPD_ReadCommand(uint8_t cmd,uint8_t *value,uint8_t len) {
#if INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ
    if (cmd == 0x70u && len == 7u)
        return epd_read_window(cmd,0u,value,len,false);
#endif
    if(len>4u)return false;
    bool ok = epd_read_window(cmd,0u,value,len,false);
    if (cmd == 0x40u && len == 2u)
        inktag_om_app_epd_temperature_read(cmd, value, len, ok);
    return ok;
}
bool EPD_ReadCommandWindow(uint8_t cmd,uint16_t skip,uint8_t *value,uint8_t len) {
    /* Fixed read-only trim windows only, not arbitrary OTP/register access.
     * Reuse OM's MOSI turnaround and restore SPI1 on success/cancellation. */
    if(cmd!=0x92u || (skip!=3945u && skip!=5993u) || len!=7u || EPD_AbortRequested())return false;
    return epd_read_window(cmd,skip,value,len,true);
}
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
        /* OM's BLE host is cooperative. A full-RAM fill must not postpone
         * ATT/GAP processing until all panel bytes have been transferred. */
        app_feed_wdt();
        evt_schedule();
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

bool EPD_ReadBusy(void) { return inktag_om_pins_busy_level(digitalRead(EPD_BUSY_PIN)!=0u); }

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
uint8_t EPD_OnMicroSpiIndex(void) { return INKTAG_BOARD_EPD_SPI_INDEX; }
bool EPD_OnMicroSpiReady(void) { return m_spi_ready; }
int32_t EPD_OnMicroSpiLastError(void) { return m_spi_last_error; }

bool EPD_WaitBusyEdge(bool status, uint16_t assert_timeout, uint16_t release_timeout) {
    EPD_BusyDiagReset();
    uint32_t led_status = digitalRead(EPD_LED_PIN);
    /* The visible-status timer is intentionally dynamic. Keep a separate
     * operation-only wake source so an OFF/steady LED policy cannot turn a
     * panel BUSY wait into an unbounded sleep or cancellation delay. */
    busy_wake_timer_start();

    inktag_elapsed_clock_t wait_clock;
    inktag_elapsed_clock_reset(&wait_clock, drv_pmu_timer_cnt_get(),
                               INKTAG_BOARD_TIMEBASE_COUNTER_MASK,
                               INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND);

    while (EPD_ReadBusy() != status && assert_timeout > 0) {
        if (EPD_AbortRequested()) break;
        app_feed_wdt();
        evt_schedule();
        inktag_om_app_epd_maintenance();
        /* The operation-only 50 ms timer bounds this event-aware wait. Keep
         * every peripheral alive while a panel operation is in flight:
         * the SDK's deeper PM states stop the RF domain long enough for
         * connectable advertising to disappear during a slow refresh. */
        OM_CRITICAL_BEGIN();
        if (evt_get_all() == 0u && !EPD_AbortRequested())
            pm_sleep(PM_STATUS_IDLE);
        OM_CRITICAL_END();
        uint32_t elapsed = inktag_elapsed_clock_sample_time_shared(
            &wait_clock, drv_pmu_timer_cnt_get()).milliseconds;
        if (elapsed > assert_timeout) elapsed = assert_timeout;
        m_busy_diag_assert_delay_ms += elapsed;
        assert_timeout = (uint16_t)(assert_timeout - elapsed);
    }
    m_busy_diag_asserted = EPD_ReadBusy() == status;

    inktag_elapsed_clock_reset(&wait_clock, drv_pmu_timer_cnt_get(),
                               INKTAG_BOARD_TIMEBASE_COUNTER_MASK,
                               INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND);

    while (EPD_ReadBusy() == status && release_timeout > 0) {
        if (EPD_AbortRequested()) break;
        app_feed_wdt();
        evt_schedule();
        inktag_om_app_epd_maintenance();
        EPD_LED_Toggle();
        OM_CRITICAL_BEGIN();
        if (evt_get_all() == 0u && !EPD_AbortRequested())
            pm_sleep(PM_STATUS_IDLE);
        OM_CRITICAL_END();
        uint32_t elapsed = inktag_elapsed_clock_sample_time_shared(
            &wait_clock, drv_pmu_timer_cnt_get()).milliseconds;
        if (elapsed > release_timeout) elapsed = release_timeout;
        m_busy_diag_active_ms += elapsed;
        release_timeout = (uint16_t)(release_timeout - elapsed);
    }
    if (led_status == LOW) EPD_LED_ON(); else EPD_LED_OFF();
    busy_wake_timer_stop();
    return !EPD_AbortRequested() && m_busy_diag_asserted &&
           EPD_ReadBusy() != status;
}

void EPD_WaitBusy(bool status, uint16_t timeout) {
    uint16_t assert_timeout = timeout < BUSY_ASSERT_TIMEOUT_MS ? timeout : BUSY_ASSERT_TIMEOUT_MS;
    (void)EPD_WaitBusyEdge(status, assert_timeout, timeout);
}

void EPD_LED_ON(void) { if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, LOW); }
void EPD_LED_OFF(void) { if (EPD_LED_PIN != 0xFF) digitalWrite(EPD_LED_PIN, HIGH); }
void EPD_LED_Toggle(void) { if (EPD_LED_PIN != 0xFF) drv_gpio_toggle(OM_GPIO0, pin_mask(EPD_LED_PIN)); }
void EPD_LED_BLINK(void) { if (EPD_LED_PIN != 0xFF) { pinMode(EPD_LED_PIN, OUTPUT); EPD_LED_ON(); delay(100); EPD_LED_OFF(); delay(100); pinMode(EPD_LED_PIN, DEFAULT); } }
uint16_t EPD_ReadVoltage(void) {
    return inktag_om_voltage_sample_mv();
}
