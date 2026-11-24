#include "inktag_status.h"

#include <string.h>

#define NO_SCENE 0xFFu
#define READY_TAIL_SCENE 0xFEu
#define LOCATE_SCENE 0xFDu
#define LOCATE_ACTIVE (1u << 13)
#define READY_APPLICATION_PENDING (1u << 14)
#define READY_RECOVERY_PENDING (1u << 15)
#define READY_PENDING_MASK \
    (READY_APPLICATION_PENDING | READY_RECOVERY_PENDING)
#define FAST_FLASH_HALF_MS 120u
#define SLOW_FLASH_HALF_MS 240u
#define TRANSIENT_DEFAULT_MS 1200u

static const uint8_t s_default_scenes[INKTAG_STATUS_SCENE_COUNT] = {
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_WHITE, INKTAG_STATUS_EFFECT_SEQUENCE_SLOW),
    INKTAG_STATUS_SCENE_VALUE(0u, INKTAG_STATUS_EFFECT_OFF),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_GREEN, INKTAG_STATUS_EFFECT_FLASH_TWICE),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_BLUE, INKTAG_STATUS_EFFECT_SLOW_FLASH),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_BLUE, INKTAG_STATUS_EFFECT_FAST_FLASH),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_BLUE, INKTAG_STATUS_EFFECT_SLOW_FLASH),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_BLUE, INKTAG_STATUS_EFFECT_FAST_FLASH),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_GREEN, INKTAG_STATUS_EFFECT_FLASH_THREE),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_RED, INKTAG_STATUS_EFFECT_FLASH_THREE),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_RED, INKTAG_STATUS_EFFECT_SLOW_FLASH),
    INKTAG_STATUS_SCENE_VALUE(INKTAG_STATUS_COLOR_WHITE, INKTAG_STATUS_EFFECT_SEQUENCE_SLOW),
};

static const uint8_t s_priority[INKTAG_STATUS_SCENE_COUNT] = {
    INKTAG_STATUS_SCENE_FIND,
    INKTAG_STATUS_SCENE_ERROR,
    INKTAG_STATUS_SCENE_OTA_INSTALL,
    INKTAG_STATUS_SCENE_OTA,
    INKTAG_STATUS_SCENE_TRANSFER,
    INKTAG_STATUS_SCENE_REFRESH,
    INKTAG_STATUS_SCENE_LOW_BATTERY,
    INKTAG_STATUS_SCENE_SUCCESS,
    INKTAG_STATUS_SCENE_STARTUP,
    INKTAG_STATUS_SCENE_CONNECTED,
    INKTAG_STATUS_SCENE_ADVERTISING,
};

static bool before(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) < 0;
}

static bool policy_allows_scene(uint8_t policy, uint8_t scene) {
    if (policy != INKTAG_STATUS_POLICY_CRITICAL) return true;
    return scene == INKTAG_STATUS_SCENE_STARTUP ||
           scene == INKTAG_STATUS_SCENE_FIND ||
           scene == INKTAG_STATUS_SCENE_OTA ||
           scene == INKTAG_STATUS_SCENE_OTA_INSTALL;
}

static uint8_t scene_value(const inktag_status_runtime_t *runtime, uint8_t scene) {
    (void)runtime;
    return s_default_scenes[scene];
}

static uint8_t map_color(uint8_t requested, uint8_t available);

static uint8_t ready_tail_color(const inktag_status_runtime_t *runtime) {
#if defined(INKTAG_STATUS_SINGLE_QUALIFIED_COLOR) &&  INKTAG_STATUS_SINGLE_QUALIFIED_COLOR
    (void)runtime;
    /* Logical green/blue both map to the sole exact-board channel. */
    return INKTAG_STATUS_COLOR_BLUE;
#else
    return (runtime->active_mask & READY_RECOVERY_PENDING) != 0u
        ? INKTAG_STATUS_COLOR_BLUE : INKTAG_STATUS_COLOR_GREEN;
#endif
}

