#ifndef INKTAG_DA_PINS_H
#define INKTAG_DA_PINS_H

#include "inktag_epd_config.h"
#include "inktag_pin_config.h"

/* Arbitrary available DA14585 GPIO mapping for Full defaults and runtime SET.
 * Fixed Flash/SWD/BS resources and package bond-out remain hardware facts. */
bool inktag_da_pins_valid(const inktag_pin_config_t *pins);
bool inktag_da_pins_decode(const uint8_t *wire, size_t length,
                          epd_config_t *config);
size_t inktag_da_pins_encode(const epd_config_t *config, uint8_t *wire,
                             size_t capacity);
/* DA native persistence. False mutation outcomes are unconfirmed: the caller
 * must not publish a route or replay the write. NULL removes only override. */
bool epd_dialog_pin_config_load(epd_config_t *defaults, epd_config_t *effective,
                                bool *has_override);
bool epd_dialog_pin_override_write(const uint8_t *wire);
/* Idle-only release of the previous panel pads. Never changes fixed BS. */
bool EPD_DialogPinsRelease(void);

#endif
