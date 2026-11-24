#include "main.h"

#include <string.h>
#include "stack/ble/ble.h"
#include "inktag_version.h"
#include "inktag_protocol_v2.h"
#include "inktag_release_v1.h"
#include "inktag_epd_driver.h"
#include "inktag_activation.h"
#include "inktag_code_guard.h"
#include "inktag_access.h"
#include "inktag_feature_store.h"
#include "inktag_findmy.h"
#include "inktag_ble_identity.h"
#include "inktag_device_display_name.h"
#include "inktag_page_cycle.h"
#if INKTAG_BUILD_PRO
#include "inktag_input_simulation.h"
#endif
#include "inktag_lifecycle.h"
#include "inktag_protocol_core.h"
#include "inktag_image_upload_v5.h"
#include "inktag_packbits.h"
#if INKTAG_TRANSFER_LZO1X_ENABLED
#include "inktag_lzo1x.h"
#endif
#include "inktag_soak_telemetry.h"
#include "inktag_temperature_wire.h"
#include "inktag_elapsed_clock.h"
#include "inktag_tlsr_activation.h"
#include "inktag_tlsr_clock.h"
#include "inktag_clock_rate.h"
#include "inktag_time_checkpoint.h"
#include "inktag_tlsr_features.h"
#include "inktag_tlsr_health.h"
#include "inktag_tlsr_identity.h"
#include "inktag_tlsr_p224_uecc.h"
#include "inktag_tlsr_gui_assets.h"

#define RX_QUEUE_SLOTS 8u
#define MAX_PACKET 244u
typedef struct { uint16_t length; uint8_t data[MAX_PACKET]; } rx_packet_t;

static RAM rx_packet_t s_queue[RX_QUEUE_SLOTS];
static RAM uint8_t s_head, s_tail;
static RAM bool s_notify;
#if INKTAG_BUILD_PRO
#include "inktag_app_advertising_state_pro.inc"
#else
static RAM bool s_picture_active;
#endif
static RAM bool s_slideshow;
static RAM uint8_t s_slideshow_id = 0xFFu;
static RAM uint32_t s_next_slide;
static RAM inktag_elapsed_clock_t s_elapsed_clock;
/* Same shared sampler, separate phase: SET_TIME must never reset raw ages. */
static RAM inktag_elapsed_clock_t s_utc_elapsed_clock;
static RAM inktag_clock_rate_t s_utc_rate;
static RAM uint16_t s_utc_remainder_ms;
/* Shared monotonic deadlines; historical name retained for stable callers. */
static RAM uint32_t s_nfc_now_ms;
static RAM inktag_time_checkpoint_t s_time_checkpoint;
static RAM bool s_boot_restore_pending;
static RAM bool s_boot_restore_due;
static RAM bool s_boot_restore_wait_startup;
static RAM bool s_startup_role_ready;
static RAM bool s_startup_sequence_complete;
static RAM inktag_boot_display_target_t s_boot_restore_target;
static RAM bool s_native_refresh_pending;
static RAM bool s_native_refresh_paused;
#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_state_pro.inc"
#else
#define s_nfc_action_pending false
#endif
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_0_0_pro.inc"
#endif
static RAM bool s_input_clear_return_valid;
static RAM uint8_t s_input_clear_return_mode;
static RAM uint8_t s_input_clear_return_argument;
static RAM uint32_t s_input_clear_return_interval;
static RAM bool s_recovery_confirmation_due;
static RAM uint32_t s_recovery_confirmation_retry_at;
#if !INKTAG_BUILD_PRO
static RAM bool s_system_reboot_pending;
#endif
static RAM bool s_system_reboot_reply_drained;
static RAM uint32_t s_system_reboot_at;
static RAM int16_t s_timezone_minutes;
static RAM bool s_connected;
/* 0 idle, 1 queued by connection, 2 foreground sampling; no periodic task. */
static RAM uint8_t s_connection_temperature_state;
#if !INKTAG_BUILD_PRO
static RAM bool s_display_active;
static RAM bool s_native_page_active;
#endif
static RAM uint8_t s_display_opcode;
static RAM uint8_t s_display_transaction;
static RAM uint8_t s_cancel_transaction;
static RAM inktag_activation_t s_activation;
static RAM inktag_device_password_t s_password;
static RAM inktag_device_password_config_t s_password_config;
#if INKTAG_BUILD_PRO
#include "inktag_app_find_state_pro.inc"
#endif
/* Trust in explicitly supplied wall time also gates ordinary checkpoints. */
static RAM bool s_findmy_time_valid;
static RAM uint32_t s_debug_grace_until;

static bool protocol_transfer_active(void);
static inktag_status_v2_t v2_epd_status(void);
static void update_timestamp_from_clock(void);

#define STATUS_NO_SCENE 0xFFu
#define STATUS_LED_TEST_MS 10000u
#define INKTAG_DEBUG_GRACE_MS 30000u

static RAM inktag_status_runtime_t s_status;
static RAM uint32_t s_status_test_until;
static RAM uint8_t s_status_test_output;
static RAM uint8_t s_status_applied_output;
static RAM bool s_status_test_active;
/* Only the current synchronous GUI operation; never a global LED policy. */
static RAM bool s_gui_led_deferred;

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

static bool activation_read_uid(void *context, uint8_t *output,
                                uint16_t output_size,
                                uint16_t *output_length) {
    uint8_t uid[16];
    (void)context;
    if (!output || !output_length || output_size < sizeof(uid)) return false;
    if (!inktag_tlsr_identity_read_uid(uid)) return false;
    memcpy(output, uid, sizeof(uid));
    memset(uid, 0, sizeof(uid));
    *output_length = sizeof(uid);
    return true;
}

#if INKTAG_BUILD_PRO
#include "inktag_app_activation_pro.inc"
#else
static void activation_init(void) {
    const inktag_tlsr_activation_config_t config = {
        .read_uid = activation_read_uid,
        .platform_id = INKTAG_ACTIVATION_PLATFORM_TLSR8359,
        .product_id = INKTAG_ACTIVATION_PRODUCT_STANDARD,
    };
    (void)inktag_tlsr_activation_init(&s_activation, &config);
}
#endif

static void send_v2_response(uint8_t opcode, uint8_t transaction_id,
                             inktag_status_v2_t status,
                             const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[MAX_PACKET];
    if (payload_length > MAX_PACKET - INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload = NULL;
        payload_length = 0u;
    }
    packet[0] = opcode;
    packet[1] = transaction_id;
    packet[2] = (uint8_t)status;
    if (payload && payload_length) memcpy(&packet[3], payload, payload_length);
    if (s_notify)
        (void)inktag_ble_notify(packet, (uint16_t)(3u + payload_length),
                                INKTAG_BLE_NOTIFY_RELIABLE);
}

/* The common empty response is exactly the same three-byte header. Keep one
 * bounded encoder instead of materializing two constant-zero arguments at
 * hundreds of rejection/ACK sites. No queue, status or reliability changes. */
static void __attribute__((noinline,noclone)) send_v2_status_response(
    uint8_t opcode,uint8_t transaction_id,inktag_status_v2_t status) {
    uint8_t packet[3]={opcode,transaction_id,(uint8_t)status};
    if (s_notify) (void)inktag_ble_notify(packet,sizeof(packet),INKTAG_BLE_NOTIFY_RELIABLE);
}

static void __attribute__((noinline,noclone)) send_v2_ok_response(
    uint8_t opcode,uint8_t transaction_id,const uint8_t *payload,uint16_t length) {
    (send_v2_response)(opcode,transaction_id,INKTAG_STATUS_OK,payload,length);
}

/* Only compile-time constants choose a shorter argument list. All other
 * arguments retain their evaluation count and normal bounded-payload path. */
#define send_v2_response(op,tx,status,payload,length) \
    ((__builtin_constant_p(payload) && __builtin_constant_p(length) && \
      (payload)==NULL && (length)==0u) ? \
     send_v2_status_response((op),(tx),(status)) : \
     (__builtin_constant_p(status) && (status)==INKTAG_STATUS_OK) ? \
     send_v2_ok_response((op),(tx),(payload),(length)) : \
     (send_v2_response)((op),(tx),(status),(payload),(length)))

static bool send_v2_event(uint8_t event, inktag_status_v2_t status,
                          uint8_t origin_opcode, uint8_t origin_transaction,
                          const uint8_t *payload, uint16_t payload_length) {
    uint8_t packet[MAX_PACKET];
    if (payload_length > MAX_PACKET - 5u) payload_length = MAX_PACKET - 5u;
    packet[0] = event;
    packet[1] = 0u;
    packet[2] = (uint8_t)status;
    packet[3] = origin_opcode;
    packet[4] = origin_transaction;
    if (payload && payload_length) memcpy(&packet[5], payload, payload_length);
    if (s_notify)
        return inktag_ble_notify(
            packet, (uint16_t)(5u + payload_length),
            event == INKTAG_EVENT_LOG ? INKTAG_BLE_NOTIFY_BEST_EFFORT :
                                       INKTAG_BLE_NOTIFY_RELIABLE);
    return false;
}

static void send_text(const char *text) {
    if (text) send_v2_event(INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                            (const uint8_t *)text, (uint16_t)strlen(text));
}

#if defined(INKTAG_TLSR_CLOCK_SSD1683_PILOT) && INKTAG_TLSR_CLOCK_SSD1683_PILOT
void inktag_app_clock_pilot_text(const char *text) {send_text(text);}
#endif

