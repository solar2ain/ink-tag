static void epd_v2_reply_status(ble_epd_t*,uint8_t,uint8_t,inktag_status_v2_t);
#include "inktag_nrf_pins.h"
/* Nordic nRF52 0.2.0 protocol adapter.
 *
 * This file is included by inktag_epd_service.c so it can reuse the established
 * Nordic display scheduler and settings/image-store state. Wire validation and
 * transfer arithmetic live in compat/common/protocol; SoftDevice, Flash and
 * exact-board decisions stay here and in inktag_board.h. */

#include "inktag_nrf_config_store.h"
#include "inktag_release_v1.h"
#include "inktag_display_settings.h"
#include "inktag_gui_assets.h"
#include "inktag_packbits.h"
#include "nrf_error.h"
#include "inktag_nrf_unsupported_status.h"
#include "inktag_temperature_wire.h"
/* Full tools require this marker exactly once in a validated App; their
 * package-policy checks determine whether publisher authentication applies. */
static const char nrf11_factory_pin_contract[]
    __attribute__((used,retain,section(".rodata.inktag_pin_contract"))) =
    "N11PINCFG1";
#ifndef INKTAG_BOARD_HEALTH_SUPPORTED
#define INKTAG_BOARD_HEALTH_SUPPORTED INKTAG_BOARD_HEALTH_QUALIFIED
#endif
#ifndef INKTAG_BOARD_TELEMETRY_SUPPORTED
#define INKTAG_BOARD_TELEMETRY_SUPPORTED INKTAG_BOARD_TELEMETRY_QUALIFIED
#endif
#ifndef INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED
#define INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED 0
#endif
#if INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED &&  (!INKTAG_NRF_FIXED_IMAGE_SLOTS || INKTAG_NRF_ACTIVATION_ENABLED || INKTAG_NRF_FIND_ENABLED)
#error "This lifecycle backend is for permanently-InkTag fixed-slot targets only"
#endif
#define INKTAG_NRF_CMD_SET_IMAGE_METADATA 0x1Eu
/* Retained wire bit identifies the 35-byte upload-name contract for existing
 * clients; it is independent of the retired 0x1E mutation handler. */
#define INKTAG_NRF_CAPABILITY_IMAGE_METADATA UINT32_C(0x10000000)

#define INKTAG_NRF_V2_OPERATION_BLE 1u
#define INKTAG_NRF_V2_TRANSFER_LEASE_TICKS APP_TIMER_TICKS(30000u)

_Static_assert(INKTAG_GUI_RENDER_BUFFER_SIZE >= sizeof(epd_device_info_qr_t),
               "GUI render buffer must hold one staged device-info QR");

static epd_device_info_qr_t* epd_v2_qr_staging(void) {
    return (epd_device_info_qr_t*)(void*)m_gui_render_buffer;
}
#define INKTAG_NRF_V2_SYSTEM_ACTION_DELAY_TICKS APP_TIMER_TICKS(250u)
#define INKTAG_NRF_CURRENT_DISPLAY_SETTINGS_VERSION \
    INKTAG_DISPLAY_SETTINGS_WIRE_VERSION
#if !INKTAG_NRF_IMAGE_STORE_ENABLED
#define epd_slideshow_stop(p_epd, notify) \
    do { (void)(p_epd); (void)(notify); } while (0)
#endif

typedef struct {
    bool active;
    inktag_transfer_descriptor_t descriptor;
    uint8_t transaction_id;
    uint8_t resource_id;
    uint32_t written;
    uint32_t running_crc;
    uint32_t last_activity_ticks;
} inktag_nrf_v2_transfer_t;

static inktag_nrf_v2_transfer_t m_v2_transfer;

#if INKTAG_NRF_IMAGE_STORE_ENABLED
typedef enum {
    INKTAG_NRF_V2_STORE_NONE = 0,
    INKTAG_NRF_V2_STORE_BEGIN,
    INKTAG_NRF_V2_STORE_COMMIT,
    INKTAG_NRF_V2_STORE_DELETE,
    INKTAG_NRF_V2_STORE_CLEAR,
} inktag_nrf_v2_store_wait_t;

typedef struct {
    uint8_t type;
    uint8_t opcode;
    uint8_t transaction_id;
} inktag_nrf_v2_store_pending_t;

static inktag_nrf_v2_store_pending_t m_v2_store_pending;
#define EPD_V2_STORE_BUSY() \
    (m_v2_store_pending.type != INKTAG_NRF_V2_STORE_NONE)
#define EPD_V2_STORE_RESET() \
    memset(&m_v2_store_pending, 0, sizeof(m_v2_store_pending))
#else
#define EPD_V2_STORE_BUSY() false
#define EPD_V2_STORE_RESET() do { } while (0)
#endif
static int16_t m_v2_timezone_minutes;
static inktag_display_settings_t m_v2_display_settings;
static inktag_display_settings_t m_v2_pending_display_settings;
static gui_countdown_t m_v2_countdown_state;
static uint8_t m_v2_rx_packet[BLE_EPD_MAX_DATA_LEN];
static uint16_t m_v2_rx_length;
static bool m_v2_rx_pending;
static bool m_v2_rx_processing;
static ble_epd_t* m_v2_display_read_service;
/* base-only targets still use the common access/quota policy.  A NULL
 * activation is the fail-closed representation of permanently-InkTag. */
APP_TIMER_DEF(m_v2_system_action_timer_id);
static volatile uint8_t m_v2_system_action_pending;
#if INKTAG_NRF_INDICATOR_ENABLED
static inktag_status_runtime_t m_v2_indicator_runtime;
#define m_v2_indicator_config m_v2_indicator_runtime.config
#endif

static void epd_v2_system_action_timer_handler(void* p_context) {
    m_v2_system_action_pending = (uint8_t)(uintptr_t)p_context;
}

static uint32_t epd_v2_system_action_timer_init(void) {
    return app_timer_create(&m_v2_system_action_timer_id,
                            APP_TIMER_MODE_SINGLE_SHOT,
                            epd_v2_system_action_timer_handler);
}

static bool epd_v2_schedule_system_action(uint8_t action) {
    return app_timer_start(m_v2_system_action_timer_id,
                           INKTAG_NRF_V2_SYSTEM_ACTION_DELAY_TICKS,
                           (void*)(uintptr_t)action) == NRF_SUCCESS;
}

static void epd_v2_process_system_action(void) {
    uint8_t action = m_v2_system_action_pending;
    if (action == 0u) return;
    m_v2_system_action_pending = 0u;
    if (action == INKTAG_SYSTEM_ACTION_RESET) {
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
    }
}


static bool epd_v2_countdown_name_supported(
    const inktag_display_settings_t* settings) {
    bool supported;
    if (settings == NULL) return false;
    if (settings->name_length == 0u) return true;
    if (!inktag_gui_assets_begin()) return false;
    supported = GUI_CountdownNameSupported(settings->name,
                                           settings->name_length);
    inktag_gui_assets_end();
    return supported;
}

static void epd_v2_display_settings_sync_runtime(ble_epd_t* p_epd) {
    if (p_epd != NULL) p_epd->config.week_start = m_v2_display_settings.week_start;
    m_v2_countdown_state.name = m_v2_display_settings.name;
    m_v2_countdown_state.target_day = m_v2_display_settings.target_day;
}

static void epd_v2_display_settings_init(ble_epd_t* p_epd) {
    if (!epd_display_settings_read(&m_v2_display_settings)) {
        memset(&m_v2_display_settings, 0, sizeof(m_v2_display_settings));
        m_v2_display_settings.week_start =
            INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START;
        m_v2_display_settings.flags = (uint8_t)(m_display_state.reserved &
            (EPD_DISPLAY_STATE_USER_FLAGS_MASK |
             EPD_DISPLAY_STATE_PRO_FLAGS_MASK));
    }
    epd_v2_display_settings_sync_runtime(p_epd);
}

static const gui_countdown_t* epd_v2_countdown(void) {
    return m_v2_display_settings.target_day != 0u ?
           &m_v2_countdown_state : NULL;
}

/* Display settings v3 is one shared wire contract on both Nordic targets.
 * Pro-capable targets reject an unauthorized hide, while permanent-InkTag
 * targets accept the calendar fields but reject the Pro-only hide bit. */
static inktag_status_v2_t epd_v2_display_settings_write_access(
    const uint8_t* payload, uint16_t length) {
    if (length < 2u || payload[0] != INKTAG_SETTINGS_SECTION_DISPLAY)
        return INKTAG_STATUS_OK;
    if (payload[1] != INKTAG_NRF_CURRENT_DISPLAY_SETTINGS_VERSION)
        return INKTAG_STATUS_UNSUPPORTED;
    if (!inktag_display_settings_decode(
            payload, length, &m_v2_pending_display_settings) ||
        !epd_v2_countdown_name_supported(&m_v2_pending_display_settings))
        return INKTAG_STATUS_INVALID_REQUEST;
    return (m_v2_pending_display_settings.flags &
            EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN) != 0u ?
           INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_OK;
}

static uint8_t epd_v2_display_settings_flags(bool include_pro_flags) {
    uint8_t flags = (uint8_t)(m_display_state.reserved &
                              EPD_DISPLAY_STATE_USER_FLAGS_MASK);
    (void)include_pro_flags;
    return flags;
}

static bool epd_v2_persist_display_settings(
    ble_epd_t* p_epd, const inktag_display_settings_t* settings,
    uint16_t reserved) {
    epd_display_state_t next_state;
    next_state = m_display_state;
    next_state.reserved = reserved;
    if (!epd_display_settings_write(&next_state, settings)) return false;
    m_display_state = next_state;
    m_v2_display_settings = *settings;
    epd_v2_display_settings_sync_runtime(p_epd);
    return true;
}


static inktag_status_v2_t epd_v2_input_action_access(uint8_t source,
                                                     uint8_t action) {
    if (INKTAG_BOARD_BUTTON_PRESENT == 0)
        return INKTAG_STATUS_UNSUPPORTED;
    return inktag_input_action_allowed((inktag_input_source_t)source, action) ?
               INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_REQUEST;
}

#if INKTAG_NRF_INDICATOR_ENABLED
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
#include "inktag_nrf_led.h"
static bool m_v2_led_test_active;
static uint32_t m_v2_led_test_deadline;
APP_TIMER_DEF(m_v2_indicator_timer);
static volatile bool m_v2_indicator_timer_armed;
static void epd_v2_indicator_wakeup(void *context) {
    (void)context;
    /* app_timer's IRQ wakes main; only main mutates the status/runtime clock. */
    m_v2_indicator_timer_armed = false;
}
#endif
static void epd_v2_indicator_apply(void* context, uint8_t color_mask) {
    (void)context;
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    if (!m_v2_led_test_active) inktag_nrf_led_apply(color_mask);
#else
    if (color_mask != INKTAG_STATUS_COLOR_OFF) EPD_LED_ON();
    else EPD_LED_OFF();
#endif
}

static void epd_v2_indicator_tick(void) {
    uint32_t now = app_monotonic_now_ms();
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    if (m_v2_led_test_active && (int32_t)(now - m_v2_led_test_deadline) >= 0) {
        m_v2_led_test_active = false;
        m_v2_indicator_runtime.last_output = UINT8_MAX;
    }
#endif
    inktag_status_tick(&m_v2_indicator_runtime, now);
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    bool ticking = m_v2_led_test_active ||
        inktag_status_needs_periodic_tick(&m_v2_indicator_runtime, now);
    if (ticking && !m_v2_indicator_timer_armed) {
        m_v2_indicator_timer_armed = true;
        APP_ERROR_CHECK(app_timer_start(m_v2_indicator_timer, APP_TIMER_TICKS(30u), NULL));
    } else if (!ticking && m_v2_indicator_timer_armed) {
        APP_ERROR_CHECK(app_timer_stop(m_v2_indicator_timer));
        m_v2_indicator_timer_armed = false;
    }
#endif
}

#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
static void epd_v2_led_test_cancel(void) {
    if (!m_v2_led_test_active) return;
    m_v2_led_test_active = false;
    inktag_nrf_led_apply(0u);
    m_v2_indicator_runtime.last_output = UINT8_MAX;
    epd_v2_indicator_tick();
}
#endif

static __attribute__((noinline,noclone)) void epd_v2_indicator_set_active(inktag_status_scene_t scene,
                                        bool active) {
    inktag_status_set_active(&m_v2_indicator_runtime, scene, active,
                             app_monotonic_now_ms());
}

static void epd_v2_indicator_trigger(inktag_status_scene_t scene) {
    inktag_status_trigger(&m_v2_indicator_runtime, scene,
                          app_monotonic_now_ms());
}

