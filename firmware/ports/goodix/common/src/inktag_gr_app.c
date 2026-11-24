#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#line 1 "../Src/user/inktag_gr_app.c"
#include "inktag_gr_app.h"
#if INKTAG_BUILD_PRO
#include "inktag_gr_advertising_pro.h"
#endif

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "app_error.h"
#include "app_io.h"
#include "app_log.h"
#include "app_pwr_mgmt.h"
#include "app_timer.h"
#include "ble_time.h"
#include "grx_sys.h"
#include "gr_plat.h"
#include "gr_soc.h"
#include "inktag_gr_external_flash.h"
#include "inktag_gr_activation.h"
#include "inktag_code_guard.h"
#include "inktag_gr_features.h"
#include "inktag_gr_panel_diagnostic.h"
#include "inktag_gr_dfu_diagnostics.h"
#include "inktag_gr_device_info_qr.h"
#include "inktag_gr_image_store.h"
#include "inktag_gr_health.h"
#include "inktag_gr_adc.h"
#if INKTAG_BUILD_PRO
#include "inktag_gr_findmy_pro.h"
#endif
#include "inktag_gr_gui_assets.h"
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
static void gr_clock_partial_invalidate(void);
#if defined(INKTAG_GR_CLOCK_PARTIAL_SSD1683) && INKTAG_GR_CLOCK_PARTIAL_SSD1683
static void gr_clock_partial_observe_panel(void);
#endif
#endif
#include "inktag_gr_settings_store.h"
#include "inktag_gr_screen.h"
#include "inktag_gr_nvds_guard.h"
#include "inktag_gr_ble_address.h"
#include "inktag_gr_board_identity.h"
#include "inktag_gr_factory_seed.h"
#include "inktag_ble_power.h"
#if defined(INKTAG_GOODIX_OTA)
#include "dfu_port.h"
#include "inktag_gr_swap.h"
#include "otas.h"
#endif
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_hal_conf.h"
#include "gr55xx_pwr.h"
#include "gr55xx_sys.h"
#else
#include "gr533x_pwr.h"
#include "gr533x_hal_conf.h"
#endif
#include "inktag_gr_service.h"

void inktag_gr_epd_spi_diag(uint8_t *hardware, uint16_t *init_status,
                            uint32_t *error_count, uint16_t *last_status,
                            uint32_t *core_hz, uint32_t *baud_divider);
bool inktag_gr_epd_panel_temperature_raw(uint8_t *command, uint8_t raw[2],
                                          uint8_t *length);
void inktag_gr_epd_panel_temperature_invalidate(void);

#include "inktag_epd_config.h"
#include "inktag_epd_driver.h"
#include "inktag_epd_temperature.h"
#include "Adafruit_GFX.h"
#include "inktag_gui.h"
#include "inktag_protocol_v2.h"
#include "inktag_version.h"
#include "inktag_gui_device_qr.h"
#include "hal_flash.h"
#include "inktag_access.h"
#include "inktag_device_password.h"
#if INKTAG_BUILD_PRO
#line 75 "../Src/user/inktag_gr_app.c"
#include "inktag_input_simulation.h"
#endif
#line 76 "../Src/user/inktag_gr_app.c"
#include "inktag_brand_overlay.h"
#include "inktag_board.h"
#include "inktag_lifecycle.h"
#include "inktag_page_cycle.h"
#include "inktag_display_settings.h"
#include "inktag_packbits.h"
#include "inktag_protocol_core.h"
#if INKTAG_TRANSFER_LZO1X_ENABLED
#include "inktag_lzo1x.h"
#endif
#include "inktag_cooperative_snapshot.h"
#include "inktag_soak_telemetry.h"
#include "inktag_status.h"
#include "inktag_temperature_wire.h"
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
#include "inktag_gr_aux_io.h"
#endif
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
#include "inktag_gr_pin_routes.h"
#include "inktag_gr_pin_gpio.h"
static void pin_config_quarantine(void);
#endif
#include "inktag_findmy.h"
#include "inktag_elapsed_clock.h"
#include "inktag_clock_rate.h"
#include "inktag_time_checkpoint.h"
#include "inktag_device_display_name.h"
#include "inktag_ble_identity.h"
#if INKTAG_GR_NFC_PACKAGED
/* Compile-time NFC increment, no new state or dispatch layer. */
#line 102 "../Src/user/inktag_gr_app.c"
#include "inktag_gr_nfc_pro.h"
#include "inktag_type2_uri_store.h"
#line 104 "../Src/user/inktag_gr_app.c"
#endif

_Static_assert(INKTAG_GR_HEALTH_WIRE_SIZE == INKTAG_HEALTH_BASE_WIRE_SIZE,
               "Goodix health base wire prefix must remain 56 bytes");

#if defined(INKTAG_GOODIX_GR5515)
#define GOODIX_TARGET_LOG_NAME "GR551x"
#define INKTAG_GPIO_MUX APP_IO_MUX
#else
#define GOODIX_TARGET_LOG_NAME "GR5331"
#define INKTAG_GPIO_MUX APP_IO_MUX_0
#endif

#if defined(INKTAG_GOODIX_GR5515)
static void boot_stage(uint32_t stage);

/* SWD-readable, RAM-only power diagnostics.  Do not log from these callbacks:
 * they execute on the scheduler's sleep path and logging would itself keep the
 * device awake.  The named globals let a debugger distinguish a peripheral
 * veto from a timer-policy fallback or a committed deep-sleep entry. */
volatile uint32_t g_inktag_gr_sleep_check_counts[2];
volatile uint32_t g_inktag_gr_sleep_prepare_count;

static pwr_mgmt_dev_state_t inktag_gr_sleep_check(void) {
    pwr_mgmt_dev_state_t state = pwr_enter_sleep_check();

    if (state == DEVICE_BUSY || state == DEVICE_IDLE) {
        g_inktag_gr_sleep_check_counts[(uint32_t)state]++;
    }
    return state;
}

static void inktag_gr_prepare_for_deep_sleep(void) {
    /* The vendor callback runs only after peripheral checks pass and the
     * scheduler has committed to deep sleep, not for an IDLE/WFE fallback. */
    g_inktag_gr_sleep_prepare_count++;
    inktag_soak_telemetry_sleep_entered();
}

// Keep the SDK platform initialization sequence intact while exposing the
// exact stage reached through the SWD-readable boot marker.
void system_platform_init(void) {
    boot_stage(0x47525030); // GRP0
    vector_table_init();
    boot_stage(0x47525031); // GRP1
    sdk_init();
    boot_stage(0x47525032); // GRP2
    soc_init();
    boot_stage(0x47525033); // GRP3
}
#endif

#define DEVICE_NAME_PREFIX "IKT_GR"
#define INKTAG_ADV_INDEX 0u
#if INKTAG_GR_FIND_BACKEND_AVAILABLE && \
    (INKTAG_BOARD_FIND_QUALIFIED || INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE)
#define INKTAG_EXPERIMENTAL_FINDMY_ENABLED 1
#else
#define INKTAG_EXPERIMENTAL_FINDMY_ENABLED 0
#endif
#define CLOCK_TIMER_INTERVAL_MS 1000
#define BLE_DISCOVERY_GRACE_MS 3000u
#if defined(INKTAG_GOODIX_GR5515)
/* Keep the existing AON app-timer timeline for raw status/safety timing.
 * UTC has its own BLE source and must not inherit these units. */
#define INKTAG_GR_MONOTONIC_TICKS_PER_SECOND 1000u
#define INKTAG_GR_MONOTONIC_COUNTER_MASK UINT32_MAX
#else
#define INKTAG_GR_MONOTONIC_TICKS_PER_SECOND 3200u
#define INKTAG_GR_MONOTONIC_COUNTER_MASK RWIP_MAX_CLOCK_TIME
#endif
#define INKTAG_GR_UTC_TICKS_PER_SECOND 3200u
#define INKTAG_GR_UTC_COUNTER_MASK RWIP_MAX_CLOCK_TIME
#define INKTAG_GR_CHECKPOINT_TICKS_PER_SECOND 1000u
#define INKTAG_GR_CHECKPOINT_COUNTER_MASK UINT32_MAX
#define INKTAG_DEBUG_ATTACH_GRACE_MS 30000u
#define DEVICE_NAME_MAX_LEN 24
/* The timer exists only while a dynamic scene is visible. Sample each 120 ms
 * product boundary three times so synchronous display/SPI work cannot turn a
 * single delayed callback into a visibly uneven edge; idle remains timer-free. */
#define STATUS_LED_TIMER_INTERVAL_MS 40u
#define DIAG_LED_PULSE_MS 120
#define DIAG_LED_DEFAULT_COUNT 3
#define DIAG_LED_TEST_MS 10000u
#define DIAG_SCAN_PULSE_MS 350
#define RX_QUEUE_SLOTS 16

typedef enum {
    EPD_WORK_NONE = 0,
    EPD_WORK_CLEAR,
    EPD_WORK_REFRESH,
    EPD_WORK_GHOST_CLEAN,
    EPD_WORK_GUI,
    EPD_WORK_DIAG_SCAN,
    EPD_WORK_IMAGE_SHOW,
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_1_pro.inc"
#line 202 "../Src/user/inktag_gr_app.c"
#endif
} epd_work_type_t;
typedef enum {
    EPD_WORK_ORIGIN_NONE = 0,
    EPD_WORK_ORIGIN_AUTOMATIC,
    EPD_WORK_ORIGIN_LOCAL_INPUT,
    EPD_WORK_ORIGIN_BLE_CLIENT,
} epd_work_origin_t;
typedef enum {
    FINDMY_MUX_MODE_INKTAG = 0,
    FINDMY_MUX_MODE_FIND,
} findmy_mux_mode_t;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_types_pro.inc"
#line 218 "../Src/user/inktag_gr_app.c"
#endif
typedef enum { DIAG_IO_GPIOA = 0, DIAG_IO_AON = 1, DIAG_IO_MSIO = 2, DIAG_IO_NORMAL = 3 } diag_io_type_id_t;

typedef struct {
    uint8_t type_id;
    uint8_t max_pin;
} diag_scan_range_t;

typedef struct {
    uint16_t length;
    uint8_t data[INKTAG_GR_MAX_DATA_LEN];
} rx_packet_t;

typedef struct {
    bool active;
    uint8_t object_type;
    uint8_t transaction_id;
    uint8_t format;
    uint8_t segment_count;
    uint8_t black_mask;
    uint8_t resource_id;
    /* Admission snapshot remains stable while the replacement head is erased.
     * Commit re-evaluates current entitlement against these pre-write facts. */
    bool replacing_existing;
    uint16_t image_saved_count;
    uint16_t image_existing_ordinal;
    uint32_t total_size;
    uint32_t written;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t segment_size[2];
    uint32_t last_activity_ms;
} v2_transfer_t;

#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
#include "inktag_gr_advertising_state_pro.inc"
#if INKTAG_GR_MINUTE_IDLE_ENABLED
#include "inktag_gr_idle_clock_state_pro.inc"
#endif
#endif

static ble_gap_adv_param_t s_gap_adv_param;
static ble_gap_adv_time_param_t s_gap_adv_time_param;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_advertising_state_pro.inc"
#line 257 "../Src/user/inktag_gr_app.c"
#endif
static uint8_t s_conn_idx = 0xFF;
/* Cleared by link/notification loss, never adopted by a replacement link. */
static volatile bool s_screen_save_owned;
/* Remains set across link loss until the original transaction unwinds. */
static bool s_screen_save_active;
static inktag_device_password_t s_password;
static inktag_device_password_config_t s_password_config;
static bool s_notify_enabled = false;
static uint32_t s_ble_discovery_until_ms = 0u;
static volatile uint32_t s_timestamp = 1735689600;
static inktag_elapsed_clock_t s_elapsed_clock;
static inktag_clock_rate_t s_clock_rate;
static inktag_elapsed_clock_t s_checkpoint_clock;
static inktag_time_checkpoint_t s_time_checkpoint;
static uint32_t s_checkpoint_now_ms;
static bool s_time_checkpoint_found;
static bool s_time_checkpoint_blocked;
static uint16_t s_device_id4;
static bool s_device_id4_valid;
static uint16_t s_utc_subsecond_ms;
static inktag_elapsed_clock_t s_status_clock;
static app_timer_id_t s_clock_timer;
static app_timer_id_t s_diag_scan_timer;
static app_timer_id_t s_status_timer;
static bool s_status_timer_created = false;
static bool s_status_timer_running = false;
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_2_pro.inc"
#line 286 "../Src/user/inktag_gr_app.c"
#endif
static epd_config_t s_config;
static epd_screen_config_t s_screen_config;
static epd_display_state_t s_display_state;
static inktag_display_settings_t s_display_settings;
static epd_device_settings_t s_device_settings;
static bool s_display_state_loaded = false;
static bool s_boot_restore_pending = false;
static bool s_boot_restore_due = false;
static inktag_boot_display_target_t s_boot_display_target =
    INKTAG_BOOT_DISPLAY_NONE;
static uint32_t s_startup_status_until_ms = 0u;
static epd_model_t *s_epd = NULL;
static bool s_epd_gpio_ready = false;
static epd_work_type_t s_epd_work_type = EPD_WORK_NONE;
static uint8_t s_epd_work_arg = 0;
static uint32_t s_epd_work_timestamp = 0;
#if !INKTAG_GR_DELAYED_ADVERTISING_ENABLED
static volatile bool s_epd_work_pending = false;
#endif
static volatile bool s_epd_work_background = false;
static volatile epd_work_origin_t s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
#if !INKTAG_GR_DELAYED_ADVERTISING_ENABLED
static volatile bool s_epd_work_active = false;
#endif
static volatile bool s_epd_work_active_background = false;
static volatile epd_work_origin_t s_epd_work_active_origin = EPD_WORK_ORIGIN_NONE;
static epd_work_type_t s_epd_active_type = EPD_WORK_NONE;
static uint8_t s_epd_active_arg = 0;
static uint32_t s_epd_active_timestamp = 0;
static volatile bool s_epd_cancel_requested = false;
static volatile bool s_epd_low_power_abort = false;
static volatile bool s_epd_ghost_cleanup = false;
static volatile bool s_epd_stop_ack_pending = false;
#if !INKTAG_GR_DELAYED_ADVERTISING_ENABLED
static volatile bool s_picture_io_active = false;
#endif
static bool s_slideshow_active = false;
static uint32_t s_slideshow_interval = 300u;
static uint32_t s_slideshow_next_at = 0u;
static uint8_t s_slideshow_current_id = UINT8_MAX;
static volatile bool s_slideshow_advance_pending = false;
#if defined(INKTAG_GOODIX_GR5515)
static bool s_clear_return_valid = false;
static epd_display_state_t s_clear_return_state;
static bool s_input_slideshow_return_valid = false;
static epd_display_state_t s_input_slideshow_return_state;
#endif
static uint8_t s_pending_picture_store_id = UINT8_MAX;
static char s_device_name[DEVICE_NAME_MAX_LEN] = DEVICE_NAME_PREFIX;
static uint8_t s_adv_data_set[31];
static uint8_t s_adv_data_len = 0;
static uint8_t s_adv_rsp_data_set[31];
static uint8_t s_adv_rsp_data_len = 0;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_state_pro.inc"
#line 346 "../Src/user/inktag_gr_app.c"
#endif
static volatile bool s_adv_restart_due = false;
static uint32_t s_adv_restart_not_before_ms = 0u;
static bool s_advertising_ready = false;
static volatile bool s_ble_disconnect_due = false;
static uint8_t s_ble_disconnect_idx = 0xFFu;
static volatile bool s_link_cleanup_due = false;
static volatile bool s_cooperative_dispatching;
static volatile bool s_cooperative_send_failed;
static inktag_status_runtime_t s_status;
static inktag_status_config_t s_status_config;
static inktag_activation_t s_activation;
#if !INKTAG_GR_DELAYED_ADVERTISING_ENABLED
static volatile uint8_t s_system_action_pending = 0u;
#endif
static uint32_t s_system_action_not_before_ms = 0u;
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_app_state_pro.inc"

#endif
#line 367 "../Src/user/inktag_gr_app.c"



#line 371 "../Src/user/inktag_gr_app.c"
static uint16_t s_notify_inflight = 0u;
static bool s_notify_failed = false;
#endif
static volatile uint32_t s_status_now_ms = 0;
#if defined(INKTAG_GOODIX_GR5515)
static volatile uint32_t s_monotonic_now_ms = 0u;
static uint64_t s_app_timer_epoch_us = 0u;
static bool s_app_timer_epoch_valid = false;
#endif
static volatile bool s_status_tick_due = false;
static volatile bool s_clock_tick_due = false;
static volatile bool s_checkpoint_sample_due = true;
static bool s_native_refresh_pending = false;
static uint8_t s_native_refresh_mode = MODE_PICTURE;
static uint32_t s_native_refresh_timestamp = 0u;
static bool s_status_initialized = false;
static bool s_status_led_test_active = false;
static uint8_t s_status_led_test_mask = 0u;
static uint32_t s_status_led_test_until_ms = 0u;
static bool s_debug_attach_grace_complete = false;
static int16_t s_timezone_minutes = 0;
static bool s_time_valid = false;
static uint8_t s_v2_work_opcode = 0;
static uint8_t s_v2_work_transaction = 0;
static epd_work_type_t s_v2_work_type = EPD_WORK_NONE;
static uint8_t s_v2_cancel_transaction = 0;
static v2_transfer_t s_v2_transfer;
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
static bool s_aux_custom_authorized;
#endif
static void v2_transfer_reset(bool erase_store);
static bool s_diag_scan_active = false;
#if defined(INKTAG_GOODIX_OTA)
static bool s_swap_confirmation_pending = true;
static bool s_swap_confirmation_due = false;
#endif
static bool s_diag_scan_due = false;
static bool s_diag_scan_custom = false;
static diag_scan_range_t s_diag_scan_custom_range = {DIAG_IO_AON, 7};
static uint8_t s_diag_scan_range = 0;
static uint8_t s_diag_scan_pin = 0;
static uint8_t s_diag_scan_first_pin = 0;
static uint8_t s_diag_scan_phase = 0;
static rx_packet_t s_rx_queue[RX_QUEUE_SLOTS];
static rx_packet_t s_rx_processing;
static volatile uint8_t s_rx_head = 0;
static volatile uint8_t s_rx_tail = 0;

static void clock_process(void);
#if INKTAG_GR_MINUTE_IDLE_ENABLED
static void clock_idle(bool before_sleep);
#endif
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
static bool gr_panel_diag_available(void);
static bool gr_panel_diag_busy(void);
static bool gr_panel_diag_running(void);
static void gr_panel_diag_service(void);
static inktag_status_v2_t gr_panel_diag_close(void);
#endif
static bool clock_save_time(void);
static bool clock_save_time_candidate(uint32_t timestamp, int16_t timezone,
                                      bool explicit_time);
static void clock_checkpoint_process(void);
static void clock_restore_checkpoint(void);
static bool status_deadline_reached(uint32_t deadline_ms);
static bool background_display_startup_complete(void);
static bool epd_io_operation_ok(void);




static uint32_t monotonic_now_tick(void) {
#if defined(INKTAG_GOODIX_GR5515)
    return s_monotonic_now_ms;
#else
    return ble_time_get().hs;
#endif
}

/* Foreground UTC snapshot, shared by both Goodix MCU targets. The SDK may
 * wake the BLE core; do not call this from timer ISRs or replace raw safety
 * clocks with it. No extra timer or keep-awake policy is introduced. */
static uint32_t clock_now_tick(void) {
    return ble_time_get().hs;
}

/* Both checkpoint intervals use the existing SDK AON app-timer timeline.
 * Reading a save deadline must not request an additional BLE-core wakeup;
 * UTC and the existing status/safety clocks keep their own source/units. */
static uint32_t clock_raw_now_tick(void) {
#if defined(INKTAG_GOODIX_GR5515)
    extern uint64_t inktag_gr_app_timer_now_us(void);
    if (s_app_timer_epoch_valid) {
        uint64_t now_us = inktag_gr_app_timer_now_us();
        if (now_us >= s_app_timer_epoch_us)
            return (uint32_t)((now_us - s_app_timer_epoch_us) / 1000u);
    }
    return monotonic_now_tick();
#else
    extern uint32_t inktag_gr_checkpoint_aon_now_ms(void);
    return inktag_gr_checkpoint_aon_now_ms();
#endif
}

#if defined(INKTAG_GOODIX_GR5515)
static void monotonic_capture_app_timer_epoch(const app_timer_id_t *timer) {
    if (timer == NULL || timer->original_delay == 0u ||
        timer->next_shot_time < timer->original_delay ||
        s_app_timer_epoch_valid) return;
    /* app_timer deadlines use one SDK-global timeline.  It can already be
     * non-zero when the BLE stack calls ble_app_init(), whereas InkTag's
     * product clock deliberately starts at zero for every application boot.
     * Anchor the first product timer to its insertion time so the first
     * 120 ms status callback cannot skip the complete RGB startup scene. */
    s_app_timer_epoch_us = timer->next_shot_time - timer->original_delay;
    s_app_timer_epoch_valid = true;
}

static void monotonic_observe_app_timer(const app_timer_id_t *timer) {
    uint64_t scheduled_us;
    uint32_t observed_ms;
    if (timer == NULL || timer->original_delay == 0u ||
        timer->next_shot_time < timer->original_delay) return;
    scheduled_us = timer->next_shot_time - timer->original_delay;
    if (!s_app_timer_epoch_valid) {
        /* Defensive fallback for an unexpected callback-before-start return.
         * Count the callback's own interval instead of importing the SDK's
         * absolute epoch into the application clock. */
        s_app_timer_epoch_us = scheduled_us >= timer->original_delay
            ? scheduled_us - timer->original_delay : 0u;
        s_app_timer_epoch_valid = true;
    }
    if (scheduled_us < s_app_timer_epoch_us) return;
    observed_ms = (uint32_t)((scheduled_us - s_app_timer_epoch_us) / 1000u);
    if ((int32_t)(observed_ms - s_monotonic_now_ms) >= 0) {
        s_monotonic_now_ms = observed_ms;
    }
}
#endif

static inktag_status_v2_t product_access(
    inktag_product_feature_t feature, bool implemented, bool board_capable,
    bool experiment_enabled) {
    return inktag_access_check(&s_activation, feature, implemented,
                               board_capable, experiment_enabled);
}

static inktag_status_v2_t screen_control_access(
    const epd_screen_config_t *screen, bool backend_available) {
    return product_access(screen &&
        epd_screen_is_e6(screen->driver_id, screen->panel_recipe) ?
            INKTAG_PRODUCT_FEATURE_E6_CONTROL : INKTAG_PRODUCT_FEATURE_BASIC,
        backend_available, true, true);
}

static inktag_status_v2_t epd_control_access(void) {
    epd_screen_t resolved;
    return screen_control_access(&s_screen_config,
        epd_screen_resolve(&s_screen_config, &resolved) == EPD_SCREEN_STATUS_OK);
}

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_access_pro.inc"
#line 533 "../Src/user/inktag_gr_app.c"
#endif

volatile uint32_t g_inktag_gr_boot_stage = 0;
volatile uint32_t g_inktag_gr_last_error = 0;
volatile uint32_t g_inktag_gr_hw_info = 0;
volatile uint32_t g_inktag_gr_rf_info = 0;

/* Compatibility family only. Electrical decisions use the numeric backend,
 * never this schema1 string or a user-visible MCU/name. */
static const char *inktag_firmware_family_id(void) {
    /* Signed executable compatibility is not the mutable PCB selector.
     * An unconfigured backend must still report its actual image family. */
    return INKTAG_FIRMWARE_FAMILY_ID;
}

