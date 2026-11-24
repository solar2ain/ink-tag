#include "inktag_nrf_build_profile.h"
#line 1 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
/* Nordic nRF52 0.2.0 protocol adapter.
 *
 * This file is included by inktag_epd_service.c so it can reuse the established
 * Nordic display scheduler and settings/image-store state. Wire validation and
 * transfer arithmetic live in firmware/common/protocol; SoftDevice, Flash and
 * exact-board decisions stay here and in inktag_board.h. */

#include "inktag_nrf_config_store.h"
#include "inktag_nrf_buttonless.h"
#include "inktag_release_v1.h"
#include "inktag_display_settings.h"
#include "inktag_gui_assets.h"
#include "inktag_temperature_wire.h"
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#include "inktag_nrf_internal_assets.h"
#endif
#include "inktag_packbits.h"
#if INKTAG_TRANSFER_LZO1X_ENABLED
#include "inktag_lzo1x.h"
#endif
#include "nrf_error.h"
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 26 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
#include "inktag_nrf_password.h"
#endif

#define INKTAG_NRF_V2_OPERATION_BLE 1u
#ifndef INKTAG_BOARD_IMAGE_INDEX_QUALIFIED
#define INKTAG_BOARD_IMAGE_INDEX_QUALIFIED 0
#endif
#ifndef INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED
#define INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED 0
#endif
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
/* IRQ ingress may request cancellation while a decoded block drains Flash.
 * Never dispatch the application scheduler recursively from the sink. */
static volatile bool m_v2_write_cancel_requested;
static volatile bool m_v2_write_active;

#if INKTAG_NRF_IMAGE_STORE_ENABLED
typedef enum {
    INKTAG_NRF_V2_STORE_NONE = 0,
    INKTAG_NRF_V2_STORE_BEGIN,
    INKTAG_NRF_V2_STORE_COMMIT,
    INKTAG_NRF_V2_STORE_DELETE,
    INKTAG_NRF_V2_STORE_CLEAR,
    INKTAG_NRF_V2_STORE_CLEAR_USER,
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
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 9
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
#define EPD_V2_INPUT_BUSY() false
#endif
#if INKTAG_NRF_TEST_ENABLED && INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 10
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static int16_t m_v2_timezone_minutes;
static inktag_display_settings_t m_v2_display_settings;
static inktag_display_settings_t m_v2_pending_display_settings;
static gui_countdown_t m_v2_countdown_state;
static uint8_t m_v2_rx_packet[BLE_EPD_MAX_DATA_LEN];
static uint16_t m_v2_rx_length;
static bool m_v2_rx_pending;
/* One existing mailbox has an execution lease, not a second queue. Flash
 * waits may poll SoftDevice IRQs; they must not overwrite the active packet. */
static bool m_v2_rx_processing;
/* Display callbacks service bounded read-only requests from the existing
 * mailbox, without recursive scheduler dispatch or an IRQ ADC/Flash path. */
static ble_epd_t* volatile m_v2_display_read_service;
/* One ordinary identity holder also serves password AUTH in a base build.
 * The base initializer cannot read grants, install tokens or run activation IO. */
static inktag_activation_t m_v2_activation;
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 123 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
/* Base-only capacity is selected explicitly in the common quota build.
 * NULL activation still fails closed for every other Pro feature. */
#define INKTAG_V2_ACCESS_ACTIVATION ((const inktag_activation_t *)0)
#endif

static inktag_status_v2_t epd_screen_control_access(
    const epd_screen_config_t *screen, bool backend) {
    return inktag_access_check(INKTAG_V2_ACCESS_ACTIVATION,
        screen != NULL && epd_screen_is_e6(screen->driver_id, screen->panel_recipe) ?
            INKTAG_PRODUCT_FEATURE_E6_CONTROL : INKTAG_PRODUCT_FEATURE_BASIC,
        backend, true, true);
}

static inktag_status_v2_t epd_current_control_access(const ble_epd_t *p_epd) {
    epd_screen_t resolved;
    bool backend = p_epd != NULL && inktag_nrf_io_current() != NULL &&
        inktag_nrf_io_busy_known() && p_epd->config.mosi_pin != INKTAG_PIN_UNUSED &&
        epd_screen_resolve(&p_epd->screen_config, &resolved) == EPD_SCREEN_STATUS_OK;
    return epd_screen_control_access(p_epd == NULL ? NULL : &p_epd->screen_config, backend);
}

/* One shared-policy decision at RX and again at leased dispatch. Passive
 * reads, pin configuration, offline storage and mandatory cleanup remain available. */
static inktag_status_v2_t epd_v2_panel_request_access(ble_epd_t *p_epd,
    uint8_t opcode, const uint8_t *payload, uint16_t length) {
    if (opcode == INKTAG_CMD_SET_SCREEN_PROFILE) {
        epd_screen_config_t screen;
        epd_screen_t resolved;
        if (epd_screen_config_decode_exact(payload, length, &screen) != EPD_SCREEN_STATUS_OK)
            return INKTAG_STATUS_INCOMPATIBLE;
        return epd_screen_control_access(&screen,
            epd_screen_resolve(&screen, &resolved) == EPD_SCREEN_STATUS_OK);
    }
    if (opcode == INKTAG_CMD_TRANSFER_WRITE || opcode == INKTAG_CMD_TRANSFER_COMMIT) {
        if (!m_v2_transfer.active || m_v2_transfer.descriptor.object_type !=
                INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) return INKTAG_STATUS_OK;
    } else if (opcode == INKTAG_CMD_TRANSFER_BEGIN) {
        if (length == 0u || payload[0] != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME)
            return INKTAG_STATUS_OK;
    } else if (opcode == INKTAG_CMD_DISPLAY_ACTION) {
        if (length == 0u || (payload[0] != INKTAG_DISPLAY_ACTION_NEXT &&
                payload[0] != INKTAG_DISPLAY_ACTION_RESUME &&
                payload[0] != INKTAG_DISPLAY_ACTION_REFRESH &&
                payload[0] != INKTAG_DISPLAY_ACTION_GHOST_CLEAN)) return INKTAG_STATUS_OK;
#if INKTAG_NRF_TEST_ENABLED
    } else if (opcode == INKTAG_CMD_RUN_TEST) {
        if (length != 0u && (payload[0] == INKTAG_TEST_TIME_SHIFT ||
                payload[0] == INKTAG_TEST_TIME_SET))
            return epd_current_control_access(p_epd);
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 11
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
        return INKTAG_STATUS_OK;
#endif
#endif
    } else if (opcode != INKTAG_CMD_SET_DISPLAY_MODE
#if INKTAG_NRF_PASSWORD_ENABLED
               && opcode != INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO
#endif
              ) return INKTAG_STATUS_OK;
    return epd_current_control_access(p_epd);
}
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 197 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
APP_TIMER_DEF(m_v2_system_action_timer_id);
static volatile uint8_t m_v2_system_action_pending;
static bool m_v2_system_action_scheduled;
#if INKTAG_NRF_INDICATOR_ENABLED
static inktag_status_config_t m_v2_indicator_config;
static inktag_status_runtime_t m_v2_indicator_runtime;
APP_TIMER_DEF(m_v2_indicator_timer_id);
static bool m_v2_indicator_timer_created;
static bool m_v2_indicator_timer_running;
#endif

#if INKTAG_NRF_PASSWORD_ENABLED
_Static_assert(NRF_SDH_BLE_PERIPHERAL_LINK_COUNT == 1,
               "password session requires one active central");
_Static_assert(INKTAG_GUI_RENDER_BUFFER_SIZE >= 480u,
               "password borrows existing transfer scratch");
static inktag_device_password_t m_v2_password;
static inktag_device_password_config_t m_v2_password_config;
static uint32_t m_v2_password_retry_ms;
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
    if (m_v2_system_action_scheduled || m_v2_system_action_pending != 0u)
        return false;
    if (app_timer_start(m_v2_system_action_timer_id,
                        INKTAG_NRF_V2_SYSTEM_ACTION_DELAY_TICKS,
                        (void*)(uintptr_t)action) != NRF_SUCCESS) return false;
    m_v2_system_action_scheduled = true;
    return true;
}

static void epd_v2_process_system_action(void) {
    uint8_t action = m_v2_system_action_pending;
    if (action == 0u) return;
    m_v2_system_action_pending = 0u;
    m_v2_system_action_scheduled = false;
    if (action == INKTAG_SYSTEM_ACTION_RESET) {
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
    }
}

#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 253 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
static bool epd_v2_activation_available(void) {
    return m_v2_activation.identity_available;
}
#endif

static bool epd_v2_activation_busy(void) {
    return
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 259 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        m_epd_operation_active ||
        m_epd_operation_pending || m_epd_io_busy || m_v2_transfer.active ||
        m_v2_system_action_scheduled || m_v2_system_action_pending != 0u ||
        EPD_V2_STORE_BUSY() || EPD_V2_INPUT_BUSY()
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        || inktag_image_store_busy()
#endif
        || inktag_nrf_record_store_pending(INKTAG_NRF_RECORD_ACTIVATION) ||
        inktag_nrf_record_store_pending(INKTAG_NRF_RECORD_HEALTH);
}

bool app_time_checkpoint_ready(void) {
    // Reuse the production vendor queue/display/transfer/maintenance predicate.
    return !epd_v2_activation_busy();
}
#line 270 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"

#if INKTAG_NRF_PASSWORD_ENABLED
/* Boot/reconciliation must retire old secret generations, not merely choose
 * the newest valid page. Uncertain IO/erase leaves control fail-closed. */
static void epd_v2_password_load(void) {
    uint8_t modules;
    uint16_t offset, length;
    (void)inktag_nrf_password_load(m_gui_render_buffer, &m_v2_password_config,
                                  &modules, &offset, &length);
    if (m_v2_password_config.valid &&
        !inktag_nrf_config_store_scrub_inactive())
        m_v2_password_config.valid = false;
    inktag_nrf_password_wipe(m_gui_render_buffer, 420u);
    inktag_device_password_session_clear(&m_v2_password);
    m_v2_password_retry_ms = app_monotonic_now_ms() + 5000u;
}

static void epd_v2_password_init(void) {
    inktag_device_password_init(&m_v2_password, app_monotonic_now_ms());
    epd_v2_password_load();
}

bool inktag_nrf_password_recovery_allowed(void) {
    return inktag_device_password_allowed(&m_v2_password,
                                          &m_v2_password_config) &&
           !epd_v2_activation_busy();
}
#endif

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
    m_display_state.reserved = (uint16_t)((m_display_state.reserved &
        (uint16_t)~(EPD_DISPLAY_STATE_USER_FLAGS_MASK |
                   EPD_DISPLAY_STATE_PRO_FLAGS_MASK |
                   EPD_DISPLAY_STATE_WEEK_START_MASK)) |
        m_v2_display_settings.flags |
        ((uint16_t)m_v2_display_settings.week_start << EPD_DISPLAY_STATE_WEEK_START_SHIFT));
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
 * Pro-capable targets reject an unauthorized hide, while base-only
 * targets accept the calendar fields but reject the Pro-only hide bit. */
#if defined(NRF52811_XXAA) && defined(__GNUC__)
/* Admission scratch must die before executing a different command. */
__attribute__((noinline, noclone))
#endif
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
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 362 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
    return (m_v2_pending_display_settings.flags &
            EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN) != 0u ?
           INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_OK;
#endif
}

