#include "inktag_om_app.h"
#include "inktag_om_profile.h"
#include "inktag_om_address.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "om.h"
#include "om_driver.h"
#if defined(INKTAG_OM_DIAGNOSTICS)
#include "shell.h"
#endif
#include "evt.h"
#include "pm.h"
#include "board.h"
#include "nvds.h"
#include "ob_config.h"
#include "omble.h"
#include "inktag_om_service.h"
#include "inktag_om_image_store.h"
#include "inktag_om_gui_assets.h"
#include "inktag_om_external_flash.h"
#include "inktag_om_nfc.h"
#include "inktag_om_device_settings.h"
#include "inktag_om_pins.h"
#include "inktag_gui_assets.h"
#include "inktag_om_activation.h"
#include "inktag_om_brand.h"
#include "inktag_om_features.h"
#include "inktag_om_findmy.h"
#include "inktag_om_p224_uecc.h"
#include "inktag_om_health.h"
#include "inktag_om_nvds.h"
#include "inktag_ble_power.h"
#include "inktag_ble_identity.h"
#include "inktag_om_board_identity.h"
#include "inktag_om_flash_layout.h"
#include "inktag_om_ble_reset.h"
#define INKTAG_OM_APPLICATION_DEBUG_WINDOW 1
#include "inktag_om_debug_retention.h"
#include "inktag_status.h"
#include "inktag_temperature_wire.h"
#include "inktag_om_swap.h"
#if defined(INKTAG_OM_NEXT_LAYOUT)
#include "inktag_om_ota_policy.h"
/* The packager binds the linked data map, not just an external policy file. */
__attribute__((used, section(".inktag_layout")))
static const uint32_t s_om_data_layout[] = {
    0x4C4D4B49u, INKTAG_OM_OTA_LAYOUT_SIGNATURE,
    INKTAG_OM_IMAGE_STORE_PRIMARY_START, INKTAG_OM_IMAGE_STORE_PRIMARY_SIZE,
    INKTAG_OM_NVDS_ADDRESS, INKTAG_OM_FACTORY_CONF_SIZE,
};
#endif
#include "inktag_epd_config.h"
#include "inktag_epd_driver.h"
#include "inktag_epd_temperature.h"
#include "inktag_gui.h"
#include "inktag_board.h"
#include "inktag_om_panel_idle.h"
#include "inktag_version.h"
#include "inktag_protocol_v2.h"
#include "inktag_lifecycle.h"
#include "inktag_lifecycle_pending.h"
#include "inktag_protocol_core.h"
#if INKTAG_TRANSFER_PACKBITS_ENABLED && INKTAG_OM_IMAGE_CODEC_PROFILE != 2
#include "inktag_packbits.h"
#endif
#if INKTAG_OM_IMAGE_CODEC_PROFILE != 0
#include "inktag_lzo1x.h"
#endif
#include "inktag_release_v1.h"
#include "inktag_image_upload_v5.h"
#include "inktag_findmy.h"
#include "inktag_access.h"
#include "inktag_elapsed_clock.h"
#include "inktag_page_cycle.h"
#include "inktag_soak_telemetry.h"
#include "inktag_timebase.h"
#include "inktag_time_checkpoint.h"

_Static_assert(INKTAG_PROTOCOL_V2_VERSION==INKTAG_RELEASE_PROTOCOL_BYTE,
               "OM release-v1 requires matching shared framing and GATT identity");
_Static_assert(EPD_SCREEN_SCHEMA_VERSION==INKTAG_RELEASE_SCREEN_VERSION,
               "OM release-v1 requires the matching exact screen codec");
_Static_assert(INKTAG_STATUS_PROTOCOL_VERSION==1u,
               "OM release-v1 requires the matching indicator codec");
_Static_assert(INKTAG_GUI_NATIVE_PACKAGE_SCHEMA==INKTAG_RELEASE_FONT_VERSION,
               "OM release-v1 requires the matching native font consumer");
_Static_assert(INKTAG_FINDMY_CONFIG_VERSION==1u && INKTAG_FINDMY_PUBLIC_VERSION==1u,
               "OM release-v1 requires the matching public Find codecs");

#if INKTAG_OM_FIND_BACKEND_AVAILABLE && \
    (INKTAG_BOARD_FIND_QUALIFIED || INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE)
#define INKTAG_EXPERIMENTAL_FINDMY_ENABLED 1
#else
#define INKTAG_EXPERIMENTAL_FINDMY_ENABLED 0
#endif
#ifndef INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED
#define INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED 0
#endif
/* Owner-auth synchronization is paused. This unqualified adapter is isolated
 * for experiments only; ordinary builds neither allocate a session nor link
 * its proof implementation. Sensitive operations still fail closed. */

extern void Reset_Handler(void);

__attribute__((section(".inktag_launch"), used))
const inktag_om_launch_record_t g_inktag_om_launch_record = {
    INKTAG_OM_LAUNCH_RECORD_MAGIC,
    (uint32_t)(uintptr_t)&Reset_Handler,
    INKTAG_OM_LAUNCH_RECORD_GUARD,
};

#define DEVICE_NAME_PREFIX "IKT_OM"
#define DEVICE_NAME_MAX_LEN 20
#define CLOCK_TIMER_INTERVAL_MS 1000
#define APPLICATION_WATCHDOG_TIMEOUT_MS 120000u
#define STATUS_TIMER_INTERVAL_MS INKTAG_OM_EVENT_SERVICE_TIMER_MS
#define DIAG_LED_TEST_MS 10000u
#define OTA_RECOVERY_FEEDBACK_MS 350u
#define OTA_RECOVERY_RADIO_SETTLE_MS 600u
#define OTA_TRIAL_CONFIRM_FIRST_RETRY_MS 500u
#define OTA_TRIAL_CONFIRM_RETRY_MS 5000u
#define ADVERTISING_RETRY_MS 300u

_Static_assert(INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH ==
                   EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH,
               "display-settings boot flag must match persisted display state");
_Static_assert(INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN ==
                   EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN,
               "display-settings brand flag must match persisted display state");
#define OM6626_GPIO_MAX_PIN 25
#define RX_PACKET_QUEUE_DEPTH 16
#define TX_RETRY_QUEUE_DEPTH 4
#define INKTAG_OM_INPUT_FEATURES 0xFFu
#define INKTAG_OM_BUTTON_ACTION_MASK 0x02F7u

typedef enum {
    EPD_WORK_NONE = 0,
    EPD_WORK_CLEAR,
    EPD_WORK_REFRESH,
    EPD_WORK_GUI,
    EPD_WORK_IMAGE_SHOW,
    EPD_WORK_GHOST_CLEAN,
} epd_work_type_t;

typedef struct {
    uint16_t length;
    uint8_t data[INKTAG_OM_MAX_DATA_LEN];
} rx_packet_t;

typedef struct {
    uint16_t length;
    uint8_t data[INKTAG_OM_MAX_DATA_LEN];
} tx_retry_packet_t;

typedef struct {
    bool active;
    uint8_t object_type;
    uint8_t transaction_id;
    uint8_t format;
    uint8_t segment_count;
    uint8_t black_mask;
    uint8_t resource_id;
    bool image_replacing;
    uint16_t image_saved_count;
    uint16_t image_replacement_ordinal;
    uint32_t total_size;
    uint32_t written;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t segment_size[2];
    uint32_t last_activity_ms;
} v2_transfer_t;

typedef struct {
    struct {
        uint8_t addr[6];
    } gap_addr;
} inktag_om_gap_address_t;

static ob_adv_param_t s_adv_param;
static ob_gap_addr_t s_peer_addr = {1, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}};
static uint8_t s_local_addr[6];
static uint8_t s_local_addr_type;
static volatile uint8_t s_identity_status;
static char s_device_name[DEVICE_NAME_MAX_LEN] = DEVICE_NAME_PREFIX;
static char s_device_label[INKTAG_OM_DEVICE_LABEL_MAX_BYTES + 1u];
static uint8_t s_conn_idx = 0xFF;
static bool s_notify_enabled = false;
static uint32_t s_ble_discovery_until_ms;
static uint32_t s_ble_command_idle_after_ms;

extern uint8_t EPD_OnMicroSpiIndex(void);
extern bool EPD_OnMicroSpiReady(void);
extern int32_t EPD_OnMicroSpiLastError(void);
static uint32_t s_timestamp = 1735689600;
static bool s_time_valid;
static inktag_elapsed_clock_t s_elapsed_clock;
static inktag_elapsed_clock_t s_runtime_clock;
static inktag_time_checkpoint_t s_time_checkpoint;
static const inktag_timebase_profile_t s_timebase_profile = {
    INKTAG_BOARD_LFCLK_SOURCE,
    INKTAG_BOARD_LFCLK_QUALITY,
    INKTAG_BOARD_LFCLK_QUALIFIED != 0,
    INKTAG_BOARD_LFCLK_FREQUENCY_HZ,
    INKTAG_BOARD_TIMEBASE_TICKS_PER_SECOND,
    INKTAG_BOARD_TIMEBASE_COUNTER_MASK,
    INKTAG_BOARD_LFCLK_ACCURACY_PPM,
};
static evt_timer_t s_clock_timer;
static evt_timer_t s_status_timer;
static evt_timer_t s_ota_recovery_timer;
static evt_timer_t s_advertising_retry_timer;

static epd_config_t s_config;
static epd_screen_config_t s_screen_config;
static inktag_om_device_info_qr_t s_device_info_qr[INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT];
static bool s_device_info_qr_valid[INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT];
static epd_display_state_t s_display_state;
static epd_device_settings_t s_device_settings;
static bool s_display_state_loaded;
static bool s_first_display_requested;
static bool s_boot_restore_pending;
static bool s_lifecycle_content_locked;
static inktag_lifecycle_pending_scope_t s_lifecycle_pending_scope;
static bool s_boot_restore_due;
static epd_model_t *s_epd = NULL;
static bool s_epd_gpio_ready = false;
static epd_work_type_t s_epd_work_type = EPD_WORK_NONE;
static uint8_t s_epd_work_arg = 0;
static uint32_t s_epd_work_timestamp = 0;
static volatile bool s_epd_work_pending = false;
static volatile bool s_epd_work_background = false;
static volatile bool s_epd_work_client_owned = false;
static volatile bool s_epd_work_active = false;
static volatile bool s_epd_work_active_background = false;
static volatile bool s_epd_work_active_client_owned = false;
static volatile bool s_native_refresh_pending;
static volatile uint8_t s_native_refresh_mode;
static volatile uint32_t s_native_refresh_timestamp;
static epd_work_type_t s_epd_active_type = EPD_WORK_NONE;
static uint8_t s_epd_active_arg;
static uint32_t s_epd_active_timestamp;
static volatile bool s_epd_cancel_requested = false;
static volatile bool s_epd_low_power_abort = false;
static uint8_t s_gui_frame_buffer[INKTAG_BOARD_GUI_FRAME_BUFFER_BYTES]
    __attribute__((aligned(4)));
#if INKTAG_OM_IMAGE_CODEC_PROFILE != 0
_Static_assert(sizeof(s_gui_frame_buffer)>=2000u,
               "OM LZO capability requires its full bounded decode workspace");
#endif
static inktag_status_runtime_t s_status;
static uint32_t s_status_now_ms;
static bool s_status_initialized;
static bool s_status_timer_armed;
static bool s_led_test_active;
static uint32_t s_led_test_deadline_ms;
static volatile bool s_ota_fixed_boot_pending;
static volatile uint32_t s_ota_fixed_boot_not_before_ms;
static rx_packet_t s_rx_packets[RX_PACKET_QUEUE_DEPTH];
static volatile uint8_t s_rx_packet_head;
static volatile uint8_t s_rx_packet_tail;
static volatile uint8_t s_rx_packet_count;
static tx_retry_packet_t s_tx_retry_packets[TX_RETRY_QUEUE_DEPTH];
static uint8_t s_tx_retry_head;
static uint8_t s_tx_retry_tail;
static uint8_t s_tx_retry_count;
static bool s_tx_retry_ready;
static bool s_picture_io_active;
static uint8_t s_pending_picture_store_id = UINT8_MAX;
static bool s_slideshow_active = false;
static uint32_t s_slideshow_interval = 300u;
static uint32_t s_slideshow_next_at = 0u;
static uint8_t s_slideshow_current_id = UINT8_MAX;
static volatile bool s_slideshow_advance_pending = false;
static bool s_image_store_abort_pending = false;
static bool s_button_action_pending = false;
static inktag_input_binding_t s_button_pending_binding;
static bool s_clear_return_valid = false;
static epd_display_state_t s_clear_return_state;
static bool s_input_slideshow_return_valid = false;
static epd_display_state_t s_input_slideshow_return_state;
static volatile bool s_advertising_restart_pending = false;
static volatile bool s_advertising_retry_timer_armed = false;
static volatile bool s_advertising_active = false;
static volatile uint8_t s_advertising_retry_count = 0;
static volatile uint32_t s_advertising_last_status = UINT32_MAX;
/* Retained for read-only SWD diagnosis when the host cannot discover GATT.
 * These are startup results, not user-visible protocol or persistent state. */
static volatile uint32_t s_startup_ble_status = UINT32_MAX;
static volatile uint32_t s_startup_service_status = UINT32_MAX;
static volatile uint32_t s_startup_callback_status = UINT32_MAX;
static volatile uint32_t s_startup_advertising_status = UINT32_MAX;
static volatile uint32_t s_startup_swap_confirm_status = UINT32_MAX;
static bool s_startup_swap_confirm_retry_pending;
static uint8_t s_startup_swap_confirm_failures;
static uint32_t s_startup_swap_confirm_retry_at_ms;
static bool s_boot_restore_scheduled;
static int16_t s_timezone_minutes;
static inktag_activation_t s_activation;
static inktag_device_password_t s_password;
static inktag_device_password_config_t s_password_config;
static uint8_t s_v2_work_opcode;
static uint8_t s_v2_work_transaction;
static uint8_t s_v2_cancel_transaction;
static v2_transfer_t s_v2_transfer;
/* V2 input remains immutable so its CRC covers exactly the client bytes.
 * Only this bounded final-output copy receives the device-side brand. */
static uint8_t s_v2_brand_chunk[
    INKTAG_OM_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE - 4u];
static volatile bool s_v2_transfer_abort_pending;
static volatile uint8_t s_system_action_pending;
static uint32_t s_system_action_not_before_ms;

static uint32_t start_advertising(void);
static uint32_t start_advertising_with_retry(void);
static void schedule_advertising_retry(void);
static void ota_recovery_timer_handler(evt_timer_t *timer, void *param);
static void process_packet(const uint8_t *p_data, uint16_t length);
static bool status_deadline_reached(uint32_t deadline_ms);
static void runtime_clock_update(void);
static void wall_clock_update(void);
static void tx_retry_process(void);
static void v2_transfer_reset(bool erase_store);
static inktag_status_v2_t epd_control_access(void);
static void process_pending_system_action(void);
static void complete_startup_if_ready(void);
static bool schedule_epd_work(epd_work_type_t type, uint8_t arg,
                              const char *queued_text);
static bool schedule_epd_work_client(epd_work_type_t type, uint8_t arg);
static bool schedule_epd_work_background(epd_work_type_t type, uint8_t arg);
static void poll_findmy_time_multiplex(void);
static void findmy_mux_configuration_changed(void);

static uint8_t s_adv_data[INKTAG_BLE_LEGACY_ADV_MAX_BYTES] = {
    0x02, 0x01, 0x06,
};
static uint8_t s_scan_rsp_data[] = {
    0x11, 0x07, INKTAG_OM_SERVICE_UUID,
};