static void epd_v2_indicator_init(void) {
    uint16_t length = 0u;
    inktag_status_config_t config;
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    APP_ERROR_CHECK(app_timer_create(&m_v2_indicator_timer,
        APP_TIMER_MODE_SINGLE_SHOT, epd_v2_indicator_wakeup));
#endif
    inktag_status_config_default(&config);
    if (!inktag_nrf_config_store_read(
            INKTAG_NRF_CONFIG_INDICATOR, &config,
            sizeof(config), &length) ||
        length != sizeof(config) ||
        !inktag_status_config_valid(&config)) {
        inktag_status_config_default(&config);
    }
    EPD_LED_SetEnabled(config.policy !=
                       INKTAG_STATUS_POLICY_OFF);
    inktag_status_init(&m_v2_indicator_runtime, &config,
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
                       INKTAG_STATUS_COLOR_WHITE, epd_v2_indicator_apply,
#else
                       INKTAG_STATUS_COLOR_BLUE, epd_v2_indicator_apply,
#endif
                       NULL, app_monotonic_now_ms());
#if !INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    epd_v2_indicator_trigger(INKTAG_STATUS_SCENE_STARTUP);
#endif
}
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
void inktag_nrf_startup_ready_begin(void) {
    epd_v2_indicator_trigger(INKTAG_STATUS_SCENE_STARTUP);
    inktag_status_role_ready(&m_v2_indicator_runtime,
        INKTAG_STATUS_READY_APPLICATION, app_monotonic_now_ms());
}
bool inktag_nrf_startup_ready_complete(void) {
    return !inktag_status_needs_periodic_tick(&m_v2_indicator_runtime,
        app_monotonic_now_ms());
}
#endif
#endif

/* Two-bit internal entries: unknown=0, omitted=1, runnable=2, idle=3.
 * Bit1 admits a command; only value3 also requires an idle display. IRQ
 * admission and the worker share this immutable InkTag command table. */
static __attribute__((noinline,noclone)) uint8_t epd_v2_command_policy(uint8_t opcode) {
    static const uint8_t policy[]
        __attribute__((section(".rodata.inktag_command_policy"))) = {
        0xa8u,0xaau,0xbfu,0xaau,0xabu,0x0au,
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        0xeau,0x17u,
#else
        0x55u,0x15u,
#endif
        0xe5u, /* GET/SET_INPUT_CONFIG are not InkTag runtime controls. */
#if INKTAG_NRF_INDICATOR_ENABLED
        0x0au,
#else
        0x05u,
#endif
        0x55u,0x00u,0x55u,0x00u,0x00u,0x00u,
#if INKTAG_NRF_ACTIVATION_ENABLED
        0x9au,
#else
        0x55u,
#endif
        0x55u,0x05u,0x00u,
#if INKTAG_NRF_FIND_ENABLED
        0xaau,
#else
        0x55u,
#endif
        0x55u,0x01u,0x00u,0x00u,0x00u,0x00u,0x00u,
#if INKTAG_NRF_TEST_ENABLED
        0x6eu,
#else
        0x55u,
#endif
        0x55u
    };
    typedef char wire_statuses_are_fixed[
        INKTAG_STATUS_OK==0 && INKTAG_STATUS_UNKNOWN_COMMAND==1 &&
        INKTAG_STATUS_UNSUPPORTED==2 ? 1 : -1];
    (void)sizeof(wire_statuses_are_fixed);
    /* Slot zero is unallocated; fold only bytes beyond the frozen table
     * into that same UNKNOWN entry before indexing. */
    if (opcode >= sizeof(policy)*4u) opcode = 0u;
    return (uint8_t)((policy[opcode>>2u] >> ((opcode&3u)*2u)) & 3u);
}

static bool epd_v2_command_requires_idle_display(uint8_t opcode) {
    return epd_v2_command_policy(opcode)==3u;
}

static inktag_status_v2_t epd_v2_command_admission_status(uint8_t opcode) {
    uint8_t policy = epd_v2_command_policy(opcode);
    return policy>=2u ? INKTAG_STATUS_OK : (inktag_status_v2_t)(policy+1u);
}

static bool epd_v2_command_supported_on_target(uint8_t opcode) {
    return epd_v2_command_admission_status(opcode)==INKTAG_STATUS_OK;
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static inktag_status_v2_t epd_v2_store_status(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return INKTAG_STATUS_OK;
        case INKTAG_IMAGE_STORE_UNSUPPORTED: return INKTAG_STATUS_UNSUPPORTED;
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT:
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return INKTAG_STATUS_INVALID_REQUEST;
        case INKTAG_IMAGE_STORE_BUSY: return INKTAG_STATUS_BUSY;
        case INKTAG_IMAGE_STORE_NO_SPACE: return INKTAG_STATUS_NO_SPACE;
        case INKTAG_IMAGE_STORE_LOW_POWER: return INKTAG_STATUS_LOW_POWER;
        case INKTAG_IMAGE_STORE_IO_ERROR: return INKTAG_STATUS_IO_ERROR;
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return INKTAG_STATUS_CHECKSUM_ERROR;
        case INKTAG_IMAGE_STORE_NOT_FOUND: return INKTAG_STATUS_NOT_FOUND;
        default: return INKTAG_STATUS_INTERNAL_ERROR;
    }
}

/* Record IDs are physical sector IDs. Apply the InkTag quota to their stable
 * ascending order so activation changes never renumber or erase records. */
static bool epd_v2_image_record_visible(ble_epd_t* p_epd,
                                        uint8_t wanted_id) {
    inktag_image_store_header_t header;
    (void)p_epd;
    return inktag_image_store_get(wanted_id, &header) == INKTAG_IMAGE_STORE_OK;
}

static inktag_image_store_result_t epd_v2_visible_store_status(
    ble_epd_t* p_epd, const epd_screen_config_t* screen, uint32_t payload_size,
    inktag_image_store_status_t* status) {
    (void)p_epd;
    return inktag_image_store_status(screen, payload_size, status);
}

static inktag_status_v2_t epd_v2_image_create_access(
    const epd_screen_config_t* screen, uint32_t payload_size) {
    inktag_image_store_status_t status;
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, &status);
    if (result != INKTAG_IMAGE_STORE_OK) return epd_v2_store_status(result);
    return status.required_sectors != 0u &&
        status.largest_free_run >= status.required_sectors ?
        INKTAG_STATUS_OK : INKTAG_STATUS_NO_SPACE;
}

static inktag_image_store_result_t epd_v2_clear_visible_resources(
    ble_epd_t* p_epd, bool incompatible_only) {
    return incompatible_only ?
        inktag_image_store_clear_incompatible(&p_epd->screen_config) :
        inktag_image_store_clear_all();
}
#endif

static void epd_v2_send_response(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id,
                                 inktag_status_v2_t status,
                                 const uint8_t* payload,
                                 uint16_t payload_length) {
    uint8_t packet[BLE_EPD_MAX_DATA_LEN];
    if (payload_length > sizeof(packet) - INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload = NULL;
        payload_length = 0u;
    }
    packet[0] = opcode;
    packet[1] = transaction_id;
    packet[2] = (uint8_t)status;
    if (payload != NULL && payload_length != 0u)
        memcpy(&packet[INKTAG_PROTOCOL_V2_HEADER_SIZE], payload, payload_length);
    (void)ble_epd_string_send(
        p_epd, packet,
        (uint16_t)(INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length));
}

/* Share empty replies through the four native argument registers. */
static __attribute__((noinline,noclone)) void epd_v2_reply_status(
    ble_epd_t *epd, uint8_t opcode, uint8_t transaction, inktag_status_v2_t status) {
    /* Nordic is little-endian; only the first three initialized bytes ship. */
    uint32_t packet = opcode | ((uint32_t)transaction << 8) | ((uint32_t)status << 16);
    (void)ble_epd_string_send(epd, (uint8_t*)&packet, INKTAG_PROTOCOL_V2_HEADER_SIZE);
}


static void epd_v2_send_display_settings(ble_epd_t* p_epd, uint8_t opcode,
                                         uint8_t transaction_id) {
    uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                     INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    size_t response_length;
    m_v2_display_settings.flags = epd_v2_display_settings_flags(false);
    response_length = inktag_display_settings_encode(
        &m_v2_display_settings, response, sizeof(response));
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)response_length);
}

static void epd_v2_send_event(ble_epd_t* p_epd, uint8_t event,
                              inktag_status_v2_t status, uint8_t origin_opcode,
                              uint8_t origin_transaction,
                              const uint8_t* payload,
                              uint16_t payload_length) {
    uint8_t packet[BLE_EPD_MAX_DATA_LEN];
    const uint16_t prefix = INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u;
    if (payload_length > sizeof(packet) - prefix)
        payload_length = (uint16_t)(sizeof(packet) - prefix);
    packet[0] = event;
    packet[1] = 0u;
    packet[2] = (uint8_t)status;
    packet[3] = origin_opcode;
    packet[4] = origin_transaction;
    if (payload != NULL && payload_length != 0u)
        memcpy(&packet[prefix], payload, payload_length);
    (void)ble_epd_string_send(p_epd, packet, (uint16_t)(prefix + payload_length));
}

static void epd_v2_send_log(ble_epd_t* p_epd, const char* text) {
    if (text == NULL) return;
    epd_v2_send_event(p_epd, INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                      (const uint8_t*)text, (uint16_t)strlen(text));
}

static uint8_t epd_v2_current_display_mode(void) {
    if (!m_display_state_loaded) return INKTAG_DISPLAY_MODE_DEVICE_INFO;
    static const uint8_t modes[] = {
        INKTAG_DISPLAY_MODE_CALENDAR, INKTAG_DISPLAY_MODE_CLOCK,
        INKTAG_DISPLAY_MODE_CLEAR, INKTAG_DISPLAY_MODE_SLIDESHOW,
        INKTAG_DISPLAY_MODE_DEVICE_INFO,
    };
    /* Picture and every unknown persisted value retain the picture fallback. */
    uint8_t index = (uint8_t)(m_display_state.mode - EPD_DISPLAY_STATE_CALENDAR);
    if (index < sizeof(modes)) return modes[index];
    return m_display_state.argument == EPD_DISPLAY_STATE_NO_SOURCE ?
               INKTAG_DISPLAY_MODE_DIRECT_IMAGE : INKTAG_DISPLAY_MODE_RESOURCE;
}

/* Every terminal path releases both the protocol lease and its LED scene.
 * Clearing only the descriptor leaves a blue transfer scene after disconnect,
 * because link_closed cannot see an already-cleared active flag. */
static void epd_v2_transfer_release(void) {
#if INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER, false);
#endif
    memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
}

static void epd_v2_transfer_reset(ble_epd_t* p_epd, bool erase_store) {
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_v2_transfer.active &&
        m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_STORED_IMAGE && erase_store &&
        inktag_image_store_capture_active()) {
        inktag_image_store_abort();
    }
#else
    (void)erase_store;
#endif
#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
    /* COMMIT releases the transfer descriptor before its queued refresh runs.
     * A cancelled queued frame still owns the physical display reference. */
    if (m_picture_io_active && !m_epd_operation_active) {
#else
    if (m_v2_transfer.active &&
        m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
#endif
        if (p_epd != NULL && p_epd->epd != NULL && EPD_BoardIsPrepared()) {
            EPD_Reset(true, 10u);
            p_epd->epd->drv->sleep(p_epd->epd);
        }
        EPD_BoardRelease();
        EPD_GPIO_Uninit();
    }
    epd_v2_transfer_release();
    m_picture_io_active = false;
}

static void epd_v2_link_closed(ble_epd_t* p_epd) {
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    epd_v2_led_test_cancel();
#endif
    m_v2_rx_pending = false;
    m_v2_display_read_service = NULL;
    if (m_v2_transfer.active) epd_v2_transfer_reset(p_epd, true);
    EPD_V2_STORE_RESET();
}

static void epd_v2_operation_complete(const nrf_epd_operation_t* operation,
                                      inktag_status_v2_t status) {
    uint8_t payload[1];
    if (operation == NULL || operation->origin != INKTAG_NRF_V2_OPERATION_BLE ||
        operation->v2_transaction == 0u) return;
    payload[0] = operation->arg;
    epd_v2_send_event(operation->p_epd, INKTAG_EVENT_COMPLETE, status,
                      operation->v2_opcode, operation->v2_transaction,
                      payload, sizeof(payload));
}

static void epd_v2_own_operation(uint8_t opcode, uint8_t transaction_id) {
    const uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY, 0u};
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
    epd_v2_led_test_cancel();
#endif
    m_epd_pending_operation.origin = INKTAG_NRF_V2_OPERATION_BLE;
    m_epd_pending_operation.v2_opcode = opcode;
    m_epd_pending_operation.v2_transaction = transaction_id;
    epd_v2_send_event(m_epd_pending_operation.p_epd, INKTAG_EVENT_PROGRESS,
        INKTAG_STATUS_OK, opcode, transaction_id, progress, sizeof(progress));
}

static bool epd_v2_schedule_operation(ble_epd_t* p_epd, uint8_t opcode,
                                      uint8_t transaction_id,
                                      nrf_epd_operation_type_t type,
                                      uint8_t arg,
                                      uint32_t operation_timestamp) {
    if (m_epd_operation_active || m_epd_operation_pending ||
        (m_picture_io_active && type != NRF_EPD_OP_REFRESH)) return false;
    if (!epd_schedule_operation(p_epd, type, arg, operation_timestamp)) return false;
    if (!m_epd_dispatch_scheduled) {
        m_epd_operation_pending = false;
        return false;
    }
    epd_v2_own_operation(opcode, transaction_id);
    return true;
}