static void send_format(const char *format, uint32_t a, uint32_t b, uint32_t c,
                        uint32_t d, uint32_t e, uint32_t f) {
    char buffer[224];
    GFX_format(buffer, sizeof(buffer), format, a, b, c, d, e, f);
    send_text(buffer);
}

static void send_diag(const char *tag) {
    const epd_screen_config_t *screen = inktag_epd_screen();
    const inktag_tlsr_gpio_profile_t *profile = inktag_tlsr_gpio_profile();
    uint8_t buffer[224];
    GFX_format((char *)buffer, sizeof(buffer),
        "%s drv=%02x active=%02x raw=%u busy=%u v=%u result=%u failed_cmd=%02x pins=%02x,%02x,%02x,%02x,%02x,%02x,%02x en=%02x led=%02x",
        tag,
        screen != NULL ? screen->driver_id : 0xFFu,
        inktag_epd_active_driver_id(),
        inktag_epd_busy_raw() ? 1u : 0u,
        inktag_epd_busy() ? 1u : 0u,
        inktag_epd_voltage(),
        (uint32_t)inktag_epd_last_result(), inktag_epd_failed_command(),
        profile ? profile->epd_mosi : 0xFFu,
        profile ? profile->epd_clk : 0xFFu,
        profile ? profile->epd_cs : 0xFFu,
        profile ? profile->epd_dc : 0xFFu,
        profile ? profile->epd_reset : 0xFFu,
        profile ? profile->epd_busy : 0xFFu, 0xFFu,
        profile ? profile->epd_enable : 0xFFu,
        profile ? profile->led_red : 0xFFu);
    send_text((const char *)buffer);
}

static bool status_before(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) < 0;
}

static void status_output(void *context, uint8_t output) {
    (void)context;
    if (s_status_test_active) output = s_status_test_output;
    if (output != s_status_applied_output) {
        s_status_applied_output = output;
        inktag_led_apply(NULL, output);
    }
}

static void status_effective_config(inktag_status_config_t *config) {
    const inktag_status_config_t *saved = &inktag_settings()->led;
    if (inktag_status_config_valid(saved)) {
        *config = *saved;
    } else {
        inktag_status_config_default(config);
    }
}

static void status_active(inktag_status_scene_t scene, bool active) {
    inktag_status_set_active(&s_status, scene, active, s_nfc_now_ms);
}

static void status_trigger(inktag_status_scene_t scene) {
    /* A BLE connection event invokes this from inside blt_sdk_main_loop().
     * Waiting until main_loop() returns to update the suspend mask is too
     * late: the SDK may already enter deep retention and gpio_init(false)
     * parks the just-lit channel. Close that first-interval race at the scene
     * boundary; main_loop() keeps suspend disabled only for the bounded
    * transient and restores ordinary retention afterwards. */
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    /* BLE initialization and controller callbacks run before the ordinary
     * application service turn. Synchronize the retained product clock before
     * creating a relative deadline; otherwise the following service turn can
     * account for several earlier seconds at once and expire a brand-new
     * startup/connection scene immediately. */
    update_timestamp_from_clock();
    inktag_status_trigger(&s_status, scene, s_nfc_now_ms);
}

static void status_reconfigure(void) {
    inktag_status_config_t config;
    const inktag_tlsr_gpio_profile_t *pins=inktag_tlsr_gpio_profile();
    inktag_status_set_available_colors(&s_status,pins?pins->indicator_colors:0u,s_nfc_now_ms);
    status_effective_config(&config);
    inktag_status_set_config(&s_status, &config, s_nfc_now_ms);
}

static void status_test_led(uint8_t color_mask) {
    color_mask &= INKTAG_STATUS_COLOR_WHITE;
    if (color_mask == 0u) {
        s_status_test_active = false;
    } else {
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        update_timestamp_from_clock();
        s_status_test_active = true;
        s_status_test_output = color_mask;
        s_status_test_until = s_nfc_now_ms + STATUS_LED_TEST_MS;
    }
    /* Force the diagnostic value (or restored scene) to the pins even when
     * the status renderer's cached output happens to have the same mask. */
    s_status.last_output = 0xFFu;
    inktag_status_tick(&s_status, s_nfc_now_ms);
}

static uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t screen_payload_size(void) {
    epd_screen_t resolved;
    return epd_screen_resolve(inktag_epd_screen(), &resolved) == EPD_SCREEN_STATUS_OK ?
           resolved.frame_bytes : 0u;
}

static bool screen_matches(const uint8_t *encoded) {
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return encoded && epd_screen_config_encode(inktag_epd_screen(), current) == EPD_SCREEN_STATUS_OK &&
           epd_screen_wire_image_compatible(encoded, current);
}

/* Wire IDs are fixed physical frame positions. Occupied ordinal is only a
 * traversal rank, not a persistent index or an edition quota. */
static bool image_store_record_ordinal(uint8_t wanted_id,
                                       uint16_t *wanted_ordinal) {
    return inktag_image_store_record_ordinal(wanted_id, wanted_ordinal);
}

static bool image_store_ordinal_visible(uint16_t ordinal) {
    const epd_screen_config_t *screen = inktag_epd_screen();
    return inktag_access_image_record_visible(
        &s_activation, ordinal, screen->visible_width,
        screen->visible_height);
}

static bool image_store_record_visible(uint8_t wanted_id) {
    uint16_t ordinal;
    return image_store_record_ordinal(wanted_id, &ordinal) &&
           image_store_ordinal_visible(ordinal);
}

static inktag_image_store_result_t image_store_next_visible(
    uint8_t after_id, uint8_t *id, inktag_image_store_header_t *header) {
    uint16_t ordinal = 0u;
    uint16_t logical_limit = inktag_image_store_fixed_slot_count();
    for (uint16_t logical_slot = 0u; logical_slot < logical_limit; ++logical_slot) {
        inktag_image_store_header_t current;
        inktag_image_store_result_t result =
            inktag_image_store_get((uint8_t)logical_slot, &current);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) return result;
        bool visible = image_store_ordinal_visible(ordinal++);
        if (visible && (after_id == UINT8_MAX || logical_slot > after_id)) {
            if (id) *id = (uint8_t)logical_slot;
            if (header) *header = current;
            return INKTAG_IMAGE_STORE_OK;
        }
    }
    /* Preserve unavailable/startup IO errors even when capacity is zero. */
    uint8_t bitmap[32];
    inktag_image_store_result_t result = inktag_image_store_occupancy(bitmap, sizeof(bitmap));
    return result == INKTAG_IMAGE_STORE_OK ? INKTAG_IMAGE_STORE_NOT_FOUND : result;
}

static bool image_store_next_visible_compatible_linear(uint8_t after_id,
                                                        bool slideshow_only,
                                                        uint8_t *id) {
    uint8_t cursor = after_id;
    for (;;) {
        uint8_t candidate;
        inktag_image_store_header_t header;
        if (image_store_next_visible(cursor, &candidate, &header) !=
            INKTAG_IMAGE_STORE_OK) return false;
        if (screen_matches(header.screen_config) &&
            (!slideshow_only || header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)) {
            if (id) *id = candidate;
            return true;
        }
        cursor = candidate;
    }
}

static bool image_store_next_visible_compatible(uint8_t after_id,
                                                 bool slideshow_only,
                                                 uint8_t *id) {
    return image_store_next_visible_compatible_linear(after_id, slideshow_only, id) ||
        (after_id != UINT8_MAX &&
         image_store_next_visible_compatible_linear(UINT8_MAX, slideshow_only, id));
}

static bool stored_image_replayable(uint8_t id, bool slideshow_only) {
    inktag_image_store_header_t header;
    return id != UINT8_MAX &&
           inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK &&
           image_store_record_visible(id) &&
           screen_matches(header.screen_config) &&
           (!slideshow_only ||
            header.content_type == INKTAG_IMAGE_CONTENT_IMAGE);
}

static inktag_image_store_result_t image_store_visible_status(
    const epd_screen_config_t *screen, uint32_t payload_size,
    inktag_image_store_status_t *status) {
    return inktag_image_store_status(screen, payload_size, status);
}


static uint16_t image_store_allocatable(const inktag_image_store_status_t *status) {
    return status->required_sectors ?
        status->free_sectors / status->required_sectors : 0u;
}


static inktag_status_v2_t image_store_create_access(
    const epd_screen_config_t *screen, uint32_t payload_size,
    uint8_t replace_record_id, uint8_t *saved_count, uint8_t *replace_ordinal_out,
    bool *replacing_out) {
    inktag_image_store_status_t status;
    uint16_t replace_ordinal = 0u;
    bool replacing = false;
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, &status);
    if (result == INKTAG_IMAGE_STORE_UNSUPPORTED)
        return INKTAG_STATUS_UNSUPPORTED;
    if (result != INKTAG_IMAGE_STORE_OK)
        return result == INKTAG_IMAGE_STORE_BUSY ? INKTAG_STATUS_BUSY : INKTAG_STATUS_IO_ERROR;
    if (replace_record_id != UINT8_MAX) {
        inktag_image_store_header_t header;
        if (replace_record_id >= inktag_image_store_fixed_slot_count())
            return INKTAG_STATUS_NOT_FOUND;
        result = inktag_image_store_get(replace_record_id, &header);
        if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NOT_FOUND)
            return INKTAG_STATUS_IO_ERROR;
        replacing = result == INKTAG_IMAGE_STORE_OK;
        if (replacing && !image_store_record_ordinal(replace_record_id, &replace_ordinal))
            return INKTAG_STATUS_IO_ERROR;
    }
    *saved_count = (uint8_t)status.saved_count;
    *replace_ordinal_out = (uint8_t)replace_ordinal;
    *replacing_out = replacing;
    return inktag_access_image_write(
        &s_activation, status.saved_count, replacing, replace_ordinal,
        replacing || status.free_sectors >= status.required_sectors,
        screen->visible_width, screen->visible_height);
}

