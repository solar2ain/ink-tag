#include "inktag_gr_pin_config.h"

#include <string.h>

/* A fixed contract alone must not turn a mixed old-common/new-port tree into
 * a release-v1 App or Recovery. Consume the owning shared production batch. */
_Static_assert(INKTAG_AUX_IO_VERSION == INKTAG_RELEASE_AUX_VERSION &&
               INKTAG_PIN_ACTIVE_LOW_MASK == INKTAG_RELEASE_EDITABLE_ACTIVE_LOW_MASK,
               "GR release-v1 requires the matching shared AUX/pin codecs");

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t checksum(const uint8_t *p, size_t n) {
    uint32_t value = UINT32_C(2166136261);
    for (size_t i = 0u; i < n; ++i) value = (value ^ p[i]) * UINT32_C(16777619);
    return value;
}

static bool zero(const uint8_t *p, size_t n) {
    for (size_t i = 0u; i < n; ++i) if (p[i]) return false;
    return true;
}

bool inktag_gr_pin_facts_valid(const inktag_gr_pin_facts_t *facts) {
    if (!facts) return false;
    return (facts->family == 0x33u && facts->board == 1u && facts->mcu == 0x31u) ||
           (facts->family == 0x55u && facts->mcu == 0x13u &&
            (
#if !defined(INKTAG_GOODIX_GR5331)
             facts->board == 1u ||
#endif
             facts->board == 2u || facts->board == 3u)) ||
           (facts->family == 0x55u && facts->mcu == 0x15u && facts->board == 3u);
}

static bool pin_in_valid_facts(void *context, inktag_pin_role_t role, uint8_t pin) {
    const inktag_gr_pin_facts_t *facts = context;
    unsigned group = pin >> 6u, index = pin & 63u;
    return (unsigned)role < INKTAG_PIN_TXD &&
           group < 3u && index < 32u &&
           (facts->usable[group] & (UINT32_C(1) << index)) != 0u &&
           ((role == INKTAG_PIN_ENABLE) == (pin == facts->rail)) &&
           pin != facts->button;
}

bool inktag_gr_pin_allowed(void *context, inktag_pin_role_t role, uint8_t pin) {
    return inktag_gr_pin_facts_valid(context) && pin_in_valid_facts(context, role, pin);
}

void inktag_gr_pin_aux_default(uint8_t aux[INKTAG_GR_PIN_AUX_BYTES]) {
    const uint8_t value[INKTAG_GR_PIN_AUX_BYTES] = {
        INKTAG_RELEASE_AUX_VERSION,255u,0u,255u,0u,255u,0u,0u,255u,255u,0xD0u,7u};
    if (aux) memcpy(aux, value, sizeof(value));
}

bool inktag_gr_pin_aux_valid(const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES],
                              const inktag_gr_pin_facts_t *facts) {
    inktag_aux_io_t config;
    if (!aux || !inktag_gr_pin_facts_valid(facts) ||
        aux[0] != INKTAG_RELEASE_AUX_VERSION ||
        !inktag_aux_io_decode(aux, INKTAG_GR_PIN_AUX_BYTES, &config)) return false;
    /* Only the existing physical slot0 is qualified; no accepted slot is
     * silently omitted from the persisted complete shared block. */
    if ((config.buttons[0].gpio != 255u && config.buttons[0].gpio != facts->button) ||
        config.buttons[1].gpio != 255u || config.buttons[2].gpio != 255u) return false;
    return true;
}

bool inktag_gr_pin_validate(const inktag_pin_config_t *pins,
                             const inktag_gr_pin_facts_t *facts,
                             const uint8_t aux[INKTAG_GR_PIN_AUX_BYTES]) {
    if (!inktag_gr_pin_aux_valid(aux, facts) ||
        /* AUX admission already validated these immutable backend facts.
         * Hoist that pure check out of the per-role loop, not out of the
         * request. The standalone public predicate still validates context. */
        !inktag_pin_config_validate(pins, pin_in_valid_facts, (void *)facts))
        return false;
    /* The wire stores only EN/LED polarity; CS/reset/BUSY are not another
     * user authority. The bus keeps recipe BUSY in private runtime state. */
    if (pins->active_low & (uint16_t)~INKTAG_RELEASE_EDITABLE_ACTIVE_LOW_MASK) return false;
    for (unsigned role = 0u; role < INKTAG_PIN_ROLE_COUNT; ++role) {
        uint8_t pin = pins->pins[role];
        if (pin == 255u) continue;
        if (pin == aux[1] || pin == aux[3] || pin == aux[5]) return false;
    }
    return true;
}

