#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#line 1 "../Src/user/inktag_gr_pin_storage.c"
#include "inktag_gr_pin_storage.h"

#include <string.h>

#include "inktag_gr_health.h"
#include "inktag_gr_settings_store.h"

static bool factory_valid(const uint8_t raw[76], const inktag_gr_pin_facts_t *facts,
    inktag_gr_pin_screen_allowed_fn allowed, void *context,
    inktag_gr_pin_factory_t *out) {
    return inktag_gr_pin_factory_decode(raw, 12u, raw + 12u, 64u,
                                       facts, allowed, context, out);
}

static bool factory_matches(const inktag_gr_pin_storage_t *state) {
    uint8_t raw[76];
    uint16_t n = sizeof(raw);
    return inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_BOARD_PROFILE, &n, raw) ==
               INKTAG_GR_SETTINGS_READ_OK && n == sizeof(raw) &&
           !memcmp(raw, state->factory, sizeof(raw));
}

static bool read_user(const inktag_gr_pin_facts_t *facts,
    const inktag_pin_config_t *defaults, inktag_gr_pin_user_t *out) {
    inktag_gr_pin_user_t next;
    uint8_t raw[48];
    uint16_t n = sizeof(raw);
    inktag_gr_settings_read_result_t result = inktag_gr_settings_get(
        INKTAG_GR_SETTINGS_SLOT_AUX_IO, &n, raw);
    if (result == INKTAG_GR_SETTINGS_READ_EMPTY) {
        memset(&next, 0, sizeof(next));
        inktag_gr_pin_aux_default(next.aux);
        next.pins = *defaults;
        if (!inktag_gr_pin_validate(&next.pins, facts, next.aux)) return false;
    } else if (result != INKTAG_GR_SETTINGS_READ_OK ||
               !inktag_gr_pin_user_decode(raw, n, facts, defaults, &next)) {
        return false;
    }
    *out = next;
    return true;
}

bool inktag_gr_pin_storage_snapshot(const inktag_gr_pin_storage_t *state,
    inktag_gr_pin_factory_t *factory, inktag_gr_pin_user_t *user) {
    inktag_gr_pin_factory_t next_factory;
    inktag_gr_pin_user_t next_user;
    if (!state || !factory || !user || !state->ready) return false;
    if (!factory_matches(state) ||
        !factory_valid(state->factory, state->facts, state->screen_allowed,
                       state->screen_context, &next_factory)) return false;
    if (!read_user(state->facts, &next_factory.pins, &next_user)) return false;
    *factory = next_factory;
    *user = next_user;
    return true;
}
bool inktag_gr_pin_storage_read(inktag_gr_pin_storage_t *state,
    inktag_gr_pin_factory_t *factory, inktag_gr_pin_user_t *user) {
    if (inktag_gr_pin_storage_snapshot(state, factory, user)) return true;
    if (state) state->ready = false;
    return false;
}

bool inktag_gr_pin_storage_init(inktag_gr_pin_storage_t *state,
    const inktag_gr_pin_facts_t *facts, const uint8_t *factory, size_t length,
    inktag_gr_pin_screen_allowed_fn screen_allowed, void *screen_context) {
    inktag_gr_pin_factory_t decoded;
    inktag_gr_pin_user_t user;
    uint8_t current[76];
    uint16_t n = sizeof(current);
    inktag_gr_settings_read_result_t result;
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    if (!facts || !factory || length != 76u ||
        !factory_valid(factory, facts, screen_allowed, screen_context, &decoded))
        return false;
    /* Neither absence nor a corrupt sibling authorizes a boot write. */
    if (!read_user(facts, &decoded.pins, &user)) return false;
    result = inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_BOARD_PROFILE, &n, current);
    if (result != INKTAG_GR_SETTINGS_READ_OK || n != length ||
        memcmp(current, factory, length)) return false;
    state->facts = facts;
    memcpy(state->factory, factory, length);
    state->screen_allowed = screen_allowed;
    state->screen_context = screen_context;
    state->ready = true;
    return inktag_gr_pin_storage_read(state, &decoded, &user);
}

