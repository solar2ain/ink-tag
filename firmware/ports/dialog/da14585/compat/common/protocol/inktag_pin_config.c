#include "inktag_pin_config.h"

#include <string.h>

static bool shape_valid(const inktag_pin_config_t *config) {
    unsigned i, j;
    unsigned display = 0u;
    if (config == NULL ||
        (config->active_low & ~INKTAG_PIN_ACTIVE_LOW_MASK) != 0u ||
        config->pins[INKTAG_PIN_TXD] != INKTAG_PIN_UNUSED ||
        config->pins[INKTAG_PIN_RXD] != INKTAG_PIN_UNUSED) return false;
    for (i = 0u; i < INKTAG_PIN_ROLE_COUNT; ++i) {
        if (config->pins[i] == INKTAG_PIN_UNUSED) {
            if ((config->active_low & (1u << i)) != 0u) return false;
            continue;
        }
        if (i < INKTAG_PIN_ENABLE) ++display;
        for (j = 0u; j < i; ++j)
            if (config->pins[j] == config->pins[i]) return false;
    }
    /* Either no display, or all six mandatory signals. Rail enable is optional.
     * A disabled display must not leave a rail configured to drive a pin. */
    return display == 6u ||
           (display == 0u && config->pins[INKTAG_PIN_ENABLE] == INKTAG_PIN_UNUSED);
}

bool inktag_pin_config_decode(const uint8_t *data, size_t length,
                              inktag_pin_config_t *out) {
    inktag_pin_config_t candidate;
    if (data == NULL || out == NULL || length != INKTAG_PIN_CONFIG_SIZE ||
        data[0] != INKTAG_PIN_CONFIG_VERSION || data[1] != 0u) return false;
    candidate.active_low = (uint16_t)data[2] | ((uint16_t)data[3] << 8u);
    memcpy(candidate.pins, data + 4u, INKTAG_PIN_ROLE_COUNT);
    if (!shape_valid(&candidate)) return false;
    *out = candidate;
    return true;
}

size_t inktag_pin_config_encode(const inktag_pin_config_t *config,
                                uint8_t *data, size_t capacity) {
    if (data == NULL || capacity < INKTAG_PIN_CONFIG_SIZE ||
        !shape_valid(config)) return 0u;
    data[0] = INKTAG_PIN_CONFIG_VERSION;
    data[1] = 0u;
    data[2] = (uint8_t)config->active_low;
    data[3] = (uint8_t)(config->active_low >> 8u);
    memcpy(data + 4u, config->pins, INKTAG_PIN_ROLE_COUNT);
    return INKTAG_PIN_CONFIG_SIZE;
}

bool inktag_pin_config_validate(const inktag_pin_config_t *config,
                                inktag_pin_allowed_fn allowed, void *context) {
    unsigned i;
    /* No backend, no usable pin authority -- even a well-formed block refuses. */
    if (allowed == NULL || !shape_valid(config)) return false;
    for (i = 0u; i < INKTAG_PIN_ROLE_COUNT; ++i)
        if (config->pins[i] != INKTAG_PIN_UNUSED &&
            !allowed(context, (inktag_pin_role_t)i, config->pins[i])) return false;
    return true;
}
