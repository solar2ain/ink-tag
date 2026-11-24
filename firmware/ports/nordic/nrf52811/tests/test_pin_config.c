#include <assert.h>
#include <string.h>
#include "inktag_pin_config.h"

static bool allowed(void *context, inktag_pin_role_t role, uint8_t pin) {
    (void)role;
    ++*(unsigned *)context;
    /* Host fixture: pad 0..31, with debug/storage pad 7 protected. */
    return pin < 32u && pin != 7u;
}

int main(void) {
    uint8_t wire[16], previous_wire[16], unaligned[17];
    inktag_pin_config_t config, out, previous;
    unsigned i, j, calls = 0u;
    memset(&config, 0, sizeof(config));
    for (i = 0; i < INKTAG_PIN_ROLE_COUNT; ++i) config.pins[i] = (uint8_t)(i + 8u);
    config.pins[INKTAG_PIN_TXD] = config.pins[INKTAG_PIN_RXD] = 255u;
    config.active_low = INKTAG_PIN_ACTIVE_LOW_MASK;
    assert(inktag_pin_config_encode(&config, wire, sizeof(wire)) == 16u);
    assert(wire[0] == 1u && wire[1] == 0u && wire[2] == 0xc0u && wire[3] == 0x03u);
    memcpy(unaligned + 1u, wire, 16u);
    assert(inktag_pin_config_decode(unaligned + 1u, 16u, &out));
    assert(!memcmp(out.pins, config.pins, 12u) && out.active_low == config.active_low);
    assert(inktag_pin_config_validate(&out, allowed, &calls) && calls == 10u);
    assert(!inktag_pin_config_validate(&out, NULL, NULL));
    previous = out;
    memcpy(previous_wire, wire, sizeof(wire));
    for (i = 0u; i <= 32u; ++i) {
        if (i == 16u) continue;
        assert(!inktag_pin_config_decode(wire, i, &out));
        assert(!memcmp(out.pins, previous.pins, 12u) && out.active_low == previous.active_low);
    }
    assert(!inktag_pin_config_decode(NULL, 16u, &out));
    assert(!inktag_pin_config_decode(wire, 16u, NULL));
    assert(!inktag_pin_config_encode(&config, NULL, 16u));
    assert(!inktag_pin_config_encode(NULL, wire, 16u));
    assert(!inktag_pin_config_encode(&config, wire, 15u));
    assert(!memcmp(wire, previous_wire, 16u));
    for (i = 0u; i < 256u; ++i) {
        wire[0] = (uint8_t)i;
        if (i != 1u) assert(!inktag_pin_config_decode(wire, 16u, &out));
        wire[0] = 1u; wire[1] = (uint8_t)i;
        if (i != 0u) assert(!inktag_pin_config_decode(wire, 16u, &out));
        wire[1] = 0u;
    }
    for (i = 0u; i < 16u; ++i) {
        config.active_low = (uint16_t)(1u << i);
        assert((inktag_pin_config_encode(&config, wire, 16u) == 16u) ==
               ((INKTAG_PIN_ACTIVE_LOW_MASK & (1u << i)) != 0u));
    }
    config.active_low = 0u;
    for (i = 0u; i < 10u; ++i) for (j = 0u; j < i; ++j) {
        config.pins[i] = config.pins[j];
        assert(!inktag_pin_config_encode(&config, wire, 16u));
        config.pins[i] = (uint8_t)(i + 8u);
    }
    for (i = 0u; i < 6u; ++i) {
        config.pins[i] = 255u;
        assert(!inktag_pin_config_encode(&config, wire, 16u));
        config.pins[i] = (uint8_t)(i + 8u);
    }
    config.pins[6] = 255u; /* Screen with no switchable rail is valid. */
    assert(inktag_pin_config_encode(&config, wire, 16u));
    for (i = 0u; i < 6u; ++i) config.pins[i] = 255u;
    assert(inktag_pin_config_encode(&config, wire, 16u));
    config.pins[6] = 1u;
    assert(!inktag_pin_config_encode(&config, wire, 16u));
    config.pins[6] = 255u;
    config.active_low = 1u << 6u;
    assert(!inktag_pin_config_encode(&config, wire, 16u));
    config.active_low = 0u;
    for (i = 0u; i < 256u; ++i) {
        config.pins[11] = (uint8_t)i;
        assert(inktag_pin_config_validate(&config, allowed, &calls) ==
               (i == 255u)); /* UART positions are reserved, even for valid pads. */
    }
    return 0;
}