static inktag_image_store_result_t image_store_clear_visible(
    bool incompatible_only, uint16_t *removed) {
    return incompatible_only
        ? inktag_image_store_clear_incompatible(inktag_epd_screen(), removed)
        : inktag_image_store_clear_all(removed);
}


static void send_screen(void) {
    uint8_t encoded[EPD_SCREEN_WIRE_SIZE];
    if (epd_screen_config_encode(inktag_epd_screen(), encoded) == EPD_SCREEN_STATUS_OK)
        (void)inktag_ble_notify(encoded, sizeof(encoded),
                                INKTAG_BLE_NOTIFY_BEST_EFFORT);
}

static uint8_t effective_device_label(char label[25]) {
    inktag_settings_t *settings = inktag_settings();
    const uint8_t *mac = inktag_ble_mac();
    uint16_t id4 = ((uint16_t)mac[1] << 8) | mac[0];
    if (!inktag_device_display_name(label, 25u, settings->label,
                                    settings->label_length, id4)) return 0u;
    return (uint8_t)strlen(label);
}

static void send_identity(void) {
    inktag_settings_t *settings = inktag_settings();
    uint8_t *mac = inktag_ble_mac();
    char name[20];
    char label[25];
    char buffer[128];
#if INKTAG_BUILD_PRO
    memcpy(name, settings->name, settings->name_length); name[settings->name_length] = 0;
#else
    uint8_t basic_name_length;
    const uint8_t *basic_name = inktag_ble_basic_name(&basic_name_length);
    memcpy(name, basic_name, basic_name_length); name[basic_name_length] = 0;
#endif
    (void)effective_device_label(label);
    GFX_format(buffer, sizeof(buffer),
        "settings:identity name=%s addr=%02X:%02X:%02X:%02X:%02X:%02X",
        name, mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    send_text(buffer);
    GFX_format(buffer, sizeof(buffer), "settings:device name=%s", label);
    send_text(buffer);
}

static void send_led_settings(void) {
    inktag_status_config_t *led = &inktag_settings()->led;
    char buffer[32];
    GFX_format(buffer, sizeof(buffer), "settings:led policy=%u", led->policy);
    send_text(buffer);
}

static const char *store_error(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return "ok";
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT: return "bad-argument";
        case INKTAG_IMAGE_STORE_BUSY: return "busy";
        case INKTAG_IMAGE_STORE_NO_SPACE: return "no-space";
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return "size-mismatch";
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return "crc-mismatch";
        case INKTAG_IMAGE_STORE_NOT_FOUND: return "not-found";
        default: return "io-error";
    }
}

static void send_store_error(const char *operation, inktag_image_store_result_t result) {
    char buffer[64];
    GFX_format(buffer, sizeof(buffer), "img:%s:error=%s", operation, store_error(result));
    send_text(buffer);
}

static bool valid_name(const uint8_t *name, uint8_t length) {
    return inktag_ble_device_name_write_valid(name, length);
}

static void record_display(uint8_t mode, uint8_t argument, uint32_t interval) {
    inktag_settings_t *settings = inktag_settings();
    /* A successful foreground display already satisfies any clock/calendar
     * boundary that elapsed while the panel was busy. */
    s_native_refresh_pending = false;
    s_native_refresh_paused = false;
    settings->display_mode = mode;
    settings->display_argument = argument;
    /* Native/static pages do not reset the saved rotation preference. */
    if (mode == EPD_DISPLAY_STATE_SLIDESHOW && interval >= 30u)
        settings->slideshow_interval = interval;
    (void)inktag_settings_save();
}

static void send_epd_result(const char *success) {
    if (inktag_epd_last_result() == INKTAG_EPD_RESULT_OK) {
        send_diag(success);
        if (!s_gui_led_deferred && s_nfc_action_pending != 2u)
            status_trigger(INKTAG_STATUS_SCENE_SUCCESS);
        return;
    }
    switch (inktag_epd_last_result()) {
        case INKTAG_EPD_RESULT_NO_BUSY_EDGE: send_diag("epd:no-busy-edge"); break;
        case INKTAG_EPD_RESULT_TIMEOUT: send_diag("epd:timeout"); break;
        case INKTAG_EPD_RESULT_STOPPED: send_diag("epd:stopped"); break;
        case INKTAG_EPD_RESULT_IO_ERROR: send_diag("epd:spi-error"); break;
        case INKTAG_EPD_RESULT_PON_TIMEOUT: send_diag("epd:pon-timeout"); break;
        case INKTAG_EPD_RESULT_DRF_TIMEOUT: send_diag("epd:drf-timeout"); break;
        case INKTAG_EPD_RESULT_POF_TIMEOUT: send_diag("epd:pof-timeout"); break;
        default: send_diag("epd:no-screen"); break;
    }
    if (!s_gui_led_deferred || inktag_epd_last_result()!=INKTAG_EPD_RESULT_STOPPED)
        status_trigger(INKTAG_STATUS_SCENE_ERROR);
}

static bool show_gui_internal(display_mode_t mode, uint8_t persistent_mode,
                              const char *terminal, bool persist_display) {
    inktag_settings_t *settings = inktag_settings();
    gui_countdown_t countdown = {
        settings->display_settings.name,
        settings->display_settings.target_day,
    };
    char name[20];
    bool shown;
#if INKTAG_BUILD_PRO
    memcpy(name, settings->name, settings->name_length); name[settings->name_length] = 0;
#else
    uint8_t basic_name_length;
    const uint8_t *basic_name = inktag_ble_basic_name(&basic_name_length);
    memcpy(name, basic_name, basic_name_length); name[basic_name_length] = 0;
#endif
    send_diag("gui:start");
    /* Native-page rasterization is product work rather than panel DRF. Keep
     * the common slow blue work cadence while pixels are composed/streamed;
     * inktag_app_gui_refresh_phase() switches to fast blue only at the real
     * controller-refresh boundary. */
    /* Decide before any status write: a partial candidate must never flash
     * slow blue first. Native full selection re-enables its own indication. */
    s_gui_led_deferred=mode==MODE_CLOCK && inktag_app_clock_partial_allowed();
    if (!s_gui_led_deferred) status_active(INKTAG_STATUS_SCENE_TRANSFER, true);
    /* Automatic clock/calendar and boot pages have no host transaction, but
     * still own the panel while cooperative BLE callbacks run. */
    s_native_page_active = true;
    shown = inktag_epd_show_gui(
        mode, settings->timestamp, settings->display_settings.week_start,
        &countdown, name);
    s_native_page_active = false;
    if (!s_gui_led_deferred) {
        status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    }
    /* A host-owned display sends its cancel ACK from v2_display_complete.
     * For an autonomous page, acknowledge only after the same panel cleanup;
     * do not invent a completion event with a nonexistent host transaction. */
    if (!s_display_active && s_cancel_transaction != 0u) {
        send_v2_response(INKTAG_CMD_CANCEL, s_cancel_transaction,
                         INKTAG_STATUS_OK, NULL, 0u);
        s_cancel_transaction = 0u;
    }
    if (shown && persist_display) {
        record_display(persistent_mode, 0xFFu, 0u);
    }
    send_epd_result(terminal);
    /* Foreground ownership keeps this result until its protocol COMPLETE;
     * autonomous/button/NFC pages have no later host completion. */
    if (!s_display_active) s_gui_led_deferred=false;
    return shown;
}

static bool show_gui(display_mode_t mode, uint8_t persistent_mode,
                     const char *terminal) {
    return show_gui_internal(mode, persistent_mode, terminal, true);
}

typedef enum {
    AUTOMATIC_DISPLAY_REQUESTED = 0,
    AUTOMATIC_DISPLAY_SUPPRESS,
} automatic_display_decision_t;

static automatic_display_decision_t automatic_display_decision(void) {
    /* Sample before LEDs, panel power or external Flash can load the rail.
     * Warning voltage does not replace or suppress an admitted normal page.
     * Preserve the existing health backend's route/ADC/threshold policy. */
    return inktag_tlsr_health_operation_allowed(
        INKTAG_POWER_OPERATION_DISPLAY, true) ?
        AUTOMATIC_DISPLAY_REQUESTED : AUTOMATIC_DISPLAY_SUPPRESS;
}

static bool automatic_display_refresh_allowed(void) {
    return !s_native_refresh_paused && !s_slideshow &&
        inktag_epd_control_access(inktag_epd_screen()) == INKTAG_STATUS_OK;
}

static bool show_automatic_native_page(uint8_t display_mode) {
    bool shown;
    automatic_display_decision_t decision = automatic_display_decision();
    if (decision == AUTOMATIC_DISPLAY_SUPPRESS) return true;
    if (display_mode == EPD_DISPLAY_STATE_CALENDAR) {
        shown = show_gui_internal(MODE_CALENDAR, display_mode,
                                  "calendar:done", false);
    } else {
        shown = show_gui_internal(MODE_CLOCK, display_mode,
                                  "clock:done", false);
    }
    /* Consume boundaries crossed during this synchronous work, including a
     * failure, not a permanent settings bit. The next real boundary may run. */
    update_timestamp_from_clock();
    s_native_refresh_pending = false;
    return shown;
}