static uint8_t epd_v2_display_settings_flags(bool include_pro_flags) {
    uint8_t flags = (uint8_t)(m_display_state.reserved &
                              EPD_DISPLAY_STATE_USER_FLAGS_MASK);
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 376 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
    (void)include_pro_flags;
#endif
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

#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 488 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif

static inktag_status_v2_t epd_v2_button_configuration_access(void) {
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 6
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 498 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
    return INKTAG_STATUS_UNSUPPORTED;
#endif
}

static inktag_status_v2_t epd_v2_input_action_access(uint8_t source,
                                                     uint8_t action) {
    if (INKTAG_BOARD_BUTTON_PRESENT == 0)
        return INKTAG_STATUS_UNSUPPORTED;
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 7
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 510 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
    inktag_button_bindings_t stock;
    inktag_button_bindings_default(&stock);
    const inktag_input_binding_t *binding = inktag_button_binding(&stock,
        (inktag_button_gesture_t)(source - INKTAG_INPUT_SOURCE_BUTTON_SINGLE +
                                 INKTAG_BUTTON_GESTURE_SINGLE));
    if (source < INKTAG_INPUT_SOURCE_BUTTON_SINGLE ||
        source > INKTAG_INPUT_SOURCE_BUTTON_LONG || !binding ||
        action != binding->action) return INKTAG_STATUS_UNSUPPORTED;
    return inktag_access_input_action(INKTAG_V2_ACCESS_ACTIVATION, source, action);
#endif
}

#if INKTAG_NRF_INDICATOR_ENABLED
static void epd_v2_indicator_apply(void* context, uint8_t color_mask) {
    (void)context;
    EPD_LED_SetEnabled(inktag_status_locate_active(&m_v2_indicator_runtime) ||
        m_v2_indicator_runtime.config.policy != INKTAG_STATUS_POLICY_OFF);
    if (color_mask != INKTAG_STATUS_COLOR_OFF) EPD_LED_ON();
    else EPD_LED_OFF();
}

static void epd_v2_indicator_timer_handler(void* context) {
    /* RTC wake only: the ordinary main/EPD maintenance loop owns rendering
     * the common state. No GPIO/state-machine reentry from this timer IRQ. */
    (void)context;
}

static void epd_v2_indicator_timer_update(void) {
    bool needed = inktag_status_needs_periodic_tick(&m_v2_indicator_runtime,
                                                   app_monotonic_now_ms());
    /* Expiry can leave the same OFF colour as the preceding locator phase,
     * so common need not call apply again. Restore the policy independently. */
    EPD_LED_SetEnabled(inktag_status_locate_active(&m_v2_indicator_runtime) ||
        m_v2_indicator_runtime.config.policy != INKTAG_STATUS_POLICY_OFF);
    if (!m_v2_indicator_timer_created) return;
    if (needed && !m_v2_indicator_timer_running) {
        m_v2_indicator_timer_running = app_timer_start(
            m_v2_indicator_timer_id, APP_TIMER_TICKS(40u), NULL) == NRF_SUCCESS;
    } else if (!needed && m_v2_indicator_timer_running) {
        if (app_timer_stop(m_v2_indicator_timer_id) == NRF_SUCCESS)
            m_v2_indicator_timer_running = false;
    }
}

static void epd_v2_indicator_tick(void) {
    inktag_status_tick(&m_v2_indicator_runtime, app_monotonic_now_ms());
    epd_v2_indicator_timer_update();
}

static void epd_v2_indicator_set_active(inktag_status_scene_t scene,
                                        bool active) {
    inktag_status_set_active(&m_v2_indicator_runtime, scene, active,
                             app_monotonic_now_ms());
    epd_v2_indicator_timer_update();
}

static void epd_v2_indicator_trigger(inktag_status_scene_t scene) {
    inktag_status_trigger(&m_v2_indicator_runtime, scene,
                          app_monotonic_now_ms());
    epd_v2_indicator_timer_update();
}

static void epd_v2_indicator_init(void) {
    uint16_t length = 0u;
    m_v2_indicator_timer_created = app_timer_create(&m_v2_indicator_timer_id,
        APP_TIMER_MODE_REPEATED, epd_v2_indicator_timer_handler) == NRF_SUCCESS;
    inktag_status_config_default(&m_v2_indicator_config);
    if (!inktag_nrf_config_store_read(
            INKTAG_NRF_CONFIG_INDICATOR, &m_v2_indicator_config,
            sizeof(m_v2_indicator_config), &length) ||
        length != sizeof(m_v2_indicator_config) ||
        !inktag_status_config_valid(&m_v2_indicator_config)) {
        inktag_status_config_default(&m_v2_indicator_config);
    }
    EPD_LED_SetEnabled(m_v2_indicator_config.policy !=
                       INKTAG_STATUS_POLICY_OFF);
    /* P0.18 is qualified only as one legacy status LED. Do not infer an RGB
     * channel ordering from unqualified neighboring pads. */
    inktag_status_init(&m_v2_indicator_runtime, &m_v2_indicator_config,
                       INKTAG_STATUS_COLOR_BLUE, epd_v2_indicator_apply,
                       NULL, app_monotonic_now_ms());
    epd_v2_indicator_trigger(INKTAG_STATUS_SCENE_STARTUP);
}
#endif

static bool epd_v2_command_requires_idle_display(uint8_t opcode) {
    switch (opcode) {
        case INKTAG_CMD_SET_SCREEN_PROFILE:
        case INKTAG_CMD_SET_IO_CONFIG:
        case INKTAG_CMD_SET_DISPLAY_MODE:
        case INKTAG_CMD_DISPLAY_ACTION:
        case INKTAG_CMD_TRANSFER_BEGIN:
        case INKTAG_CMD_DELETE_RESOURCE:
        case INKTAG_CMD_CLEAR_RESOURCES:
        case INKTAG_CMD_SYSTEM_ACTION:
        case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION:
        case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
            return true;
        default:
            return false;
    }
}

static bool epd_v2_command_supported_on_target(uint8_t opcode) {
    switch (opcode) {
        case INKTAG_CMD_GET_ADVERTISING_CONFIG:
        case INKTAG_CMD_SET_ADVERTISING_CONFIG:
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
            return true;
#else
            return false;
#endif
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
        case INKTAG_CMD_GET_GUI_ASSET_INFO:
            return INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED != 0;
#endif
        case INKTAG_CMD_GET_RESOURCE_INFO:
        case INKTAG_CMD_LIST_RESOURCES:
        case INKTAG_CMD_READ_RESOURCE:
        case INKTAG_CMD_DELETE_RESOURCE:
        case INKTAG_CMD_CLEAR_RESOURCES:
            return INKTAG_NRF_IMAGE_STORE_ENABLED != 0;
        case INKTAG_CMD_SET_RESOURCE_ALIAS:
        case INKTAG_CMD_SET_IMAGE_METADATA:
            return false; /* Retired IDs: name/type belong to V5 upload. */
        case INKTAG_CMD_GET_FIND_CAPABILITIES:
        case INKTAG_CMD_GET_FIND_CONFIG:
        case INKTAG_CMD_SET_FIND_CONFIG:
        case INKTAG_CMD_FIND_ACTION:
            return INKTAG_NRF_FIND_ENABLED != 0;
        case INKTAG_CMD_GET_FIND_OWNER_STATUS:
        case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION:
        case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
        case INKTAG_CMD_ABORT_FIND_OWNER_ROTATION:
            return INKTAG_NRF_FIND_ENABLED != 0;
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
#if !INKTAG_NRF_ACTIVATION_ENABLED
            return m_v2_activation.identity_available;
#endif
        case INKTAG_CMD_SET_ACTIVATION_CODE:
        case INKTAG_CMD_GET_ACTIVATION_REQUEST:
            return INKTAG_NRF_ACTIVATION_ENABLED != 0;
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
            return INKTAG_NRF_PASSWORD_ENABLED != 0;
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
            return INKTAG_BUILD_PRO && INKTAG_NRF_PASSWORD_ENABLED != 0;
        case INKTAG_CMD_GET_INDICATOR_CONFIG:
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
            return INKTAG_NRF_INDICATOR_ENABLED != 0;
        case INKTAG_CMD_LIST_TESTS:
        case INKTAG_CMD_RUN_TEST:
        case INKTAG_CMD_GET_TEST_STATUS:
            return INKTAG_NRF_TEST_ENABLED != 0;
        case INKTAG_CMD_GET_INPUT_CONFIG:
            return INKTAG_BOARD_INPUT_QUALIFIED != 0;
        case INKTAG_CMD_SET_INPUT_CONFIG:
            return INKTAG_BUILD_PRO && INKTAG_BOARD_INPUT_QUALIFIED != 0;
        case INKTAG_CMD_GET_IO_CONFIG:
        case INKTAG_CMD_SET_IO_CONFIG:
            return true;
        case INKTAG_CMD_GET_NFC_CAPABILITIES:
        case INKTAG_CMD_GET_NFC_STATUS:
        case INKTAG_CMD_GET_NFC_NDEF:
        case INKTAG_CMD_SET_NFC_NDEF:
        case INKTAG_CMD_GET_LAYOUT_CAPABILITIES:
        case INKTAG_CMD_VALIDATE_LAYOUT:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_GET_LAYOUT_STATUS:
        case INKTAG_CMD_GET_SECURITY_INFO:
        case INKTAG_CMD_OPEN_DEBUG_SESSION:
        case INKTAG_CMD_DEBUG_READ:
        case INKTAG_CMD_CLOSE_DEBUG_SESSION:
        case INKTAG_CMD_ABORT_DEBUG_OPERATION:
            return false;
        case INKTAG_CMD_DEBUG_EXECUTE:
            return false;
        default:
            return true;
    }
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

/* Address/ID is fixed; visibility follows actual physically stored records. */
static bool epd_v2_image_record_visible(ble_epd_t* p_epd,
                                        uint8_t wanted_id) {
    uint8_t ordinal;
    return inktag_image_store_ordinal(wanted_id, &ordinal) == INKTAG_IMAGE_STORE_OK &&
        inktag_access_image_record_visible(INKTAG_V2_ACCESS_ACTIVATION, ordinal,
            p_epd->screen_config.visible_width, p_epd->screen_config.visible_height);
}

static inktag_image_store_result_t epd_v2_visible_store_status(
    ble_epd_t* p_epd, const epd_screen_config_t* screen, uint32_t payload_size,
    inktag_image_store_status_t* status) {
    inktag_image_store_result_t result =
        inktag_image_store_status(screen, payload_size, status);
    uint16_t sector = 0u;
    if (result != INKTAG_IMAGE_STORE_OK ||
        inktag_access_is_pro(INKTAG_V2_ACCESS_ACTIVATION) ||
        inktag_access_default_image_record_limit(screen->visible_width,
            screen->visible_height) == UINT16_MAX)
        return result;
    status->saved_count = 0u;
    status->compatible_count = 0u;
    status->playable_count = 0u;
    status->incompatible_count = 0u;
    while (sector < inktag_image_store_fixed_slot_count()) {
        inktag_image_store_header_t header;
        result = inktag_image_store_get((uint8_t)sector, &header);
        if (result != INKTAG_IMAGE_STORE_OK &&
            result != INKTAG_IMAGE_STORE_NOT_FOUND) return result;
        if (result == INKTAG_IMAGE_STORE_OK) {
            if (epd_v2_image_record_visible(p_epd, (uint8_t)sector)) {
                status->saved_count++;
                if (epd_screen_config_matches(p_epd, header.screen_config)) {
                    status->compatible_count++;
                    if (header.content_type == INKTAG_IMAGE_CONTENT_IMAGE)
                        status->playable_count++;
                } else {
                    status->incompatible_count++;
                }
            }
        }
        ++sector;
    }
    return INKTAG_IMAGE_STORE_OK;
}

static inktag_status_v2_t epd_v2_image_create_access(
    ble_epd_t* p_epd, const epd_screen_config_t* screen, uint32_t payload_size,
    uint8_t replace_id) {
    inktag_image_store_status_t status;
    inktag_image_store_result_t result =
        epd_v2_visible_store_status(p_epd, screen, payload_size, &status);
    if (result != INKTAG_IMAGE_STORE_OK) return epd_v2_store_status(result);
    bool existing = false;
    uint8_t ordinal = 0u;
    if (replace_id != UINT8_MAX) {
        inktag_image_store_header_t old;
        if (replace_id >= inktag_image_store_fixed_slot_count())
            return INKTAG_STATUS_NOT_FOUND;
        result = inktag_image_store_get(replace_id, &old);
        if (result == INKTAG_IMAGE_STORE_OK) {
            existing = true;
            result = inktag_image_store_ordinal(replace_id, &ordinal);
        }
        if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NOT_FOUND)
            return epd_v2_store_status(result);
    }
    return inktag_access_image_write(
        INKTAG_V2_ACCESS_ACTIVATION, status.saved_count, existing, ordinal,
        existing || (status.required_sectors != 0u &&
            status.largest_free_run >= status.required_sectors),
        screen->visible_width, screen->visible_height);
}

static inktag_image_store_result_t epd_v2_clear_visible_resources(
    ble_epd_t* p_epd, bool incompatible_only) {
    /* Both editions use all physical records. Reuse the original bounded
     * store traversal; no count-sentinel-sized stack snapshot or new window. */
    return incompatible_only ?
        inktag_image_store_clear_incompatible(&p_epd->screen_config) :
        inktag_image_store_clear_all();
}
#endif

/* Keep payload-free replies on the four-register ABI. They carry the same
 * three bytes, own their IRQ-safe stack buffer and use the same SDK sender.
 * No framing, notification-accounting or retry behavior changes. */
__attribute__((noinline, noclone))
static bool epd_v2_send_empty_response(ble_epd_t* p_epd, uint8_t opcode,
    uint8_t transaction_id, inktag_status_v2_t status) {
    uint8_t packet[INKTAG_PROTOCOL_V2_HEADER_SIZE] = {
        opcode, transaction_id, (uint8_t)status};
    return ble_epd_string_send(p_epd, packet, sizeof(packet)) == NRF_SUCCESS;
}

__attribute__((noinline, noclone))
static bool epd_v2_send_response_payload(ble_epd_t* p_epd, uint32_t header,
                                 const uint8_t* payload,
                                 uint16_t payload_length) {
    const uint8_t opcode = (uint8_t)header;
    const uint8_t transaction_id = (uint8_t)(header >> 8);
    inktag_status_v2_t status = (inktag_status_v2_t)(uint8_t)(header >> 16);
    if (payload_length > BLE_EPD_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE ||
        (payload_length != 0u && payload == NULL)) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload = NULL;
        payload_length = 0u;
    }
    /* Bounded by the checked ATT payload limit, never shared with an IRQ.
     * Empty rejections need only the header, not another full-MTU stack frame. */
    uint8_t packet[INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length];
    packet[0] = opcode;
    packet[1] = transaction_id;
    packet[2] = (uint8_t)status;
    if (payload != NULL && payload_length != 0u)
        memcpy(&packet[INKTAG_PROTOCOL_V2_HEADER_SIZE], payload, payload_length);
    return ble_epd_string_send(
        p_epd, packet,
        (uint16_t)(INKTAG_PROTOCOL_V2_HEADER_SIZE + payload_length)) == NRF_SUCCESS;
}

__attribute__((always_inline))
static inline bool epd_v2_send_response(ble_epd_t* p_epd, uint8_t opcode,
    uint8_t transaction_id, inktag_status_v2_t status,
    const uint8_t* payload, uint16_t payload_length) {
    return payload_length == 0u ?
        epd_v2_send_empty_response(p_epd, opcode, transaction_id, status) :
        epd_v2_send_response_payload(p_epd,
            (uint32_t)opcode | ((uint32_t)transaction_id << 8) |
                ((uint32_t)(uint8_t)status << 16), payload, payload_length);
}

static void epd_v2_send_display_settings(ble_epd_t* p_epd, uint8_t opcode,
                                         uint8_t transaction_id) {
    uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                     INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    size_t response_length;
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 8
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 854 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
    m_v2_display_settings.flags = epd_v2_display_settings_flags(false);
#endif
    response_length = inktag_display_settings_encode(
        &m_v2_display_settings, response, sizeof(response));
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)response_length);
}

/* A private packed header keeps payload replies/events on the four-register
 * ABI. It is not a wire/storage struct and never escapes this call frame. */
__attribute__((noinline, noclone))
static bool epd_v2_send_event_payload(ble_epd_t* p_epd, uint32_t header,
                              const uint8_t* payload, uint16_t payload_length) {
    const uint8_t event = (uint8_t)header;
    inktag_status_v2_t status = (inktag_status_v2_t)(uint8_t)(header >> 8);
    const uint8_t origin_opcode = (uint8_t)(header >> 16);
    const uint8_t origin_transaction = (uint8_t)(header >> 24);
    const uint16_t prefix = INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u;
    if (payload_length != 0u && payload == NULL) {
        status = INKTAG_STATUS_INTERNAL_ERROR;
        payload_length = 0u;
    }
    if (payload_length > BLE_EPD_MAX_DATA_LEN - prefix)
        payload_length = (uint16_t)(BLE_EPD_MAX_DATA_LEN - prefix);
    uint8_t packet[prefix + payload_length];
    packet[0] = event;
    packet[1] = 0u;
    packet[2] = (uint8_t)status;
    packet[3] = origin_opcode;
    packet[4] = origin_transaction;
    if (payload != NULL && payload_length != 0u)
        memcpy(&packet[prefix], payload, payload_length);
    return ble_epd_string_send(p_epd, packet, (uint16_t)(prefix + payload_length)) == NRF_SUCCESS;
}

__attribute__((always_inline))
static inline bool epd_v2_send_event(ble_epd_t* p_epd, uint8_t event,
                              inktag_status_v2_t status, uint8_t origin_opcode,
                              uint8_t origin_transaction,
                              const uint8_t* payload, uint16_t payload_length) {
    return epd_v2_send_event_payload(p_epd,
        (uint32_t)event | ((uint32_t)(uint8_t)status << 8) |
            ((uint32_t)origin_opcode << 16) | ((uint32_t)origin_transaction << 24),
        payload, payload_length);
}

static void epd_v2_send_log(ble_epd_t* p_epd, const char* text) {
    if (text == NULL) return;
    epd_v2_send_event(p_epd, INKTAG_EVENT_LOG, INKTAG_STATUS_OK, 0u, 0u,
                      (const uint8_t*)text, (uint16_t)strlen(text));
}

