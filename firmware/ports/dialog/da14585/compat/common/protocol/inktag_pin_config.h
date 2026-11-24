#ifndef INKTAG_PIN_CONFIG_H
#define INKTAG_PIN_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "inktag_release_v1.h"

/* Common block used by capability-negotiated release-v1 IO. Not a Factory envelope,
 * GPIO authority or permission to enable an unqualified vendor backend. */
#define INKTAG_PIN_CONFIG_VERSION 1u
#define INKTAG_PIN_CONFIG_SIZE 16u
#define INKTAG_PIN_UNUSED 255u

typedef enum {
    INKTAG_PIN_MOSI, INKTAG_PIN_CLOCK, INKTAG_PIN_CS, INKTAG_PIN_DC,
    INKTAG_PIN_RESET, INKTAG_PIN_BUSY, INKTAG_PIN_ENABLE,
    INKTAG_PIN_LED_RED, INKTAG_PIN_LED_GREEN, INKTAG_PIN_LED_BLUE,
    INKTAG_PIN_TXD, INKTAG_PIN_RXD,
    INKTAG_PIN_ROLE_COUNT
} inktag_pin_role_t;

/* Only rail and LED polarity are configurable. Screen signalling belongs to
 * the exact driver/recipe/backend. The two UART positions stay reservedFF. */
#define INKTAG_PIN_ACTIVE_LOW_MASK INKTAG_RELEASE_EDITABLE_ACTIVE_LOW_MASK

typedef struct {
    uint8_t pins[INKTAG_PIN_ROLE_COUNT];
    uint16_t active_low;
} inktag_pin_config_t;

/* Vendor checks physical pins, mux and protected resources. It MUST additionally
 * validate the complete candidate against immutable board/backend facts before
 * applying it. A decoded block or CRC is not electrical qualification. */
typedef bool (*inktag_pin_allowed_fn)(void *context, inktag_pin_role_t role,
                                     uint8_t pin);

bool inktag_pin_config_decode(const uint8_t *data, size_t length,
                              inktag_pin_config_t *out);
size_t inktag_pin_config_encode(const inktag_pin_config_t *config,
                                uint8_t *data, size_t capacity);
bool inktag_pin_config_validate(const inktag_pin_config_t *config,
                                inktag_pin_allowed_fn allowed, void *context);

#endif