static bool show_stored_with_diag(uint8_t id) {
    inktag_image_store_header_t header;
    bool shown;
    if (inktag_image_store_get(id, &header) != INKTAG_IMAGE_STORE_OK) {
        send_diag("picture:not-found");
        return false;
    }
    if (!image_store_record_visible(id)) {
        send_diag("picture:denied");
        return false;
    }
    if (!screen_matches(header.screen_config)) {
        send_diag("picture:incompatible");
        return false;
    }
    /* Autonomous slideshow/boot replay has no host display transaction, but
     * owns the panel across cooperative BLE callbacks just like a native page.
     * s_picture_active instead belongs to a DIRECT_FRAME transfer. */
    s_native_page_active = true;
    send_diag("picture:start");
    shown = inktag_epd_show_stored(id, inktag_app_brand_edition());
    s_native_page_active = false;
    /* Match native-page cleanup: host transactions retain their cancel ACK;
     * an autonomous replay acknowledges only after the panel is parked. */
    if (!s_display_active && s_cancel_transaction != 0u) {
        send_v2_response(INKTAG_CMD_CANCEL, s_cancel_transaction,
                         INKTAG_STATUS_OK, NULL, 0u);
        s_cancel_transaction = 0u;
    }
    send_diag(shown ? "picture:done" : "picture:error");
    return shown;
}

static bool clear_with_diag(uint8_t color) {
    bool shown;
    send_diag("clear:start");
    shown = inktag_epd_clear(color);
    send_diag(shown ? "clear:done" : "clear:error");
    return shown;
}

static void slideshow_stop(bool report) {
    s_slideshow = false;
    s_slideshow_id = 0xFFu;
    s_next_slide = 0u;
    if (report) send_text("img:slideshow:stopped");
}

static void update_timestamp_from_clock(void) {
    inktag_settings_t *settings = inktag_settings();
    uint32_t previous_timestamp = settings->timestamp;
    uint32_t now_tick = inktag_tlsr_clock_now();
    inktag_elapsed_time_t elapsed = inktag_elapsed_clock_sample_time_shared(
        &s_elapsed_clock, now_tick);
    uint32_t utc_elapsed_ms = inktag_elapsed_clock_sample_time_shared(
        &s_utc_elapsed_clock, now_tick).milliseconds;

    /* UTC alone is scaled; security/NFC/health and SDK deadlines stay raw.
     * Use milliseconds, not rounded seconds, and retain both fractions. */
    uint32_t adjusted_ms = utc_elapsed_ms;
    if (!inktag_clock_rate_apply(&s_utc_rate, utc_elapsed_ms, &adjusted_ms))
        send_diag("clock:rate:error"); /* Preserve raw elapsed time on failure. */
    /* The 32-bit 16 MHz counter bounds one sample to <269 seconds.
     * Combining before division is safe and shares the div/mod operation. */
    adjusted_ms += s_utc_remainder_ms;
    elapsed.seconds = adjusted_ms / 1000u;
    s_utc_remainder_ms = (uint16_t)(adjusted_ms % 1000u);
    settings->timestamp += elapsed.seconds;
    s_nfc_now_ms += elapsed.milliseconds;
    if (elapsed.seconds == 0u) return;
#if INKTAG_BUILD_PRO
#include "inktag_app_find_elapsed_pro.inc"
#endif
    if (!automatic_display_refresh_allowed()) return;

    if ((settings->display_mode == EPD_DISPLAY_STATE_CALENDAR &&
         inktag_elapsed_clock_crossed_period(
             previous_timestamp, settings->timestamp, 86400u)) ||
        (settings->display_mode == EPD_DISPLAY_STATE_CLOCK &&
         inktag_elapsed_clock_crossed_period(
             previous_timestamp, settings->timestamp, 60u))) {
        /* Collapse delayed callbacks and any number of crossed boundaries
         * into one device-owned refresh after foreground work becomes idle. */
        s_native_refresh_pending = true;
    }
}

#if INKTAG_BUILD_PRO
#include "inktag_app_find_pro.inc"
#else
static void mark_findmy_time_valid(void) { s_findmy_time_valid = true; }
static void apply_findmy_runtime(void) {}
static void clear_findmy_runtime_identity(void) {}
static bool refresh_findmy_key_if_safe(void) { return true; }
#endif

static void rebase_timestamp_clock(uint32_t previous_timestamp,
                                   uint8_t previous_week_start) {
    inktag_settings_t *settings = inktag_settings();
    uint32_t now_tick = inktag_tlsr_clock_now();

    /* Settle unsampled RAW time and preserve its sub-ms remainder. Only the
     * UTC sampler/correction phase is rebased by SET_TIME. It must not
     * discard a boundary already queued by the timer, and a correction which
     * changes the visible calendar/clock value needs one redraw of its own. */
    if (automatic_display_refresh_allowed() &&
        ((settings->display_mode == EPD_DISPLAY_STATE_CALENDAR &&
          (previous_week_start != settings->display_settings.week_start ||
           inktag_elapsed_clock_crossed_period(
               previous_timestamp, settings->timestamp, 86400u))) ||
         (settings->display_mode == EPD_DISPLAY_STATE_CLOCK &&
          inktag_elapsed_clock_crossed_period(
              previous_timestamp, settings->timestamp, 60u)))) {
        s_native_refresh_pending = true;
    }
    s_nfc_now_ms += inktag_elapsed_clock_sample_time_shared(
        &s_elapsed_clock, now_tick).milliseconds;
    inktag_elapsed_clock_reset(
        &s_utc_elapsed_clock, now_tick,
        s_elapsed_clock.counter_mask, s_elapsed_clock.ticks_per_second);
    (void)inktag_clock_rate_set(&s_utc_rate, s_utc_rate.ppm);
    s_utc_remainder_ms = 0u;
}

static bool save_explicit_time(void) {
    bool saved = inktag_settings_save_time();
    /* Flash service time belongs to both timelines, even on refusal. */
    update_timestamp_from_clock();
    if (saved) inktag_time_checkpoint_saved(&s_time_checkpoint, s_nfc_now_ms);
    else inktag_time_checkpoint_failed(&s_time_checkpoint, s_nfc_now_ms);
    return saved;
}

static void service_time_checkpoint(void) {
    if (!s_findmy_time_valid || !inktag_settings_time_checkpoint_ready() ||
        !inktag_time_checkpoint_due(&s_time_checkpoint, s_nfc_now_ms) ||
        s_tail != s_head || s_connected || s_display_active ||
        s_picture_active || protocol_transfer_active() ||
        inktag_image_store_capture_active() || inktag_nfc_busy() ||
        s_nfc_action_pending || s_system_reboot_pending ||
        s_recovery_confirmation_due) return;
    if (!inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        inktag_time_checkpoint_failed(&s_time_checkpoint, s_nfc_now_ms);
        return;
    }
    update_timestamp_from_clock();
    bool saved = inktag_settings_checkpoint_time();
    update_timestamp_from_clock();
    if (saved) inktag_time_checkpoint_saved(&s_time_checkpoint, s_nfc_now_ms);
    else inktag_time_checkpoint_failed(&s_time_checkpoint, s_nfc_now_ms);
}

static void slideshow_arm_next_after_refresh(void) {
    inktag_settings_t *settings = inktag_settings();
    uint32_t interval = settings->slideshow_interval;
    if (interval < 30u) interval = 30u;
    /* E-paper refreshes pump BLE but deliberately do not recurse into the
     * application dispatcher. Account for that elapsed wall time before
     * starting a new slide interval, or a slow BWRY refresh can consume the
     * whole interval and immediately trigger the following image. */
    update_timestamp_from_clock();
    s_next_slide = settings->timestamp + interval;
}

static bool slideshow_advance(void) {
    uint8_t next;
    bool shown;
    inktag_settings_t *settings = inktag_settings();
    if (!image_store_next_visible_compatible(s_slideshow_id, true, &next)) {
        if (s_slideshow) slideshow_arm_next_after_refresh();
        send_text("img:slideshow:error=no-compatible-images");
        status_trigger(INKTAG_STATUS_SCENE_ERROR);
        return false;
    }
    status_active(INKTAG_STATUS_SCENE_REFRESH, true);
    shown = show_stored_with_diag(next);
    status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    if (shown) {
        s_slideshow_id = next;
        if (s_slideshow) {
            slideshow_arm_next_after_refresh();
            record_display(4u, next, settings->slideshow_interval);
        }
        send_format("img:show:done id=%u", next, 0, 0, 0, 0, 0);
        status_trigger(INKTAG_STATUS_SCENE_SUCCESS);
        return true;
    } else {
        if (s_slideshow) slideshow_arm_next_after_refresh();
        send_text("img:show:error=display");
        status_trigger(INKTAG_STATUS_SCENE_ERROR);
        return false;
    }
}

static bool slideshow_show_at(uint8_t id, uint32_t interval) {
    bool shown;
    slideshow_stop(false);
    if (!stored_image_replayable(id, true)) return false;
    inktag_settings()->slideshow_interval = interval < 30u ? 300u : interval;
    s_slideshow = true;
    status_active(INKTAG_STATUS_SCENE_REFRESH, true);
    shown = show_stored_with_diag(id);
    status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    if (s_slideshow) slideshow_arm_next_after_refresh();
    if (!shown) {
        status_trigger(INKTAG_STATUS_SCENE_ERROR);
        return false;
    }
    s_slideshow_id = id;
    if (s_slideshow)
        record_display(EPD_DISPLAY_STATE_SLIDESHOW, id,
                       inktag_settings()->slideshow_interval);
    send_format("img:show:done id=%u", id, 0, 0, 0, 0, 0);
    if (s_nfc_action_pending != 2u) status_trigger(INKTAG_STATUS_SCENE_SUCCESS);
    return true;
}