static const char *inktag_mcu_display_name(void) {
#if defined(INKTAG_GOODIX_GR5515)
    if ((g_inktag_gr_hw_info >> 16) == SDK_SUCCESS) {
        uint8_t package = (uint8_t)g_inktag_gr_hw_info;
        if (package == PACKAGE_GR5513BEND || package == PACKAGE_GR5513BENDU)
            return "GR5513";
        if (package == PACKAGE_GR5515I0ND) return "GR5515";
    }
    return "--";
#else
    return "GR5331";
#endif
}

static const diag_scan_range_t s_diag_scan_ranges[] = {
    {DIAG_IO_AON, 7},
    {DIAG_IO_MSIO, 7},
};

uint32_t timestamp(void) { return s_timestamp; }
static int16_t clock_rate_default_ppm(void) {
#if defined(INKTAG_GOODIX_GR5515)
    if (inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC42V)
        return INKTAG_BOARD_ZKC42V_CLOCK_RATE_PPM;
    if (inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC42VM)
        return INKTAG_BOARD_CLOCK_RATE_PPM;
#else
    if (inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC21V)
        return INKTAG_BOARD_CLOCK_RATE_PPM;
#endif
    return 0;
}

static uint32_t clock_elapsed_seconds(uint32_t now_tick) {
    inktag_elapsed_time_t elapsed =
        inktag_elapsed_clock_sample_time(&s_elapsed_clock, now_tick);
    uint32_t adjusted_ms = elapsed.milliseconds;
    uint32_t subsecond;
    /* Normal samples are bounded by the existing product clock. Keep all
     * SDK/status/PM counters raw; only UTC consumes this corrected increment. */
    (void)inktag_clock_rate_apply(&s_clock_rate, adjusted_ms, &adjusted_ms);
    subsecond = s_utc_subsecond_ms + adjusted_ms % 1000u;
    s_utc_subsecond_ms = (uint16_t)(subsecond % 1000u);
    return adjusted_ms / 1000u + subsecond / 1000u;
}

void set_timestamp(uint32_t value) {
    s_timestamp = value;
    s_utc_subsecond_ms = 0u;
    (void)inktag_clock_rate_set(&s_clock_rate, s_clock_rate.ppm);
    inktag_elapsed_clock_reset(&s_elapsed_clock, clock_now_tick(),
                               INKTAG_GR_UTC_COUNTER_MASK,
                               INKTAG_GR_UTC_TICKS_PER_SECOND);
}
void app_feed_wdt(void) { inktag_gr_health_watchdog_feed(); }

void inktag_gui_cooperative_yield(void) {
    inktag_gr_cooperative_maintenance();
}

bool inktag_dfu_link_allowed(uint8_t conn_idx) {
    /* Authentication belongs to the one accepted physical application link,
     * not to whichever central happens to write the vendor OTAS service. */
    return conn_idx != 0xFFu && conn_idx == s_conn_idx &&
           inktag_device_password_allowed(&s_password, &s_password_config);
}

bool inktag_dfu_mode_switch_allowed(void) {
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_3_pro.inc"
#line 618 "../Src/user/inktag_gr_app.c"
#endif
    /* The vendor OTAS service is a second ingress, outside process_packet. */
    if (!inktag_device_password_allowed(&s_password, &s_password_config)) {
        /* Do not leak a stale low-voltage/layout rejection from another call. */
        inktag_gr_dfu_reject_set(INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH, 0u, 0u, 0u, 0u);
        return false;
    }
    bool layout_ok = inktag_gr_layout_v3_installed();
    bool allowed = inktag_gr_health_operation_allowed(
        INKTAG_POWER_OPERATION_OTA, true);
    uint16_t voltage_mv = inktag_gr_health_voltage_mv(false);
    bool voltage_valid = inktag_gr_health_voltage_valid();
    if (!layout_ok) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_MODE_SWITCH, 0u, 0u, 0u, 0u);
    } else if (allowed) {
        inktag_gr_dfu_reject_clear();
    } else {
        inktag_gr_dfu_reject_set_power(voltage_mv, voltage_valid);
    }
    return layout_ok && allowed;
}
void inktag_dfu_before_reset(void) {
    (void)clock_save_time();
}
static bool epd_abort_requested(void) {
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_4_pro.inc"
#line 648 "../Src/user/inktag_gr_app.c"
#endif
    /* A cancelled ghost-clean pass gets one bounded best-effort return to
     * white. Ignore only the client's cancellation during that cleanup; the
     * common voltage-validity guard remains authoritative. */
    if (s_epd_cancel_requested && !s_epd_ghost_cleanup) return true;
    if (s_epd_work_active &&
        !inktag_gr_health_display_continue_allowed(s_status_now_ms)) {
        s_epd_low_power_abort = true;
        return true;
    }
    return false;
}

static bool send_bytes(uint8_t *p_data, uint16_t len) {
    if (s_conn_idx == 0xFF || !s_notify_enabled ||
        inktag_gr_notify(s_conn_idx, p_data, len) != SDK_SUCCESS) {
        if (s_cooperative_dispatching) s_cooperative_send_failed = true;
        return false;
    }
#if defined(INKTAG_GOODIX_GR5515)
    ++s_notify_inflight;
#endif
    return true;
}

static void put_u16_le(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16_le(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/* Empty responses use register-only arguments, with the same SDK admission,
 * per-call buffer, cooperative error accounting and notification counters. */
__attribute__((noinline, noclone))
static bool send_v2_empty_response(uint8_t opcode, uint8_t transaction_id,
                                   inktag_status_v2_t status) {
    uint8_t packet[INKTAG_PROTOCOL_V2_HEADER_SIZE] = {
        opcode, transaction_id, (uint8_t)status};
    return send_bytes(packet, sizeof(packet));
}

__attribute__((noinline, noclone))
static bool send_v2_payload_response(uint8_t opcode, uint8_t transaction_id,
                             inktag_status_v2_t status,
                             const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[INKTAG_GR_MAX_DATA_LEN];
    if (payload_length > sizeof(packet) - INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload = NULL;
        payload_length = 0;
    }
    packet[0] = opcode;
    packet[1] = transaction_id;
    packet[2] = (uint8_t)status;
    if (payload_length > 0u && payload != NULL) {
        memcpy(&packet[INKTAG_PROTOCOL_V2_HEADER_SIZE], payload, payload_length);
    }
    return send_bytes(packet, (uint16_t)(INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length));
}

__attribute__((always_inline))
static inline bool send_v2_response(uint8_t opcode, uint8_t transaction_id,
                                    inktag_status_v2_t status,
                                    const uint8_t *payload, uint16_t payload_length) {
    return payload_length == 0u ?
        send_v2_empty_response(opcode, transaction_id, status) :
        send_v2_payload_response(opcode, transaction_id, status, payload, payload_length);
}

static bool send_v2_event(uint8_t event, inktag_status_v2_t status,
                          uint8_t origin_opcode, uint8_t origin_transaction,
                          const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[INKTAG_GR_MAX_DATA_LEN];
    const uint16_t event_prefix_size = INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u;
    if (payload_length > sizeof(packet) - event_prefix_size) payload_length = sizeof(packet) - event_prefix_size;
    packet[0] = event;
    packet[1] = 0;
    packet[2] = (uint8_t)status;
    packet[3] = origin_opcode;
    packet[4] = origin_transaction;
    if (payload_length > 0u && payload != NULL) memcpy(&packet[event_prefix_size], payload, payload_length);
    return send_bytes(packet, (uint16_t)(event_prefix_size + payload_length));
}

static void send_text(const char *text) {
    if (text == NULL) return;
    uint16_t length = (uint16_t)strlen(text);
    send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0, 0,
                  (const uint8_t *)text, length);
}

static void send_v2_work_complete(inktag_status_v2_t status, uint8_t argument) {
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_complete_pro.inc"
#endif
#line 758 "../Src/user/inktag_gr_app.c"

#endif
    if (s_v2_work_transaction == 0u) {
        s_v2_work_type = EPD_WORK_NONE;
        return;
    }
    uint8_t payload[2] = {argument, 0u};
    uint16_t payload_length = 1u;
    if (s_v2_work_type == EPD_WORK_GHOST_CLEAN) {
        payload[0] = INKTAG_DISPLAY_ACTION_GHOST_CLEAN;
        payload[1] = argument;
        payload_length = 2u;
    }
    send_v2_event(INKTAG_EVENT_COMPLETE, status,
                  s_v2_work_opcode, s_v2_work_transaction,
                  payload, payload_length);
    s_v2_work_opcode = 0;
    s_v2_work_transaction = 0;
    s_v2_work_type = EPD_WORK_NONE;
}

static void process_packet(uint8_t *p_data, uint16_t length);
static void status_led_event_clock(void);
static void v2_find_owner_service(void);
static void v2_find_owner_clear(void);
#if defined(INKTAG_GOODIX_GR5515)
static void v2_input_simulation_service(void);
static void v2_input_simulation_cancel(uint8_t transaction,
                                       const uint8_t *payload, uint16_t length);
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_clear_pro.inc"
#else
static void v2_input_simulation_clear(void) { s_notify_inflight = 0u; s_notify_failed = false; }
#endif
#line 794 "../Src/user/inktag_gr_app.c"

#endif

static bool rx_queue_push(const uint8_t *data, uint16_t length) {
    if (!data || length == 0 || length > INKTAG_GR_MAX_DATA_LEN) return false;
    uint8_t head = s_rx_head;
    uint8_t next = (uint8_t)((head + 1u) % RX_QUEUE_SLOTS);
    if (next == s_rx_tail) return false;
    s_rx_queue[head].length = length;
    memcpy(s_rx_queue[head].data, data, length);
    __DMB();
    s_rx_head = next;
    return true;
}

static void rx_queue_process_one(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint8_t tail = s_rx_tail;
    if (tail == s_rx_head) {
        if ((primask & 1u) == 0u) __enable_irq();
        return;
    }
    s_rx_processing.length = s_rx_queue[tail].length;
    memcpy(s_rx_processing.data, s_rx_queue[tail].data, s_rx_processing.length);
    s_rx_tail = (uint8_t)((tail + 1u) % RX_QUEUE_SLOTS);
    if ((primask & 1u) == 0u) __enable_irq();
    /* Refresh only for an actual foreground command, after its bounded IRQ
     * copy. Finite error/success/locator feedback must not consume idle time
     * at its first LED tick. Empty idle turns remain sample/timer-free. */
    status_led_event_clock();
    process_packet(s_rx_processing.data, s_rx_processing.length);
}

static void rx_queue_reset(void) {
    s_rx_tail = s_rx_head;
}

#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
static void restart_advertising_if_due(void);
static void start_findmy_mux_if_due(void);
static void poll_findmy_time_multiplex(void);
static void advertising_service(bool refresh_identity);
#endif

void inktag_gr_cooperative_maintenance(void) {
    static bool servicing;
    /* Flash programming critical sections are not yield points. The SDK
     * event pump does not sleep or recursively enter app_process(). */
    if (servicing || (__get_PRIMASK() & 1u) != 0u) return;
    servicing = true;
    app_feed_wdt();
    pwr_mgmt_check_ble_event();
#if INKTAG_GR_NFC_STREAM_PILOT
    /* EPD waits already yield in 5 ms slices. Sample the AON clock here so
     * the visible refresh cadence does not depend on timer callback delivery. */
    if (s_epd_work_active)
        status_led_event_clock();
    else
#endif
    inktag_gr_status_led_poll();
    clock_process();
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_5_pro.inc"
#line 844 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
    /* Long EPD work yields here rather than returning to app_process().
     * Use the same deadlines for expiry, external wake and disconnect. */
    advertising_service(false);
#endif
    inktag_soak_telemetry_radio_maintenance();
    rx_packet_t packet;
    bool snapshot = false;
    __disable_irq();
    uint8_t tail = s_rx_tail;
    if (s_conn_idx != 0xffu && s_notify_enabled && tail != s_rx_head &&
        s_rx_queue[tail].length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        inktag_protocol_cooperative_snapshot(s_rx_queue[tail].data[0])) {
        /* Do not overwrite s_rx_processing: it may hold the outer storage
         * command. Never skip a queued mutation to find a later read. */
        packet = s_rx_queue[tail];
        snapshot = true;
    }
    __enable_irq();
    if (snapshot && s_conn_idx != 0xffu && s_notify_enabled) {
        s_cooperative_send_failed = false;
        s_cooperative_dispatching = true;
        process_packet(packet.data, packet.length);
        s_cooperative_dispatching = false;
        /* Completion callbacks may be deferred during a long SDK operation;
         * use actual notification admission, not that unrelated counter.
         * Read-only snapshots can safely retry on the next yield. Retain the
         * FIFO head on backpressure; never consume a silently lost reply. */
        __disable_irq();
        if (!s_cooperative_send_failed && s_rx_tail == tail)
            s_rx_tail = (uint8_t)((tail + 1u) % RX_QUEUE_SLOTS);
        __enable_irq();
    }
    servicing = false;
}

static void boot_stage(uint32_t stage) {
    g_inktag_gr_boot_stage = stage;
}

void inktag_gr_boot_stage_set(uint32_t stage) {
    boot_stage(stage);
}

uint32_t inktag_gr_boot_stage_get(void) {
    return g_inktag_gr_boot_stage;
}

void inktag_gr_boot_irq_state_capture(void) {
    g_inktag_gr_last_error = (__get_PRIMASK() & 0x1u) |
                             ((__get_BASEPRI() & 0xFFu) << 8) |
                             ((__get_FAULTMASK() & 0x1u) << 16) |
                             ((__get_CONTROL() & 0x3u) << 24);
}

void inktag_gr_hw_info_capture(void) {
#if defined(INKTAG_GOODIX_GR5515) || defined(INKTAG_GOODIX_GR5331)
    package_type_t package_type = PACKAGE_NONE;
    uint16_t package_status = sys_device_package_get(&package_type);

    // High half: API status; low half: package_type_t read from chip data.
    g_inktag_gr_hw_info = ((uint32_t)package_status << 16) | (uint16_t)package_type;
#endif
#if defined(INKTAG_GOODIX_GR5515)
    // High half: TX mode; low half: RF matching network impedance in ohms.
    g_inktag_gr_rf_info = ((uint32_t)ble_rf_tx_mode_get() << 16) |
                          (uint16_t)ble_rf_match_circuit_get();
#endif
}

static void boot_error(uint32_t stage, sdk_err_t error_code) {
    g_inktag_gr_boot_stage = stage;
    g_inktag_gr_last_error = (uint32_t)error_code;
}


typedef struct {
    int16_t value_sixteenths;
    bool valid;
} gr_temperature_sample_t;

typedef struct {
    gr_temperature_sample_t mcu_die;
    gr_temperature_sample_t panel_controller;
} gr_temperature_sources_t;

#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
#define GR_PANEL_TEMPERATURE_MAX_AGE_MS (15u * 60u * 1000u)
#define GR_CONNECTION_TEMPERATURE_TIMEOUT_MS 5000u
static gr_temperature_sample_t s_chip_temperature_cache;
static uint32_t s_chip_temperature_sampled_ms;
static gr_temperature_sample_t s_panel_temperature_cache;



static uint32_t s_panel_temperature_sampled_ms;
static uint32_t s_panel_temperature_deadline_ms;
static bool s_panel_temperature_sample_pending;
static volatile bool s_panel_temperature_sample_active;
static volatile bool s_panel_temperature_sample_cancel;
#endif

/* Read the MCU die sensor. A failed ADC operation or an implausible result is
 * unavailable; it must never be converted into a plausible 25 C sample. */
static gr_temperature_sample_t read_chip_temperature(void) {
    adc_handle_t adc = {0};
    uint16_t samples[16] = {0};
    uint32_t sum = 0;
    int32_t temperature = 25;
    gr_temperature_sample_t sample = {0};

    adc.init.channel_n = ADC_INPUT_SRC_TMP;
    adc.init.channel_p = ADC_INPUT_SRC_TMP;
    adc.init.input_mode = ADC_INPUT_SINGLE;
    adc.init.ref_source = ADC_REF_SRC_BUF_INT;
    adc.init.ref_value = ADC_REF_VALUE_0P8;
#if defined(INKTAG_GOODIX_GR5515)
    adc.init.clock = ADC_CLK_1P6M;
#else
    adc.init.clock = ADC_CLK_1M;
#endif

    if (hal_adc_init(&adc) != HAL_OK) {
        hal_adc_deinit(&adc);
        return sample;
    }
    if (inktag_gr_adc_sample16(&adc, samples) != HAL_OK) {
        hal_adc_deinit(&adc);
        return sample;
    }

    /* Goodix's reference implementations discard the first eight conversion
     * results while the internal sensor/reference settles. */
    for (uint8_t i = 8; i < 16; i++) sum += samples[i];
    uint16_t average = (uint16_t)(sum / 8u);

#if defined(INKTAG_GOODIX_GR5515)
    adc_trim_info_t trim = {0};
    if (sys_adc_trim_get(&trim) != SDK_SUCCESS || trim.slope_int_0p8 <= 0) {
        hal_adc_deinit(&adc);
        return sample;
    }
    {
        /* Goodix's floating-point formula simplifies to:
         *   delta_C = (sample - trim) * 4000 / (slope * 7)
         * Keep it integer so a one-degree display does not pull extra double
         * division code into the size-constrained stock GR5515 slot. */
        int32_t numerator = ((int32_t)average - trim.adc_temp) * 4000;
        int32_t denominator = trim.slope_int_0p8 * 7;
        int32_t delta = numerator >= 0
                            ? (numerator + denominator / 2) / denominator
                            : (numerator - denominator / 2) / denominator;
        temperature += delta;
    }
#else
    double converted = 0.0;
    hal_adc_temperature_conv(&adc, &average, &converted, 1);
    /* These ordered comparisons also reject NaN without adding libm. */
    if (!(converted >= -40.0 && converted <= 125.0)) {
        hal_adc_deinit(&adc);
        return sample;
    }
    temperature = (int32_t)(converted < 0.0 ? converted - 0.5 : converted + 0.5);
#endif

    hal_adc_deinit(&adc);
    if (temperature < -40 || temperature > 125) return sample;
    sample.value_sixteenths = (int16_t)(temperature * 16);
    sample.valid = true;
    return sample;
}

/* Queries only consume the sample captured by an ordinary display job.
 * Never initialize the EPD or its high-voltage rail for GET_STATUS. */
static gr_temperature_sample_t read_panel_temperature(void) {
    gr_temperature_sample_t sample = {0};
    uint8_t command, length, raw[2] = {0xffu, 0xffu};
    int16_t sixteenths;
    if (s_epd == NULL) return sample;
    if (!inktag_gr_epd_panel_temperature_raw(&command, raw, &length)) {
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        /* GET_STATUS never powers the controller. Only a decoded sample from
         * a completed powered transaction may survive its rail being parked. */
        if ((!s_epd_gpio_ready
#if INKTAG_EPD_TEMPERATURE_OPTION
             || (s_epd && !EPD_TemperatureReadEnabled(s_epd))
#endif
            ) && inktag_epd_temperature_sample_fresh(
                s_panel_temperature_cache.valid,
                s_panel_temperature_sampled_ms, s_status_now_ms,
                GR_PANEL_TEMPERATURE_MAX_AGE_MS))
            return s_panel_temperature_cache;
#endif
        return sample;
    }
    bool valid = false;
    if ((s_epd->driver_id == EPD_SCREEN_DRIVER_UC8176 ||
         s_epd->driver_id == EPD_SCREEN_DRIVER_UC8276) &&
        command == UC8xxx_TSC && length == 2u) {
        valid = inktag_uc_internal_r40_temperature_sixteenths(true, raw,
                                                               &sixteenths);
    } else if (((s_epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
               (s_epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0 ||
                s_epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1)) ||
               (s_epd->driver_id == EPD_SCREEN_DRIVER_JD79665CA &&
                s_epd->panel_recipe == INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6)) &&
               command == 0x40u && length == 2u) { /* JD R40 */
        valid = inktag_jd_r40_temperature_sixteenths(true, raw, &sixteenths);
    } else if ((s_epd->driver_id == EPD_SCREEN_DRIVER_IST7513 ||
                s_epd->driver_id == EPD_SCREEN_DRIVER_IST7601) &&
               command == 0x40u && length == 1u) {
        /* The common callback validates the native revision before issuing
         * R40. Do not reinterpret its binary integer as JD quarters. */
        valid = inktag_osprey_r40_temperature_sixteenths(true, raw[0], &sixteenths);
    } else if ((s_epd->driver_id == EPD_SCREEN_DRIVER_SSD1680 ||
                s_epd->driver_id == EPD_SCREEN_DRIVER_SSD1619) &&
               command == SSD1xxx_TSENSOR_READ && length == 2u) {
        /* SSD1619A R1B shares the signed12-bit layout (Rev0.10 p24).
         * Its qualified400x300 clock observer acquires this native sample;
         * decoding a cached read does not power an unrelated panel. */
        valid = inktag_ssd1680_r1b_temperature_sixteenths(true, raw,
                                                        &sixteenths);
    } else if (s_epd->driver_id == EPD_SCREEN_DRIVER_SSD1683 &&
               command == SSD1xxx_TSENSOR_READ && length == 1u) {
        valid = inktag_ssd1683_r1b_temperature_sixteenths(true, raw,
                                                        &sixteenths);
    }
    if (valid) {
        sample.value_sixteenths = sixteenths;
        sample.valid = true;
    }
    return sample;
}












static void init_panel_for_display(epd_model_t *epd) {
    uint8_t command = 0xffu, length = 0u, raw[2] = {0xffu, 0xffu};






    inktag_gr_epd_panel_temperature_invalidate();
    epd->drv->init(epd);
    if (EPD_AbortRequested()) {
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        s_panel_temperature_cache.valid=false;
#endif
        return;
    }



#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
#if INKTAG_EPD_TEMPERATURE_OPTION
    if (EPD_TemperatureReadEnabled(epd) || !epd_io_operation_ok())
        s_panel_temperature_cache.valid=false;

#else
    s_panel_temperature_cache.valid=false;
#endif
#endif
    /* JD and SSD1683 init already acquire their checked samples. SSD1680
     * selects its internal sensor but needs the shared bounded acquisition.
     * UC8276 init owns its pre-PON acquisition, including a failed sample.
     * UC8176 retains the port read. Never
     * reload SSD1677 OTP after its custom gray LUT, or retry SSD1683 init's
     * failed acquisition here. Unknown controller formats stay invalid. */
    if (!inktag_gr_epd_panel_temperature_raw(&command, raw, &length)) {
        if (epd->driver_id == EPD_SCREEN_DRIVER_SSD1680) {
            int8_t temperature;
            (void)SSD1xxx_ReadTemperature(epd, &temperature);
        } else if (EPD_TemperatureReadEnabled(epd) &&
                   (epd->driver_id == EPD_SCREEN_DRIVER_UC8176
#if !INKTAG_GR_NFC_STREAM_PILOT
                    || epd->driver_id == EPD_SCREEN_DRIVER_UC8276
#endif
                   )) {
            (void)EPD_ReadCommand(UC8xxx_TSC, raw, sizeof(raw));
        } else if ((epd->driver_id == EPD_SCREEN_DRIVER_IST7513 ||
                    epd->driver_id == EPD_SCREEN_DRIVER_IST7601) &&
                   epd->drv->read_temp != NULL) {
            /* Native revision check, then bounded R40; no duplicate driver,
             * PON/DRF or guessed fallback in this port. */
            (void)epd->drv->read_temp(epd);
        }
    }
    gr_temperature_sample_t sample = read_panel_temperature();
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    if (sample.valid
#if INKTAG_EPD_TEMPERATURE_OPTION
        && EPD_TemperatureReadEnabled(epd)
#endif
    ) {
        s_panel_temperature_cache = sample;
        s_panel_temperature_sampled_ms = s_status_now_ms;
    }
#endif
    (void)inktag_gr_epd_panel_temperature_raw(&command, raw, &length);
    char diagnostic[80];
    GFX_format(diagnostic, sizeof(diagnostic),
             "epd:panel-temp valid=%u c16=%d cmd=%02x raw=%02x,%02x len=%u",
             sample.valid ? 1u : 0u, sample.value_sixteenths,
             command, raw[0], raw[1], length);
    send_text(diagnostic);
#if defined(INKTAG_GR_CLOCK_PARTIAL_SSD1683) && INKTAG_GR_CLOCK_PARTIAL_SSD1683
    gr_clock_partial_observe_panel();
#endif
}

static gr_temperature_sources_t read_temperature_sources(void) {
    gr_temperature_sources_t sources = {
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        .mcu_die = inktag_epd_temperature_sample_fresh(
            s_chip_temperature_cache.valid, s_chip_temperature_sampled_ms,
            s_status_now_ms, GR_PANEL_TEMPERATURE_MAX_AGE_MS) ?
                s_chip_temperature_cache : (gr_temperature_sample_t){0},
        /* A read-only cooperative snapshot may run during controller init.
         * Publish its sample only after sleep/cleanup have succeeded. */
        .panel_controller = s_panel_temperature_sample_active ?
            (gr_temperature_sample_t){0} : read_panel_temperature(),
#else
        .mcu_die = read_chip_temperature(),
        .panel_controller = read_panel_temperature(),
#endif
    };
    return sources;
}

static bool select_effective_temperature(const gr_temperature_sources_t *sources,
                                         int8_t *temperature) {
    if (sources == NULL) return false;
    return inktag_temperature_select_lower(
        sources->mcu_die.valid, sources->mcu_die.value_sixteenths,
        sources->panel_controller.valid,
        sources->panel_controller.value_sixteenths,
        temperature);
}

static bool read_effective_temperature(int8_t *temperature) {
    gr_temperature_sources_t sources = read_temperature_sources();
    return select_effective_temperature(&sources, temperature);
}

static void send_diag(const char *tag) {
    char buf[256];
    uint8_t spi_hardware = 0;
    uint16_t spi_init_status = 0;
    uint32_t spi_error_count = 0;
    uint16_t spi_last_status = 0;
    uint32_t spi_core_hz = 0;
    uint32_t spi_baud_divider = 0;
    inktag_gr_epd_spi_diag(&spi_hardware, &spi_init_status,
                           &spi_error_count, &spi_last_status,
                           &spi_core_hz, &spi_baud_divider);
    uint8_t raw_busy = EPD_ReadBusy() ? 1 : 0;
    uint8_t busy = s_epd && s_epd->drv && s_epd->drv->read_busy
                       ? (s_epd->drv->read_busy(s_epd) ? 1 : 0)
                       : raw_busy;
    GFX_format(buf, sizeof(buf),
             "%s drv=%02x active=%02x raw=%u busy=%u v=%u edge=%u assert_ms=%" PRIu32 " busy_ms=%" PRIu32 " spi=%u init=%u txerr=%" PRIu32 " last=%u core=%" PRIu32 " div=%" PRIu32 " pins=%02x,%02x,%02x,%02x,%02x,%02x,%02x wk=%02x en=%02x led=%02x",
             tag,
             s_screen_config.driver_id,
             s_epd ? s_epd->driver_id : 0xff,
             raw_busy,
             busy,
             EPD_ReadVoltage(),
             EPD_BusyDiagAsserted() ? 1 : 0,
             EPD_BusyDiagAssertDelayMs(),
             EPD_BusyDiagActiveMs(),
             spi_hardware,
             (unsigned)spi_init_status,
             spi_error_count,
             (unsigned)spi_last_status,
             spi_core_hz,
             spi_baud_divider,
             s_config.mosi_pin,
             s_config.sclk_pin,
             s_config.cs_pin,
             s_config.dc_pin,
             s_config.rst_pin,
             s_config.busy_pin,
             s_config.bs_pin,
             s_config.wakeup_pin,
             s_config.en_pin,
             s_config.led_pin);
    send_text(buf);
}

void inktag_gr_epd_trace(const char *tag) {
    send_diag(tag);
}

#if defined(INKTAG_GOODIX_OTA)
#endif

static bool screen_config_matches(const uint8_t encoded[EPD_SCREEN_WIRE_SIZE]) {
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return encoded != NULL &&
           epd_screen_config_encode(&s_screen_config, current) == EPD_SCREEN_STATUS_OK &&
           epd_screen_wire_image_compatible(encoded, current);
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static epd_model_t *image_store_model(void) {
    return epd_find(&s_screen_config);
}

bool inktag_gr_image_store_config_read(epd_screen_config_t *screen) {
    if (!screen) return false;
    *screen = s_screen_config;
    return true;
}

static uint32_t image_store_expected_payload(const epd_model_t *model) {
    epd_screen_t resolved;
    if (!model || epd_screen_resolve(&s_screen_config, &resolved) != EPD_SCREEN_STATUS_OK) return 0;
    return resolved.frame_bytes;
}

/* Apply the common base quota to stable ascending logical IDs.
 * A Pro -> base -> Pro transition
 * hides records without renumbering or erasing them. */
static bool image_store_record_ordinal(uint8_t wanted, uint16_t *ordinal) {
    uint8_t n;
    if (!ordinal || inktag_image_store_ordinal(wanted, &n) != INKTAG_IMAGE_STORE_OK)
        return false;
    *ordinal = n;
    return true;
}
static bool image_store_record_visible(uint8_t wanted_id) {
    uint16_t ordinal;
    return image_store_record_ordinal(wanted_id, &ordinal) &&
        inktag_access_image_record_visible(&s_activation, ordinal,
            s_screen_config.visible_width, s_screen_config.visible_height);
}

static inktag_image_store_result_t image_store_next_visible(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header) {
    uint8_t cursor = after_id;
    for (;;) {
        uint8_t candidate;
        inktag_image_store_header_t current;
        inktag_image_store_result_t result =
            inktag_image_store_next(cursor, &candidate, &current);
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        if (image_store_record_visible(candidate)) {
            if (id) *id = candidate;
            if (header) *header = current;
            return INKTAG_IMAGE_STORE_OK;
        }
        cursor = candidate;
    }
}

static bool image_store_next_visible_compatible(uint8_t after_id,
                                                 bool slideshow_only,
                                                 uint8_t *id) {
    uint8_t cursor = after_id;
    bool wrapped = false;
    for (;;) {
        uint8_t candidate;
        inktag_image_store_header_t header;
        inktag_image_store_result_t result =
            image_store_next_visible(cursor, &candidate, &header);
        if (result != INKTAG_IMAGE_STORE_OK) {
            if (wrapped || after_id == UINT8_MAX) return false;
            cursor = UINT8_MAX;
            wrapped = true;
            continue;
        }
        if (wrapped && candidate > after_id) return false;
        if (screen_config_matches(header.screen_config) &&
            (!slideshow_only ||
             header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)) {
            if (id) *id = candidate;
            return true;
        }
        cursor = candidate;
    }
}

/* Storage filtering keeps stable slot IDs and does not wrap. The common
 * four-page policy uses its first IMAGE to enter the single slideshow page;
 * timer rotation keeps image_store_next_visible_compatible() wrap behavior. */
static bool image_store_next_visible_compatible_no_wrap(uint8_t after_id,
                                                         bool slideshow_only,
                                                         uint8_t *id) {
    uint8_t cursor = after_id;
    for (;;) {
        uint8_t candidate;
        inktag_image_store_header_t header;
        if (image_store_next_visible(cursor, &candidate, &header) !=
            INKTAG_IMAGE_STORE_OK) {
            return false;
        }
        if (screen_config_matches(header.screen_config) &&
            (!slideshow_only ||
             header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)) {
            if (id) *id = candidate;
            return true;
        }
        cursor = candidate;
    }
}

static inktag_image_store_result_t image_store_visible_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status, uint16_t *allocatable) {
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, status);
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    *allocatable = (uint16_t)(inktag_image_store_fixed_slot_count() - status->saved_count);
    return result;
}