static uint8_t sequence_color_count(uint8_t requested, uint8_t available) {
#if defined(INKTAG_STATUS_SINGLE_QUALIFIED_COLOR) &&  INKTAG_STATUS_SINGLE_QUALIFIED_COLOR
    return requested != 0u &&
           (available & INKTAG_STATUS_COLOR_WHITE) != 0u ? 1u : 0u;
#else
    uint8_t choices = requested & available & INKTAG_STATUS_COLOR_WHITE;
    uint8_t count = 0u;
    if (choices == 0u) choices = map_color(requested, available);
    for (uint8_t bit = INKTAG_STATUS_COLOR_RED;
         bit <= INKTAG_STATUS_COLOR_BLUE; bit <<= 1u) {
        if ((choices & bit) != 0u) count++;
    }
    return count;
#endif
}

static uint32_t transient_duration(const inktag_status_runtime_t *runtime,
                                   uint8_t value, uint8_t scene) {
    uint8_t effect = INKTAG_STATUS_SCENE_EFFECT(value);
    if (effect >= INKTAG_STATUS_EFFECT_FLASH_ONCE &&
        effect <= INKTAG_STATUS_EFFECT_FLASH_THREE)
        return FAST_FLASH_HALF_MS * 2u *
            (effect - INKTAG_STATUS_EFFECT_FLASH_ONCE + 1u);
    switch (effect) {
        case INKTAG_STATUS_EFFECT_SEQUENCE_SLOW:
            if (scene == INKTAG_STATUS_SCENE_STARTUP) {
                return INKTAG_STATUS_STARTUP_COLOR_MS *
                    sequence_color_count(INKTAG_STATUS_SCENE_COLOR(value),
                                         runtime->available_colors);
            }
            return SLOW_FLASH_HALF_MS * 6u;
        default: return TRANSIENT_DEFAULT_MS;
    }
}

static uint8_t priority_scene(const inktag_status_runtime_t *runtime,
                              uint8_t transient) {
    /* Rendering and the read-only timer query use the same priority order;
     * only the renderer consumes expired feedback and queued ready tails. */
    for (uint8_t i = 0; i < INKTAG_STATUS_SCENE_COUNT; i++) {
        uint8_t scene = s_priority[i];
        if (!policy_allows_scene(runtime->config.policy, scene)) continue;
        /* Transient feedback participates in the same priority order as an
         * active scene. In particular, a just-connected flash must not hide a
         * transfer that starts before the status timer gets another turn. */
        if (scene == transient) return scene;
        if (scene == INKTAG_STATUS_SCENE_STARTUP &&
            transient == READY_TAIL_SCENE) return READY_TAIL_SCENE;
        if ((runtime->active_mask & (1u << scene)) != 0u) return scene;
    }
    return NO_SCENE;
}

static uint8_t selected_scene(inktag_status_runtime_t *runtime, uint32_t now_ms) {
    uint8_t transient = runtime->transient_scene;
    if ((runtime->active_mask & LOCATE_ACTIVE) != 0u) {
        if (runtime->current_scene != LOCATE_SCENE ||
            now_ms - runtime->scene_started_at <
                INKTAG_STATUS_LOCATE_DURATION_MS) {
            return LOCATE_SCENE;
        }
        runtime->active_mask &= (uint16_t)~LOCATE_ACTIVE;
    }
    if (runtime->transient_scene != NO_SCENE) {
        if (!before(now_ms, runtime->transient_until)) {
            uint8_t expired = runtime->transient_scene;
            runtime->transient_scene = NO_SCENE;
            transient = NO_SCENE;
            if (expired == INKTAG_STATUS_SCENE_STARTUP &&
                (runtime->active_mask & READY_PENDING_MASK) != 0u) {
                runtime->transient_scene = READY_TAIL_SCENE;
                runtime->transient_until = now_ms +
                    INKTAG_STATUS_READY_HALF_MS * 7u;
                transient = READY_TAIL_SCENE;
            } else if (expired == READY_TAIL_SCENE) {
                runtime->active_mask &= (uint16_t)~READY_PENDING_MASK;
            }
        }
    }
    return priority_scene(runtime, transient);
}