static uint8_t epd_v2_current_display_mode(void) {
    if (!m_display_state_loaded) return INKTAG_DISPLAY_MODE_DEVICE_INFO;
    switch (m_display_state.mode) {
        case EPD_DISPLAY_STATE_CALENDAR: return INKTAG_DISPLAY_MODE_CALENDAR;
        case EPD_DISPLAY_STATE_CLOCK: return INKTAG_DISPLAY_MODE_CLOCK;
        case EPD_DISPLAY_STATE_CLEAR: return INKTAG_DISPLAY_MODE_CLEAR;
        case EPD_DISPLAY_STATE_SLIDESHOW: return INKTAG_DISPLAY_MODE_SLIDESHOW;
        case EPD_DISPLAY_STATE_DEVICE_INFO: return INKTAG_DISPLAY_MODE_DEVICE_INFO;
        case EPD_DISPLAY_STATE_PICTURE:
        default:
            return m_display_state.argument == EPD_DISPLAY_STATE_NO_SOURCE ?
                       INKTAG_DISPLAY_MODE_DIRECT_IMAGE :
                       INKTAG_DISPLAY_MODE_RESOURCE;
    }
}

/* Keep one copy of each transfer stage on Nordic targets. No wire/state change;
 * prevents LTO constant-opcode clones in the packet dispatcher. */
#if (defined(NRF52811_XXAA) || defined(NRF52832_XXAA)) && defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static void epd_v2_transfer_reset(ble_epd_t* p_epd, bool erase_store) {
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_protocol_font_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    if (m_v2_transfer.active) {
        uint8_t object = m_v2_transfer.descriptor.object_type;
        if (object == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            inktag_device_password_session_clear(&m_v2_password);
        if (object == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
            object == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR)
            inktag_nrf_password_wipe(m_gui_render_buffer, 480u);
    }
#endif
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
    if (m_v2_transfer.active &&
        m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        epd_release_picture(p_epd);
    }
#if INKTAG_NRF_INDICATOR_ENABLED
    if (m_v2_transfer.active)
        epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER, false);
#endif
    memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
    m_v2_write_cancel_requested = false;
    m_picture_io_active = false;
}

static void epd_v2_link_closed(ble_epd_t* p_epd) {
    /* Revoke the old central's completion, not background resource ownership. */
    m_epd_active_operation.v2_transaction = 0u;
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 974 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 12
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    inktag_device_password_session_clear(&m_v2_password);
    if (!m_v2_rx_processing)
        inktag_nrf_password_wipe(m_v2_rx_packet, sizeof(m_v2_rx_packet));
#endif
    m_v2_rx_pending = false;
    m_v2_display_read_service = NULL;
    if (m_v2_write_active) m_v2_write_cancel_requested = true;
    else if (m_v2_transfer.active) epd_v2_transfer_reset(p_epd, true);
    EPD_V2_STORE_RESET();
}

static void epd_v2_operation_complete(const nrf_epd_operation_t* operation,
                                      inktag_status_v2_t status) {
    uint8_t payload[2];
    uint16_t payload_length = 1u;
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 13
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    /* The worker passes its revocable active slot. Serialize its identity
     * with HVX so a reconnect cannot receive the previous central's terminal.
     * This event is at most seven bytes; no ADC/Flash/wait in this region. */
    CRITICAL_REGION_ENTER();
    if (operation != NULL && operation->origin == INKTAG_NRF_V2_OPERATION_BLE &&
        operation->v2_transaction != 0u) {
        payload[0] = operation->arg;
        if (operation->type == NRF_EPD_OP_GHOST_CLEAN) {
            payload[0] = INKTAG_DISPLAY_ACTION_GHOST_CLEAN;
            payload[1] = operation->arg;
            payload_length = 2u;
        }
        epd_v2_send_event(operation->p_epd, INKTAG_EVENT_COMPLETE, status,
                          operation->v2_opcode, operation->v2_transaction,
                          payload, payload_length);
    }
    CRITICAL_REGION_EXIT();
}

static void epd_v2_ghost_progress(uint8_t done, uint8_t total) {
    uint8_t payload[2] = {INKTAG_BUSY_PHASE_DISPLAY,
                          (uint8_t)(((uint16_t)done * 100u) / total)};
    m_ghost_progress = payload[1];
    if (m_epd_active_operation.origin != INKTAG_NRF_V2_OPERATION_BLE) return;
    epd_v2_send_event(m_epd_active_operation.p_epd, INKTAG_EVENT_PROGRESS,
        INKTAG_STATUS_OK, m_epd_active_operation.v2_opcode,
        m_epd_active_operation.v2_transaction, payload, sizeof(payload));
}

static bool epd_v2_schedule_operation(ble_epd_t* p_epd, uint8_t opcode,
                                      uint8_t transaction_id,
                                      nrf_epd_operation_type_t type,
                                      uint8_t arg,
                                      uint32_t operation_timestamp) {
    uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY, 0u};
    if (m_epd_operation_active || m_epd_operation_pending ||
        (m_picture_io_active && type != NRF_EPD_OP_REFRESH)) return false;
    if (!epd_schedule_operation(p_epd, type, arg, operation_timestamp)) return false;
    if (!m_epd_dispatch_scheduled) {
        m_epd_operation_pending = false;
        return false;
    }
    m_epd_pending_operation.origin = INKTAG_NRF_V2_OPERATION_BLE;
    m_epd_pending_operation.v2_opcode = opcode;
    m_epd_pending_operation.v2_transaction = transaction_id;
    epd_v2_send_event(p_epd, INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                      opcode, transaction_id, progress, sizeof(progress));
    return true;
}

#if INKTAG_NRF_PASSWORD_ENABLED
static void epd_v2_password_request(ble_epd_t* p_epd, uint8_t opcode,
    uint8_t transaction_id, uint8_t flags, const uint8_t* payload, uint16_t length) {
#if !INKTAG_BUILD_PRO
    if (opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO ||
        (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE && length == 1u &&
         payload[0] > INKTAG_PASSWORD_AUTH && payload[0] <= INKTAG_PASSWORD_DISABLE)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
            INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
    uint8_t response[INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES];
    uint16_t response_length = INKTAG_DEVICE_PASSWORD_STATUS_BYTES;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint32_t now = app_monotonic_now_ms();
    if (flags != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (!m_v2_password_config.valid) status = INKTAG_STATUS_PERSISTENCE_ERROR;
    else if (!epd_v2_activation_available()) status = INKTAG_STATUS_UNSUPPORTED;
    else if (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE) {
        if (length != 1u || payload[0] > INKTAG_PASSWORD_DISABLE)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!inktag_device_password_begin(&m_v2_password,
                &m_v2_password_config, now, payload[0],
                inktag_nrf_random_bytes, NULL, response))
            status = INKTAG_STATUS_DENIED;
        else response_length = INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES;
    } else if (opcode == INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD) {
        if (!inktag_device_password_authorize(&m_v2_password,
                &m_v2_password_config, now, m_v2_activation.identity.device_id,
                payload, length)) status = INKTAG_STATUS_DENIED;
    } else if (length != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    #if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_password_management_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    if (status == INKTAG_STATUS_OK &&
        response_length == INKTAG_DEVICE_PASSWORD_STATUS_BYTES &&
        !inktag_device_password_status(&m_v2_password, &m_v2_password_config,
                                       now, response))
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    epd_v2_send_response(p_epd, opcode, transaction_id, status,
        status == INKTAG_STATUS_OK ? response : NULL,
        status == INKTAG_STATUS_OK ? response_length : 0u);
    inktag_nrf_password_wipe(response, sizeof(response));
}
#endif

static uint32_t epd_v2_capabilities(void) {
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_DEVICE_INFO_QR |
                            INKTAG_CAPABILITY_DEVICE_LABEL |
                            INKTAG_CAPABILITY_OTA_RECOVERY;
    if (inktag_nrf_io_current()) capabilities |= INKTAG_CAPABILITY_PIN_CONFIG;
    if (INKTAG_TRANSFER_PACKBITS_ENABLED &&
        INKTAG_BOARD_TRANSFER_PACKBITS_QUALIFIED)
        capabilities |= INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE;
#if INKTAG_BUILD_PRO && INKTAG_NRF_PASSWORD_ENABLED && INKTAG_BOARD_PASSWORD_QUALIFIED
    if (m_v2_password_config.valid && epd_v2_activation_available())
        capabilities |= INKTAG_CAPABILITY_DEVICE_PASSWORD;
#endif
#if INKTAG_NRF_DEVICE_NAME_WRITABLE && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_ble_name_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS) && INKTAG_BUILD_PRO
    if (INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED)
        capabilities |= INKTAG_CAPABILITY_GUI_ASSET_UPDATE;
#endif
#if INKTAG_NRF_TEST_ENABLED
#if INKTAG_BOARD_INPUT_QUALIFIED
    capabilities |= INKTAG_CAPABILITY_TEST;
#elif INKTAG_NRF_INDICATOR_ENABLED
    if (m_v2_indicator_timer_created) capabilities |= INKTAG_CAPABILITY_TEST;
#endif
#endif
    const inktag_capability_gate_t telemetry = {
        true, true, INKTAG_BOARD_TELEMETRY_QUALIFIED != 0, true,
    };
    const inktag_capability_gate_t input = {
        true, true, INKTAG_BOARD_INPUT_QUALIFIED != 0,
        INKTAG_BOARD_BUTTON_PRESENT != 0 &&
            true,
    };
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    const inktag_capability_gate_t resources = {
        true, true, INKTAG_BOARD_IMAGE_STORE_QUALIFIED != 0 &&
            INKTAG_BOARD_IMAGE_INDEX_QUALIFIED != 0,
        inktag_image_store_supported(),
    };
#endif
    const inktag_capability_gate_t lifecycle = {
        true, true, INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
#if INKTAG_NRF_IMAGE_STORE_ENABLED && INKTAG_NRF_PASSWORD_ENABLED
        inktag_image_store_supported() && m_v2_password_config.valid,
#else
        false,
#endif
    };
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 9
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1165 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_INDICATOR_ENABLED
    const inktag_capability_gate_t indicator = {
        true, true, INKTAG_BOARD_HAS_STATUS_LED != 0,
        m_v2_indicator_timer_created,
    };
#endif
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1185 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    const inktag_capability_gate_t health = {
        true, true, INKTAG_BOARD_HEALTH_QUALIFIED != 0, true,
    };
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_TELEMETRY, &telemetry);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INPUT, &input);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_RESOURCES, &resources);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_SLIDESHOW, &resources);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_IMAGE_METADATA, &resources);
#endif
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE, &lifecycle);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE_V2, &lifecycle);
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 10
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1208 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_INDICATOR_ENABLED
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INDICATOR, &indicator);
#endif
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 6
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1219 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_HEALTH, &health);
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
    uint32_t capabilities = epd_v2_capabilities();
#if defined(INKTAG_NRF_CLOCK_PARTIAL_400X300) && INKTAG_NRF_CLOCK_PARTIAL_400X300
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_protocol_partial_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1246 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    inktag_write_u32_le(&payload[1], capabilities);
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
                          (1u << INKTAG_DISPLAY_ACTION_SLEEP) |
                          (1u << INKTAG_DISPLAY_ACTION_GHOST_CLEAN));
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
    /* Bytes17..23 remain the zero/absent settings extension. */
    inktag_write_u32_le(&payload[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
        INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION |
        (INKTAG_TRANSFER_LZO1X_ENABLED ?
         INKTAG_CAPABILITY_EXTENSION_TRANSFER_LZO1X_2000 : 0u));
    /* A default ATT MTU carries only the original17-byte prefix. Do not
     * turn a connection's initial capability read into a silent HVX failure.
     * The shared client treats a short response as extension mask zero. */
    uint16_t response_length = sizeof(payload) + INKTAG_PROTOCOL_V2_HEADER_SIZE <=
        p_epd->max_data_len ? sizeof(payload) : 17u;
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, response_length);
}

#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
static void epd_v2_send_gui_asset_info(ble_epd_t* p_epd, uint8_t opcode,
                                       uint8_t transaction_id) {
    inktag_nrf_gui_asset_info_t info;
    uint8_t response[INKTAG_GUI_ROLE_INFO_SIZE] = {0};
    if (!INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED ||
        !inktag_nrf_gui_asset_info(&info)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    inktag_gui_role_info_t wire_info = {
        .base = {INKTAG_RELEASE_FONT_INFO_VERSION, info.schema, info.active_bank, 1u, info.generation,
            info.total_size, INKTAG_BOARD_USER_FONT_SIZE, info.package_crc32},
        .supported_schemas = INKTAG_RELEASE_FONT_SCHEMA_MASK,
        .role_version = INKTAG_RELEASE_FONT_ROLE_VERSION, .flags = INKTAG_RELEASE_FONT_FLAGS,
    };
    if (!inktag_gui_role_encode_info(&wire_info, response, sizeof(response))) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
}
#endif

static uint8_t epd_v2_runtime_name(uint8_t name[EPD_DEVICE_NAME_MAX_BYTES]) {
    uint16_t length = EPD_DEVICE_NAME_MAX_BYTES;
    if (sd_ble_gap_device_name_get(name, &length) != NRF_SUCCESS) return 0u;
    return length > EPD_DEVICE_NAME_MAX_BYTES ? EPD_DEVICE_NAME_MAX_BYTES :
                                               (uint8_t)length;
}

static uint8_t epd_v2_display_name(char label[EPD_DEVICE_LABEL_MAX_BYTES + 1u]) {
    if (!inktag_device_display_name(label, EPD_DEVICE_LABEL_MAX_BYTES + 1u,
            m_device_settings.label, m_device_settings.label_length,
            app_device_id4())) return 0u;
    return (uint8_t)strlen(label);
}

static void epd_v2_send_device_info(ble_epd_t* p_epd, uint8_t opcode,
                                    uint8_t transaction_id) {
    uint8_t payload[96] = {0};
    uint8_t name[EPD_DEVICE_NAME_MAX_BYTES];
    uint8_t name_length = epd_v2_runtime_name(name);
    char label[EPD_DEVICE_LABEL_MAX_BYTES + 1u];
    uint8_t label_length = epd_v2_display_name(label);
    uint8_t board_length = (uint8_t)strlen(INKTAG_BOARD_FIRMWARE_FAMILY);
    uint32_t application_size = 0u, application_checksum = 0u;
    ble_gap_addr_t address;
    bool address_valid = sd_ble_gap_addr_get(&address) == NRF_SUCCESS;
    uint16_t offset;
    payload[0] = 1u;
    payload[1] = board_length;
    payload[2] = name_length;
    payload[3] = label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        uint8_t i;
        for (i = 0u; i < 6u; i++) payload[5u + i] = address.addr[5u - i];
    }
    inktag_write_u32_le(&payload[11],
        ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
        ((uint32_t)INKTAG_VERSION_MINOR << 8) | INKTAG_VERSION_PATCH);
    (void)inktag_nrf_application_info(&application_size, &application_checksum);
    inktag_write_u32_le(&payload[15], application_size);
    inktag_write_u32_le(&payload[19], application_checksum);
    offset = 23u;
    memcpy(&payload[offset], INKTAG_BOARD_FIRMWARE_FAMILY, board_length);
    offset = (uint16_t)(offset + board_length);
    memcpy(&payload[offset], name, name_length);
    offset = (uint16_t)(offset + name_length);
    memcpy(&payload[offset], label, label_length);
    offset = (uint16_t)(offset + label_length);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, offset);
}