static inktag_status_v2_t image_store_create_access(
    const epd_screen_config_t *screen, uint32_t payload_size) {
    inktag_image_store_status_t status;
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, &status);
    if (result == INKTAG_IMAGE_STORE_UNSUPPORTED)
        return INKTAG_STATUS_UNSUPPORTED;
    if (result == INKTAG_IMAGE_STORE_NO_SPACE)
        return INKTAG_STATUS_NO_SPACE;
    if (result != INKTAG_IMAGE_STORE_OK)
        return INKTAG_STATUS_IO_ERROR;
    return inktag_access_image_write(
        &s_activation, status.saved_count, false, 0u,
        status.saved_count < inktag_image_store_fixed_slot_count(),
        s_screen_config.visible_width, s_screen_config.visible_height);
}

static inktag_status_v2_t image_store_write_access(
    const epd_screen_config_t *screen, uint32_t payload, uint8_t replace) {
    if (replace == UINT8_MAX) return image_store_create_access(screen, payload);
    inktag_image_store_status_t status;
    inktag_image_store_result_t result = inktag_image_store_status(screen, payload, &status);
    if (result != INKTAG_IMAGE_STORE_OK) return result == INKTAG_IMAGE_STORE_UNSUPPORTED
        ? INKTAG_STATUS_UNSUPPORTED : result == INKTAG_IMAGE_STORE_NO_SPACE
        ? INKTAG_STATUS_NO_SPACE : INKTAG_STATUS_IO_ERROR;
    uint16_t ordinal;
    if (replace >= inktag_image_store_fixed_slot_count()) return INKTAG_STATUS_INVALID_REQUEST;
    if (!image_store_record_ordinal(replace, &ordinal))
        return image_store_create_access(screen, payload);
    return inktag_access_image_write(&s_activation, status.saved_count, true, ordinal,
        true, /* In-place replacement needs no spare/COW slot. */
        screen->visible_width, screen->visible_height);
}

static inktag_image_store_result_t image_store_clear_visible(
    bool incompatible_only, uint16_t *removed) {
    return incompatible_only
        ? inktag_gr_image_store_wait(inktag_image_store_clear_incompatible(&s_screen_config),
            INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE, removed)
        : inktag_gr_image_store_wait(inktag_image_store_clear_all(),
            INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, removed);
}

static const char *image_store_result_name(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return "ok";
        case INKTAG_IMAGE_STORE_UNSUPPORTED: return "unsupported";
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT: return "bad-argument";
        case INKTAG_IMAGE_STORE_BUSY: return "busy";
        case INKTAG_IMAGE_STORE_NO_SPACE: return "no-space";
        case INKTAG_IMAGE_STORE_IO_ERROR: return "io-error";
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return "size-mismatch";
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return "crc-mismatch";
        case INKTAG_IMAGE_STORE_NOT_FOUND: return "not-found";
        case INKTAG_IMAGE_STORE_LOW_POWER: return "low-power";
        default: return "unknown";
    }
}

static void send_image_store_error(const char *operation, inktag_image_store_result_t result) {
    char buf[112];
    if (result == INKTAG_IMAGE_STORE_IO_ERROR) {
        uint16_t sector = UINT16_MAX;
        uint32_t flash_id = 0;
        uint32_t flash_size = 0;
        inktag_image_store_error_context(&sector);
        hal_flash_get_info(&flash_id, &flash_size);
        GFX_format(buf, sizeof(buf),
                 "img:%s:error=io-error sector=%u flash=%08" PRIx32 "/%" PRIu32,
                 operation, sector, flash_id, flash_size);
    } else {
        GFX_format(buf, sizeof(buf), "img:%s:error=%s", operation, image_store_result_name(result));
    }
    send_text(buf);
}

static void slideshow_stop(bool notify) {
    bool was_active = s_slideshow_active;
    s_slideshow_active = false;
    s_slideshow_next_at = 0;
    s_slideshow_current_id = UINT8_MAX;
    s_slideshow_advance_pending = false;
    if (notify && was_active) send_text("img:slideshow:stopped");
}

static void slideshow_arm_next_after_refresh(void) {
    s_slideshow_next_at = s_timestamp + s_slideshow_interval;
    s_slideshow_advance_pending = false;
}

/* A queued candidate is not the displayed image. Failure consumes this
 * boundary, retains the last successful ID and uses the ordinary interval.
 * STOP/pause clears active, so late completion cannot restart a slideshow. */
static void display_work_finished(epd_work_type_t type, uint8_t argument,
                                   bool success) {
    if (!success && type == EPD_WORK_GUI &&
        s_native_refresh_mode == argument) s_native_refresh_pending = false;
    if (type != EPD_WORK_IMAGE_SHOW || !s_slideshow_active) return;
    if (success) s_slideshow_current_id = argument;
    slideshow_arm_next_after_refresh();
}

static bool slideshow_select_next(uint8_t *id) {
    return image_store_model() != NULL &&
           image_store_next_visible_compatible(
               s_slideshow_current_id, true, id);
}

static uint32_t board_pin_mask(uint8_t pin) {
    return pin == 0xFF ? 0u : (1u << (pin & 0x3Fu));
}

static app_io_type_t board_pin_type(uint8_t pin) {
    switch ((pin >> 6) & 0x03u) {
        case 1: return APP_IO_TYPE_AON;
        case 2: return APP_IO_TYPE_MSIO;
#if defined(INKTAG_GOODIX_GR5515)
        default: return APP_IO_TYPE_NORMAL;
#else
        default: return APP_IO_TYPE_GPIOA;
#endif
    }
}

#ifndef INKTAG_BOARD_HAS_RUNTIME_RGB_LED
#define INKTAG_BOARD_HAS_RUNTIME_RGB_LED 0
#endif

static bool runtime_rgb_led_available(void) {
    if (inktag_gr_board_identity() == INKTAG_GR_BOARD_UNKNOWN) return false;
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required()) return inktag_gr_pin_routes_drive_ready();
#endif
#if INKTAG_BOARD_HAS_LED_DIAG
    return true;
#elif INKTAG_BOARD_HAS_RUNTIME_RGB_LED && defined(INKTAG_GOODIX_GR5515)
    return inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC42V;
#else
    return false;
#endif
}

static uint8_t runtime_led_pin(unsigned channel) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required()) {
        const inktag_pin_config_t *pins = inktag_gr_pin_routes_peek();
        return pins && channel < 3u ? pins->pins[INKTAG_PIN_LED_RED + channel] : 255u;
    }
#endif
    const uint8_t pins[3] = {INKTAG_BOARD_LED_RED_PIN, INKTAG_BOARD_LED_GREEN_PIN,
                            INKTAG_BOARD_LED_BLUE_PIN};
    return channel < 3u ? pins[channel] : 255u;
}

static bool runtime_led_active_low(uint8_t pin) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required()) {
        const inktag_pin_config_t *pins = inktag_gr_pin_routes_peek();
        if (!pins) return false;
        for (unsigned role = INKTAG_PIN_LED_RED; role <= INKTAG_PIN_LED_BLUE; ++role)
            if (pins->pins[role] == pin) return (pins->active_low & (1u << role)) != 0u;
        return false;
    }
#endif
    return INKTAG_BOARD_LED_ACTIVE_LOW != 0;
}

static void board_led_write(uint8_t pin, bool on) {
#if INKTAG_BOARD_HAS_STATUS_LED || INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (pin == 0xFFu) return;
    app_io_pin_state_t active = runtime_led_active_low(pin) ? APP_IO_PIN_RESET : APP_IO_PIN_SET;
    app_io_pin_state_t inactive = runtime_led_active_low(pin) ? APP_IO_PIN_SET : APP_IO_PIN_RESET;
    app_io_write_pin(board_pin_type(pin), board_pin_mask(pin), on ? active : inactive);
#else
    (void)pin;
    (void)on;
#endif
}

static void board_led_init(uint8_t pin) {
#if INKTAG_BOARD_HAS_STATUS_LED || INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (pin == 0xFFu) return;
    board_led_write(pin, false);
    app_io_init_t init;
    memset(&init, 0, sizeof(init));
    init.pin = board_pin_mask(pin);
    init.mode = APP_IO_MODE_OUTPUT;
    init.pull = APP_IO_NOPULL;
    init.mux = INKTAG_GPIO_MUX;
    app_io_init(board_pin_type(pin), &init);
#else
    (void)pin;
#endif
}

static void status_led_force_off(void) {
#if INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (!runtime_rgb_led_available()) return;
    board_led_write(runtime_led_pin(0u), false);
    board_led_write(runtime_led_pin(1u), false);
    board_led_write(runtime_led_pin(2u), false);
#elif INKTAG_BOARD_HAS_STATUS_LED
    board_led_write(INKTAG_BOARD_STATUS_LED_PIN, false);
#endif
}

static uint8_t status_led_available_colors(void) {
#if INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (!runtime_rgb_led_available()) return INKTAG_STATUS_COLOR_OFF;
    return (runtime_led_pin(0u) != 255u ? INKTAG_STATUS_COLOR_RED : 0u) |
           (runtime_led_pin(1u) != 255u ? INKTAG_STATUS_COLOR_GREEN : 0u) |
           (runtime_led_pin(2u) != 255u ? INKTAG_STATUS_COLOR_BLUE : 0u);
#elif INKTAG_BOARD_HAS_STATUS_LED
    return INKTAG_STATUS_COLOR_BLUE;
#else
    return INKTAG_STATUS_COLOR_OFF;
#endif
}

static void status_led_write_mask(uint8_t color_mask) {
#if INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (!runtime_rgb_led_available()) return;
    board_led_write(runtime_led_pin(0u),
                    (color_mask & INKTAG_STATUS_COLOR_RED) != 0u);
    board_led_write(runtime_led_pin(1u),
                    (color_mask & INKTAG_STATUS_COLOR_GREEN) != 0u);
    board_led_write(runtime_led_pin(2u),
                    (color_mask & INKTAG_STATUS_COLOR_BLUE) != 0u);
#elif INKTAG_BOARD_HAS_STATUS_LED
    board_led_write(INKTAG_BOARD_STATUS_LED_PIN, color_mask != 0u);
#else
    (void)color_mask;
#endif
}

static void status_led_output(void *context, uint8_t color_mask) {
    (void)context;
    if (s_status_led_test_active) return;
    status_led_write_mask(color_mask);
}

static void status_led_timer_handler(void *p_ctx) {
    (void)p_ctx;
#if defined(INKTAG_GOODIX_GR5515)
    /* app_timer advances next_shot_time from its AON sleep-timer timeline
     * before invoking this callback. Copy that scheduled hardware time rather
     * than counting callbacks, so delayed/coalesced service loses no elapsed
     * time and no BLE-domain counter is required during SLEEP. */
    monotonic_observe_app_timer(&s_status_timer);
#endif
    /* The callback remains only a sampling opportunity; policy and GPIO work
     * stay in the application loop. */
    s_status_tick_due = true;
}

static void status_timer_create_if_available(void) {
    if (!s_status_timer_created &&
#if !INKTAG_GR_BUTTON_IRQ_ENABLED
        (status_led_available_colors() != INKTAG_STATUS_COLOR_OFF
#if INKTAG_GR_NFC_STREAM_PILOT
         || inktag_gr_nfc_capabilities_supported()
#endif
        ) &&
#endif
        app_timer_create(&s_status_timer, ATIMER_REPEAT,
                         status_led_timer_handler) == SDK_SUCCESS)
        s_status_timer_created = true;
}

static void status_timer_update(void) {
    bool needed;
    sdk_err_t error_code;

    if (!s_status_timer_created || !s_status_initialized) return;
    needed = inktag_status_needs_periodic_tick(&s_status, s_status_now_ms);
#if INKTAG_GR_BUTTON_IRQ_ENABLED
    needed = needed || inktag_gr_aux_io_gesture_active();
#endif
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_6_pro.inc"
#line 1679 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    needed = needed || gr_panel_diag_running();
#endif
    if (needed == s_status_timer_running
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_7_pro.inc"
#line 1686 "../Src/user/inktag_gr_app.c"
#endif
       ) return;
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_8_pro.inc"
#line 1695 "../Src/user/inktag_gr_app.c"
#endif
    if (needed) {
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_9_pro.inc"
#line 1700 "../Src/user/inktag_gr_app.c"
#else
        error_code = app_timer_start(s_status_timer, STATUS_LED_TIMER_INTERVAL_MS, NULL);
#endif
        if (error_code == SDK_SUCCESS) {
#if defined(INKTAG_GOODIX_GR5515)
            monotonic_capture_app_timer_epoch(&s_status_timer);
#endif
            s_status_timer_running = true;
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_10_pro.inc"
#line 1709 "../Src/user/inktag_gr_app.c"
#endif
        }
    } else {
        (void)app_timer_stop(s_status_timer);
        s_status_timer_running = false;
    }
}

static inktag_status_v2_t status_led_config_access(
    const inktag_status_config_t *config) {
    inktag_status_v2_t access;
    if (!inktag_status_config_valid(config)) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    access = inktag_access_indicator_policy(&s_activation, config->policy);
    return access;
}

static void status_led_canonicalize(const inktag_status_config_t *requested,
                                    inktag_status_config_t *canonical) {
    inktag_status_config_default(canonical);
    canonical->policy = requested->policy;
}

static void status_led_enforce_entitlement(void) {
    inktag_status_config_t effective = s_status_config;
    if (!s_status_initialized) return;
    if (status_led_config_access(&effective) != INKTAG_STATUS_OK) {
        inktag_status_config_default(&effective);
    }
    if (memcmp(&s_status.config, &effective, sizeof(effective)) != 0) {
        inktag_status_set_config(&s_status, &effective, s_status_now_ms);
    }
}

static inktag_status_v2_t status_led_set_configuration(
    const uint8_t *wire, uint16_t length) {
    inktag_status_config_t requested;
    inktag_status_config_t canonical;
    uint8_t encoded[INKTAG_STATUS_WIRE_SIZE];
    inktag_status_v2_t access;
    if (!inktag_status_config_decode(&requested, wire, length)) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    access = status_led_config_access(&requested);
    if (access != INKTAG_STATUS_OK) return access;
    status_led_canonicalize(&requested, &canonical);
    if (!inktag_status_config_encode(&canonical, encoded)) {
        return INKTAG_STATUS_INTERNAL_ERROR;
    }
    /* process_packet() checked command entry; this is the actual NVDS write
     * boundary and therefore takes its own fresh product-policy sample. */
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                             true)) {
        return INKTAG_STATUS_LOW_POWER;
    }
    if (!inktag_gr_settings_put(INKTAG_GR_SETTINGS_SLOT_INDICATOR,
                                sizeof(encoded), encoded)) {
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    }
    s_status_config = canonical;
    status_led_enforce_entitlement();
    return INKTAG_STATUS_OK;
}