static ob_data_t s_adv = { s_adv_data, 0 };
static ob_data_t s_scan_rsp = { s_scan_rsp_data, sizeof(s_scan_rsp_data) };

uint32_t timestamp(void) {
    wall_clock_update();
    return s_timestamp;
}
void set_timestamp(uint32_t value) {
    s_timestamp = value;
    s_time_valid = true;
    inktag_elapsed_clock_reset(&s_elapsed_clock,
                               drv_pmu_timer_cnt_get(),
                               s_timebase_profile.counter_mask,
                               s_timebase_profile.counter_ticks_per_second);
}
void app_feed_wdt(void) { inktag_om_health_watchdog_feed(); }

void inktag_gui_cooperative_yield(void) {
    app_feed_wdt();
    evt_schedule();
    inktag_om_app_epd_maintenance();
}
static bool epd_abort_requested(void) {
    return s_epd_cancel_requested || s_epd_low_power_abort ||
        ((s_epd_work_active || s_picture_io_active) &&
         epd_control_access() != INKTAG_STATUS_OK);
}

void inktag_om_app_epd_maintenance(void) {
    runtime_clock_update();
    tx_retry_process();
    if (!s_epd_cancel_requested && s_epd_work_active &&
        !inktag_om_health_display_continue_allowed(s_status_now_ms)) {
        s_epd_low_power_abort = true;
    }
}

static void send_bytes(const uint8_t *p_data, uint16_t len) {
    uint32_t notify_result;
    if (s_conn_idx == 0xFF || !s_notify_enabled) return;
    if (p_data == NULL || len == 0u || len > INKTAG_OM_MAX_DATA_LEN) return;
    if (s_tx_retry_count == 0u) {
        inktag_om_health_stage(0x4F4D5453u,
                               INKTAG_OM_HEALTH_OPERATION_BLE); /* OMTS */
        notify_result = inktag_om_notify(s_conn_idx, p_data, len);
        inktag_om_health_stage(0x4F4D4944u,
                               INKTAG_OM_HEALTH_OPERATION_BLE); /* OMID */
    } else {
        notify_result = 1u;
    }
    if (s_tx_retry_count == 0u && notify_result == 0u) {
        s_tx_retry_ready = false;
        return;
    }
    if (s_tx_retry_count >= TX_RETRY_QUEUE_DEPTH) return;
    tx_retry_packet_t *packet = &s_tx_retry_packets[s_tx_retry_tail];
    packet->length = len;
    memcpy(packet->data, p_data, len);
    s_tx_retry_tail = (uint8_t)((s_tx_retry_tail + 1u) % TX_RETRY_QUEUE_DEPTH);
    s_tx_retry_count++;
    /* A failed send normally means an older notification still owns the
     * controller buffer.  Retry only after its TX_COMPLETE event; repeatedly
     * calling the vendor API while it is busy can wedge it until the WDT. */
    s_tx_retry_ready = false;
}

static void tx_retry_reset(void) {
    s_tx_retry_head = 0u;
    s_tx_retry_tail = 0u;
    s_tx_retry_count = 0u;
    s_tx_retry_ready = false;
}

static void tx_retry_process(void) {
    tx_retry_packet_t *packet;
    if (s_tx_retry_count == 0u || s_conn_idx == 0xFFu ||
        !s_notify_enabled || !s_tx_retry_ready) return;
    packet = &s_tx_retry_packets[s_tx_retry_head];
    s_tx_retry_ready = false;
    inktag_om_health_stage(0x4F4D5458u,
                           INKTAG_OM_HEALTH_OPERATION_BLE); /* OMTX */
    if (inktag_om_notify(s_conn_idx, packet->data, packet->length) != 0u)
        return;
    inktag_om_health_stage(0x4F4D4944u,
                           INKTAG_OM_HEALTH_OPERATION_BLE); /* OMID */
    s_tx_retry_head = (uint8_t)((s_tx_retry_head + 1u) % TX_RETRY_QUEUE_DEPTH);
    s_tx_retry_count--;
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

static inktag_status_v2_t product_access(inktag_product_feature_t feature,
                                         bool implemented,
                                         bool board_capable,
                                         bool experiment_enabled) {
    return inktag_access_check(&s_activation, feature, implemented,
                               board_capable, experiment_enabled);
}

static inktag_status_v2_t screen_control_access(
    const epd_screen_config_t *screen, bool backend_available) {
    return product_access(screen != NULL &&
        epd_screen_is_e6(screen->driver_id, screen->panel_recipe) ?
            INKTAG_PRODUCT_FEATURE_E6_CONTROL : INKTAG_PRODUCT_FEATURE_BASIC,
        backend_available, true, true);
}

bool inktag_om_app_custom_input_allowed(void) {
    return false;
}



static inktag_status_v2_t epd_control_access(void) {
    return screen_control_access(&s_screen_config,
        s_epd != NULL && inktag_om_pins_screen_available());
}

bool inktag_om_app_epd_control_allowed(void) {
    return epd_control_access() == INKTAG_STATUS_OK;
}

static void send_v2_response(uint8_t opcode, uint8_t transaction_id,
                             inktag_status_v2_t status,
                             const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[INKTAG_OM_MAX_DATA_LEN];
    if (payload_length > sizeof(packet) - INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload = NULL;
        payload_length = 0u;
    }
    packet[0] = opcode;
    packet[1] = transaction_id;
    packet[2] = (uint8_t)status;
    if (payload && payload_length) {
        memcpy(&packet[INKTAG_PROTOCOL_V2_HEADER_SIZE], payload, payload_length);
    }
    send_bytes(packet, (uint16_t)(INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length));
}

/* The frequent empty ACK uses the identical three-byte wire, without passing
 * unused payload arguments or reserving a maximum-size payload frame. */
static __attribute__((noinline)) void send_v2_status(uint8_t opcode,uint8_t transaction_id,
                                                     inktag_status_v2_t status) {
    const uint8_t packet[]={opcode,transaction_id,(uint8_t)status};
    send_bytes(packet,sizeof(packet));
}

static void v2_finish_cancel(void) {
    uint8_t transaction = s_v2_cancel_transaction;
    s_v2_cancel_transaction = 0u;
    if (transaction != 0u) {
        send_v2_status(INKTAG_CMD_CANCEL, transaction, INKTAG_STATUS_OK);
    }
}

static void send_v2_event(uint8_t event, inktag_status_v2_t status,
                          uint8_t origin_opcode, uint8_t origin_transaction,
                          const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[INKTAG_OM_MAX_DATA_LEN];
    const uint16_t prefix = INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u;
    if (payload_length > sizeof(packet) - prefix) payload_length = sizeof(packet) - prefix;
    packet[0] = event;
    packet[1] = 0u;
    packet[2] = (uint8_t)status;
    packet[3] = origin_opcode;
    packet[4] = origin_transaction;
    if (payload && payload_length) memcpy(&packet[prefix], payload, payload_length);
    send_bytes(packet, (uint16_t)(prefix + payload_length));
}

static void send_text(const char *text) {
    if (!text) return;
    send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                  (const uint8_t *)text, (uint16_t)strlen(text));
}

static void send_v2_work_complete_payload(inktag_status_v2_t status,
                                          const uint8_t *payload,
                                          uint16_t payload_length) {
    if (s_v2_work_transaction == 0u) return;
    send_v2_event(INKTAG_EVENT_COMPLETE, status, s_v2_work_opcode,
                  s_v2_work_transaction, payload, payload_length);
    s_v2_work_opcode = 0u;
    s_v2_work_transaction = 0u;
}

static void send_v2_work_complete(inktag_status_v2_t status,
                                  uint8_t argument) {
    send_v2_work_complete_payload(status, &argument, sizeof(argument));
}

static void send_v2_ghost_clean_progress(uint8_t completed_passes,
                                         uint8_t total_passes) {
    uint8_t progress[2];
    if (s_v2_work_transaction == 0u || total_passes == 0u) return;
    progress[0] = INKTAG_BUSY_PHASE_DISPLAY;
    progress[1] = (uint8_t)(((uint16_t)completed_passes * 100u) /
                            total_passes);
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  s_v2_work_opcode, s_v2_work_transaction,
                  progress, sizeof(progress));
}

static void rx_packet_queue_reset(void) {
    s_rx_packet_head = 0u;
    s_rx_packet_tail = 0u;
    s_rx_packet_count = 0u;
}

static bool release_packet_admitted(const uint8_t *data,uint16_t length) {
    uint8_t semantic;
    return data && length>=INKTAG_PROTOCOL_V2_HEADER_SIZE && data[1]!=0u &&
        inktag_release_request_flags(data[2],&semantic);
}

static bool rx_packet_enqueue(const uint8_t *data, uint16_t length) {
    if(!release_packet_admitted(data,length))return false;
    if (!data || length < INKTAG_PROTOCOL_V2_HEADER_SIZE ||
        length > INKTAG_OM_MAX_DATA_LEN ||
        s_rx_packet_count >= RX_PACKET_QUEUE_DEPTH) return false;
    rx_packet_t *packet = &s_rx_packets[s_rx_packet_tail];
    packet->length = length;
    memcpy(packet->data, data, length);
    s_rx_packet_tail = (uint8_t)((s_rx_packet_tail + 1u) % RX_PACKET_QUEUE_DEPTH);
    s_rx_packet_count++;
    return true;
}

static bool rx_packet_dequeue(rx_packet_t *packet) {
    if (!packet || s_rx_packet_count == 0u) return false;
    *packet = s_rx_packets[s_rx_packet_head];
    s_rx_packet_head = (uint8_t)((s_rx_packet_head + 1u) % RX_PACKET_QUEUE_DEPTH);
    s_rx_packet_count--;
    return true;
}

extern uint8_t __inktag_image_end__;

static void send_screen_config(void) {
    uint8_t payload[EPD_SCREEN_WIRE_SIZE];
    if (epd_screen_config_encode(&s_screen_config, payload) == EPD_SCREEN_STATUS_OK) {
        send_bytes(payload, sizeof(payload));
    }
}

static void send_diag(const char *tag) {
#if defined(INKTAG_OM_DIAGNOSTICS)
    char buf[256];
    uint8_t raw_busy = EPD_ReadBusy() ? 1 : 0;
    uint8_t busy = s_epd && s_epd->drv && s_epd->drv->read_busy
                       ? (s_epd->drv->read_busy(s_epd) ? 1 : 0)
                       : raw_busy;
    snprintf(buf, sizeof(buf),
             "%s drv=%02x active=%02x raw=%u busy=%u v=%u edge=%u assert_ms=%" PRIu32 " busy_ms=%" PRIu32 " pins=%02x,%02x,%02x,%02x,%02x,%02x,%02x wk=%02x en=%02x led=%02x spi_bus=%u spi_ok=%u spi_err=%" PRId32,
             tag, s_screen_config.driver_id, s_epd ? s_epd->driver_id : 0xff,
             raw_busy, busy, EPD_ReadVoltage(),
             EPD_BusyDiagAsserted() ? 1 : 0,
             EPD_BusyDiagAssertDelayMs(), EPD_BusyDiagActiveMs(),
             s_config.mosi_pin, s_config.sclk_pin, s_config.cs_pin, s_config.dc_pin,
             s_config.rst_pin, s_config.busy_pin, s_config.bs_pin, s_config.wakeup_pin,
             s_config.en_pin, s_config.led_pin,
             EPD_OnMicroSpiIndex(), EPD_OnMicroSpiReady() ? 1 : 0,
             EPD_OnMicroSpiLastError());
    send_text(buf);
#else
    /* Keep completion/error tags; verbose pin/voltage/timing data is opt-in. */
    send_text(tag);
#endif
}

typedef struct {
    int16_t value_sixteenths;
    bool valid;
} om_temperature_sample_t;

typedef struct {
    om_temperature_sample_t mcu_die;
    om_temperature_sample_t panel_controller;
} om_temperature_sources_t;

/* The SDK's convenience reader returns a signed whole-degree value through a
 * pointer-sized result. Reject a busy ADC and implausible conversion instead
 * of turning either condition into a plausible 25 C reading. */
static om_temperature_sample_t read_chip_temperature(void) {
    om_temperature_sample_t sample = {0};
    if ((intptr_t)drv_adc_control(ADC_CONTROL_IS_BUSY, NULL) != 0) return sample;
    intptr_t temperature = (intptr_t)drv_adc_control(ADC_CONTROL_READ_TEMPERATURE, NULL);
    if (temperature < -40 || temperature > 125) return sample;
    sample.value_sixteenths = (int16_t)(temperature * 16);
    sample.valid = true;
    return sample;
}

/* A query must not repower the panel. A cached sample is usable only while
 * the sampled controller remains powered; parking invalidates that source. */
#define PANEL_TEMPERATURE_MAX_AGE_MS (15u * 60u * 1000u)
static om_temperature_sample_t s_panel_temperature_cache;
static uint32_t s_panel_temperature_sample_ms;
static void runtime_clock_update(void);

void inktag_om_app_epd_temperature_read(uint8_t command, const uint8_t *raw,
                                         uint8_t length, bool read_ok) {
    (void)command; (void)raw; (void)length; (void)read_ok;
}



static om_temperature_sample_t read_panel_temperature(void) {
    if (!s_epd_gpio_ready || !s_panel_temperature_cache.valid ||
        (uint32_t)(s_status_now_ms - s_panel_temperature_sample_ms) >
            PANEL_TEMPERATURE_MAX_AGE_MS) {
        om_temperature_sample_t invalid = {0};
        return invalid;
    }
    return s_panel_temperature_cache;
}

/* Only a successful refresh has the controller initialized and the rail on.
 * The common decoder owns wire format and failure semantics; this adapter
 * owns exact-board selection and a short-lived sample cache. */
static void sample_panel_temperature_after_refresh(void) {
    uint8_t command;
    uint8_t raw[2] = {0xFFu, 0xFFu};
    int16_t decoded = 0;
    bool ssd1680 = false;
    s_panel_temperature_cache.valid = false;
    if (!s_epd_gpio_ready || s_epd == NULL || !EPD_OnMicroSpiReady()) return;
    if (inktag_om_board_identity() == INKTAG_OM_BOARD_ZKC21V &&
        s_epd->driver_id == EPD_SCREEN_DRIVER_SSD1680 &&
        s_epd->panel_recipe == INKTAG_RECIPE_SSD1680_ZKC21V_FACTORY) {
        command = SSD1xxx_TSENSOR_READ;
        ssd1680 = true;
    } else if (inktag_om_board_identity() == INKTAG_OM_BOARD_ZKC21B &&
               s_epd->driver_id == EPD_SCREEN_DRIVER_JD79661 &&
               s_epd->panel_recipe == INKTAG_RECIPE_JD79661_ZKC21B_FACTORY_B2) {
        command = 0x40u;
    } else if (inktag_om_board_identity() == INKTAG_OM_BOARD_ZKC42B &&
               s_epd->driver_id == EPD_SCREEN_DRIVER_JD79668AA &&
               s_epd->panel_recipe == INKTAG_RECIPE_JD79668AA_ZKC42B_FACTORY_B3) {
        command = 0x40u;
    } else {
        return;
    }
    bool read_ok = EPD_ReadCommand(command, raw, sizeof(raw));
    bool valid = ssd1680
        ? inktag_ssd1680_r1b_temperature_sixteenths(read_ok, raw, &decoded)
        : inktag_jd_r40_temperature_sixteenths(read_ok, raw, &decoded);
    s_panel_temperature_cache.valid = valid;
    if (valid) {
        runtime_clock_update();
        s_panel_temperature_cache.value_sixteenths = decoded;
        s_panel_temperature_sample_ms = s_status_now_ms;
    }
    char text[68];
    snprintf(text, sizeof(text),
             "panel:temperature cmd=%02x raw=%02x,%02x valid=%u sixteenths=%d",
             command, raw[0], raw[1], valid ? 1u : 0u,
             valid ? decoded : 0);
    send_text(text);
}