static bool page_cycle_move(bool previous, uint8_t *argument) {
    inktag_settings_t *settings = inktag_settings();
    epd_display_state_t current = {
        s_slideshow ? EPD_DISPLAY_STATE_SLIDESHOW : settings->display_mode,
        settings->display_argument,
        0u,
        settings->slideshow_interval,
    };
    uint8_t first = UINT8_MAX;
    /* Only the two entrances need a scan. Rotation itself keeps its existing
     * timer-driven sparse-slot traversal; manual navigation never steps it. */
    bool available =
        ((previous && current.mode == EPD_DISPLAY_STATE_CALENDAR) ||
         (!previous && current.mode == EPD_DISPLAY_STATE_DEVICE_INFO)) &&
        image_store_next_visible_compatible_linear(UINT8_MAX, true, &first);
    inktag_page_cycle_target_t target = previous ?
        inktag_page_cycle_previous(&current, available, false) :
        inktag_page_cycle_next(&current, available, false);
    if (argument) *argument = UINT8_MAX;

    switch (target) {
        case INKTAG_PAGE_CYCLE_CALENDAR:
            slideshow_stop(false);
            return show_gui(MODE_CALENDAR, EPD_DISPLAY_STATE_CALENDAR,
                            "calendar:done");
        case INKTAG_PAGE_CYCLE_CLOCK:
            slideshow_stop(false);
            return show_gui(MODE_CLOCK, EPD_DISPLAY_STATE_CLOCK,
                            "clock:done");
        case INKTAG_PAGE_CYCLE_DEVICE_INFO:
            slideshow_stop(false);
            return show_gui(MODE_DEVICE_INFO, EPD_DISPLAY_STATE_DEVICE_INFO,
                            "device-info:done");
        case INKTAG_PAGE_CYCLE_SLIDESHOW:
            if (!slideshow_show_at(first, settings->slideshow_interval))
                return false;
            if (argument) *argument = first;
            return true;
        default:
            return false;
    }
}

static bool page_cycle_advance(uint8_t *argument) {
    return page_cycle_move(false, argument);
}

static bool page_cycle_previous(uint8_t *argument) {
    return page_cycle_move(true, argument);
}



#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_status_pro.inc"
#else
static bool input_show_gui(display_mode_t mode, uint8_t persistent_mode,
    const char *terminal) {
    slideshow_stop(false);
    return show_gui(mode, persistent_mode, terminal);
}

#endif

static bool panel_transaction_active(void) {
    return s_display_active || s_native_page_active || s_picture_active ||
           protocol_transfer_active() || inktag_image_store_capture_active() ||
           s_system_reboot_pending;
}

static bool input_action_busy(void) {
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_1_pro.inc"
#endif
    return panel_transaction_active() || s_head != s_tail;
}

#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_2_pro.inc"
#endif

static inktag_status_v2_t input_display_result(bool shown) {
    inktag_status_v2_t status;
    if (shown) return INKTAG_STATUS_OK;
    status = v2_epd_status();
    return status == INKTAG_STATUS_OK ? INKTAG_STATUS_NOT_FOUND : status;
}

static inktag_status_v2_t process_input_action(const inktag_input_binding_t *binding,
                                 inktag_input_source_t source) {
    inktag_settings_t *settings = inktag_settings();
    bool completed = true;
    if (!binding) return INKTAG_STATUS_INVALID_REQUEST;
    if (inktag_access_input_action(&s_activation, source, binding->action) !=
        INKTAG_STATUS_OK) {
        send_text("input:denied");
        return INKTAG_STATUS_DENIED;
    }
    switch (binding->action) {
        case INKTAG_INPUT_ACTION_DISABLED:
        case INKTAG_INPUT_ACTION_CONTROL_URL:
            send_text("input:disabled");
            break;
        case INKTAG_INPUT_ACTION_NEXT_PAGE:
            completed = page_cycle_advance(NULL);
            break;
        case INKTAG_INPUT_ACTION_PREVIOUS_PAGE:
            completed = page_cycle_previous(NULL);
            break;
        case INKTAG_INPUT_ACTION_SHOW_SLOT:
        {
            bool shown;
            if (!stored_image_replayable(binding->slot_id, false))
                return INKTAG_STATUS_NOT_FOUND;
            slideshow_stop(false);
            status_active(INKTAG_STATUS_SCENE_REFRESH, true);
            shown = show_stored_with_diag(binding->slot_id);
            completed = shown;
            status_active(INKTAG_STATUS_SCENE_REFRESH, false);
            if (shown) {
                record_display(0u, binding->slot_id, 0u);
                send_format("img:show:done id=%u", binding->slot_id, 0, 0, 0, 0, 0);
                status_trigger(INKTAG_STATUS_SCENE_SUCCESS);
            } else {
                send_text("input:slot-missing");
                status_trigger(INKTAG_STATUS_SCENE_ERROR);
            }
            break;
        }
        case INKTAG_INPUT_ACTION_SHOW_DEVICE_INFO:
            completed = input_show_gui(MODE_DEVICE_INFO, 5u, "device-info:done");
            break;
        case INKTAG_INPUT_ACTION_SHOW_CALENDAR:
            completed = input_show_gui(MODE_CALENDAR, 1u, "calendar:done");
            break;
        case INKTAG_INPUT_ACTION_SHOW_CLOCK:
            completed = input_show_gui(MODE_CLOCK, 2u, "clock:done");
            break;
        case INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW:
            if (s_slideshow) {
                uint8_t last_id = s_slideshow_id;
                slideshow_stop(true);
                if (last_id != 0xFFu) record_display(0u, last_id, 0u);
            } else {
                if (settings->slideshow_interval < 30u)
                    settings->slideshow_interval = 300u;
                s_slideshow = true;
                s_slideshow_id = 0xFFu;
                s_next_slide = settings->timestamp;
                completed = slideshow_advance();
            }
            break;
        case INKTAG_INPUT_ACTION_STOP:
            s_native_refresh_paused = true;
            s_native_refresh_pending = false;
            inktag_epd_stop();
            slideshow_stop(false);
            send_text("epd:stopped");
            break;
        case INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS:
            if (settings->display_mode != 3u) {
                bool cleared;
                s_input_clear_return_valid = true;
                s_input_clear_return_mode = s_slideshow ?
                    EPD_DISPLAY_STATE_SLIDESHOW : settings->display_mode;
                s_input_clear_return_argument = s_slideshow ?
                    s_slideshow_id : settings->display_argument;
                s_input_clear_return_interval = settings->slideshow_interval;
                slideshow_stop(false);
                status_active(INKTAG_STATUS_SCENE_REFRESH, true);
                cleared = clear_with_diag(1u);
                completed = cleared;
                status_active(INKTAG_STATUS_SCENE_REFRESH, false);
                if (cleared) record_display(3u, 1u, 0u);
                status_trigger(cleared ? INKTAG_STATUS_SCENE_SUCCESS :
                                         INKTAG_STATUS_SCENE_ERROR);
            } else if (s_input_clear_return_valid) {
                uint8_t mode = s_input_clear_return_mode;
                uint8_t argument = s_input_clear_return_argument;
                uint32_t interval = s_input_clear_return_interval;
                if (mode == 5u)
                    completed = show_gui(MODE_DEVICE_INFO, 5u, "device-info:done");
                else if (mode == 1u)
                    completed = show_gui(MODE_CALENDAR, 1u, "calendar:done");
                else if (mode == 2u)
                    completed = show_gui(MODE_CLOCK, 2u, "clock:done");
                else if (mode == 0u && argument != 0xFFu) {
                    bool shown;
                    status_active(INKTAG_STATUS_SCENE_REFRESH, true);
                    shown = show_stored_with_diag(argument);
                    completed = shown;
                    status_active(INKTAG_STATUS_SCENE_REFRESH, false);
                    if (shown) record_display(0u, argument, 0u);
                    status_trigger(shown ? INKTAG_STATUS_SCENE_SUCCESS :
                                           INKTAG_STATUS_SCENE_ERROR);
                } else if (mode == 4u) {
                    completed = slideshow_show_at(argument, interval);
                }
                else completed = false;
                if (completed) s_input_clear_return_valid = false;
            }
            else completed = false;
            break;
        default:
            send_text("input:unsupported");
            return INKTAG_STATUS_UNSUPPORTED;
    }
    return input_display_result(completed);
}

#if INKTAG_BUILD_PRO
static inktag_status_v2_t apply_time(uint32_t timestamp,
    int16_t timezone, uint8_t week_start, bool quiet);
#include "inktag_app_nfc_commands_pro.inc"
#endif

#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_app_aux_hook_1_pro.inc"
#endif
#endif



#include "inktag_protocol_v2.inc"
#if INKTAG_BUILD_PRO
#include "inktag_app_idle_pro.inc"
#endif

/* Only these existing Telink handlers are safe inside synchronous EPD work.
 * NFC probes, resource reads and all writes retain foreground ownership. This
 * is a backend reentrancy list, not an alternative protocol/security parser. */
static bool cooperative_snapshot_opcode(uint8_t opcode) {
    switch (opcode) {
        case INKTAG_CMD_HELLO:
        case INKTAG_CMD_GET_CAPABILITIES:
        case INKTAG_CMD_GET_DEVICE_INFO:
        case INKTAG_CMD_GET_STATUS:
        case INKTAG_CMD_GET_HEALTH:
        case INKTAG_CMD_GET_TIME:
        case INKTAG_CMD_GET_SCREEN_PROFILE:
        case INKTAG_CMD_GET_INPUT_CONFIG:
        case INKTAG_CMD_GET_IO_CONFIG:
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
            return true;
        default: return false;
    }
}