void inktag_gr_status_led_poll(void) {
    inktag_elapsed_time_t elapsed;
    bool locate_was_active;
    if (!s_status_initialized || !s_status_tick_due) return;
    s_status_tick_due = false;
    elapsed = inktag_elapsed_clock_sample_time(&s_status_clock,
                                               monotonic_now_tick());
    s_status_now_ms += elapsed.milliseconds;
    locate_was_active = inktag_status_locate_active(&s_status);
    inktag_status_tick(&s_status, s_status_now_ms);
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
    if (locate_was_active && !inktag_status_locate_active(&s_status))
#else
    (void)locate_was_active;
#endif
    if (s_status_led_test_active &&
        (int32_t)(s_status_now_ms - s_status_led_test_until_ms) >= 0) {
        s_status_led_test_active = false;
        s_status_led_test_mask = 0u;
        s_status.last_output = 0xFFu;
        inktag_status_tick(&s_status, s_status_now_ms);
    }
    inktag_activation_expire(&s_activation, s_status_now_ms);
    status_led_enforce_entitlement();
}

static void status_led_event_clock(void) {
    if (!s_status_initialized) return;
#if defined(INKTAG_GOODIX_GR5515)
    /* Between idle callbacks use the same continuous AON timer epoch, not
     * its last callback copy. No BLE-clock wakeup or extra timer is needed. */
    s_monotonic_now_ms = clock_raw_now_tick();
#endif
    /* A new finite scene must not inherit up to one second of idle time at
     * the first 40 ms callback. Only event boundaries force a sample. */
    s_status_tick_due = true;
    inktag_gr_status_led_poll();
}

static void status_led_reapply(void) {
    if (!s_status_initialized) return;
    s_status.last_output = 0xFFu;
    inktag_status_tick(&s_status, s_status_now_ms);
}

static void status_led_init(void) {
    uint8_t encoded[INKTAG_STATUS_WIRE_SIZE];
    uint16_t encoded_length = sizeof(encoded);
    inktag_status_config_t effective;
#if INKTAG_BOARD_HAS_LED_DIAG || INKTAG_BOARD_HAS_RUNTIME_RGB_LED
    if (runtime_rgb_led_available()) {
        board_led_init(runtime_led_pin(0u));
        board_led_init(runtime_led_pin(1u));
        board_led_init(runtime_led_pin(2u));
    }
#elif INKTAG_BOARD_HAS_STATUS_LED
    board_led_init(INKTAG_BOARD_STATUS_LED_PIN);
#endif
    status_led_force_off();
    inktag_status_config_default(&s_status_config);
    if (inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_INDICATOR,
                               &encoded_length, encoded) ==
            INKTAG_GR_SETTINGS_READ_OK &&
        inktag_status_config_decode(&s_status_config, encoded,
                                    encoded_length)) {
        inktag_status_config_t canonical;
        status_led_canonicalize(&s_status_config, &canonical);
        s_status_config = canonical;
    } else {
        inktag_status_config_default(&s_status_config);
    }
    effective = s_status_config;
    if (status_led_config_access(&effective) != INKTAG_STATUS_OK) {
        inktag_status_config_default(&effective);
    }
    /* The first timer sample must start at the visible scene, not before
     * synchronous NVDS/GUI/Find/service setup. Otherwise GR5331's running
     * BLE counter charges that setup time to the first red phase. This only
     * reanchors the status accumulator; UTC and safety clocks are untouched. */
    inktag_elapsed_clock_reset(&s_status_clock, monotonic_now_tick(),
                               INKTAG_GR_MONOTONIC_COUNTER_MASK,
                               INKTAG_GR_MONOTONIC_TICKS_PER_SECOND);
    inktag_status_init(&s_status, &effective, status_led_available_colors(),
                       status_led_output, NULL, s_status_now_ms);
    s_status_initialized = true;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_ADVERTISING,
                             true, s_status_now_ms);
    inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_STARTUP,
                          s_status_now_ms);
    s_startup_status_until_ms = s_status.transient_until;
}

static void status_led_set_connected(bool connected) {
    if (!s_status_initialized) return;
    status_led_event_clock();
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_ADVERTISING,
                             !connected, s_status_now_ms);
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_CONNECTED,
                             false, s_status_now_ms);
    if (connected) {
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_CONNECTED,
                              s_status_now_ms);
    }
}

static uint32_t diag_pin_mask(uint8_t pin) {
    return (pin >= 32) ? 0 : (1u << pin);
}

static bool diag_io_type_from_id(uint8_t type_id, app_io_type_t *io_type) {
    if (!io_type) return false;

    switch ((diag_io_type_id_t)type_id) {
        case DIAG_IO_GPIOA:
            *io_type = APP_IO_TYPE_GPIOA;
            return true;
        case DIAG_IO_AON:
            *io_type = APP_IO_TYPE_AON;
            return true;
        case DIAG_IO_MSIO:
            *io_type = APP_IO_TYPE_MSIO;
            return true;
        case DIAG_IO_NORMAL:
            *io_type = APP_IO_TYPE_NORMAL;
            return true;
        default:
            return false;
    }
}

static bool runtime_led_diag_available(void) {
    return runtime_rgb_led_available();
}

static bool diag_set_led_mask(uint8_t color_mask) {
    if (!runtime_led_diag_available() || color_mask > INKTAG_STATUS_COLOR_WHITE)
        return false;
    if (color_mask == INKTAG_STATUS_COLOR_OFF) {
        s_status_led_test_active = false;
        s_status_led_test_mask = 0u;
        status_led_reapply();
        return true;
    }
    s_status_led_test_active = true;
    s_status_led_test_mask = color_mask;
    s_status_led_test_until_ms = s_status_now_ms + DIAG_LED_TEST_MS;
    status_led_write_mask(s_status_led_test_mask);
    return true;
}

static void diag_scan_timer_handler(void *p_ctx) {
    (void)p_ctx;
    s_diag_scan_due = true;
}

static void diag_scan_finish(const char *text) {
    app_timer_stop(s_diag_scan_timer);
    s_diag_scan_active = false;
    s_diag_scan_due = false;
    s_diag_scan_custom = false;
    s_diag_scan_range = 0;
    s_diag_scan_pin = 0;
    s_diag_scan_first_pin = 0;
    s_diag_scan_phase = 0;
    status_led_reapply();
    if (text) send_text(text);
}

static void diag_scan_start(uint8_t type_id, uint8_t first_pin, uint8_t last_pin, bool custom) {
    if (s_diag_scan_active) {
        send_text("led:scan busy");
        return;
    }
    if (first_pin > last_pin) {
        uint8_t tmp = first_pin;
        first_pin = last_pin;
        last_pin = tmp;
    }
    if (last_pin > 31) {
        last_pin = 31;
    }

    s_diag_scan_active = true;
    s_diag_scan_due = true;
    s_diag_scan_custom = custom;
    s_diag_scan_custom_range.type_id = type_id;
    s_diag_scan_custom_range.max_pin = last_pin;
    s_diag_scan_range = 0;
    s_diag_scan_pin = custom ? first_pin : 0;
    s_diag_scan_first_pin = custom ? first_pin : 0;
    s_diag_scan_phase = 0;

    char buf[44];
    if (custom) {
        GFX_format(buf, sizeof(buf), "led:scan start t=%u p=%02x..%02x", type_id, first_pin, last_pin);
        send_text(buf);
    } else {
        send_text("led:scan start AON,MSIO");
    }
}

static void diag_scan_process(void) {
    if (!s_diag_scan_active || !s_diag_scan_due) {
        return;
    }

    s_diag_scan_due = false;
    char buf[40];

    uint8_t range_count = s_diag_scan_custom ? 1 : (uint8_t)(sizeof(s_diag_scan_ranges) / sizeof(s_diag_scan_ranges[0]));
    while (s_diag_scan_range < range_count) {
        const diag_scan_range_t *range = s_diag_scan_custom ? &s_diag_scan_custom_range : &s_diag_scan_ranges[s_diag_scan_range];
        app_io_type_t io_type;
        if (!diag_io_type_from_id(range->type_id, &io_type)) {
            s_diag_scan_range++;
            s_diag_scan_pin = s_diag_scan_custom ? s_diag_scan_first_pin : 0;
            s_diag_scan_phase = 0;
            continue;
        }

        if (s_diag_scan_pin > range->max_pin) {
            s_diag_scan_range++;
            s_diag_scan_pin = s_diag_scan_custom ? s_diag_scan_first_pin : 0;
            s_diag_scan_phase = 0;
            continue;
        }

        uint32_t mask = diag_pin_mask(s_diag_scan_pin);
        if (mask == 0) {
            s_diag_scan_pin++;
            s_diag_scan_phase = 0;
            continue;
        }

        if (s_diag_scan_phase == 0) {
            app_io_init_t init;
            memset(&init, 0, sizeof(init));
            init.pin = mask;
            init.mode = APP_IO_MODE_OUTPUT;
            init.pull = APP_IO_NOPULL;
            init.mux = INKTAG_GPIO_MUX;
            app_io_init(io_type, &init);

            app_io_write_pin(io_type, mask, APP_IO_PIN_SET);
            GFX_format(buf, sizeof(buf), "led:scan t=%u p=%02x hi", range->type_id, s_diag_scan_pin);
            send_text(buf);
            s_diag_scan_phase = 1;
            app_timer_start(s_diag_scan_timer, DIAG_SCAN_PULSE_MS, NULL);
            return;
        }

        app_io_write_pin(io_type, mask, APP_IO_PIN_RESET);
        GFX_format(buf, sizeof(buf), "led:scan t=%u p=%02x lo", range->type_id, s_diag_scan_pin);
        send_text(buf);
        s_diag_scan_pin++;
        s_diag_scan_phase = 0;
        app_timer_start(s_diag_scan_timer, DIAG_SCAN_PULSE_MS, NULL);
        return;
    }

    diag_scan_finish("led:scan done");
}

static bool epd_gpio_open_if_needed(void) {
    if (epd_control_access() != INKTAG_STATUS_OK) return false;
    if (!s_epd_gpio_ready) {
        EPD_GPIO_Init();
        s_epd_gpio_ready = true;
    }
    return true;
}

static void epd_gpio_close_if_needed(void) {
    if (s_epd_gpio_ready) {
        EPD_GPIO_Uninit();
        s_epd_gpio_ready = false;
    }
}

static bool ensure_epd_screen(void) {
    if (!s_epd) s_epd = epd_find(&s_screen_config);
    if (!s_epd) {
        send_text("epd:power no-screen");
        return false;
    }
    return epd_gpio_open_if_needed();
}

static bool epd_io_operation_ok(void) {
    uint16_t status = 0u;
    uint32_t errors = 0u;
    inktag_gr_epd_spi_diag(NULL, &status, &errors, NULL, NULL, NULL);
    return EPD_DriverOperationOK(s_epd) && status == APP_DRV_SUCCESS && errors == 0u;
}

#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
static bool panel_temperature_sensor_supported(const epd_model_t *epd) {
    if (!epd) return false;
    if (epd->driver_id == EPD_SCREEN_DRIVER_UC8176 ||
        epd->driver_id == EPD_SCREEN_DRIVER_UC8276 ||
        epd->driver_id == EPD_SCREEN_DRIVER_SSD1680 ||
        epd->driver_id == EPD_SCREEN_DRIVER_SSD1683 ||
        epd->driver_id == EPD_SCREEN_DRIVER_IST7513 ||
        epd->driver_id == EPD_SCREEN_DRIVER_IST7601) return true;
    if (epd->driver_id == EPD_SCREEN_DRIVER_JD79665CA)
        return epd->panel_recipe == INKTAG_RECIPE_JD79665CA_LENMORY_H364F067_E6;
    return epd->driver_id == EPD_SCREEN_DRIVER_JD79665AA &&
           (epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A0 ||
            epd->panel_recipe == INKTAG_RECIPE_JD79665AA_HUAWEI_398_A1);
}

static bool panel_temperature_sample_abort_requested(void) {
    return s_panel_temperature_sample_cancel ||
           status_deadline_reached(s_panel_temperature_deadline_ms) ||
           !inktag_gr_health_display_continue_allowed(s_status_now_ms);
}

/* One attempt per ordinary connection, never run from a BLE callback or a
 * GET_STATUS request. No RAM-plane write or refresh is issued. The exact
 * controller init/read/sleep path remains subject to physical qualification. */
static bool panel_temperature_sample_process(void) {
    if (!s_panel_temperature_sample_pending) return false;
    if (s_conn_idx == 0xFFu) {
        s_panel_temperature_sample_pending = false;
        return false;
    }






    if (
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        gr_panel_diag_busy() ||
#endif
        s_epd_work_active || s_epd_work_pending || s_picture_io_active ||
        s_diag_scan_active || s_v2_transfer.active ||
        inktag_image_store_capture_active() || s_rx_head != s_rx_tail ||
        s_system_action_pending != 0u ||
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_temperature_busy_pro.inc"
#endif
#line 2111 "../Src/user/inktag_gr_app.c"
#endif
        s_epd_gpio_ready || !background_display_startup_complete()) return false;
    if (!s_notify_enabled ||
        !status_deadline_reached(s_ble_discovery_until_ms)) {
        return false;
    }
    s_panel_temperature_sample_pending = false;
    s_chip_temperature_cache = read_chip_temperature();
    s_chip_temperature_sampled_ms = s_status_now_ms;



    if (!panel_temperature_sensor_supported(s_epd)) return true;
    /* Keep the common driver's first electrical failure; a reconnect must
     * not reset/reinitialize a failed E6 controller merely for telemetry. */
    if (!EPD_DriverOperationOK(s_epd)) return true;
    s_panel_temperature_cache.valid = false;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,
                                            true)) {
        return true;
    }
    s_panel_temperature_sample_active = true;
    s_panel_temperature_sample_cancel = false;
    s_panel_temperature_deadline_ms =
        s_status_now_ms + GR_CONNECTION_TEMPERATURE_TIMEOUT_MS;
    inktag_gr_health_display_begin();
    EPD_SetAbortCallback(panel_temperature_sample_abort_requested);
    if (ensure_epd_screen()) {
        init_panel_for_display(s_epd);
        if (EPD_AbortRequested() || !epd_io_operation_ok()) {
            s_panel_temperature_cache.valid = false;
            if (s_epd->driver_id == EPD_SCREEN_DRIVER_IST7513 ||
                s_epd->driver_id == EPD_SCREEN_DRIVER_IST7601)
                s_epd->drv->sleep(s_epd); /* common bounded POF/deep sleep */
            else EPD_Reset(true, 10u);
        } else {
            s_epd->drv->sleep(s_epd);
            delay_ms(100u);
            if (EPD_AbortRequested() || !epd_io_operation_ok())
                s_panel_temperature_cache.valid = false;
        }
    }
    epd_gpio_close_if_needed();
    EPD_SetAbortCallback(NULL);
    inktag_gr_health_display_end();
    s_panel_temperature_sample_active = false;
    return true;
}
#endif

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_identity_pro.inc"
#line 2208 "../Src/user/inktag_gr_app.c"
#endif

static void init_device_name(void) {
    ble_gap_bdaddr_t bd_addr;
    sdk_err_t address_status;
    memset(&bd_addr, 0, sizeof(bd_addr));
    address_status = ble_gap_addr_get(&bd_addr);
    /* Only the initial ordinary address is authoritative. Runtime rename can
     * call this again while Find owns the controller address. */
    if (!s_device_id4_valid && address_status == SDK_SUCCESS) {
        s_device_id4 = ((uint16_t)bd_addr.gap_addr.addr[1] << 8) |
                       bd_addr.gap_addr.addr[0];
        s_device_id4_valid = true;
    }

#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_name_boot_pro.inc"
#endif
#line 2228 "../Src/user/inktag_gr_app.c"
if (address_status == SDK_SUCCESS) {
        GFX_format(s_device_name, sizeof(s_device_name), "%s_%02X%02X",
                 DEVICE_NAME_PREFIX,
                 bd_addr.gap_addr.addr[1],
                 bd_addr.gap_addr.addr[0]);
    } else {
        GFX_format(s_device_name, sizeof(s_device_name), "%s", DEVICE_NAME_PREFIX);
    }

    uint8_t name_len = (uint8_t)strlen(s_device_name);
    if (name_len > sizeof(s_adv_rsp_data_set) - 2) {
        name_len = sizeof(s_adv_rsp_data_set) - 2;
    }
    // Keep the GR533x SDK's proven packet arrangement: the service UUID and
    // manufacturer marker are in the primary advertisement, while the
    // per-device name is returned in the scan response.
    uint8_t pos = 0;
    s_adv_data_set[pos++] = 0x11;
    s_adv_data_set[pos++] = BLE_GAP_AD_TYPE_COMPLETE_LIST_128_BIT_UUID;
    memcpy(&s_adv_data_set[pos], (const uint8_t[]){INKTAG_GR_SERVICE_UUID}, 16);
    pos += 16;
    s_adv_data_set[pos++] = 0x05;
    s_adv_data_set[pos++] = BLE_GAP_AD_TYPE_MANU_SPECIFIC_DATA;
    s_adv_data_set[pos++] = 0xF7;
    s_adv_data_set[pos++] = 0x04;
    s_adv_data_set[pos++] = 0x02;
    s_adv_data_set[pos++] = 0x03;
    s_adv_data_len = pos;
    s_adv_rsp_data_set[0] = name_len + 1;
    s_adv_rsp_data_set[1] = BLE_GAP_AD_TYPE_COMPLETE_NAME;
    memcpy(&s_adv_rsp_data_set[2], s_device_name, name_len);
    s_adv_rsp_data_len = name_len + 2;
}

static uint8_t device_display_name(char output[INKTAG_DEVICE_LABEL_MAX_BYTES + 1u]) {
    output[0] = '\0';
    if (s_device_id4_valid || s_device_settings.label_length != 0u)
        (void)inktag_device_display_name(output, INKTAG_DEVICE_LABEL_MAX_BYTES + 1u,
            s_device_settings.label, s_device_settings.label_length, s_device_id4);
    return (uint8_t)strlen(output);
}

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_address_pro.inc"
#line 2289 "../Src/user/inktag_gr_app.c"
#endif

#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_name_apply_pro.inc"
#else
static bool apply_runtime_device_name(void) { return true; }
#endif
#line 2301 "../Src/user/inktag_gr_app.c"


#if defined(INKTAG_GOODIX_OTA)
/* The running XIP application may only hand control to fixed BLE Recovery. */
bool inktag_dfu_programming_allowed(void) { return false; }

static void goodix_ota_enter(void) {
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, true)) {
        send_text("ota:low-power");
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_LOW_BATTERY,
                              s_status_now_ms);
        return;
    }
    inktag_gr_health_stage(0x47524F54u, INKTAG_GR_HEALTH_OPERATION_OTA); /* GROT */
    /* This only arms the handoff to fixed Recovery. Keep the running App's
     * normal name and LED state until DFU_MODE_SET commits the role switch.
     * Only the actual fixed Recovery may advertise the stable _OTA name. */

    char buf[40];
    GFX_format(buf, sizeof(buf), "ota=ready name=%s", s_device_name);
    send_text(buf);
}

static void goodix_ota_program_start(void) {
    /* PROGRAM_START is metadata, not proof that the host sent a data block.
     * The running XIP App rejects programming; fixed Recovery owns download. */
}

static void goodix_ota_programming(uint8_t progress) {
    (void)progress;
    if (!s_status_initialized) return;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_OTA,
                             true, s_status_now_ms);
}

static void goodix_ota_program_end(uint8_t status) {
    if (!s_status_initialized) return;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_OTA,
                             false, s_status_now_ms);
    if (status == 1u) {
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_OTA_INSTALL,
                                 true, s_status_now_ms);
    } else {
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_ERROR,
                              s_status_now_ms);
    }
}

dfu_pro_callback_t g_inktag_gr_dfu_callbacks = {
    .dfu_program_start_callback = goodix_ota_program_start,
    .dfu_programing_callback = goodix_ota_programming,
    .dfu_program_end_callback = goodix_ota_program_end,
#if defined(INKTAG_GOODIX_GR5331)
    .dfu_flash_copy_process = NULL,
#endif
};
#endif

typedef struct {
    epd_model_t *epd;
    bool black_plane;
    bool begin;
} epd_gui_ram_plane_t;

