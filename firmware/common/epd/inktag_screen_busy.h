#ifndef INKTAG_SCREEN_BUSY_H
#define INKTAG_SCREEN_BUSY_H
#include "inktag_epd_screen.h"

/* Physical controller BUSY level before any proven board inverter. Read-only;
 * no GPIO/record changes. Unknown or invalid recipes leave the output intact.
 * Pin-override precedence and electrical inversion belong to the port. */
static inline bool epd_screen_busy_active_high(const epd_screen_config_t *screen,
                                              bool *active_high) {
    epd_screen_t resolved;
    if (!screen || !active_high || epd_screen_resolve(screen, &resolved) != EPD_SCREEN_STATUS_OK)
        return false;
    switch (((uint16_t)screen->driver_id << 8) | screen->panel_recipe) {
#include "inktag_busy_defaults.inc"
        default: return false;
    }
}
#endif
