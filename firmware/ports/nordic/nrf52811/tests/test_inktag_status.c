#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inktag_status.h"
#include "inktag_temperature_wire.h"

static uint8_t s_output;

static void apply(void *context, uint8_t color) {
    (void)context;
    s_output = color;
}

static void test_default_contract_and_wire_codec(void) {
    inktag_status_config_t config;
    inktag_status_config_t decoded;
    uint8_t wire[INKTAG_STATUS_WIRE_SIZE];
    inktag_status_config_default(&config);
    assert(inktag_status_config_valid(&config));
    assert(config.policy == INKTAG_STATUS_POLICY_ALL);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_STARTUP)) ==
           INKTAG_STATUS_COLOR_WHITE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_STARTUP)) ==
           INKTAG_STATUS_EFFECT_SEQUENCE_SLOW);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_CONNECTED)) ==
           INKTAG_STATUS_COLOR_GREEN);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_CONNECTED)) ==
           INKTAG_STATUS_EFFECT_FLASH_TWICE);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_TRANSFER)) ==
           INKTAG_STATUS_COLOR_BLUE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_TRANSFER)) ==
           INKTAG_STATUS_EFFECT_SLOW_FLASH);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_REFRESH)) ==
           INKTAG_STATUS_COLOR_BLUE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_REFRESH)) ==
           INKTAG_STATUS_EFFECT_FAST_FLASH);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_OTA)) ==
           INKTAG_STATUS_COLOR_BLUE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_OTA)) ==
           INKTAG_STATUS_EFFECT_SLOW_FLASH);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_OTA_INSTALL)) ==
           INKTAG_STATUS_COLOR_BLUE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_OTA_INSTALL)) ==
           INKTAG_STATUS_EFFECT_FAST_FLASH);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_SUCCESS)) ==
           INKTAG_STATUS_COLOR_GREEN);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_ERROR)) ==
           INKTAG_STATUS_COLOR_RED);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_FIND)) ==
           INKTAG_STATUS_COLOR_WHITE);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_FIND)) ==
           INKTAG_STATUS_EFFECT_SEQUENCE_SLOW);
    assert(INKTAG_STATUS_SCENE_COLOR(inktag_status_default_scene(INKTAG_STATUS_SCENE_ADVERTISING)) ==
           INKTAG_STATUS_COLOR_OFF);
    assert(INKTAG_STATUS_SCENE_EFFECT(inktag_status_default_scene(INKTAG_STATUS_SCENE_ADVERTISING)) ==
           INKTAG_STATUS_EFFECT_OFF);
    config.policy = INKTAG_STATUS_POLICY_CRITICAL;
    assert(inktag_status_config_encode(&config, wire));
    memset(&decoded, 0, sizeof(decoded));
    assert(inktag_status_config_decode(&decoded, wire, sizeof(wire)));
    assert(memcmp(&decoded, &config, sizeof(config)) == 0);
    wire[1] = 2u;
    assert(!inktag_status_config_decode(&decoded, wire, sizeof(wire)));
    {
        uint8_t legacy[15] = {2u, 1u};
        assert(!inktag_status_config_decode(&decoded, legacy, sizeof(legacy)));
    }
}

static void test_priority_and_flash_timing(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE, apply, NULL, 0u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_CONNECTED, true, 10u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_REFRESH, true, 20u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 140u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_TRANSFER, true, 150u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 389u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 390u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 630u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_ERROR, true, 660u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
}

static void test_active_transfer_preempts_connected_transient(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_CONNECTED, 10u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);

    /* A client can begin its transfer while the two-flash connection pattern
     * is still live. The transfer must turn blue synchronously even if BLE or
     * Flash work delays the next periodic status tick. */
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_TRANSFER, true, 20u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
}

static void test_sequential_startup_and_channel_reuse(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE, apply, NULL, 0u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_STARTUP, 0u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 359u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 360u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 719u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 720u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 1079u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 1080u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);

    inktag_status_set_available_colors(&runtime, INKTAG_STATUS_COLOR_RED |
                                       INKTAG_STATUS_COLOR_BLUE, 4000u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_FIND, true, 4000u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 4239u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 4240u);
    /* The sequence advances over physically available channels only. */
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);

    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_GREEN,
                       apply, NULL, 5000u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_STARTUP, 5000u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 5359u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 5360u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
}