static uint32_t epd_v2_capabilities(void) {
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_PIN_CONFIG |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE |
                            INKTAG_CAPABILITY_DEVICE_INFO_QR |
                            INKTAG_CAPABILITY_DEVICE_LABEL |
                            INKTAG_CAPABILITY_OTA_RECOVERY;
#if INKTAG_NRF_TEST_ENABLED
    capabilities |= INKTAG_CAPABILITY_TEST;
#endif
    const inktag_capability_gate_t telemetry = {
        true, true, INKTAG_BOARD_TELEMETRY_SUPPORTED != 0, true,
    };
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    const inktag_capability_gate_t resources = {
        true, true, INKTAG_BOARD_IMAGE_STORE_QUALIFIED != 0,
        inktag_image_store_supported(),
    };
#endif
    const inktag_capability_gate_t lifecycle = {
        true, true, INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED != 0, true,
    };
#if INKTAG_NRF_INDICATOR_ENABLED
    const inktag_capability_gate_t indicator = {
        true, true, INKTAG_BOARD_HAS_STATUS_LED != 0, true,
    };
#endif
    const inktag_capability_gate_t health = {
        true, true, INKTAG_BOARD_HEALTH_SUPPORTED != 0, true,
    };
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_TELEMETRY, &telemetry);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_RESOURCES, &resources);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_SLIDESHOW, &resources);
#endif
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE, &lifecycle);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE_V2, &lifecycle);
#if INKTAG_NRF_INDICATOR_ENABLED
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INDICATOR, &indicator);
#endif
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_HEALTH, &health);
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
    if (inktag_image_store_supported()) capabilities |= INKTAG_NRF_CAPABILITY_IMAGE_METADATA;
#endif
    return capabilities;
}

static void epd_v2_send_hello(ble_epd_t* p_epd, uint8_t opcode,
                              uint8_t transaction_id) {
    uint8_t payload[6] = {1u, INKTAG_PROTOCOL_V2_VERSION, 0u, 0u, 0u, 0u};
    inktag_write_u16_le(&payload[2], p_epd->max_data_len);
    inktag_write_u16_le(&payload[4], (uint16_t)(p_epd->max_data_len - 7u));
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
}

static void epd_v2_send_capabilities(ble_epd_t* p_epd, uint8_t opcode,
                                     uint8_t transaction_id) {
    uint8_t payload[INKTAG_CAPABILITIES_EXTENDED_SIZE] = {0};
    uint16_t modes = 0u;
    uint16_t actions = 0u;
    uint8_t mode;
    payload[0] = 1u;
    inktag_write_u32_le(&payload[1], epd_v2_capabilities());
    for (mode = INKTAG_DISPLAY_MODE_DEVICE_INFO;
         mode <= INKTAG_DISPLAY_MODE_DIRECT_IMAGE; mode++)
        modes |= (uint16_t)(1u << mode);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (inktag_image_store_supported()) {
        modes |= (uint16_t)((1u << INKTAG_DISPLAY_MODE_RESOURCE) |
                            (1u << INKTAG_DISPLAY_MODE_SLIDESHOW));
        actions |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_NEXT) |
                              (1u << INKTAG_DISPLAY_ACTION_PAUSE) |
                              (1u << INKTAG_DISPLAY_ACTION_RESUME));
    }
#endif
    actions |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_REFRESH) |
                          (1u << INKTAG_DISPLAY_ACTION_SLEEP));
    inktag_write_u16_le(&payload[5], modes);
    inktag_write_u16_le(&payload[7], actions);
    inktag_write_u16_le(&payload[9], p_epd->max_data_len);
    inktag_write_u16_le(&payload[11], (uint16_t)(p_epd->max_data_len - 7u));
    payload[13] = INKTAG_NRF_DEVICE_NAME_WRITABLE ?
        EPD_DEVICE_NAME_MAX_BYTES : 0u;
    payload[14] = EPD_DEVICE_LABEL_MAX_BYTES;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    inktag_write_u16_le(&payload[15], inktag_image_store_sector_count());
#endif
    inktag_write_u32_le(&payload[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
        INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION);
    uint16_t response_length = sizeof(payload) + INKTAG_PROTOCOL_V2_HEADER_SIZE <=
        p_epd->max_data_len ? sizeof(payload) : 17u;
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, response_length);
}

static uint8_t epd_v2_runtime_name(uint8_t name[EPD_DEVICE_NAME_MAX_BYTES]) {
    uint16_t length = EPD_DEVICE_NAME_MAX_BYTES;
    if (sd_ble_gap_device_name_get(name, &length) != NRF_SUCCESS) return 0u;
    return length > EPD_DEVICE_NAME_MAX_BYTES ? EPD_DEVICE_NAME_MAX_BYTES :
                                               (uint8_t)length;
}

static void epd_v2_application_info(uint8_t output[8]) {
    /* Recovery validates Bank 0 before executing App. Report only the final
     * committed bank, after the candidate's boot/epoch confirmation reset;
     * neither a pending install nor invalid metadata attests OTA success. */
    const uint32_t* settings =
        (const uint32_t*)(uintptr_t)INKTAG_BOARD_DFU_SETTINGS_START;
    if (settings[1] == 2u && settings[3] == 0u && settings[8] == 1u &&
        settings[6] != 0u && settings[6] <= 0x11000u) {
        /* Both Nordic CPUs and the persisted SDK fields are little endian. */
        memcpy(output, &settings[6], 8u);
    }
}

static void epd_v2_send_device_info(ble_epd_t* p_epd, uint8_t opcode,
                                    uint8_t transaction_id) {
    uint8_t payload[96] = {0};
    uint8_t name[EPD_DEVICE_NAME_MAX_BYTES];
    uint8_t name_length = epd_v2_runtime_name(name);
    uint8_t board_length = (uint8_t)strlen(INKTAG_BOARD_ID);
    ble_gap_addr_t address;
    bool address_valid = sd_ble_gap_addr_get(&address) == NRF_SUCCESS;
    uint16_t offset;
    payload[0] = 1u;
    payload[1] = board_length;
    payload[2] = name_length;
    payload[3] = m_device_settings.label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        uint8_t i;
        for (i = 0u; i < 6u; i++) payload[5u + i] = address.addr[5u - i];
    }
    inktag_write_u32_le(&payload[11],
        ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
        ((uint32_t)INKTAG_VERSION_MINOR << 8) | INKTAG_VERSION_PATCH);
    epd_v2_application_info(&payload[15]);
    offset = 23u;
    memcpy(&payload[offset], INKTAG_BOARD_ID, board_length);
    offset = (uint16_t)(offset + board_length);
    memcpy(&payload[offset], name, name_length);
    offset = (uint16_t)(offset + name_length);
    memcpy(&payload[offset], m_device_settings.label,
           m_device_settings.label_length);
    offset = (uint16_t)(offset + m_device_settings.label_length);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, offset);
}

static void epd_v2_send_status(ble_epd_t* p_epd, uint8_t opcode,
                               uint8_t transaction_id) {
    uint8_t payload[16 + INKTAG_TEMPERATURE_WIRE_SIZE];
    uint8_t flags = 0x04u;
    int8_t temperature = -127;
    uint8_t progress = 0u;
    uint8_t active_opcode = 0u;
    uint8_t active_transaction = 0u;
    if (m_epd_operation_active || m_epd_operation_pending) flags |= 0x01u;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_slideshow_active) flags |= 0x02u;
#endif
    if (m_v2_transfer.active) flags |= 0x10u;
    if (m_v2_transfer.active && m_v2_transfer.descriptor.total_size != 0u) {
        progress = (uint8_t)((m_v2_transfer.written * 100u) /
                             m_v2_transfer.descriptor.total_size);
        active_opcode = INKTAG_CMD_TRANSFER_WRITE;
        active_transaction = m_v2_transfer.transaction_id;
    } else if (m_epd_operation_active &&
               m_epd_active_operation.origin == INKTAG_NRF_V2_OPERATION_BLE) {
        active_opcode = m_epd_active_operation.v2_opcode;
        active_transaction = m_epd_active_operation.v2_transaction;
    } else if (m_epd_operation_pending &&
               m_epd_pending_operation.origin == INKTAG_NRF_V2_OPERATION_BLE) {
        active_opcode = m_epd_pending_operation.v2_opcode;
        active_transaction = m_epd_pending_operation.v2_transaction;
    }
    inktag_nrf_health_temperature_sample();
    if (inktag_nrf_health_effective_temperature(&temperature)) flags |= 0x08u;
    payload[0] = 1u;
    payload[1] = epd_v2_current_display_mode();
    payload[2] = flags;
    payload[3] = progress;
    payload[4] = active_opcode;
    payload[5] = active_transaction;
    payload[6] = UINT8_MAX;
    payload[7] = (uint8_t)temperature;
    inktag_write_u16_le(&payload[8], EPD_ReadVoltage());
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    payload[6] = m_slideshow_current_id;
    inktag_write_u32_le(&payload[10], m_slideshow_interval);
#else
    inktag_write_u32_le(&payload[10], 0u);
#endif
    inktag_write_u16_le(&payload[14], (uint16_t)m_v2_timezone_minutes);
    int16_t mcu = 0, panel = 0;
    bool mcu_valid = inktag_nrf_health_mcu_temperature(&mcu);
    bool panel_valid = inktag_nrf_health_panel_temperature(&panel);
    inktag_temperature_wire_encode(&payload[16], mcu_valid, mcu,
                                   panel_valid, panel);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
}

static void epd_v2_send_time(ble_epd_t* p_epd, uint8_t opcode,
                             uint8_t transaction_id) {
    uint8_t payload[7];
    uint32_t local = timestamp();
    uint32_t utc = (uint32_t)((int64_t)local -
                              (int32_t)m_v2_timezone_minutes * 60);
    inktag_write_u32_le(payload, utc);
    inktag_write_u16_le(&payload[4], (uint16_t)m_v2_timezone_minutes);
    payload[6] = p_epd->config.week_start;
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
}

/* Unsigned wrap tests express the existing uint32 wire bounds without
 * introducing any year range or wide-integer arithmetic into the MCU path. */
static bool epd_v2_time_offset(uint32_t base, int32_t delta, uint32_t *result) {
    uint32_t next = base + (uint32_t)delta;
    if (delta < 0 ? next > base : next < base) return false;
    *result = next;
    return true;
}

/* The external NOR writer never runs app_sched_execute. Reserve its existing
 * display queue entry first; a failed TIME write cancels the pending entry
 * before the main scheduler can dispatch it. No rollback write is required. */
static void epd_v2_time_command(ble_epd_t *p_epd, uint8_t opcode, uint8_t tx,
                                const uint8_t *payload, uint16_t length) {
    bool test = opcode == INKTAG_CMD_RUN_TEST;
    bool shift = false;
    uint8_t mode = 0u, maximum_week = 1u;
    int16_t zone = m_v2_timezone_minutes;
    uint32_t local;
    uint8_t week = p_epd->config.week_start;
    uint8_t error = INKTAG_STATUS_INVALID_REQUEST;
    if (test) {
        if (length < 2u) goto invalid;
        shift = payload[0] == INKTAG_TEST_TIME_SHIFT;
        mode = payload[1];
        if (mode < MODE_CALENDAR || mode > MODE_CLOCK) goto invalid;
        payload += 2u;
        length -= 2u;
        maximum_week = 6u;
    }
    if (shift) {
        if (length != 4u) goto invalid;
        int32_t delta = (int32_t)inktag_read_u32_le(payload);
        if (!delta || delta < -(366 * 86400) || delta > 366 * 86400) goto invalid;
        uint32_t utc;
        if (!epd_v2_time_offset(timestamp(), delta, &local) ||
            !epd_v2_time_offset(local, -(int32_t)zone * 60, &utc)) goto invalid;
    } else {
        if (length != 7u || payload[6] > maximum_week) goto invalid;
        zone = (int16_t)inktag_read_u16_le(payload + 4u);
        if (zone < -840 || zone > 840) goto invalid;
        if (!epd_v2_time_offset(inktag_read_u32_le(payload),
                                (int32_t)zone * 60, &local)) goto invalid;
        week = payload[6];
    }
    if (test) {
        error = INKTAG_STATUS_INVALID_STATE;
        if (p_epd->epd == NULL) goto invalid;
        error = INKTAG_STATUS_BUSY;
        bool reserved = epd_schedule_operation(p_epd, NRF_EPD_OP_GUI, mode, (uint32_t)local);
        if (!reserved || !m_epd_dispatch_scheduled) {
            if (reserved) m_epd_operation_pending = false;
            goto invalid;
        }
    }
    if (!epd_time_state_write((uint32_t)local, zone)) {
        if (test) m_epd_operation_pending = false;
        error = INKTAG_STATUS_PERSISTENCE_ERROR;
        goto invalid;
    }
    m_v2_timezone_minutes = zone;
    set_timestamp((uint32_t)local);
    m_native_timer_update_pending = false;
    if (test) {
        /* Test-only page/week hints do not rewrite the user's ID7 preferences. */
        p_epd->config.display_mode = mode;
        p_epd->config.week_start = week;
        epd_v2_own_operation(opcode, tx);
    }
    epd_v2_send_time(p_epd, opcode, tx);
    return;
invalid:
    epd_v2_reply_status(p_epd, opcode, tx, error);
}

