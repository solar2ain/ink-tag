/* DA projection of c25d3a79d common transaction/wait helpers.
 * GPIO/SPI entry points are supplied by the maintained DA materializer prefix;
 * Nordic adapter and ADC source are not part of this independent Free port.
 */
#include "inktag_epd_driver.h"
#include "inktag_da_health.h"
#include <string.h>

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

bool EPD_ReadBusy(void) { return digitalRead(EPD_BUSY_PIN); }

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
            if (EPD_AbortRequested()) return false;
#else
        goto done;
#endif
    }
    m_busy_diag_asserted = EPD_ReadBusy() == status;
    EPD_BUSY_TRACE(m_busy_diag_asserted ? "busy:on" : "busy:no-assert");
#if defined(INKTAG_COMPACT_BUSY_DIAG)
    if (!m_busy_diag_asserted) return false;
    EPD_WaitBusy(status, release_timeout);
    bool released = EPD_ReadBusy() != status;
    EPD_BUSY_TRACE(released ? "busy:off" : "busy:held");
    return m_busy_diag_asserted && released;
#else
    if (!m_busy_diag_asserted || EPD_AbortRequested()) goto done;
    EPD_WaitBusy(status, release_timeout);
done:
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
    if (EPD_LED_ENABLED && EPD_LED_PIN != 255) digitalWrite(EPD_LED_PIN, digitalRead(EPD_LED_PIN) ? LOW : HIGH);
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
    return inktag_da_read_battery_mv();
}
