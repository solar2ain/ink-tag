#include "main.h"

#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "inktag_soak_telemetry.h"

static RAM volatile bool s_led_resume_pending;
static RAM volatile uint8_t s_idle_suspend_mask;

_attribute_ram_code_ void user_init_normal(void) {
    inktag_board_id_t board;
    extern volatile uint8_t inktag_boot_stage;
    inktag_boot_stage = 0x10u;
    s_led_resume_pending = false;
    random_generator_init();
    if (!inktag_tlsr_recovery_boot()) {
        /* A Recovery journal failure must never fall through to main_loop()
         * with the BLE/app state uninitialized.  Repeated reset requests are
         * intentionally safer than running an unconfirmed image. */
        while (1) start_reboot();
    }
    inktag_boot_stage = 0x11u;
    board = inktag_board_detect();
    {
        const inktag_tlsr_board_profile_t *profile =
            inktag_tlsr_board_profile(board);
        s_idle_suspend_mask = SUSPEND_DISABLE;
        if (profile != NULL) {
            /* One SDK policy for every configured TLSR8359 backend. Keep
             * connected traffic on ordinary Suspend; only idle advertising
             * uses retention after the debug grace. Unknown hardware stays
             * awake instead of guessing its GPIO/storage initialization. */
            s_idle_suspend_mask = SUSPEND_ADV | SUSPEND_CONN |
                                  DEEPSLEEP_RETENTION_ADV;
        }
    }
    inktag_boot_stage = 0x12u;
    inktag_storage_init(board);
    inktag_tlsr_pin_routes_init(board);
    inktag_boot_stage = 0x13u;
    inktag_epd_configure(board);
    inktag_boot_stage = 0x14u;
    inktag_nfc_init();
    inktag_boot_stage = 0x15u;
    inktag_app_init();
    inktag_boot_stage = 0x16u;
    inktag_ble_init();
    inktag_boot_stage = 0x17u;
    inktag_app_start();
    /* app_start runs before the first main_loop() pass and may reconfigure
     * advertising while it restores Find state. Apply the startup hold after
     * all of that work, rather than waiting for the end-of-loop
     * requires_awake check, so the SDK cannot enter retention before the
     * first red phase is serviced. The ordinary loop restores event-aware
     * retention as soon as the bounded transient expires. */
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    inktag_epd_cold_boot_sleep(board);
    inktag_boot_stage = 0x18u;
}

_attribute_ram_code_ void user_init_deepRetn(void) {
    blc_ll_initBasicMCU();
    rf_set_power_level_index(RF_POWER_P3p01dBm);
#if INKTAG_BUILD_PRO
#include "inktag_ble_recover_hidden_pro.inc"
#else
    blc_ll_recoverDeepRetention();
#endif
#if INKTAG_BUILD_PRO
    inktag_nfc_resume();
#endif
    /* app_config.h restores the fixed NFC and inactive LED GPIO state through
     * gpio_init(false). Defer only the currently rendered LED mask until
     * normal Flash code is safe to call from main_loop(). */
    s_led_resume_pending = true;
}

_attribute_ram_code_ void main_loop(void) {
    blt_sdk_main_loop();
    /* Counts cooperative BLE service turns, not transmitted RF packets. */
    inktag_soak_telemetry_radio_maintenance();
    if (s_led_resume_pending) {
        s_led_resume_pending = false;
        inktag_app_resume();
    }
    inktag_app_process();
    inktag_ble_process();
    if (inktag_app_requires_awake() || inktag_ble_requires_awake())
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    else {
        uint8_t mask = s_idle_suspend_mask;
        /* The grace keeps ordinary Suspend, not a 30-second full-speed hold.
         * SWS software reset remains the accepted maintenance entry path. */
        if (inktag_app_debug_grace_active())
            mask &= ~(DEEPSLEEP_RETENTION_ADV | DEEPSLEEP_RETENTION_CONN);
        bls_pm_setSuspendMask(mask);
#if INKTAG_BUILD_PRO
        inktag_ble_hidden_idle();
#endif
    }
}