static void epd_v2_send_screen(ble_epd_t* p_epd, uint8_t opcode,
                               uint8_t transaction_id) {
    uint8_t payload[EPD_SCREEN_WIRE_SIZE];
    epd_screen_status_t status = epd_screen_config_encode(
        &p_epd->screen_config, payload);
    epd_v2_send_response(
        p_epd, opcode, transaction_id,
        status == EPD_SCREEN_STATUS_OK ? INKTAG_STATUS_OK :
                                         INKTAG_STATUS_INTERNAL_ERROR,
        status == EPD_SCREEN_STATUS_OK ? payload : NULL,
        status == EPD_SCREEN_STATUS_OK ? sizeof(payload) : 0u);
}

static void epd_v2_send_identity(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id) {
    uint8_t payload[72] = {0};
    uint8_t name[EPD_DEVICE_NAME_MAX_BYTES];
    uint8_t name_length = epd_v2_runtime_name(name);
    ble_gap_addr_t address;
    bool address_valid = sd_ble_gap_addr_get(&address) == NRF_SUCCESS;
    uint16_t offset = 11u;
    payload[0] = INKTAG_SETTINGS_SECTION_IDENTITY;
    payload[1] = EPD_DEVICE_SETTINGS_VERSION;
    payload[2] = name_length;
    payload[3] = m_device_settings.label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        uint8_t i;
        for (i = 0u; i < 6u; i++) payload[5u + i] = address.addr[5u - i];
    }
    memcpy(&payload[offset], name, name_length);
    offset = (uint16_t)(offset + name_length);
    memcpy(&payload[offset], m_device_settings.label,
           m_device_settings.label_length);
    offset = (uint16_t)(offset + m_device_settings.label_length);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, offset);
}



#if INKTAG_NRF_IMAGE_STORE_ENABLED
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
/* UI scale changes do not change stored pixels. TRANSFER_BEGIN still uses
 * the exact profile matcher; only the profile-change clear uses this test. */
static bool epd_v2_screen_images_match(const ble_epd_t *p_epd,
                                      const uint8_t *encoded) {
    uint8_t current[EPD_SCREEN_WIRE_SIZE];
    return epd_screen_config_encode(&p_epd->screen_config, current) == EPD_SCREEN_STATUS_OK &&
           epd_screen_wire_image_compatible(current, encoded);
}
#endif

static void epd_v2_send_resource_info(ble_epd_t* p_epd, uint8_t opcode,
                                      uint8_t transaction_id) {
    epd_screen_t screen;
    inktag_image_store_status_t status;
    inktag_image_store_result_t result;
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
    uint8_t payload[34] = {0};
#else
    uint8_t payload[30] = {0};
#endif
    if (!inktag_image_store_supported() ||
        epd_screen_resolve(&p_epd->screen_config, &screen) != EPD_SCREEN_STATUS_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    result = epd_v2_visible_store_status(
        p_epd, &p_epd->screen_config, screen.frame_bytes, &status);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result));
        return;
    }
    payload[0] = 1u;
    inktag_write_u16_le(&payload[1], screen.config.width);
    inktag_write_u16_le(&payload[3], screen.config.height);
    inktag_write_u32_le(&payload[5], screen.frame_bytes);
    inktag_write_u16_le(&payload[9], status.required_sectors);
    inktag_write_u16_le(&payload[11], status.total_sectors);
    inktag_write_u16_le(&payload[13], status.free_sectors);
    inktag_write_u16_le(&payload[15], status.largest_free_run);
    inktag_write_u16_le(&payload[17], status.saved_count);
    inktag_write_u16_le(&payload[19], status.compatible_count);
    inktag_write_u16_le(&payload[21], status.playable_count);
    inktag_write_u16_le(&payload[23], status.incompatible_count);
    payload[25] = m_slideshow_active ? 1u : 0u;
    inktag_write_u32_le(&payload[26], m_slideshow_interval);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
    inktag_write_u16_le(&payload[30], status.free_sectors / status.required_sectors);
    inktag_write_u16_le(&payload[32], inktag_image_store_sector_count());
#endif
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
}

static void epd_v2_list_resource(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id,
                                 const uint8_t* payload, uint16_t length) {
    uint16_t sector = length == 1u && payload[0] != UINT8_MAX ?
                          (uint16_t)payload[0] + 1u : 0u;
    if (length > 1u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    for (; sector < inktag_image_store_sector_count(); sector++) {
        inktag_image_store_header_t header;
        epd_screen_config_t stored;
        uint8_t response[64] = {0};
        uint8_t name_length = 0u;
        bool decoded;
        inktag_image_store_result_t result = inktag_image_store_get((uint8_t)sector, &header);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result));
            return;
        }
        decoded = epd_screen_config_decode_exact(header.screen_config,
                    EPD_SCREEN_WIRE_SIZE, &stored) == EPD_SCREEN_STATUS_OK;
        while (name_length < INKTAG_IMAGE_STORE_NAME_SIZE &&
               header.name[name_length] != '\0') name_length++;
        response[0] = 0u;
        response[1] = (uint8_t)sector;
        response[2] = header.content_type;
        response[3] = header.format;
        response[4] = header.segment_count;
        response[5] = decoded &&
            epd_screen_config_matches(p_epd, header.screen_config) ? 1u : 0u;
        response[6] = name_length;
        inktag_write_u16_le(&response[7], decoded ? stored.width : 0u);
        inktag_write_u16_le(&response[9], decoded ? stored.height : 0u);
        inktag_write_u32_le(&response[11], header.payload_size);
        inktag_write_u32_le(&response[15], header.created_at);
        memcpy(&response[19], header.name, name_length);
        response[19u + name_length] = inktag_image_reading_rotation(header.black_mask);
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             response, (uint16_t)(20u + name_length));
        return;
    }
    {
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
        uint8_t done[2u + (INKTAG_BOARD_IMAGE_STORE_SIZE / 4096u + 7u) / 8u] = {1u, UINT8_MAX};
        uint16_t done_length = 2u + inktag_image_store_copy_occupancy(done + 2u);
#else
        uint8_t done[2] = {1u, UINT8_MAX};
        uint16_t done_length = sizeof(done);
#endif
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             done, done_length);
    }
}

static void epd_v2_read_resource(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id,
                                 const uint8_t* payload, uint16_t length) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    uint8_t response[BLE_EPD_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    uint32_t offset;
    uint16_t requested;
    if (length != 6u || payload[5] == 0u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result));
        return;
    }
    if (!epd_v2_image_record_visible(p_epd, payload[0])) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_DENIED);
        return;
    }
    offset = inktag_read_u32_le(&payload[1]);
    requested = payload[5];
    if (requested > sizeof(response) - 9u)
        requested = (uint16_t)(sizeof(response) - 9u);
    if (offset >= header.payload_size) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (requested > header.payload_size - offset)
        requested = (uint16_t)(header.payload_size - offset);
    response[0] = payload[0];
    inktag_write_u32_le(&response[1], offset);
    inktag_write_u32_le(&response[5], header.payload_size);
    result = inktag_image_store_read(payload[0], offset, &response[9], requested);
    epd_v2_send_response(p_epd, opcode, transaction_id,
                         epd_v2_store_status(result),
                         result == INKTAG_IMAGE_STORE_OK ? response : NULL,
                         result == INKTAG_IMAGE_STORE_OK ?
                             (uint16_t)(9u + requested) : 0u);
}
#endif

static void epd_v2_transfer_begin(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id,
                                  const uint8_t* payload, uint16_t length) {
    inktag_transfer_descriptor_t descriptor;
    epd_screen_t screen;
    uint8_t expected_segments;
    inktag_status_v2_t wire_status;
    if (m_v2_transfer.active || m_epd_operation_active ||
        m_epd_operation_pending || EPD_V2_STORE_BUSY()) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY);
        return;
    }
    wire_status = inktag_transfer_descriptor_decode(payload, length, &descriptor);
    if (wire_status != INKTAG_STATUS_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             wire_status);
        return;
    }
    /* Bit7 describes BLE encoding only; never persist it as a plane mask. */
    if ((descriptor.black_mask & INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) &&
        descriptor.object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
        descriptor.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        if (length != 20u ||
            !inktag_device_info_qr_transfer_valid(&descriptor)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
    } else {
        if (descriptor.object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            && descriptor.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE
#endif
        ) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        if (epd_screen_resolve(&p_epd->screen_config, &screen) !=
                EPD_SCREEN_STATUS_OK) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE);
            return;
        }
        expected_segments =
            (screen.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
             screen.transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
             screen.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) ?
                2u : 1u;
        if (!inktag_transfer_descriptor_matches(
                &descriptor, screen.transfer_format, expected_segments,
                screen.frame_bytes)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE);
            return;
        }
    }
    if (!inktag_nrf_health_operation_allowed(
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            (descriptor.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
             descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) ?
                INKTAG_POWER_OPERATION_FLASH :
#else
            descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ?
                INKTAG_POWER_OPERATION_FLASH :
#endif
                INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
    memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
    m_v2_transfer.active = true;
#if INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER, true);
#endif
    m_v2_transfer.descriptor = descriptor;
    m_v2_transfer.transaction_id = transaction_id;
    m_v2_transfer.resource_id = UINT8_MAX;
    m_v2_transfer.running_crc = 0xFFFFFFFFu;
    m_v2_transfer.last_activity_ticks = app_timer_cnt_get();
    if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        epd_device_info_qr_t* qr = epd_v2_qr_staging();
        m_v2_transfer.resource_id = 0u;
        qr->modules = descriptor.format;
        qr->version = 1u;
        qr->length = (uint16_t)descriptor.total_size;
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    else if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        epd_screen_config_t stored_screen;
        uint8_t id = UINT8_MAX;
        uint8_t name_length;
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
        uint8_t replace_slot = UINT8_MAX;
#endif
        char name[INKTAG_IMAGE_STORE_NAME_SIZE] = {0};
        inktag_image_store_result_t result;
        uint8_t i;
        if (length >= 38u && inktag_nrf_screen_omitted(&payload[21],
                                                     EPD_SCREEN_WIRE_SIZE)) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        if (length < 39u || payload[38] > INKTAG_IMAGE_STORE_NAME_SIZE - 1u ||
            (length != (uint16_t)(39u + payload[38]) &&
             length != (uint16_t)(40u + payload[38])) ||
            epd_screen_config_decode_exact(&payload[21], EPD_SCREEN_WIRE_SIZE,
                &stored_screen) != EPD_SCREEN_STATUS_OK ||
            !epd_screen_config_matches(p_epd, &payload[21])) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        name_length = payload[38];
        if (length == (uint16_t)(40u + name_length)) {
#if !INKTAG_NRF_SIMPLE_IMAGE_METADATA
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_UNSUPPORTED);
            return;
#else
            replace_slot = payload[39u + name_length];
            if (replace_slot == UINT8_MAX
#if !INKTAG_NRF_SIMPLE_IMAGE_METADATA
                || !epd_v2_image_record_visible(p_epd, replace_slot)
#endif
                ) {
                epd_v2_transfer_reset(p_epd, false);
                epd_v2_reply_status(p_epd, opcode, transaction_id,
                                     INKTAG_STATUS_NOT_FOUND);
                return;
            }
#endif
        }
        for (i = 0u; i < name_length; i++) {
            uint8_t c = payload[39u + i];
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
            name[i] = (char)c;
#else
            name[i] = ((c >= '0' && c <= '9') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= 'a' && c <= 'z') || c == '-' || c == '_') ?
                          (char)c : '_';
#endif
        }
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
        wire_status = replace_slot != UINT8_MAX ? INKTAG_STATUS_OK :
#else
        wire_status =
#endif
            epd_v2_image_create_access(
            &stored_screen, descriptor.total_size);
        if (wire_status != INKTAG_STATUS_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 wire_status);
            return;
        }
        epd_slideshow_stop(p_epd, false);
#if INKTAG_NRF_SIMPLE_IMAGE_METADATA
        if (replace_slot != UINT8_MAX && !inktag_image_store_replace_slot(replace_slot)) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_NOT_FOUND);
            return;
        }
#endif
        result = inktag_image_store_begin(
            &stored_screen, descriptor.format, descriptor.segment_count,
            (uint8_t)(descriptor.black_mask & ~INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE),
            payload[20], descriptor.segment_size,
            descriptor.total_size, descriptor.expected_crc, timestamp(),
            name, &id);
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result));
            return;
        }
        m_v2_transfer.resource_id = id;
        m_v2_store_pending.type = INKTAG_NRF_V2_STORE_BEGIN;
        m_v2_store_pending.opcode = opcode;
        m_v2_store_pending.transaction_id = transaction_id;
        return;
    }
