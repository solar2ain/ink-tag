#include "inktag_gr_pin_recovery.h"

#include <string.h>

#include "inktag_gr_settings_store.h"

static bool opaque_screen(void *context, const uint8_t screen[17]) {
    (void)context;
    (void)screen;
    /* Recovery does not qualify or execute screen recipes. The enclosing
     * factory length/identity/checksum/commit and pin ownership are checked.
     * Only a compact LED tuple can escape this maintenance-only consumer. */
    return true;
}

bool inktag_gr_pin_recovery_leds(const inktag_gr_pin_facts_t *facts,
    const uint8_t *factory, size_t length, inktag_gr_pin_recovery_leds_t *out) {
    uint8_t current_factory[76], aux[48];
    uint16_t n = sizeof(current_factory);
    inktag_gr_pin_factory_t decoded;
    inktag_gr_pin_user_t user;
    inktag_gr_pin_recovery_leds_t next;
    inktag_gr_settings_read_result_t result;
    if (!out || !factory || length != 76u ||
        !inktag_gr_pin_factory_decode(factory, 12u, factory + 12u, 64u,
                                       facts, opaque_screen, NULL, &decoded))
        return false;
    if (inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_BOARD_PROFILE, &n,
                               current_factory) != INKTAG_GR_SETTINGS_READ_OK ||
        n != sizeof(current_factory) || memcmp(current_factory, factory, length))
        return false;
    n = sizeof(aux);
    result = inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_AUX_IO, &n, aux);
    if (result == INKTAG_GR_SETTINGS_READ_EMPTY) {
        memset(&user, 0, sizeof(user));
        inktag_gr_pin_aux_default(user.aux);
        user.pins = decoded.pins;
        if (!inktag_gr_pin_validate(&user.pins, facts, user.aux)) return false;
    } else if (result != INKTAG_GR_SETTINGS_READ_OK ||
        !inktag_gr_pin_user_decode(aux, n, facts, &decoded.pins, &user)) return false;
    memcpy(next.pins, &user.pins.pins[INKTAG_PIN_LED_RED], sizeof(next.pins));
    next.active_low = (uint8_t)((user.pins.active_low >> INKTAG_PIN_LED_RED) & 7u);
    *out = next;
    return true;
}