static void sample_panel_temperature_after_init(void) {
    s_panel_temperature_cache.valid = false;
}

static void epd_panel_initialize(void) {
    if (!inktag_om_app_epd_control_allowed()) return;
    s_panel_temperature_cache.valid = false;
    s_epd->drv->init(s_epd);
    sample_panel_temperature_after_init();
}



static om_temperature_sources_t read_temperature_sources(void) {
    om_temperature_sources_t sources = {
        .mcu_die = read_chip_temperature(),
        .panel_controller = read_panel_temperature(),
    };
    return sources;
}

static bool select_effective_temperature(const om_temperature_sources_t *sources,
                                         int8_t *temperature) {
    if (sources == NULL) return false;
    return inktag_temperature_select_lower(
        sources->mcu_die.valid, sources->mcu_die.value_sixteenths,
        sources->panel_controller.valid,
        sources->panel_controller.value_sixteenths,
        temperature);
}

static bool read_effective_temperature(int8_t *temperature) {
    om_temperature_sources_t sources = read_temperature_sources();
    return select_effective_temperature(&sources, temperature);
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool screen_config_matches(const uint8_t encoded[EPD_SCREEN_WIRE_SIZE]) {
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return epd_screen_config_encode(&s_screen_config, current) == EPD_SCREEN_STATUS_OK &&
           epd_screen_wire_image_compatible(current, encoded);
}

static epd_model_t *image_store_model(void) {
    return epd_find(&s_screen_config);
}

static uint32_t image_store_expected_payload(const epd_model_t *model) {
    epd_screen_t resolved;
    if (!model || epd_screen_resolve(&s_screen_config, &resolved) != EPD_SCREEN_STATUS_OK) return 0;
    return resolved.frame_bytes;
}

/* Stable record IDs are ordered independently from their physical extent.
 * The common product layer applies the base limit to that stable order so a
 * Pro -> base -> Pro transition never renumbers, erases or exposes a random
 * subset of records. */
static bool image_store_record_ordinal(uint8_t wanted_id, uint16_t *wanted_ordinal) {
    uint8_t ordinal;
    if (inktag_image_store_ordinal(wanted_id, &ordinal) != INKTAG_IMAGE_STORE_OK)
        return false;
    if (wanted_ordinal) *wanted_ordinal = ordinal;
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
    if (!inktag_image_store_supported()) {
        inktag_image_store_status_t unavailable;
        return inktag_image_store_status(&s_screen_config, 0u, &unavailable);
    }
    uint16_t first = after_id == UINT8_MAX ? 0u : (uint16_t)after_id + 1u;
    for (uint16_t candidate = first;
         candidate < inktag_image_store_fixed_slot_count(); candidate++) {
        inktag_image_store_header_t current;
        inktag_image_store_result_t result =
            inktag_image_store_get((uint8_t)candidate, &current);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        if (image_store_record_visible((uint8_t)candidate)) {
            if (id) *id = (uint8_t)candidate;
            if (header) *header = current;
            return INKTAG_IMAGE_STORE_OK;
        }
    }
    return INKTAG_IMAGE_STORE_NOT_FOUND;
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

static inktag_image_store_result_t image_store_visible_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status) {
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, status);
    if (result != INKTAG_IMAGE_STORE_OK ||
        inktag_access_is_pro(&s_activation)) {
        return result;
    }

    status->saved_count = 0u;
    status->compatible_count = 0u;
    status->playable_count = 0u;
    status->incompatible_count = 0u;
    uint8_t cursor = UINT8_MAX;
    for (;;) {
        uint8_t id;
        inktag_image_store_header_t header;
        result = image_store_next_visible(cursor, &id, &header);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) break;
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        status->saved_count++;
        if (screen_config_matches(header.screen_config)) {
            status->compatible_count++;
            if (header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)
                status->playable_count++;
        } else {
            status->incompatible_count++;
        }
        cursor = id;
    }
    return INKTAG_IMAGE_STORE_OK;
}

/* Common owns progress; the vendor scheduler services BLE/timers between
 * bounded physical steps. No ACK precedes the durable completion event. */
static inktag_image_store_result_t image_store_finish(
    inktag_image_store_result_t result, inktag_image_store_event_type_t expected,
    uint16_t *removed) {
    if (result != INKTAG_IMAGE_STORE_OK) return result;
    for (uint16_t step = 0u; step <= 634u; step++) {
        inktag_image_store_event_t event;
        if (inktag_image_store_take_event(&event)) {
            if (event.type != expected) return INKTAG_IMAGE_STORE_BUSY;
            if (removed) *removed = event.removed;
            return event.result;
        }
        inktag_image_store_process();
        app_feed_wdt();
        evt_schedule();
        runtime_clock_update();
        if (expected == INKTAG_IMAGE_STORE_EVENT_BEGIN && !s_v2_transfer.active)
            return INKTAG_IMAGE_STORE_BUSY; /* CANCEL already consumed abort. */
        if (inktag_image_store_capture_active() &&
            (s_v2_transfer_abort_pending ||
             !inktag_device_password_command_allowed(&s_password,
                &s_password_config, INKTAG_CMD_TRANSFER_WRITE) ||
             status_deadline_reached(s_v2_transfer.last_activity_ms + 30000u)))
            inktag_image_store_abort();
    }
    return INKTAG_IMAGE_STORE_IO_ERROR;
}

static void image_store_abort(void) {
    inktag_image_store_event_t event;
    inktag_image_store_abort();
    (void)inktag_image_store_take_event(&event);
}

static inktag_status_v2_t image_store_create_access(
    const epd_screen_config_t *screen, uint32_t payload_size, uint8_t replace_id) {
    inktag_image_store_status_t status;
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, &status);
    if (result == INKTAG_IMAGE_STORE_UNSUPPORTED)
        return INKTAG_STATUS_UNSUPPORTED;
    if (result != INKTAG_IMAGE_STORE_OK)
        return INKTAG_STATUS_IO_ERROR;
    uint16_t ordinal = 0u;
    bool replacing = false;
    if (replace_id != UINT8_MAX) {
        if (replace_id >= inktag_image_store_fixed_slot_count())
            return INKTAG_STATUS_NOT_FOUND;
        inktag_image_store_header_t header;
        result = inktag_image_store_get(replace_id, &header);
        if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NOT_FOUND)
            return INKTAG_STATUS_IO_ERROR;
        replacing = result == INKTAG_IMAGE_STORE_OK;
        if (replacing && !image_store_record_ordinal(replace_id, &ordinal))
            return INKTAG_STATUS_IO_ERROR;
    }
    /* Snapshot before the in-place erase: no other image mutation or profile
     * change is admitted during capture. Commit rechecks current Pro state. */
    s_v2_transfer.image_saved_count = status.saved_count;
    s_v2_transfer.image_replacing = replacing;
    s_v2_transfer.image_replacement_ordinal = ordinal;
    return inktag_access_image_write(
        &s_activation, status.saved_count, replacing, ordinal,
        replacing || status.free_sectors >= status.required_sectors,
        s_screen_config.visible_width, s_screen_config.visible_height);
}

static inktag_image_store_result_t image_store_clear_visible(
    bool incompatible_only, uint16_t *removed) {
    return image_store_finish(incompatible_only
        ? inktag_image_store_clear_incompatible(&s_screen_config)
        : inktag_image_store_clear_all(), incompatible_only
        ? INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE
        : INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, removed);
}

static const char *image_store_result_name(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return "ok";
        case INKTAG_IMAGE_STORE_UNSUPPORTED: return "unsupported";
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT: return "bad-argument";
        case INKTAG_IMAGE_STORE_BUSY: return "busy";
        case INKTAG_IMAGE_STORE_NO_SPACE: return "no-space";
        case INKTAG_IMAGE_STORE_LOW_POWER: return "low-power";
        case INKTAG_IMAGE_STORE_IO_ERROR: return "io-error";
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return "size-mismatch";
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return "crc-mismatch";
        case INKTAG_IMAGE_STORE_NOT_FOUND: return "not-found";
        default: return "unknown";
    }
}

static void send_image_store_error(const char *operation,
                                   inktag_image_store_result_t result) {
    char buf[56];
    snprintf(buf, sizeof(buf), "img:%s:error=%s", operation,
             image_store_result_name(result));
    send_text(buf);
}

static void slideshow_stop(bool notify) {
    bool was_active = s_slideshow_active;
    s_slideshow_active = false;
    s_slideshow_next_at = 0;
    s_slideshow_current_id = UINT8_MAX;
    s_slideshow_advance_pending = false;
    s_input_slideshow_return_valid = false;
    if (notify && was_active) send_text("img:slideshow:stopped");
}

static void slideshow_arm_next_after_refresh(void) {
    s_slideshow_next_at = s_timestamp + s_slideshow_interval;
    s_slideshow_advance_pending = false;
}

static bool slideshow_select_next(uint8_t *id) {
    return image_store_model() != NULL &&
           image_store_next_visible_compatible(
               s_slideshow_current_id, true, id);
}

static bool diag_pin_valid(uint8_t pin) { return pin <= OM6626_GPIO_MAX_PIN; }
static uint32_t diag_pin_mask(uint8_t pin) { return diag_pin_valid(pin) ? (1u << pin) : 0; }

static void diag_pin_config(uint8_t pin, gpio_dir_t direction, gpio_level_t initial) {
    if (!diag_pin_valid(pin)) return;
    pin_config_t pin_cfg = {pin, {PINMUX_GPIO_MODE_CFG}, PMU_PIN_MODE_PP, PMU_PIN_DRIVER_CURRENT_NORMAL};
    gpio_config_t gpio_cfg = {OM_GPIO0, pin, direction, initial, GPIO_TRIG_NONE};
    if (direction == GPIO_DIR_INPUT) pin_cfg.mode = PMU_PIN_MODE_FLOAT;
    drv_pin_init(&pin_cfg, 1);
    drv_gpio_init(&gpio_cfg, 1);
}

static gpio_level_t board_led_level(uint8_t pin,bool on) {
    bool low=INKTAG_BOARD_LED_ACTIVE_LOW;
    for(unsigned i=0;i<3u;++i)
        if(pin==inktag_om_pins_led(i))low=inktag_om_pins_active_low((inktag_pin_role_t)(INKTAG_PIN_LED_RED+i));
    bool high = low ? !on : on;
    return high ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static void board_led_write(uint8_t pin, bool on) {
    if (!diag_pin_valid(pin)) return;
    gpio_level_t level = board_led_level(pin,on);
    diag_pin_config(pin, GPIO_DIR_OUTPUT, level);
    drv_gpio_write(OM_GPIO0, diag_pin_mask(pin), level);
}

static void board_led_apply(uint8_t pin, bool on) {
    if (inktag_om_aux_pin_claimed(pin)) return;
    board_led_write(pin, on);
}

#include "inktag_om_status_pwm.inc"


static void diag_led_write_mask(uint8_t color_mask) {
    status_pwm_stop();
#if INKTAG_BOARD_HAS_LED_DIAG
    board_led_apply(inktag_om_pins_led(0u),
                    (color_mask & INKTAG_STATUS_COLOR_RED) != 0u);
    board_led_apply(inktag_om_pins_led(1u),
                    (color_mask & INKTAG_STATUS_COLOR_GREEN) != 0u);
    board_led_apply(inktag_om_pins_led(2u),
                    (color_mask & INKTAG_STATUS_COLOR_BLUE) != 0u);
#else
    (void)color_mask;
#endif
}

static void status_led_force_off(void) {
    status_pwm_stop();
#if INKTAG_BOARD_HAS_LED_DIAG
    board_led_write(inktag_om_pins_led(0u), false);
    board_led_write(inktag_om_pins_led(1u), false);
    board_led_write(inktag_om_pins_led(2u), false);
#endif
}

static uint8_t status_led_available_colors(void) {
    uint8_t colors = 0u;
#if INKTAG_BOARD_HAS_LED_DIAG
    if (diag_pin_valid(inktag_om_pins_led(0u)) && !inktag_om_aux_pin_claimed(inktag_om_pins_led(0u))) colors |= INKTAG_STATUS_COLOR_RED;
    if (diag_pin_valid(inktag_om_pins_led(1u)) && !inktag_om_aux_pin_claimed(inktag_om_pins_led(1u))) colors |= INKTAG_STATUS_COLOR_GREEN;
    if (diag_pin_valid(inktag_om_pins_led(2u)) && !inktag_om_aux_pin_claimed(inktag_om_pins_led(2u))) colors |= INKTAG_STATUS_COLOR_BLUE;
#elif INKTAG_BOARD_HAS_STATUS_LED
    if (!inktag_om_aux_pin_claimed(INKTAG_BOARD_STATUS_LED_PIN)) colors = INKTAG_STATUS_COLOR_BLUE;
#endif
    return colors;
}

static void status_led_output(void *context, uint8_t color_mask) {
    (void)context;
    if (s_led_test_active) return;
    bool pwm = status_pwm_update();
#if INKTAG_BOARD_HAS_LED_DIAG
    board_led_apply(inktag_om_pins_led(0u),
                    (color_mask & INKTAG_STATUS_COLOR_RED) != 0u);
    board_led_apply(inktag_om_pins_led(1u),
                    (color_mask & INKTAG_STATUS_COLOR_GREEN) != 0u);
    if (!pwm) board_led_apply(inktag_om_pins_led(2u),
                             (color_mask & INKTAG_STATUS_COLOR_BLUE) != 0u);
#elif INKTAG_BOARD_HAS_STATUS_LED
    board_led_apply(INKTAG_BOARD_STATUS_LED_PIN, color_mask != 0u);
#else
    (void)color_mask;
#endif
    (void)pwm;
}

static void status_led_timer_handler(evt_timer_t *timer, void *param);

__RAM_CODES("APP")
static void power_sleep_notify(pm_sleep_state_t state, pm_status_t status) {
    /* SDK calls this only after accepting a real SLEEP/DEEP_SLEEP decision.
     * The callback also runs on wake; avoid XIP/common-code access then. */
    if (state == PM_SLEEP_ENTRY && status >= PM_STATUS_SLEEP) {
        inktag_soak_telemetry_sleep_entered();
    }
}

static void runtime_clock_update(void) {
    inktag_elapsed_time_t elapsed =
        inktag_elapsed_clock_sample_time_shared(&s_runtime_clock,
                                         drv_pmu_timer_cnt_get());
    s_status_now_ms += elapsed.milliseconds;
}

uint32_t inktag_om_app_monotonic_ms(void) { return s_status_now_ms; }

static void status_timer_update(void) {
    bool needed;
    if (!s_status_initialized) return;
    /* Selection can change while the software colour is already off, so
     * reconcile even when common correctly suppressed a duplicate callback. */
    (void)status_pwm_update();
    needed = s_led_test_active ||
             inktag_status_needs_periodic_tick(&s_status, s_status_now_ms);
    if (needed == s_status_timer_armed) return;
    if (needed) {
        s_status_timer_armed = evt_timer_set(
            &s_status_timer, STATUS_TIMER_INTERVAL_MS, EVT_TIMER_REPEAT,
            status_led_timer_handler, NULL);
    } else {
        evt_timer_del(&s_status_timer);
        s_status_timer_armed = false;
    }
}

static void status_led_timer_handler(evt_timer_t *timer, void *param) {
    bool locate_was_active;
    (void)timer;
    (void)param;
    runtime_clock_update();
    if (s_led_test_active &&
        (int32_t)(s_status_now_ms - s_led_test_deadline_ms) >= 0) {
        s_led_test_active = false;
        diag_led_write_mask(0u);
        s_status.last_output = 0xFFu;
    }
    locate_was_active = inktag_status_locate_active(&s_status);
    inktag_status_tick(&s_status, s_status_now_ms);
    if (locate_was_active && !inktag_status_locate_active(&s_status))
    status_timer_update();
}

static void status_led_init(void) {
    inktag_status_config_t config;
    /* The monotonic clock starts before NVDS, NFC and BLE initialization.
     * Account for that elapsed boot work before anchoring the visible RGB
     * round, or its first timer callback skips part of the red phase. */
    runtime_clock_update();
    status_led_force_off();
    inktag_om_device_settings_get_led(&config);
    if (inktag_access_indicator_policy(&s_activation, config.policy) !=
            INKTAG_STATUS_OK) {
        inktag_status_config_default(&config);
    }
    inktag_status_init(&s_status, &config, status_led_available_colors(),
                       status_led_output, NULL, s_status_now_ms);
    s_status_initialized = true;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_ADVERTISING,
                             true, s_status_now_ms);
    inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_STARTUP, s_status_now_ms);
    status_timer_update();
}

