#ifndef INKTAG_LIFECYCLE_H
#define INKTAG_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_protocol_v2.h"

typedef struct {
    bool reset;
    bool timed_sleep;
    bool factory_reset;
    bool ota_recovery;
    bool reset_settings;
    bool clear_user_data;
    uint8_t sleep_wake_mask;
    uint32_t minimum_sleep_seconds;
    uint32_t maximum_sleep_seconds;
} inktag_lifecycle_capabilities_t;

typedef enum {
    INKTAG_LIFECYCLE_EFFECT_NONE = 0,
    INKTAG_LIFECYCLE_EFFECT_RESET,
    INKTAG_LIFECYCLE_EFFECT_TIMED_SLEEP,
    INKTAG_LIFECYCLE_EFFECT_FACTORY_RESET,
    INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY,
    INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS,
    INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA,
} inktag_lifecycle_effect_t;

typedef struct {
    inktag_lifecycle_effect_t effect;
    uint8_t action;
    uint8_t wake_mask;
    uint32_t sleep_seconds;
} inktag_lifecycle_plan_t;

inktag_status_v2_t inktag_lifecycle_plan_request(
    uint8_t flags, const uint8_t *payload, uint16_t payload_length,
    const inktag_lifecycle_capabilities_t *capabilities,
    inktag_lifecycle_plan_t *plan);

#endif
