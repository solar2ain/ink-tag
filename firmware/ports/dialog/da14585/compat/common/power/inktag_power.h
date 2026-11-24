#ifndef INKTAG_POWER_H
#define INKTAG_POWER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INKTAG_POWER_UNKNOWN = 0,
    INKTAG_POWER_NORMAL = 1,
    INKTAG_POWER_WARNING = 2,
    INKTAG_POWER_CRITICAL = 3,
} inktag_power_state_t;

typedef enum {
    INKTAG_POWER_OPERATION_DISPLAY = 0,
    INKTAG_POWER_OPERATION_FLASH = 1,
    INKTAG_POWER_OPERATION_OTA = 2,
} inktag_power_operation_t;

typedef struct {
    uint16_t warning_mv;
    uint16_t critical_mv;
    uint16_t display_min_mv;
    uint16_t flash_min_mv;
    uint16_t ota_min_mv;
    uint16_t recovery_hysteresis_mv;
} inktag_power_thresholds_t;

/* Product policy is evaluated from a stable idle or transaction-entry sample.
 * Samples taken while a high-load operation is already running are retained
 * separately for sag/minimum telemetry and hardware-safety decisions; they
 * must never overwrite the policy sample and retroactively trigger a page. */
typedef struct {
    uint16_t policy_mv;
    uint16_t operation_mv;
    uint16_t operation_min_mv;
    bool policy_valid;
    bool operation_valid;
    bool operation_min_valid;
    bool operation_active;
} inktag_power_sample_state_t;

static inline void inktag_power_sample_state_reset(
    inktag_power_sample_state_t *state) {
    if (!state) return;
    state->policy_mv = 0u;
    state->operation_mv = 0u;
    state->operation_min_mv = 0u;
    state->policy_valid = false;
    state->operation_valid = false;
    state->operation_min_valid = false;
    state->operation_active = false;
}

static inline void inktag_power_policy_sample_record(
    inktag_power_sample_state_t *state, uint16_t voltage_mv, bool valid) {
    if (!state || state->operation_active) return;
    state->policy_mv = valid ? voltage_mv : 0u;
    state->policy_valid = valid;
}

static inline void inktag_power_operation_sample_begin(
    inktag_power_sample_state_t *state) {
    if (!state) return;
    state->operation_active = true;
    state->operation_mv = state->policy_valid ? state->policy_mv : 0u;
    state->operation_min_mv = state->operation_mv;
    state->operation_valid = state->policy_valid;
    state->operation_min_valid = state->policy_valid;
}

static inline void inktag_power_operation_sample_end(
    inktag_power_sample_state_t *state) {
    if (!state) return;
    state->operation_active = false;
}

static inline void inktag_power_operation_sample_record(
    inktag_power_sample_state_t *state, uint16_t voltage_mv, bool valid) {
    if (!state || !state->operation_active) return;
    state->operation_mv = valid ? voltage_mv : 0u;
    state->operation_valid = valid;
    if (valid && (!state->operation_min_valid ||
                  voltage_mv < state->operation_min_mv)) {
        state->operation_min_mv = voltage_mv;
        state->operation_min_valid = true;
    }
}

/* Product-wide 0.2 power policy. These are operational policy values, not
 * battery-cell ratings or proof that an exact board's ADC is qualified.
 * A display threshold of zero explicitly means that a valid voltage sample
 * never blocks EPD refresh. The strict entry rejects invalid samples; the
 * separately named human-directed best-effort pilot treats them as unavailable
 * protection, never as NORMAL telemetry. Valid low-voltage thresholds are
 * identical. Exact-board threshold overrides still require electrical evidence. */
#define INKTAG_POWER_POLICY_WARNING_MV 2400u
#define INKTAG_POWER_POLICY_CRITICAL_MV 2000u
#define INKTAG_POWER_POLICY_DISPLAY_MIN_MV 0u
#define INKTAG_POWER_POLICY_FLASH_MIN_MV 2400u
#define INKTAG_POWER_POLICY_OTA_MIN_MV 2600u
#define INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV 150u

#define INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER                         \
    {                                                                      \
        .warning_mv = INKTAG_POWER_POLICY_WARNING_MV,                      \
        .critical_mv = INKTAG_POWER_POLICY_CRITICAL_MV,                    \
        .display_min_mv = INKTAG_POWER_POLICY_DISPLAY_MIN_MV,              \
        .flash_min_mv = INKTAG_POWER_POLICY_FLASH_MIN_MV,                  \
        .ota_min_mv = INKTAG_POWER_POLICY_OTA_MIN_MV,                      \
        .recovery_hysteresis_mv =                                          \
            INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV,                    \
    }

static inline void inktag_power_policy_thresholds(
    inktag_power_thresholds_t *thresholds) {
    if (!thresholds) return;
    thresholds->warning_mv = INKTAG_POWER_POLICY_WARNING_MV;
    thresholds->critical_mv = INKTAG_POWER_POLICY_CRITICAL_MV;
    thresholds->display_min_mv = INKTAG_POWER_POLICY_DISPLAY_MIN_MV;
    thresholds->flash_min_mv = INKTAG_POWER_POLICY_FLASH_MIN_MV;
    thresholds->ota_min_mv = INKTAG_POWER_POLICY_OTA_MIN_MV;
    thresholds->recovery_hysteresis_mv =
        INKTAG_POWER_POLICY_RECOVERY_HYSTERESIS_MV;
}

inktag_power_state_t inktag_power_classify(
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds);

bool inktag_power_operation_allowed(
    inktag_power_operation_t operation,
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds);

/* Human-directed best-effort voltage policy pilot. Unknown remains UNKNOWN
 * in telemetry, but does not by itself deny an otherwise supported operation.
 * Known valid low voltage retains exactly the same thresholds. Invalid
 * operation/threshold inputs still refuse; this never authorizes hardware,
 * authentication, Flash ranges, signatures or integrity on its own. */
bool inktag_power_operation_allowed_if_voltage_available(
    inktag_power_operation_t operation,
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds);

bool inktag_power_warning_cleared(
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds);

#endif
