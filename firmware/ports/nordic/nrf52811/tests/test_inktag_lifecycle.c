#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_lifecycle.h"

static inktag_lifecycle_capabilities_t all_capabilities(void) {
    const inktag_lifecycle_capabilities_t value = {
        .reset = true,
        .timed_sleep = true,
        .factory_reset = true,
        .ota_recovery = true,
        .reset_settings = true,
        .clear_user_data = true,
        .sleep_wake_mask = INKTAG_SLEEP_WAKE_TIMER,
        .minimum_sleep_seconds = 5u,
        .maximum_sleep_seconds = 3600u,
    };
    return value;
}

static inktag_status_v2_t plan(
    uint8_t flags, const uint8_t *payload, uint16_t length,
    const inktag_lifecycle_capabilities_t *capabilities,
    inktag_lifecycle_plan_t *result) {
    return inktag_lifecycle_plan_request(
        flags, payload, length, capabilities, result);
}

static void test_simple_actions_and_confirmation(void) {
    inktag_lifecycle_capabilities_t capabilities = all_capabilities();
    inktag_lifecycle_plan_t result;
    const uint8_t reset[] = {INKTAG_SYSTEM_ACTION_RESET};
    const uint8_t settings[] = {INKTAG_SYSTEM_ACTION_RESET_SETTINGS};
    const uint8_t clear[] = {INKTAG_SYSTEM_ACTION_CLEAR_USER_DATA};
    const uint8_t factory[] = {INKTAG_SYSTEM_ACTION_FACTORY_RESET};
    assert(plan(0u, reset, sizeof(reset), &capabilities, &result) ==
           INKTAG_STATUS_OK);
    assert(result.effect == INKTAG_LIFECYCLE_EFFECT_RESET);
    assert(plan(0u, settings, sizeof(settings), &capabilities, &result) ==
           INKTAG_STATUS_DENIED);
    assert(plan(INKTAG_REQUEST_FLAG_CONFIRM, settings, sizeof(settings),
                &capabilities, &result) == INKTAG_STATUS_OK);
    assert(result.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS);
    assert(plan(INKTAG_REQUEST_FLAG_CONFIRM, clear, sizeof(clear),
                &capabilities, &result) == INKTAG_STATUS_OK);
    assert(result.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA);
    assert(plan(INKTAG_REQUEST_FLAG_CONFIRM, factory, sizeof(factory),
                &capabilities, &result) == INKTAG_STATUS_UNSUPPORTED);
    assert(result.effect == INKTAG_LIFECYCLE_EFFECT_NONE);
    assert(plan(0u, factory, sizeof(factory), &capabilities, &result) ==
           INKTAG_STATUS_UNSUPPORTED);
}

static void test_sleep_bounds_and_wake_qualification(void) {
    inktag_lifecycle_capabilities_t capabilities = all_capabilities();
    inktag_lifecycle_plan_t result;
    uint8_t sleep[] = {
        INKTAG_SYSTEM_ACTION_SLEEP, 1u, INKTAG_SLEEP_WAKE_TIMER,
        5u, 0u, 0u, 0u,
    };
    assert(plan(0u, sleep, sizeof(sleep), &capabilities, &result) ==
           INKTAG_STATUS_OK);
    assert(result.effect == INKTAG_LIFECYCLE_EFFECT_TIMED_SLEEP);
    assert(result.sleep_seconds == 5u);
    sleep[2] = INKTAG_SLEEP_WAKE_BUTTON;
    assert(plan(0u, sleep, sizeof(sleep), &capabilities, &result) ==
           INKTAG_STATUS_UNSUPPORTED);
    sleep[2] = INKTAG_SLEEP_WAKE_TIMER;
    sleep[3] = 4u;
    assert(plan(0u, sleep, sizeof(sleep), &capabilities, &result) ==
           INKTAG_STATUS_INVALID_REQUEST);
    capabilities.timed_sleep = false;
    assert(plan(0u, sleep, sizeof(sleep), &capabilities, &result) ==
           INKTAG_STATUS_UNSUPPORTED);
}

static void test_invalid_and_unavailable_actions(void) {
    inktag_lifecycle_capabilities_t capabilities = all_capabilities();
    inktag_lifecycle_plan_t result;
    const uint8_t unknown[] = {0xffu};
    const uint8_t reset_extra[] = {INKTAG_SYSTEM_ACTION_RESET, 0u};
    const uint8_t ota[] = {INKTAG_SYSTEM_ACTION_OTA_RECOVERY};
    assert(plan(0u, NULL, 0u, &capabilities, &result) ==
           INKTAG_STATUS_INVALID_REQUEST);
    assert(plan(0u, unknown, sizeof(unknown), &capabilities, &result) ==
           INKTAG_STATUS_DENIED);
    assert(plan(0u, reset_extra, sizeof(reset_extra), &capabilities, &result) ==
           INKTAG_STATUS_INVALID_REQUEST);
    capabilities.ota_recovery = false;
    assert(plan(0u, ota, sizeof(ota), &capabilities, &result) ==
           INKTAG_STATUS_UNSUPPORTED);
}

int main(void) {
    test_simple_actions_and_confirmation();
    test_sleep_bounds_and_wake_qualification();
    test_invalid_and_unavailable_actions();
    puts("Lifecycle policy tests passed.");
    return 0;
}