#endif
    else {
        epd_slideshow_stop(p_epd, false);
        m_picture_io_active = true;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        m_pending_picture_store_id = UINT8_MAX;
#endif
        EPD_GPIO_Init();
        EPD_BoardPrepare();
        p_epd->epd = epd_init(&p_epd->screen_config);
        if (p_epd->epd == NULL) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE);
            return;
        }
    }
    {
        uint8_t response[5] = {
            descriptor.object_type, m_v2_transfer.resource_id, 0u, 0u,
            descriptor.segment_count,
        };
        inktag_write_u16_le(&response[2], (uint16_t)(p_epd->max_data_len - 7u));
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
    }
}

typedef struct {
    ble_epd_t *epd;
    inktag_status_v2_t status;
} epd_v2_decode_context_t;

/* CRC/offsets describe decoded bytes. A failure after a partial hardware write
 * aborts the transfer; it must never be retried as an acknowledged block. */
static bool epd_v2_write_decoded(void *context, uint8_t *data,
                                  uint16_t data_length) {
    epd_v2_decode_context_t *decoded = context;
    ble_epd_t *p_epd = decoded->epd;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint32_t next_crc = inktag_crc32_update(m_v2_transfer.running_crc,
                                           data, data_length);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_v2_transfer.descriptor.object_type ==
        INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        status = epd_v2_store_status(
            inktag_image_store_append(data, data_length));
    } else
#endif
    if (m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        memcpy(&epd_v2_qr_staging()->data[m_v2_transfer.written],
               data, data_length);
    } else if (p_epd->epd == NULL) {
        status = INKTAG_STATUS_INVALID_STATE;
    } else {
        uint16_t consumed = 0u;
        while (consumed < data_length) {
            uint32_t absolute = m_v2_transfer.written + consumed;
            uint8_t segment = absolute <
                m_v2_transfer.descriptor.segment_size[0] ? 0u : 1u;
            uint32_t segment_start = segment == 0u ? 0u :
                m_v2_transfer.descriptor.segment_size[0];
            uint32_t segment_end = segment_start +
                m_v2_transfer.descriptor.segment_size[segment];
            uint16_t chunk = (uint16_t)(data_length - consumed);
            bool black;
            if (absolute + chunk > segment_end)
                chunk = (uint16_t)(segment_end - absolute);
            black = (m_v2_transfer.descriptor.black_mask &
                     (1u << segment)) != 0u;
            if (!epd_brand_overlay_chunk(
                    p_epd, m_v2_transfer.descriptor.format, segment,
                    absolute - segment_start, (uint8_t*)&data[consumed],
                    chunk, inktag_image_reading_rotation(
                        m_v2_transfer.descriptor.black_mask))) {
                status = INKTAG_STATUS_INVALID_REQUEST;
                break;
            }
            if (!EPD_DriverWriteRam(
                    p_epd->epd, absolute == segment_start, black,
                    (uint8_t*)&data[consumed], chunk)) {
                status = INKTAG_STATUS_INVALID_STATE;
                break;
            }
            consumed = (uint16_t)(consumed + chunk);
            app_feed_wdt();
        }
    }
    decoded->status = status;
    if (status != INKTAG_STATUS_OK) return false;
    m_v2_transfer.running_crc = next_crc;
    m_v2_transfer.written += data_length;
    return true;
}

static void epd_v2_transfer_write(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id,
                                  const uint8_t* payload, uint16_t length) {
    epd_v2_decode_context_t decoded = {p_epd, INKTAG_STATUS_OK};
    uint32_t offset;
    uint16_t data_length;
    uint16_t prefix;
    bool compressed;
    if (!m_v2_transfer.active || transaction_id != m_v2_transfer.transaction_id) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE);
        return;
    }
    compressed = (m_v2_transfer.descriptor.black_mask &
                   INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u;
    prefix = compressed ? 6u : 4u;
    offset = length >= prefix ? inktag_read_u32_le(payload) : UINT32_MAX;
    data_length = length >= prefix ? (compressed ? inktag_read_u16_le(payload + 4u) :
                                      (uint16_t)(length - prefix)) : 0u;
    if (length <= prefix || offset != m_v2_transfer.written || !data_length ||
        m_v2_transfer.written > m_v2_transfer.descriptor.total_size ||
        data_length > m_v2_transfer.descriptor.total_size - m_v2_transfer.written ||
        (compressed && (length - prefix > UINT8_MAX ||
          !inktag_packbits_validate_block(payload + prefix,
                 (uint8_t)(length - prefix), data_length)))) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_v2_transfer.descriptor.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        !inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        epd_v2_transfer_reset(p_epd, true);
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
#endif
    if (compressed)
        (void)inktag_packbits_stream_validated(payload + prefix,
            (uint8_t)(length - prefix), epd_v2_write_decoded, &decoded);
    else
        (void)epd_v2_write_decoded(&decoded, (uint8_t *)(payload + prefix), data_length);
    if (decoded.status != INKTAG_STATUS_OK) {
        epd_v2_transfer_reset(p_epd, true);
        epd_v2_reply_status(p_epd, opcode, transaction_id, decoded.status);
        return;
    }
    m_v2_transfer.last_activity_ticks = app_timer_cnt_get();
    {
        uint8_t response[4];
        inktag_write_u32_le(response, m_v2_transfer.written);
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
    }
}

static void epd_v2_transfer_commit(ble_epd_t* p_epd, uint8_t opcode,
                                   uint8_t transaction_id) {
    uint8_t object_type;
    if (!m_v2_transfer.active ||
        transaction_id != m_v2_transfer.transaction_id ||
        m_v2_transfer.written != m_v2_transfer.descriptor.total_size) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE);
        return;
    }
    if ((m_v2_transfer.running_crc ^ 0xFFFFFFFFu) !=
        m_v2_transfer.descriptor.expected_crc) {
        epd_v2_transfer_reset(p_epd, true);
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_CHECKSUM_ERROR);
        return;
    }
    object_type = m_v2_transfer.descriptor.object_type;
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        epd_device_info_qr_t* qr = epd_v2_qr_staging();
        qr->state = 0u;
        qr->crc = inktag_crc32_update(UINT32_MAX, qr->data, qr->length) ^ UINT32_MAX;
        const uint8_t *record = (const uint8_t *)(const void *)qr;
        uint16_t qr_record_length = (uint16_t)(9u + qr->length);
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_DEVICE_INFO_QR, record, qr_record_length)) {
            inktag_status_v2_t status =
                inktag_nrf_config_store_last_error() == NRF_ERROR_FORBIDDEN ?
                    INKTAG_STATUS_LOW_POWER : INKTAG_STATUS_PERSISTENCE_ERROR;
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id, status);
            return;
        }
        /* Device-page rendering reads the committed record through its
         * bounded decoder; never cache a partially committed candidate. */
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    else if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            epd_v2_transfer_reset(p_epd, true);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER);
            return;
        }
        inktag_image_store_result_t result = inktag_image_store_commit();
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_transfer_reset(p_epd, true);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result));
            return;
        }
        m_v2_store_pending.type = INKTAG_NRF_V2_STORE_COMMIT;
        m_v2_store_pending.opcode = opcode;
        m_v2_store_pending.transaction_id = transaction_id;
        return;
    }
#endif
    else {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_DISPLAY, true)) {
            epd_v2_transfer_reset(p_epd, true);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER);
            return;
        }
        if (!epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       NRF_EPD_OP_REFRESH, 0u, timestamp())) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_BUSY);
            return;
        }
    }
    epd_v2_transfer_release();
    {
        uint8_t response[1] = {
            object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ?
                0u : UINT8_MAX,
        };
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
    }
}

static void epd_v2_set_display_mode(ble_epd_t* p_epd, uint8_t opcode,
                                    uint8_t transaction_id,
                                    const uint8_t* payload, uint16_t length) {
    uint8_t mode;
    bool scheduled = false;
    inktag_status_v2_t failure = INKTAG_STATUS_NOT_FOUND;
    if (length < 1u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
    mode = payload[0];
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_GUI,
            MODE_DEVICE_INFO, timestamp());
    } else if (mode == INKTAG_DISPLAY_MODE_CALENDAR ||
               mode == INKTAG_DISPLAY_MODE_CLOCK) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_GUI,
            mode == INKTAG_DISPLAY_MODE_CALENDAR ? MODE_CALENDAR : MODE_CLOCK,
            timestamp());
    } else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_CLEAR,
            length > 1u ? (payload[1] != 0u) : 1u, timestamp());
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        scheduled = length == 2u &&
            epd_stored_image_replayable(p_epd, payload[1], false) &&
            epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                      NRF_EPD_OP_IMAGE_SHOW, payload[1],
                                      timestamp());
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint32_t interval = length == 5u ?
                                inktag_read_u32_le(&payload[1]) : 3600u;
        uint8_t first = UINT8_MAX;
        uint8_t previous = m_slideshow_current_id;
        bool first_available;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        m_slideshow_current_id = UINT8_MAX;
        first_available = epd_slideshow_select_next(p_epd, &first);
        m_slideshow_current_id = previous;
        if (first_available) {
            scheduled = epd_v2_schedule_operation(
                p_epd, opcode, transaction_id, NRF_EPD_OP_IMAGE_SHOW,
                first, timestamp());
            if (scheduled) {
                m_slideshow_interval = interval;
                m_slideshow_active = true;
                m_slideshow_next_at = timestamp() + interval;
            }
        }
#endif
    } else if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        failure = INKTAG_STATUS_INVALID_STATE;
    } else {
        failure = INKTAG_STATUS_UNSUPPORTED;
    }
    /* A second voltage check or queue refusal must preserve the old intent. */
    if (scheduled && (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO ||
                      mode == INKTAG_DISPLAY_MODE_CALENDAR ||
                      mode == INKTAG_DISPLAY_MODE_CLOCK ||
                      mode == INKTAG_DISPLAY_MODE_CLEAR))
        epd_slideshow_stop(p_epd, false);
    epd_v2_reply_status(p_epd, opcode, transaction_id,
                         scheduled ? INKTAG_STATUS_OK : failure);
}

static void epd_v2_display_action(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id,
                                  const uint8_t* payload, uint16_t length) {
    bool ok;
    if (length != 1u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
    } else if (m_epd_operation_active || m_epd_operation_pending ||
               m_v2_transfer.active || EPD_V2_STORE_BUSY()) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY);
    } else if ((payload[0] == INKTAG_DISPLAY_ACTION_NEXT ||
                payload[0] == INKTAG_DISPLAY_ACTION_RESUME ||
                payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) &&
               !inktag_nrf_health_operation_allowed(
                   INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT) {
        nrf_epd_operation_type_t operation;
        uint8_t argument;
        bool slideshow_step;
        ok = epd_page_cycle_resolve(p_epd, &operation, &argument,
                                    &slideshow_step) &&
             epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       operation, argument, timestamp());
        if (ok) epd_page_cycle_scheduled(p_epd, slideshow_step);
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             ok ? INKTAG_STATUS_OK : INKTAG_STATUS_NOT_FOUND);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && m_slideshow_active) {
        uint8_t current = m_slideshow_current_id;
        epd_slideshow_stop(p_epd, false);
        if (current != UINT8_MAX)
            epd_record_display_state(p_epd, EPD_DISPLAY_STATE_PICTURE,
                                     current, 0u);
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_RESUME &&
               m_display_state_loaded &&
               m_display_state.mode == EPD_DISPLAY_STATE_PICTURE &&
               epd_stored_image_replayable(p_epd, m_display_state.argument,
                                            true)) {
        m_slideshow_current_id = m_display_state.argument;
        m_slideshow_active = true;
        if (m_slideshow_interval < 30u) m_slideshow_interval = 3600u;
        m_slideshow_next_at = timestamp() + m_slideshow_interval;
        epd_record_display_state(p_epd, EPD_DISPLAY_STATE_SLIDESHOW,
                                 m_slideshow_current_id, m_slideshow_interval);
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK);
#endif
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        ok = epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       NRF_EPD_OP_REFRESH, 0u, timestamp());
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        if (p_epd->epd != NULL && EPD_BoardIsPrepared())
            p_epd->epd->drv->sleep(p_epd->epd);
        EPD_BoardRelease();
        EPD_GPIO_Uninit();
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK);
    } else {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE);
    }
}

static void epd_v2_cancel(ble_epd_t* p_epd, uint8_t opcode,
                          uint8_t transaction_id) {
#if defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) && INKTAG_BOARD_LAZY_DISPLAY_BUS
    if (m_v2_transfer.active ||
        (m_picture_io_active && !m_epd_operation_active))
        epd_v2_transfer_reset(p_epd, true);
#else
    if (m_v2_transfer.active) epd_v2_transfer_reset(p_epd, true);
#endif
    epd_slideshow_stop(p_epd, false);
    if (m_epd_operation_pending &&
        m_epd_pending_operation.origin == INKTAG_NRF_V2_OPERATION_BLE)
        m_epd_operation_pending = false;
    if (m_epd_operation_active &&
        m_epd_active_operation.origin == INKTAG_NRF_V2_OPERATION_BLE)
        m_epd_cancel_requested = true;
    epd_v2_reply_status(p_epd, opcode, transaction_id,
                         INKTAG_STATUS_OK);
}