static uint8_t map_color(uint8_t requested, uint8_t available) {
    available &= INKTAG_STATUS_COLOR_WHITE;
    if (requested == 0u || available == 0u) return 0u;
#if defined(INKTAG_STATUS_SINGLE_QUALIFIED_COLOR) &&  INKTAG_STATUS_SINGLE_QUALIFIED_COLOR
    /* Exact-board adapters using this specialization expose one qualified
     * physical channel and map every non-off logical colour onto it. */
    return available;
#else
    if ((available & (available - 1u)) == 0u) return available;
    uint8_t mapped = requested & available;
    if (mapped != 0u) return mapped;
    if ((available & INKTAG_STATUS_COLOR_BLUE) != 0u) return INKTAG_STATUS_COLOR_BLUE;
    if ((available & INKTAG_STATUS_COLOR_GREEN) != 0u) return INKTAG_STATUS_COLOR_GREEN;
    return INKTAG_STATUS_COLOR_RED;
#endif
}

static bool effect_on(uint8_t effect, uint32_t elapsed_ms) {
    if (effect >= INKTAG_STATUS_EFFECT_FLASH_ONCE &&
        effect <= INKTAG_STATUS_EFFECT_FLASH_THREE)
        return elapsed_ms < FAST_FLASH_HALF_MS * 2u *
                   (effect - INKTAG_STATUS_EFFECT_FLASH_ONCE + 1u) &&
               (elapsed_ms % (FAST_FLASH_HALF_MS * 2u)) < FAST_FLASH_HALF_MS;
    switch (effect) {
        case INKTAG_STATUS_EFFECT_SOLID: return true;
        case INKTAG_STATUS_EFFECT_SLOW_FLASH:
            return (elapsed_ms % (SLOW_FLASH_HALF_MS * 2u)) < SLOW_FLASH_HALF_MS;
        case INKTAG_STATUS_EFFECT_FAST_FLASH:
            return (elapsed_ms % (FAST_FLASH_HALF_MS * 2u)) < FAST_FLASH_HALF_MS;
        default: return false;
    }
}

static uint8_t sequence_output(uint8_t requested, uint8_t available,
                               uint32_t elapsed_ms, uint32_t step_ms) {
#if defined(INKTAG_STATUS_SINGLE_QUALIFIED_COLOR) &&  INKTAG_STATUS_SINGLE_QUALIFIED_COLOR
    uint8_t choices = requested != 0u ?
        available & INKTAG_STATUS_COLOR_WHITE : 0u;
    if (choices == 0u) return 0u;
    return ((elapsed_ms / step_ms) & 1u) == 0u ? choices : 0u;
#else
    uint8_t choices = requested & available & INKTAG_STATUS_COLOR_WHITE;
    if (choices == 0u) choices = map_color(requested, available);
    uint8_t count = sequence_color_count(requested, available);
    if (count == 0u) return 0u;
    uint32_t phase = elapsed_ms / step_ms;
    if (count == 1u) {
        return (phase & 1u) == 0u ? choices : 0u;
    }
    uint8_t selected = (uint8_t)(phase % count);
    for (uint8_t bit = INKTAG_STATUS_COLOR_RED;
         bit <= INKTAG_STATUS_COLOR_BLUE; bit <<= 1u) {
        if ((choices & bit) == 0u) continue;
        if (selected == 0u) return bit;
        selected--;
    }
    return 0u;
#endif
}