static void status_led_update_available(void) {
    if (!s_status_initialized) return;
    inktag_status_set_available_colors(&s_status, status_led_available_colors(),
                                       s_status_now_ms);
    status_timer_update();
}

#include "inktag_om_display_feedback.inc"

static void status_led_set_connected(bool connected) {
    if (!s_status_initialized) return;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_ADVERTISING,
                             !connected, s_status_now_ms);
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_CONNECTED,
                             false, s_status_now_ms);
    if (connected) {
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_CONNECTED,
                              s_status_now_ms);
    }
    status_timer_update();
}

static bool runtime_led_diag_available(void) {
#if INKTAG_BOARD_HAS_LED_DIAG
    return true;
#else
    return false;
#endif
}

static bool diag_set_led_mask(uint8_t color_mask) {
#if INKTAG_BOARD_HAS_LED_DIAG
    if (!runtime_led_diag_available() ||
        color_mask > INKTAG_STATUS_COLOR_WHITE) return false;
    runtime_clock_update();
    s_led_test_active = color_mask != 0u;
    s_led_test_deadline_ms = s_status_now_ms + DIAG_LED_TEST_MS;
    diag_led_write_mask(color_mask);
    if (!s_led_test_active) {
        s_status.last_output = 0xFFu;
        inktag_status_tick(&s_status, s_status_now_ms);
    }
    status_timer_update();
    return true;
#else
    (void)color_mask;
    return false;
#endif
}

static bool inktag_om_aux_io_supported(void) { return false; }









static void epd_gpio_open_if_needed(void) {
    if (!inktag_om_app_epd_control_allowed()) return;
    if (!s_epd_gpio_ready) {
        s_panel_temperature_cache.valid = false;
        /* Direct image transfers intentionally retain the initialized panel
         * bus across independently acknowledged BLE writes.  OM deep sleep
         * does not retain the active SPI transaction state, so cap power
         * management at event-aware IDLE until the panel bus is parked. */
        pm_sleep_prevent(PM_ID_USER2);
        EPD_GPIO_Init();
        s_epd_gpio_ready = true;
    }
}

static void epd_gpio_close_if_needed(void) {
    s_panel_temperature_cache.valid = false;
    if (s_epd_gpio_ready) {
        EPD_GPIO_Uninit();
        s_epd_gpio_ready = false;
        pm_sleep_allow(PM_ID_USER2);
    }
}

static void epd_release_owned_panel(void) {
    /* Authority gates new work, never an already-owned POF/sleep/park. */
    if (s_epd_gpio_ready) {
        if (s_epd) s_epd->drv->sleep(s_epd);
        epd_gpio_close_if_needed();
    }
}

static bool finish_first_display_request_if_allowed(void) {
    /* The Full first-display marker remains subject to ordinary display
     * admission, independently of the selected optional NFC provider. */
    return !inktag_om_app_epd_control_allowed() ||
        inktag_om_swap_finish_factory_device_info_request();
}

void inktag_om_app_configuration_park(void) {
    status_led_force_off();
    epd_gpio_close_if_needed();
}

static bool diag_epd_ensure_screen(void) {
    if(!inktag_om_pins_screen_available())return false;
    if (!s_epd) s_epd = epd_find(&s_screen_config);
    if (!s_epd) {
        send_text("epd:no-screen");
        epd_gpio_close_if_needed();
        return false;
    }
    if (!inktag_om_app_epd_control_allowed()) return false;
    epd_gpio_open_if_needed();
    send_screen_config();
    return true;
}

static void update_advertised_name(void) {
    uint8_t name_len = (uint8_t)strlen(s_device_name);
    static const uint8_t marker[] = {0x05, 0xFF, 0xF7, 0x04, 0x04, 0x03};
    if (name_len > INKTAG_BLE_DEVICE_NAME_MAX_BYTES) return;
    s_adv_data[3] = (uint8_t)(name_len + 1u);
    s_adv_data[4] = 0x09;
    memcpy(&s_adv_data[5], s_device_name, name_len);
    memcpy(&s_adv_data[5u + name_len], marker, sizeof(marker));
    s_adv.len = (uint16_t)(5u + name_len + sizeof(marker));
}

static bool apply_runtime_device_name(void) {
    snprintf(s_device_name, sizeof(s_device_name), "%s_%02X%02X",
             DEVICE_NAME_PREFIX, s_local_addr[1], s_local_addr[0]);
    update_advertised_name();
    return true;
}

static uint16_t device_display_id4(void) {
    /* Immutable ordinary-App suffix; never a Find/Recovery/renamed GAP value. */
    return ((uint16_t)s_local_addr[1] << 8) | s_local_addr[0];
}

static bool effective_device_display_name(char output[EPD_DEVICE_LABEL_MAX_BYTES + 1u]) {
    return inktag_device_display_name(output, EPD_DEVICE_LABEL_MAX_BYTES + 1u,
        s_device_settings.label, s_device_settings.label_length, device_display_id4());
}

static bool inktag_om_gap_addr_get(inktag_om_gap_address_t *address) {
    if (!address) return false;
    memcpy(address->gap_addr.addr, s_local_addr, sizeof(s_local_addr));
    return true;
}

static bool refresh_findmy_key_if_safe(void) { return true; }
static bool findmy_runtime_enabled(void) { return false; }

static void init_device_identity(uint8_t nvds_status) {
    s_identity_status = inktag_om_address_load(nvds_status, s_local_addr, &s_local_addr_type);
    /* Failure is inspectable over the existing debug path, never a fake MAC
     * or automatic persistence repair. Watchdog is disabled at this stage. */
    if (s_identity_status > INKTAG_OM_ADDRESS_HS_LEGACY)
        for (;;) { __WFI(); }
    snprintf(s_device_name, sizeof(s_device_name), "%s_%02X%02X", DEVICE_NAME_PREFIX,
             s_local_addr[1], s_local_addr[0]);
    (void)inktag_om_device_label_read(s_device_label);
    update_advertised_name();
}

static bool pins_apply_to_config(void) {
    inktag_pin_config_t routing;
    if(!inktag_om_pins_effective(&routing)) {s_epd=NULL;return false;}
    s_config.mosi_pin=routing.pins[0];s_config.sclk_pin=routing.pins[1];
    s_config.cs_pin=routing.pins[2];s_config.dc_pin=routing.pins[3];
    s_config.rst_pin=routing.pins[4];s_config.busy_pin=routing.pins[5];
    s_config.en_pin=routing.pins[6];
    EPD_GPIO_Load(&s_config);
    if(!inktag_om_pins_screen_available())s_epd=NULL;
    return true;
}

static void load_default_config_if_needed(void) {
    uint32_t saved_timestamp;
    bool factory_device_info = inktag_om_swap_factory_device_info_requested();
    bool display_state_missing;

    epd_config_init(&s_config);
    epd_config_read(&s_config);
    if (epd_config_empty(&s_config)) {
        uint8_t cfg[] = INKTAG_BOARD_DEFAULT_EPD_CONFIG;
        memcpy(&s_config, cfg, sizeof(cfg));
        s_config.display_mode = MODE_PICTURE;
        s_config.week_start = INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
    }
    display_state_missing = !epd_display_state_read(&s_display_state);
    if (display_state_missing) {
        inktag_display_state_default(&s_display_state);
    } else if (factory_device_info) {
        inktag_display_state_default(&s_display_state);
    }
    s_display_state_loaded = true;
    /* FactoryDeviceInfo is the final Full-image phase name. The common
     * one-shot plan selects device information after startup completes. */
    s_first_display_requested = factory_device_info;
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_config.display_mode = epd_display_state_driver_mode(s_display_state.mode);
    bool has_valid_screen = epd_screen_config_read(&s_screen_config) &&
                            epd_find(&s_screen_config) != NULL;
    /* Full seeds the exact profile offline. Missing/invalid media cannot
     * cause a guessed display route, Flash repair or a normal-boot redraw. */
    s_epd = has_valid_screen ? epd_find(&s_screen_config) : NULL;
    if (epd_time_state_read(&saved_timestamp, &s_timezone_minutes)) {
        /* The bounded retained anchor already includes startup elapsed time. */
        set_timestamp(saved_timestamp);
    }
    (void)pins_apply_to_config();
}

typedef struct {
    epd_model_t *epd;
    bool black_plane;
    bool begin;
} epd_gui_ram_plane_t;

static bool record_display_state(uint8_t mode, uint8_t argument,
                                 uint32_t interval_seconds) {
    epd_display_state_t next = {
        .mode = mode,
        .argument = argument,
        .reserved = s_display_state_loaded ? s_display_state.reserved : 0u,
        .interval_seconds = interval_seconds,
    };
    s_config.display_mode = epd_display_state_driver_mode(mode);
    if (!s_display_state_loaded || memcmp(&next, &s_display_state, sizeof(next)) != 0) {
        if (epd_display_state_write(&next)) {
            s_display_state = next;
            s_display_state_loaded = true;
        } else {
            return false;
        }
    }
    return true;
}

static bool display_state_flag(uint16_t flag) {
    return s_display_state_loaded && (s_display_state.reserved & flag) != 0u;
}

static bool automatic_display_refresh_allowed(void) {
    return !s_lifecycle_content_locked && inktag_om_app_epd_control_allowed();
}

static void apply_display_settings_runtime(
    const inktag_display_settings_t *settings) {
    uint16_t user_flags = EPD_DISPLAY_STATE_USER_FLAGS_MASK;
    if (!settings) return;
    s_config.week_start = settings->week_start;
    s_display_state.reserved = (uint16_t)(
        (s_display_state.reserved & (uint16_t)~user_flags) |
        ((uint16_t)settings->flags & user_flags));
}

static bool status_deadline_reached(uint32_t deadline_ms) {
    return (int32_t)(s_status_now_ms - deadline_ms) >= 0;
}

static bool native_refresh_ble_idle(void) {
    return s_conn_idx == 0xFFu ||
        (status_deadline_reached(s_ble_discovery_until_ms) &&
         status_deadline_reached(s_ble_command_idle_after_ms) &&
         s_rx_packet_count == 0u && !s_v2_transfer.active &&
         !s_picture_io_active && !inktag_image_store_capture_active());
}

static void epd_gui_write_ram_plane(void *user_data, uint8_t *black, uint8_t *color,
                                    uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    epd_gui_ram_plane_t *plane = (epd_gui_ram_plane_t *)user_data;
    (void)color;
    if (!plane || !plane->epd || !black ||
        !inktag_om_app_epd_control_allowed()) return;
    if (x != 0 || w != plane->epd->width || y >= plane->epd->height || h == 0) return;

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

static bool epd_gui_update(display_mode_t mode, uint32_t draw_timestamp) {
    if (!inktag_om_app_epd_control_allowed()) return false;
    if (!s_epd) {
        send_text("gui:no-screen");
        return false;
    }
    epd_gpio_open_if_needed();
    epd_panel_initialize();

    char screen_info[32];
    const char *color_name = s_epd->color == COLOR_BW ? "BW" :
                             s_epd->color == COLOR_BWR ? "BWR" :
                             s_epd->color == COLOR_BWRY ? "BWRY" : "COLOR";
    snprintf(screen_info, sizeof(screen_info), "%ux%u / %s",
             s_epd->visible_width, s_epd->visible_height, color_name);
    if (mode == MODE_DEVICE_INFO) {
        for (uint8_t i = 0u; i < INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT; i++) {
            s_device_info_qr_valid[i] = inktag_om_device_info_qr_read(i, &s_device_info_qr[i]);
        }
    }

    inktag_display_settings_t display_settings;
    gui_countdown_t countdown;
    const gui_countdown_t *countdown_ptr = NULL;
    inktag_om_device_settings_get_display(&display_settings);
    if (display_settings.target_day != 0u &&
        display_settings.name_length != 0u) {
        countdown.name = display_settings.name;
        countdown.target_day = display_settings.target_day;
        countdown_ptr = &countdown;
    }
    int8_t temperature = GUI_TEMPERATURE_UNAVAILABLE;
    (void)read_effective_temperature(&temperature);
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
        .week_start = display_settings.week_start,
        .countdown = countdown_ptr,
        .temperature = temperature,
        .voltage = EPD_ReadVoltage(),
        .activated = false,
        .brand_hidden = false,
        .mcu_name = "OM6626",
        .bluetooth_name = s_device_name,
        .device_name = s_device_label[0] ? s_device_label : NULL,
        .screen_info = screen_info,
    };
    (void)GUI_SetDeviceName(&data, s_device_settings.label,
                            s_device_settings.label_length, device_display_id4());
    for (uint8_t i = 0u; i < INKTAG_OM_DEVICE_INFO_QR_SLOT_COUNT; i++) {
        if (s_device_info_qr_valid[i]) {
            data.device_info_qr_data[i] = s_device_info_qr[i].data;
            data.device_info_qr_modules[i] = s_device_info_qr[i].modules;
        }
    }

    send_diag("gui:start");
    if (EPD_GuiNeedsContiguousColorPlanes(s_epd)) {
        gui_options_t black_options = {
            .color_plane = GFX_COLOR_PLANE_BLACK,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
            .render_buffer = s_gui_frame_buffer,
            .render_buffer_size = sizeof(s_gui_frame_buffer),
        };
        epd_gui_ram_plane_t black_plane = {.epd = s_epd, .black_plane = true, .begin = true};
        send_diag("gui:black");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &black_plane, &black_options)) {
            send_text("gui:no-memory");
            return false;
        }
        app_feed_wdt();
        if (EPD_AbortRequested()) return false;

        gui_options_t red_options = {
            .color_plane = GFX_COLOR_PLANE_RED,
            .force_buffer_color = true,
            .buffer_color = COLOR_BW,
            .render_buffer = s_gui_frame_buffer,
            .render_buffer_size = sizeof(s_gui_frame_buffer),
        };
        epd_gui_ram_plane_t red_plane = {.epd = s_epd, .black_plane = false, .begin = true};
        send_diag("gui:red");
        if (!DrawGUIEx(&data, epd_gui_write_ram_plane, &red_plane, &red_options)) {
            send_text("gui:no-memory");
            return false;
        }
        app_feed_wdt();
    } else if (s_epd->ic == DRV_IC_JD79XXX_ZKC && s_epd->color == COLOR_BWRY) {
        /* The factory B2/B3 writer opens DTM1 once and streams one complete
         * packed 2-bpp frame.  OM6626 has enough application SRAM for the
         * 8000-byte B2 and 30000-byte B3 frames, so avoid exposing lengthy
         * GUI render pauses between controller data pages. */
        gui_options_t full_frame_options = {
            .color_plane = GFX_COLOR_PLANE_ALL,
            .prefer_full_frame = true,
            .render_buffer = s_gui_frame_buffer,
            .render_buffer_size = sizeof(s_gui_frame_buffer),
        };
        send_diag("gui:draw-full");
        if (!DrawGUIEx(&data, (buffer_callback)s_epd->drv->write_image, s_epd, &full_frame_options)) {
            send_text("gui:no-memory");
            return false;
        }
        app_feed_wdt();
    } else {
        gui_options_t page_options = {
            .color_plane = GFX_COLOR_PLANE_ALL,
            .render_buffer = s_gui_frame_buffer,
            .render_buffer_size = sizeof(s_gui_frame_buffer),
        };
        send_diag("gui:draw");
        if (!DrawGUIEx(&data, (buffer_callback)s_epd->drv->write_image, s_epd, &page_options)) {
            send_text("gui:no-memory");
            return false;
        }
        app_feed_wdt();
    }

    if (EPD_AbortRequested()) return false;
    send_diag("gui:refresh");
    s_epd->drv->refresh(s_epd);
    return !EPD_AbortRequested();
}

