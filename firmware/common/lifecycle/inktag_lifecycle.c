#include "inktag_lifecycle.h"

#include <stddef.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static inktag_status_v2_t require_simple_action(
    bool supported, uint8_t flags, uint16_t payload_length,
    bool confirmation_required, inktag_lifecycle_effect_t effect,
    uint8_t action, inktag_lifecycle_plan_t *plan) {
    if (payload_length != 1u) return INKTAG_STATUS_INVALID_REQUEST;
    if (!supported) return INKTAG_STATUS_UNSUPPORTED;
    if (confirmation_required &&
        (flags & INKTAG_REQUEST_FLAG_CONFIRM) == 0u) {
        return INKTAG_STATUS_DENIED;
    }
    plan->effect = effect;
    plan->action = action;
    return INKTAG_STATUS_OK;
}

inktag_status_v2_t inktag_lifecycle_plan_request(
    uint8_t flags, const uint8_t *payload, uint16_t payload_length,
    const inktag_lifecycle_capabilities_t *capabilities,
    inktag_lifecycle_plan_t *plan) {
    uint8_t action;
    if (payload == NULL || payload_length == 0u || capabilities == NULL ||
        plan == NULL) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    memset(plan, 0, sizeof(*plan));
    action = payload[0];
    switch (action) {
        case INKTAG_SYSTEM_ACTION_RESET:
            return require_simple_action(
                capabilities->reset, flags, payload_length, false,
                INKTAG_LIFECYCLE_EFFECT_RESET, action, plan);
        case INKTAG_SYSTEM_ACTION_SLEEP: {
            uint8_t wake_mask;
            uint32_t seconds;
            if (!capabilities->timed_sleep) return INKTAG_STATUS_UNSUPPORTED;
            if (payload_length != 7u || payload[1] != 1u) {
                return INKTAG_STATUS_INVALID_REQUEST;
            }
            wake_mask = payload[2];
            if (wake_mask == 0u ||
                (wake_mask & (uint8_t)~(INKTAG_SLEEP_WAKE_TIMER |
                                        INKTAG_SLEEP_WAKE_BUTTON)) != 0u) {
                return INKTAG_STATUS_INVALID_REQUEST;
            }
            if ((wake_mask & (uint8_t)~capabilities->sleep_wake_mask) != 0u) {
                return INKTAG_STATUS_UNSUPPORTED;
            }
            seconds = read_u32_le(&payload[3]);
            if (seconds < capabilities->minimum_sleep_seconds ||
                seconds > capabilities->maximum_sleep_seconds) {
                return INKTAG_STATUS_INVALID_REQUEST;
            }
            plan->effect = INKTAG_LIFECYCLE_EFFECT_TIMED_SLEEP;
            plan->action = action;
            plan->wake_mask = wake_mask;
            plan->sleep_seconds = seconds;
            return INKTAG_STATUS_OK;
        }
        case INKTAG_SYSTEM_ACTION_FACTORY_RESET:
            /* Retired wire ID: never repurpose or start a destructive backend. */
            return INKTAG_STATUS_UNSUPPORTED;
        case INKTAG_SYSTEM_ACTION_OTA_RECOVERY:
            return require_simple_action(
                capabilities->ota_recovery, flags, payload_length, false,
                INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY, action, plan);
        case INKTAG_SYSTEM_ACTION_RESET_SETTINGS:
            return require_simple_action(
                capabilities->reset_settings, flags, payload_length, true,
                INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS, action, plan);
        case INKTAG_SYSTEM_ACTION_CLEAR_USER_DATA:
            return require_simple_action(
                capabilities->clear_user_data, flags, payload_length, true,
                INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA, action, plan);
        default:
            return INKTAG_STATUS_DENIED;
    }
}