static void render(inktag_status_runtime_t *runtime, uint32_t now_ms) {
    if (!runtime) return;
    uint8_t scene = selected_scene(runtime, now_ms);
    if (scene != runtime->current_scene) {
        runtime->current_scene = scene;
        runtime->scene_started_at = now_ms;
    }
    uint8_t output = 0u;
    if (scene == LOCATE_SCENE) {
        output = sequence_output(INKTAG_STATUS_COLOR_WHITE,
                                 runtime->available_colors,
                                 now_ms - runtime->scene_started_at,
                                 INKTAG_STATUS_LOCATE_COLOR_MS);
    } else if (runtime->config.policy != INKTAG_STATUS_POLICY_OFF &&
               scene != NO_SCENE) {
        uint8_t value = scene == READY_TAIL_SCENE
            ? INKTAG_STATUS_SCENE_VALUE(ready_tail_color(runtime),
                                        INKTAG_STATUS_EFFECT_FLASH_THREE)
            : scene_value(runtime, scene);
        uint8_t effect = INKTAG_STATUS_SCENE_EFFECT(value);
        if (effect == INKTAG_STATUS_EFFECT_SEQUENCE_SLOW) {
            output = sequence_output(INKTAG_STATUS_SCENE_COLOR(value),
                                     runtime->available_colors,
                                     now_ms - runtime->scene_started_at,
                                     scene == INKTAG_STATUS_SCENE_STARTUP ?
                                     INKTAG_STATUS_STARTUP_COLOR_MS : SLOW_FLASH_HALF_MS);
        } else if (scene == READY_TAIL_SCENE) {
            uint32_t elapsed = now_ms - runtime->scene_started_at;
            if (elapsed >= INKTAG_STATUS_READY_HALF_MS &&
                effect_on(effect, elapsed - INKTAG_STATUS_READY_HALF_MS)) {
                output = map_color(INKTAG_STATUS_SCENE_COLOR(value),
                                   runtime->available_colors);
            }
        } else if (effect_on(effect, now_ms - runtime->scene_started_at)) {
            output = map_color(INKTAG_STATUS_SCENE_COLOR(value), runtime->available_colors);
        }
    }
    if (runtime->apply && output != runtime->last_output) {
        runtime->last_output = output;
        runtime->apply(runtime->context, output);
    }
}

void inktag_status_config_default(inktag_status_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->version = INKTAG_STATUS_PROTOCOL_VERSION;
    config->policy = INKTAG_STATUS_POLICY_DEFAULT;
}

uint8_t inktag_status_default_scene(inktag_status_scene_t scene) {
    return scene < INKTAG_STATUS_SCENE_COUNT ? s_default_scenes[scene] : 0u;
}

bool inktag_status_config_valid(const inktag_status_config_t *config) {
    return config && config->version == INKTAG_STATUS_PROTOCOL_VERSION &&
           (config->policy == INKTAG_STATUS_POLICY_OFF ||
            config->policy == INKTAG_STATUS_POLICY_ALL ||
            config->policy == INKTAG_STATUS_POLICY_CRITICAL);
}

bool inktag_status_config_encode(const inktag_status_config_t *config,
                                 uint8_t wire[INKTAG_STATUS_WIRE_SIZE]) {
    if (!wire || !inktag_status_config_valid(config)) return false;
    wire[0] = config->version;
    wire[1] = config->policy;
    return true;
}

bool inktag_status_config_decode(inktag_status_config_t *config,
                                 const uint8_t *wire, uint16_t length) {
    if (!config || !wire || length != INKTAG_STATUS_WIRE_SIZE) return false;
    inktag_status_config_t decoded = {0};
    decoded.version = wire[0];
    decoded.policy = wire[1];
    if (!inktag_status_config_valid(&decoded)) return false;
    *config = decoded;
    return true;
}

