#ifndef INKTAG_GR_PIN_RECOVERY_H
#define INKTAG_GR_PIN_RECOVERY_H

#include "inktag_gr_pin_config.h"

typedef struct {
    uint8_t pins[3];
    uint8_t active_low;
} inktag_gr_pin_recovery_leds_t;

/* Bounded maintenance subset, not a complete App-profile result. The existing
 * read-only initialized settings backend must already be ready. No init, put,
 * delete, GC, GPIO, screen recipe, password, Find or retention operation. */
bool inktag_gr_pin_recovery_leds(const inktag_gr_pin_facts_t *facts,
    const uint8_t *factory, size_t length, inktag_gr_pin_recovery_leds_t *out);

#endif