void inktag_app_cooperative_snapshot(void) {
    rx_packet_t packet;
    if (s_connection_temperature_state == 2u || s_tail == s_head ||
        s_queue[s_tail].length < INKTAG_PROTOCOL_V2_HEADER_SIZE ||
        !cooperative_snapshot_opcode(s_queue[s_tail].data[0]) ||
        !inktag_ble_notifications_drained()) return;
    /* Never skip a queued mutation, retain a pointer into a reusable slot, or
     * re-enter app_process(). Original dispatch rechecks flags and password
     * admission even if authorization changed after enqueue.
     * Wait for notification capacity instead of losing replies during SDK
     * backpressure; the surrounding service still drains BLE on every yield. */
    packet = s_queue[s_tail];
    s_tail = (uint8_t)((s_tail + 1u) % RX_QUEUE_SLOTS);
    process_packet(packet.data, packet.length);
}

static bool protocol_transfer_active(void) {
    return s_v2_transfer.active;
}

void inktag_app_init(void) {
    inktag_power_thresholds_t thresholds;
    inktag_timebase_profile_t timebase;
    epd_display_state_t persisted_display;
    inktag_boot_display_plan_t boot_plan;
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_app_aux_hook_2_pro.inc"
#endif
#endif
    inktag_board_id_t board = inktag_board_detect();
    bool thresholds_qualified = inktag_tlsr_board_power_thresholds(
        board, &thresholds);

    s_head = s_tail = 0;
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_1_0_pro.inc"
#endif
    s_notify = false;
    if (!inktag_tlsr_clock_profile(board, &timebase)) {
        timebase.counter_ticks_per_second = 16000000u;
        timebase.counter_mask = 0xFFFFFFFFu;
    }
    inktag_elapsed_clock_reset(
        &s_elapsed_clock, inktag_tlsr_clock_now(), timebase.counter_mask,
        timebase.counter_ticks_per_second);
    s_utc_elapsed_clock = s_elapsed_clock;
    s_nfc_now_ms = 0u;
    inktag_time_checkpoint_init(&s_time_checkpoint, s_nfc_now_ms,
                                inktag_settings_time_valid());
    inktag_device_password_init(&s_password, s_nfc_now_ms);
    (void)inktag_tlsr_password_load(&s_password_config);
    s_debug_grace_until = INKTAG_DEBUG_GRACE_MS;
    inktag_soak_telemetry_init(
        INKTAG_SOAK_FLAG_FIND_WINDOWS |
        INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
        INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
        INKTAG_SOAK_FLAG_SLEEP_ENTRIES,
        s_nfc_now_ms);
    s_timezone_minutes = inktag_settings()->timezone_minutes;
    persisted_display.mode = inktag_settings()->display_mode;
    persisted_display.argument = inktag_settings()->display_argument;
    persisted_display.reserved =
        (uint16_t)inktag_settings()->display_settings.flags |
        inktag_settings()->display_internal_flags;
    persisted_display.interval_seconds = inktag_settings()->slideshow_interval;
    s_recovery_confirmation_due = inktag_tlsr_recovery_confirmation_pending();
    boot_plan = inktag_boot_display_plan(
        inktag_tlsr_board_first_display_requested(),
        s_recovery_confirmation_due, &persisted_display);
    /* First-display consumption waits for verified activation in app_start.
     * A InkTag E6 preset must stay connectable without spending this marker. */
    s_boot_restore_target = boot_plan.target;
    s_boot_restore_pending = boot_plan.target != INKTAG_BOOT_DISPLAY_NONE;
    s_boot_restore_wait_startup = boot_plan.defer_until_startup_complete;
    s_boot_restore_due = false;
    s_startup_role_ready = false;
    s_startup_sequence_complete = false;
    s_native_refresh_pending = false;
    s_native_refresh_paused = false;
#if INKTAG_BUILD_PRO
    s_nfc_action_pending = false;
#endif
    s_input_clear_return_valid = false;
    s_recovery_confirmation_retry_at = 0u;
    s_system_reboot_pending = false;
    s_system_reboot_reply_drained = false;
    s_system_reboot_at = 0u;
    s_connected = false;
    s_connection_temperature_state = 0u;
    s_display_active = false;
    s_native_page_active = false;
    s_gui_led_deferred = false;
    s_display_opcode = 0u;
    s_display_transaction = 0u;
    s_cancel_transaction = 0u;
    memset(&s_activation, 0, sizeof(s_activation));
#if INKTAG_BUILD_PRO
#include "inktag_app_find_init_pro.inc"
#endif
    /* Flash preserves a last snapshot, not power-off elapsed time. No armed
     * continuous reboot anchor exists here. Retention skips cold App init and
     * keeps the live trust flag; cold/reset/Recovery return require SET_TIME. */
    s_findmy_time_valid = false;
#if INKTAG_BUILD_PRO
    s_findmy_active_key_index = INKTAG_FINDMY_FALLBACK_KEY_INDEX;
#endif
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_app_aux_hook_3_pro.inc"
#endif
#endif
    memset(&s_status, 0, sizeof(s_status));
    s_status_test_until = 0u;
    s_status_test_output = 0u;
    s_status_applied_output = 0xFFu;
    s_status_test_active = false;
    const inktag_tlsr_board_profile_t *power_profile =
        inktag_tlsr_board_profile(board);
    inktag_tlsr_health_init(power_profile && power_profile->supply_adc ?
                            inktag_epd_voltage : NULL,
                            power_profile ? &thresholds : NULL,
                            thresholds_qualified, true);
}

void inktag_app_resume(void) {
    /* Restore retained outputs after gpio_init(false). The RAM wake owner
     * restores only lost PC4 wake bits; full GPIO reconfiguration
     * here previously broke Nowa BLE timer wake/GATT. */
    inktag_epd_resume();
    inktag_led_apply(NULL, s_status_applied_output);
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_app_aux_hook_4_pro.inc"
#endif
#endif
}

bool inktag_app_debug_grace_active(void) {
    /* Cold/normal reset reopens this deadline in app_init. Expire once so
     * uint32 wrap/half-range comparisons cannot reopen an old boot grace. */
    if (s_debug_grace_until == 0u) return false;
    if (status_before(s_nfc_now_ms, s_debug_grace_until)) return true;
    s_debug_grace_until = 0u;
    return false;
}

bool inktag_app_requires_awake(void) {
    /* Lifecycle replies are acknowledged before their delayed reset. Do not
     * enter deep retention in that short handoff window: on actual TLSR8359
     * hardware it can defer the deadline indefinitely and leave the host with
     * a successful RESET/OTA_RECOVERY reply but no subsequent reboot. */
    if (s_system_reboot_pending) return true;
    /* FM11NC08 Type-4 traffic is a millisecond-scale, multi-APDU exchange.
     * PC4 can wake an idle application, but suspending between SELECT/READ
     * frames adds enough scheduling latency for some phones to abandon the
     * ISO-DEP transaction and start again. Keep only the bounded RF session
     * awake; the field tracker releases this hold after 200 ms of quiet. */
    if (inktag_nfc_busy()) return true;
    /* The QR staging record lives in ordinary SRAM instead of spending scarce
     * retained RAM. Its transfer is at most 407 bytes, so keep only that
     * bounded transaction awake; image and other protocol transfers retain
     * their established event-aware sleep behavior. */
    if (inktag_tlsr_device_info_qr_transfer_active()) return true;
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#endif
#else
    /* Fall through to the bounded status-output checks. */
#endif
    if (inktag_status_locate_active(&s_status)) return true;
    /* gpio_init(false) restores the board's safe, inactive LED levels on
     * every deep-retention wake. Keep the CPU awake only for bounded LED
     * diagnostics and transient scenes so their sub-second phases remain
     * continuously visible. Long-lived advertising and inactive LEDs still
     * use the ordinary event-aware retention path. */
    if (s_status_test_active &&
        status_before(s_nfc_now_ms, s_status_test_until)) return true;
    return s_status.transient_scene != STATUS_NO_SCENE &&
           status_before(s_nfc_now_ms, s_status.transient_until);
}

static void admit_boot_display(void) {
    if (s_boot_restore_pending) {
        if (inktag_epd_control_access(inktag_epd_screen()) != INKTAG_STATUS_OK ||
            (inktag_tlsr_board_first_display_requested() &&
             !inktag_tlsr_board_consume_first_display_request())) {
            s_boot_restore_pending = false;
            s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
        }
    }
}

void inktag_app_start(void) {
    inktag_status_config_t status_config;
    const inktag_tlsr_gpio_profile_t *profile = inktag_tlsr_gpio_profile();
    /* PA7 is SWS as well as the blue LED. Do not touch it until the radio and
     * advertising state are established; the GPIO adapter releases it back to
     * AS_SWIRE whenever the shared status renderer turns blue off. */
#if INKTAG_BUILD_PRO
#include "inktag_app_code_guard_pro.inc"
#endif

    activation_init();
#if INKTAG_BUILD_PRO
    advertising_config_load();
    apply_custom_input_runtime();
#endif
    admit_boot_display();
    /* Cold App entry (including reset/OTA), never retention restoration. */
    (void)inktag_clock_rate_set(&s_utc_rate,
        inktag_tlsr_board_clock_rate_default(inktag_board_detect()));
    s_utc_remainder_ms = 0u;
#if INKTAG_BUILD_PRO
#include "inktag_app_find_start_pro.inc"
#endif
    inktag_led_init();
    status_effective_config(&status_config);
    inktag_status_init(&s_status, &status_config,
                       profile ? profile->indicator_colors : 0u,
                       status_output, NULL,
                       s_nfc_now_ms);
    status_active(INKTAG_STATUS_SCENE_ADVERTISING, true);
    status_trigger(INKTAG_STATUS_SCENE_STARTUP);
}