bool inktag_temperature_select_lower(bool first_valid,
                                     int16_t first_sixteenths,
                                     bool second_valid,
                                     int16_t second_sixteenths,
                                     int8_t *celsius) {
    int16_t selected;
    int16_t whole;
    if (!first_valid && !second_valid) return false;
    if (!first_valid) selected = second_sixteenths;
    else if (!second_valid) selected = first_sixteenths;
    else selected = first_sixteenths < second_sixteenths
        ? first_sixteenths : second_sixteenths;
    whole = selected / 16;
    if (selected < 0 && selected % 16 != 0) --whole;
    if (whole < -128 || whole > 127) return false;
    if (celsius != NULL) *celsius = (int8_t)whole;
    return true;
}

void inktag_status_init(inktag_status_runtime_t *runtime,
                        const inktag_status_config_t *config,
                        uint8_t available_colors,
                        inktag_status_apply_fn apply, void *context,
                        uint32_t now_ms) {
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
    if (inktag_status_config_valid(config)) runtime->config = *config;
    else inktag_status_config_default(&runtime->config);
    runtime->apply = apply;
    runtime->context = context;
    runtime->available_colors = available_colors & INKTAG_STATUS_COLOR_WHITE;
    runtime->current_scene = NO_SCENE;
    runtime->transient_scene = NO_SCENE;
    runtime->last_output = 0xFFu;
    render(runtime, now_ms);
}

void inktag_status_set_config(inktag_status_runtime_t *runtime,
                              const inktag_status_config_t *config,
                              uint32_t now_ms) {
    if (!runtime || !inktag_status_config_valid(config)) return;
    runtime->config = *config;
    runtime->last_output = 0xFFu;
    render(runtime, now_ms);
}

void inktag_status_set_available_colors(inktag_status_runtime_t *runtime,
                                        uint8_t available_colors,
                                        uint32_t now_ms) {
    if (!runtime) return;
    runtime->available_colors = available_colors & INKTAG_STATUS_COLOR_WHITE;
    runtime->last_output = 0xFFu;
    render(runtime, now_ms);
}

void inktag_status_set_active(inktag_status_runtime_t *runtime,
                              inktag_status_scene_t scene, bool active,
                              uint32_t now_ms) {
    if (!runtime || scene >= INKTAG_STATUS_SCENE_COUNT) return;
    uint16_t bit = (uint16_t)(1u << scene);
    if (active) runtime->active_mask |= bit;
    else runtime->active_mask &= (uint16_t)~bit;
    render(runtime, now_ms);
}

void inktag_status_trigger(inktag_status_runtime_t *runtime,
                           inktag_status_scene_t scene, uint32_t now_ms) {
    if (!runtime || scene >= INKTAG_STATUS_SCENE_COUNT) return;
    /* STARTUP is a short, product-wide ordering barrier. Connection and
     * ordinary completion feedback that arrives during its RGB round is stale by
     * the time the sequence finishes, so drop it instead of consuming RAM for
     * a general queue or cutting the startup indication short. Role readiness
     * uses the one dedicated fixed tail slot below. */
    if (scene != INKTAG_STATUS_SCENE_STARTUP &&
        runtime->transient_scene == INKTAG_STATUS_SCENE_STARTUP &&
        before(now_ms, runtime->transient_until)) return;
    runtime->active_mask &= (uint16_t)~READY_PENDING_MASK;
    runtime->transient_scene = (uint8_t)scene;
    runtime->transient_until = now_ms +
        transient_duration(runtime, scene_value(runtime, (uint8_t)scene),
                           (uint8_t)scene);
    runtime->current_scene = NO_SCENE;
    render(runtime, now_ms);
}