static bool image_store_show(uint8_t id) {
    if (!inktag_om_app_epd_control_allowed()) return false;
    inktag_image_store_header_t header;
    inktag_image_store_result_t result = inktag_image_store_get(id, &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_image_store_error("show", result);
        return false;
    }
    if (!image_store_model() || !screen_config_matches(header.screen_config)) {
        send_text("img:show:error=incompatible");
        return false;
    }

    char buf[48];
    snprintf(buf, sizeof(buf), "img:show:start id=%u", id);
    send_text(buf);
    epd_gpio_open_if_needed();
    epd_panel_initialize();

    uint8_t data[INKTAG_OM_MAX_DATA_LEN];
    uint32_t offset = 0;
    for (uint8_t segment = 0; segment < header.segment_count; segment++) {
        uint32_t segment_offset = 0;
        uint32_t remaining = header.segment_size[segment];
        bool begin = true;
        bool black = (header.black_mask & (1u << segment)) != 0;
        while (remaining > 0) {
            if (EPD_AbortRequested()) return false;
            uint16_t chunk = remaining > sizeof(data) ? sizeof(data) : (uint16_t)remaining;
            result = inktag_image_store_read(id, offset, data, chunk);
            if (result != INKTAG_IMAGE_STORE_OK) {
                send_image_store_error("show", result);
                return false;
            }
            if (!inktag_om_brand_apply_oriented(
                    &s_screen_config, &s_activation,
                    display_state_flag(EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN),
                    header.format, segment,
                    segment_offset, data, chunk,
                    inktag_image_reading_rotation(header.black_mask))) {
                send_text("img:show:error=brand");
                return false;
            }
            if (!EPD_DriverWriteRam(s_epd, begin, black, data, chunk))
                return false;
            begin = false;
            offset += chunk;
            segment_offset += chunk;
            remaining -= chunk;
            app_feed_wdt();
        }
    }
    snprintf(buf, sizeof(buf), "img:show:refresh id=%u", id);
    send_text(buf);
    s_epd->drv->refresh(s_epd);
    return !EPD_AbortRequested();
}

static bool schedule_epd_work_internal(epd_work_type_t type, uint8_t arg,
                                       const char *queued_text,
                                       bool background, bool client_owned) {
    if(!inktag_om_app_epd_control_allowed())return false;
    if (s_epd_work_active ||
        (s_epd_work_pending && (!s_epd_work_background || background))) {
        send_text("epd:busy");
        return false;
    }
    if (!background) {
        if (type == EPD_WORK_GUI && arg == s_native_refresh_mode)
            s_native_refresh_pending = false;
    }
    s_epd_work_type = type;
    s_epd_work_arg = arg;
    s_epd_work_background = background;
    s_epd_work_client_owned = client_owned;
    s_epd_work_pending = true;
    if (queued_text) send_text(queued_text);
    return true;
}

static bool schedule_epd_work(epd_work_type_t type, uint8_t arg,
                              const char *queued_text) {
    return schedule_epd_work_internal(type, arg, queued_text, false, false);
}

static bool schedule_epd_work_client(epd_work_type_t type, uint8_t arg) {
    return schedule_epd_work_internal(type, arg, NULL, false, true);
}

static bool schedule_epd_work_background(epd_work_type_t type, uint8_t arg) {
    return schedule_epd_work_internal(type, arg, NULL, true, false);
}

static void discard_pending_background_epd_work(void) {
    if (!s_epd_work_pending || !s_epd_work_background || s_epd_work_active)
        return;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_work_arg = 0u;
    s_epd_work_timestamp = 0u;
    s_epd_work_pending = false;
    s_epd_work_background = false;
    s_epd_work_client_owned = false;
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
    if (!inktag_om_app_epd_control_allowed()) return;
    if (!s_display_state_loaded) return;
    if (s_display_state.mode == EPD_DISPLAY_STATE_CALENDAR ||
        s_display_state.mode == EPD_DISPLAY_STATE_CLOCK) {
        s_epd_work_timestamp = s_timestamp;
        (void)schedule_epd_work_background(EPD_WORK_GUI,
                                           s_display_state.mode);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        s_epd_work_timestamp = s_timestamp;
        (void)schedule_epd_work_background(EPD_WORK_GUI, MODE_DEVICE_INFO);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_CLEAR) {
        uint8_t clear_white = s_display_state.argument <= 1u ? s_display_state.argument : 1u;
        (void)schedule_epd_work_background(EPD_WORK_CLEAR, clear_white);
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_PICTURE) {
        if (stored_image_replayable(s_display_state.argument, false)) {
            (void)schedule_epd_work_background(EPD_WORK_IMAGE_SHOW,
                                               s_display_state.argument);
        }
    } else if (s_display_state.mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = s_display_state.argument;
        uint32_t interval = s_display_state.interval_seconds;
        if (interval < 30u || interval > 86400u) interval = 300u;
        s_slideshow_current_id = UINT8_MAX;
        if (!stored_image_replayable(id, true) && !slideshow_select_next(&id)) return;
        s_slideshow_interval = interval;
        s_slideshow_active = true;
        s_slideshow_current_id = id;
        s_slideshow_next_at = s_timestamp + interval;
        (void)schedule_epd_work_background(EPD_WORK_IMAGE_SHOW, id);
    }
    /* A transient picture with no stored source intentionally leaves retained
     * e-paper contents untouched after reboot. */
}

static void complete_startup_if_ready(void) {
    if (s_boot_restore_scheduled) return;
    if (s_startup_ble_status != OB_ERROR_NO_ERR ||
        s_startup_service_status != OB_ERROR_NO_ERR ||
        s_startup_callback_status != OB_ERROR_NO_ERR ||
        s_startup_advertising_status != OB_ERROR_NO_ERR) {
        return;
    }

    inktag_swap_state_t state = INKTAG_SWAP_STATE_NONE;
    bool installed_update =
        inktag_om_swap_status(&state, NULL) == INKTAG_SWAP_OK &&
        state == INKTAG_SWAP_STATE_TRIAL_STARTED;
    inktag_swap_result_t result = inktag_om_swap_confirm();
    s_startup_swap_confirm_status = (uint32_t)result;
    if (result != INKTAG_SWAP_OK && result != INKTAG_SWAP_NOT_PENDING) {
        /* A marginal rail can fail the force-sampled 2.6 V OTA metadata gate
         * once while clocks/radio settle.  The old one-shot path left a
         * healthy application permanently unconfirmed and the next reset in
         * Recovery.  Retry from the ordinary event loop; the storage callback
         * keeps the real write power-gated and this path never busy-waits. */
        if (installed_update && result == INKTAG_SWAP_IO_ERROR) {
            uint32_t delay_ms = s_startup_swap_confirm_failures == 0u ?
                OTA_TRIAL_CONFIRM_FIRST_RETRY_MS :
                OTA_TRIAL_CONFIRM_RETRY_MS;
            if (s_startup_swap_confirm_failures != UINT8_MAX)
                s_startup_swap_confirm_failures++;
            s_startup_swap_confirm_retry_at_ms = s_status_now_ms + delay_ms;
            s_startup_swap_confirm_retry_pending = true;
        }
        return;
    }

    s_startup_swap_confirm_retry_pending = false;
    inktag_status_role_ready(&s_status, INKTAG_STATUS_READY_APPLICATION,
                             s_status_now_ms);
    s_boot_restore_scheduled = true;
    if (s_first_display_requested)
        (void)finish_first_display_request_if_allowed();
    inktag_boot_display_plan_t boot_plan = inktag_boot_display_plan(
        s_first_display_requested, installed_update, &s_display_state);
    s_first_display_requested = false;
    if (!inktag_om_app_epd_control_allowed()) {
        s_boot_restore_pending = s_boot_restore_due = false;
        return; /* BLE/activation remain ready; do not consume the Full marker. */
    }
    if (boot_plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO) {
        s_display_state.mode = EPD_DISPLAY_STATE_DEVICE_INFO;
        s_display_state.argument = EPD_DISPLAY_STATE_NO_SOURCE;
        s_config.display_mode = MODE_DEVICE_INFO;
    }
    s_boot_restore_pending = boot_plan.target != INKTAG_BOOT_DISPLAY_NONE;
    if (installed_update) {
        /* The common planner suppresses the OTA install boot even when an
         * opt-in replay or stale first-display marker exists. */
        s_boot_restore_pending = false;
        s_boot_restore_due = false;
        return;
    }
    /* Normally the advertising-started event promotes this request.  Cover
     * the synchronous-callback case as well without running display work from
     * the BLE event or startup stack. */
    if (s_advertising_active && s_boot_restore_pending) {
        s_boot_restore_pending = false;
        s_boot_restore_due = true;
    }
}



static void stop_current_operation(void) {
    slideshow_stop(true);
    s_image_store_abort_pending = inktag_image_store_capture_active();
    s_epd_work_pending = false;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_cancel_requested = true;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             false, s_status_now_ms);
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                             false, s_status_now_ms);
    send_text(s_epd_work_active ? "epd:stopping" : "epd:stopped");
}

/* Common owns the four logical pages; timer rotation still owns image steps.
 * Resolve without starting rotation, then publish it only after queue admission. */
static bool page_cycle_resolve(bool previous, epd_work_type_t *type, uint8_t *arg) {
    uint8_t first_id = UINT8_MAX;
    bool available = image_store_model() != NULL &&
        image_store_next_visible_compatible(UINT8_MAX, true, &first_id);
    const epd_display_state_t *current = s_display_state_loaded ? &s_display_state : NULL;
    inktag_page_cycle_target_t target = previous ?
        inktag_page_cycle_previous(current, available, false) :
        inktag_page_cycle_next(current, available, false);
    if (target == INKTAG_PAGE_CYCLE_SLIDESHOW) {
        *type = EPD_WORK_IMAGE_SHOW;
        *arg = first_id;
    } else {
        *type = EPD_WORK_GUI;
        *arg = target == INKTAG_PAGE_CYCLE_CLOCK ? MODE_CLOCK :
               target == INKTAG_PAGE_CYCLE_DEVICE_INFO ? MODE_DEVICE_INFO : MODE_CALENDAR;
    }
    return true;
}

static void page_cycle_scheduled(epd_work_type_t type, uint8_t arg) {
    (void)arg; /* The image ID is committed at successful panel completion. */
    if (type != EPD_WORK_IMAGE_SHOW) {
        slideshow_stop(true);
        s_epd_work_timestamp = s_timestamp;
        return;
    }
    s_slideshow_interval = s_slideshow_interval >= 30u ? s_slideshow_interval : 300u;
    s_slideshow_active = true;
    s_slideshow_next_at = s_timestamp + s_slideshow_interval;
}

static bool restore_input_display(const epd_display_state_t *state, const char *origin) {
    if (!state) return false;
    char queued[40];
    if (state->mode == EPD_DISPLAY_STATE_CALENDAR || state->mode == EPD_DISPLAY_STATE_CLOCK) {
        snprintf(queued, sizeof(queued), "%s:%s:queued", origin,
                 state->mode == EPD_DISPLAY_STATE_CALENDAR ? "calendar" : "clock");
        s_epd_work_timestamp = s_timestamp;
        return schedule_epd_work(EPD_WORK_GUI, state->mode, queued);
    }
    if (state->mode == EPD_DISPLAY_STATE_DEVICE_INFO) {
        snprintf(queued, sizeof(queued), "%s:device-info:queued", origin);
        return schedule_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO, queued);
    }
    if (state->mode == EPD_DISPLAY_STATE_CLEAR) {
        uint8_t clear_white = state->argument <= 1u ? state->argument : 1u;
        snprintf(queued, sizeof(queued), "%s:clear:queued", origin);
        return schedule_epd_work(EPD_WORK_CLEAR, clear_white, queued);
    }
    if (state->mode == EPD_DISPLAY_STATE_PICTURE &&
        stored_image_replayable(state->argument, false)) {
        snprintf(queued, sizeof(queued), "%s:slot:queued", origin);
        return schedule_epd_work(EPD_WORK_IMAGE_SHOW, state->argument, queued);
    }
    if (state->mode == EPD_DISPLAY_STATE_SLIDESHOW) {
        uint8_t id = state->argument;
        if (!stored_image_replayable(id, true) && !slideshow_select_next(&id)) return false;
        snprintf(queued, sizeof(queued), "%s:slideshow:queued", origin);
        if (!schedule_epd_work(EPD_WORK_IMAGE_SHOW, id, queued)) return false;
        s_slideshow_interval = state->interval_seconds >= 30u ? state->interval_seconds : 300u;
        s_slideshow_active = true;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
        return true;
    }
    return false;
}