bool inktag_gr_pin_storage_report(inktag_gr_pin_storage_t *state, uint8_t out[36]) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t user;
    uint8_t next[36];
    if (!out || !inktag_gr_pin_storage_read(state, &factory, &user)) return false;
    next[0] = INKTAG_RELEASE_PIN_REPORT_VERSION;
    next[1] = 2u | (user.override_present ? 1u : 0u);
    next[2] = (uint8_t)INKTAG_GR_PIN_EDITABLE_ROLES;
    next[3] = (uint8_t)(INKTAG_GR_PIN_EDITABLE_ROLES >> 8u);
    if (!inktag_pin_config_encode(&factory.pins, next + 4u, 16u) ||
        !inktag_pin_config_encode(&user.pins, next + 20u, 16u)) return false;
    memcpy(out, next, sizeof(next));
    return true;
}

static inktag_gr_pin_storage_result_t commit(inktag_gr_pin_storage_t *state,
    const inktag_gr_pin_factory_t *factory, const inktag_gr_pin_user_t *before,
    const inktag_gr_pin_user_t *after) {
    uint8_t old[48], next[48], readback[48];
    uint16_t n = sizeof(readback);
    inktag_gr_settings_write_result_t result;
    if (!inktag_gr_pin_user_encode(after, state->facts, &factory->pins, next) ||
        !inktag_gr_pin_user_encode(before, state->facts, &factory->pins, old))
        return INKTAG_GR_PIN_STORAGE_INVALID;
    /* No boot migration and no rewrite for a semantic no-op. */
    if (!memcmp(old, next, sizeof(next))) return INKTAG_GR_PIN_STORAGE_OK;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_GR_PIN_STORAGE_LOW_POWER;
    result = inktag_gr_settings_put_result(INKTAG_GR_SETTINGS_SLOT_AUX_IO,
                                          sizeof(next), next);
    if (result != INKTAG_GR_SETTINGS_WRITE_OK) {
        /* Do not publish either the previous or intended route on an unknown
         * commit outcome. Only an explicit later boot/init can re-establish it. */
        state->ready = false;
        return result == INKTAG_GR_SETTINGS_WRITE_NO_SPACE ?
            INKTAG_GR_PIN_STORAGE_NO_SPACE : INKTAG_GR_PIN_STORAGE_ERROR;
    }
    if (inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_AUX_IO, &n, readback) !=
            INKTAG_GR_SETTINGS_READ_OK || n != sizeof(next) ||
        memcmp(readback, next, sizeof(next)) || !factory_matches(state)) {
        state->ready = false;
        return INKTAG_GR_PIN_STORAGE_ERROR;
    }
    return INKTAG_GR_PIN_STORAGE_OK;
}

inktag_gr_pin_storage_result_t inktag_gr_pin_storage_set_override(
    inktag_gr_pin_storage_t *state, const uint8_t *wire, size_t length, bool busy) {
    inktag_gr_pin_factory_t factory;
    inktag_gr_pin_user_t before, after;
    inktag_pin_config_t pins;
    if (busy) return INKTAG_GR_PIN_STORAGE_BUSY;
    if (wire ? !inktag_pin_config_decode(wire, length, &pins) : length != 0u)
        return INKTAG_GR_PIN_STORAGE_INVALID;
    if (!inktag_gr_pin_storage_read(state, &factory, &before))
        return INKTAG_GR_PIN_STORAGE_ERROR;
    after = before;
    after.override_present = wire != NULL;
    after.pins = wire ? pins : factory.pins;
    return commit(state, &factory, &before, &after);
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_aux_storage_pro.inc"
#else
inktag_gr_pin_storage_result_t inktag_gr_pin_storage_set_aux(inktag_gr_pin_storage_t*s,const uint8_t a[INKTAG_GR_PIN_AUX_BYTES],bool busy){(void)s;(void)a;(void)busy;return INKTAG_GR_PIN_STORAGE_INVALID;}
#endif
#line 163 "../Src/user/inktag_gr_pin_storage.c"
