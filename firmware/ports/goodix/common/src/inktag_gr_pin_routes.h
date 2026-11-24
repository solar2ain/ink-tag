#ifndef INKTAG_GR_PIN_ROUTES_H
#define INKTAG_GR_PIN_ROUTES_H
#include "inktag_gr_pin_storage.h"
#include "inktag_epd_config.h"

/* An erased bare base remains unconfigured; all nonempty selectors must be
 * exact IBD1/GPF1. No legacy selector dispatch or guessed board fallback. */
void inktag_gr_pin_routes_init(uint8_t family, uint8_t mcu, uint8_t allowed_boards);
bool inktag_gr_pin_routes_required(void);
bool inktag_gr_pin_routes_available(void);
bool inktag_gr_pin_routes_drive_ready(void);
const inktag_pin_config_t *inktag_gr_pin_routes_peek(void);
/* Last physically applied trusted tuple: parking only, never new GPIO work. */
const inktag_pin_config_t *inktag_gr_pin_routes_applied(void);
void inktag_gr_pin_routes_mark_applied(bool applied);
bool inktag_gr_pin_routes_screen(epd_screen_config_t *out);
bool inktag_gr_pin_routes_config(epd_config_t *inout);
bool inktag_gr_pin_routes_status(uint8_t out[36]);
bool inktag_gr_pin_routes_aux(uint8_t out[INKTAG_GR_PIN_AUX_BYTES]);
inktag_gr_pin_storage_result_t inktag_gr_pin_routes_set(
    const uint8_t *wire, size_t length, bool busy);
inktag_gr_pin_storage_result_t inktag_gr_pin_routes_set_aux(
    const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES], bool busy);
bool inktag_gr_pin_routes_busy_high(bool *out);
void inktag_gr_pin_routes_invalidate(void);
typedef struct {
    inktag_pin_config_t pins;
    bool busy_known;
    bool busy_high;
} inktag_gr_pin_candidate_t;
/* Bounded existing authority read, no GPIO or persistent changes. */
bool inktag_gr_pin_routes_prepare(const epd_screen_config_t *screen,
                                  inktag_gr_pin_candidate_t *candidate);
/* Trusted candidate under the same foreground ownership; no IO/failure. */
void inktag_gr_pin_routes_publish(const inktag_gr_pin_candidate_t *candidate);
#endif