static void process_input_action(uint8_t action, uint8_t slot_id,
                                 const char *origin, bool button_source) {
    char queued[40];
    if (!button_source) return;
    inktag_input_source_t source = INKTAG_INPUT_SOURCE_BUTTON_SINGLE;
    inktag_status_v2_t access =
        inktag_access_input_action(&s_activation, source, action);
    if (access != INKTAG_STATUS_OK) {
        snprintf(queued, sizeof(queued), "%s:denied", origin);
        send_text(queued);
        return;
    }
    if (action == INKTAG_INPUT_ACTION_DISABLED) {
        snprintf(queued, sizeof(queued), "%s:disabled", origin);
        send_text(queued);
        return;
    }
    if (action == INKTAG_INPUT_ACTION_STOP) {
        stop_current_operation();
        return;
    }
    if (!inktag_om_app_epd_control_allowed()) {
        snprintf(queued, sizeof(queued), "%s:denied", origin);
        send_text(queued);
        return;
    }
    if (action == INKTAG_INPUT_ACTION_SHOW_SLOT) {
        slideshow_stop(true);
        snprintf(queued, sizeof(queued), "%s:slot:queued", origin);
        if (!stored_image_replayable(slot_id, false) ||
            !schedule_epd_work(EPD_WORK_IMAGE_SHOW, slot_id, queued)) {
            snprintf(queued, sizeof(queued), "%s:slot-missing", origin);
            send_text(queued);
        }
        return;
    }
    if (action == INKTAG_INPUT_ACTION_SHOW_DEVICE_INFO) {
        slideshow_stop(true);
        snprintf(queued, sizeof(queued), "%s:device-info:queued", origin);
        (void)schedule_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO, queued);
        return;
    }
    if (action == INKTAG_INPUT_ACTION_SHOW_CALENDAR ||
        action == INKTAG_INPUT_ACTION_SHOW_CLOCK) {
        slideshow_stop(true);
        bool calendar = action == INKTAG_INPUT_ACTION_SHOW_CALENDAR;
        snprintf(queued, sizeof(queued), "%s:%s:queued", origin,
                 calendar ? "calendar" : "clock");
        s_epd_work_timestamp = s_timestamp;
        (void)schedule_epd_work(EPD_WORK_GUI, calendar ? MODE_CALENDAR : MODE_CLOCK, queued);
        return;
    }
    if (action == INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW) {
        if (s_slideshow_active) {
            epd_display_state_t return_state = s_input_slideshow_return_state;
            bool return_valid = s_input_slideshow_return_valid;
            slideshow_stop(false);
            s_input_slideshow_return_valid = false;
            send_text("img:slideshow:stopped");
            if (return_valid && restore_input_display(&return_state, origin)) return;
            snprintf(queued, sizeof(queued), "%s:device-info:queued", origin);
            (void)schedule_epd_work(EPD_WORK_GUI, MODE_DEVICE_INFO, queued);
            return;
        }
        uint8_t first_id = UINT8_MAX;
        if (image_store_model() == NULL ||
            !image_store_next_visible_compatible(UINT8_MAX, true, &first_id)) {
            if (!button_source) send_text("img:slideshow:error=no-compatible-images");
            return;
        }
        snprintf(queued, sizeof(queued), "%s:slideshow:queued", origin);
        if (!schedule_epd_work(EPD_WORK_IMAGE_SHOW, first_id, queued)) return;
        s_input_slideshow_return_valid =
            s_display_state_loaded &&
            s_display_state.mode != EPD_DISPLAY_STATE_SLIDESHOW;
        if (s_input_slideshow_return_valid)
            s_input_slideshow_return_state = s_display_state;
        s_slideshow_interval = s_slideshow_interval >= 30u ? s_slideshow_interval : 300u;
        s_slideshow_active = true;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
        return;
    }
    if (action == INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS) {
        if (!s_display_state_loaded || s_display_state.mode != EPD_DISPLAY_STATE_CLEAR) {
            snprintf(queued, sizeof(queued), "%s:clear:queued", origin);
            if (!schedule_epd_work(EPD_WORK_CLEAR, 1u, queued)) return;
            if (s_display_state_loaded) {
                s_clear_return_state = s_display_state;
                s_clear_return_valid = true;
            }
            slideshow_stop(true);
        } else if (!s_clear_return_valid ||
                   !restore_input_display(&s_clear_return_state, origin)) {
            snprintf(queued, sizeof(queued), "%s:return-missing", origin);
            send_text(queued);
        }
        return;
    }

    if (action != INKTAG_INPUT_ACTION_NEXT_PAGE &&
        action != INKTAG_INPUT_ACTION_PREVIOUS_PAGE) {
        snprintf(queued, sizeof(queued), "%s:unsupported", origin);
        send_text(queued);
        return;
    }
    epd_work_type_t type;
    uint8_t arg;
    snprintf(queued, sizeof(queued), "%s:page:queued", origin);
    if (page_cycle_resolve(action == INKTAG_INPUT_ACTION_PREVIOUS_PAGE, &type, &arg) &&
        schedule_epd_work(type, arg, queued)) page_cycle_scheduled(type, arg);
}

static bool input_action_busy(void) {
    return s_epd_work_active || s_epd_work_pending || s_v2_transfer.active ||
           s_picture_io_active ||
           s_system_action_pending != 0u;
}


static bool persist_time_snapshot(void) {
    /* Capture UTC at the existing write entry; never reset its PMU sampler
     * across synchronous NVDS work. Keep the current v1 writer/voltage gate. */
    bool saved = epd_time_state_write(timestamp(), s_timezone_minutes);
    runtime_clock_update();
    if (saved) inktag_time_checkpoint_saved(&s_time_checkpoint, s_status_now_ms);
    else inktag_time_checkpoint_failed(&s_time_checkpoint, s_status_now_ms);
    return saved;
}

static void process_time_checkpoint(void) {
    if (!s_time_valid || s_lifecycle_content_locked || input_action_busy() ||
        s_ota_fixed_boot_pending || false || false ||
        s_image_store_abort_pending || s_v2_transfer_abort_pending ||
        inktag_om_device_settings_io_busy() || inktag_image_store_capture_active()) return;
    if (inktag_time_checkpoint_due(&s_time_checkpoint, s_status_now_ms))
        (void)persist_time_snapshot();
}


static inktag_status_v2_t onmicro_ota_enter(void) {
    if (!inktag_device_password_allowed(&s_password, &s_password_config))
        return INKTAG_STATUS_DENIED;
    inktag_om_recovery_request_result_t request =
        inktag_om_recovery_request();
    if (request != INKTAG_OM_RECOVERY_REQUEST_OK) {
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_ERROR,
                              s_status_now_ms);
        return request == INKTAG_OM_RECOVERY_REQUEST_LOW_POWER ?
            INKTAG_STATUS_LOW_POWER : INKTAG_STATUS_UNSUPPORTED;
    }
    inktag_om_health_stage(0x4F4D4F54u, INKTAG_OM_HEALTH_OPERATION_OTA); /* OMOT */
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_OTA,
                             true, s_status_now_ms);
    if (!evt_timer_set(&s_ota_recovery_timer, OTA_RECOVERY_FEEDBACK_MS,
                       EVT_TIMER_ONE_SHOT, ota_recovery_timer_handler, NULL)) {
        s_ota_fixed_boot_pending = true;
    }
    return INKTAG_STATUS_OK;
}

static void process_pending_system_action(void) {
    if (s_system_action_pending == 0u ||
        !status_deadline_reached(s_system_action_not_before_ms)) return;
    /* A time delay alone does not prove that the controller transmitted the
     * response notification. Keep the connection alive until the retry FIFO
     * is empty and the final accepted notification reports TX_COMPLETE. If
     * the client has already disconnected there is no reply left to protect. */
    if (s_conn_idx != 0xFFu &&
        (s_tx_retry_count != 0u || !s_tx_retry_ready)) return;
    uint8_t action = s_system_action_pending;
    s_system_action_pending = 0u;
    s_system_action_not_before_ms = 0u;
    if (action == INKTAG_SYSTEM_ACTION_RESET ||
        action == INKTAG_SYSTEM_ACTION_RESET_SETTINGS ||
        action == INKTAG_SYSTEM_ACTION_FACTORY_RESET) {
        drv_pmu_force_reboot();
    }
}