#if INKTAG_BUILD_PRO
#include "inktag_app_partial_pro.inc"
#else
bool inktag_app_clock_partial_allowed(void) { return false; }
#endif

#if INKTAG_BUILD_PRO
#include "inktag_app_activation_query_pro.inc"
#else
bool inktag_app_activated(void) { return false; }
#endif

inktag_status_v2_t inktag_app_screen_access(
    const epd_screen_config_t *screen, bool backend_available) {
    return inktag_access_check(&s_activation,
        screen != NULL && epd_screen_is_e6(screen->driver_id, screen->panel_recipe) ?
            INKTAG_PRODUCT_FEATURE_E6_CONTROL : INKTAG_PRODUCT_FEATURE_BASIC,
        backend_available, true, true);
}

#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_permission_pro.inc"
#else
bool inktag_app_nfc_commands_allowed(void) { return false; }
#endif

#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_3_pro.inc"
#endif

#if INKTAG_BUILD_PRO
#include "inktag_app_brand_edition_pro.inc"
#else
inktag_brand_edition_t inktag_app_brand_edition(void) { return INKTAG_BRAND_EDITION_DEFAULT; }
#endif

#if INKTAG_BUILD_PRO
#include "inktag_app_find_permission_pro.inc"
#else
bool inktag_app_find_runtime_allowed(bool experiment_enabled) { (void)experiment_enabled; return false; }
#endif

uint32_t inktag_app_monotonic_ms(void) { return s_nfc_now_ms; }

void inktag_app_set_notify(bool enabled) {
    s_notify = enabled;
}

void inktag_app_set_connected(bool connected) {
    if (connected != s_connected) {
        inktag_device_password_session_clear(&s_password);
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_2_0_pro.inc"
#endif
        s_connection_temperature_state = connected ? 1u : 0u;
    }
    s_connected = connected;
    status_active(INKTAG_STATUS_SCENE_ADVERTISING, !connected);
    status_active(INKTAG_STATUS_SCENE_CONNECTED, false);
    if (connected) status_trigger(INKTAG_STATUS_SCENE_CONNECTED);
    else {
        status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
        status_active(INKTAG_STATUS_SCENE_OTA, false);
    }
}

void inktag_app_disconnected(void) {
    bool cancel_host_display = s_display_active || protocol_transfer_active();
#if INKTAG_BUILD_PRO
    inktag_app_advertising_activity();
#endif
    /* A disconnected client cannot finish or abort its transfer. Drop only
     * host-owned transient work; autonomous slideshow state remains owned by
     * the device and its persisted display state. */
    s_tail = s_head;
    /* A delayed native-page cancel ACK belongs only to the vanished link. */
    s_cancel_transaction = 0u;
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_1_1_pro.inc"
#endif
    /* Boot restore/day ticks belong to the device, not a read-only link.
     * Keep foreground busy until cleanup returns, but revoke its old reply. */
    s_display_transaction = 0u;
    v2_transfer_reset(true);
    v2_find_owner_clear();
    inktag_device_password_session_clear(&s_password);
    /* Snapshot ownership before transfer reset clears it. A read-only link
     * vanishing during device-owned slideshow/native refresh is not CANCEL. */
    if (cancel_host_display) inktag_epd_stop();
}

void inktag_app_set_ota_active(bool active, bool success) {
    status_active(INKTAG_STATUS_SCENE_OTA, active);
    if (!active) {
        status_trigger(success ? INKTAG_STATUS_SCENE_SUCCESS :
                                 INKTAG_STATUS_SCENE_ERROR);
    }
}

void inktag_app_status_service(void) {
    update_timestamp_from_clock();
    if (s_status_test_active &&
        !status_before(s_nfc_now_ms, s_status_test_until)) {
        s_status_test_active = false;
        s_status.last_output = 0xFFu;
    }
    inktag_status_tick(&s_status, s_nfc_now_ms);
}

void inktag_app_gui_prepared(uint32_t gap_bytes, uint32_t prepare_ms,
                             uint32_t stream_ms, uint32_t stream_bytes,
                             uint32_t max_gap_ms) {
    /* Actual monotonic intervals, not the historical edge-time placeholders.
     * Stream includes bounded cooperative radio service inside data writes.
     * at_B locates the largest gap in the cumulative data-call byte stream. */
    send_format("gui:prepared at=%u p_ms=%u tx_ms=%u B=%u gap_ms=%u",
                gap_bytes, prepare_ms, stream_ms, stream_bytes, max_gap_ms, 0u);
}

void inktag_app_gui_refresh_phase(bool partial) {
    /* This is the product-wide native-page stage tag consumed by both CLI
     * logs and WebUI progress. The adapter has selected its actual partial
     * or full transaction; only full work owns a refresh indication. */
    s_gui_led_deferred=partial;
#if INKTAG_BUILD_PRO
    if (partial) send_text("clock:partial");
#endif
    if (!partial) {
        status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
    }
    send_diag("gui:refresh");
}