static void epd_v2_send_status(ble_epd_t* p_epd, uint8_t opcode,
                               uint8_t transaction_id) {
    uint8_t payload[16u + INKTAG_TEMPERATURE_WIRE_SIZE] = {0};
    uint8_t flags = 0x04u;
    int8_t temperature = -127;
    int16_t mcu_sixteenths = 0, panel_sixteenths = 0;
    bool mcu_valid, panel_valid;
    uint16_t payload_length = 16u;
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
        if (m_epd_active_operation.type == NRF_EPD_OP_GHOST_CLEAN)
            progress = m_ghost_progress;
        active_opcode = m_epd_active_operation.v2_opcode;
        active_transaction = m_epd_active_operation.v2_transaction;
    } else if (m_epd_operation_pending &&
               m_epd_pending_operation.origin == INKTAG_NRF_V2_OPERATION_BLE) {
        active_opcode = m_epd_pending_operation.v2_opcode;
        active_transaction = m_epd_pending_operation.v2_transaction;
    }
    inktag_nrf_health_temperature_sample();
    if (inktag_nrf_health_effective_temperature(&temperature)) flags |= 0x08u;
    mcu_valid = inktag_nrf_health_mcu_temperature(&mcu_sixteenths);
    panel_valid = inktag_nrf_health_panel_temperature(&panel_sixteenths);
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
#endif
    inktag_write_u16_le(&payload[14], (uint16_t)m_v2_timezone_minutes);
    /* One sample feeds the base value and its source details; querying status
     * must not wake a panel or reuse a failed/stale source as valid. */
    if (p_epd->max_data_len >= sizeof(payload) + 3u) {
        inktag_temperature_wire_encode(&payload[16], mcu_valid, mcu_sixteenths,
                                      panel_valid, panel_sixteenths);
        payload_length = sizeof(payload);
    }
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, payload_length);
}

static void epd_v2_send_time(ble_epd_t* p_epd, uint8_t opcode,
                             uint8_t transaction_id) {
    uint8_t payload[7];
    uint32_t local = timestamp();
    uint32_t utc = local == 0u ? 0u : (uint32_t)((int64_t)local -
                              (int32_t)m_v2_timezone_minutes * 60);
    inktag_write_u32_le(payload, utc);
    inktag_write_u16_le(&payload[4], (uint16_t)m_v2_timezone_minutes);
    payload[6] = p_epd->config.week_start;
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
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
    char label[EPD_DEVICE_LABEL_MAX_BYTES + 1u];
    uint8_t label_length = epd_v2_display_name(label);
    ble_gap_addr_t address;
    bool address_valid = sd_ble_gap_addr_get(&address) == NRF_SUCCESS;
    uint16_t offset = 11u;
    payload[0] = INKTAG_SETTINGS_SECTION_IDENTITY;
    payload[1] = EPD_DEVICE_SETTINGS_VERSION;
    payload[2] = name_length;
    payload[3] = label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        uint8_t i;
        for (i = 0u; i < 6u; i++) payload[5u + i] = address.addr[5u - i];
    }
    memcpy(&payload[offset], name, name_length);
    offset = (uint16_t)(offset + name_length);
    memcpy(&payload[offset], label, label_length);
    offset = (uint16_t)(offset + label_length);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, offset);
}

#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static void epd_v2_send_input(ble_epd_t* p_epd, uint8_t opcode, uint8_t transaction_id) {
    if (!INKTAG_BOARD_BUTTON_PRESENT || !INKTAG_BOARD_INPUT_QUALIFIED) {
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#if INKTAG_BUILD_PRO
    if (ble_epd_button_custom_enabled()) {
        epd_v2_send_custom_input(p_epd, opcode, transaction_id);
        return;
    }
#endif
    uint8_t payload[10] = {1u, 0x07u};
    inktag_button_bindings_t stock;
    inktag_button_bindings_default(&stock);
    _Static_assert(sizeof(stock) == 6u, "button stock wire ABI");
    inktag_write_u16_le(payload + 2u,
        (1u << stock.single.action) | (1u << stock.double_click.action) |
        (1u << stock.long_press.action));
    memcpy(payload + 4u, &stock, sizeof(stock));
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static void epd_v2_send_resource_info(ble_epd_t* p_epd, uint8_t opcode,
                                      uint8_t transaction_id) {
    epd_screen_t screen;
    inktag_image_store_status_t status;
    inktag_image_store_result_t result;
    uint8_t payload[34] = {0};
    if (!inktag_image_store_supported() ||
        epd_screen_resolve(&p_epd->screen_config, &screen) != EPD_SCREEN_STATUS_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    result = epd_v2_visible_store_status(
        p_epd, &p_epd->screen_config, screen.frame_bytes, &status);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result), NULL, 0u);
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
    uint16_t allocatable = status.required_sectors ?
        status.free_sectors / status.required_sectors : 0u;
    uint16_t limit = inktag_access_default_image_record_limit(
        screen.config.visible_width, screen.config.visible_height);
    if (!inktag_access_is_pro(INKTAG_V2_ACCESS_ACTIVATION) && limit != UINT16_MAX) {
        uint16_t remaining = 0u;
        if (status.saved_count < limit) remaining = limit - status.saved_count;
        if (allocatable > remaining) allocatable = remaining;
    }
    inktag_write_u16_le(&payload[30], allocatable);
    inktag_write_u16_le(&payload[32], inktag_image_store_fixed_slot_count());
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
}

static void epd_v2_list_resource(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id,
                                 const uint8_t* payload, uint16_t length) {
    uint16_t sector = length == 1u && payload[0] != UINT8_MAX ?
                          (uint16_t)payload[0] + 1u : 0u;
    if (length > 1u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    for (; sector < inktag_image_store_fixed_slot_count(); sector++) {
        inktag_image_store_header_t header;
        epd_screen_config_t stored;
        uint8_t response[64] = {0};
        uint8_t name_length = 0u;
        bool decoded;
        inktag_image_store_result_t result =
            inktag_image_store_get((uint8_t)sector, &header);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND) continue;
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result), NULL, 0u);
            return;
        }
        if (!epd_v2_image_record_visible(p_epd, (uint8_t)sector)) continue;
        decoded = epd_screen_config_decode(header.screen_config,
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
        uint8_t done[34] = {1u, UINT8_MAX};
        uint8_t bytes = (uint8_t)((inktag_image_store_fixed_slot_count() + 7u) / 8u);
        inktag_image_store_result_t result = inktag_image_store_occupancy(done + 2u, 32u);
        epd_v2_send_response(p_epd, opcode, transaction_id, epd_v2_store_status(result),
                             result == INKTAG_IMAGE_STORE_OK ? done : NULL,
                             result == INKTAG_IMAGE_STORE_OK ? (uint16_t)(2u + bytes) : 0u);
    }
}

#if defined(NRF52811_XXAA) && defined(__GNUC__)
/* A read response must not live beneath transfer/COW validation. */
__attribute__((noinline, noclone))
#endif
static void epd_v2_read_resource(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id,
                                 const uint8_t* payload, uint16_t length) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    uint8_t response[BLE_EPD_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    uint32_t offset;
    uint16_t requested;
    if (length != 6u || payload[5] == 0u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result), NULL, 0u);
        return;
    }
    if (!epd_v2_image_record_visible(p_epd, payload[0])) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
    offset = inktag_read_u32_le(&payload[1]);
    requested = payload[5];
    if (requested > sizeof(response) - 9u)
        requested = (uint16_t)(sizeof(response) - 9u);
    if (offset >= header.payload_size) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
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

#if (defined(NRF52811_XXAA) || defined(NRF52832_XXAA)) && defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static void epd_v2_transfer_begin(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id,
                                  const uint8_t* payload, uint16_t length) {
    inktag_transfer_descriptor_t descriptor;
    epd_screen_t screen;
    uint8_t expected_segments;
    inktag_status_v2_t wire_status;
    if (m_v2_transfer.active || m_epd_operation_active ||
        m_epd_operation_pending || EPD_V2_STORE_BUSY()
#if INKTAG_NRF_PASSWORD_ENABLED
        || epd_v2_activation_busy()
#endif
    ) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    wire_status = inktag_transfer_descriptor_decode(payload, length, &descriptor);
    if (wire_status != INKTAG_STATUS_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             wire_status, NULL, 0u);
        return;
    }
    if ((descriptor.black_mask &
         (INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE |
          INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X)) != 0u &&
        descriptor.object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
        descriptor.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        if (length != 20u ||
            !inktag_device_info_qr_transfer_valid(&descriptor)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
    }
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_protocol_font_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
    else if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    #if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_password_management_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
    else {
        if (descriptor.object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            && descriptor.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE
#endif
        ) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            return;
        }
        if (epd_screen_resolve(&p_epd->screen_config, &screen) !=
                EPD_SCREEN_STATUS_OK) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
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
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
    }
    if (!inktag_nrf_health_operation_allowed(
            descriptor.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ?
                INKTAG_POWER_OPERATION_DISPLAY : INKTAG_POWER_OPERATION_FLASH,
            true)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
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
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_protocol_font_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    #if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_password_management_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    else if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        inktag_stored_image_v5_descriptor_t stored;
        epd_screen_config_t stored_screen;
        uint8_t id = UINT8_MAX;
        inktag_image_store_result_t result;
        wire_status = inktag_stored_image_v5_descriptor_decode(payload, length, &stored);
        if (wire_status != INKTAG_STATUS_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 wire_status, NULL, 0u);
            return;
        }
        if (stored.content_type > INKTAG_IMAGE_CONTENT_MAX ||
            epd_screen_config_decode(stored.screen_profile, EPD_SCREEN_WIRE_SIZE,
                &stored_screen) != EPD_SCREEN_STATUS_OK ||
            !epd_screen_config_matches(p_epd, stored.screen_profile)) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
        wire_status = epd_v2_image_create_access(
            p_epd, &stored_screen, descriptor.total_size, stored.replace_slot);
        if (wire_status != INKTAG_STATUS_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 wire_status, NULL, 0u);
            return;
        }
        epd_slideshow_stop(p_epd, false);
        if (stored.replace_slot != UINT8_MAX &&
            !inktag_image_store_replace_slot(stored.replace_slot)) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_BUSY, NULL, 0u);
            return;
        }
        result = inktag_image_store_begin(
            &stored_screen, descriptor.format, descriptor.segment_count,
            descriptor.black_mask &
                INKTAG_IMAGE_STORED_FLAGS_MASK,
            stored.content_type, descriptor.segment_size,
            descriptor.total_size, descriptor.expected_crc, timestamp(),
            stored.name, &id);
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result), NULL, 0u);
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
        wire_status = epd_current_control_access(p_epd);
        if (wire_status != INKTAG_STATUS_OK) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id, wire_status, NULL, 0u);
            return;
        }
        epd_slideshow_stop(p_epd, false);
        m_picture_io_active = true;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        m_pending_picture_store_id = UINT8_MAX;
#endif
        EPD_GPIO_Init();
        EPD_BoardPrepare();
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
        EPD_SPI_ClearError();
#endif
        p_epd->epd = epd_init(&p_epd->screen_config);
        epd_cache_initialized_temperature(p_epd->epd);
        if (p_epd->epd == NULL) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
        if (epd_uc8xxx_power_warning(p_epd->epd))
            epd_send_uc8xxx_power_error(p_epd);
        if (EPD_SPI_LastError() != NRF_SUCCESS) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_IO_ERROR, NULL, 0u);
            return;
        }
#endif
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
    ble_epd_t* epd;
    uint32_t offset;
    uint32_t crc;
    uint16_t connection;
    inktag_status_v2_t status;
    bool compressed;
} epd_v2_write_sink_t;

#if defined(NRF52811_XXAA) && defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static bool epd_v2_transfer_sink(void* context, uint8_t* data,
                                  uint16_t data_length) {
    epd_v2_write_sink_t* sink = context;
    ble_epd_t* p_epd = sink->epd;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint32_t next_crc;
    if (m_v2_write_cancel_requested) {
        sink->status = INKTAG_STATUS_CANCELLED;
        return false;
    }
    if (m_v2_transfer.descriptor.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        sink->status = epd_current_control_access(p_epd);
        if (sink->status != INKTAG_STATUS_OK) return false;
    }
    if (!m_v2_transfer.active || p_epd->conn_handle != sink->connection ||
        !p_epd->is_notification_enabled) {
        sink->status = INKTAG_STATUS_INVALID_STATE;
        return false;
    }
    /* Hash the decoded client bytes before the final-output overlay. Stored
     * bytes and their CRC remain raw; RLE is never a persistent format. */
    next_crc = inktag_crc32_update(sink->crc, data, data_length);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (m_v2_transfer.descriptor.object_type ==
        INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, false)) {
            sink->status = INKTAG_STATUS_LOW_POWER;
            return false;
        }
        do {
            status = epd_v2_store_status(
                inktag_image_store_append(data, data_length));
            if (status != INKTAG_STATUS_BUSY || !sink->compressed) break;
            /* append is all-or-nothing for this span. Drain only the Flash
             * backend, never queued product commands. SoftDevice events run
             * in IRQ context in this port, including disconnect/cancel. */
            inktag_image_store_process();
            {
                inktag_image_store_event_t event;
                if (inktag_image_store_take_event(&event)) {
                    status = event.type == INKTAG_IMAGE_STORE_EVENT_WRITE ?
                        epd_v2_store_status(event.result) :
                        INKTAG_STATUS_INTERNAL_ERROR;
                    break;
                }
            }
            app_feed_wdt();
            if (m_v2_write_cancel_requested) {
                status = INKTAG_STATUS_CANCELLED;
                break;
            }
            if (!m_v2_transfer.active ||
                p_epd->conn_handle != sink->connection ||
                !p_epd->is_notification_enabled) {
                status = INKTAG_STATUS_INVALID_STATE;
                break;
            }
            if (app_timer_cnt_diff_compute(app_timer_cnt_get(),
                    m_v2_transfer.last_activity_ticks) >=
                    INKTAG_NRF_V2_TRANSFER_LEASE_TICKS) {
                status = INKTAG_STATUS_TIMEOUT;
                break;
            }
            nrf_delay_ms(1u);
        } while (true);
    } else
#endif
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_protocol_font_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
    if (m_v2_transfer.descriptor.object_type ==
            INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        memcpy(&epd_v2_qr_staging()->data[sink->offset],
               data, data_length);
    }