void inktag_om_app_process(void) {
    /* A timer callback is only a wake/sampling opportunity. Derive every
     * product deadline from the always-running PMU counter so delayed or
     * coalesced callbacks never turn into counted 50 ms/1 s ticks. */
    runtime_clock_update();
    wall_clock_update();
    inktag_soak_telemetry_radio_maintenance();
    app_feed_wdt();
    tx_retry_process();
    inktag_om_health_process(s_status_now_ms);
    if (s_startup_swap_confirm_retry_pending &&
        status_deadline_reached(s_startup_swap_confirm_retry_at_ms)) {
        s_startup_swap_confirm_retry_pending = false;
        complete_startup_if_ready();
    }
    if (!s_epd_work_active && !inktag_om_app_epd_control_allowed()) {
        s_boot_restore_pending = s_boot_restore_due = false;
        s_native_refresh_pending = false;
        if (s_slideshow_active) slideshow_stop(false);
        discard_pending_background_epd_work();
        if (s_v2_transfer.active &&
            s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
            uint8_t transaction = s_v2_transfer.transaction_id;
            inktag_status_v2_t access = epd_control_access();
            v2_transfer_reset(true);
            send_v2_event(INKTAG_EVENT_ERROR, access,
                          INKTAG_CMD_TRANSFER_WRITE, transaction, NULL, 0u);
        }
        /* A committed direct frame may own power until its queued refresh. */
        epd_release_owned_panel();
    }
    process_pending_system_action();
    if (!s_lifecycle_content_locked) inktag_om_device_settings_process();
    (void)refresh_findmy_key_if_safe();
    if (s_v2_transfer_abort_pending) {
        s_v2_transfer_abort_pending = false;
        v2_transfer_reset(true);
        return;
    }
    if (s_v2_transfer.active &&
        status_deadline_reached(s_v2_transfer.last_activity_ms + 30000u)) {
        uint8_t expired_transaction = s_v2_transfer.transaction_id;
        v2_transfer_reset(true);
        send_v2_event(INKTAG_EVENT_ERROR, INKTAG_STATUS_TIMEOUT,
                      INKTAG_CMD_TRANSFER_WRITE, expired_transaction, NULL, 0u);
        return;
    }
    if (s_ota_fixed_boot_pending && s_conn_idx == 0xFFu &&
        status_deadline_reached(s_ota_fixed_boot_not_before_ms)) {
        s_ota_fixed_boot_pending = false;
        s_ota_fixed_boot_not_before_ms = 0u;
        inktag_om_swap_enter_fixed_boot();
    }
    if (s_advertising_restart_pending) {
        s_advertising_restart_pending = false;
        if (s_conn_idx == 0xFFu && !s_advertising_active) {
            (void)start_advertising_with_retry();
        }
    }
    poll_findmy_time_multiplex();
    if (s_boot_restore_due && !automatic_display_refresh_allowed()) {
        s_boot_restore_due = false;
    }
    if (s_boot_restore_due && s_advertising_active && s_conn_idx == 0xFFu &&
        s_status.transient_scene == UINT8_MAX &&
        !s_epd_work_pending && !s_epd_work_active &&
        s_epd_work_type == EPD_WORK_NONE &&
        !inktag_image_store_capture_active()) {
        s_boot_restore_due = false;
        schedule_boot_display_restore();
    }

    rx_packet_t rx_packet;
    if (rx_packet_dequeue(&rx_packet)) {
        process_packet(rx_packet.data, rx_packet.length);
        return;
    }

    /* A pending or unknown aggregate clear may still be in progress. Keep
     * basic BLE/OTA rescue alive, but dispatch no display, button or slot work. */
    if (s_lifecycle_content_locked) return;

    process_time_checkpoint();

    if (!s_button_action_pending) {
        inktag_button_gesture_t gesture;
        inktag_input_binding_t binding;
        if (inktag_om_device_settings_take_button(&gesture, &binding) &&
            product_access(INKTAG_PRODUCT_FEATURE_BUTTON_BINDING,
                           INKTAG_OM_AUX_IO_BACKEND_AVAILABLE != 0,
                           INKTAG_BOARD_AUX_IO_QUALIFIED != 0 &&
                               inktag_om_aux_io_supported(),
                           true) ==
                INKTAG_STATUS_OK) {
            char buf[48];
            const char *name = gesture == INKTAG_BUTTON_GESTURE_SINGLE ? "single" :
                               gesture == INKTAG_BUTTON_GESTURE_DOUBLE ? "double" : "long";
            snprintf(buf, sizeof(buf), "button:event gesture=%s action=%u", name, binding.action);
            send_text(buf);
            if (binding.action == INKTAG_INPUT_ACTION_STOP) {
                process_input_action(binding.action, binding.slot_id, "button", true);
            } else {
                s_button_pending_binding = binding;
                s_button_action_pending = true;
            }
        }
    }

    if (s_slideshow_advance_pending && !s_epd_work_pending && !s_epd_work_active &&
        s_epd_work_type == EPD_WORK_NONE &&
        !inktag_image_store_capture_active()) {
        uint8_t next_id = UINT8_MAX;
        /* A missing/read-failed slot consumes this interval, not the request.
         * Commit the current ID only after a successful panel completion. */
        slideshow_arm_next_after_refresh();
        if (slideshow_select_next(&next_id)) {
            (void)schedule_epd_work_background(EPD_WORK_IMAGE_SHOW, next_id);
        }
        return;
    }

    if (s_button_action_pending && !s_epd_work_pending && !s_epd_work_active &&
        s_epd_work_type == EPD_WORK_NONE &&
        !inktag_image_store_capture_active()) {
        inktag_input_binding_t binding = s_button_pending_binding;
        s_button_action_pending = false;
        if (inktag_om_app_custom_input_allowed())
            process_input_action(binding.action, binding.slot_id, "button", true);
        return;
    }

    if (s_image_store_abort_pending) {
        s_image_store_abort_pending = false;
        image_store_abort();
        return;
    }

    if (s_native_refresh_pending &&
        (!automatic_display_refresh_allowed() || s_slideshow_active ||
         s_config.display_mode != s_native_refresh_mode)) {
        s_native_refresh_pending = false;
    }
    if (s_native_refresh_pending && s_epd_work_pending &&
        s_epd_work_background && s_epd_work_type == EPD_WORK_GUI &&
        s_epd_work_arg == s_native_refresh_mode) {
        /* A boot-restore or interrupted redraw already covers this boundary. */
        s_epd_work_timestamp = s_native_refresh_timestamp;
        s_native_refresh_pending = false;
    }
    if (s_native_refresh_pending && !s_epd_work_pending &&
        !s_epd_work_active && s_epd_work_type == EPD_WORK_NONE &&
        !false && !false &&
        !s_v2_transfer.active && !s_picture_io_active &&
        !inktag_image_store_capture_active() && native_refresh_ble_idle()) {
        if (schedule_epd_work_background(EPD_WORK_GUI,
                                         s_native_refresh_mode)) {
            s_epd_work_timestamp = s_native_refresh_timestamp;
            s_native_refresh_pending = false;
        }
    }

    if (!s_epd_work_pending ||
        (s_epd_work_background && s_conn_idx != 0xFFu &&
         ((s_epd_work_type == EPD_WORK_GUI &&
           (!native_refresh_ble_idle() || false ||
            false)) ||
          (s_epd_work_type != EPD_WORK_GUI && !s_slideshow_active)))) return;

    /* NFC authorization is checked again at actual panel execution, after
     * feedback/queuing. Losing Pro cannot execute an earlier admitted command. */
    inktag_status_v2_t panel_access = epd_control_access();
    if (panel_access != INKTAG_STATUS_OK) {
        uint8_t argument = s_epd_work_arg;
        bool client_owned = s_epd_work_client_owned;
        s_epd_work_pending = s_epd_work_background = s_epd_work_client_owned = false;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        epd_release_owned_panel();
        if (client_owned) send_v2_work_complete(panel_access, argument);
        return;
    }

    /* Take the action-entry policy sample while the refresh LED, panel power
     * and external Flash are still idle. A valid low voltage never blocks the
     * display because displayMin=0; unavailable samples remain UNKNOWN. */
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,
                                             true)) {
        uint8_t failed_arg = s_epd_work_arg;
        bool failed_client_owned = s_epd_work_client_owned;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        s_epd_work_background = false;
        s_epd_work_client_owned = false;
        s_epd_work_pending = false;
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_LOW_BATTERY,
                              s_status_now_ms);
        if (failed_client_owned)
            send_v2_work_complete(INKTAG_STATUS_LOW_POWER, failed_arg);
        return;
    }

    if (s_epd_work_background && s_epd_work_type == EPD_WORK_GUI &&
        (s_epd_work_arg == MODE_CALENDAR ||
         s_epd_work_arg == MODE_CLOCK)) {
        s_epd_work_timestamp = s_timestamp;
    }

    epd_work_type_t work_type = s_epd_work_type;
    uint8_t work_arg = s_epd_work_arg;
    uint32_t work_timestamp = s_epd_work_timestamp;
    bool work_background = s_epd_work_background;
    bool work_client_owned = s_epd_work_client_owned;
    s_epd_active_type = work_type;
    s_epd_active_arg = work_arg;
    s_epd_active_timestamp = work_timestamp;
    s_epd_work_active_background = work_background;
    s_epd_work_active_client_owned = work_client_owned;
    s_epd_work_type = EPD_WORK_NONE;
    s_epd_work_arg = 0;
    s_epd_work_timestamp = 0;
    s_epd_work_background = false;
    s_epd_work_client_owned = false;
    s_epd_work_pending = false;
    s_epd_work_active = true;
    s_epd_cancel_requested = false;
    s_epd_low_power_abort = false;
    EPD_SetAbortCallback(epd_abort_requested);
    inktag_status_v2_t work_access = INKTAG_STATUS_OK;
    if (work_type == EPD_WORK_IMAGE_SHOW) {
        inktag_image_store_header_t header;
        if (inktag_image_store_get(work_arg, &header) == INKTAG_IMAGE_STORE_OK &&
            !image_store_record_visible(work_arg)) {
            work_access = INKTAG_STATUS_DENIED;
        }
    }
    if (work_access != INKTAG_STATUS_OK) {
        s_epd_work_active = false;
        s_epd_work_active_background = false;
        s_epd_work_active_client_owned = false;
        s_epd_active_type = EPD_WORK_NONE;
        EPD_SetAbortCallback(NULL);
        if (work_client_owned)
            send_v2_work_complete(work_access, work_arg);
        return;
    }
    /* Freeze the idle/action-entry policy sample before enabling the refresh
     * LED or any EPD load. Subsequent samples belong only to operation safety
     * and sag telemetry, never to the idle policy sample. */
    inktag_om_health_display_begin();
    bool work_success = true;
    if (work_type == EPD_WORK_REFRESH) {
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                                 false, s_status_now_ms);
    }
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                             true, s_status_now_ms);
    /* Arm visible refresh feedback before entering synchronous panel code.
     * BUSY polling owns a separate operation-only wake timer, so an OFF or
     * steady indicator policy can still leave the status timer stopped. */
    status_timer_update();
    inktag_om_health_stage(0x4F4D4450u, INKTAG_OM_HEALTH_OPERATION_DISPLAY); /* OMDP */

    if (!s_epd && !diag_epd_ensure_screen()) {
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                                 false, s_status_now_ms);
        inktag_status_trigger(&s_status, INKTAG_STATUS_SCENE_ERROR,
                              s_status_now_ms);
        s_epd_work_active = false;
        s_epd_work_active_background = false;
        s_epd_work_active_client_owned = false;
        s_epd_active_type = EPD_WORK_NONE;
        EPD_SetAbortCallback(NULL);
        if (work_client_owned) {
            send_v2_work_complete(INKTAG_STATUS_INCOMPATIBLE, work_arg);
        }
        inktag_om_health_display_end();
        return;
    }

    switch (work_type) {
        case EPD_WORK_CLEAR:
            epd_gpio_open_if_needed();
            send_diag("clear:start");
            epd_panel_initialize();
            send_diag("clear:init");
            s_epd->drv->clear(s_epd, work_arg != 0);
            break;
        case EPD_WORK_REFRESH:
            epd_gpio_open_if_needed();
            send_diag("picture:refresh");
            if (work_success) s_epd->drv->refresh(s_epd);
            break;
        case EPD_WORK_GUI:
            work_success = epd_gui_update((display_mode_t)work_arg, work_timestamp);
            break;
        case EPD_WORK_IMAGE_SHOW:
            work_success = image_store_show(work_arg);
            break;
        case EPD_WORK_GHOST_CLEAN: {
            uint8_t cycle;
            uint8_t pass;
            uint8_t completed_passes = 0u;
            uint8_t total_passes = (uint8_t)(work_arg * 3u);
            bool panel_white = false;
            static const bool pass_white[3] = {true, false, true};

            epd_gpio_open_if_needed();
            send_diag("ghost-clean:start");
            epd_panel_initialize();
            for (cycle = 0u; cycle < work_arg && work_success; ++cycle) {
                for (pass = 0u; pass < 3u; ++pass) {
                    if (EPD_AbortRequested()) {
                        work_success = false;
                        break;
                    }
                    s_epd->drv->clear(s_epd, pass_white[pass]);
                    panel_white = pass_white[pass];
                    if (EPD_AbortRequested() || !EPD_OnMicroSpiReady() ||
                        EPD_OnMicroSpiLastError() != OM_ERROR_OK ||
                        !EPD_BusyDiagAsserted() ||
                        s_epd->drv->read_busy(s_epd)) {
                        work_success = false;
                        break;
                    }
                    send_v2_ghost_clean_progress(++completed_passes,
                                                 total_passes);
                }
            }
            /* A failed black pass must not be reported as success. If the
             * caller cancelled while the panel was dark, allow one bounded
             * best-effort white pass before restoring the CANCELLED result.
             * Low-voltage safety aborts are never overridden. */
            if (!panel_white && !s_epd_low_power_abort) {
                bool cancelled = s_epd_cancel_requested;
                s_epd_cancel_requested = false;
                s_epd->drv->clear(s_epd, true);
                s_epd_cancel_requested =
                    cancelled || s_epd_cancel_requested;
            }
            break;
        }
        case EPD_WORK_NONE:
        default:
            break;
    }
    bool interrupted = s_epd_cancel_requested || EPD_AbortRequested();
    bool low_power_abort = s_epd_low_power_abort;
    bool cancelled = interrupted && !low_power_abort;
    bool refresh_expected = work_success && (work_type == EPD_WORK_REFRESH ||
                            work_type == EPD_WORK_GUI ||
                            work_type == EPD_WORK_IMAGE_SHOW ||
                            work_type == EPD_WORK_GHOST_CLEAN ||
                            (work_type == EPD_WORK_CLEAR && work_arg != 0));
    bool spi_failed = !EPD_OnMicroSpiReady() || EPD_OnMicroSpiLastError() != OM_ERROR_OK;
    if (interrupted && !s_epd_cancel_requested && !low_power_abort && !spi_failed) {
        work_access = epd_control_access();
        if (work_access != INKTAG_STATUS_OK) cancelled = false;
    }
    bool missing_busy_edge = refresh_expected && !interrupted && !EPD_BusyDiagAsserted();
    bool stuck_busy = s_epd && !interrupted && s_epd->drv->read_busy(s_epd);
    if (spi_failed) send_diag("epd:spi-error");
    if (missing_busy_edge) send_diag("epd:no-busy-edge");
    bool work_failed = interrupted || spi_failed || missing_busy_edge ||
                       stuck_busy || !work_success ||
                       (s_epd && !EPD_DriverOperationOK(s_epd));
    if (!work_failed && refresh_expected) sample_panel_temperature_after_refresh();
    if (work_failed) {
        bool background_retry_queued =
            s_epd_work_pending && s_epd_work_background &&
            !s_epd_work_client_owned && s_epd_work_type == work_type &&
            s_epd_work_arg == work_arg &&
            s_epd_work_timestamp == work_timestamp;
        /* GATT discovery may cooperatively cancel and retain this exact job.
         * Actual power/SPI failures consume only that retry, never a newer
         * foreground job, and defer slideshow work to its normal interval. */
        bool background_cancel_requeued =
            s_epd_work_active_background && !work_client_owned &&
            cancelled && !spi_failed && background_retry_queued;
        if (s_epd_work_active_background && background_retry_queued &&
            !background_cancel_requeued) {
            s_epd_work_pending = false;
            s_epd_work_background = false;
            s_epd_work_type = EPD_WORK_NONE;
        }
        if (work_type == EPD_WORK_IMAGE_SHOW && !background_cancel_requeued) {
            if (cancelled && work_client_owned) slideshow_stop(true);
            else if (s_slideshow_active) slideshow_arm_next_after_refresh();
        }
        if (work_type == EPD_WORK_REFRESH) s_pending_picture_store_id = UINT8_MAX;
        EPD_Reset(true, 10);
        send_text(low_power_abort ? "epd:low-power" :
                  cancelled ? "epd:stopped" : "epd:timeout");
        if (work_client_owned) {
            send_v2_work_complete(work_access != INKTAG_STATUS_OK ? work_access :
                                  low_power_abort ? INKTAG_STATUS_LOW_POWER :
                                  cancelled ? INKTAG_STATUS_CANCELLED :
                                               INKTAG_STATUS_IO_ERROR,
                                  work_arg);
        }
    } else {
        if (work_type == EPD_WORK_CLEAR) {
            record_display_state(EPD_DISPLAY_STATE_CLEAR, work_arg, 0);
        } else if (work_type == EPD_WORK_REFRESH) {
            record_display_state(EPD_DISPLAY_STATE_PICTURE,
                                 s_pending_picture_store_id, 0);
            s_pending_picture_store_id = UINT8_MAX;
        } else if (work_type == EPD_WORK_GUI) {
            record_display_state(work_arg == MODE_DEVICE_INFO ? EPD_DISPLAY_STATE_DEVICE_INFO : work_arg,
                                 EPD_DISPLAY_STATE_NO_SOURCE, 0);
        } else if (work_type == EPD_WORK_IMAGE_SHOW) {
            record_display_state(s_slideshow_active ? EPD_DISPLAY_STATE_SLIDESHOW :
                                 EPD_DISPLAY_STATE_PICTURE,
                                 work_arg,
                                 s_slideshow_active ? s_slideshow_interval : 0);
            if (s_slideshow_active) {
                s_slideshow_current_id = work_arg;
                slideshow_arm_next_after_refresh();
            }
        } else if (work_type == EPD_WORK_GHOST_CLEAN) {
            record_display_state(EPD_DISPLAY_STATE_CLEAR, 1u, 0u);
        }
        if (work_type == EPD_WORK_CLEAR) send_diag("clear:done");
        else if (work_type == EPD_WORK_REFRESH) send_diag("picture:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_CALENDAR) send_diag("calendar:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_CLOCK) send_diag("clock:done");
        else if (work_type == EPD_WORK_GUI && work_arg == MODE_DEVICE_INFO) send_diag("device-info:done");
        else if (work_type == EPD_WORK_GHOST_CLEAN) send_diag("ghost-clean:done");
        else if (work_type == EPD_WORK_GUI) send_diag("gui:done");
        else if (work_type == EPD_WORK_IMAGE_SHOW) {
            char buf[40];
            snprintf(buf, sizeof(buf), "img:show:done id=%u", work_arg);
            send_text(buf);
        }
        if (s_epd) s_epd->drv->sleep(s_epd);
        delay(100);
        if (work_client_owned && work_type == EPD_WORK_GHOST_CLEAN) {
            const uint8_t completion[2] = {
                INKTAG_DISPLAY_ACTION_GHOST_CLEAN, work_arg,
            };
            send_v2_work_complete_payload(INKTAG_STATUS_OK, completion,
                                          sizeof(completion));
        } else if (work_client_owned) {
            send_v2_work_complete(INKTAG_STATUS_OK, work_arg);
        }
    }
    epd_gpio_close_if_needed();
    EPD_SetAbortCallback(NULL);
    inktag_om_health_display_end();
    status_led_finish_display(work_failed, low_power_abort);
    s_epd_work_active = false;
    s_epd_work_active_background = false;
    s_epd_work_active_client_owned = false;
    s_epd_active_type = EPD_WORK_NONE;
    s_epd_cancel_requested = false;
    s_epd_low_power_abort = false;
    /* v2_cancel defers its ACK until the panel has unwound and powered down.
     * The original display has already received its own terminal event. */
    v2_finish_cancel();
    inktag_om_health_stage(0x4F4D4944u, INKTAG_OM_HEALTH_OPERATION_BLE); /* OMID */
}

#include "inktag_om_protocol_v2.inc"

static void inktag_service_event(inktag_om_evt_t *p_evt) {
    switch (p_evt->evt_type) {
        case INKTAG_OM_EVT_NOTIFY_ENABLED:
            s_conn_idx = p_evt->conn_idx;
            /* Screen state is returned only by V2 GET_SCREEN_PROFILE. */
            s_notify_enabled = true;
            s_tx_retry_ready = true;
            break;
        case INKTAG_OM_EVT_NOTIFY_COMPLETE:
            s_tx_retry_ready = true;
            break;
        case INKTAG_OM_EVT_NOTIFY_DISABLED:
            inktag_device_password_session_clear(&s_password);
            v2_find_owner_clear();
            s_notify_enabled = false;
            tx_retry_reset();
            s_image_store_abort_pending = inktag_image_store_capture_active();
            s_v2_transfer_abort_pending = s_v2_transfer.active;
            rx_packet_queue_reset();
            if (s_epd_work_pending && s_epd_work_client_owned) {
                s_epd_work_pending = false;
                s_epd_work_background = false;
                s_epd_work_client_owned = false;
                s_epd_work_type = EPD_WORK_NONE;
            }
            if (s_epd_work_active && s_epd_work_active_client_owned)
                s_epd_cancel_requested = true;
            s_v2_work_opcode = 0u;
            s_v2_work_transaction = 0u;
            s_v2_cancel_transaction = 0u;
            break;
        case INKTAG_OM_EVT_RX_DATA:
            if(!release_packet_admitted(p_evt->p_data,p_evt->length)) {
                if(p_evt->p_data && p_evt->length>=INKTAG_PROTOCOL_V2_HEADER_SIZE)
                    send_v2_status(p_evt->p_data[0],p_evt->p_data[1],INKTAG_STATUS_INVALID_REQUEST);
                break;
            }
            bool command_allowed = false;
            if (p_evt->p_data[0] != INKTAG_CMD_TRANSFER_WRITE &&
                p_evt->p_data[0] != INKTAG_CMD_TRANSFER_COMMIT)
                command_allowed = inktag_device_password_command_allowed(
                    &s_password,&s_password_config,p_evt->p_data[0]);
            if (v2_base_unsupported_request(p_evt->p_data, p_evt->length)) {
                inktag_status_v2_t status = command_allowed ?
                    INKTAG_STATUS_UNSUPPORTED : s_password_config.valid ?
                        INKTAG_STATUS_DENIED : INKTAG_STATUS_PERSISTENCE_ERROR;
                send_v2_status(p_evt->p_data[0],p_evt->p_data[1],status);
                break;
            }
            if (p_evt->p_data[0] != INKTAG_CMD_TRANSFER_WRITE &&
                p_evt->p_data[0] != INKTAG_CMD_TRANSFER_COMMIT &&
                command_allowed) {
                inktag_status_v2_t access = v2_panel_request_access(p_evt->p_data[0],
                    &p_evt->p_data[INKTAG_PROTOCOL_V2_HEADER_SIZE],
                    p_evt->length - INKTAG_PROTOCOL_V2_HEADER_SIZE);
                if (access != INKTAG_STATUS_OK) {
                    send_v2_status(p_evt->p_data[0], p_evt->p_data[1], access);
                    break;
                }
            }
            s_ble_command_idle_after_ms = s_status_now_ms + 500u;
            if (p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                ((p_evt->p_data[0] == INKTAG_CMD_CANCEL &&
                  (s_epd_work_active || inktag_image_store_capture_active())) ||
                 (s_epd_work_active &&
                  (p_evt->p_data[0] == INKTAG_CMD_GET_HEALTH ||
                   p_evt->p_data[0] == INKTAG_CMD_GET_TIME)))) {
                /* The NFC dispatcher permits RAM readouts/control and returns
                 * BUSY for mutations and tag-bus diagnostics. Reject them now
                 * rather than replaying a queued write after RF cancellation.
                 * Ordinary authentication and validation still run first. */
                runtime_clock_update();
                process_packet(p_evt->p_data, p_evt->length);
            } else if (p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                       v2_command_requires_idle_display(p_evt->p_data[0]) &&
                       (s_epd_work_active ||
                        (s_epd_work_pending && !s_epd_work_background))) {
                /* User-visible display commands are never queued behind a
                 * slow panel refresh; the client can explicitly cancel and
                 * retry if it wants to replace the active operation. */
                send_v2_status(p_evt->p_data[0], p_evt->p_data[1], INKTAG_STATUS_BUSY);
            } else if (!rx_packet_enqueue(p_evt->p_data, p_evt->length)) {
                if (p_evt->length >= INKTAG_PROTOCOL_V2_HEADER_SIZE) {
                    send_v2_status(p_evt->p_data[0], p_evt->p_data[1], INKTAG_STATUS_BUSY);
                }
            }
            break;
        default:
            break;
    }
}