static void test_startup_drops_overlapping_transient_feedback(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    uint32_t startup_until;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_STARTUP, 0u);
    startup_until = runtime.transient_until;

    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_CONNECTED, 100u);
    assert(runtime.transient_scene == INKTAG_STATUS_SCENE_STARTUP);
    assert(runtime.transient_until == startup_until);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 360u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 720u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);

    /* Even higher-priority transient feedback is stale after the 1.08-second
     * boot sequence and is deliberately not queued. */
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_ERROR, 800u);
    assert(runtime.transient_scene == INKTAG_STATUS_SCENE_STARTUP);
    assert(runtime.transient_until == startup_until);
    inktag_status_tick(&runtime, 1080u);
    assert(runtime.transient_scene == UINT8_MAX);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);

    /* Once STARTUP has expired, the next connection indication is accepted. */
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_CONNECTED, 1081u);
    assert(runtime.transient_scene == INKTAG_STATUS_SCENE_CONNECTED);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
}

static void test_boot_role_ready_tail(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_STARTUP, 0u);
    inktag_status_role_ready(&runtime, INKTAG_STATUS_READY_APPLICATION, 100u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, 1079u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 1080u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 1199u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 1200u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 1320u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 1440u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 1920u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);

    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 2000u);
    inktag_status_role_ready(&runtime, INKTAG_STATUS_READY_RECOVERY, 2001u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 2120u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 2121u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
}

static void assert_active_flash_boundaries(inktag_status_scene_t scene,
                                           uint32_t half_ms) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_set_active(&runtime, scene, true, 0u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, half_ms - 1u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, half_ms);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, half_ms * 2u - 1u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, half_ms * 2u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
}

static void test_operation_flash_boundaries(void) {
    assert_active_flash_boundaries(INKTAG_STATUS_SCENE_TRANSFER, 240u);
    assert_active_flash_boundaries(INKTAG_STATUS_SCENE_OTA, 240u);
    assert_active_flash_boundaries(INKTAG_STATUS_SCENE_REFRESH, 120u);
    assert_active_flash_boundaries(INKTAG_STATUS_SCENE_OTA_INSTALL, 120u);
}

static void test_critical_policy_only_keeps_startup_find_and_ota(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    config.policy = INKTAG_STATUS_POLICY_CRITICAL;
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_CONNECTED, true, 1u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_ERROR, true, 2u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_OTA, true, 3u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_FIND, true, 4u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
}

static void test_ota_scene_transition_and_success(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 0u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_OTA, true, 0u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 240u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_OTA_INSTALL, true, 300u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 420u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    /* Removing the higher-priority install scene restarts the resumed
     * download scene at phase zero instead of resuming half-way through. */
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_OTA_INSTALL, false, 421u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, 661u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_OTA, false, 662u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_SUCCESS, 662u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 782u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, 902u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 1142u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, 1382u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
}

static void test_monotonic_wrap_timing(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    const uint32_t start = UINT32_MAX - 127u;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, start);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_TRANSFER, true, start);
    inktag_status_tick(&runtime, start + 239u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, start + 240u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_tick(&runtime, start + 480u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);

    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_TRANSFER, false,
                             start + 481u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_SUCCESS, start + 481u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, start + 1201u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
}