static bool display_state_flag(uint16_t flag) {
    return s_display_state_loaded && (s_display_state.reserved & flag) != 0u;
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_brand_hidden_request_pro.inc"
#else
static bool brand_hidden_requested(void) { return false; }
#endif
#line 2372 "../Src/user/inktag_gr_app.c"


static void display_settings_default(inktag_display_settings_t *settings) {
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    settings->week_start = s_config.week_start <= 1u ?
        s_config.week_start : 0u;
    if (s_display_state_loaded) {
        settings->flags = (uint8_t)(s_display_state.reserved &
            (EPD_DISPLAY_STATE_USER_FLAGS_MASK |
             EPD_DISPLAY_STATE_PRO_FLAGS_MASK));
    }
}

static bool display_settings_read(inktag_display_settings_t *settings) {
    uint8_t wire[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                 INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    uint16_t length = sizeof(wire);
    if (settings == NULL ||
        inktag_gr_settings_get(INKTAG_GR_SETTINGS_SLOT_DISPLAY_SETTINGS,
                               &length, wire) !=
            INKTAG_GR_SETTINGS_READ_OK ||
        !inktag_display_settings_decode(wire, length, settings)) {
        return false;
    }
    return settings->name_length == 0u ||
           GUI_CountdownNameSupported(settings->name,
                                      settings->name_length);
}

static bool display_settings_write(
    const inktag_display_settings_t *settings) {
    uint8_t wire[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                 INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    size_t length = inktag_display_settings_encode(settings, wire,
                                                   sizeof(wire));
    return length != 0u &&
           inktag_gr_health_operation_allowed(
               INKTAG_POWER_OPERATION_FLASH, true) &&
           inktag_gr_settings_put(
               INKTAG_GR_SETTINGS_SLOT_DISPLAY_SETTINGS,
               (uint16_t)length, wire);
}

static void display_settings_apply_runtime(
    const inktag_display_settings_t *settings) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_brand_hidden_apply_pro.inc"
#else
    const uint16_t mutable_flags = EPD_DISPLAY_STATE_USER_FLAGS_MASK;
#endif
#line 2421 "../Src/user/inktag_gr_app.c"
    if (settings == NULL) return;
    s_config.week_start = settings->week_start;
    if (s_display_state_loaded) {
        s_display_state.reserved = (uint16_t)(
            (s_display_state.reserved & (uint16_t)~mutable_flags) |
            (settings->flags & mutable_flags));
    }
}

INKTAG_GUARD_C static inktag_brand_edition_t image_brand_edition(void) {
    return inktag_access_brand_edition(&s_activation,
                                       brand_hidden_requested());
}

/* SET_TIME must invalidate a native frame only after its clock record commits.
 * Use the common elapsed-period decision and the existing deferred redraw,
 * never an extra display transaction or a connection-triggered redraw loop. */
static void native_time_sync_applied(uint32_t previous_timestamp,
                                     uint8_t previous_week_start,
                                     bool previous_time_valid) {
    uint8_t mode = s_display_state.mode;
    bool changed;
    if (s_slideshow_active) return;
    if (mode == MODE_CALENDAR) {
        changed = !previous_time_valid ||
            previous_week_start != s_config.week_start ||
            inktag_elapsed_clock_crossed_period(
                previous_timestamp, s_timestamp, 86400u);
    } else if (mode == MODE_CLOCK) {
        changed = !previous_time_valid ||
            inktag_elapsed_clock_crossed_period(
                previous_timestamp, s_timestamp, 60u);
    } else return;
    if (!changed && !s_native_refresh_pending) return;
    s_native_refresh_pending = true;
    s_native_refresh_mode = mode;
    s_native_refresh_timestamp = s_timestamp;
    /* This only requests a scheduler turn. It creates no additional timer;
     * panel/transfer/startup/low-power ownership still controls execution. */
    s_clock_tick_due = true;
}

static bool status_deadline_reached(uint32_t deadline_ms) {
    return (int32_t)(s_status_now_ms - deadline_ms) >= 0;
}

static bool background_display_startup_complete(void) {
    return !runtime_rgb_led_available() ||
           status_deadline_reached(s_startup_status_until_ms);
}

static void status_led_mark_application_ready(void) {
    status_led_event_clock();
    bool startup_active =
        s_status.transient_scene == INKTAG_STATUS_SCENE_STARTUP &&
        !status_deadline_reached(s_status.transient_until);

    inktag_status_role_ready(&s_status, INKTAG_STATUS_READY_APPLICATION,
                             s_status_now_ms);
    /* Background panel work must not preempt either half of the fixed boot
     * indication.  When readiness arrives during STARTUP, the common status
     * machine queues a 120 ms separator plus three 120/120 ms green pulses;
     * transient_until still describes only the RGB round until its next tick.
     * Extend the port's display gate over that queued tail as well. */
    s_startup_status_until_ms = startup_active
        ? s_status.transient_until + INKTAG_STATUS_READY_HALF_MS * 7u
        : s_status.transient_until;
}

static void process_pending_system_action(void) {
    uint8_t action;
    if (s_system_action_pending == 0u ||
        !status_deadline_reached(s_system_action_not_before_ms)) return;
    action = s_system_action_pending;
    s_system_action_pending = 0u;
    s_system_action_not_before_ms = 0u;
    if (action == INKTAG_SYSTEM_ACTION_RESET ||
        action == INKTAG_SYSTEM_ACTION_RESET_SETTINGS) {
        hal_nvic_system_reset();
    }
}

#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
#include "inktag_gr_advertising_pro.inc"
#endif

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_advertising_pro.inc"
#line 2541 "../Src/user/inktag_gr_app.c"
#endif

static void restart_advertising_if_due(void) {
    if (!s_adv_restart_due || s_conn_idx != 0xFFu ||
        !status_deadline_reached(s_adv_restart_not_before_ms)) {
        return;
    }
    s_adv_restart_due = false;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_restart_pro.inc"
#line 2554 "../Src/user/inktag_gr_app.c"
#else
    sdk_err_t error_code = ble_gap_adv_start(INKTAG_ADV_INDEX, &s_gap_adv_time_param);
#endif
    boot_error(0x47524139, error_code); // GRA9
    if (error_code != SDK_SUCCESS) {
        s_adv_restart_due = true;
        s_adv_restart_not_before_ms = s_status_now_ms + 500u;
    }
}

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_mux_pro.inc"
#line 2610 "../Src/user/inktag_gr_app.c"
#endif

static void disconnect_if_notifications_closed(void) {
    if (!s_ble_disconnect_due) return;
    uint8_t conn_idx = s_ble_disconnect_idx;
    s_ble_disconnect_due = false;
    s_ble_disconnect_idx = 0xFFu;
    if (conn_idx != 0xFFu && s_conn_idx == conn_idx && !s_notify_enabled) {
        (void)ble_gap_disconnect(conn_idx);
    }
}

static bool record_display_state(uint8_t mode, uint8_t argument, uint32_t interval_seconds) {
    epd_display_state_t next = {
        .mode = mode,
        .argument = argument,
        .reserved = s_display_state_loaded ? s_display_state.reserved : 0u,
        .interval_seconds = interval_seconds,
    };
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        return false;
    }
    if (!s_display_state_loaded || memcmp(&next, &s_display_state, sizeof(next)) != 0) {
        if (epd_display_state_write(&next)) {
            s_display_state = next;
            s_display_state_loaded = true;
            return true;
        }
        return false;
    }
    return true;
}

static void epd_gui_write_ram_plane(void *user_data, uint8_t *black, uint8_t *color,
                                    uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    epd_gui_ram_plane_t *plane = (epd_gui_ram_plane_t *)user_data;
    (void)color;
    if (!plane || !plane->epd || !black) return;
    uint16_t ram_width = (plane->epd->width + 7u) & ~7u;
    if (x != 0 || w != ram_width || y >= plane->epd->height || h == 0) return;

    uint32_t len = ((uint32_t)(w + 7) / 8) * h;
    while (len > 0) {
        if (EPD_AbortRequested()) return;
        app_feed_wdt();
        uint8_t chunk = len > UINT8_MAX ? UINT8_MAX : (uint8_t)len;
        (void)EPD_DriverWriteRam(plane->epd, plane->begin,
                                 plane->black_plane, black, chunk);
        plane->begin = false;
        black += chunk;
        len -= chunk;
    }
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_clock_partial_pro.inc"
#endif

static bool epd_gui_update(display_mode_t mode, uint32_t draw_timestamp) {
    if (!epd_gpio_open_if_needed()) return false;
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
    /* A proposed region is not panel history. Publish only after actual full
     * refresh succeeds; no partial-refresh admission or previous frame is kept. */
    gui_fixed_region_t proposed_region = {0};
#endif
    int8_t temperature = GUI_TEMPERATURE_UNAVAILABLE;
    gui_countdown_t countdown = {
        .name = s_display_settings.name,
        .target_day = s_display_settings.target_day,
    };

    char screen_info[32];
    const char *color_name = s_epd->color == COLOR_BW ? "BW" :
                             s_epd->color == COLOR_BWR ? "BWR" :
                             s_epd->color == COLOR_BWRY ? "BWRY" :
                             s_epd->color == COLOR_BWRYGB ? "BWRYGB" : "COLOR";
    GFX_format(screen_info, sizeof(screen_info), "%ux%u / %s",
             s_epd->visible_width, s_epd->visible_height, color_name);

    gui_data_t data = {
        .mode = mode,
        .color = s_epd->color,
        .width = s_epd->width,
        .height = s_epd->height,
#if defined(INKTAG_GUI_RAW_VIEWPORT)
        .visible_x = s_epd->visible_x,
        .visible_y = s_epd->visible_y,
#endif
        .visible_width = s_epd->visible_width,
        .visible_height = s_epd->visible_height,
        .force_rotation = true,
        .rotation = (GFX_Rotate)(s_epd->transform & EPD_SCREEN_TRANSFORM_ROTATION_MASK),
        .mirror_x = (s_epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_X) != 0u,
        .mirror_y = (s_epd->transform & EPD_SCREEN_TRANSFORM_MIRROR_Y) != 0u,
        .native_ui_scale = s_screen_config.native_ui_scale,
        .timestamp = draw_timestamp,
        .week_start = s_config.week_start,
        .countdown = s_display_settings.target_day != 0u ?
            &countdown : NULL,
        /* Static live GAP storage survives all synchronous pages/planes.
         * Do not substitute the independent label when GAP is unavailable. */
        .bluetooth_name = s_device_name[0] ? s_device_name : "--",
        .activated = false,
        .temperature = temperature,
        .voltage = EPD_ReadVoltage(),
        .mcu_name = inktag_mcu_display_name(),
        .screen_info = screen_info,
        .activated = inktag_activation_is_pro(&s_activation),
        .brand_hidden = !inktag_access_brand_should_render(
            &s_activation, brand_hidden_requested()),
    };
    if (s_device_id4_valid || s_device_settings.label_length != 0u)
        (void)GUI_SetDeviceName(&data, s_device_settings.label,
                                s_device_settings.label_length, s_device_id4);
    data.device_name = data.ssid;
    if (mode == MODE_DEVICE_INFO) {
        uint8_t modules;
        uint16_t qr_length;
        const uint8_t *qr_data;
        data.device_info_qr_data[0] = inktag_default_device_qr;
        data.device_info_qr_modules[0] = INKTAG_DEFAULT_DEVICE_QR_MODULES;
        if (inktag_gr_device_info_qr_read(0u, &modules, &qr_data,
                                          &qr_length)) {
            data.device_info_qr_data[0] = qr_data;
            data.device_info_qr_modules[0] = modules;
        }
    }

    init_panel_for_display(s_epd);
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    s_chip_temperature_cache = read_chip_temperature();
    s_chip_temperature_sampled_ms = s_status_now_ms;
#endif
    (void)read_effective_temperature(&data.temperature);

#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    /* Recheck entitlement at actual foreground/background dispatch. base
     * takes the unchanged ordinary full-refresh path, never a denied clock. */
    if (mode==MODE_CLOCK && gr_clock_partial_available())
        return gr_clock_partial_update(&data);
    gr_clock_partial_invalidate();
#endif
    send_diag("gui:start");
#if INKTAG_EPD_ENABLE_JD79665CA
    if (s_epd->transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) {
        gui_options_t area0_options = {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
            .fixed_region = &proposed_region,
#endif
            .color_plane = GFX_COLOR_PLANE_E6_AREA0,
            .force_buffer_color = true,
            .buffer_color = COLOR_BWRY,
        };
        epd_gui_ram_plane_t area0 = {.epd = s_epd, .black_plane = true, .begin = true};
        send_diag("gui:area0");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &area0, &area0_options)) return false;
        app_feed_wdt();
        if (EPD_AbortRequested()) return false;

        gui_options_t area6_options = {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
            .fixed_region = &proposed_region,
#endif
            .color_plane = GFX_COLOR_PLANE_E6_AREA6,
            .force_buffer_color = true,
            .buffer_color = COLOR_BWRY,
        };
        epd_gui_ram_plane_t area6 = {.epd = s_epd, .black_plane = false, .begin = true};
        send_diag("gui:area6");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &area6, &area6_options)) return false;
        app_feed_wdt();
    } else
#endif
    if ((s_epd->ic == DRV_IC_UC8151 || s_epd->ic == DRV_IC_SSD1680) && s_epd->color == COLOR_BWR) {
        gui_options_t black_options = {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
            .fixed_region = &proposed_region,
#endif
            .color_plane = GFX_COLOR_PLANE_BLACK,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
            .prefer_full_frame = true,
        };
        epd_gui_ram_plane_t black_plane = {.epd = s_epd, .black_plane = true, .begin = true};
        send_diag("gui:black");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &black_plane, &black_options)) return false;
        app_feed_wdt();
        if (EPD_AbortRequested()) return false;

        gui_options_t red_options = {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
            .fixed_region = &proposed_region,
#endif
            .color_plane = GFX_COLOR_PLANE_RED,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
            .prefer_full_frame = true,
        };
        epd_gui_ram_plane_t red_plane = {.epd = s_epd, .black_plane = false, .begin = true};
        send_diag("gui:red");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &red_plane, &red_options)) return false;
        app_feed_wdt();
    } else {
        send_diag("gui:draw");
        /* GR5331 and GR551x have enough SRAM for their registered native
         * frame.  Rendering it once avoids repeating the complete layout for
         * every strip; larger future profiles still fall back to pages. */
        gui_options_t full_frame_options = {
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
            .fixed_region = &proposed_region,
#endif
            .prefer_full_frame = true,
        };
        if (!DrawGUIEx(&data, (buffer_callback)s_epd->drv->write_image,
                       s_epd, &full_frame_options)) return false;
        app_feed_wdt();
    }

    if (EPD_AbortRequested()) return false;
    send_diag("gui:refresh");
    s_epd->drv->refresh(s_epd);
    if (EPD_AbortRequested()) return false;
#if defined(INKTAG_GUI_ROLE_FONT_ASSETS)
    if (proposed_region.field.digit_count != 0u) {
        char diagnostic[64];
        GFX_format(diagnostic, sizeof(diagnostic), "gui:font:s%u,g%u,t%u,q%u",
            (unsigned)proposed_region.key.font_schema,
            (unsigned)proposed_region.key.font_generation,
            (unsigned)proposed_region.key.font_tier,
            (unsigned)proposed_region.key.font_scale_q8);
        send_diag(diagnostic);
        GFX_format(diagnostic, sizeof(diagnostic), "gui:fixed:%d,%d,%u,%u",
            (int)proposed_region.physical.x, (int)proposed_region.physical.y,
            (unsigned)proposed_region.physical.width,
            (unsigned)proposed_region.physical.height);
        send_diag(diagnostic);
    }
#endif
    return true;
}

static bool ghost_clean_fill_plane(bool black_plane, uint8_t value,
                                   uint32_t length) {
    uint8_t buffer[64];
    bool begin = true;
    memset(buffer, value, sizeof(buffer));
    while (length > 0u) {
        if (EPD_AbortRequested()) return false;
        uint8_t chunk = length > sizeof(buffer) ? sizeof(buffer) :
                                                  (uint8_t)length;
        if (!EPD_DriverWriteRam(s_epd, begin, black_plane, buffer, chunk))
            return false;
        begin = false;
        length -= chunk;
        app_feed_wdt();
    }
    return true;
}

static bool ghost_clean_write_solid(bool white) {
    epd_screen_t resolved;
    uint32_t plane_bytes;
    uint8_t value;

    if (!s_epd ||
        epd_screen_resolve(&s_screen_config, &resolved) != EPD_SCREEN_STATUS_OK) {
        return false;
    }

    plane_bytes = resolved.frame_bytes;
    switch (resolved.transfer_format) {
        case EPD_SCREEN_TRANSFER_DUAL_1BPP:
            plane_bytes /= 2u;
            if (!ghost_clean_fill_plane(true, white ? 0xFFu : 0x00u,
                                        plane_bytes)) {
                return false;
            }
            return ghost_clean_fill_plane(false, 0xFFu, plane_bytes);
        case EPD_SCREEN_TRANSFER_PLANAR_2X1BPP:
            plane_bytes /= 2u;
            value = white ? 0xFFu : 0x00u;
            if (!ghost_clean_fill_plane(true, value, plane_bytes)) return false;
            return ghost_clean_fill_plane(false, value, plane_bytes);
        case EPD_SCREEN_TRANSFER_INDEXED_2X2BPP:
            plane_bytes /= 2u;
            value = white ? 0x55u : 0x00u;
            if (!ghost_clean_fill_plane(true, value, plane_bytes)) return false;
            return ghost_clean_fill_plane(false, value, plane_bytes);
        case EPD_SCREEN_TRANSFER_INDEXED_2BPP:
            return ghost_clean_fill_plane(true, white ? 0x55u : 0x00u,
                                          plane_bytes);
        case EPD_SCREEN_TRANSFER_INDEXED_4BPP:
            return ghost_clean_fill_plane(true, white ? 0x11u : 0x00u,
                                          plane_bytes);
        case EPD_SCREEN_TRANSFER_MONO_1BPP:
        default:
            return ghost_clean_fill_plane(true, white ? 0xFFu : 0x00u,
                                          plane_bytes);
    }
}

static bool ghost_clean_pass(bool white) {
    /* Some controllers (notably Hanshow JD79668AA) power off after every
     * refresh. A multi-pass clean must re-arm PON before the next RAM write;
     * one init for the whole operation lets later passes report progress
     * without an actual optical refresh. */
    init_panel_for_display(s_epd);
    if (EPD_AbortRequested()) return false;
    if (!ghost_clean_write_solid(white) || EPD_AbortRequested()) return false;
    s_epd->drv->refresh(s_epd);
    send_diag("ghost-clean:pass");
    return !EPD_AbortRequested() && EPD_BusyDiagAsserted() &&
           !s_epd->drv->read_busy(s_epd);
}

static void ghost_clean_send_progress(uint8_t completed_passes,
                                      uint8_t total_passes) {
    if (s_v2_work_transaction == 0u || total_passes == 0u) return;
    uint8_t progress[2] = {
        INKTAG_BUSY_PHASE_DISPLAY,
        (uint8_t)(((uint16_t)completed_passes * 100u) / total_passes),
    };
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  s_v2_work_opcode, s_v2_work_transaction,
                  progress, sizeof(progress));
}

static bool ghost_clean_run(uint8_t cycles) {
    if (epd_control_access() != INKTAG_STATUS_OK) return false;
    uint8_t completed_passes = 0u;
    uint8_t total_passes = (uint8_t)(cycles * 3u);
    bool success = true;
    bool panel_white = false;

    for (uint8_t cycle = 0u; cycle < cycles && success; ++cycle) {
        panel_white = false;
        success = ghost_clean_pass(true);
        if (!success) break;
        panel_white = true;
        ghost_clean_send_progress(++completed_passes, total_passes);

        panel_white = false;
        success = ghost_clean_pass(false);
        if (!success) break;
        ghost_clean_send_progress(++completed_passes, total_passes);

        success = ghost_clean_pass(true);
        if (!success) break;
        panel_white = true;
        ghost_clean_send_progress(++completed_passes, total_passes);
    }

    /* If cancellation or a failed pass leaves a black/unknown image, make one
     * bounded best-effort return to white. Preserve the original result and
     * ignore only the client's cancel flag during this cleanup. */
    if (!panel_white && s_epd && !s_epd_low_power_abort) {
        s_epd_ghost_cleanup = true;
        EPD_Reset(true, 10u);
        (void)ghost_clean_pass(true);
        s_epd_ghost_cleanup = false;
    }
    return success;
}

static bool image_store_show(uint8_t id) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result = inktag_image_store_get(id, &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_image_store_error("show", result);
        return false;
    }
    if (!image_store_record_visible(id)) {
        send_text("img:show:error=denied");
        return false;
    }
    epd_model_t *model = image_store_model();
    if (!model || !screen_config_matches(header.screen_config)) {
        send_text("img:show:error=incompatible");
        return false;
    }

    char buf[48];
    GFX_format(buf, sizeof(buf), "img:show:start id=%u", id);
    send_text(buf);
    if (!epd_gpio_open_if_needed()) return false;




    if (!s_epd) s_epd = epd_init(&s_screen_config);

    if (!s_epd) {
        send_text("img:show:error=no-screen");
        epd_gpio_close_if_needed();
        return false;
    }
    init_panel_for_display(s_epd);

    uint8_t data[244];
    uint32_t offset = 0;
    for (uint8_t segment = 0; segment < header.segment_count; segment++) {
        uint32_t remaining = header.segment_size[segment];
        uint32_t segment_offset = 0u;
        bool begin = true;
        bool black = (header.black_mask & (1u << segment)) != 0;
        while (remaining > 0) {
            if (EPD_AbortRequested()) return false;
            uint16_t chunk = remaining > sizeof(data) ? sizeof(data) : (uint16_t)remaining;
            result = inktag_image_store_read(id, offset, data, chunk);
            if (result != INKTAG_IMAGE_STORE_OK) {
                send_image_store_error("show", result);
                s_epd->drv->sleep(s_epd);
                epd_gpio_close_if_needed();
                return false;
            }
            if (!inktag_brand_overlay_apply_oriented(
                    &s_screen_config, header.format, segment, segment_offset,
                    data, chunk, image_brand_edition(),
                    inktag_image_reading_rotation(header.black_mask))) {
                send_text("img:show:error=brand-overlay");
                s_epd->drv->sleep(s_epd);
                epd_gpio_close_if_needed();
                return false;
            }
            if (!EPD_DriverWriteRam(
                    s_epd, begin, black, data, (uint8_t)chunk)) {
                s_epd->drv->sleep(s_epd);
                epd_gpio_close_if_needed();
                return false;
            }
            begin = false;
            offset += chunk;
            segment_offset += chunk;
            remaining -= chunk;
            app_feed_wdt();
        }
    }
    GFX_format(buf, sizeof(buf), "img:show:refresh id=%u", id);
    send_text(buf);
    s_epd->drv->refresh(s_epd);
    return !EPD_AbortRequested();
}

static bool schedule_epd_work_internal(epd_work_type_t type, uint8_t arg,
                                       const char *queued_text, bool background,
                                       epd_work_origin_t origin) {
    inktag_status_v2_t access = type == EPD_WORK_DIAG_SCAN ?
        INKTAG_STATUS_OK : epd_control_access();
    if (access != INKTAG_STATUS_OK) {
        if (!background) send_text(access == INKTAG_STATUS_DENIED ? "epd:denied" : "epd:unsupported");
        return false;
    }
    if (s_diag_scan_active
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        || gr_panel_diag_busy()
#endif
        ) {
        if (!background) send_text("epd:busy");
        return false;
    }

    bool replace_background = !background && s_epd_work_pending &&
                              s_epd_work_background && !s_epd_work_active;
    if ((s_epd_work_active || s_epd_work_pending) && !replace_background) {
        if (!background) send_text("epd:busy");
        return false;
    }
    if (!background) {
        /* A foreground page supersedes a latched automatic native redraw. */
        s_native_refresh_pending = false;
    }
    s_epd_work_type = type;
    s_epd_work_arg = arg;
    s_epd_work_background = background;
    s_epd_work_origin = origin;
    s_epd_work_pending = true;
    if (queued_text) send_text(queued_text);
    return true;
}

static bool schedule_epd_work(epd_work_type_t type, uint8_t arg, const char *queued_text) {
    return schedule_epd_work_internal(type, arg, queued_text, false,
                                      EPD_WORK_ORIGIN_BLE_CLIENT);
}

static bool schedule_local_epd_work(epd_work_type_t type, uint8_t arg) {
    return schedule_epd_work_internal(type, arg, NULL, false,
                                      EPD_WORK_ORIGIN_LOCAL_INPUT);
}

static bool schedule_background_epd_work(epd_work_type_t type, uint8_t arg) {
    return schedule_epd_work_internal(type, arg, NULL, true,
                                      EPD_WORK_ORIGIN_AUTOMATIC);
}

static void discard_pending_background_epd_work(void) {
    if (!s_epd_work_pending || !s_epd_work_background || s_epd_work_active)
        return;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_work_arg = 0u;
    s_epd_work_timestamp = 0u;
    s_epd_work_pending = false;
    s_epd_work_background = false;
    s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
}

static bool stored_image_replayable(uint8_t id, bool slideshow_only) {
    inktag_image_store_header_t header;
    return id != UINT8_MAX && image_store_model() != NULL &&
           inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK &&
           image_store_record_visible(id) &&
           screen_config_matches(header.screen_config) &&
           (!slideshow_only || header.content_type == INKTAG_IMAGE_CONTENT_IMAGE);
}

static void schedule_boot_display_restore(void) {
    inktag_boot_display_target_t target = s_boot_display_target;
    s_boot_display_target = INKTAG_BOOT_DISPLAY_NONE;
    if (target == INKTAG_BOOT_DISPLAY_DEVICE_INFO) {
        s_epd_work_timestamp = s_timestamp;
        (void)schedule_background_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO);
        return;
    }
    if (target != INKTAG_BOOT_DISPLAY_REPLAY || !s_display_state_loaded) return;
    if (s_display_state.mode == EPD_DISPLAY_STATE_CALENDAR ||
        s_display_state.mode == EPD_DISPLAY_STATE_CLOCK ||
        s_display_state.mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        s_epd_work_timestamp = s_timestamp;
        uint8_t gui_mode = s_display_state.mode == EPD_DISPLAY_STATE_DEVICE_INFO ?
                           MODE_DEVICE_INFO : s_display_state.mode;
        (void)schedule_background_epd_work(EPD_WORK_GUI, gui_mode);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_CLEAR) {
        uint8_t clear_white = s_display_state.argument <= 1u ? s_display_state.argument : 1u;
        (void)schedule_background_epd_work(EPD_WORK_CLEAR, clear_white);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_PICTURE) {
        if (stored_image_replayable(s_display_state.argument, false)) {
            (void)schedule_background_epd_work(EPD_WORK_IMAGE_SHOW,
                                               s_display_state.argument);
        }
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = s_display_state.argument;
        uint32_t interval = s_display_state.interval_seconds;
        if (interval < 30u || interval > 86400u) interval = 300u;
        if (!stored_image_replayable(id, true) && !slideshow_select_next(&id)) return;
        if (!schedule_background_epd_work(EPD_WORK_IMAGE_SHOW, id)) return;
        s_slideshow_interval = interval;
        s_slideshow_active = true;
        slideshow_arm_next_after_refresh();
    }
    /* A picture without a valid stored source is intentionally not replayed:
     * initializing the controller would destroy retained e-paper contents. */
}

