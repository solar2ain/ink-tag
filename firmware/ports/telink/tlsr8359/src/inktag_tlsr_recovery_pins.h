#ifndef INKTAG_TLSR_RECOVERY_PINS_H
#define INKTAG_TLSR_RECOVERY_PINS_H
#include <stdbool.h>
#include <stdint.h>
/* No panel/board description, user actions or mutable Settings state. */
typedef struct {
    uint16_t pins[3];
    uint8_t active_low;
} inktag_tlsr_recovery_led_pins_t;
bool inktag_tlsr_recovery_led_pins(inktag_tlsr_recovery_led_pins_t *out);
#endif