static void test_mono_and_off_policy(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_GREEN, apply, NULL, 0u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_ERROR, true, 0u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    config.policy = INKTAG_STATUS_POLICY_OFF;
    inktag_status_set_config(&runtime, &config, 1u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
}

static void test_bounded_free_device_locate_sequence(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    const uint32_t start = UINT32_MAX - 47u;
    inktag_status_config_default(&config);
    config.policy = INKTAG_STATUS_POLICY_OFF;
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, start);

    inktag_status_locate_start(&runtime, start);
    assert(inktag_status_locate_active(&runtime));
    assert(inktag_status_needs_periodic_tick(&runtime, start));
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, start + 239u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, start + 240u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, start + 480u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, start + 720u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime,
                       start + INKTAG_STATUS_LOCATE_DURATION_MS - 1u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    assert(inktag_status_needs_periodic_tick(
        &runtime, start + INKTAG_STATUS_LOCATE_DURATION_MS - 1u));
    inktag_status_tick(&runtime,
                       start + INKTAG_STATUS_LOCATE_DURATION_MS);
    assert(!inktag_status_locate_active(&runtime));
    assert(!inktag_status_needs_periodic_tick(
        &runtime, start + INKTAG_STATUS_LOCATE_DURATION_MS));
    assert(s_output == INKTAG_STATUS_COLOR_OFF);

    inktag_status_locate_start(&runtime, 100u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_locate_stop(&runtime, 101u);
    assert(!inktag_status_locate_active(&runtime));
    assert(!inktag_status_needs_periodic_tick(&runtime, 101u));
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
    inktag_status_locate_stop(&runtime, 102u);
    assert(s_output == INKTAG_STATUS_COLOR_OFF);
}

static void test_periodic_tick_contract(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;

    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, 0u, apply, NULL, 0u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_REFRESH, true, 0u);
    assert(!inktag_status_needs_periodic_tick(&runtime, 0u));

    inktag_status_set_available_colors(&runtime, INKTAG_STATUS_COLOR_WHITE, 1u);
    assert(inktag_status_needs_periodic_tick(&runtime, 1u));
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_REFRESH, false, 2u);
    assert(!inktag_status_needs_periodic_tick(&runtime, 2u));

    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_CONNECTED, 10u);
    assert(inktag_status_needs_periodic_tick(&runtime, 489u));
    assert(!inktag_status_needs_periodic_tick(&runtime, 490u));

    config.policy = INKTAG_STATUS_POLICY_CRITICAL;
    inktag_status_set_config(&runtime, &config, 500u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_CONNECTED, true, 500u);
    assert(!inktag_status_needs_periodic_tick(&runtime, 500u));

    config.policy = INKTAG_STATUS_POLICY_OFF;
    inktag_status_set_config(&runtime, &config, 501u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_REFRESH, true, 501u);
    assert(!inktag_status_needs_periodic_tick(&runtime, 501u));

    inktag_status_config_default(&config);
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, 1000u);
    inktag_status_trigger(&runtime, INKTAG_STATUS_SCENE_STARTUP, 1000u);
    inktag_status_role_ready(&runtime, INKTAG_STATUS_READY_APPLICATION, 1001u);
    assert(inktag_status_needs_periodic_tick(&runtime, 2079u));
    assert(inktag_status_needs_periodic_tick(&runtime, 2080u));
    inktag_status_tick(&runtime, 2080u);
    assert(inktag_status_needs_periodic_tick(&runtime, 2919u));
    inktag_status_tick(&runtime, 2920u);
    assert(!inktag_status_needs_periodic_tick(&runtime, 2920u));
}

static void test_bounded_local_locator(void) {
    inktag_status_config_t config;
    inktag_status_runtime_t runtime;
    const uint32_t start = UINT32_MAX - 1000u;

    inktag_status_config_default(&config);
    config.policy = INKTAG_STATUS_POLICY_OFF;
    inktag_status_init(&runtime, &config, INKTAG_STATUS_COLOR_WHITE,
                       apply, NULL, start);
    inktag_status_locate_start(&runtime, start);
    assert(INKTAG_STATUS_LOCATE_DURATION_MS == 72000u);
    assert(inktag_status_locate_active(&runtime));
    assert(inktag_status_needs_periodic_tick(&runtime, start));
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, start + 239u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime, start + 240u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_tick(&runtime, start + 480u);
    assert(s_output == INKTAG_STATUS_COLOR_BLUE);
    inktag_status_tick(&runtime, start + 720u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_tick(&runtime,
                       start + INKTAG_STATUS_LOCATE_DURATION_MS - 1u);
    assert(inktag_status_locate_active(&runtime));
    inktag_status_tick(&runtime,
                       start + INKTAG_STATUS_LOCATE_DURATION_MS);
    assert(!inktag_status_locate_active(&runtime));
    assert(!inktag_status_needs_periodic_tick(
        &runtime, start + INKTAG_STATUS_LOCATE_DURATION_MS));
    assert(s_output == INKTAG_STATUS_COLOR_OFF);

    /* Stop is idempotent and immediately restores the selected ordinary
     * scene from phase zero. */
    config.policy = INKTAG_STATUS_POLICY_ALL;
    inktag_status_set_config(&runtime, &config, 100u);
    inktag_status_set_active(&runtime, INKTAG_STATUS_SCENE_CONNECTED,
                             true, 100u);
    inktag_status_locate_start(&runtime, 101u);
    assert(s_output == INKTAG_STATUS_COLOR_RED);
    inktag_status_locate_stop(&runtime, 102u);
    assert(!inktag_status_locate_active(&runtime));
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);
    inktag_status_locate_stop(&runtime, 103u);
    assert(s_output == INKTAG_STATUS_COLOR_GREEN);

    /* A board with no LED channels still expires its active locator
     * state at the common boundary. */
    inktag_status_init(&runtime, &config, 0u, apply, NULL, 200u);
    inktag_status_locate_start(&runtime, 200u);
    assert(inktag_status_needs_periodic_tick(&runtime, 200u));
    inktag_status_tick(&runtime, 200u + INKTAG_STATUS_LOCATE_DURATION_MS);
    assert(!inktag_status_locate_active(&runtime));
}