#if INKTAG_NRF_PASSWORD_ENABLED
    #if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_password_management_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
    else if (p_epd->epd == NULL) {
        status = INKTAG_STATUS_INVALID_STATE;
    } else {
        uint16_t consumed = 0u;
        while (consumed < data_length) {
            status = epd_current_control_access(p_epd);
            if (status != INKTAG_STATUS_OK) break;
            uint32_t absolute = sink->offset + consumed;
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
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
        if (EPD_SPI_LastError() != NRF_SUCCESS) status = INKTAG_STATUS_IO_ERROR;
#endif
    }
    sink->status = status;
    if (status != INKTAG_STATUS_OK) return false;
    sink->crc = next_crc;
    sink->offset += data_length;
    app_feed_wdt();
    return true;
}

#if (defined(NRF52811_XXAA) || defined(NRF52832_XXAA)) && defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static void epd_v2_transfer_write(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id,
                                  const uint8_t* payload, uint16_t length) {
    uint32_t offset;
    uint16_t data_length;
    uint8_t codec = m_v2_transfer.descriptor.black_mask &
        (INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE |
         INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X);
    bool compressed = codec != 0u;
    bool accepted;
    epd_v2_write_sink_t sink;
    if (!m_v2_transfer.active ||
        transaction_id != m_v2_transfer.transaction_id) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    /* Also defend the write boundary: no disabled/combined codec can reach
     * workspace decode, CRC, Flash or panel writes even with stale state. */
    if (codec != 0u &&
        (!((INKTAG_TRANSFER_PACKBITS_ENABLED &&
            codec == INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) ||
           (INKTAG_TRANSFER_LZO1X_ENABLED &&
            codec == INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X)) ||
         (m_v2_transfer.descriptor.object_type !=
              INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
          m_v2_transfer.descriptor.object_type !=
              INKTAG_TRANSFER_OBJECT_STORED_IMAGE))) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    offset = length >= 4u ? inktag_read_u32_le(payload) : UINT32_MAX;
    data_length = compressed ?
        (length > 6u ? inktag_read_u16_le(&payload[4]) : 0u) :
        (length > 4u ? (uint16_t)(length - 4u) : 0u);
    if (m_v2_transfer.written > m_v2_transfer.descriptor.total_size ||
        data_length == 0u || offset != m_v2_transfer.written ||
        data_length > m_v2_transfer.descriptor.total_size -
                          m_v2_transfer.written
#if INKTAG_TRANSFER_PACKBITS_ENABLED
        || (codec == INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE &&
            (length - 6u > UINT8_MAX ||
            !inktag_packbits_validate_block(&payload[6],
                (uint8_t)(length - 6u), data_length)))
#endif
    ) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
#if INKTAG_TRANSFER_LZO1X_ENABLED
    if (codec == INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X) {
        size_t produced;
        /* Image transfer owns the existing render workspace. Decode and
         * validate the complete block before any persistent/output mutation. */
        if (length <= 6u || data_length > 2000u ||
            data_length > sizeof(m_gui_render_buffer) ||
            !inktag_lzo1x_decode(payload + 6u, length - 6u,
                m_gui_render_buffer, data_length, &produced) ||
            produced != data_length) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
    }
#endif
    sink = (epd_v2_write_sink_t){
        p_epd, offset, m_v2_transfer.running_crc, p_epd->conn_handle,
        INKTAG_STATUS_OK, compressed,
    };
    m_v2_write_cancel_requested = false;
    /* Disconnect must not park/release the shared panel/Flash bus from IRQ
     * context while this sink owns an in-flight write. */
    m_epd_io_busy = true;
    m_v2_write_active = true;
    accepted = true;
#if INKTAG_TRANSFER_LZO1X_ENABLED
    if (codec == INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X) {
        for (uint16_t used = 0u; used < data_length && accepted;) {
            uint16_t chunk = data_length - used;
            /* Fit the existing all-or-nothing Flash append span. */
            if (chunk > 128u) chunk = 128u;
            accepted = epd_v2_transfer_sink(&sink,
                m_gui_render_buffer + used, chunk);
            used += chunk;
        }
    } else
#endif
#if INKTAG_TRANSFER_PACKBITS_ENABLED
    if (compressed) {
        accepted =
        inktag_packbits_stream_block(&payload[6], (uint8_t)(length - 6u),
            data_length, epd_v2_transfer_sink, &sink);
    } else
#endif
    accepted =
        epd_v2_transfer_sink(&sink, (uint8_t*)&payload[4], data_length);
    m_v2_write_active = false;
    m_epd_io_busy = false;
    if (m_v2_write_cancel_requested ||
        p_epd->conn_handle != sink.connection ||
        !p_epd->is_notification_enabled) {
        accepted = false;
        sink.status = INKTAG_STATUS_CANCELLED;
    }
    if (!accepted) {
        /* A compressed block can have emitted earlier spans. Do not permit
         * whole-block retry after partial progress, even on backend BUSY. */
        if (compressed || sink.status != INKTAG_STATUS_BUSY)
            epd_v2_transfer_reset(p_epd, true);
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             sink.status, NULL, 0u);
        return;
    }
    m_v2_transfer.running_crc = sink.crc;
    m_v2_transfer.written = sink.offset;
    m_v2_transfer.last_activity_ticks = app_timer_cnt_get();
    {
        uint8_t response[4];
        inktag_write_u32_le(response, m_v2_transfer.written);
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
    }
}

#if (defined(NRF52811_XXAA) || defined(NRF52832_XXAA)) && defined(__GNUC__)
__attribute__((noinline, noclone))
#endif
static void epd_v2_transfer_commit(ble_epd_t* p_epd, uint8_t opcode,
                                   uint8_t transaction_id) {
    uint8_t object_type;
    if (!m_v2_transfer.active ||
        transaction_id != m_v2_transfer.transaction_id ||
        m_v2_transfer.written != m_v2_transfer.descriptor.total_size) {
#if INKTAG_NRF_PASSWORD_ENABLED
        if (m_v2_transfer.active && m_v2_transfer.descriptor.object_type ==
                INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            epd_v2_transfer_reset(p_epd, false);
#endif
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    if ((m_v2_transfer.running_crc ^ 0xFFFFFFFFu) !=
        m_v2_transfer.descriptor.expected_crc) {
        epd_v2_transfer_reset(p_epd, true);
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_CHECKSUM_ERROR, NULL, 0u);
        return;
    }
    object_type = m_v2_transfer.descriptor.object_type;
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_protocol_font_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        epd_v2_transfer_reset(p_epd, false);
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    #if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_password_management_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        epd_device_info_qr_t* qr = epd_v2_qr_staging();
#if INKTAG_NRF_PASSWORD_ENABLED
        uint16_t qr_record_length;
        inktag_status_v2_t status = INKTAG_STATUS_OK;
        if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
            status = INKTAG_STATUS_LOW_POWER;
        else if ((uint16_t)(qr->length + (m_v2_password_config.enabled ? 73u : 8u)) > 420u)
            status = INKTAG_STATUS_NO_SPACE;
        else if (!inktag_device_password_store_qr(&m_v2_password_config,
                qr->modules, qr->data, qr->length, m_gui_render_buffer,
                &qr_record_length))
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!inktag_nrf_password_commit(m_gui_render_buffer, qr_record_length,
                                             &m_v2_password_config)) {
            inktag_device_password_session_clear(&m_v2_password);
            status = INKTAG_STATUS_PERSISTENCE_ERROR;
        }
        if (status == INKTAG_STATUS_OK &&
            !inktag_nrf_config_store_inactive_erased()) {
            m_v2_password_config.valid = false;
            inktag_device_password_session_clear(&m_v2_password);
            status = INKTAG_STATUS_PERSISTENCE_ERROR;
        }
        epd_v2_transfer_reset(p_epd, false);
        epd_v2_send_response(p_epd, opcode, transaction_id, status,
            status == INKTAG_STATUS_OK ? (const uint8_t[]){0u} : NULL,
            status == INKTAG_STATUS_OK ? 1u : 0u);
        return;
#else
        uint16_t qr_record_length =
            (uint16_t)(offsetof(epd_device_info_qr_t, data) + qr->length);
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_DEVICE_INFO_QR, qr, qr_record_length)) {
            inktag_status_v2_t status =
                inktag_nrf_config_store_last_error() == NRF_ERROR_FORBIDDEN ?
                    INKTAG_STATUS_LOW_POWER : INKTAG_STATUS_PERSISTENCE_ERROR;
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id, status,
                                 NULL, 0u);
            return;
        }
        memcpy(&m_device_info_qr[0], qr, qr_record_length);
#endif
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    else if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            epd_v2_transfer_reset(p_epd, true);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        inktag_image_store_result_t result = inktag_image_store_commit();
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_transfer_reset(p_epd, true);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result), NULL, 0u);
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
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        if (!epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       NRF_EPD_OP_REFRESH, 0u, timestamp())) {
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_BUSY, NULL, 0u);
            return;
        }
    }
#if INKTAG_NRF_INDICATOR_ENABLED
    epd_v2_indicator_set_active(INKTAG_STATUS_SCENE_TRANSFER, false);
#endif
    memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
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
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    mode = payload[0];
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_GUI,
            MODE_DEVICE_INFO, timestamp());
        if (scheduled) epd_slideshow_stop(p_epd, false);
    } else if (mode == INKTAG_DISPLAY_MODE_CALENDAR ||
               mode == INKTAG_DISPLAY_MODE_CLOCK) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_GUI,
            mode == INKTAG_DISPLAY_MODE_CALENDAR ? MODE_CALENDAR : MODE_CLOCK,
            timestamp());
        if (scheduled) epd_slideshow_stop(p_epd, false);
    } else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        scheduled = epd_v2_schedule_operation(
            p_epd, opcode, transaction_id, NRF_EPD_OP_CLEAR,
            length > 1u ? (payload[1] != 0u) : 1u, timestamp());
        if (scheduled) epd_slideshow_stop(p_epd, false);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        scheduled = length == 2u &&
            epd_stored_image_replayable(p_epd, payload[1], false) &&
            epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                      NRF_EPD_OP_IMAGE_SHOW, payload[1],
                                      timestamp());
        if (scheduled && m_slideshow_active) {
            m_slideshow_next_at = timestamp() + m_slideshow_interval;
        }
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint32_t interval = length == 5u ?
                                inktag_read_u32_le(&payload[1]) : 3600u;
        uint8_t first = UINT8_MAX;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        scheduled = epd_slideshow_select_next(p_epd, UINT8_MAX, &first) &&
            epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                      NRF_EPD_OP_IMAGE_SHOW, first, timestamp());
        if (scheduled) {
            m_slideshow_interval = interval;
            m_slideshow_active = true;
            m_slideshow_next_at = timestamp() + interval;
        }
#endif
    } else if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        failure = INKTAG_STATUS_INVALID_STATE;
    } else {
        failure = INKTAG_STATUS_UNSUPPORTED;
    }
    epd_v2_send_response(p_epd, opcode, transaction_id,
                         scheduled ? INKTAG_STATUS_OK : failure, NULL, 0u);
}

static void epd_v2_display_action(ble_epd_t* p_epd, uint8_t opcode,
                                  uint8_t transaction_id, uint8_t flags,
                                  const uint8_t* payload, uint16_t length) {
    bool ok;
    if (length > 0u && payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN) {
        inktag_status_v2_t status;
        if (length != 2u || payload[1] < 1u || payload[1] > 10u)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if ((flags & INKTAG_REQUEST_FLAG_CONFIRM) == 0u)
            status = INKTAG_STATUS_DENIED;
        else if (m_epd_operation_active || m_epd_operation_pending || m_epd_io_busy ||
                 m_v2_transfer.active || EPD_V2_STORE_BUSY())
            status = INKTAG_STATUS_BUSY;
        else if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true))
            status = INKTAG_STATUS_LOW_POWER;
        else {
            ok = epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                NRF_EPD_OP_GHOST_CLEAN, payload[1], timestamp());
            if (ok) epd_slideshow_stop(p_epd, false);
            status = ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY;
        }
        epd_v2_send_response(p_epd, opcode, transaction_id, status, NULL, 0u);
        return;
    }
    if (length != 1u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
    } else if (m_epd_operation_active || m_epd_operation_pending ||
               m_v2_transfer.active || EPD_V2_STORE_BUSY()) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
    } else if ((payload[0] == INKTAG_DISPLAY_ACTION_NEXT ||
                payload[0] == INKTAG_DISPLAY_ACTION_RESUME ||
                payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) &&
               !inktag_nrf_health_operation_allowed(
                   INKTAG_POWER_OPERATION_DISPLAY, true)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT) {
        nrf_epd_operation_type_t operation;
        uint8_t argument;
        bool slideshow_step;
        ok = epd_page_cycle_resolve(p_epd, false, &operation, &argument,
                                    &slideshow_step) &&
             epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       operation, argument, timestamp());
        if (ok) epd_page_cycle_scheduled(p_epd, slideshow_step);
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             ok ? INKTAG_STATUS_OK : INKTAG_STATUS_NOT_FOUND,
                             NULL, 0u);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && m_slideshow_active) {
        uint8_t current = m_slideshow_current_id;
        epd_slideshow_stop(p_epd, false);
        if (current != UINT8_MAX)
            epd_record_display_state(p_epd, EPD_DISPLAY_STATE_PICTURE,
                                     current, 0u);
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
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
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
#endif
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        ok = epd_v2_schedule_operation(p_epd, opcode, transaction_id,
                                       NRF_EPD_OP_REFRESH, 0u, timestamp());
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY,
                             NULL, 0u);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        if (p_epd->epd != NULL && EPD_BoardIsPrepared())
            p_epd->epd->drv->sleep(p_epd->epd);
        EPD_BoardRelease();
        EPD_GPIO_Uninit();
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
    } else {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE, NULL, 0u);
    }
}

/* IRQ-safe RAM requests only. The scheduled worker owns cleanup and the
 * original BLE terminal event, including work cancelled before dispatch. */
static void epd_v2_cancel_display(ble_epd_t* p_epd) {
    CRITICAL_REGION_ENTER();
    epd_slideshow_stop(p_epd, false);
    m_native_timer_update_pending = false;
    if (m_epd_operation_pending)
        m_epd_pending_operation.background |= NRF_EPD_OPERATION_CANCELLED;
    if (m_epd_operation_active) m_epd_cancel_requested = true;
    CRITICAL_REGION_EXIT();
}

