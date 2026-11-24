#ifndef INKTAG_TLSR_PIN_BACKEND_H
#define INKTAG_TLSR_PIN_BACKEND_H
#include "inktag_pin_config.h"

/* B85 GPIO encoding, independent of the port's SDK enum representation.
 * Ports E/MSPI are deliberately outside this public 0..31 namespace.
 * Exact-board ownership must be supplied; an empty MCU-wide mask is not
 * electrical permission to take over NFC, sampling or wakeup nets. */
typedef struct {
    uint32_t usable;
    uint32_t protected_pins;
} inktag_tlsr_pin_backend_t;

static inline uint16_t inktag_tlsr_pin_gpio(uint8_t pin) {
    return pin < 32u ? (uint16_t)(((uint16_t)(pin >> 3u) << 8u) |
                                  (uint16_t)(1u << (pin & 7u))) : 0u;
}
static inline bool inktag_tlsr_pin_allowed(void *context,
                                          inktag_pin_role_t role, uint8_t pin) {
    const inktag_tlsr_pin_backend_t *backend = context;
    if (!backend || pin >= 32u || role >= INKTAG_PIN_TXD ||
        !(backend->usable & (UINT32_C(1) << pin)) ||
        (backend->protected_pins & (UINT32_C(1) << pin))) return false;
    /* PA7 may keep the existing blue/SWS sharing contract, never a persistent
     * screen/rail/red/green output. UART has no qualified backend yet. */
    return pin != 7u || role == INKTAG_PIN_LED_BLUE;
}
static inline bool inktag_tlsr_pin_bus_is_hardware_spi(
    const inktag_pin_config_t *pins) {
    return pins && pins->pins[INKTAG_PIN_MOSI] == 14u &&
        pins->pins[INKTAG_PIN_CLOCK] == 31u && pins->pins[INKTAG_PIN_CS] == 26u &&
        pins->pins[INKTAG_PIN_DC] == 15u;
}
/* Polarity admission is shared by App and the LED-only maintenance reader.
 * Full candidates still include the screen roles, so neither consumer may
 * silently accept a configuration which its sibling cannot apply. */
static inline bool inktag_tlsr_pin_backend_validate(
    const inktag_pin_config_t *pins, inktag_tlsr_pin_backend_t *backend) {
    /* CS/RESET waveforms are fixed by the qualified adapter/recipe. EN is a
     * real board switch polarity, consumed by panel drive and safe release. */
    return inktag_pin_config_validate(pins,inktag_tlsr_pin_allowed,backend) &&
        (pins->pins[INKTAG_PIN_LED_BLUE]!=7u ||
         (pins->active_low&(1u<<INKTAG_PIN_LED_BLUE)));
}
#endif