#if defined(INKTAG_GOODIX_GR5515)
static void stop_current_operation(void) {
    slideshow_stop(true);
    s_native_refresh_pending = false;
    s_boot_restore_pending = s_boot_restore_due = false;
    if (s_epd_work_pending)
        send_v2_work_complete(INKTAG_STATUS_CANCELLED, s_epd_work_arg);
    s_epd_work_pending = false;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
    s_epd_cancel_requested = true;
    if (s_v2_transfer.active) v2_transfer_reset(true);
}

static bool restore_input_display(const epd_display_state_t *state) {
    if (!state) return false;
    if (state->mode == EPD_DISPLAY_STATE_CALENDAR ||
        state->mode == EPD_DISPLAY_STATE_CLOCK) {
        s_epd_work_timestamp = s_timestamp;
        return schedule_local_epd_work(EPD_WORK_GUI, state->mode);
    }
    if (state->mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        return schedule_local_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO);
    }
    if (state->mode == EPD_DISPLAY_STATE_CLEAR) {
        return schedule_local_epd_work(
            EPD_WORK_CLEAR, state->argument <= 1u ? state->argument : 1u);
    }
    if (state->mode == EPD_DISPLAY_STATE_PICTURE &&
        stored_image_replayable(state->argument, false)) {
        return schedule_local_epd_work(EPD_WORK_IMAGE_SHOW, state->argument);
    }
    if (state->mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = state->argument;
        if (!stored_image_replayable(id, true) &&
            !image_store_next_visible_compatible_no_wrap(UINT8_MAX, true, &id))
            return false;
        if (!schedule_local_epd_work(EPD_WORK_IMAGE_SHOW, id)) return false;
        uint32_t interval = state->interval_seconds;
        s_slideshow_interval = interval >= 30u && interval <= 86400u ?
                               interval : 300u;
        s_slideshow_active = true;
        s_slideshow_next_at = 0u; /* rearmed after successful refresh */
        s_slideshow_advance_pending = false;
        return true;
    }
    return false;
}

/* Common owns the four logical pages; this adapter only maps queued work.
 * Rotation and saved interval are untouched if the foreground queue refuses. */
static bool schedule_page_cycle_target(inktag_page_cycle_target_t target,
                                        uint8_t first_id) {
    epd_work_type_t type = EPD_WORK_GUI;
    uint8_t argument;
    switch (target) {
        case INKTAG_PAGE_CYCLE_CALENDAR: argument = MODE_CALENDAR; break;
        case INKTAG_PAGE_CYCLE_CLOCK: argument = MODE_CLOCK; break;
        case INKTAG_PAGE_CYCLE_DEVICE_INFO: argument = MODE_DEVICE_INFO; break;
        case INKTAG_PAGE_CYCLE_SLIDESHOW:
            if (first_id == UINT8_MAX) return false;
            type = EPD_WORK_IMAGE_SHOW;
            argument = first_id;
            break;
        default: return false;
    }
    if (!schedule_local_epd_work(type, argument)) return false;
    slideshow_stop(true);
    s_input_slideshow_return_valid = false;
    s_epd_work_timestamp = s_timestamp;
    if (target == INKTAG_PAGE_CYCLE_SLIDESHOW) {
        if (s_slideshow_interval < 30u || s_slideshow_interval > 86400u)
            s_slideshow_interval = 300u;
        s_slideshow_active = true;
        s_slideshow_next_at = 0u; /* interval starts after successful refresh */
    }
    return true;
}

static void process_page_cycle_action(bool previous) {
    epd_display_state_t current;
    const epd_display_state_t *current_ptr = NULL;
    uint8_t first_id = UINT8_MAX;
    bool images_available = image_store_next_visible_compatible_no_wrap(
        UINT8_MAX, true, &first_id);
    if (s_slideshow_active) {
        memset(&current, 0, sizeof(current));
        current.mode = EPD_DISPLAY_STATE_SLIDESHOW;
        current.argument = s_slideshow_current_id;
        current_ptr = &current;
    } else if (s_display_state_loaded) {
        current_ptr = &s_display_state;
    }
    inktag_page_cycle_target_t target = previous ?
        inktag_page_cycle_previous(current_ptr, images_available, false) :
        inktag_page_cycle_next(current_ptr, images_available, false);
    (void)schedule_page_cycle_target(target, first_id);
}

static void process_next_page_action(void) {
    process_page_cycle_action(false);
}

static void process_previous_page_action(void) {
    process_page_cycle_action(true);
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_input_actions_pro.inc"
#else
static void process_input_action(const inktag_input_binding_t*b,inktag_input_source_t s){(void)b;(void)s;}
#endif
#line 3350 "../Src/user/inktag_gr_app.c"





#line 3369 "../Src/user/inktag_gr_app.c"


#line 3393 "../Src/user/inktag_gr_app.c"

#if !INKTAG_BUILD_PRO
static bool input_action_busy(void){return false;}
#endif
#line 3403 "../Src/user/inktag_gr_app.c"

#if INKTAG_BUILD_PRO
#include "inktag_gr_nfc_commands_pro.inc"

#endif
#line 3411 "../Src/user/inktag_gr_app.c"






#line 3424 "../Src/user/inktag_gr_app.c"






#line 3438 "../Src/user/inktag_gr_app.c"






#line 3472 "../Src/user/inktag_gr_app.c"






#line 3499 "../Src/user/inktag_gr_app.c"


#if INKTAG_GR_NFC_STREAM_PILOT
/* Compile-time NFC increment, no new state or dispatch layer. */
#line 3502 "../Src/user/inktag_gr_app.c"
#include "inktag_gr_nfc_image_pro.inc"
#line 3503 "../Src/user/inktag_gr_app.c"
#endif

#if INKTAG_BUILD_PRO
#include "inktag_gr_process_nfc_input_pro.inc"

#endif
#line 3585 "../Src/user/inktag_gr_app.c"

#endif


#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE && INKTAG_BUILD_PRO
#include "inktag_gr_custom_input_poll_pro.inc"
#endif


void inktag_gr_app_process(void) {
    app_feed_wdt();
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    pin_config_quarantine();
#endif
    inktag_gr_status_led_poll();
    clock_process();
    /* One RAM counter per application scheduler/service turn. This is not an
     * RF packet count and is never persisted by the device. */
    inktag_soak_telemetry_radio_maintenance();
    inktag_gr_health_process(s_status_now_ms);
    v2_find_owner_service();
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    gr_panel_diag_service();
#endif
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
    process_aux_io();
#endif
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_12_pro.inc"
#line 3610 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_BUILD_PRO
    process_nfc_input();
#endif
#endif
    process_pending_system_action();
    disconnect_if_notifications_closed();
    if (s_link_cleanup_due) {
        s_link_cleanup_due = false;
        if (s_v2_transfer.active) v2_transfer_reset(true);
    }
    if (s_v2_transfer.active &&
        status_deadline_reached(s_v2_transfer.last_activity_ms + 30000u)) {
        uint8_t expired_transaction = s_v2_transfer.transaction_id;
        v2_transfer_reset(true);
        send_v2_event(INKTAG_EVENT_ERROR, INKTAG_STATUS_TIMEOUT,
                      INKTAG_CMD_TRANSFER_WRITE, expired_transaction, NULL, 0);
    }
    /* Restart advertising outside the disconnect callback. A failed immediate
     * restart otherwise leaves a healthy tag permanently undiscoverable; the
     * bounded retry remains independent of display work. */
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
    advertising_service(true);
#else
    restart_advertising_if_due();
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_poll_pro.inc"
#line 3625 "../Src/user/inktag_gr_app.c"
#endif
#endif
#if defined(INKTAG_GOODIX_OTA)
    if (s_swap_confirmation_pending && s_swap_confirmation_due &&
        inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, false)) {
        /* Flash mutation must run from the application loop, not from a BLE
         * stack event callback.  Goodix delivers GAP events while servicing
         * the controller; blocking that callback to program external Flash
         * can reset the trial image before it records confirmation. */
        s_swap_confirmation_pending = false;
        s_swap_confirmation_due = false;
        boot_stage(0x47524330); // GRC0: entering swap confirmation
        inktag_swap_result_t result = inktag_gr_swap_confirm();
        bool confirmed_trial = result != INKTAG_SWAP_NOT_PENDING;
        // A factory image has no trial transaction to confirm. Advertising is
        // already healthy in that case, so do not leave a benign state in the
        // boot error field where diagnostics would report it as a failure.
        if (result == INKTAG_SWAP_NOT_PENDING) {
            result = INKTAG_SWAP_OK;
        }
        if (result == INKTAG_SWAP_OK) {
            /* The common boot continuation is the only readiness indication:
             * it queues behind the uninterrupted RGB STARTUP round and emits
             * the fixed green application-ready tail after advertising and
             * any pending trial confirmation are both complete.  A normal
             * SUCCESS transient here used to be dropped by STARTUP, leaving
             * Goodix cold boots with no role-ready indication. */
            status_led_mark_application_ready();
        } else if (confirmed_trial) {
            inktag_status_trigger(
                &s_status, INKTAG_STATUS_SCENE_ERROR, s_status_now_ms);
        }
        boot_error(0x47524331, (sdk_err_t)result); // GRC1
    }
    dfu_schedule();
#endif
    /* Goodix delivers GATT writes from its BLE event context. Keep panel and
     * external-Flash work out of that context so long image-store operations
     * cannot starve the controller or resume XIP code inside a radio IRQ. */
    rx_queue_process_one();
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    /* Keep clock/radio/health and packet service above independent. */
    gr_panel_diag_service();
    if (gr_panel_diag_busy()) return;
#endif
#if defined(INKTAG_GOODIX_GR5515)
    v2_input_simulation_service();
#endif
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_13_pro.inc"
#line 3674 "../Src/user/inktag_gr_app.c"
#endif
    diag_scan_process();
    clock_checkpoint_process();

    if (s_slideshow_active && s_slideshow_advance_pending &&
        !s_epd_work_pending && !s_epd_work_active && !s_v2_transfer.active &&
        s_system_action_pending == 0u &&
        s_epd_work_type == EPD_WORK_NONE && !s_diag_scan_active &&
        !s_picture_io_active && !inktag_image_store_capture_active()) {
        uint8_t next_id = UINT8_MAX;
        slideshow_arm_next_after_refresh();
        if (slideshow_select_next(&next_id)) {
            s_epd_work_type = EPD_WORK_IMAGE_SHOW;
            s_epd_work_arg = next_id;
            s_epd_work_background = true;
            s_epd_work_origin = EPD_WORK_ORIGIN_AUTOMATIC;
            s_epd_work_pending = true;
        }
    }

    if (s_advertising_ready && s_boot_restore_due && s_conn_idx == 0xFFu &&
        !s_epd_work_pending && !s_epd_work_active &&
        s_epd_work_type == EPD_WORK_NONE && !s_diag_scan_active) {
        s_boot_restore_due = false;
        schedule_boot_display_restore();
    }

#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    if (panel_temperature_sample_process()) return;
#endif

    if (!s_epd_work_pending) {
        return;
    }
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_work_admission_pro.inc"
#endif
#line 3723 "../Src/user/inktag_gr_app.c"





#line 3735 "../Src/user/inktag_gr_app.c"

#endif
    /* A connection is not panel ownership. Run due native pages and an active
     * slideshow once discovery and protocol work are idle, even if WebUI holds
     * GATT overnight. Unrelated background work remains deferred. */
    if (s_epd_work_type != EPD_WORK_DIAG_SCAN) {
        inktag_status_v2_t access = epd_control_access();
        if (access != INKTAG_STATUS_OK) {
            uint8_t argument = s_epd_work_arg;
            s_epd_work_type = EPD_WORK_NONE;
            s_epd_work_arg = 0u;
            s_epd_work_timestamp = 0u;
            s_epd_work_pending = s_epd_work_background = false;
            s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
            s_native_refresh_pending = false;
            s_boot_restore_pending = s_boot_restore_due = false;
            slideshow_stop(false);
            /* Direct COMMIT has cleared its transfer but the queued refresh
             * can still own the powered bus. Refusal must release that owner
             * without opening GPIO or applying the new admission gate again. */
            if (s_epd_gpio_ready) {
                if (s_epd) s_epd->drv->sleep(s_epd);
                epd_gpio_close_if_needed();
                s_picture_io_active = false;
            }
            s_pending_picture_store_id = UINT8_MAX;
            send_v2_work_complete(access, argument);
            return;
        }
    }
    if (s_epd_work_background && s_conn_idx != 0xFFu &&
        (((s_epd_work_type != EPD_WORK_GUI ||
           (s_epd_work_arg != MODE_CALENDAR && s_epd_work_arg != MODE_CLOCK)) &&
          (s_epd_work_type != EPD_WORK_IMAGE_SHOW || !s_slideshow_active)) ||
         !status_deadline_reached(s_ble_discovery_until_ms) ||
         s_v2_transfer.active || s_picture_io_active ||
         s_diag_scan_active || s_rx_head != s_rx_tail ||
         inktag_image_store_capture_active())) {
        return;
    }
    /* ZKC42V's RGB startup sequence is a bounded product-visible transient.
     * Automatic boot, clock and slideshow work may queue, but
     * must not activate REFRESH and preempt STARTUP before it completes. */
    if (s_epd_work_background && !background_display_startup_complete()) {
        return;
    }
    if (s_epd_work_type != EPD_WORK_DIAG_SCAN &&
        !inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true)) {
        display_work_finished(s_epd_work_type, s_epd_work_arg, false);
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        s_epd_work_pending = false;
        s_epd_work_background = false;
        s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_nfc_worker_2_pro.inc"

#endif
#line 3795 "../Src/user/inktag_gr_app.c"

#endif
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_LOW_BATTERY,
                              s_status_now_ms);
        send_text("epd:low-power");
        send_v2_work_complete(INKTAG_STATUS_LOW_POWER, 0u);
        return;
    }
    /* Warning color is rendered by common GUI during the requested page.
     * Only the real DISPLAY admission above can refuse automatic work. */
#if INKTAG_GR_MINUTE_IDLE_ENABLED
    /* Restore the ordinary cadence before synchronous panel work, including
     * a minute refresh queued during this same foreground turn. */
    if (s_clock_minute_idle) clock_idle(false);
#endif
    epd_work_type_t work_type = s_epd_work_type;
    uint8_t work_arg = s_epd_work_arg;
    uint32_t work_timestamp = s_epd_work_timestamp;
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    if (work_type!=EPD_WORK_GUI || work_arg!=MODE_CLOCK)
        gr_clock_partial_invalidate();
#endif
    s_epd_work_active_background = s_epd_work_background;
    s_epd_work_active_origin = s_epd_work_origin;
    s_epd_active_type = work_type;
    s_epd_active_arg = work_arg;
    s_epd_active_timestamp = work_timestamp;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_work_arg = 0;
    s_epd_work_timestamp = 0;
    s_epd_work_pending = false;
    s_epd_work_background = false;
    s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
    s_epd_work_active = true;
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
    inktag_gr_nfc_display_begin();
#endif
#endif
    s_epd_cancel_requested = false;
    s_epd_low_power_abort = false;
    if (work_type != EPD_WORK_DIAG_SCAN) {
        inktag_gr_health_display_begin();
    }
    EPD_SetAbortCallback(epd_abort_requested);
    if (work_type == EPD_WORK_IMAGE_SHOW) {
        inktag_image_store_header_t header;
        if (inktag_image_store_get(work_arg, &header) == INKTAG_IMAGE_STORE_OK &&
            !image_store_record_visible(work_arg)) {
            display_work_finished(work_type, work_arg, false);
            inktag_gr_health_display_end();
            s_epd_work_active = false;
            s_epd_work_active_background = false;
            s_epd_work_active_origin = EPD_WORK_ORIGIN_NONE;
            s_epd_active_type = EPD_WORK_NONE;
            EPD_SetAbortCallback(NULL);
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_nfc_worker_3_pro.inc"

#endif
#line 3843 "../Src/user/inktag_gr_app.c"

#endif
            send_v2_work_complete(INKTAG_STATUS_DENIED, work_arg);
            return;
        }
    }
    inktag_gr_health_stage(0x47524450u, INKTAG_GR_HEALTH_OPERATION_DISPLAY); /* GRDP */
    bool work_success = true;
    bool defer_clock_led=false;
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    /* Foreground CLOCK can reuse the same history as an automatic minute.
     * Wait for the native full/partial decision before any refresh edge. */
    defer_clock_led=work_type==EPD_WORK_GUI && work_arg==MODE_CLOCK &&
        gr_clock_partial_available();
#endif
    if (work_type != EPD_WORK_DIAG_SCAN && !defer_clock_led) {
        if (work_type == EPD_WORK_REFRESH) {
            inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                                     false, s_status_now_ms);
        }
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                                 true, s_status_now_ms);
        /* Display work runs synchronously inside this scheduler turn. Arm the
         * 120 ms visual timer before entering rendering/BUSY waits; waiting
         * until app_before_sleep() would leave only the one-second clock
         * sampling opportunity and visibly slow the refresh LED scene. */
        status_timer_update();
    }

    if (!s_epd && work_type != EPD_WORK_DIAG_SCAN) {
        // Do not require an explicit screen-profile setup before every display
        // command. Web Bluetooth can reconnect with a remembered screen and
        // issue CLEAR/REFRESH directly, so resolve the persisted configuration
        // lazily on the device as well.
        if (!ensure_epd_screen()) {
            send_text("epd:no-screen");
            display_work_finished(work_type, work_arg, false);
            inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                                     false, s_status_now_ms);
#if defined(INKTAG_GOODIX_GR5515) && INKTAG_BUILD_PRO
#include "inktag_gr_nfc_worker_4_pro.inc"
#endif
            inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_ERROR,
                                  s_status_now_ms);
            inktag_gr_health_display_end();
            s_epd_work_active = false;
            s_epd_work_active_background = false;
            s_epd_active_type = EPD_WORK_NONE;
            EPD_SetAbortCallback(NULL);
            if (s_v2_cancel_transaction != 0u) {
                send_v2_response(INKTAG_CMD_CANCEL, s_v2_cancel_transaction,
                                 INKTAG_STATUS_OK, NULL, 0);
                s_v2_cancel_transaction = 0u;
            }
            send_v2_work_complete(INKTAG_STATUS_INCOMPATIBLE, work_arg);
            return;
        }
    }

    switch (work_type) {
        case EPD_WORK_CLEAR:
            if (!epd_gpio_open_if_needed()) { work_success = false; break; }
            send_diag("clear:start");
            init_panel_for_display(s_epd);
            send_diag("clear:init");
            s_epd->drv->clear(s_epd, work_arg != 0);
            break;
        case EPD_WORK_REFRESH:
            if (!epd_gpio_open_if_needed()) { work_success = false; break; }
            send_diag("picture:refresh");
            s_epd->drv->refresh(s_epd);
            break;
        case EPD_WORK_GHOST_CLEAN:
            if (!epd_gpio_open_if_needed()) { work_success = false; break; }
            send_diag("ghost-clean:start");
            work_success = ghost_clean_run(work_arg);
            break;
        case EPD_WORK_GUI:
            work_success = epd_gui_update((display_mode_t)work_arg, work_timestamp);
            break;
        case EPD_WORK_DIAG_SCAN:
            diag_scan_start(s_diag_scan_custom_range.type_id,
                            s_diag_scan_first_pin,
                            s_diag_scan_custom_range.max_pin,
                            work_arg != 0);
            break;
        case EPD_WORK_IMAGE_SHOW:
            work_success = image_store_show(work_arg);
            break;
        case EPD_WORK_NONE:
        default:
            break;
    }

    if (work_type != EPD_WORK_DIAG_SCAN && !epd_io_operation_ok())
        work_success = false;
    bool interrupted = s_epd_cancel_requested || EPD_AbortRequested();
    bool low_power_abort = s_epd_low_power_abort;
    bool cancelled = interrupted && !low_power_abort;
    bool no_busy_edge = work_success && !interrupted && work_type != EPD_WORK_DIAG_SCAN &&
                        (work_type != EPD_WORK_CLEAR || work_arg != 0) && !EPD_BusyDiagAsserted();
    if (no_busy_edge) work_success = false;
    bool stuck_busy = s_epd && !interrupted && s_epd->drv->read_busy(s_epd);
    if (work_type != EPD_WORK_DIAG_SCAN) {
        if (interrupted || stuck_busy || !work_success) {
            send_diag("epd:failed");
            s_pending_picture_store_id = UINT8_MAX;
            if (s_epd_gpio_ready) {
                if (s_epd && (s_epd->driver_id == EPD_SCREEN_DRIVER_IST7513 ||
                              s_epd->driver_id == EPD_SCREEN_DRIVER_IST7601))
                    s_epd->drv->sleep(s_epd);
                else EPD_Reset(true, 10);
            }
        } else {
#if defined(INKTAG_GR_CLOCK_PARTIAL_SSD1683) && INKTAG_GR_CLOCK_PARTIAL_SSD1683
            if (!gr_clock_partial_finish_sleep())
#endif
            if (s_epd) s_epd->drv->sleep(s_epd);
            delay_ms(100);
        }
        /* Include power/pin release in the final cancel/voltage/IO outcome
         * before committing history. Each native wait keeps its own budget. */
        epd_gpio_close_if_needed();
        bool cleanup_interrupted = EPD_AbortRequested();
        if (cleanup_interrupted || !epd_io_operation_ok()) work_success = false;
        low_power_abort = low_power_abort || s_epd_low_power_abort;
        interrupted = interrupted || s_epd_cancel_requested || cleanup_interrupted || low_power_abort;
        cancelled = interrupted && !low_power_abort;
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        if (interrupted || stuck_busy || !work_success)
            s_panel_temperature_cache.valid = false;
#endif
        if (!interrupted && !stuck_busy && work_success) {
            if (work_type == EPD_WORK_CLEAR) {
                record_display_state(EPD_DISPLAY_STATE_CLEAR, work_arg, 0);
            } else if (work_type == EPD_WORK_GHOST_CLEAN) {
                record_display_state(EPD_DISPLAY_STATE_CLEAR, 1u, 0u);
            } else if (work_type == EPD_WORK_REFRESH) {
                record_display_state(EPD_DISPLAY_STATE_PICTURE, s_pending_picture_store_id, 0);
                s_pending_picture_store_id = UINT8_MAX;
            } else if (work_type == EPD_WORK_GUI) {
                record_display_state(work_arg == MODE_DEVICE_INFO ? EPD_DISPLAY_STATE_DEVICE_INFO : work_arg,
                                     EPD_DISPLAY_STATE_NO_SOURCE, 0);
            } else if (work_type == EPD_WORK_IMAGE_SHOW) {
                record_display_state(s_slideshow_active ? EPD_DISPLAY_STATE_SLIDESHOW :
                                     EPD_DISPLAY_STATE_PICTURE,
                                     work_arg,
                                     s_slideshow_active ? s_slideshow_interval : 0);
            }
            if (work_type != EPD_WORK_REFRESH) {
                s_pending_picture_store_id = UINT8_MAX;
            }
        } else {
            s_pending_picture_store_id = UINT8_MAX;
        }
        inktag_gr_health_display_end();
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                                 false, s_status_now_ms);
        if (low_power_abort || (!cancelled && (stuck_busy || !work_success))) {
#if defined(INKTAG_GOODIX_GR5515) && INKTAG_BUILD_PRO
#include "inktag_gr_nfc_worker_5_pro.inc"
#endif
            inktag_status_trigger(&s_status, low_power_abort ?
                                  INKTAG_STATUS_SCENE_LOW_BATTERY : INKTAG_STATUS_SCENE_ERROR,
                                  s_status_now_ms);
        } else if (!cancelled && !(defer_clock_led &&
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
                    s_clock_partial_state && s_clock_partial.pending &&
                    s_clock_partial.pending_partial
#else
                    false
#endif
                    )) {
            inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_SUCCESS,
                                  s_status_now_ms);
        }
    }
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    gr_clock_partial_complete(work_type==EPD_WORK_GUI && work_arg==MODE_CLOCK &&
        !interrupted && !stuck_busy && work_success);
