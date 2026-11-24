#include "inktag_da_pins.h"

#include <stddef.h>
#include <string.h>

static bool signal_pad(void *context, inktag_pin_role_t role, uint8_t pin) {
    static const uint8_t maximum[] = {7u, 5u, 9u, 7u};
    (void)context;
    /* Renesas DA14585 rev3.4 pin table/SDK gpio.h. Package bond-out still
     * determines which physical pads exist. Storage and active SWD resources
     * are not free GPIO; BS stays the existing board-controlled signal. */
    return role <= INKTAG_PIN_ENABLE && pin < 0x40u &&
           (pin & 15u) <= maximum[pin >> 4u] &&
           !(pin < 8u && ((0x69u >> pin) & 1u)) &&
           pin != 0x14u && pin != 0x15u && pin != 0x22u;
}

bool inktag_da_pins_valid(const inktag_pin_config_t *pins) {
    /* Common shape/polarity/disable semantics; actual unused peripherals and
     * protected chip resources remain the backend's responsibility. */
    return inktag_pin_config_validate(pins, signal_pad, NULL);
}

static bool __attribute__((noinline)) pin_wire_valid(const uint8_t *wire) {
    unsigned i;
    if (wire == NULL ||
        wire[0] != INKTAG_PIN_CONFIG_VERSION || wire[1] != 0u ||
        (wire[2] & ~0x40u) != 0u || wire[3] != 0u) return false;
    for (i = 11u; i < INKTAG_PIN_CONFIG_SIZE; ++i)
        if (wire[i] != 0xffu) return false;
    for (i = 4u; i < 11u; ++i) {
        unsigned j;
        if (wire[i] == 0xffu) {
            if (i == 10u) {
                if (wire[2] != 0u) return false;
                continue;
            }
            if (wire[4] != 0xffu) return false;
            continue;
        }
        if (wire[4] == 0xffu) return false;
        if (!signal_pad(NULL, (inktag_pin_role_t)(i - 4u), wire[i])) return false;
        for (j = 4u; j < i; ++j)
            if (wire[j] == wire[i]) return false;
    }
    return true;
}

__attribute__((noinline)) bool inktag_da_pins_decode(const uint8_t *wire, size_t length,
                          epd_config_t *config) {
    static const char capability[] = "INKTAG-DA-PIN-COMMON-V1";
    /* Bind the host admission marker to the real consumer. The empty asm
     * keeps its address live through section GC without changing GPIO/wire. */
    __asm__ __volatile__("" : : "r" (capability));
    /* Equivalent specialization of common pin16 + this board's strict
     * subset. Preserve the generic codec for other targets and the oracle
     * tests; avoid decoding/validating the same twelve roles twice in SRAM. */
    if (config == NULL || length != INKTAG_PIN_CONFIG_SIZE ||
        !pin_wire_valid(wire)) return false;
    _Static_assert(offsetof(epd_config_t, bs_pin) == 6u &&
                   offsetof(epd_config_t, en_pin) == 10u,
                   "DA native pin tuple changed");
    memcpy(config, wire + 4u, 6u);
    config->bs_pin = 0x22u;
    config->reserved_screen_legacy = 0xffu;
    config->wakeup_pin = 0xffu;
    config->led_pin = 0xffu;
    config->en_pin = wire[10];
    config->enable_active_low = wire[2] != 0u;
    return true;
}

size_t inktag_da_pins_encode(const epd_config_t *config, uint8_t *wire,
                             size_t capacity) {
    uint8_t encoded[INKTAG_PIN_CONFIG_SIZE];
    if (config == NULL || wire == NULL || capacity < sizeof(encoded) ||
        config->bs_pin != 0x22u || config->reserved_screen_legacy != 0xffu ||
        config->wakeup_pin != 0xffu || config->led_pin != 0xffu ||
        config->enable_active_low > 1u) return 0u;
    memset(encoded, 0xff, sizeof(encoded));
    encoded[0] = INKTAG_PIN_CONFIG_VERSION;
    encoded[1] = encoded[3] = 0u;
    encoded[2] = config->enable_active_low ? 0x40u : 0u;
    memcpy(encoded + 4u, config, 6u);
    encoded[10] = config->en_pin;
    if (!pin_wire_valid(encoded)) return 0u;
    memcpy(wire, encoded, sizeof(encoded));
    return sizeof(encoded);
}