static void epd_v2_cancel(ble_epd_t* p_epd, uint8_t opcode,
                          uint8_t transaction_id, const uint8_t* payload,
                          uint16_t length) {
#if INKTAG_NRF_TEST_ENABLED && INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 14
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    if (length != 0u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
            length == 2u && payload[0] == INKTAG_CMD_RUN_TEST ?
                INKTAG_STATUS_NOT_FOUND : INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (m_v2_transfer.active) epd_v2_transfer_reset(p_epd, true);
    epd_v2_cancel_display(p_epd);
    epd_v2_send_response(p_epd, opcode, transaction_id,
                         INKTAG_STATUS_OK, NULL, 0u);
}

static void epd_v2_get_settings(ble_epd_t* p_epd, uint8_t opcode,
                                uint8_t transaction_id,
                                const uint8_t* payload, uint16_t length) {
    if (length != 1u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        epd_v2_send_identity(p_epd, opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        epd_v2_send_display_settings(p_epd, opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        uint8_t report[INKTAG_CLOCK_RATE_REPORT_BYTES];
        inktag_clock_rate_encode(app_clock_rate_ppm(), app_clock_rate_default_ppm(), report);
        epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                             report, sizeof(report));
    } else {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

static void epd_v2_set_settings(ble_epd_t* p_epd, uint8_t opcode,
                                uint8_t transaction_id,
                                const uint8_t* payload, uint16_t length) {
    if (length != 0u && payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        int16_t ppm;
        if (!inktag_clock_rate_decode(payload, length, &ppm) ||
            !app_clock_rate_set(ppm)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        } else {
            uint8_t report[INKTAG_CLOCK_RATE_REPORT_BYTES];
            inktag_clock_rate_encode(app_clock_rate_ppm(), app_clock_rate_default_ppm(), report);
            epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                                 report, sizeof(report));
        }
        return;
    }
    if (length >= 2u && payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY &&
        payload[1] == EPD_DEVICE_SETTINGS_VERSION) {
        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        epd_device_settings_t next = m_device_settings;
        if (length != (uint16_t)(4u + name_length + label_length) ||
#if !INKTAG_NRF_DEVICE_NAME_WRITABLE
            name_length != 0u ||
#else
            !inktag_ble_device_name_write_valid(&payload[4], name_length) ||
#endif
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
#if INKTAG_NRF_DEVICE_NAME_WRITABLE && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_ble_name_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#if INKTAG_NRF_DEVICE_NAME_WRITABLE
        next.name_length = name_length;
#endif
        next.label_length = label_length;
#if INKTAG_NRF_DEVICE_NAME_WRITABLE
        memset(next.name, 0, sizeof(next.name));
#endif
        memset(next.label, 0, sizeof(next.label));
#if INKTAG_NRF_DEVICE_NAME_WRITABLE
        memcpy(next.name, &payload[4], name_length);
#endif
        memcpy(next.label, &payload[4u + name_length], label_length);
        if (!epd_device_settings_write(&next)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        m_device_settings = next;
        {
#if INKTAG_NRF_DEVICE_NAME_WRITABLE && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_ble_name_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
            uint8_t reboot_required[1] = {0u};
#endif
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
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 12
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 2653 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        uint16_t next_reserved = (uint16_t)(
            (m_display_state.reserved &
             (uint16_t)~writable_flags) |
            (m_v2_pending_display_settings.flags & writable_flags));
        if (!epd_v2_persist_display_settings(
                p_epd, &m_v2_pending_display_settings, next_reserved)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, payload, length);
    } else {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
#include "inktag_nrf_protocol_advertising_pro.inc"
#endif

static void epd_v2_system_action(ble_epd_t* p_epd, uint8_t opcode,
                                 uint8_t transaction_id, uint8_t flags,
                                 const uint8_t* payload, uint16_t length) {
    const inktag_lifecycle_capabilities_t capabilities = {
        .reset = true,
        .timed_sleep = false,
        .factory_reset = false,
        .ota_recovery = true,
        .reset_settings = INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        .clear_user_data = INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &capabilities, &plan);
    if (status != INKTAG_STATUS_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id, status, NULL, 0u);
        return;
    }
    if (m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        || inktag_image_store_busy()
#endif
    ) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
#if INKTAG_BOARD_LIFECYCLE_QUALIFIED
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) {
#if INKTAG_NRF_PASSWORD_ENABLED
        if (!inktag_device_password_consume_settings_reset(&m_v2_password,
                &m_v2_password_config, app_monotonic_now_ms())) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
#endif
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        epd_slideshow_stop(p_epd, false);
        bool saved = epd_display_settings_prepare() &&
                     inktag_nrf_config_store_reset_settings();
#if INKTAG_NRF_PASSWORD_ENABLED
        /* Reload even on false-after-commit. OFF requires trusted readback;
         * every attempt invalidates AUTH/W and no action is replayed. */
        epd_v2_password_load();
        memset(m_device_info_qr, 0, sizeof(m_device_info_qr));
        if (!m_v2_password_config.valid || m_v2_password_config.enabled)
            saved = false;
#endif
        if (saved) {
            epd_device_settings_default(&m_device_settings);
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
            epd_v2_advertising_load();
            app_advertising_activity();
#endif
            m_v2_timezone_minutes = 0;
            (void)app_clock_rate_set(app_clock_rate_default_ppm());
            set_timestamp(0u);
            p_epd->screen_config = (epd_screen_config_t)INKTAG_BOARD_DEFAULT_SCREEN_CONFIG;
            (void)inktag_nrf_factory_screen(&p_epd->screen_config);
            inktag_nrf_health_panel_temperature_record(false, 0);
            p_epd->epd = epd_find(&p_epd->screen_config);
            epd_v2_display_settings_init(p_epd);
            m_button_clear_return_valid = false;
            saved = app_device_name_reset();
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 7
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 2742 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        }
        epd_v2_send_response(p_epd, opcode, transaction_id,
            saved ? INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR,
            NULL, 0u);
        return;
    }
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        epd_slideshow_stop(p_epd, false);
        if (!epd_display_settings_prepare()) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        inktag_image_store_result_t result = inktag_image_store_clear_all();
        if (result != INKTAG_IMAGE_STORE_OK) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 epd_v2_store_status(result), NULL, 0u);
            return;
        }
        /* Reuse the existing async image lease. Metadata clears only after
         * the bounded image operation reports success; disconnect/cancel use
         * the same cleanup as ordinary resource commands. */
        m_v2_store_pending.type = INKTAG_NRF_V2_STORE_CLEAR_USER;
        m_v2_store_pending.opcode = opcode;
        m_v2_store_pending.transaction_id = transaction_id;
        return;
    }
#endif
#endif
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET) {
        (void)epd_time_state_write(timestamp(), m_v2_timezone_minutes);
        if (!epd_v2_schedule_system_action(plan.action)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_IO_ERROR, NULL, 0u);
            return;
        }
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY) {
        bool recovery_accepted = true;
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_OTA, true)) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
#if INKTAG_NRF_PASSWORD_ENABLED
        recovery_accepted = inktag_nrf_password_recovery_allowed();
#endif
        if (!recovery_accepted) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
        /* ACK may cause a normal client disconnect during the drain/power
         * sample. Continue only this accepted call, not its cleared session. */
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
        nrf_delay_ms(250u);
        if (!inktag_nrf_health_operation_allowed(
                INKTAG_POWER_OPERATION_OTA, true)) {
            epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR,
                              INKTAG_STATUS_LOW_POWER, opcode,
                              transaction_id, NULL, 0u);
            return;
        }
        if (inktag_nrf_buttonless_recovery_start_accepted(recovery_accepted) != NRF_SUCCESS) {
            epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR,
                              INKTAG_STATUS_IO_ERROR, opcode, transaction_id,
                              NULL, 0u);
        }
    } else {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

#if INKTAG_BOARD_INPUT_QUALIFIED
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 6
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
static void epd_v2_set_input(ble_epd_t* p_epd, uint8_t opcode, uint8_t transaction_id,
    const uint8_t* payload, uint16_t length) {
    (void)payload; (void)length;
    epd_v2_send_response(p_epd, opcode, transaction_id, epd_v2_button_configuration_access(), NULL, 0u);
}
#endif
#endif

#if INKTAG_NRF_IMAGE_STORE_ENABLED
static void epd_v2_delete_resource(ble_epd_t* p_epd, uint8_t opcode,
                                   uint8_t transaction_id,
                                   const uint8_t* payload, uint16_t length) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    if (length != 1u || m_v2_store_pending.type != 0u ||
        m_epd_operation_active || m_epd_operation_pending ||
        m_v2_transfer.active) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
            length == 1u ? INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST,
            NULL, 0u);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result), NULL, 0u);
        return;
    }
    if (!epd_v2_image_record_visible(p_epd, payload[0])) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    epd_slideshow_stop(p_epd, false);
    result = inktag_image_store_delete(payload[0]);
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result), NULL, 0u);
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
        epd_v2_send_response(p_epd, opcode, transaction_id,
            length == 1u ? INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST,
            NULL, 0u);
        return;
    }
    if (!inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    epd_slideshow_stop(p_epd, false);
    result = payload[0] == INKTAG_RESOURCE_CLEAR_ALL ?
                 epd_v2_clear_visible_resources(p_epd, false) :
             payload[0] == INKTAG_RESOURCE_CLEAR_INCOMPATIBLE ?
                 epd_v2_clear_visible_resources(p_epd, true) :
                 INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    if (result != INKTAG_IMAGE_STORE_OK) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             epd_v2_store_status(result), NULL, 0u);
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
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        if (!inktag_nrf_config_store_write(
                INKTAG_NRF_CONFIG_INDICATOR, &decoded, sizeof(decoded))) {
            epd_v2_send_response(p_epd, opcode, transaction_id,
                                 INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        m_v2_indicator_config = decoded;
        EPD_LED_SetEnabled(decoded.policy != INKTAG_STATUS_POLICY_OFF);
        inktag_status_set_config(&m_v2_indicator_runtime, &decoded,
                                 app_monotonic_now_ms());
        epd_v2_indicator_timer_update();
    } else if (length != 0u) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (!inktag_status_config_encode(&m_v2_indicator_config, response)) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INTERNAL_ERROR, NULL, 0u);
        return;
    }
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
}
#endif

#if INKTAG_NRF_TEST_ENABLED
static bool epd_v2_input_busy(void) {
    return m_epd_operation_active || m_epd_operation_pending || m_epd_io_busy ||
        m_v2_transfer.active || EPD_V2_STORE_BUSY() ||
        m_v2_system_action_scheduled || m_v2_system_action_pending != 0u
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        || inktag_image_store_busy()
#endif
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 13
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 2996 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        ;
}

#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 15
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static void epd_v2_test_time(ble_epd_t* p_epd, uint8_t transaction_id,
                              const uint8_t* payload, uint16_t length) {
    inktag_status_v2_t status = INKTAG_STATUS_INVALID_REQUEST;
    int16_t timezone = m_v2_timezone_minutes;
    int64_t local;
    uint8_t week_start = m_v2_display_settings.week_start;
    inktag_display_settings_t next_settings = m_v2_display_settings;
    bool week_changed;
    if (length < 2u || (payload[1] != INKTAG_DISPLAY_MODE_CALENDAR &&
            payload[1] != INKTAG_DISPLAY_MODE_CLOCK)) goto reject;
    if (payload[0] == INKTAG_TEST_TIME_SHIFT) {
        int32_t delta;
        if (length != 6u) goto reject;
        delta = (int32_t)inktag_read_u32_le(&payload[2]);
        if (delta == 0 || delta < -31622400 || delta > 31622400) goto reject;
        local = (int64_t)timestamp() + delta;
    } else {
        uint32_t utc;
        if (length != 9u || payload[8] > 1u) goto reject;
        utc = inktag_read_u32_le(&payload[2]);
        timezone = (int16_t)inktag_read_u16_le(&payload[6]);
        if (timezone < -840 || timezone > 840) goto reject;
        local = (int64_t)utc + (int32_t)timezone * 60;
        week_start = payload[8];
    }
    /* The compact time record cannot represent dates before 2024. Validate
     * before queuing, writing settings or rebasing RAM/Find wall time. */
    if (local < 1704067200ll || local > 4102444800ll) goto reject;
    if (p_epd->conn_handle == BLE_CONN_HANDLE_INVALID ||
            !p_epd->is_notification_enabled) {
        status = INKTAG_STATUS_INVALID_STATE; goto reject;
    }
    if (epd_v2_input_busy() || m_picture_io_active || EPD_V2_INPUT_BUSY()) {
        status = INKTAG_STATUS_BUSY; goto reject;
    }
    status = epd_current_control_access(p_epd);
    if (status != INKTAG_STATUS_OK) goto reject;
    if (!inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true) ||
            !inktag_nrf_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true)) {
        status = INKTAG_STATUS_LOW_POWER; goto reject;
    }
    /* Reserve the real GUI worker first. A scheduler failure must not change
     * either the persisted time or its RAM/Find basis. The worker executes
     * after this request returns, and owns the ordinary COMPLETE event. */
    if (!epd_schedule_operation(p_epd, NRF_EPD_OP_GUI, payload[1], (uint32_t)local) ||
            !m_epd_dispatch_scheduled) {
        m_epd_operation_pending = false;
        status = INKTAG_STATUS_BUSY; goto reject;
    }
    m_epd_pending_operation.origin = INKTAG_NRF_V2_OPERATION_BLE;
    m_epd_pending_operation.v2_opcode = INKTAG_CMD_RUN_TEST;
    m_epd_pending_operation.v2_transaction = transaction_id;
    next_settings.week_start = week_start;
    week_changed = week_start != m_v2_display_settings.week_start;
    if (!(week_changed ? epd_time_display_settings_write((uint32_t)local, timezone, &next_settings) :
                        epd_time_state_write((uint32_t)local, timezone))) {
        goto persist_failed;
    }
    m_v2_display_settings = next_settings;
    epd_v2_display_settings_sync_runtime(p_epd);
    m_v2_timezone_minutes = timezone;
    set_timestamp((uint32_t)local);
    epd_v2_send_time(p_epd, INKTAG_CMD_RUN_TEST, transaction_id);
    return;
persist_failed:
    m_epd_operation_pending = false;
    status = INKTAG_STATUS_PERSISTENCE_ERROR;
reject:
    epd_v2_send_response(p_epd, INKTAG_CMD_RUN_TEST, transaction_id, status, NULL, 0u);
}