static void wall_clock_update(void) {
    uint32_t previous_timestamp;
    uint32_t elapsed_seconds;

    previous_timestamp = s_timestamp;
    elapsed_seconds = inktag_elapsed_clock_sample_shared(&s_elapsed_clock,
                                                   drv_pmu_timer_cnt_get());
    if (elapsed_seconds == 0u) return;
    s_timestamp += elapsed_seconds;
    if (s_slideshow_active &&
        (int32_t)(s_timestamp - s_slideshow_next_at) >= 0 &&
        !s_slideshow_advance_pending) {
        /* Timer callbacks never touch external Flash. Selection and replay are
         * deferred to inktag_om_app_process(), alongside every other SFS job. */
        s_slideshow_advance_pending = true;
    }
    bool refresh_due =
        (s_config.display_mode == MODE_CALENDAR &&
         inktag_elapsed_clock_crossed_period(previous_timestamp, s_timestamp, 86400u)) ||
        (s_config.display_mode == MODE_CLOCK &&
         inktag_elapsed_clock_crossed_period(previous_timestamp, s_timestamp, 60u));
    if (automatic_display_refresh_allowed() && !s_slideshow_active &&
        refresh_due) {
        /* Keep the latest due boundary even while BLE or the panel is busy. */
        s_native_refresh_pending = true;
        s_native_refresh_mode = s_config.display_mode;
        s_native_refresh_timestamp = s_timestamp;
    }
}

static void clock_timer_handler(evt_timer_t *timer, void *param) {
    (void)timer;
    (void)param;
    wall_clock_update();
}

static void ota_recovery_timer_handler(evt_timer_t *timer, void *param) {
    (void)timer;
    (void)param;
    s_ota_fixed_boot_pending = true;
    s_ota_fixed_boot_not_before_ms =
        s_status_now_ms + OTA_RECOVERY_RADIO_SETTLE_MS;
    s_advertising_restart_pending = false;
    if (s_advertising_retry_timer_armed) {
        evt_timer_del(&s_advertising_retry_timer);
        s_advertising_retry_timer_armed = false;
    }
    (void)ob_gap_adv_stop(0u);
}

static uint32_t start_advertising(void) {
    memset(&s_adv_param, 0, sizeof(s_adv_param));
    s_adv_param.own_addr_type = s_local_addr_type;
    s_adv_param.prim_phy = OB_ADV_PHY_1M;
    s_adv_param.secd_phy = OB_ADV_PHY_1M;
    s_adv_param.tx_pwr = 0;
    s_adv_param.filter_policy = OB_ADV_FILTER_NONE;
    s_adv_param.prim_ch_map = OB_ADV_CH_ALL;
    s_adv_param.prim_intv_min = INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS;
    s_adv_param.prim_intv_max = s_adv_param.prim_intv_min;
    s_adv_param.local_addr = s_local_addr;
    s_adv_param.peer_addr = &s_peer_addr;
    s_adv_param.adv_properties = OB_ADV_PROP_LEGACY_IND;
    return ob_gap_adv_start(0, &s_adv_param, &s_adv, &s_scan_rsp);
}
static void advertising_retry_timer_handler(evt_timer_t *timer, void *param) {
    (void)timer;
    (void)param;
    s_advertising_retry_timer_armed = false;
    if (s_conn_idx == 0xFFu && !s_advertising_active) {
        s_advertising_restart_pending = true;

    }
}

static void schedule_advertising_retry(void) {
    if (s_conn_idx != 0xFFu || s_advertising_active ||
        s_advertising_retry_timer_armed) {
        return;
    }
    if (evt_timer_set(&s_advertising_retry_timer, ADVERTISING_RETRY_MS,
                      EVT_TIMER_ONE_SHOT, advertising_retry_timer_handler,
                      NULL)) {
        s_advertising_retry_timer_armed = true;
    } else {
        s_advertising_restart_pending = true;
    }
}

static uint32_t start_advertising_with_retry(void) {
    if (s_conn_idx != 0xFFu) return OB_ERROR_NO_ERR;
    uint32_t status = start_advertising();
    s_advertising_last_status = status;
    if (s_advertising_retry_count != UINT8_MAX) s_advertising_retry_count++;
    if (status != OB_ERROR_NO_ERR) {
        s_advertising_active = false;
        schedule_advertising_retry();
    }
    return status;
}

static void poll_findmy_time_multiplex(void) {}
static void findmy_mux_configuration_changed(void) {}
static void app_gap_event_cb(uint16_t evt_id, const omble_evt_t *evt) {
    if (evt_id == OB_GAP_EVT_CONNECTED) {
        inktag_device_password_session_clear(&s_password);
        inktag_soak_telemetry_connection_started();
        if (s_advertising_retry_timer_armed) {
            evt_timer_del(&s_advertising_retry_timer);
            s_advertising_retry_timer_armed = false;
        }
        s_advertising_active = false;
        s_advertising_restart_pending = false;
        s_advertising_retry_count = 0;
        s_conn_idx = evt->gap.conn_idx;
        s_ble_discovery_until_ms = s_status_now_ms + 5000u;
        s_ble_command_idle_after_ms = s_status_now_ms;
        if (s_epd_work_active && s_epd_work_active_background &&
            !s_epd_cancel_requested && !s_epd_low_power_abort) {
            /* Let GATT discovery own the scheduler. The interrupted automatic
             * job is retained and resumes once this connection is idle. */
            s_epd_cancel_requested = true;
            if (!s_epd_work_pending) {
                s_epd_work_type = s_epd_active_type;
                s_epd_work_arg = s_epd_active_arg;
                s_epd_work_timestamp = s_epd_active_timestamp;
                s_epd_work_background = true;
                s_epd_work_client_owned = false;
                s_epd_work_pending = true;
            }
        }
        status_led_set_connected(true);
    } else if (evt_id == OB_GAP_EVT_DISCONNECTED) {
        inktag_device_password_session_clear(&s_password);
        v2_find_owner_clear();
        rx_packet_queue_reset();
        tx_retry_reset();
        s_image_store_abort_pending = inktag_image_store_capture_active();
        s_v2_transfer_abort_pending = s_v2_transfer.active;
        if (s_epd_work_pending && s_epd_work_client_owned) {
            s_epd_work_pending = false;
            s_epd_work_background = false;
            s_epd_work_client_owned = false;
            s_epd_work_type = EPD_WORK_NONE;
        }
        if (s_epd_work_active && s_epd_work_active_client_owned)
            s_epd_cancel_requested = true;
        s_notify_enabled = false;
        s_conn_idx = 0xFF;
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                                 false, s_status_now_ms);
        inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_REFRESH,
                                 false, s_status_now_ms);
        if (!s_epd_work_active) epd_gpio_close_if_needed();
        status_led_set_connected(false);
        /* The controller can report STARTED for a new set while it is still
         * retiring the set consumed by the connection. That leaves a phantom
         * active state with no packets on air. Explicitly close the old set,
         * let the controller settle, then start it from the main loop. */
        if (s_advertising_retry_timer_armed) {
            evt_timer_del(&s_advertising_retry_timer);
            s_advertising_retry_timer_armed = false;
        }
        s_advertising_active = false;
        s_advertising_restart_pending = false;
        (void)ob_gap_adv_stop(0u);
        if (s_ota_fixed_boot_pending) {
            /* A CPU-only reboot while the controller is retiring this
             * connection leaves Recovery unable to advertise.  Keep the app
             * radio idle, then reboot only after the disconnect has settled. */
            s_ota_fixed_boot_not_before_ms =
                s_status_now_ms + OTA_RECOVERY_RADIO_SETTLE_MS;
        } else {
            schedule_advertising_retry();
        }
    } else if (evt_id == OB_GAP_EVT_ADV_STATE_CHANGED) {
        if (evt->gap.adv_state_changed.adv_idx != 0u) return;
        if (evt->gap.adv_state_changed.state == OB_GAP_ADV_ST_STARTED) {
            s_advertising_active = true;
            s_advertising_restart_pending = false;
            s_advertising_retry_count = 0;
            s_advertising_last_status = OB_ERROR_NO_ERR;
            if (s_advertising_retry_timer_armed) {
                evt_timer_del(&s_advertising_retry_timer);
                s_advertising_retry_timer_armed = false;
            }
            if (s_boot_restore_pending) {
                s_boot_restore_pending = false;
                s_boot_restore_due = true;
            }
        } else {
            s_advertising_active = false;
            if (s_conn_idx == 0xFFu) schedule_advertising_retry();

        }
    } else if (evt_id == OB_GAP_EVT_CONN_PARAMS_REQUEST) {
        ob_gap_conn_params_t param = {0x18, 0, 500};
        ob_gap_conn_param_update(evt->gap.conn_idx, &param);
    }
}

void inktag_om_app_start(void) {
    struct ob_stack_param param = {
        .max_connection = 1,
        .max_ext_adv_set = 1,
        .max_att_mtu = OB_LE_HOST_ATT_MTU,
        .max_gatt_serv_num = 4,
        .max_gatt_write_cache = OB_LE_HOST_ATT_WRITE_CACHE_SIZE,
        .smp_sc_support = 0,
    };

    drv_pmu_pin_io13_reset_mode_enable(1);
    /* The factory application closes SWD after leaving ROM ISP.  InkTag keeps
     * it explicitly enabled so later probe/flash sessions can attach during a
     * normal boot without holding GPIO4/BOOT low. */
    drv_pmu_jtag_enable(true);
    drv_wdt_init(0);
    board_init();
    inktag_om_ble_reset_for_role_start();
    drv_rf_init();
    evt_init();
    evt_timer_init();
    pm_sleep_notify_user_callback_register(power_sleep_notify);
    inktag_elapsed_clock_reset(&s_elapsed_clock,
                               drv_pmu_timer_cnt_get(),
                               s_timebase_profile.counter_mask,
                               s_timebase_profile.counter_ticks_per_second);
    inktag_elapsed_clock_reset(&s_runtime_clock,
                               drv_pmu_timer_cnt_get(),
                               s_timebase_profile.counter_mask,
                               s_timebase_profile.counter_ticks_per_second);
    inktag_soak_telemetry_init(
        INKTAG_SOAK_FLAG_FIND_WINDOWS |
        INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
        INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
        INKTAG_SOAK_FLAG_SLEEP_ENTRIES,
        s_status_now_ms);
#if defined(INKTAG_OM_DIAGNOSTICS)
    shell_init(NULL);
#endif
    init_device_identity(nvds_init(INKTAG_OM_NVDS_ADDRESS));
    /* Initialize ADC-backed policy before any project-owned NVDS default,
     * identity or migration write can occur. */
    inktag_om_health_init((uint8_t)drv_pmu_reboot_reason(),
                          0x4F4D4230u); /* OMB0 */
    inktag_om_board_identity_init();
    inktag_om_pins_reload();
    if (inktag_om_board_identity() == INKTAG_OM_BOARD_ZKC21B) {
        /* Apply the exact stock board's GPIO8-11 low-output state after
         * shared startup pull-up defaults, without an EVB LED/mux table. */
        drv_pmu_pin_mode_set(INKTAG_BOARD_ZKC21B_STOCK_LOW_PAD_MASK,
                             PMU_PIN_MODE_PP);
        drv_gpio_write(OM_GPIO0, INKTAG_BOARD_ZKC21B_STOCK_LOW_PAD_MASK,
                       GPIO_LEVEL_LOW);
        drv_gpio_set_dir(OM_GPIO0, INKTAG_BOARD_ZKC21B_STOCK_LOW_PAD_MASK,
                         GPIO_DIR_OUTPUT);
    }
    const uint8_t panel_idle_en_pin = inktag_om_panel_idle_en_pin();
    if (panel_idle_en_pin != UINT8_MAX) {
        /* Recovery may hand off with a retained rail. Use the current EN
         * electrical polarity; qualified existing boards remain inactive low. */
        const uint32_t panel_en_mask = 1u << panel_idle_en_pin;
        drv_pmu_pin_mode_set(panel_en_mask, PMU_PIN_MODE_PP);
        drv_gpio_write(OM_GPIO0,panel_en_mask,
            inktag_om_pins_active_low(INKTAG_PIN_ENABLE)?GPIO_LEVEL_HIGH:GPIO_LEVEL_LOW);
        drv_gpio_set_dir(OM_GPIO0, panel_en_mask, GPIO_DIR_OUTPUT);
    }
    inktag_om_external_flash_init();
    uint32_t ble_status = omble_init(&param);
    s_startup_ble_status = ble_status;

    load_default_config_if_needed();
    runtime_clock_update();
    inktag_time_checkpoint_init(&s_time_checkpoint, s_status_now_ms, s_time_valid);
    (void)inktag_image_store_init();
    inktag_om_device_settings_init(
        &s_config,
        (uint8_t)(s_display_state.reserved &
                  (EPD_DISPLAY_STATE_USER_FLAGS_MASK |
                   EPD_DISPLAY_STATE_PRO_FLAGS_MASK)));
    {
        inktag_display_settings_t display_settings;
        inktag_om_device_settings_get_display(&display_settings);
        apply_display_settings_runtime(&display_settings);
    }
    if (!epd_device_settings_read(&s_device_settings)) {
        epd_device_settings_default(&s_device_settings);
    }
    (void)apply_runtime_device_name();
    (void)inktag_om_activation_init(&s_activation);
    /* Keep the ordinary 30-second debug grace; base performs no NFC UID access. */
    inktag_om_swd_attach_window_start();
    inktag_device_password_init(&s_password, s_status_now_ms);
    (void)inktag_om_password_load(&s_password_config);
    (void)om_lifecycle_resume();
    status_led_init();

    inktag_om_service_init_t svc = {.evt_handler = inktag_service_event};
    uint32_t service_status = inktag_om_service_init(&svc);
    s_startup_service_status = service_status;
    uint32_t callback_status = ob_event_callback_reg(app_gap_event_cb);
    s_startup_callback_status = callback_status;

    evt_timer_set(&s_clock_timer, CLOCK_TIMER_INTERVAL_MS, EVT_TIMER_REPEAT, clock_timer_handler, NULL);
    /* Keep the monotonic PMU clock alive even if a future idle interval has
     * no armed event timer.  Normal radio/timer wakeups use SDK sleep rather
     * than the product's separate explicit lifecycle deep-sleep action. */
    drv_pmu_32k_enable_in_deep_sleep(true);
    pm_sleep_allow(PM_ID_SHELL);
    pm_sleep_enable(true);
    uint32_t advertising_status = start_advertising_with_retry();
    s_startup_advertising_status = advertising_status;
    /* A single-slot trial becomes permanent only after the BLE stack, InkTag
     * service and advertising path all initialized successfully. */
    complete_startup_if_ready();
    drv_wdt_init(APPLICATION_WATCHDOG_TIMEOUT_MS);
    /* SDK enable clears WDT_INT_CPU_EN; arm the existing capture callback last. */
    drv_wdt_register_isr_callback(inktag_om_health_watchdog_alarm);

#if defined(INKTAG_OM_DIAGNOSTICS)
    OM_LOG_DEBUG("InkTag OM6626 started\n");
#endif
}

__RAM_CODES("APP")
int main(void) {
    inktag_om_app_start();
    __enable_irq();
    while (1) {
        evt_schedule();
        inktag_om_app_process();
        status_timer_update();

        /* Match the SDK's lost-wakeup-safe idle sequence: an interrupt that
         * becomes pending after the queue check wakes WFI, then runs when the
         * critical section restores interrupts. */
        OM_CRITICAL_BEGIN();
        if (evt_get_all() == 0u) {
            pm_power_manage();
        }
        OM_CRITICAL_END();
    }
}
