#ifndef INKTAG_GR_PIN_STORAGE_H
#define INKTAG_GR_PIN_STORAGE_H

#include "inktag_gr_pin_config.h"

typedef enum {
    INKTAG_GR_PIN_STORAGE_OK,
    INKTAG_GR_PIN_STORAGE_INVALID,
    INKTAG_GR_PIN_STORAGE_BUSY,
    INKTAG_GR_PIN_STORAGE_LOW_POWER,
    INKTAG_GR_PIN_STORAGE_NO_SPACE,
    INKTAG_GR_PIN_STORAGE_ERROR
} inktag_gr_pin_storage_result_t;

typedef struct {
    /* Trusted immutable compiled table, with static lifetime. Never a host
     * configuration or caller-stack object. No duplicate table in scarce RAM. */
    const inktag_gr_pin_facts_t *facts;
    uint8_t factory[INKTAG_GR_PIN_FACTORY_SETTINGS_BYTES];
    inktag_gr_pin_screen_allowed_fn screen_allowed;
    void *screen_context;
    bool ready;
} inktag_gr_pin_storage_t;

/* Read-only initialization. Configured Full seeds the complete protected
 * BOARD_PROFILE offline; ordinary boot never imports or repairs it. */
bool inktag_gr_pin_storage_init(inktag_gr_pin_storage_t *state,
    const inktag_gr_pin_facts_t *facts, const uint8_t *factory, size_t length,
    inktag_gr_pin_screen_allowed_fn screen_allowed, void *screen_context);

/* Fresh snapshots, no GPIO changes. Failure invalidates this RAM consumer;
 * the caller must quarantine GPIO users instead of applying a stale route.
 * There is deliberately no automatic repair/reset/retry entry. */
bool inktag_gr_pin_storage_read(inktag_gr_pin_storage_t *state,
    inktag_gr_pin_factory_t *factory, inktag_gr_pin_user_t *user);
bool inktag_gr_pin_storage_snapshot(const inktag_gr_pin_storage_t *state,
    inktag_gr_pin_factory_t *factory, inktag_gr_pin_user_t *user);
bool inktag_gr_pin_storage_report(inktag_gr_pin_storage_t *state, uint8_t out[36]);

/* Busy is supplied by App's existing complete work-owner check, before any
 * mutation. All candidates are validated before the fresh voltage check/put.
 * Publish/apply GPIO only after OK. NULL/0 removes just the pin override,
 * never the auxiliary tuple or the protected factory record. */
inktag_gr_pin_storage_result_t inktag_gr_pin_storage_set_override(
    inktag_gr_pin_storage_t *state, const uint8_t *wire, size_t length, bool busy);
inktag_gr_pin_storage_result_t inktag_gr_pin_storage_set_aux(
    inktag_gr_pin_storage_t *state, const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES], bool busy);

#endif
