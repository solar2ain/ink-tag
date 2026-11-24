#ifndef INKTAG_OM_PINS_H
#define INKTAG_OM_PINS_H
#include "inktag_om_pin_layout.h"
#include "inktag_epd_screen.h"
typedef struct {
    inktag_pin_config_t defaults, effective;
    bool overridden, busy_known, busy_invert;
} inktag_om_pin_candidate_t;
/* Prepare performs only bounded reads. Publish cannot fail or perform IO. */
bool inktag_om_pins_prepare(const epd_screen_config_t *, inktag_om_pin_candidate_t *);
bool inktag_om_pins_prepare_override(bool, const uint8_t *, inktag_om_pin_candidate_t *);
void inktag_om_pins_publish(const inktag_om_pin_candidate_t *);
bool inktag_om_pins_screen_available(void);
bool inktag_om_pins_busy_level(bool raw);
bool inktag_om_pins_active_low(inktag_pin_role_t role);
/* Read-only consumers share the same bounded board/preferences decoders. */
void inktag_om_pins_reload(void);
void inktag_om_pins_invalidate(void);
bool inktag_om_pins_available(void);
bool inktag_om_pins_info(uint8_t out[36]);
bool inktag_om_pins_effective(inktag_pin_config_t *out);
uint8_t inktag_om_pins_led(uint8_t channel);
bool inktag_om_pins_has_override(void);
#endif