#endif
    display_work_finished(work_type, work_arg,
                          !interrupted && !stuck_busy && work_success);
    EPD_SetAbortCallback(NULL);
    s_epd_work_active = false;
    s_epd_work_active_background = false;
    s_epd_work_active_origin = EPD_WORK_ORIGIN_NONE;
    s_epd_active_type = EPD_WORK_NONE;
    inktag_gr_health_stage(0x47524944u, INKTAG_GR_HEALTH_OPERATION_BLE); /* GRID */
    bool stop_requested = cancelled || s_epd_stop_ack_pending;
    s_epd_stop_ack_pending = false;
    s_epd_cancel_requested = false;
    s_epd_low_power_abort = false;
    if (s_v2_cancel_transaction != 0u) {
        send_v2_response(INKTAG_CMD_CANCEL, s_v2_cancel_transaction,
                         INKTAG_STATUS_OK, NULL, 0);
        s_v2_cancel_transaction = 0u;
    }
    inktag_status_v2_t completion_status = low_power_abort ? INKTAG_STATUS_LOW_POWER :
        cancelled ? INKTAG_STATUS_CANCELLED :
        (stuck_busy || !work_success) ? INKTAG_STATUS_IO_ERROR : INKTAG_STATUS_OK;
    send_v2_work_complete(completion_status, work_arg);
    if (low_power_abort) {
        send_text("epd:low-power");
    } else if (stop_requested) {
        send_text("epd:stopped");
    } else if (work_type != EPD_WORK_DIAG_SCAN) {
        if (stuck_busy || !work_success) send_text(no_busy_edge ? "epd:no-busy-edge" : "epd:timeout");
        else if (work_type == EPD_WORK_CLEAR) send_diag("clear:done");
        else if (work_type == EPD_WORK_GHOST_CLEAN) send_diag("ghost-clean:done");
        else if (work_type == EPD_WORK_REFRESH) send_diag("picture:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_CALENDAR) send_diag("calendar:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_CLOCK) send_diag("clock:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_DEVICE_INFO) send_diag("device-info:done");
        else if (work_type == EPD_WORK_GUI) send_diag("gui:done");
        else if (work_type == EPD_WORK_IMAGE_SHOW) {
            char buf[32];
            GFX_format(buf, sizeof(buf), "img:show:done id=%u", work_arg);
            send_text(buf);
        }
    }
}

void inktag_gr_app_before_sleep(void) {
    /* The generated non-RTOS main calls this immediately before the vendor's
     * event-aware, lost-wakeup-safe pwr_mgmt_schedule(). */
    /* Arm the existing 40 ms finite-feedback sampler before debug-grace WFE
     * can wait a full second. Stable off/solid scenes remain timer-free. */
    status_timer_update();
    if (!s_debug_attach_grace_complete) {
        if (s_status_now_ms < INKTAG_DEBUG_ATTACH_GRACE_MS) {
            pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);
            /* ACTIVE preserves the running-state debug path on GR551x. Park
             * the Cortex-M with WFE until the next interrupt so the bounded
             * grace window does not become a 30-second CPU busy loop. */
            __WFE();
        } else {
            /* The product's application-only debug exception is a bounded
             * boot/reset grace period. Recovery remains permanently ACTIVE. */
            /* Both supported Goodix SDKs keep BLE scheduling and the
             * platform monotonic counter alive across their qualified
             * event-aware SLEEP path.  GR5331 vendor BLE examples use this
             * same mode; retaining IDLE here kept the core rail running and
             * dominated the board's quiescent current. */
            pwr_mgmt_mode_set(PMR_MGMT_SLEEP_MODE);
            s_debug_attach_grace_complete = true;
        }
    }
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_14_pro.inc"
#line 4104 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_GR_MINUTE_IDLE_ENABLED
    clock_idle(true);
    if (s_clock_minute_idle)
        inktag_gr_health_watchdog_sleep(true);
#endif
#if !defined(INKTAG_GOODIX_GR5515)
    inktag_soak_telemetry_sleep_entered();
#endif
}

void inktag_gr_app_after_sleep(void) {
#if INKTAG_GR_MINUTE_IDLE_ENABLED
    inktag_gr_health_watchdog_sleep(false);
    if (s_clock_minute_idle) {
        /* External wake must expose current time before button/NFC work or a
         * newly opened advertising deadline; do not wait for the minute IRQ. */
#if defined(INKTAG_GOODIX_GR5515)
        s_monotonic_now_ms = clock_raw_now_tick();
#endif
        s_status_tick_due = s_clock_tick_due = true;
    }
#endif
    /* Reserved for post-resume work.  GR551x warm boot is owned by the SDK;
     * application code must not alter its AON wake flag or retention token. */
}

static void load_device_settings(void) {
    if (!epd_device_settings_read(&s_device_settings)) {
        epd_device_settings_default(&s_device_settings);
        return;
    }
    epd_device_settings_t migrated = s_device_settings;
    if (inktag_button_bindings_migrate_default(&migrated.button) &&
        epd_device_settings_write(&migrated)) {
        /* The existing writer checks fresh Flash power and commits safely.
         * Publish RAM only after it succeeds; otherwise retain the old tuple. */
        s_device_settings = migrated;
    }
}

#if defined(INKTAG_GR_PIN_CONFIG_PORT)
static bool pin_config_io_busy(void) {
    return s_epd_work_active || s_epd_work_pending || s_picture_io_active ||
        s_v2_transfer.active || s_diag_scan_active || s_slideshow_active ||
        s_system_action_pending != 0u || inktag_image_store_capture_active() ||
        s_epd_gpio_ready || !inktag_gr_epd_pins_idle()
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
        || s_nfc_command_work_pending
#endif
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_pin_busy_pro.inc"
#endif
#line 4140 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        || s_panel_temperature_sample_active || s_panel_temperature_sample_pending
#endif
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        || gr_panel_diag_busy()
#endif
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
        || inktag_gr_aux_io_busy()
#endif
        ;
}

static bool pin_config_apply(const inktag_pin_config_t *old) {
    if (!old || !inktag_gr_epd_pins_park()) return false;
    inktag_gr_pin_routes_mark_applied(false);
    /* Park the previous trusted tuple even when commit uncertainty has
     * invalidated the new consumer. Never fall back to guessed outputs. */
    for (unsigned role = INKTAG_PIN_LED_RED; role <= INKTAG_PIN_LED_BLUE; ++role) {
        uint8_t pin = old->pins[role];
        if (pin == 255u) continue;
        app_io_write_pin(board_pin_type(pin), board_pin_mask(pin),
            old->active_low & (1u << role) ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
        (void)app_io_deinit(board_pin_type(pin), board_pin_mask(pin));
    }
    if (!inktag_gr_pin_routes_config(&s_config)) {
        s_epd = NULL;
        inktag_gr_pin_routes_invalidate();
        inktag_status_set_available_colors(&s_status, 0u, s_status_now_ms);
        return false;
    }
    EPD_GPIO_Load(&s_config);
    inktag_gr_epd_pin_screen(&s_screen_config);
    s_epd = s_config.mosi_pin == 255u ? NULL : epd_find(&s_screen_config);
    for (unsigned channel = 0u; channel < 3u; ++channel) board_led_init(runtime_led_pin(channel));
    inktag_gr_pin_routes_mark_applied(true);
    inktag_status_set_available_colors(&s_status, status_led_available_colors(), s_status_now_ms);
    /* An explicit LED override can add the first channel after a dark boot.
     * Create once, but only arm while the existing policy needs finite ticks. */
    status_timer_create_if_available();
    status_led_reapply();
    status_timer_update();
    return true;
}

static void pin_config_quarantine(void) {
    if (!inktag_gr_pin_routes_applied() || inktag_gr_pin_routes_available() ||
        !inktag_gr_pin_routes_required() || s_epd_work_active || s_picture_io_active ||
        s_diag_scan_active || !inktag_gr_epd_pins_idle()
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        || gr_panel_diag_busy()
#endif
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        || s_panel_temperature_sample_active
#endif
        ) return;
    (void)pin_config_apply(inktag_gr_pin_routes_applied());
    inktag_gr_pin_routes_mark_applied(false);
}
#endif

static void load_default_config_if_needed(void) {
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    s_chip_temperature_cache.valid = false;
    s_panel_temperature_cache.valid = false;
    s_panel_temperature_sample_pending = false;
#endif
    if (inktag_gr_board_identity() == INKTAG_GR_BOARD_UNKNOWN) {
        memset(&s_config, 0xFF, sizeof(s_config));
        epd_device_settings_default(&s_device_settings);
        s_epd = NULL;
        s_boot_restore_pending = false;
        return; /* BLE rescue stays available; do not guess powered GPIOs. */
    }
    bool display_state_found;
    bool first_display_requested;
    bool ota_install_boot;
    inktag_boot_display_plan_t boot_display_plan;
    epd_screen_config_t default_screen = INKTAG_BOARD_DEFAULT_SCREEN_CONFIG;
    uint8_t default_epd_config[] = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
#if defined(INKTAG_GOODIX_GR5515) && defined(INKTAG_BOARD_ZKC42V_SCREEN_CONFIG)
    epd_screen_config_t zkc42v_screen = INKTAG_BOARD_ZKC42V_SCREEN_CONFIG;
    bool is_zkc42v = inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC42V;
    if (is_zkc42v) default_screen = zkc42v_screen;
#endif
#if defined(INKTAG_GOODIX_GR5515) && defined(INKTAG_BOARD_GR5513_EPD_CONFIG)
    uint8_t package = (uint8_t)g_inktag_gr_hw_info;
    uint32_t defaults = (uint32_t)default_screen.driver_id | (0x10u << 8) | (0x19u << 16);
    if (package == PACKAGE_GR5513BEND || package == PACKAGE_GR5513BENDU) {
        if (!is_zkc42v) {
            const epd_screen_config_t gr5513_screen = INKTAG_BOARD_GR5513_SCREEN_CONFIG;
            default_screen = gr5513_screen;
        }
        defaults = (uint32_t)default_screen.driver_id | (0x03u << 8) | (0x02u << 16);
    }
    default_screen.driver_id = (uint8_t)defaults;
    default_epd_config[1] = (uint8_t)(defaults >> 8);
    default_epd_config[2] = (uint8_t)(defaults >> 16);
#endif

#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required() && !inktag_gr_pin_routes_screen(&default_screen)) {
        s_epd = NULL;
        return;
    }
#endif
    epd_config_init(&s_config);
    load_device_settings();
    epd_config_read(&s_config);
    if (epd_config_empty(&s_config)) {
        memcpy(&s_config, default_epd_config, sizeof(default_epd_config));
        s_config.week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    }
    display_state_found = epd_display_state_read(&s_display_state);
    if (!display_state_found) inktag_display_state_default(&s_display_state);
    s_display_state_loaded = true;
    if (!display_settings_read(&s_display_settings)) {
        display_settings_default(&s_display_settings);
    }
    display_settings_apply_runtime(&s_display_settings);
    first_display_requested = inktag_gr_first_display_request_pending();
    ota_install_boot = inktag_gr_ota_install_boot();
    boot_display_plan = inktag_boot_display_plan(
        first_display_requested, ota_install_boot,
        display_state_found ? &s_display_state : NULL);
    /* Defer first-display consumption until the requested screen and verified
     * activation are available. A base E6 boot must keep its marker intact. */
    s_boot_display_target = boot_display_plan.target;
    s_boot_restore_pending =
        s_boot_display_target != INKTAG_BOOT_DISPLAY_NONE;
    inktag_gr_settings_read_result_t screen_read = inktag_gr_screen_read(&s_screen_config);
    if (screen_read == INKTAG_GR_SETTINGS_READ_EMPTY) {
        s_screen_config = default_screen;
    } else if (screen_read != INKTAG_GR_SETTINGS_READ_OK) {
        memset(&s_screen_config, 0, sizeof(s_screen_config));
        s_boot_restore_pending = false; /* No damaged/unknown ID3 boot repair. */
    }
    s_epd = epd_find(&s_screen_config);
    clock_restore_checkpoint();
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required()) {
        inktag_gr_pin_candidate_t routes;
        if (!inktag_gr_pin_routes_prepare(&s_screen_config, &routes)) {
            s_epd = NULL; return;
        }
        inktag_gr_pin_routes_publish(&routes);
        if (!inktag_gr_pin_routes_config(&s_config)) { s_epd = NULL; return; }
        if (s_config.mosi_pin == 255u) { s_epd = NULL; s_boot_restore_pending = false; }
        inktag_gr_pin_routes_mark_applied(true);
    }
    inktag_gr_epd_pin_screen(&s_screen_config);
#endif
    EPD_GPIO_Load(&s_config);
    if (epd_control_access() != INKTAG_STATUS_OK ||
        (boot_display_plan.consume_first_display_request &&
         !inktag_gr_first_display_request_consume())) {
        s_boot_display_target = INKTAG_BOOT_DISPLAY_NONE;
        s_boot_restore_pending = s_boot_restore_due = false;
    }
}

#include "inktag_gr_panel_diagnostic.inc"
/* Keep single-use bounded protocol handlers out of the large dispatcher.
 * This only controls code sharing: payloads, gates, queues and worker bodies
 * are unchanged. Never apply it to the vendor radio/RTC/power implementation. */
#if defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize ("no-inline-functions-called-once")
#endif
#include "inktag_gr_protocol_v2.inc"
#if defined(__GNUC__)
#pragma GCC pop_options
#endif