static void epd_v2_run_test(ble_epd_t* p_epd, uint8_t opcode,
                            uint8_t transaction_id, const uint8_t* payload,
                            uint16_t length) {
    inktag_status_v2_t status = INKTAG_STATUS_UNSUPPORTED;
    if (length == 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (payload[0] == INKTAG_TEST_TIME_SHIFT || payload[0] == INKTAG_TEST_TIME_SET) {
        epd_v2_test_time(p_epd, transaction_id, payload, length);
        return;
    }
    else if (payload[0] == INKTAG_TEST_DEVICE_LOCATE) {
        if (length != 2u || payload[1] > 1u)
            status = INKTAG_STATUS_INVALID_REQUEST;
#if INKTAG_NRF_INDICATOR_ENABLED
        else if (m_v2_indicator_timer_created) {
            if (payload[1] != 0u)
                inktag_status_locate_start(&m_v2_indicator_runtime,
                                           app_monotonic_now_ms());
            else
                inktag_status_locate_stop(&m_v2_indicator_runtime,
                                          app_monotonic_now_ms());
            epd_v2_indicator_timer_update();
            if (payload[1] != 0u && !m_v2_indicator_timer_running) {
                /* A created timer is not proof that its start was admitted.
                 * Do not report a 72-second locator while leaving a steady lamp
                 * with no wakeup to advance/expire it. The client may retry. */
                inktag_status_locate_stop(&m_v2_indicator_runtime,
                                          app_monotonic_now_ms());
                epd_v2_indicator_timer_update();
                status = INKTAG_STATUS_BUSY;
            } else status = INKTAG_STATUS_OK;
        }
#endif
    }
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 16
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    epd_v2_send_response(p_epd, opcode, transaction_id, status, NULL, 0u);
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
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    record_index = request_length == 1u ? request[0] : 0u;
    length = inktag_nrf_health_encode(record_index, payload,
                                      INKTAG_NRF_HEALTH_WIRE_SIZE);
    if (length == 0u || (record_index != 0u && record_index >= payload[20])) {
        epd_v2_send_response(
            p_epd, opcode, transaction_id,
            length == 0u ? INKTAG_STATUS_INTERNAL_ERROR :
                           INKTAG_STATUS_NOT_FOUND,
            NULL, 0u);
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

#line 3297 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
static void epd_v2_send_activation_status(ble_epd_t* p_epd, uint8_t opcode,
                                          uint8_t transaction_id) {
    uint8_t response[INKTAG_ACTIVATION_STATUS_WIRE_SIZE];
    if (
#if INKTAG_NRF_ACTIVATION_ENABLED
        !epd_v2_activation_available()
#else
        !m_v2_activation.identity_available
#endif
    ) {
        epd_v2_send_response(p_epd, opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    inktag_activation_encode_status(&m_v2_activation, response);
    epd_v2_send_response(p_epd, opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
}

#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 14
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3398 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif

#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 8
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3586 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif

#if defined(__GNUC__) && defined(NRF52811_XXAA)
/* This dispatch is large enough that GCC's Thumb TBB/TBH lowering needs
 * per-case branch veneers.  A comparison tree is smaller on nRF52811 while
 * leaving the driver and renderer switches on their normal -Oz policy. */
__attribute__((optimize("no-jump-tables")))
#endif
static void epd_v2_dispatch_request(ble_epd_t* p_epd,
    const inktag_protocol_request_t* parsed);

static void epd_v2_process_packet(ble_epd_t* p_epd, const uint8_t* packet,
                                  uint16_t packet_length) {
    inktag_protocol_request_t request;
    inktag_status_v2_t parse_status = inktag_protocol_parse_request(
        packet, packet_length, &request);
    if (parse_status != INKTAG_STATUS_OK) {
        if (packet != NULL && packet_length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
            epd_v2_send_response(p_epd, packet[0], packet[1], parse_status,
                                 NULL, 0u);
        return;
    }
    inktag_nrf_health_stage(0x4E52424Cu, INKTAG_NRF_HEALTH_OPERATION_BLE); /* NRBL */
#if INKTAG_NRF_PASSWORD_ENABLED
    if (!inktag_device_password_command_allowed(&m_v2_password,
            &m_v2_password_config, request.opcode)) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 9
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3614 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
            m_v2_password_config.valid ? INKTAG_STATUS_DENIED :
                                        INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
#endif
#if INKTAG_BUILD_PRO
    if (!epd_v2_command_supported_on_target(request.opcode)) {
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
#line 3626 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
    inktag_status_v2_t panel_access = epd_v2_panel_request_access(
        p_epd, request.opcode, request.payload, request.payload_length);
    if (panel_access != INKTAG_STATUS_OK) {
        if (m_v2_transfer.active && m_v2_transfer.descriptor.object_type ==
                INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) epd_v2_transfer_reset(p_epd, true);
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id, panel_access, NULL, 0u);
        return;
    }
    if (request.opcode == INKTAG_CMD_SET_SETTINGS) {
        inktag_status_v2_t access = epd_v2_display_settings_write_access(
            request.payload, request.payload_length);
        if (access != INKTAG_STATUS_OK) {
            epd_v2_send_response(p_epd, request.opcode,
                                 request.transaction_id, access, NULL, 0u);
            return;
        }
    }
    /* The leased request bytes do not change during this call. Cache only
     * common command classification, never power, authorization or busy state. */
    const bool persistent_write = inktag_protocol_command_mutates_persistent_state(
        request.opcode, request.payload, request.payload_length)
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
        || request.opcode == INKTAG_CMD_SET_ADVERTISING_CONFIG
#endif
#if INKTAG_NRF_TEST_ENABLED
        || (request.opcode == INKTAG_CMD_RUN_TEST && request.payload_length != 0u &&
            (request.payload[0] == INKTAG_TEST_TIME_SHIFT ||
             request.payload[0] == INKTAG_TEST_TIME_SET))
#endif
        ;
    if (persistent_write &&
        !inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 10
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3658 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    if (EPD_V2_INPUT_BUSY() && persistent_write) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 11
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3666 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    if (epd_v2_command_requires_idle_display(request.opcode) &&
        (m_epd_operation_active || m_epd_operation_pending || m_epd_io_busy ||
         m_v2_transfer.active || EPD_V2_STORE_BUSY() || EPD_V2_INPUT_BUSY())) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 12
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3676 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 13
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3686 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 14
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 3698 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    epd_v2_dispatch_request(p_epd, &request);
}

static void epd_v2_get_io(ble_epd_t* p_epd, uint8_t opcode, uint8_t transaction,
    uint8_t selector) {
    const inktag_nrf_io_config_t* current = inktag_nrf_io_current();
    if (!current) {
        epd_v2_send_response(p_epd, opcode, transaction, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
    uint8_t response[36] = {INKTAG_RELEASE_PIN_REPORT_VERSION, 0u, 0x3Fu, 0u};
    uint16_t length;
    if (selector == 2u) {
        inktag_nrf_io_config_t defaults;
        bool valid = inktag_nrf_factory_io(&defaults);
        if (valid) {
            (void)inktag_nrf_io_derive(&p_epd->screen_config, &defaults);
            response[1] |= INKTAG_PIN_REPORT_DEFAULT_VALID;
        } else {
            memset(&defaults, 0, sizeof(defaults));
        }
        if (!valid || memcmp(defaults.pins, current->pins, 16u))
            response[1] |= INKTAG_PIN_REPORT_OVERRIDE_PRESENT;
        if (inktag_nrf_io_busy_known()) response[1] |= INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED;
        memcpy(response + 4u, defaults.pins, 16u);
        memcpy(response + 20u, current->pins, 16u);
        length = 36u;
    } else if (selector == 3u) {
        memcpy(response, current->aux, 12u);length = 12u;
    } else {
        epd_v2_send_response(p_epd, opcode, transaction, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    epd_v2_send_response(p_epd, opcode, transaction, INKTAG_STATUS_OK, response, length);
}

static inktag_status_v2_t epd_v2_set_io(ble_epd_t* p_epd,
    const uint8_t* payload, uint16_t length) {
    if (!payload) return INKTAG_STATUS_INVALID_REQUEST;
    const inktag_nrf_io_config_t* current = inktag_nrf_io_current();
    if (!current) return INKTAG_STATUS_PERSISTENCE_ERROR;
    if (m_epd_operation_active || m_epd_operation_pending || m_epd_io_busy ||
        m_picture_io_active || m_v2_transfer.active || EPD_V2_STORE_BUSY() ||
        EPD_V2_INPUT_BUSY() || !app_button_configuration_idle()) return INKTAG_STATUS_BUSY;
    inktag_nrf_io_config_t next = *current;
    if (length == 2u && payload[0] == 2u && payload[1] == 0u) {
        inktag_nrf_io_config_t defaults;
        if (!inktag_nrf_factory_io(&defaults)) return INKTAG_STATUS_PERSISTENCE_ERROR;
        memcpy(next.pins, defaults.pins, sizeof(next.pins));
        (void)inktag_nrf_io_derive(&p_epd->screen_config, &next);
    } else if (length == 18u && payload[0] == 2u && payload[1] == 1u) {
        memcpy(next.pins, payload + 2u, sizeof(next.pins));
    } else if (length == 12u && payload[0] == INKTAG_RELEASE_AUX_VERSION) {
        /* PIN_CONFIG admits this board's routing and AUX readback, not IO
         * writes. Even an unchanged/disabled slot1 candidate needs qualified
         * IO write authority, which this exact board does not advertise. */
        return INKTAG_STATUS_UNSUPPORTED;
    } else return INKTAG_STATUS_INVALID_REQUEST;
    if (!inktag_nrf_io_valid(&next)) return INKTAG_STATUS_INVALID_REQUEST;
    if (!inktag_nrf_io_settings_write(&next)) {
        inktag_nrf_io_invalidate();
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        inktag_image_store_invalidate();
#endif
        p_epd->epd = NULL;
        app_button_configuration_apply();
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    }
    inktag_nrf_io_publish(&next, &p_epd->screen_config);
    inktag_nrf_io_project(inktag_nrf_io_current(), &p_epd->config);
    EPD_GPIO_Load(&p_epd->config);
    p_epd->epd = inktag_nrf_io_busy_known() && p_epd->config.mosi_pin != INKTAG_PIN_UNUSED ?
        epd_find(&p_epd->screen_config) : NULL;
    app_button_configuration_apply();
    return INKTAG_STATUS_OK;
}

/* The RX execution lease owns this preflight/commit/publication sequence.
 * No live model selection, GPIO, replay write or cache publication in preflight. */
static inktag_status_v2_t epd_v2_set_screen(ble_epd_t* p_epd,
    const uint8_t* payload, uint16_t length) {
    epd_screen_config_t screen;
    epd_screen_t resolved;
    const inktag_nrf_io_config_t* current = inktag_nrf_io_current();
    if (m_epd_operation_active || m_epd_operation_pending || m_epd_io_busy ||
        m_picture_io_active || m_v2_transfer.active || EPD_V2_STORE_BUSY() || EPD_V2_INPUT_BUSY() ||
        !app_button_configuration_idle())
        return INKTAG_STATUS_BUSY;
    if (!current) return INKTAG_STATUS_PERSISTENCE_ERROR;
    if (epd_screen_config_decode_exact(payload, length, &screen) != EPD_SCREEN_STATUS_OK ||
        epd_screen_resolve(&screen, &resolved) != EPD_SCREEN_STATUS_OK)
        return INKTAG_STATUS_INCOMPATIBLE;
    inktag_nrf_io_config_t io = *current;
    if (!inktag_nrf_io_derive(&screen, &io)) return INKTAG_STATUS_INCOMPATIBLE;
    inktag_status_v2_t access = epd_screen_control_access(&screen, true);
    if (access != INKTAG_STATUS_OK) return access;
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    inktag_image_store_profile_view_t view;
    if (!inktag_image_store_profile_allowed(&screen)) return INKTAG_STATUS_INCOMPATIBLE;
    inktag_image_store_result_t result = inktag_image_store_prepare_profile(&screen, &view);
    if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NO_SPACE)
        return epd_v2_store_status(result);
#endif
    if (!epd_screen_config_write(&screen)) {
        /* A false return may follow a durable commit. Never roll back/replay
         * or keep resource/routing caches claiming the old state is certain. */
#if INKTAG_NRF_IMAGE_STORE_ENABLED
        inktag_image_store_invalidate();
#endif
        inktag_nrf_io_invalidate();
        p_epd->epd = NULL;
        p_epd->screen_config.schema_version = 0u;
        inktag_nrf_health_panel_temperature_record(false, 0);
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    }
    /* Prepared durable state -> RAM only. No GPIO, second commit or rescan. */
    epd_slideshow_stop(p_epd, false);
    p_epd->screen_config = screen;
    inktag_nrf_io_publish(&io, &screen);
    inktag_nrf_io_project(inktag_nrf_io_current(), &p_epd->config);
    EPD_GPIO_Load(&p_epd->config);
#if INKTAG_NRF_IMAGE_STORE_ENABLED
    inktag_image_store_publish_profile(&view);
#endif
    p_epd->epd = p_epd->config.mosi_pin != INKTAG_PIN_UNUSED ? epd_find(&screen) : NULL;
    m_display_state.mode = EPD_DISPLAY_STATE_PICTURE;
    m_display_state.argument = EPD_DISPLAY_STATE_NO_SOURCE;
    p_epd->config.display_mode = MODE_PICTURE;
    inktag_nrf_health_panel_temperature_record(false, 0);
    return INKTAG_STATUS_OK;
}

static void __attribute__((noinline, noclone)) epd_v2_dispatch_request(
    ble_epd_t* p_epd, const inktag_protocol_request_t* parsed) {
    const inktag_protocol_request_t request = *parsed;
    switch (request.opcode) {
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
        case INKTAG_CMD_GET_ADVERTISING_CONFIG:
        case INKTAG_CMD_SET_ADVERTISING_CONFIG:
            epd_v2_advertising_config(p_epd, request.opcode, request.transaction_id,
                request.opcode == INKTAG_CMD_SET_ADVERTISING_CONFIG,
                request.payload, request.payload_length);
            break;
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
            epd_v2_password_request(p_epd, request.opcode, request.transaction_id,
                request.flags, request.payload, request.payload_length);
            break;
#endif
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
        case INKTAG_CMD_GET_TIME:
            epd_v2_send_time(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_SET_TIME:
            if (request.payload_length != 7u || request.payload[6] > 1u ||
                (int16_t)inktag_read_u16_le(&request.payload[4]) < -840 ||
                (int16_t)inktag_read_u16_le(&request.payload[4]) > 840) {
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_REQUEST,
                    NULL, 0u);
            } else {
                uint32_t utc = inktag_read_u32_le(request.payload);
                int16_t timezone =
                    (int16_t)inktag_read_u16_le(&request.payload[4]);
                int64_t local = (int64_t)utc + (int32_t)timezone * 60;
                if (local < 1704067200ll || local > 4102444800ll) {
                    epd_v2_send_response(p_epd, request.opcode,
                        request.transaction_id, INKTAG_STATUS_INVALID_REQUEST,
                        NULL, 0u);
                    break;
                }
                if (!epd_time_state_write((uint32_t)local, timezone)) {
                    epd_v2_send_response(p_epd, request.opcode,
                        request.transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR,
                        NULL, 0u);
                    break;
                }
                m_v2_timezone_minutes = timezone;
                set_timestamp((uint32_t)local);
                epd_v2_send_time(p_epd, request.opcode,
                                 request.transaction_id);
            }
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE:
            epd_v2_send_screen(p_epd, request.opcode, request.transaction_id);
            break;
        case INKTAG_CMD_GET_IO_CONFIG:
            if (request.payload_length == 1u &&
                    (request.payload[0] == 2u || request.payload[0] == 3u))
                epd_v2_get_io(p_epd, request.opcode, request.transaction_id,
                    request.payload[0]);
            else epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            break;
        case INKTAG_CMD_SET_IO_CONFIG: {
            inktag_status_v2_t status = epd_v2_set_io(p_epd, request.payload, request.payload_length);
            if (status == INKTAG_STATUS_OK)
                epd_v2_get_io(p_epd, request.opcode, request.transaction_id,
                    request.payload_length == 12u ? INKTAG_RELEASE_AUX_GET_SELECTOR : INKTAG_RELEASE_PIN_GET_SELECTOR);
            else epd_v2_send_response(p_epd, request.opcode, request.transaction_id, status, NULL, 0u);
            break;
        }
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            inktag_status_v2_t status = epd_v2_set_screen(p_epd,
                request.payload, request.payload_length);
            if (status != INKTAG_STATUS_OK) {
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, status, NULL, 0u);
            } else {
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
                request.transaction_id, request.flags, request.payload,
                request.payload_length);
            break;
        case INKTAG_CMD_CANCEL:
            epd_v2_cancel(p_epd, request.opcode, request.transaction_id,
                           request.payload, request.payload_length);
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
            if (request.payload_length != 0u) {
#if INKTAG_NRF_PASSWORD_ENABLED
                if (m_v2_transfer.active && m_v2_transfer.descriptor.object_type ==
                        INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
                    epd_v2_transfer_reset(p_epd, false);
#endif
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_REQUEST,
                    NULL, 0u);
            } else
                epd_v2_transfer_commit(p_epd, request.opcode,
                                       request.transaction_id);
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!m_v2_transfer.active || request.transaction_id !=
                                             m_v2_transfer.transaction_id) {
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_STATE,
                    NULL, 0u);
            } else {
                epd_v2_transfer_reset(p_epd, true);
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_OK, NULL, 0u);
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
#if defined(INKTAG_GUI_ATOMIC_FONT_ASSETS)
        case INKTAG_CMD_GET_GUI_ASSET_INFO:
            if (request.payload_length != 0u)
                epd_v2_send_response(p_epd, request.opcode,
                    request.transaction_id, INKTAG_STATUS_INVALID_REQUEST,
                    NULL, 0u);
            else
                epd_v2_send_gui_asset_info(p_epd, request.opcode,
                                           request.transaction_id);
            break;
#endif
        case INKTAG_CMD_GET_RESOURCE_INFO:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_send_resource_info(p_epd, request.opcode,
                                      request.transaction_id);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        case INKTAG_CMD_LIST_RESOURCES:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_list_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        case INKTAG_CMD_READ_RESOURCE:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_read_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        case INKTAG_CMD_DELETE_RESOURCE:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_delete_resource(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        case INKTAG_CMD_SET_RESOURCE_ALIAS:
        case INKTAG_CMD_SET_IMAGE_METADATA:
            epd_v2_send_response(p_epd, request.opcode, request.transaction_id,
                                 INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_CLEAR_RESOURCES:
#if INKTAG_NRF_IMAGE_STORE_ENABLED
            epd_v2_clear_resources(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
#if INKTAG_BOARD_INPUT_QUALIFIED
        case INKTAG_CMD_GET_INPUT_CONFIG:
            epd_v2_send_input(p_epd, request.opcode,
                              request.transaction_id);
            break;
        case INKTAG_CMD_SET_INPUT_CONFIG:
            epd_v2_set_input(p_epd, request.opcode,
                request.transaction_id, request.payload,
                request.payload_length);
            break;
#endif
#if INKTAG_NRF_TEST_ENABLED
        case INKTAG_CMD_LIST_TESTS: {
            uint8_t response[6] = {1u, 2u, INKTAG_TEST_TIME_SHIFT, INKTAG_TEST_TIME_SET};
#if INKTAG_NRF_INDICATOR_ENABLED
            if (m_v2_indicator_timer_created)
                response[2u + response[1]++] = INKTAG_TEST_DEVICE_LOCATE;
#endif
#if INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 17
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_OK,
                response, (uint16_t)(2u + response[1]));
            break;
        }
        case INKTAG_CMD_RUN_TEST:
            epd_v2_run_test(p_epd, request.opcode, request.transaction_id,
                            request.payload, request.payload_length);
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
#if !INKTAG_NRF_ACTIVATION_ENABLED
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
            epd_v2_send_activation_status(p_epd, request.opcode, request.transaction_id);
            break;
#endif
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 15
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4130 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 15
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4150 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
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
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
#if INKTAG_NRF_INDICATOR_ENABLED
            epd_v2_indicator_config(p_epd, request.opcode,
                request.transaction_id, true, request.payload,
                request.payload_length);
#else
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
            break;
        default:
            epd_v2_send_response(p_epd, request.opcode,
                request.transaction_id, INKTAG_STATUS_UNKNOWN_COMMAND,
                NULL, 0u);
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
    /* Keep the mailbox leased through synchronous dispatch, including all
     * vendor Flash-event waits. Disconnect revokes auth/transactions at once
     * but cannot free this storage for a new central until dispatch returns. */
    epd_v2_process_packet(p_epd, m_v2_rx_packet, length);
#if INKTAG_NRF_PASSWORD_ENABLED
    inktag_nrf_password_wipe(m_v2_rx_packet, length);
#endif
    m_v2_rx_processing = false;
    m_v2_rx_pending = false;
    m_v2_rx_length = 0u;
}

static bool epd_v2_defer_display_read(ble_epd_t* p_epd, uint8_t opcode) {
    bool snapshot = inktag_protocol_cooperative_snapshot(opcode);
    /* Called only after framing, AUTH, panel access and mailbox admission.
     * Optional connection sampling must release its pins before a queued
     * AUTH/storage/configuration command runs. Signal cancellation here;
     * never perform panel cleanup or dispatch a writer from the BLE IRQ. */
    if (m_connection_temperature_active && !snapshot)
        m_epd_cancel_requested = true;
    if ((!m_epd_operation_active && !m_connection_temperature_active) || !snapshot)
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
    if (p_epd == NULL || packet == NULL || length == 0u ||
        length > sizeof(m_v2_rx_packet)) return;
    /* Reject retired framing before AUTH/Find revocation, IRQ cancellation or
     * mailbox publication. The worker parses and normalizes the retained raw
     * packet again; no extra queue/buffer or session state is introduced. */
    uint8_t flags;
    if (length < INKTAG_PROTOCOL_V2_HEADER_SIZE || packet[1] == 0u ||
        !inktag_release_request_flags(packet[2], &flags)) {
        if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
            epd_v2_send_response(p_epd, packet[0], packet[1],
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
#if INKTAG_NRF_PASSWORD_ENABLED
    /* Fast IRQ CANCEL paths must obey the same session fence as dispatch. */
    if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        !inktag_device_password_command_allowed(&m_v2_password,
                                                &m_v2_password_config, packet[0])) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 16
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4247 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, packet[0], packet[1],
            m_v2_password_config.valid ? INKTAG_STATUS_DENIED :
                                        INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
#endif
#if !INKTAG_BUILD_PRO
    /* Base has no positive private handlers. Reject unsupported IDs before
     * panel/mailbox admission can cancel sampling or change a transaction.
     * Framing and ordinary AUTH above keep their original precedence. */
    if (!epd_v2_command_supported_on_target(packet[0]) ||
        (packet[0] == INKTAG_CMD_TRANSFER_BEGIN &&
         length >= INKTAG_PROTOCOL_V2_HEADER_SIZE + 2u &&
         packet[INKTAG_PROTOCOL_V2_HEADER_SIZE + 1u] ==
             INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) ||
        (packet[0] == INKTAG_CMD_RUN_TEST &&
         length > INKTAG_PROTOCOL_V2_HEADER_SIZE &&
         packet[INKTAG_PROTOCOL_V2_HEADER_SIZE] == INKTAG_TEST_INPUT_SIMULATE)) {
        epd_v2_send_response(p_epd, packet[0], packet[1],
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
#line 4254 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#if INKTAG_NRF_TEST_ENABLED && INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 18
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    inktag_status_v2_t panel_access = epd_v2_panel_request_access(
        p_epd, packet[0], packet + INKTAG_PROTOCOL_V2_HEADER_SIZE,
        (uint16_t)(length - INKTAG_PROTOCOL_V2_HEADER_SIZE));
    if (panel_access != INKTAG_STATUS_OK) {
        /* IRQ admission is read-only. Main-loop tick parks an owned direct
         * transfer; never run sleep/POF or reset from the BLE IRQ. */
        epd_v2_send_response(p_epd, packet[0], packet[1], panel_access, NULL, 0u);
        return;
    }
    if (m_v2_write_active && length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        (packet[0] == INKTAG_CMD_CANCEL ||
         packet[0] == INKTAG_CMD_TRANSFER_ABORT)) {
        inktag_protocol_request_t request;
        inktag_status_v2_t status = inktag_protocol_parse_request(
            packet, length, &request);
        if (status == INKTAG_STATUS_OK && request.payload_length != 0u)
            status = INKTAG_STATUS_INVALID_REQUEST;
        if (status == INKTAG_STATUS_OK &&
            request.opcode == INKTAG_CMD_TRANSFER_ABORT &&
            request.transaction_id != m_v2_transfer.transaction_id)
            status = INKTAG_STATUS_INVALID_STATE;
        if (status == INKTAG_STATUS_OK) {
            m_v2_write_cancel_requested = true;
            if (request.opcode == INKTAG_CMD_CANCEL) epd_v2_cancel_display(p_epd);
        }
        epd_v2_send_response(p_epd, packet[0], packet[1], status, NULL, 0u);
        return;
    }
    if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        packet[0] == INKTAG_CMD_CANCEL &&
        (m_epd_operation_active || m_epd_operation_pending)) {
        inktag_protocol_request_t request;
        inktag_status_v2_t status = inktag_protocol_parse_request(
            packet, length, &request);
        if (status == INKTAG_STATUS_OK && request.payload_length != 0u)
            status = request.payload_length == 2u && request.payload[0] == INKTAG_CMD_RUN_TEST ?
                INKTAG_STATUS_NOT_FOUND : INKTAG_STATUS_INVALID_REQUEST;
        if (status == INKTAG_STATUS_OK) epd_v2_cancel_display(p_epd);
        epd_v2_send_response(p_epd, packet[0], packet[1], status, NULL, 0u);
        return;
    }
    if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE &&
        (epd_v2_command_requires_idle_display(packet[0])
#if INKTAG_NRF_TEST_ENABLED
         || (packet[0] == INKTAG_CMD_RUN_TEST && length > INKTAG_PROTOCOL_V2_HEADER_SIZE &&
             (packet[INKTAG_PROTOCOL_V2_HEADER_SIZE] == INKTAG_TEST_TIME_SHIFT ||
              packet[INKTAG_PROTOCOL_V2_HEADER_SIZE] == INKTAG_TEST_TIME_SET))
#endif
        ) &&
        (m_epd_operation_active || m_epd_operation_pending ||
         m_v2_transfer.active || EPD_V2_STORE_BUSY() || EPD_V2_INPUT_BUSY())) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 17
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4319 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        epd_v2_send_response(p_epd, packet[0], packet[1],
                             INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    if (m_v2_rx_pending || m_v2_rx_processing) {
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 18
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4327 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
            epd_v2_send_response(p_epd, packet[0], packet[1],
                                 INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    memcpy(m_v2_rx_packet, packet, length);
    m_v2_rx_length = length;
    m_v2_rx_pending = true;
    if (epd_v2_defer_display_read(p_epd, packet[0])) return;
    if (app_sched_event_put(&p_epd, sizeof(p_epd), epd_v2_process_queued) !=
        NRF_SUCCESS) {
        m_v2_rx_pending = false;
        m_v2_rx_length = 0u;
#if INKTAG_NRF_PASSWORD_ENABLED
        inktag_nrf_password_wipe(m_v2_rx_packet, length);
#endif
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 19
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4346 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
        if (length >= INKTAG_PROTOCOL_V2_HEADER_SIZE)
            epd_v2_send_response(p_epd, packet[0], packet[1],
                                 INKTAG_STATUS_INTERNAL_ERROR, NULL, 0u);
    }
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
        memset(&m_v2_transfer, 0, sizeof(m_v2_transfer));
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
            epd_v2_transfer_reset(p_epd, false);
            epd_v2_send_response(p_epd, opcode, transaction, status, NULL, 0u);
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
            epd_v2_send_response(p_epd, opcode, transaction, status, NULL, 0u);
        }
        epd_v2_transfer_reset(p_epd, false);
    } else if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_DELETE &&
               event->type == INKTAG_IMAGE_STORE_EVENT_DELETE) {
        epd_v2_send_response(p_epd, opcode, transaction, status, NULL, 0u);
        if (event->result == INKTAG_IMAGE_STORE_OK) {
            response[0] = event->id;
            epd_v2_send_event(p_epd, INKTAG_EVENT_RESOURCE_CHANGED,
                              INKTAG_STATUS_OK, opcode, transaction,
                              response, 1u);
        }
    } else if (m_v2_store_pending.type == INKTAG_NRF_V2_STORE_CLEAR_USER &&
               event->type == INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL) {
        if (status == INKTAG_STATUS_OK) {
            if (!inktag_nrf_config_store_clear_images())
                status = INKTAG_STATUS_PERSISTENCE_ERROR;
            else {
                inktag_display_state_default(&m_display_state);
                m_display_state_loaded = true;
                m_pending_picture_store_id = UINT8_MAX;
                m_button_clear_return_valid = false;
                m_button_slideshow_return_valid = false;
                epd_v2_display_settings_init(p_epd);
                p_epd->config.display_mode = MODE_CALENDAR;
            }
        }
        inktag_write_u16_le(response, event->removed);
        epd_v2_send_response(p_epd, opcode, transaction, status,
            status == INKTAG_STATUS_OK ? response : NULL,
            status == INKTAG_STATUS_OK ? 2u : 0u);
        if (status == INKTAG_STATUS_OK)
            epd_v2_send_event(p_epd, INKTAG_EVENT_RESOURCE_CHANGED,
                              status, opcode, transaction, NULL, 0u);
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
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 20
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4465 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
#if INKTAG_NRF_TEST_ENABLED && INKTAG_BOARD_INPUT_QUALIFIED && INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 19
#include "inktag_nrf_input_simulation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#if INKTAG_NRF_PASSWORD_ENABLED
    if (!m_v2_password_config.valid && !epd_v2_activation_busy() &&
        (int32_t)(app_monotonic_now_ms() - m_v2_password_retry_ms) >= 0)
        epd_v2_password_load();
#endif
    inktag_nrf_record_event_t record_event;
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 16
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4477 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    while (inktag_nrf_record_store_take_event(&record_event)) {
#if INKTAG_NRF_ACTIVATION_ENABLED
#define INKTAG_NRF_PRO_SECTION 17
#include "inktag_nrf_protocol_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4509 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#else
        if (record_event.kind == INKTAG_NRF_RECORD_HEALTH)
            inktag_nrf_health_record_complete(record_event.success);
#endif
    }
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 21
#include "inktag_nrf_protocol_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 4516 "firmware/ports/nordic/common/src/inktag_nrf_protocol_v2.inc"
#endif
    if (m_v2_transfer.active &&
        m_v2_transfer.descriptor.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
        epd_current_control_access(p_epd) != INKTAG_STATUS_OK) {
        uint8_t transaction = m_v2_transfer.transaction_id;
        inktag_status_v2_t status = epd_current_control_access(p_epd);
        epd_v2_transfer_reset(p_epd, true);
        epd_v2_send_event(p_epd, INKTAG_EVENT_ERROR, status,
            INKTAG_CMD_TRANSFER_WRITE, transaction, NULL, 0u);
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