void inktag_app_packet(const uint8_t *data, uint16_t length) {
    bool display_owner_command;
    uint8_t next;
    uint8_t flags;
    /* A truncated header cannot be admitted or take boot/display ownership. */
    if (!data || length < INKTAG_PROTOCOL_V2_HEADER_SIZE || length > MAX_PACKET) return;
    /* Keep the tagged packet in the queue for the deferred admission check.
     * Reject old namespaces before AUTH, fast CANCEL or any ownership change. */
    if (data[1] == 0u || !inktag_release_request_flags(data[2], &flags)) {
        send_v2_response(data[0], data[1], INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    /* Ingress has a fast CANCEL path during refresh. Authenticate before
     * that path (and before changing boot/display ownership), then recheck
     * again at deferred dispatch in case revision/session changed. */
    if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        !inktag_device_password_command_allowed(&s_password, &s_password_config, data[0])) {
        send_v2_response(data[0], data[1], INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_4_pro.inc"
#endif
    /* New work is rejected before taking queue/boot ownership. Continuations
     * are checked in deferred dispatch, where an owned transfer can safely
     * sleep/park without re-entering cleanup from a cooperative BLE callback. */
    if (data[0] != INKTAG_CMD_TRANSFER_WRITE &&
        data[0] != INKTAG_CMD_TRANSFER_COMMIT) {
        inktag_status_v2_t access = v2_panel_request_access(
            data[0], &data[3], length - INKTAG_PROTOCOL_V2_HEADER_SIZE);
        if (access != INKTAG_STATUS_OK) {
            send_v2_response(data[0], data[1], access, NULL, 0u);
            return;
        }
    }
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_5_pro.inc"
#endif
    /* A scoped simulation cancel never touches physical RF or an unrelated
     * display. Only the matching synthetic synchronous pass uses this fast
     * ingress stop path; replies wait for cleanup and queue drain. */
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_cancel_pro.inc"
#endif
    if (length == INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        data[0] == INKTAG_CMD_CANCEL && flags == 0u) {
        s_boot_restore_pending = false;
        s_boot_restore_due = false;
        s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
    }
    if (length == INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        data[0] == INKTAG_CMD_CANCEL && data[1] != 0u && flags == 0u &&
        ((s_display_active && s_display_transaction != 0u) ||
         (!s_display_active && s_native_page_active))) {
        /* Match the GR/OM callback boundary: CANCEL is the sole command that
         * bypasses deferred RX while panel work is active.  This callback
         * only latches state; cleanup and protocol replies happen after the
         * display routine returns to the application process. */
        if (s_cancel_transaction == 0u) s_cancel_transaction = data[1];
        s_slideshow = false;
        s_native_refresh_paused = true;
        s_native_refresh_pending = false;
        inktag_epd_stop();
        return;
    }
    display_owner_command = length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
                            v2_command_requires_idle_display(data[0]);
    if ((s_display_active || s_native_page_active) && display_owner_command) {
        /* A slow e-paper refresh must not turn later user-visible commands
         * into an implicit FIFO. Match the GR/OM ingress contract and tell
         * the client to retry after the active display transaction. */
        send_v2_response(data[0], data[1], INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    next = (uint8_t)((s_head + 1u) % RX_QUEUE_SLOTS);
    if (next == s_tail) {
        if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
            send_v2_response(data[0], data[1], INKTAG_STATUS_BUSY, NULL, 0u);
        else
            send_text("command:queue-full");
        return;
    }
    if (display_owner_command) {
        /* HELLO/reads and refused/full-queue requests do not replace pending
         * boot work; admitted foreground ownership does. */
        s_boot_restore_pending = false;
        s_boot_restore_due = false;
        s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
    }
    s_queue[s_head].length = length;
    memcpy(s_queue[s_head].data, data, length);
    s_head = next;
}

static void sample_connection_temperature_if_idle(void) {
    if (s_connection_temperature_state != 1u || !s_notify ||
        panel_transaction_active() || s_nfc_action_pending ||
        inktag_nfc_service_pending() || inktag_nfc_busy()) return;
    s_connection_temperature_state = 2u;
    /* Synchronous foreground work never returns to main-loop sleep until
     * cleanup. Every cooperative panel yield also disables suspend before
     * invoking the SDK, so no duplicate requires_awake veto is necessary. */
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    if (inktag_tlsr_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true))
        (void)inktag_epd_sample_temperature();
    /* Cooperative SDK service may disconnect/reconnect. Never discard the
     * new link's queued sample or spin on a failed sensor in the old link. */
    if (s_connection_temperature_state == 2u)
        s_connection_temperature_state = 0u;
}

void inktag_app_process(void) {
    inktag_settings_t *settings = inktag_settings();
    inktag_app_status_service();
    v2_find_owner_service();
    (void)refresh_findmy_key_if_safe();
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_app_aux_hook_7_pro.inc"
#endif
#endif
    inktag_activation_expire(&s_activation, s_nfc_now_ms);
    if (s_system_reboot_pending &&
        !status_before(s_nfc_now_ms, s_system_reboot_at) &&
        inktag_ble_notifications_drained()) {
        /* Flash-backed lifecycle preparation can outlast the nominal delay.
         * main_loop() services application work before the BLE notification
         * queue, so the deadline alone could reboot before the authoritative
         * V2 reply was offered even once. Queue acceptance also precedes the
         * connection event that sends the packet, so retain one additional
         * 250 ms guard after the queue first drains. */
        if (!s_system_reboot_reply_drained) {
            s_system_reboot_reply_drained = true;
            s_system_reboot_at = s_nfc_now_ms + 250u;
        } else {
            s_system_reboot_pending = false;
            s_system_reboot_reply_drained = false;
            start_reboot();
            return;
        }
    }
    if (!s_startup_role_ready && !s_recovery_confirmation_due &&
        inktag_ble_advertising_started()) {
        /* Queue the fixed application-ready tail behind RGB startup.  A
         * trial image reaches this point only after its Recovery metadata is
         * durably confirmed below. */
        inktag_status_role_ready(&s_status, INKTAG_STATUS_READY_APPLICATION,
                                 s_nfc_now_ms);
        s_startup_role_ready = true;
    }
    if (s_startup_role_ready &&
        s_status.transient_scene == STATUS_NO_SCENE)
        s_startup_sequence_complete = true;
    if (s_recovery_confirmation_due && inktag_ble_advertising_started() &&
        s_status.transient_scene != INKTAG_STATUS_SCENE_STARTUP &&
        !status_before(s_nfc_now_ms, s_recovery_confirmation_retry_at)) {
        if (inktag_tlsr_recovery_confirm()) {
            s_recovery_confirmation_due = false;
            inktag_status_role_ready(&s_status,
                                     INKTAG_STATUS_READY_APPLICATION,
                                     s_nfc_now_ms);
            s_startup_role_ready = true;
        } else {
            s_recovery_confirmation_retry_at = s_nfc_now_ms + 1000u;
            status_trigger(INKTAG_STATUS_SCENE_ERROR);
        }
    }
    /* The connection callback only queues this. Publish temperature after
     * sleep/parking, before the first ordinary status reply, not from a
     * recursive SDK callback or a GET_STATUS handler. */
    sample_connection_temperature_if_idle();
    /* GR and OM dequeue one foreground BLE request before background input and
     * display work, then return so its reply reaches the controller promptly.
     * Preserve NFC frame timing only when PC4 or its ISR has real contact work
     * pending; an idle fitted FM11 must not tax every image fragment. */
    if (s_tail != s_head && !inktag_nfc_service_pending()) {
        rx_packet_t packet = s_queue[s_tail];
        s_tail = (uint8_t)((s_tail + 1u) % RX_QUEUE_SLOTS);
        process_packet(packet.data, packet.length);
        return;
    }
    /* Match the GR/OM startup contract: background peripherals may be queued,
     * but they must not monopolize the cooperative loop or preempt the
     * product-visible RGB startup transient. The FM11 contact-side setup can
     * read the complete saved NDEF file, so start it only after STARTUP has
     * left the shared status machine. */
#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_start_pro.inc"
#endif
    v2_input_simulation_service();
#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_field_pro.inc"
#endif
    if (s_tail != s_head) {
        rx_packet_t packet = s_queue[s_tail];
        s_tail = (uint8_t)((s_tail + 1u) % RX_QUEUE_SLOTS);
        process_packet(packet.data, packet.length);
        return;
    }
    /* Physical RF service and queued BLE requests above remain live. Do not
     * start another synchronous background refresh before this operation's
     * terminal has actually drained; that would delay or hide its completion. */
#if INKTAG_BUILD_PRO
#include "inktag_app_simulation_3_0_pro.inc"
#endif
    /* Revoked E6 authorization consumes pending work, never its saved data or
     * first-display marker. Do not keep a slideshow/timer retry loop alive. */
    if ((s_boot_restore_pending || s_boot_restore_due ||
         s_native_refresh_pending || s_slideshow) &&
        epd_screen_is_e6(inktag_epd_screen()->driver_id,
                         inktag_epd_screen()->panel_recipe) &&
        inktag_epd_control_access(inktag_epd_screen()) != INKTAG_STATUS_OK) {
        s_boot_restore_pending = s_boot_restore_due = false;
        s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
        s_native_refresh_pending = false;
        slideshow_stop(false);
    }
    if (s_boot_restore_pending && inktag_ble_advertising_started() &&
        (!s_boot_restore_wait_startup ||
         s_startup_sequence_complete)) {
        s_boot_restore_pending = false;
        s_boot_restore_due = true;
    }
    if (s_boot_restore_due && s_tail == s_head &&
        !s_connected && !inktag_nfc_busy() && !s_nfc_action_pending &&
        !s_display_active && !s_picture_active && !protocol_transfer_active() &&
        !inktag_image_store_capture_active() && !s_system_reboot_pending) {
        bool restored = false;
        inktag_boot_display_target_t target = s_boot_restore_target;
        automatic_display_decision_t decision = automatic_display_decision();
        s_boot_restore_due = false;
        s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
        if (decision == AUTOMATIC_DISPLAY_SUPPRESS)
            restored = true;
        else if (target == INKTAG_BOOT_DISPLAY_DEVICE_INFO)
            restored = show_gui(MODE_DEVICE_INFO,
                                EPD_DISPLAY_STATE_DEVICE_INFO,
                                "device-info:done");
        else if (settings->display_mode == 1u)
            restored = show_gui(MODE_CALENDAR, 1u, "calendar:done");
        else if (settings->display_mode == 2u)
            restored = show_gui(MODE_CLOCK, 2u, "clock:done");
        else if (settings->display_mode == 3u) {
            status_active(INKTAG_STATUS_SCENE_REFRESH, true);
            restored = clear_with_diag(settings->display_argument <= 2u ?
                                       settings->display_argument : 1u);
            status_active(INKTAG_STATUS_SCENE_REFRESH, false);
            status_trigger(restored ? INKTAG_STATUS_SCENE_SUCCESS :
                                      INKTAG_STATUS_SCENE_ERROR);
        } else if (settings->display_mode == 4u) {
            s_slideshow = true;
            s_slideshow_id = settings->display_argument;
            slideshow_arm_next_after_refresh();
            restored = true;
        } else if (settings->display_mode == 5u)
            restored = show_gui(MODE_DEVICE_INFO, 5u, "device-info:done");
        else if (settings->display_mode == 0u && settings->display_argument != 0xFFu) {
            status_active(INKTAG_STATUS_SCENE_REFRESH, true);
            restored = show_stored_with_diag(settings->display_argument);
            status_active(INKTAG_STATUS_SCENE_REFRESH, false);
            status_trigger(restored ? INKTAG_STATUS_SCENE_SUCCESS :
                                      INKTAG_STATUS_SCENE_ERROR);
        } else {
            restored = true;
        }
        (void)restored; /* Existing paths already report terminal/error. */
        update_timestamp_from_clock();
        s_native_refresh_pending = false;
    }
#if INKTAG_BUILD_PRO
#include "inktag_app_nfc_dispatch_pro.inc"
#endif
#if INKTAG_TLSR_NFC_FMSC_PILOT && INKTAG_BUILD_PRO
#include "inktag_app_nfc_hook_6_pro.inc"
#endif
    if (s_native_refresh_pending &&
        (!automatic_display_refresh_allowed() ||
         (settings->display_mode != EPD_DISPLAY_STATE_CALENDAR &&
          settings->display_mode != EPD_DISPLAY_STATE_CLOCK))) {
        s_native_refresh_pending = false;
    }
    if (s_native_refresh_pending && s_tail == s_head &&
        !s_boot_restore_due && !s_nfc_action_pending &&
        !inktag_nfc_busy() &&
        !s_display_active && !s_picture_active &&
        !protocol_transfer_active() &&
        !inktag_image_store_capture_active() &&
        !s_system_reboot_pending) {
        /* An idle GATT connection is not panel ownership.  In particular,
         * Windows may retain it well past midnight; EPD waits service BLE, so
         * execute the latched redraw once the command/transfer queues are idle. */
        uint8_t display_mode = settings->display_mode;
        s_native_refresh_pending = false;
        (void)show_automatic_native_page(display_mode);
    }
    if (s_slideshow && (int32_t)(settings->timestamp - s_next_slide) >= 0 &&
        s_tail == s_head && !s_boot_restore_due && !s_nfc_action_pending &&
        !inktag_nfc_busy() && !s_display_active && !s_picture_active &&
        !protocol_transfer_active() && !inktag_image_store_capture_active() &&
        !s_system_reboot_pending) {
        automatic_display_decision_t decision = automatic_display_decision();
        if (decision == AUTOMATIC_DISPLAY_SUPPRESS) {
            slideshow_arm_next_after_refresh();
        } else {
            (void)slideshow_advance();
        }
    }
    service_time_checkpoint();
}