bool inktag_gr_pin_factory_decode(const uint8_t *selector, size_t selector_length,
    const uint8_t *record, size_t record_length, const inktag_gr_pin_facts_t *facts,
    inktag_gr_pin_screen_allowed_fn screen_allowed, void *screen_context,
    inktag_gr_pin_factory_t *out) {
    inktag_gr_pin_factory_t candidate;
    uint8_t aux[INKTAG_GR_PIN_AUX_BYTES];
    if (!selector || !record || !out || !screen_allowed ||
        !inktag_gr_pin_facts_valid(facts) || selector_length != 12u ||
        record_length != 64u || memcmp(selector, "IBD1", 4u) ||
        selector[4] != 1u || selector[5] != facts->family ||
        selector[6] != facts->board || selector[7] != 1u ||
        u32(selector + 8u) != checksum(selector, 8u) ||
        memcmp(record, "GPF1", 4u) || record[4] != 1u ||
        record[5] != facts->family || record[6] != facts->board ||
        record[7] != facts->mcu || !zero(record + 41u, 15u) ||
        u32(record + 56u) != checksum(record, 56u) ||
        memcmp(record + 60u, "GPF!", 4u) ||
        !inktag_pin_config_decode(record + 8u, 16u, &candidate.pins)) return false;
    inktag_gr_pin_aux_default(aux);
    if (!inktag_gr_pin_validate(&candidate.pins, facts, aux) ||
        !screen_allowed(screen_context, record + 24u)) return false;
    memcpy(candidate.screen, record + 24u, sizeof(candidate.screen));
    *out = candidate;
    return true;
}

bool inktag_gr_pin_user_decode(const uint8_t *data, size_t length,
    const inktag_gr_pin_facts_t *facts, const inktag_pin_config_t *defaults,
    inktag_gr_pin_user_t *out) {
    inktag_gr_pin_user_t candidate;
    if (!data || !out || !defaults) return false;
    memset(&candidate, 0, sizeof(candidate));
    candidate.pins = *defaults;
    if (length != 48u || memcmp(data, "GUF1", 4u) || data[4] != 1u ||
        data[5] > 1u || !zero(data + 34u, 6u) ||
        u32(data + 40u) != checksum(data, 40u) ||
        memcmp(data + 44u, "GUF!", 4u)) return false;
    memcpy(candidate.aux, data + 6u, sizeof(candidate.aux));
    candidate.override_present = data[5] != 0u;
    if (candidate.override_present) {
        if (!inktag_pin_config_decode(data + 18u, 16u, &candidate.pins)) return false;
    } else if (!zero(data + 18u, 16u)) return false;
    /* Validate the default even when overridden. A user block cannot repair
     * missing or corrupt immutable authority by hiding it. */
    if (!inktag_gr_pin_validate(defaults, facts, candidate.aux) ||
        !inktag_gr_pin_validate(&candidate.pins, facts, candidate.aux)) return false;
    *out = candidate;
    return true;
}

bool inktag_gr_pin_user_encode(const inktag_gr_pin_user_t *user,
    const inktag_gr_pin_facts_t *facts, const inktag_pin_config_t *defaults,
    uint8_t out[48]) {
    uint8_t candidate[48] = {0};
    if (!user || !out || !defaults ||
        !inktag_gr_pin_validate(defaults, facts, user->aux) ||
        !inktag_gr_pin_validate(&user->pins, facts, user->aux)) return false;
    /* An absent override is represented by the default, never stale user data. */
    if (!user->override_present &&
        (user->pins.active_low != defaults->active_low ||
         memcmp(user->pins.pins, defaults->pins, sizeof(defaults->pins)))) return false;
    memcpy(candidate, "GUF1", 4u);
    candidate[4] = 1u;
    candidate[5] = user->override_present ? 1u : 0u;
    memcpy(candidate + 6u, user->aux, sizeof(user->aux));
    if (user->override_present &&
        !inktag_pin_config_encode(&user->pins, candidate + 18u, 16u)) return false;
    put32(candidate + 40u, checksum(candidate, 40u));
    memcpy(candidate + 44u, "GUF!", 4u);
    memcpy(out, candidate, sizeof(candidate));
    return true;
}