void inktag_status_role_ready(inktag_status_runtime_t *runtime,
                              inktag_status_ready_role_t role,
                              uint32_t now_ms) {
    if (!runtime || role > INKTAG_STATUS_READY_RECOVERY ||
        (runtime->active_mask & READY_PENDING_MASK) != 0u) return;
    runtime->active_mask |= role == INKTAG_STATUS_READY_RECOVERY
        ? READY_RECOVERY_PENDING : READY_APPLICATION_PENDING;
    if (runtime->transient_scene == INKTAG_STATUS_SCENE_STARTUP &&
        before(now_ms, runtime->transient_until)) return;
    runtime->transient_scene = READY_TAIL_SCENE;
    runtime->transient_until = now_ms + INKTAG_STATUS_READY_HALF_MS * 7u;
    runtime->current_scene = NO_SCENE;
    render(runtime, now_ms);
}

void inktag_status_locate_start(inktag_status_runtime_t *runtime,
                                uint32_t now_ms) {
    if (!runtime) return;
    runtime->active_mask |= LOCATE_ACTIVE;
    runtime->current_scene = NO_SCENE;
    render(runtime, now_ms);
}

void inktag_status_locate_stop(inktag_status_runtime_t *runtime,
                               uint32_t now_ms) {
    if (!runtime) return;
    runtime->active_mask &= (uint16_t)~LOCATE_ACTIVE;
    runtime->current_scene = NO_SCENE;
    render(runtime, now_ms);
}

bool inktag_status_locate_active(const inktag_status_runtime_t *runtime) {
    return runtime && (runtime->active_mask & LOCATE_ACTIVE) != 0u;
}

bool inktag_status_needs_periodic_tick(const inktag_status_runtime_t *runtime,
                                       uint32_t now_ms) {
    uint8_t transient;
    uint8_t selected = NO_SCENE;
    bool selected_is_transient = false;
    uint8_t value;
    uint8_t effect;
    uint32_t elapsed;

    if (!runtime) return false;
    /* The timer owns the exact 72-second locator stop boundary,
     * including boards with no LED channels. */
    if (inktag_status_locate_active(runtime)) return true;
    if (runtime->config.policy == INKTAG_STATUS_POLICY_OFF ||
        runtime->available_colors == 0u) return false;

    transient = runtime->transient_scene;
    if (transient != NO_SCENE && !before(now_ms, runtime->transient_until)) {
        if (transient == INKTAG_STATUS_SCENE_STARTUP &&
            (runtime->active_mask & READY_PENDING_MASK) != 0u) {
            /* One service turn is still required to enter the queued role
             * tail; the query itself remains read-only. */
            return true;
        }
        transient = NO_SCENE;
    }

    selected = priority_scene(runtime, transient);
    selected_is_transient = selected == transient;
    if (selected == NO_SCENE) return false;

    value = selected == READY_TAIL_SCENE
        ? INKTAG_STATUS_SCENE_VALUE(ready_tail_color(runtime),
                                    INKTAG_STATUS_EFFECT_FLASH_THREE)
        : scene_value(runtime, selected);
    if (map_color(INKTAG_STATUS_SCENE_COLOR(value),
                  runtime->available_colors) == 0u) return false;
    effect = INKTAG_STATUS_SCENE_EFFECT(value);
    if (effect == INKTAG_STATUS_EFFECT_OFF) return false;
    if (selected_is_transient) return true;
    if (effect == INKTAG_STATUS_EFFECT_SOLID) return false;
    if (effect == INKTAG_STATUS_EFFECT_SLOW_FLASH ||
        effect == INKTAG_STATUS_EFFECT_FAST_FLASH ||
        effect == INKTAG_STATUS_EFFECT_SEQUENCE_SLOW) return true;

    elapsed = now_ms - runtime->scene_started_at;
    return effect >= INKTAG_STATUS_EFFECT_FLASH_ONCE &&
           effect <= INKTAG_STATUS_EFFECT_FLASH_THREE &&
           elapsed < FAST_FLASH_HALF_MS * 2u *
               (effect - INKTAG_STATUS_EFFECT_FLASH_ONCE + 1u);
}

void inktag_status_tick(inktag_status_runtime_t *runtime, uint32_t now_ms) {
    render(runtime, now_ms);
}
