#include "inktag_aux_io.h"

bool inktag_aux_io_validate(const inktag_aux_io_t *config) {
    uint8_t pins[INKTAG_AUX_BUTTON_COUNT];
    unsigned i, j;
    if (config == NULL) return false;
    for (i = 0u; i < INKTAG_AUX_BUTTON_COUNT; ++i) {
        uint8_t flags = config->buttons[i].flags;
        pins[i] = config->buttons[i].gpio;
        if ((flags & ~7u) != 0u || (flags >> 1u) > 2u ||
            (pins[i] == INKTAG_AUX_PIN_UNUSED && flags != 0u)) return false;
    }
    for (i = 0u; i < sizeof(pins); ++i)
        if (pins[i] != INKTAG_AUX_PIN_UNUSED)
            for (j = 0u; j < i; ++j)
                if (pins[i] == pins[j]) return false;
    return true;
}

bool inktag_aux_io_decode(const uint8_t *wire, size_t length, inktag_aux_io_t *out) {
    inktag_aux_io_t next;
    unsigned i;
    if (wire == NULL || out == NULL || length != INKTAG_AUX_IO_WIRE_SIZE ||
        wire[0] != INKTAG_AUX_IO_VERSION) return false;
    for (i = 0u; i < INKTAG_AUX_BUTTON_COUNT; ++i) {
        next.buttons[i].gpio = wire[1u + i * 2u];
        next.buttons[i].flags = wire[2u + i * 2u];
    }
    for (i = 0u; i < 3u; ++i) next.reserved[i] = wire[7u + i];
    next.reserved_word = (uint16_t)wire[10] | ((uint16_t)wire[11] << 8u);
    if (!inktag_aux_io_validate(&next)) return false;
    *out = next;
    return true;
}

size_t inktag_aux_io_encode(const inktag_aux_io_t *config, uint8_t *wire, size_t capacity) {
    unsigned i;
    if (wire == NULL || capacity < INKTAG_AUX_IO_WIRE_SIZE || !inktag_aux_io_validate(config)) return 0u;
    wire[0] = INKTAG_AUX_IO_VERSION;
    for (i = 0u; i < INKTAG_AUX_BUTTON_COUNT; ++i) {
        wire[1u + i * 2u] = config->buttons[i].gpio;
        wire[2u + i * 2u] = config->buttons[i].flags;
    }
    for (i = 0u; i < 3u; ++i) wire[7u + i] = config->reserved[i];
    wire[10] = (uint8_t)config->reserved_word;
    wire[11] = (uint8_t)(config->reserved_word >> 8u);
    return INKTAG_AUX_IO_WIRE_SIZE;
}