static void inktag_service_event(inktag_gr_evt_t *p_evt) {
    /* Keep notifications, queued writes and cancellation owned by the same
     * accepted link. Extra SDK connection slots do not share its password. */
    if (p_evt->conn_idx != s_conn_idx || s_conn_idx == 0xFFu) return;
    switch (p_evt->evt_type) {
        case INKTAG_GR_EVT_NOTIFY_ENABLED:
            /* Screen state is returned only by V2 GET_SCREEN_PROFILE. */
            s_notify_enabled = true;
            break;
        case INKTAG_GR_EVT_NOTIFY_DISABLED:
            s_screen_save_owned = false;
            inktag_device_password_session_clear(&s_password);
#if defined(INKTAG_GOODIX_GR5515)
            v2_input_simulation_clear();
#endif
            s_notify_enabled = false;
            /* V2 responses require notifications. Once the client closes the
             * CCCD, ask the controller to end the link from the main loop so
             * Windows cannot keep a silent GATT connection indefinitely. */
            if (s_conn_idx != 0xFFu) {
                s_ble_disconnect_idx = s_conn_idx;
                s_ble_disconnect_due = true;
            }
            rx_queue_reset();
            if (s_epd_work_pending &&
                s_epd_work_origin == EPD_WORK_ORIGIN_BLE_CLIENT) {
                s_epd_work_pending = false;
                s_epd_work_type = EPD_WORK_NONE;
                s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
            }
            if (s_epd_work_active &&
                s_epd_work_active_origin == EPD_WORK_ORIGIN_BLE_CLIENT) {
                s_epd_cancel_requested = true;
            }
            s_link_cleanup_due = true;
            s_v2_work_opcode = 0u;
            s_v2_work_transaction = 0u;
            s_v2_work_type = EPD_WORK_NONE;
            s_v2_cancel_transaction = 0u;
            v2_find_owner_clear();
            break;
        case INKTAG_GR_EVT_RX_DATA: {
            inktag_protocol_request_t request;
            /* Gate wire identity before AUTH, cancellation or queue admission.
             * The bounded queue retains the original transport bytes; semantic
             * flags are published only by the shared parser, never raw byte2. */
            if (inktag_protocol_parse_request(p_evt->p_data, p_evt->length,
                                              &request) != INKTAG_STATUS_OK) {
                if (p_evt->p_data && p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
                    send_v2_response(p_evt->p_data[0], p_evt->p_data[1],
                                     INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
                break;
            }
            if (
                !inktag_device_password_command_allowed(
                    &s_password, &s_password_config, request.opcode)) {
                send_v2_response(p_evt->p_data[0], p_evt->p_data[1],
                                 INKTAG_STATUS_DENIED, NULL, 0u);
                break;
            }
#if !INKTAG_BUILD_PRO
            if (v2_base_request_unsupported(request.opcode, request.payload, request.payload_length)) {
                send_v2_response(request.opcode, request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
                break;
            }
#endif
            inktag_status_v2_t transfer_status = v2_image_transfer_request_status(
                request.opcode, request.payload, request.payload_length);
            if (transfer_status != INKTAG_STATUS_OK) {
                send_v2_response(request.opcode, request.transaction_id, transfer_status, NULL, 0u);
                break;
            }
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_15_pro.inc"
#line 4381 "../Src/user/inktag_gr_app.c"
#endif
            if (request.opcode != INKTAG_CMD_TRANSFER_WRITE &&
                request.opcode != INKTAG_CMD_TRANSFER_COMMIT) {
                inktag_status_v2_t access = v2_panel_request_access(
                    request.opcode, request.payload, request.payload_length);
                if (access != INKTAG_STATUS_OK) {
                    send_v2_response(request.opcode, request.transaction_id,
                                     access, NULL, 0u);
                    break;
                }
            }
            if (p_evt->length > INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                p_evt->p_data[0] == INKTAG_CMD_CANCEL) {
#if defined(INKTAG_GOODIX_GR5515)
                v2_input_simulation_cancel(p_evt->p_data[1],
                    &p_evt->p_data[INKTAG_PROTOCOL_V2_HEADER_SIZE],
                    p_evt->length - INKTAG_PROTOCOL_V2_HEADER_SIZE);
#else
                send_v2_response(INKTAG_CMD_CANCEL, p_evt->p_data[1],
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
#endif
                break; /* Invalid/unmatched targeted cancel never aborts EPD. */
            }
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_rx_cancel_pro.inc"
#endif
#line 4415 "../Src/user/inktag_gr_app.c"

            if (p_evt->length == INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                p_evt->p_data[0] == INKTAG_CMD_CANCEL &&
                s_epd_work_active) {
                /* CANCEL is the only command that must bypass the deferred RX
                 * queue. Only set state flags here; the active panel routine
                 * observes the abort callback and performs cleanup in the
                 * application loop. */
                slideshow_stop(false);
                s_native_refresh_pending = false;
                s_boot_restore_pending = s_boot_restore_due = false;
                s_picture_io_active = false;
                s_epd_work_pending = false;
                s_epd_work_type = EPD_WORK_NONE;
                s_epd_work_background = false;
                s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
                s_epd_cancel_requested = true;
                s_epd_stop_ack_pending = true;
                s_v2_cancel_transaction = p_evt->p_data[1];
                break;
            }
            if (p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                v2_command_requires_idle_display(p_evt->p_data[0]) &&
                (
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_rx_busy_pro.inc"
#endif
#line 4441 "../Src/user/inktag_gr_app.c"
#endif
                 s_epd_work_active ||
                 (s_epd_work_pending && !s_epd_work_background))) {
                /* BLE packets may be buffered, but user-visible screen work
                 * must never become a FIFO behind a slow panel refresh. */
                send_v2_response(p_evt->p_data[0], p_evt->p_data[1],
                                 INKTAG_STATUS_BUSY, NULL, 0);
                break;
            }
            if (!rx_queue_push(p_evt->p_data, p_evt->length)) {
                if (p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE) {
                    send_v2_response(p_evt->p_data[0], p_evt->p_data[1],
                                     INKTAG_STATUS_BUSY, NULL, 0);
                }
            }
            break;
        }
        case INKTAG_GR_EVT_NOTIFY_COMPLETE:
#if defined(INKTAG_GOODIX_GR5515)
            if (s_notify_inflight != 0u) --s_notify_inflight;
            if (p_evt->status != BLE_SUCCESS) s_notify_failed = true;
#endif
            break;
        default:
            break;
    }
}

#if INKTAG_GR_MINUTE_IDLE_ENABLED
#include "inktag_gr_idle_clock_pro.inc"
#endif

static void clock_timer_handler(void *p_ctx) {
    (void)p_ctx;
#if defined(INKTAG_GOODIX_GR5515)
    monotonic_observe_app_timer(&s_clock_timer);
#endif
    /* Status policy, activation expiry and non-LED deadlines share this
     * ordinary one-second product clock when no dynamic LED scene is active. */
    s_status_tick_due = true;
    s_clock_tick_due = true;
    s_checkpoint_sample_due = true;
}

static uint32_t clock_checkpoint_now(void) {
    s_checkpoint_now_ms += inktag_elapsed_clock_sample_time(
        &s_checkpoint_clock, clock_raw_now_tick()).milliseconds;
    return s_checkpoint_now_ms;
}

static void clock_restore_checkpoint(void) {
    uint32_t saved_timestamp;
    s_time_checkpoint_found =
        epd_time_state_read(&saved_timestamp, &s_timezone_minutes);
    if (s_time_checkpoint_found) s_timestamp = saved_timestamp;
    /* Flash proves a past snapshot, not elapsed time while power was absent.
     * This port has no qualified armed retained anchor, even for soft reset. */
    s_time_valid = false;
}

/* Foreground only. Do not infer write authority from a timestamp cache or
 * replay an ambiguous mutation. Explicit and ordinary saves use one boundary. */
static bool clock_save_time_candidate(uint32_t timestamp, int16_t timezone,
                                      bool explicit_time) {
    uint32_t before = 0u, after = 0u, snapshot;
    uint32_t elapsed_from = s_timestamp;
    int16_t before_zone = 0, after_zone = 0;
    bool before_valid, before_empty, wrote, after_valid;
    if ((!explicit_time && !s_time_valid) || s_time_checkpoint_blocked ||
        !inktag_gr_nvds_ready())
        return false;
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        inktag_time_checkpoint_failed(&s_time_checkpoint, clock_checkpoint_now());
        return false;
    }
    before_valid = epd_time_state_read(&before, &before_zone);
    before_empty = !before_valid &&
        inktag_gr_settings_absent(INKTAG_GR_SETTINGS_SLOT_TIME);
    if (!before_valid && !before_empty) {
        s_time_checkpoint_blocked = true;
        return false;
    }
    s_clock_tick_due = true;
    clock_process();
    /* Keep the running sampler, Find and pending work on the old timeline
     * until the proposed record is verified. Periodic saves use live time. */
    snapshot = explicit_time ? timestamp + (s_timestamp - elapsed_from) : s_timestamp;
    if (!explicit_time) timezone = s_timezone_minutes;
    wrote = epd_time_state_write(snapshot, timezone);
    after_valid = epd_time_state_read(&after, &after_zone);
    /* Include NVDS/program/readback time in both timelines without writing a
     * second record or recursively entering the checkpoint service. */
    s_clock_tick_due = true;
    clock_process();
    uint32_t now_ms = clock_checkpoint_now();
    if (wrote && after_valid && after == snapshot &&
        after_zone == timezone) {
        inktag_time_checkpoint_saved(&s_time_checkpoint, now_ms);
        return true;
    }
    if (inktag_gr_nvds_ready() && !wrote &&
        ((before_valid && after_valid && before == after &&
          before_zone == after_zone) ||
         (before_empty && !after_valid &&
          inktag_gr_settings_absent(INKTAG_GR_SETTINGS_SLOT_TIME)))) {
        inktag_time_checkpoint_failed(&s_time_checkpoint, now_ms);
    } else {
        s_time_checkpoint_blocked = true;
    }
    return false;
}

static bool clock_save_time(void) {
    return clock_save_time_candidate(0u, 0, false);
}

static void clock_checkpoint_process(void) {
    /* Sample only on the existing product timer's foreground opportunity,
     * not every idle-loop turn. Keep sampling even without trusted UTC so
     * the raw accumulator observes wraps; explicit saves remain independent. */
    if (!s_checkpoint_sample_due) return;
    s_checkpoint_sample_due = false;
    uint32_t now_ms = clock_checkpoint_now();
    if (!s_time_valid || s_time_checkpoint_blocked ||
        !inktag_time_checkpoint_due(&s_time_checkpoint, now_ms) ||
        s_epd_work_pending || s_epd_work_active || s_diag_scan_active ||
        s_picture_io_active || s_v2_transfer.active ||
        s_system_action_pending != 0u || s_rx_head != s_rx_tail ||
        inktag_image_store_capture_active()) return;
    (void)clock_save_time();
}

static void clock_process(void) {
    uint32_t previous_timestamp;
    uint32_t elapsed_seconds;
    bool refresh_due;

    if (!s_clock_tick_due) return;
    s_clock_tick_due = false;
    bool display_allowed = epd_control_access() == INKTAG_STATUS_OK;
    if (!display_allowed) {
        s_native_refresh_pending = false;
        s_boot_restore_pending = s_boot_restore_due = false;
        slideshow_stop(false);
    }
    previous_timestamp = s_timestamp;
    elapsed_seconds = clock_elapsed_seconds(clock_now_tick());
    if (elapsed_seconds != 0u) {
        s_timestamp += elapsed_seconds;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_time_pro.inc"
#line 4596 "../Src/user/inktag_gr_app.c"
#endif
        if (s_slideshow_active && !s_epd_work_pending && !s_epd_work_active &&
            (int32_t)(s_timestamp - s_slideshow_next_at) >= 0 &&
            !s_slideshow_advance_pending) {
            /* Timer callbacks never touch external Flash. Selection and replay are
             * deferred to inktag_gr_app_process(). */
            s_slideshow_advance_pending = true;
        }
        refresh_due = display_allowed && s_time_valid && (
            (s_display_state.mode == EPD_DISPLAY_STATE_CALENDAR &&
             inktag_elapsed_clock_crossed_period(previous_timestamp, s_timestamp, 86400u)) ||
            (s_display_state.mode == EPD_DISPLAY_STATE_CLOCK &&
             inktag_elapsed_clock_crossed_period(previous_timestamp, s_timestamp, 60u)));
        if (!s_slideshow_active && refresh_due) {
            /* Retain a crossed boundary while a transfer, diagnostic or display
             * operation owns the panel. The next idle clock sample collapses any
             * number of missed boundaries into one redraw at the newest time. */
            s_native_refresh_pending = true;
            s_native_refresh_mode = s_display_state.mode;
            s_native_refresh_timestamp = s_timestamp;
        }
    }

#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_app_bulk_16_pro.inc"
#line 4621 "../Src/user/inktag_gr_app.c"
#endif
    if (s_native_refresh_pending &&
        (s_slideshow_active ||
         s_display_state.mode != s_native_refresh_mode)) {
        s_native_refresh_pending = false;
    }
    if (s_native_refresh_pending)
        s_native_refresh_timestamp = s_timestamp;
    if (s_time_valid &&
        s_epd_work_pending && s_epd_work_background &&
        s_epd_work_type == EPD_WORK_GUI &&
        s_epd_work_arg == s_display_state.mode &&
        (s_epd_work_arg == MODE_CALENDAR || s_epd_work_arg == MODE_CLOCK))
        s_epd_work_timestamp = s_timestamp;
    if (s_native_refresh_pending && s_epd_work_pending &&
        s_epd_work_background && s_epd_work_type == EPD_WORK_GUI &&
        s_epd_work_arg == s_native_refresh_mode) {
        /* Collapse a queued older frame into the newest crossed boundary. */
        s_epd_work_timestamp = s_native_refresh_timestamp;
        s_native_refresh_pending = false;
    }
    if (s_native_refresh_pending && !s_picture_io_active &&
        !s_epd_work_pending && !s_epd_work_active &&
        s_epd_work_type == EPD_WORK_NONE && !s_diag_scan_active) {
        s_epd_work_type = EPD_WORK_GUI;
        s_epd_work_arg = s_native_refresh_mode;
        s_epd_work_timestamp = s_native_refresh_timestamp;
        s_epd_work_background = true;
        s_epd_work_origin = EPD_WORK_ORIGIN_AUTOMATIC;
        s_epd_work_pending = true;
        s_native_refresh_pending = false;
    }
#if INKTAG_GR_MINUTE_IDLE_ENABLED
    /* Keep the absolute minute across unrelated wakes. Select the timer
     * deadline after foreground radio service, before sleeping. */
    if (!s_clock_minute_idle || s_checkpoint_sample_due) {
        uint32_t now = s_status_now_ms;
        /* A real timer tick consumes the cached wake, even if SDK tick
         * rounding woke us just before the selected deadline. */
        s_clock_wake_at_ms = now;
        s_clock_minute_at_ms = now + clock_next_minute_delay();
    }
#endif
}

static void gap_params_init(void) {
    sdk_err_t error_code;

    boot_stage(0x47524130); // GRA0
    ble_gap_pair_enable(false);
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_address_capture_pro.inc"
#line 4664 "../Src/user/inktag_gr_app.c"
#endif
    init_device_name();
    error_code = ble_gap_device_name_set(BLE_GAP_WRITE_PERM_DISABLE, (uint8_t const *)s_device_name, strlen(s_device_name));
    boot_error(0x47524131, error_code); // GRA1
    APP_ERROR_CHECK(error_code);

    s_gap_adv_param.adv_intv_max = INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS;
    s_gap_adv_param.adv_intv_min = INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS;
    s_gap_adv_param.adv_mode = BLE_GAP_ADV_TYPE_ADV_IND;
    s_gap_adv_param.disc_mode = BLE_GAP_DISC_MODE_GEN_DISCOVERABLE;
    s_gap_adv_param.chnl_map = BLE_GAP_ADV_CHANNEL_37_38_39;
    s_gap_adv_param.filter_pol = BLE_GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
#if defined(INKTAG_GOODIX_GR5515)
    s_gap_adv_param.max_tx_pwr = 7;
#else
    // The original GR5331 port advertised reliably at the SDK default. Keep
    // it at 0 dBm until the board's RF matching network is characterized;
    // explicitly requesting the top PA step can succeed in the controller
    // while producing no observable packet on this production tag.
    s_gap_adv_param.max_tx_pwr = 0;
#endif

    error_code = ble_gap_adv_param_set(INKTAG_ADV_INDEX, BLE_GAP_OWN_ADDR_STATIC, &s_gap_adv_param);
    boot_error(0x47524132, error_code); // GRA2
    APP_ERROR_CHECK(error_code);
    error_code = ble_gap_adv_data_set(INKTAG_ADV_INDEX, BLE_GAP_ADV_DATA_TYPE_DATA, s_adv_data_set, s_adv_data_len);
    boot_error(0x47524133, error_code); // GRA3
    APP_ERROR_CHECK(error_code);
    error_code = ble_gap_adv_data_set(INKTAG_ADV_INDEX, BLE_GAP_ADV_DATA_TYPE_SCAN_RSP, s_adv_rsp_data_set, s_adv_rsp_data_len);
    boot_error(0x47524134, error_code); // GRA4
    APP_ERROR_CHECK(error_code);

#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_advertising_init_pro.inc"
#line 4705 "../Src/user/inktag_gr_app.c"
#endif

    s_gap_adv_time_param.duration = 0;
    s_gap_adv_time_param.max_adv_evt = 0;
    boot_stage(0x47524135); // GRA5
}

static void services_init(void) {
    sdk_err_t error_code;
#if defined(INKTAG_GOODIX_OTA)
    error_code = dfu_service_init(goodix_ota_enter);
    boot_error(0x47525332, error_code); // GRS2
    APP_ERROR_CHECK(error_code);
    if (otas_service_start_handle_get() == 0xFFFFu) {
        /* A valid handle proves OTAS was assigned a GATT database range. */
        boot_error(0x47525332, SDK_ERR_NO_RESOURCES); // GRS2
        APP_ERROR_CHECK(SDK_ERR_NO_RESOURCES);
    }
#endif
    inktag_gr_service_init_t init = {.evt_handler = inktag_service_event};
    error_code = inktag_gr_service_init(&init);
    boot_error(0x47525331, error_code); // GRS1
    APP_ERROR_CHECK(error_code);
}

void ble_evt_handler(const ble_evt_t *p_evt) {
    switch (p_evt->evt_id) {
        case BLE_COMMON_EVT_STACK_INIT:
            if (p_evt->evt_status == BLE_SUCCESS) ble_app_init();
            break;
        case BLE_GAPM_EVT_ADV_START:
            // Unlike ble_gap_adv_start()'s immediate return value, this is
            // the controller's asynchronous completion status.
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_adv_start_pro.inc"
#line 4747 "../Src/user/inktag_gr_app.c"
#endif
            boot_error(0x47524138, p_evt->evt_status); // GRA8
            s_advertising_ready = p_evt->evt_status == BLE_SUCCESS;
            if (p_evt->evt_status == BLE_SUCCESS) {
                s_adv_restart_due = false;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_start_ready_pro.inc"
#line 4761 "../Src/user/inktag_gr_app.c"
#endif
            } else if (s_conn_idx == 0xFFu) {
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_start_retry_pro.inc"
#line 4767 "../Src/user/inktag_gr_app.c"
#else
                s_adv_restart_due = true;
                s_adv_restart_not_before_ms = s_status_now_ms + 500u;
#endif
            }
            if (p_evt->evt_status == BLE_SUCCESS && s_boot_restore_pending) {
                s_boot_restore_pending = false;
                s_boot_restore_due = true;
            }
#if defined(INKTAG_GOODIX_OTA)
            if (p_evt->evt_status == BLE_SUCCESS && s_swap_confirmation_pending) {
                /* Confirm a trial only after the controller has actually
                 * started advertising. Defer the Flash write to the main
                 * application loop so the BLE event callback can return. */
                s_swap_confirmation_due = true;
            }
#endif
            break;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_adv_stop_pro.inc"
#line 4812 "../Src/user/inktag_gr_app.c"
#endif
        case BLE_GAPC_EVT_CONNECTED:
            if (s_conn_idx != 0xFFu &&
                s_conn_idx != p_evt->evt.gapc_evt.index) {
                (void)ble_gap_disconnect(p_evt->evt.gapc_evt.index);
                break;
            }
            inktag_device_password_session_clear(&s_password);
#if defined(INKTAG_GOODIX_GR5515)
            v2_input_simulation_clear();
#endif
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_connect_telemetry_pro.inc"
#line 4829 "../Src/user/inktag_gr_app.c"
#endif
            inktag_soak_telemetry_connection_started();
            s_conn_idx = p_evt->evt.gapc_evt.index;
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
            if (s_panel_temperature_sample_active)
                s_panel_temperature_sample_cancel = true;
            s_chip_temperature_cache.valid = false;
            s_panel_temperature_cache.valid = false;
            s_panel_temperature_sample_pending = true;
#endif
            s_ble_discovery_until_ms = s_status_now_ms + BLE_DISCOVERY_GRACE_MS;
            s_advertising_ready = false;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_connect_pro.inc"
#line 4847 "../Src/user/inktag_gr_app.c"
#endif
            if (s_epd_work_active && s_epd_work_active_background) {
                /* A client may connect just after an automatic refresh began.
                 * Abort promptly, retain that background job for disconnect,
                 * and let GATT discovery own the main loop meanwhile. */
                s_epd_cancel_requested = true;
                if (!s_epd_work_pending) {
                    s_epd_work_type = s_epd_active_type;
                    s_epd_work_arg = s_epd_active_arg;
                    s_epd_work_timestamp = s_epd_active_timestamp;
                    s_epd_work_background = true;
                    s_epd_work_origin = EPD_WORK_ORIGIN_AUTOMATIC;
                    s_epd_work_pending = true;
                }
            }
            /*
             * Keep BLE/GATT discovery independent from panel power-up.  Some
             * production boards multiplex or gate the EPD pins; touching the
             * panel bus in the connection callback can fault or disturb the
             * link before the client has discovered the InkTag service.
             * Current queued screen work opens the GPIOs on demand.
             */
            status_led_set_connected(true);
            break;
        case BLE_GAPC_EVT_DISCONNECTED:
            {
            if (s_conn_idx != p_evt->evt.gapc_evt.index) break;
            inktag_device_password_session_clear(&s_password);
#if defined(INKTAG_GOODIX_GR5515)
            v2_input_simulation_clear();
#endif
            s_notify_enabled = false;
            s_screen_save_owned = false;
            s_conn_idx = 0xFF;
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
            if (s_panel_temperature_sample_active)
                s_panel_temperature_sample_cancel = true;
            s_panel_temperature_sample_pending = false;
#endif
            s_advertising_ready = false;
            s_ble_disconnect_due = false;
            s_ble_disconnect_idx = 0xFFu;
            rx_queue_reset();
            if (s_epd_work_pending &&
                s_epd_work_origin == EPD_WORK_ORIGIN_BLE_CLIENT) {
                s_epd_work_pending = false;
                s_epd_work_type = EPD_WORK_NONE;
                s_epd_work_origin = EPD_WORK_ORIGIN_NONE;
            }
            if (s_epd_work_active &&
                s_epd_work_active_origin == EPD_WORK_ORIGIN_BLE_CLIENT) {
                s_epd_cancel_requested = true;
            }
            s_link_cleanup_due = true;
            /* A disconnected client no longer owns a transaction ID. Display
             * cleanup may still finish asynchronously, but it must not leave
             * stale protocol ownership visible after reconnect. */
            s_v2_work_opcode = 0u;
            s_v2_work_transaction = 0u;
            s_v2_work_type = EPD_WORK_NONE;
            s_v2_cancel_transaction = 0u;
            v2_find_owner_clear();
            diag_scan_finish(NULL);
            /* Active display or picture I/O owns the panel until its normal
             * completion cleanup; otherwise release it immediately. */
            if (!s_epd_work_active && !s_picture_io_active
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
                && !gr_panel_diag_busy()
#endif
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
                && !s_panel_temperature_sample_active
#endif
                ) epd_gpio_close_if_needed();
            status_led_set_connected(false);
            /* Defer advertising restart until this stack callback returns;
             * the main loop retries a rejected start without blocking BLE. */
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_event_disconnect_pro.inc"
#line 4930 "../Src/user/inktag_gr_app.c"
#else
            s_adv_restart_due = true;
            s_adv_restart_not_before_ms = s_status_now_ms + 100u;
#endif
            }
            break;
        case BLE_GAPC_EVT_CONN_PARAM_UPDATE_REQ:
            ble_gap_conn_param_update_reply(p_evt->evt.gapc_evt.index, true);
            break;
#if defined(INKTAG_GOODIX_OTA)
        case BLE_GATT_COMMON_EVT_MTU_EXCHANGE:
            if (p_evt->evt_status == BLE_SUCCESS) {
                dfu_ble_set_mtu_size(p_evt->evt.gatt_common_evt.params.mtu_exchange.mtu);
            }
            break;
#endif
        default:
            break;
    }
}

void ble_app_init(void) {
    sdk_version_t version;
    uint16_t soak_flags = INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
                          INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
                          INKTAG_SOAK_FLAG_SLEEP_ENTRIES;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_telemetry_pro.inc"
#line 4958 "../Src/user/inktag_gr_app.c"
#endif
    boot_stage(0x47523031); // GR01
    sys_sdk_verison_get(&version);
    APP_LOG_INFO("%s SDK%d.%d.%d", GOODIX_TARGET_LOG_NAME, version.major, version.minor, version.build);

    s_status_now_ms = 0u;
#if defined(INKTAG_GOODIX_GR5515)
    s_monotonic_now_ms = 0u;
    s_app_timer_epoch_us = 0u;
    s_app_timer_epoch_valid = false;
#endif
    inktag_elapsed_clock_reset(&s_status_clock, monotonic_now_tick(),
                               INKTAG_GR_MONOTONIC_COUNTER_MASK,
                               INKTAG_GR_MONOTONIC_TICKS_PER_SECOND);
    inktag_soak_telemetry_init(soak_flags, s_status_now_ms);
    boot_stage(0x47523032); // GR02
    /* Establish the boot policy sample while every product LED is still in
     * its reset/off state.  The visible startup sequence is a real load and
     * must not turn its transient rail sag into a stable low-battery event. */
    inktag_gr_health_init(sys_device_reset_reason(), g_inktag_gr_boot_stage);
    if (!inktag_gr_nvds_ready()) {
        /* Fresh Full/corrupt NVDS remains quarantined. The physical signed
         * initializer can inspect this initialized SDK/voltage context, but
         * no board seed, GUI, settings, images, QR or radio starts here. */
        boot_error(0x47524E51, SDK_ERR_INVALID_PARAM); // GRNQ: current boot quarantined
        return;
    }
#if defined(INKTAG_GOODIX_GR5515)
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
    if ((g_inktag_gr_hw_info >> 16) != SDK_SUCCESS ||
        (uint8_t)g_inktag_gr_hw_info != PACKAGE_GR5513BENDU) {
        boot_error(0x47524950, SDK_ERR_INVALID_PARAM); // GRIP: incompatible package
        return;
    }
    uint8_t board_mask = (uint8_t)(1u << INKTAG_GR_BOARD_ZKC21V);
#else
    uint8_t board_mask = (uint8_t)(1u << INKTAG_GR_BOARD_ZKC42VM);
    if ((uint8_t)g_inktag_gr_hw_info != PACKAGE_GR5515I0ND)
        board_mask |= (uint8_t)((1u << INKTAG_GR_BOARD_ZKC42V) |
                               (1u << INKTAG_GR_BOARD_ZKC21V));
#endif
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    uint8_t pin_mcu = (g_inktag_gr_hw_info >> 16) != SDK_SUCCESS ? 0u :
        ((uint8_t)g_inktag_gr_hw_info == PACKAGE_GR5515I0ND ? 0x15u :
        (((uint8_t)g_inktag_gr_hw_info == PACKAGE_GR5513BEND ||
          (uint8_t)g_inktag_gr_hw_info == PACKAGE_GR5513BENDU) ? 0x13u : 0u));
    inktag_gr_pin_routes_init(0x55u, pin_mcu, board_mask);
#endif
#else
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    inktag_gr_pin_routes_init(0x33u, 0x31u, (uint8_t)(1u << INKTAG_GR_BOARD_ZKC21V));
#endif
#endif
    /* GR5513 Full images stage the factory ITFA in otherwise blank internal
     * image-store sectors because J-Link cannot program the board TD40. Copy
     * it before advertising or accepting image-store commands. GR5515 and
     * GR5331 simply validate their directly programmed bank A here. */
    if (inktag_gr_board_identity() != INKTAG_GR_BOARD_UNKNOWN)
        (void)inktag_gr_gui_asset_bootstrap();
#if INKTAG_BOARD_ACTIVATION_QUALIFIED
    (void)inktag_gr_activation_init(&s_activation);
#else
    memset(&s_activation, 0, sizeof(s_activation));
#endif
    load_default_config_if_needed();
    (void)inktag_image_store_init();
    inktag_device_password_init(&s_password, s_status_now_ms);
    (void)inktag_gr_password_load(&s_password_config);
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
#include "inktag_gr_find_init_pro.inc"
#line 5035 "../Src/user/inktag_gr_app.c"
#endif
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
    s_aux_custom_authorized =
        product_access(INKTAG_PRODUCT_FEATURE_BUTTON_BINDING, true, true, true) == INKTAG_STATUS_OK &&
#if defined(INKTAG_GOODIX_GR5515)
        inktag_gr_board_identity() == INKTAG_GR_BOARD_ZKC42VM;
#else
        INKTAG_BOARD_AUX_IO_QUALIFIED != 0;
#endif
    inktag_gr_aux_io_init(
        s_aux_custom_authorized
    );
#endif
#if INKTAG_BUILD_PRO
#include "inktag_gr_nfc_init_pro.inc"

#endif
#line 5060 "../Src/user/inktag_gr_app.c"
    boot_stage(0x47523033); // GR03
    if (!inktag_gr_ble_address_init()) {
        boot_error(0x47524144, SDK_ERR_INVALID_PARAM); // GRAD: identity refused
        return; /* No service/advertisement under an unconfirmed identity. */
    }
    services_init();
    boot_stage(0x47523034); // GR04
    gap_params_init();
#if INKTAG_BUILD_PRO
#include "inktag_gr_nfc_startup_uri_pro.inc"

#endif
#line 5082 "../Src/user/inktag_gr_app.c"
    boot_stage(0x47523035); // GR05
    /* Start the visible boot scene only after every synchronous Flash,
     * Find, NFC and BLE-service initializer has returned. Status GPIO work is
     * deliberately deferred out of timer callbacks, so starting it earlier
     * made that one-time setup time look like an overlong first red phase.
     * The boot voltage sample above still precedes every product LED load. */
    status_led_init();
    status_timer_create_if_available();
    status_timer_update();
    inktag_elapsed_clock_reset(&s_elapsed_clock, clock_now_tick(),
                               INKTAG_GR_UTC_COUNTER_MASK,
                               INKTAG_GR_UTC_TICKS_PER_SECOND);
    s_utc_subsecond_ms = 0u;
    (void)inktag_clock_rate_set(&s_clock_rate, clock_rate_default_ppm());
    app_timer_create(&s_clock_timer, ATIMER_REPEAT, clock_timer_handler);
    if (app_timer_start(s_clock_timer, CLOCK_TIMER_INTERVAL_MS, NULL) ==
        SDK_SUCCESS) {
#if defined(INKTAG_GOODIX_GR5515)
        monotonic_capture_app_timer_epoch(&s_clock_timer);
#endif
    }
    inktag_elapsed_clock_reset(&s_checkpoint_clock, clock_raw_now_tick(),
                               INKTAG_GR_CHECKPOINT_COUNTER_MASK,
                               INKTAG_GR_CHECKPOINT_TICKS_PER_SECOND);
    s_checkpoint_now_ms = 0u;
    s_time_checkpoint_blocked = false;
    inktag_time_checkpoint_init(&s_time_checkpoint, 0u, s_time_checkpoint_found);
    app_timer_create(&s_diag_scan_timer, ATIMER_ONE_SHOT, diag_scan_timer_handler);
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
    advertising_config_load();
    s_findmy_mux_switch_pending = true;
#endif
    sdk_err_t error_code = ble_gap_adv_start(INKTAG_ADV_INDEX, &s_gap_adv_time_param);
    boot_error(0x47524136, error_code); // GRA6
    APP_ERROR_CHECK(error_code);
    boot_stage(0x47524137); // GRA7
#if defined(INKTAG_GOODIX_GR5515)
    /* app_io_init() lazily calls app_pwr_mgmt_init(), whose first use restores
     * the SDK's default callback. Register InkTag's wrapper only after every
     * board peripheral has initialized so sleep telemetry and veto counters
     * remain attached to the callback that the scheduler actually invokes. */
    pwr_mgmt_set_callback(inktag_gr_sleep_check,
                          inktag_gr_prepare_for_deep_sleep);
#endif
}
