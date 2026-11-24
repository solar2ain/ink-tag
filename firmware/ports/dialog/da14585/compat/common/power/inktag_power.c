#include "inktag_power.h"

static uint16_t operation_threshold(
    inktag_power_operation_t operation,
    const inktag_power_thresholds_t *thresholds) {
    if (!thresholds) return UINT16_MAX;
    switch (operation) {
        case INKTAG_POWER_OPERATION_DISPLAY:
            return thresholds->display_min_mv;
        case INKTAG_POWER_OPERATION_FLASH:
            return thresholds->flash_min_mv;
        case INKTAG_POWER_OPERATION_OTA:
            return thresholds->ota_min_mv;
        default:
            return UINT16_MAX;
    }
}

inktag_power_state_t inktag_power_classify(
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds) {
    if (!voltage_valid || !thresholds) return INKTAG_POWER_UNKNOWN;
    if (voltage_mv < thresholds->critical_mv) return INKTAG_POWER_CRITICAL;
    if (voltage_mv < thresholds->warning_mv) return INKTAG_POWER_WARNING;
    return INKTAG_POWER_NORMAL;
}

bool inktag_power_operation_allowed(
    inktag_power_operation_t operation,
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds) {
    if (!voltage_valid || !thresholds) return false;
    const uint16_t threshold = operation_threshold(operation, thresholds);
    if (threshold == UINT16_MAX) return false;
    return voltage_mv >= threshold;
}

bool inktag_power_warning_cleared(
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds) {
    if (!voltage_valid || !thresholds) return false;
    return voltage_mv >=
        (uint32_t)thresholds->warning_mv + thresholds->recovery_hysteresis_mv;
}

bool inktag_power_operation_allowed_if_voltage_available(
    inktag_power_operation_t operation,
    uint16_t voltage_mv,
    bool voltage_valid,
    const inktag_power_thresholds_t *thresholds) {
    if (!voltage_valid)
        return operation_threshold(operation, thresholds) != UINT16_MAX;
    return inktag_power_operation_allowed(operation, voltage_mv,
                                          voltage_valid, thresholds);
}