static void epd_v2_get_settings(ble_epd_t* p_epd, uint8_t opcode,
                                uint8_t transaction_id,
                                const uint8_t* payload, uint16_t length) {
    if (length != 1u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
    } else if (payload[0] != INKTAG_SETTINGS_SECTION_IDENTITY &&
               payload[0] != INKTAG_SETTINGS_SECTION_DISPLAY) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED);
    } else if (!epd_device_settings_read(&m_device_settings) ||
               !epd_display_settings_read(&m_v2_display_settings)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_PERSISTENCE_ERROR);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        epd_v2_send_identity(p_epd, opcode, transaction_id);
    } else {
        epd_v2_send_display_settings(p_epd, opcode, transaction_id);
    }
}

static void epd_v2_set_settings(ble_epd_t* p_epd, uint8_t opcode,
                                uint8_t transaction_id,
                                const uint8_t* payload, uint16_t length) {
    if (length >= 2u && payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY &&
        payload[1] == EPD_DEVICE_SETTINGS_VERSION) {
        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        epd_device_settings_t next = m_device_settings;
        if (length != (uint16_t)(4u + name_length + label_length) ||
            name_length != 0u ||
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        next.label_length = label_length;
        memset(next.label, 0, sizeof(next.label));
        memcpy(next.label, &payload[4u + name_length], label_length);
        if (!epd_device_settings_write(&next)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        m_device_settings = next;
        {
            uint8_t reboot_required[1] = {0u};
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_OK, reboot_required,
                                 sizeof(reboot_required));
        }
        epd_v2_send_event(p_epd, INKTAG_EVENT_DEVICE_CHANGED,
                          INKTAG_STATUS_OK, opcode, transaction_id, NULL, 0u);
    } else if (length >= INKTAG_DISPLAY_SETTINGS_BASE_SIZE &&
               payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY &&
               payload[1] == INKTAG_NRF_CURRENT_DISPLAY_SETTINGS_VERSION) {
        uint16_t writable_flags = EPD_DISPLAY_STATE_USER_FLAGS_MASK;
        uint16_t next_reserved = (uint16_t)(
            (m_display_state.reserved &
             (uint16_t)~writable_flags) |
            (m_v2_pending_display_settings.flags & writable_flags));
        if (!epd_v2_persist_display_settings(
                p_epd, &m_v2_pending_display_settings, next_reserved)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, payload, length);
    } else {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED);
    }
}

static bool epd_v2_apply_saved_screen(ble_epd_t *p_epd) {
    EPD_GPIO_Uninit();
    p_epd->epd = NULL;
    memset(&p_epd->screen_config,0,sizeof(p_epd->screen_config));
    inktag_settings_journal_read_result_t result = epd_screen_config_read_result(&p_epd->screen_config);
#if defined(INKTAG_NRF_BOARD_NRF52811_AESL)
    inktag_nrf_health_panel_temperature_sample(NULL);
#endif
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
    bool images_ready = inktag_image_store_init();
    if (result==INKTAG_SETTINGS_JOURNAL_READ_FOUND && !images_ready)
        return false;
#endif
    if (result==INKTAG_SETTINGS_JOURNAL_READ_FOUND)
        p_epd->epd = p_epd->config.mosi_pin!=255u ? epd_find(&p_epd->screen_config) : NULL;
#if !defined(INKTAG_BOARD_LAZY_DISPLAY_BUS) || !INKTAG_BOARD_LAZY_DISPLAY_BUS
    if (p_epd->epd) EPD_GPIO_Init();
#endif
    return result!=INKTAG_SETTINGS_JOURNAL_READ_ERROR;
}

#if INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED
static inktag_status_v2_t epd_v2_cleanup(ble_epd_t *p_epd, bool images,
                                        uint16_t *removed) {
    epd_display_state_t state = m_display_state;
    inktag_display_settings_t defaults;
    const inktag_display_settings_t *settings = &m_v2_display_settings;
    uint32_t mask = INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK;
    if (images) {
        if (!epd_display_settings_read(&m_v2_display_settings))
            return INKTAG_STATUS_PERSISTENCE_ERROR;
        inktag_image_store_result_t result = inktag_image_store_clear_user_images(removed);
        if (result != INKTAG_IMAGE_STORE_OK) return epd_v2_store_status(result);
        state.mode = EPD_DISPLAY_STATE_PICTURE;
        state.argument = EPD_DISPLAY_STATE_NO_SOURCE;
        state.interval_seconds = 0u;
        mask = INKTAG_SETTINGS_CLEAR_IMAGES_MASK;
    } else {
        memset(&defaults, 0, sizeof(defaults));
        epd_config_t factory;
        epd_config_read(&factory);
        if (epd_config_empty(&factory)) return INKTAG_STATUS_PERSISTENCE_ERROR;
        defaults.week_start = factory.week_start;
        settings = &defaults;
        state.reserved = 0u;
    }
    /* This InkTag target has no password backend. The existing journal commits
     * ordinary removal and QR removal together, or retains the whole old set.
     * No owner marker, activation or Find record is interpreted or removed. */
    if (!epd_display_settings_cleanup(&state, settings, mask))
        return inktag_nrf_config_store_last_error() == NRF_ERROR_FORBIDDEN ?
            INKTAG_STATUS_LOW_POWER : INKTAG_STATUS_PERSISTENCE_ERROR;
    if (!epd_device_settings_read(&m_device_settings))
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    m_display_state = state;
    epd_slideshow_stop(p_epd, false);
    m_native_timer_update_pending = false;
    if (images) {
        p_epd->config.display_mode = epd_display_state_driver_mode(state.mode);
    } else {
        uint8_t mode=p_epd->config.display_mode;
        epd_config_read(&p_epd->config);
        if(epd_config_empty(&p_epd->config))return INKTAG_STATUS_PERSISTENCE_ERROR;
        p_epd->config.display_mode=mode;EPD_GPIO_Load(&p_epd->config);
        m_v2_display_settings = defaults;
        epd_v2_display_settings_sync_runtime(p_epd);
        if (!epd_v2_apply_saved_screen(p_epd)) return INKTAG_STATUS_PERSISTENCE_ERROR;
#if INKTAG_NRF_INDICATOR_ENABLED
        inktag_status_config_t indicator;
        inktag_status_config_default(&indicator);
        EPD_LED_SetEnabled(indicator.policy != INKTAG_STATUS_POLICY_OFF);
        inktag_status_set_config(&m_v2_indicator_runtime,&indicator,app_monotonic_now_ms());
#endif
    }
    return INKTAG_STATUS_OK;
}
#endif

static void epd_v2_system_action(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id, uint8_t flags,
                                 const uint8_t* payload, uint16_t length) {
    const inktag_lifecycle_capabilities_t capabilities = {
        .reset = true,
        .timed_sleep = false,
        .factory_reset = false,
        .ota_recovery = true,
        .reset_settings = INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED != 0,
        .clear_user_data = INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED != 0,
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &capabilities, &plan);
    if (status != INKTAG_STATUS_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id, status);
        return;
    }
    if (m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active || EPD_V2_STORE_BUSY()
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        || inktag_image_store_busy()
#endif
    ) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET) {
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
        epd_v2_led_test_cancel();
#endif
        (void)epd_time_state_write(timestamp(), m_v2_timezone_minutes);
        if (!epd_v2_schedule_system_action(plan.action)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_IO_ERROR);
            return;
        }
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK);
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY) {
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
        epd_v2_led_test_cancel();
#endif
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_OTA, true)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER);
            return;
        }
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK);
        nrf_delay_ms(250u);
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_OTA, true)) {
            epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR,
                              INKTAG_STATUS_LOW_POWER, opcode,
                              transaction_id, NULL, 0u);
            return;
        }
        if (ble_dfu_buttonless_bootloader_start_prepare() != NRF_SUCCESS) {
            epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR,
                              INKTAG_STATUS_IO_ERROR, opcode, transaction_id,
                              NULL, 0u);
        }
#if INKTAG_BOARD_LIFECYCLE_V2_SUPPORTED
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS ||
               plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        uint16_t removed = 0u;
        bool images = plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA;
        status = epd_v2_cleanup(p_epd, images, &removed);
        if (status == INKTAG_STATUS_OK && !images &&
            !epd_v2_schedule_system_action(INKTAG_SYSTEM_ACTION_RESET))
            status = INKTAG_STATUS_IO_ERROR;
        uint8_t reply[2];
        inktag_write_u16_le(reply, removed);
        epd_v2_send_response(p_epd, opcode, transaction_id, status,
            status == INKTAG_STATUS_OK && images ? reply : NULL,
            status == INKTAG_STATUS_OK && images ? sizeof(reply) : 0u);
#endif
    } else {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED);
    }
}



#if INKTAG_NRF_IMAGE_STORE_ENABLED
static void epd_v2_delete_resource(ble_epd_t* p_epd, uint8_t opcode,
                                   uint8_t transaction_id,
                                   const uint8_t* payload, uint16_t length) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    if (length != 1u || m_v2_store_pending.type != 0u ||
        m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
            length == 1u ? INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result));
        return;
    }
    if (!epd_v2_image_record_visible(p_epd, payload[0])) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_DENIED);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
    epd_slideshow_stop(p_epd, false);
    result = inktag_image_store_delete(payload[0]);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result));
        return;
    }
    m_v2_store_pending.type = INKTAG_NRF_V2_STORE_DELETE;
    m_v2_store_pending.opcode = opcode;
    m_v2_store_pending.transaction_id = transaction_id;
}

static void epd_v2_clear_resources(ble_epd_t* p_epd, uint8_t opcode,
                                   uint8_t transaction_id,
                                   const uint8_t* payload, uint16_t length) {
    inktag_image_store_result_t result;
    if (length != 1u || m_v2_store_pending.type != 0u ||
        m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
            length == 1u ? INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
    epd_slideshow_stop(p_epd, false);
    result = payload[0] == INKTAG_RESOURCE_CLEAR_ALL ?
                 epd_v2_clear_visible_resources(p_epd, false) :
             payload[0] == INKTAG_RESOURCE_CLEAR_INCOMPATIBLE ?
                 epd_v2_clear_visible_resources(p_epd, true) :
                 INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result));
        return;
    }
    m_v2_store_pending.type = INKTAG_NRF_V2_STORE_CLEAR;
    m_v2_store_pending.opcode = opcode;
    m_v2_store_pending.transaction_id = transaction_id;
}
#endif