static void test_temperature_selection(void) {
    int8_t temperature = 99;
    assert(!inktag_temperature_select_lower(false, -20 * 16,
                                             false, 26 * 16, &temperature));
    assert(temperature == 99);
    assert(inktag_temperature_select_lower(true, 23 * 16,
                                            false, -40 * 16, &temperature));
    assert(temperature == 23);
    assert(inktag_temperature_select_lower(false, 80 * 16,
                                            true, 26 * 16 + 8, &temperature));
    assert(temperature == 26);
    assert(inktag_temperature_select_lower(true, 21 * 16,
                                            true, 26 * 16 + 8, &temperature));
    assert(temperature == 21);
    assert(inktag_temperature_select_lower(true, 5 * 16,
                                            true, -1 * 16 + 8, &temperature));
    assert(temperature == -1);
    assert(!inktag_temperature_select_lower(true, 128 * 16,
                                             false, 0, &temperature));
    /* Exhaust the source's int16 range, including both signed floor edges.
     * Invalid sources and out-of-range results must leave output untouched. */
    for (int32_t value = INT16_MIN; value <= INT16_MAX; ++value) {
        int32_t whole = value >= 0 ? value / 16 : -((-value + 15) / 16);
        bool expected = whole >= INT8_MIN && whole <= INT8_MAX;
        for (unsigned source = 0; source < 2u; ++source) {
            temperature = 99;
            bool valid = inktag_temperature_select_lower(source == 0u, (int16_t)value,
                source != 0u, (int16_t)value, &temperature);
            assert(valid == expected);
            assert(temperature == (expected ? whole : 99));
        }
        temperature = 99;
        assert(!inktag_temperature_select_lower(false, (int16_t)value,
                                                false, (int16_t)value, &temperature));
        assert(temperature == 99);
    }
}

static void test_temperature_source_wire(void) {
    uint8_t wire[INKTAG_TEMPERATURE_WIRE_SIZE];
    inktag_temperature_wire_encode(wire, true, -40, true, 408);
    const uint8_t expected[] = {'I', 'T', 'T', 1u, 3u,
                                0xD8u, 0xFFu, 0x98u, 0x01u};
    assert(memcmp(wire, expected, sizeof(expected)) == 0);
    inktag_temperature_wire_encode(wire, false, -40, true, 408);
    assert(wire[4] == INKTAG_TEMPERATURE_WIRE_PANEL_VALID);
    assert(wire[5] == 0u && wire[6] == 0u);
}

int main(void) {
    test_default_contract_and_wire_codec();
    test_priority_and_flash_timing();
    test_active_transfer_preempts_connected_transient();
    test_sequential_startup_and_channel_reuse();
    test_startup_drops_overlapping_transient_feedback();
    test_boot_role_ready_tail();
    test_operation_flash_boundaries();
    test_critical_policy_only_keeps_startup_find_and_ota();
    test_ota_scene_transition_and_success();
    test_monotonic_wrap_timing();
    test_mono_and_off_policy();
    test_bounded_free_device_locate_sequence();
    test_periodic_tick_contract();
    test_bounded_local_locator();
    test_temperature_selection();
    test_temperature_source_wire();
    puts("inktag status tests passed");
    return 0;
}