#if INKTAG_NRF_INDICATOR_ENABLED
static void epd_v2_indicator_config(ble_epd_t* p_epd, uint8_t opcode,
                                    uint8_t transaction_id, bool write,
                                    const uint8_t* payload, uint16_t length) {
    uint8_t response[INKTAG_STATUS_WIRE_SIZE];
    if (write) {
        inktag_status_config_t decoded;
        if (!inktag_status_config_decode(&decoded, payload, length)) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_INDICATOR, &decoded, sizeof(decoded))) {
            epd_v2_reply_status(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        EPD_LED_SetEnabled(decoded.policy != INKTAG_STATUS_POLICY_OFF);
        inktag_status_set_config(&m_v2_indicator_runtime, &decoded,
                                 app_monotonic_now_ms());
    } else if (length != 0u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (!inktag_status_config_encode(&m_v2_indicator_config, response)) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
}
#endif

static void epd_v2_send_health(ble_epd_t* p_epd, uint8_t opcode,
                               uint8_t transaction_id,
                               const uint8_t* request,
                               uint16_t request_length) {
    uint8_t record_index;
    uint8_t payload[INKTAG_NRF_HEALTH_WIRE_SIZE
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
                    + INKTAG_SOAK_TELEMETRY_WIRE_SIZE
#endif
    ];
    uint16_t length;
    if (request_length > 1u) {
        epd_v2_reply_status(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    record_index = request_length == 1u ? request[0] : 0u;
    length = inktag_nrf_health_encode(record_index, payload,
                                      INKTAG_NRF_HEALTH_WIRE_SIZE);
    if (length == 0u || (record_index != 0u && record_index >= payload[20])) {
        epd_v2_reply_status(
            p_epd, opcode, transaction_id,
            length == 0u ? INKTAG_STATUS_INTERNAL_ERROR :
                           INKTAG_STATUS_NOT_FOUND);
        return;
    }
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
    length = (uint16_t)(length + inktag_soak_telemetry_encode(
        &payload[length], (uint16_t)(sizeof(payload) - length),
        app_monotonic_now_ms()));
#endif
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, length);
}





#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
static bool epd_v2_run_led_test(ble_epd_t* p_epd, uint8_t opcode,
                                uint8_t transaction, const uint8_t* payload,
                                uint16_t length) {
    if (!length || payload[0] != INKTAG_TEST_LED_CHANNEL) return false;
    if (length != 2u || payload[1] > 7u) {
        epd_v2_reply_status(p_epd, opcode, transaction,
                             INKTAG_STATUS_INVALID_REQUEST);
    } else {
        m_v2_led_test_active = payload[1] != 0u;
        m_v2_led_test_deadline = app_monotonic_now_ms() + 10000u;
        inktag_nrf_led_apply(payload[1]);
        m_v2_indicator_runtime.last_output = UINT8_MAX;
        epd_v2_reply_status(p_epd, opcode, transaction, INKTAG_STATUS_OK);
        epd_v2_send_event(p_epd, INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK,
                         opcode, transaction, &payload[1], 1u);
    }
    return true;
}
#endif

/* These existing chip readings need no board-selected pin or new driver.
 * SAADC has no qualified programmed-reference diagnostic on this board. */
static void epd_v2_adc_test(ble_epd_t *epd, uint8_t opcode, uint8_t tx,
                            const uint8_t *payload, uint16_t length) {
    if (length != 2u || payload[1] > 2u) {
        epd_v2_reply_status(epd, opcode, tx, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    uint8_t channel = payload[1];
    if (channel == 2u) {
        epd_v2_reply_status(epd, opcode, tx, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    uint8_t response[6] = {1u, channel, 0u, 0u, 0u, 0u};
    uint16_t reading;
    if (!channel) {
        int16_t temperature = 0;
        inktag_nrf_health_temperature_sample();
        response[2] = inktag_nrf_health_mcu_temperature(&temperature);
        /* The Nordic provider stores exact quarter-degree samples times4. */
        reading = (uint16_t)((uint32_t)(int32_t)temperature >> 2);
        response[5] = (uint8_t)(int8_t)(temperature / 16);
    } else {
        reading = inktag_nrf_health_voltage_mv(true);
        response[2] = inktag_nrf_health_voltage_valid();
    }
    inktag_write_u16_le(response + 3u, reading);
    epd_v2_send_response(epd, opcode, tx, INKTAG_STATUS_OK, response, sizeof(response));
    epd_v2_send_event(epd, INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK, opcode, tx, payload + 1u, 1u);
}

static void epd_v2_get_io(ble_epd_t *p_epd,uint8_t opcode,uint8_t transaction,uint8_t selector) {
    if(selector==INKTAG_RELEASE_AUX_GET_SELECTOR) {
        /* Read-only board facts: no qualified stock key. The final five
         * bytes remain opaque ABI placeholders. */
        static const uint8_t aux[12]={1u,255u,0u,255u,0u,255u,0u,0u,255u,255u,0xd0u,0x07u};
        epd_v2_send_response(p_epd,opcode,transaction,INKTAG_STATUS_OK,aux,sizeof(aux));return;
    }
    (void)*(volatile const char*)nrf11_factory_pin_contract;
    const uint8_t *current=inktag_nrf_pins_current();uint8_t response[36];
    if(!current || !inktag_nrf_factory_pins(response+4u)) {
        epd_v2_reply_status(p_epd,opcode,transaction,INKTAG_STATUS_PERSISTENCE_ERROR);return;
    }
    response[0]=INKTAG_RELEASE_PIN_REPORT_VERSION;
    response[1]=INKTAG_PIN_REPORT_DEFAULT_VALID;
    if(memcmp(response+4u,current,16u))response[1]|=INKTAG_PIN_REPORT_OVERRIDE_PRESENT;
    if(p_epd->epd)response[1]|=INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED;
    inktag_write_u16_le(response+2u,INKTAG_BOARD_PIN_EDITABLE_ROLES);
    memcpy(response+20u,current,16u);
    epd_v2_send_response(p_epd,opcode,transaction,INKTAG_STATUS_OK,response,sizeof(response));
}
static __attribute__((noinline,noclone)) inktag_status_v2_t epd_v2_set_io(
    ble_epd_t *p_epd,const uint8_t *payload,uint16_t length) {
    if(!payload)return INKTAG_STATUS_INVALID_REQUEST;
    if(length==12u && payload[0]==INKTAG_RELEASE_AUX_VERSION)return INKTAG_STATUS_UNSUPPORTED;
    bool restore=length==2u && payload[0]==2u && payload[1]==0u;
    if(!restore && !(length==18u && payload[0]==2u && payload[1]==1u))
        return INKTAG_STATUS_INVALID_REQUEST;
    /* The common packet dispatcher already checks display/transfer/store
     * idleness for SET_IO_CONFIG. Only the additional IO owners remain here. */
    if(m_picture_io_active || m_v2_system_action_pending || m_v2_led_test_active)
        return INKTAG_STATUS_BUSY;
    if(!inktag_nrf_pins_current())return INKTAG_STATUS_PERSISTENCE_ERROR;
    uint8_t next[16];
    inktag_status_v2_t status=inktag_nrf_settings_pins_commit(restore?NULL:payload+2u,next);
    if(status!=INKTAG_STATUS_OK) {
        if(status==INKTAG_STATUS_PERSISTENCE_ERROR) {
            inktag_nrf_pins_invalidate();memset(&p_epd->config,255,sizeof(p_epd->config));p_epd->epd=NULL;
        }
        return status;
    }
    if(!inktag_nrf_pins_publish(next))return INKTAG_STATUS_PERSISTENCE_ERROR;
    inktag_nrf_pins_project(&p_epd->config);EPD_GPIO_Load(&p_epd->config);
    p_epd->epd=p_epd->config.mosi_pin!=255u?epd_find(&p_epd->screen_config):NULL;
    inktag_nrf_led_apply(m_v2_indicator_runtime.last_output);
    return INKTAG_STATUS_OK;
}

#if defined(__GNUC__) && defined(NRF52811_XXAA)
/* Preserve the qualified dispatcher override after inserting the IO helpers:
 * dense command groups use tables; the rest of the target keeps its flags. */
__attribute__((optimize("jump-tables")))
#endif
static void epd_v2_process_packet(ble_epd_t* p_epd, const uint8_t* packet,
                                  uint16_t packet_length) {
    /* Private mailbox only: queue admission already validated the wire and
     * stored semantic flags. Never send this internal representation to BLE. */
    const inktag_protocol_request_t request = {
        packet[0], packet[1], packet[2], packet + INKTAG_PROTOCOL_V2_HEADER_SIZE,
        (uint16_t)(packet_length - INKTAG_PROTOCOL_V2_HEADER_SIZE)
    };
    inktag_nrf_health_stage(0x4E52424Cu, INKTAG_NRF_HEALTH_OPERATION_BLE);
    if (request.opcode == INKTAG_CMD_SET_SETTINGS) {
        inktag_status_v2_t access = epd_v2_display_settings_write_access(
            request.payload, request.payload_length);
        if (access != INKTAG_STATUS_OK) {
            epd_v2_reply_status(p_epd, request.opcode,
                                 request.transaction_id, access);
            return;
        }
    }
    if (inktag_protocol_command_mutates_persistent_state(
            request.opcode, request.payload, request.payload_length) &&
        !inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        epd_v2_reply_status(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (epd_v2_command_requires_idle_display(request.opcode) &&
        (m_epd_operation_active || m_epd_operation_pending ||
         m_v2_transfer.active || EPD_V2_STORE_BUSY())) {
        epd_v2_reply_status(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_BUSY);
        return;
    }
    switch (request.opcode) {
        case INKTAG_CMD_HELLO:
            epd_v2_send_hello(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_GET_CAPABILITIES:
            epd_v2_send_capabilities(p_epd, request.opcode,
                                     request.transaction_id);
            break;
        case INKTAG_CMD_GET_DEVICE_INFO:
            epd_v2_send_device_info(p_epd, request.opcode,
                                    request.transaction_id);
            break;
        case INKTAG_CMD_GET_STATUS:
            epd_v2_send_status(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_GET_GUI_ASSET_INFO: {
            /* This fixed InkTag layout has no owner ITFA-v1 bank. The native
             * 16-byte/schema-2 factory asset is not a v1 package: report the
             * standard no-bank state, never reinterpret its payload CRC.
             * maximumBytes is the protocol package ceiling, not reserved
             * space or permission to install an owner package. */
            static const uint8_t response[20] = {
                1u, 1u, 0xFFu, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                0u, 0x40u, 0u, 0u, 0u, 0u, 0u, 0u,
            };
            bool valid = request.payload_length == 0u;
            epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                valid ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_REQUEST,
                valid ? response : NULL, valid ? sizeof(response) : 0u);
            break;
        }
        case INKTAG_CMD_GET_TIME:
            epd_v2_send_time(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_SET_TIME:
            epd_v2_time_command(p_epd, request.opcode, request.transaction_id,
                                request.payload, request.payload_length);
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE:
            epd_v2_send_screen(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            epd_screen_config_t screen;
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
            inktag_image_store_result_t profile_result;
#endif
            if (m_epd_operation_active || m_epd_operation_pending ||
                m_v2_transfer.active || EPD_V2_STORE_BUSY()) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_BUSY);
            } else if (epd_config_empty(&p_epd->config) ||
                       !epd_device_settings_read(&m_device_settings)) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            } else if (inktag_nrf_screen_omitted(request.payload,
                                                request.payload_length)) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
            } else if (epd_screen_config_decode_exact(
                           request.payload, request.payload_length, &screen) !=
                           EPD_SCREEN_STATUS_OK || epd_find(&screen) == NULL) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INCOMPATIBLE);
#if INKTAG_NRF_FIXED_IMAGE_SLOTS
            } else if ((profile_result = inktag_image_store_prepare_profile(&screen)) != INKTAG_IMAGE_STORE_OK) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, epd_v2_store_status(profile_result));
#endif
            } else if (!epd_screen_config_write(&screen)) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            } else {
                epd_slideshow_stop(p_epd, false);
                epd_record_display_state(p_epd, EPD_DISPLAY_STATE_PICTURE,
                                         EPD_DISPLAY_STATE_NO_SOURCE, 0u);
                if (!epd_v2_apply_saved_screen(p_epd)) {
                    epd_v2_reply_status(p_epd, request.opcode,
                        request.transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
                    break;
                }
                epd_v2_send_screen(p_epd, request.opcode,
                                   request.transaction_id);
                epd_v2_send_event(p_epd, INKTAG_EVENT_DEVICE_CHANGED,
                    INKTAG_STATUS_OK, request.opcode, request.transaction_id,
                    NULL, 0u);
            }
            break;
        }
        case INKTAG_CMD_SET_DISPLAY_MODE:
            epd_v2_set_display_mode(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_DISPLAY_ACTION:
            epd_v2_display_action(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_CANCEL:
            epd_v2_cancel(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_GET_SETTINGS:
            epd_v2_get_settings(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_SET_SETTINGS:
            epd_v2_set_settings(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_SYSTEM_ACTION:
            epd_v2_system_action(p_epd, request.opcode,
                request.transaction_id, request.flags, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_TRANSFER_BEGIN:
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
            epd_v2_led_test_cancel();
#endif
            epd_v2_transfer_begin(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_TRANSFER_WRITE:
            epd_v2_transfer_write(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_TRANSFER_COMMIT:
            if (request.payload_length != 0u)
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            else
                epd_v2_transfer_commit(p_epd, request.opcode,
                                       request.transaction_id);
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!m_v2_transfer.active || request.transaction_id !=
                                             m_v2_transfer.transaction_id) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_STATE);
            } else {
                epd_v2_transfer_reset(p_epd, true);
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_OK);
            }
            break;
        case INKTAG_CMD_GET_TRANSFER_STATUS: {
            uint8_t response[11] = {0};
            response[0] = m_v2_transfer.active ? 1u : 0u;
            response[1] = m_v2_transfer.descriptor.object_type;
            response[2] = m_v2_transfer.transaction_id;
            inktag_write_u32_le(&response[3], m_v2_transfer.written);
            inktag_write_u32_le(&response[7],
                m_v2_transfer.descriptor.total_size);
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_OK,
                response, sizeof(response));
            break;
        }
        case INKTAG_CMD_GET_IO_CONFIG:
            if(request.payload_length==1u && (request.payload[0]==2u || request.payload[0]==3u))
                epd_v2_get_io(p_epd,request.opcode,request.transaction_id,request.payload[0]);
            else epd_v2_reply_status(p_epd,request.opcode,request.transaction_id,
                INKTAG_STATUS_INVALID_REQUEST);
            break;
        case INKTAG_CMD_SET_IO_CONFIG: {
            inktag_status_v2_t status=epd_v2_set_io(p_epd,request.payload,request.payload_length);
            if(status==INKTAG_STATUS_OK)epd_v2_get_io(p_epd,request.opcode,request.transaction_id,2u);
            else epd_v2_reply_status(p_epd,request.opcode,request.transaction_id,status);
            break;
        }
        case INKTAG_CMD_GET_RESOURCE_INFO:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_send_resource_info(p_epd, request.opcode,
                                      request.transaction_id);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_LIST_RESOURCES:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_list_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_READ_RESOURCE:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_read_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_DELETE_RESOURCE:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_delete_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_CLEAR_RESOURCES:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_clear_resources(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;

#if INKTAG_NRF_TEST_ENABLED
        case INKTAG_CMD_LIST_TESTS: {
#if INKTAG_BOARD_INPUT_QUALIFIED
            const uint8_t response[] = {
                1u, 1u, INKTAG_TEST_INPUT_SIMULATE,
            };
#elif INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
            static const uint8_t response[] = {1u, 5u, INKTAG_TEST_LED_CHANNEL, INKTAG_TEST_DEVICE_LOCATE, INKTAG_TEST_TIME_SHIFT, INKTAG_TEST_TIME_SET, INKTAG_TEST_ADC_READ};
#else
            const uint8_t response[] = {1u, 0u};
#endif
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_OK,
                response, sizeof(response));
            break;
        }
        case INKTAG_CMD_RUN_TEST:
            if (request.payload_length && request.payload[0] == INKTAG_TEST_ADC_READ) {
                epd_v2_adc_test(p_epd, request.opcode, request.transaction_id,
                                request.payload, request.payload_length);
                break;
            }
            if (request.payload_length && (request.payload[0] == INKTAG_TEST_TIME_SHIFT ||
                                           request.payload[0] == INKTAG_TEST_TIME_SET)) {
                epd_v2_time_command(p_epd, request.opcode, request.transaction_id,
                                    request.payload, request.payload_length);
                break;
            }
#if INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED
            if (request.payload_length && request.payload[0] == INKTAG_TEST_DEVICE_LOCATE) {
                if (request.payload_length != 2u || request.payload[1] > 1u) {
                    epd_v2_reply_status(p_epd, request.opcode, request.transaction_id,
                        INKTAG_STATUS_INVALID_REQUEST);
                } else {
                    epd_v2_led_test_cancel();
                    if (request.payload[1]) inktag_status_locate_start(&m_v2_indicator_runtime, app_monotonic_now_ms());
                    else inktag_status_locate_stop(&m_v2_indicator_runtime, app_monotonic_now_ms());
                    epd_v2_reply_status(p_epd, request.opcode, request.transaction_id, INKTAG_STATUS_OK);
                    epd_v2_send_event(p_epd, INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK,
                        request.opcode, request.transaction_id, request.payload + 1u, 1u);
                }
                break;
            }
            if (epd_v2_run_led_test(p_epd, request.opcode, request.transaction_id,
                                     request.payload, request.payload_length)) break;
#endif
#if INKTAG_BOARD_INPUT_QUALIFIED
            if (request.payload_length == 2u &&
                request.payload[0] == INKTAG_TEST_INPUT_SIMULATE &&
                request.payload[1] >= INKTAG_BUTTON_GESTURE_SINGLE &&
                request.payload[1] <= INKTAG_BUTTON_GESTURE_LONG) {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_OK);
                ble_epd_handle_button_gesture(
                    p_epd, (inktag_button_gesture_t)request.payload[1]);
            } else {
                epd_v2_reply_status(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            }
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_GET_TEST_STATUS: {
            uint8_t response[3] = {
                1u, (m_epd_operation_active || m_epd_operation_pending) ? 1u : 0u,
                epd_v2_current_display_mode(),
            };
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_OK,
                response, sizeof(response));
            break;
        }
#endif
        case INKTAG_CMD_GET_HEALTH:
            epd_v2_send_health(p_epd, request.opcode,
                               request.transaction_id, request.payload,
                               request.payload_length);
            break;
        case INKTAG_CMD_GET_INDICATOR_CONFIG:
#if INKTAG_NRF_INDICATOR_ENABLED
            epd_v2_indicator_config(p_epd, request.opcode,
                request.transaction_id, false, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
#if INKTAG_NRF_INDICATOR_ENABLED
            epd_v2_indicator_config(p_epd, request.opcode,
                request.transaction_id, true, request.payload,
                request.payload_length);
#else
            epd_v2_reply_status(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        default:
            /* IRQ admission rejects every omitted or unknown command. */
            break;
    }
}

static void epd_v2_process_queued(void* p_event_data, uint16_t event_size) {
    uint16_t length;
    ble_epd_t* p_epd = p_event_data == NULL ? NULL :
        *(ble_epd_t**)p_event_data;
    (void)event_size;
    if (!m_v2_rx_pending || m_v2_rx_processing || p_epd == NULL) return;
    length = m_v2_rx_length;
    m_v2_rx_processing = true;
    /* Lease the mailbox through synchronous dispatch, including Flash waits.
     * A new packet cannot overwrite it while the current command uses it. */
    epd_v2_process_packet(p_epd, m_v2_rx_packet, length);
    m_v2_rx_processing = false;
    m_v2_rx_pending = false;
    m_v2_rx_length = 0u;
}

static bool epd_v2_defer_display_read(ble_epd_t* p_epd, uint8_t opcode) {
    if (!m_epd_operation_active ||
        (opcode != INKTAG_CMD_GET_TIME && opcode != INKTAG_CMD_GET_HEALTH))
        return false;
    m_v2_display_read_service = p_epd;
    return true;
}

static void epd_v2_display_read_yield(void) {
    ble_epd_t* p_epd = m_v2_display_read_service;
    if (p_epd == NULL || m_v2_rx_processing) return;
    m_v2_display_read_service = NULL;
    epd_v2_process_queued(&p_epd, sizeof(p_epd));
}

static void epd_v2_queue_packet(ble_epd_t* p_epd, const uint8_t* packet,
                                uint16_t length) {
    if (p_epd == NULL || packet == NULL || length < INKTAG_PROTOCOL_V2_HEADER_SIZE ||
        length > sizeof(m_v2_rx_packet)) return;
    /* Validate exactly once before cancellation, BUSY or mailbox publication. */
    inktag_protocol_request_t request;
    inktag_status_v2_t status = inktag_protocol_parse_request(packet,length,&request);
    if (status != INKTAG_STATUS_OK) goto reply;
    /* AUX customization is Pro-only. Reject before display/BUSY/ADC/mailbox. */
    if (request.opcode == INKTAG_CMD_SET_IO_CONFIG &&
        request.payload_length == INKTAG_RELEASE_AUX_BYTES &&
        request.payload[0] == INKTAG_RELEASE_AUX_VERSION) {
        status = INKTAG_STATUS_UNSUPPORTED;
        goto reply;
    }
    /* Unsupported InkTag commands must not lease/overwrite the mailbox,
     * schedule work or disturb an operation already in progress. The same
     * parsed request is carried into the worker after this admission. */
    uint8_t policy = epd_v2_command_policy(request.opcode);
    if (policy < 2u) {
        status = (inktag_status_v2_t)(policy+1u);
        goto reply;
    }
    if (packet[0] == INKTAG_CMD_CANCEL &&
        ((m_epd_operation_active &&
          m_epd_active_operation.origin == INKTAG_NRF_V2_OPERATION_BLE) ||
         (m_epd_operation_pending &&
          m_epd_pending_operation.origin == INKTAG_NRF_V2_OPERATION_BLE))) {
        epd_slideshow_stop(p_epd, false);
        if (m_epd_operation_pending &&
            m_epd_pending_operation.origin == INKTAG_NRF_V2_OPERATION_BLE)
            m_epd_operation_pending = false;
        if (m_epd_operation_active &&
            m_epd_active_operation.origin == INKTAG_NRF_V2_OPERATION_BLE)
            m_epd_cancel_requested = true;
        goto reply;
    }
    if (policy == 3u &&
        (m_epd_operation_active || m_epd_operation_pending ||
         m_v2_transfer.active || EPD_V2_STORE_BUSY())) {
        status = INKTAG_STATUS_BUSY;
        goto reply;
    }
    if (m_v2_rx_pending || m_v2_rx_processing) {
        status = INKTAG_STATUS_BUSY;
        goto reply;
    }
    memcpy(m_v2_rx_packet, packet, length);
    m_v2_rx_packet[2] = request.flags;
    m_v2_rx_length = length;
    m_v2_rx_pending = true;
    if (epd_v2_defer_display_read(p_epd, packet[0])) return;
    if (app_sched_event_put(&p_epd, sizeof(p_epd), epd_v2_process_queued) !=
        NRF_SUCCESS) {
        m_v2_rx_pending = false;
        m_v2_rx_length = 0u;
        status = INKTAG_STATUS_INTERNAL_ERROR;
        goto reply;
    }
    return;
reply:
    epd_v2_reply_status(p_epd, packet[0], packet[1], status);
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static bool epd_v2_handle_store_event(ble_epd_t* p_epd,
                                      const inktag_image_store_event_t* event) {
    uint8_t response[5] = {0};
    uint8_t opcode;
    uint8_t transaction;
    inktag_status_v2_t status;
    if (event == NULL)
        return true;
    if (event->type == INKTAG_IMAGE_STORE_EVENT_WRITE &&
        m_v2_transfer.active &&
        m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        uint8_t transaction_id = m_v2_transfer.transaction_id;
        inktag_status_v2_t event_status = epd_v2_store_status(event->result);
        /* An asynchronous payload-write failure leaves the store transaction
         * active so it can be cleaned up safely. Explicitly abort it before
         * releasing the protocol lease; otherwise every later store command
         * remains permanently BUSY. */
        if (inktag_image_store_capture_active()) inktag_image_store_abort();
        epd_v2_transfer_release();
        EPD_V2_STORE_RESET();
        epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR, event_status,
                          INKTAG_CMD_TRANSFER_WRITE, transaction_id,
                          NULL, 0u);
        return true;
    }
    if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_NONE) return true;
    opcode = m_v2_store_pending.opcode;
    transaction = m_v2_store_pending.transaction_id;
    status = epd_v2_store_status(event->result);
    if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_BEGIN &&
        event->type == INKTAG_IMAGE_STORE_EVENT_BEGIN) {
        if (event->result == INKTAG_IMAGE_STORE_OK) {
            response[0] = m_v2_transfer.descriptor.object_type;
            response[1] = event->id;
            inktag_write_u16_le(&response[2],
                                (uint16_t)(p_epd->max_data_len - 7u));
            response[4] = m_v2_transfer.descriptor.segment_count;
            epd_v2_send_response(p_epd, opcode, transaction, status,
                                 response, sizeof(response));
        } else {
            epd_v2_transfer_release();
            epd_v2_reply_status(p_epd, opcode, transaction, status);
        }
    } else if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_COMMIT &&
               event->type == INKTAG_IMAGE_STORE_EVENT_COMMIT) {
        if (event->result == INKTAG_IMAGE_STORE_OK) {
            response[0] = event->id;
            m_pending_picture_store_id = event->id;
            epd_v2_send_response(p_epd, opcode, transaction, status,
                                 response, 1u);
            epd_v2_send_event(p_epd, INKTAG_EVENT_RESOURCE_CHANGED,
                              INKTAG_STATUS_OK, opcode, transaction,
                              response, 1u);
        } else {
            epd_v2_reply_status(p_epd, opcode, transaction, status);
        }
        epd_v2_transfer_release();
    } else if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_DELETE &&
               event->type == INKTAG_IMAGE_STORE_EVENT_DELETE) {
        epd_v2_reply_status(p_epd, opcode, transaction, status);
        if (event->result == INKTAG_IMAGE_STORE_OK) {
            response[0] = event->id;
            epd_v2_send_event(p_epd, INKTAG_EVENT_RESOURCE_CHANGED,
                              INKTAG_STATUS_OK, opcode, transaction,
                              response, 1u);
        }
    } else if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_CLEAR &&
               (event->type == INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL ||
                event->type == INKTAG_IMAGE_STORE_EVENT_CLEAR_INCOMPATIBLE)) {
        inktag_write_u16_le(response, event->removed);
        epd_v2_send_response(p_epd, opcode, transaction, status,
            event->result == INKTAG_IMAGE_STORE_OK ? response : NULL,
            event->result == INKTAG_IMAGE_STORE_OK ? 2u : 0u);
        if (event->result == INKTAG_IMAGE_STORE_OK)
            epd_v2_send_event(p_epd, INKTAG_EVENT_RESOURCE_CHANGED,
                              INKTAG_STATUS_OK, opcode, transaction,
                              response, 2u);
    } else {
        return false;
    }
    memset(&m_v2_store_pending, 0, sizeof(m_v2_store_pending));
    return true;
}
#endif

static void epd_v2_tick(ble_epd_t* p_epd) {
    inktag_nrf_record_event_t record_event;
    while (inktag_nrf_record_store_take_event(&record_event)) {
        if (record_event.kind == INKTAG_NRF_RECORD_HEALTH)
            inktag_nrf_health_record_complete(record_event.success);
    }
    if (m_v2_transfer.active &&
        app_timer_cnt_diff_compute(app_timer_cnt_get(),
                                   m_v2_transfer.last_activity_ticks) >=
            INKTAG_NRF_V2_TRANSFER_LEASE_TICKS) {
        uint8_t transaction = m_v2_transfer.transaction_id;
        epd_v2_transfer_reset(p_epd, true);
        EPD_V2_STORE_RESET();
        epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR, INKTAG_STATUS_TIMEOUT,
                          INKTAG_CMD_TRANSFER_WRITE, transaction, NULL, 0u);
    }
}
