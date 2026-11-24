#include "inktag_nrf_build_profile.h"
#line 1 "firmware/ports/nordic/common/src/main.c"
/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

#include <stdint.h>
#include <string.h>

#include "ble.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#if defined(NRF52832_XXAA)
#include "ble_conn_params.h"
#endif
#include "ble_dfu.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "nordic_common.h"
#include "nrf.h"
#if defined(INKTAG_NRF_SDK17)
#if defined(NRF52832_XXAA)
#include "nrf_ble_gatt.h"
#endif
#include "nrf_bootloader_info.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#else
#include "fstorage.h"
#include "softdevice_handler.h"
#endif
#include "inktag_epd_service.h"
#include "app_error.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "main.h"
#include "inktag_board.h"
#if INKTAG_BUILD_PRO
#include "inktag_nrf_advertising_pro.h"
#else
#define INKTAG_NRF_DELAYED_ADVERTISING_ENABLED 0
#endif
#include "inktag_monotonic_clock.h"
#include "inktag_clock_rate.h"
#include "inktag_time_checkpoint.h"
#include "inktag_ble_power.h"
#include "inktag_ble_identity.h"
#include "inktag_nrf_ble_address.h"
#if defined(INKTAG_NRF_PROTOCOL_V2)
#include "inktag_nrf_health.h"
#endif
#include "inktag_nrf_config_store.h"
#include "inktag_nrf_io_config.h"
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
#include "inktag_soak_telemetry.h"
#endif
#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
#include "nrf_drv_gpiote.h"
#endif
#include "nrf_drv_wdt.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"
#if defined(INKTAG_NRF_SDK17)
#include "nrf_log_default_backends.h"
#endif

// clang-format off
#define CENTRAL_LINK_COUNT              0                                               /**< Number of central links used by the application. When changing this number remember to adjust the RAM settings*/
#define PERIPHERAL_LINK_COUNT           1                                               /**< Number of peripheral links used by the application. When changing this number remember to adjust the RAM settings*/

#define DEVICE_NAME                      "IKT_NRF"                                      /**< Factory application name prefix. */
#define APP_ADV_INTERVAL                 INKTAG_CONNECTABLE_ADV_INTERVAL_UNITS           /**< Product-wide long-lived connectable advertising interval (1 s). */
#define APP_ADV_TIMEOUT_IN_SECONDS       0                                              /**< Normal InkTag operation stays connectable until connected. */
#define APP_TIMER_PRESCALER              0                                              /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE          4                                              /**< Size of timer operation queues. */

#if defined(INKTAG_NRF_SDK17)
#define APP_BLE_CONN_CFG_TAG            1                                               /**< A tag identifying the SoftDevice BLE configuration. */
#define APP_BLE_OBSERVER_PRIO           3                                               /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define TIMER_TICKS(MS) APP_TIMER_TICKS(MS)
#define APP_TIMER_COUNTER_FREQUENCY     (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY + 1u))
#else
#define TIMER_TICKS(MS) APP_TIMER_TICKS(MS, APP_TIMER_PRESCALER)
#define APP_TIMER_COUNTER_FREQUENCY     (APP_TIMER_CLOCK_FREQ / (APP_TIMER_PRESCALER + 1u))
// Low frequency clock source to be used by the SoftDevice
#define NRF_CLOCK_LFCLKSRC      {.source        = INKTAG_BOARD_LFCLK_SOURCE,          \
                                 .rc_ctiv       = INKTAG_BOARD_LFCLK_RC_CTIV,         \
                                 .rc_temp_ctiv  = INKTAG_BOARD_LFCLK_RC_TEMP_CTIV,    \
                                 .xtal_accuracy = INKTAG_BOARD_LFCLK_ACCURACY}
#endif

#define MIN_CONN_INTERVAL                MSEC_TO_UNITS(7.5, UNIT_1_25_MS)               /**< Minimum connection interval (7.5 ms) */
#define MAX_CONN_INTERVAL                MSEC_TO_UNITS(90, UNIT_1_25_MS)                /**< Maximum connection interval (90 ms; browsers often choose >30 ms). */
#define SLAVE_LATENCY                    0                                              /**< Slave latency. Keep browser links responsive during long EPD refreshes. */
#define CONN_SUP_TIMEOUT                 MSEC_TO_UNITS(30000, UNIT_10_MS)               /**< Connection supervisory timeout (30 s; e-paper refresh can block for multiple seconds). */
#define FIRST_CONN_PARAMS_UPDATE_DELAY   TIMER_TICKS(5000)                              /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY    TIMER_TICKS(30000)                             /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT     3                                              /**< Number of attempts before giving up the connection parameter negotiation. */

#define SCHED_MAX_EVENT_DATA_SIZE       EPD_GUI_SCHD_EVENT_DATA_SIZE                    /**< Maximum size of scheduler events. */
#define SCHED_QUEUE_SIZE                10                                              /**< Maximum number of events in the scheduler queue. */

#define CLOCK_TIMER_INTERVAL             TIMER_TICKS(1000)                              /**< Clock timer interval (ticks). */
#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
#define BUTTON_TIMER_INTERVAL            TIMER_TICKS(20)
#endif
#define MIN_VALID_TIMESTAMP              1704067200UL                                   /**< 2024-01-01 00:00:00. */
#define MAX_VALID_TIMESTAMP              4102444800UL                                   /**< 2100-01-01 00:00:00. */

#ifndef INKTAG_NRF_DEVICE_NAME_WRITABLE
#define INKTAG_NRF_DEVICE_NAME_WRITABLE INKTAG_BUILD_PRO
#endif
#ifndef INKTAG_NRF_MINIMAL_ADVERTISING
#define INKTAG_NRF_MINIMAL_ADVERTISING 0
#endif

#define DEAD_BEEF                        0xDEADBEEF                                     /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#if defined(INKTAG_NRF_SDK17) && defined(NRF52832_XXAA)
NRF_BLE_GATT_DEF(m_gatt); /* Independent of the advertising encoder. */
#endif

#if defined(INKTAG_NRF_SDK17) && INKTAG_NRF_MINIMAL_ADVERTISING
static uint8_t                           m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static uint8_t                           m_adv_encoded[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint8_t                           m_scan_response_encoded[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static bool                              m_advertising_disconnect_disabled;
#define INKTAG_NRF_ADV_HANDLE m_adv_handle
#elif defined(INKTAG_NRF_SDK17)
BLE_ADVERTISING_DEF(m_advertising);                                                     /**< Advertising module instance. */
#define INKTAG_NRF_ADV_HANDLE m_advertising.adv_handle
#else
static ble_dfu_t                         m_dfus;                                        /**< Structure used to identify the DFU service. */
#endif
static uint16_t                          m_conn_handle = BLE_CONN_HANDLE_INVALID;       /**< Handle of the current connection. */
static ble_uuid_t                        m_adv_uuids[] = {{BLE_UUID_EPD_SVC, \
                                                           EPD_SVC_UUID_TYPE}};         /**< Universally unique service identifier. */

BLE_EPD_DEF(m_epd);                                                                     /**< Structure to identify the EPD Service. */
static uint32_t                          m_timestamp;                                   /**< Current local timestamp, unset until restored/synchronized. */
static uint32_t                          m_uptime_ms;
static inktag_monotonic_clock_t          m_monotonic_clock;
static inktag_clock_rate_t              m_clock_rate;
static uint16_t                          m_wall_milliseconds;
static uint32_t                          m_runtime_uptime_ms;
static inktag_monotonic_clock_t          m_runtime_monotonic_clock;
static inktag_time_checkpoint_t          m_time_checkpoint;
static uint32_t                          m_display_timestamp;
static bool                              m_time_valid;
/* Usable saved estimate is distinct from a durably synchronized current boot.
 * Neither state is persisted as proof of continuity or added to the wire. */
static bool                              m_time_calibrated;
APP_TIMER_DEF(m_clock_timer_id);                                                        /**< Clock timer. */
#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
APP_TIMER_DEF(m_button_timer_id);
static inktag_button_state_t             m_button_state;
static bool                              m_button_timer_running;
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
static uint32_t                          m_button_now_ms;
static void button_timer_timeout_handler(void* p_context);
#endif
static nrf_drv_wdt_channel_id            m_wdt_channel_id;
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
static void clock_idle(bool sampled);
#endif
static uint32_t                          m_wdt_last_feed_time = 0;
static uint32_t                          m_resetreas;
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 179 "firmware/ports/nordic/common/src/main.c"
#endif
// clang-format on

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num   Line number of the failing ASSERT call.
 * @param[in] file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t* p_file_name) {
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

uint32_t app_uptime_ms(void) { return m_uptime_ms; }
uint32_t app_lfclk_status(void) { return NRF_CLOCK->LFCLKSTAT; }
bool app_time_valid(void) { return m_time_valid; }
bool app_time_calibrated(void) { return m_time_calibrated; }

static inktag_elapsed_time_t clock_sample(void) {
    inktag_elapsed_time_t elapsed;
    CRITICAL_REGION_ENTER();
    elapsed =
        inktag_monotonic_clock_sample(&m_monotonic_clock, app_timer_cnt_get());
    uint32_t adjusted;
    APP_ERROR_CHECK_BOOL(inktag_clock_rate_apply(&m_clock_rate,
                                                elapsed.milliseconds, &adjusted));
    adjusted += m_wall_milliseconds;
    m_timestamp += adjusted / 1000u;
    m_wall_milliseconds = (uint16_t)(adjusted % 1000u);
    m_uptime_ms += elapsed.milliseconds;
    CRITICAL_REGION_EXIT();
    return elapsed;
}

// Read the running counter, not the last one-second callback's cached value.
uint32_t timestamp(void) {
    uint32_t value;
    CRITICAL_REGION_ENTER();
    (void)clock_sample();
    value = m_time_valid ? m_timestamp : 0u;
    CRITICAL_REGION_EXIT();
    return value;
}

int16_t app_clock_rate_ppm(void) { return m_clock_rate.ppm; }
int16_t app_clock_rate_default_ppm(void) {
    return INKTAG_BOARD_CLOCK_RATE_DEFAULT_PPM;
}

bool app_clock_rate_set(int16_t ppm) {
    if (ppm < -INKTAG_CLOCK_RATE_MAX_PPM || ppm > INKTAG_CLOCK_RATE_MAX_PPM)
        return false;
    CRITICAL_REGION_ENTER();
    /* Settle the elapsed interval at its old rate; retain UTC subsecond time. */
    (void)clock_sample();
    (void)inktag_clock_rate_set(&m_clock_rate, ppm);
    CRITICAL_REGION_EXIT();
    return true;
}

uint32_t app_monotonic_now_ms(void) {
    uint32_t value;
    CRITICAL_REGION_ENTER();
    inktag_elapsed_time_t elapsed = inktag_monotonic_clock_sample(
        &m_runtime_monotonic_clock, app_timer_cnt_get());
    m_runtime_uptime_ms += elapsed.milliseconds;
    value = m_runtime_uptime_ms;
    CRITICAL_REGION_EXIT();
    return value;
}

static bool timestamp_is_valid(uint32_t value) {
    return value >= MIN_VALID_TIMESTAMP && value <= MAX_VALID_TIMESTAMP;
}

static void time_checkpoint_saved(uint32_t now_ms) {
    inktag_time_checkpoint_saved(&m_time_checkpoint, now_ms);
}

static void time_checkpoint_write(uint32_t value, uint32_t now_ms) {
    if (epd_time_write(value)) {
        time_checkpoint_saved(now_ms);
    } else {
        inktag_time_checkpoint_failed(&m_time_checkpoint, now_ms);
    }
}

static void time_checkpoint_try(void) {
    uint32_t now_ms;
    if (!m_time_valid || !app_time_checkpoint_ready()) return;
    now_ms = app_monotonic_now_ms();
    if (!inktag_time_checkpoint_due(&m_time_checkpoint, now_ms)) return;
    // One latest snapshot, main-loop only; backend guards voltage/readback.
    time_checkpoint_write(timestamp(), now_ms);
}

// set the timestamp
uint16_t app_device_id4(void) {
    // Immutable normal-App suffix: independent of GAP rename/Find address.
    return (uint16_t)NRF_FICR->DEVICEADDR[0];
}

static void app_factory_device_name(char name[20]) {
    GFX_format(name, 20u, "%s_%02X%02X", DEVICE_NAME,
               (uint8_t)(NRF_FICR->DEVICEADDR[0] >> 8),
               (uint8_t)NRF_FICR->DEVICEADDR[0]);
}

bool app_device_name_reset(void) {
    char name[20];
    ble_gap_conn_sec_mode_t security;
    app_factory_device_name(name);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&security);
    uint8_t length = (uint8_t)strlen(name);
    if (sd_ble_gap_device_name_set(&security, (const uint8_t *)name,
                                   length) != NRF_SUCCESS)
        return false;
#if defined(INKTAG_NRF_SDK17) && INKTAG_NRF_MINIMAL_ADVERTISING
    /* The normal advertising frame is cached separately from GAP. Keep its
     * existing six-byte product marker, so reconnect advertises the reset
     * factory name rather than the removed user name. No radio restart here. */
    memmove(&m_adv_encoded[5u + length],
            &m_adv_encoded[4u + m_adv_encoded[3]], 6u);
    memcpy(&m_adv_encoded[5], name, length);
    m_adv_encoded[3] = (uint8_t)(length + 1u);
#endif
    return true;
}

void set_timestamp(uint32_t timestamp) {
    app_timer_stop(m_clock_timer_id);
    CRITICAL_REGION_ENTER();
    (void)clock_sample();
    m_timestamp = timestamp;
    m_wall_milliseconds = 0u;
    (void)inktag_clock_rate_set(&m_clock_rate, m_clock_rate.ppm);
    inktag_monotonic_clock_rebase_wall(&m_monotonic_clock);
    m_time_valid = timestamp_is_valid(timestamp);
    m_time_calibrated = m_time_valid;
    m_display_timestamp = timestamp;
    CRITICAL_REGION_EXIT();
    // V2 SET_TIME has already committed the timestamp+timezone before this
    // RAM reanchor. Do not append the same record twice on every connection.
    if (m_time_valid) time_checkpoint_saved(app_monotonic_now_ms());
    app_timer_start(m_clock_timer_id, CLOCK_TIMER_INTERVAL, NULL);
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    clock_idle(true);
#endif
}

// reload the wdt channel
void app_feed_wdt(void) {
    if ((uint32_t)(m_uptime_ms - m_wdt_last_feed_time) >= 30000u) {
        NRF_LOG_DEBUG("Feed WDT\n");
        nrf_drv_wdt_channel_feed(m_wdt_channel_id);
        m_wdt_last_feed_time = m_uptime_ms;
    }
}

#if defined(INKTAG_NRF_SDK17)
static void buttonless_dfu_sdh_state_observer(nrf_sdh_state_evt_t state, void* p_context) {
    if (state == NRF_SDH_EVT_STATE_DISABLED) {
        // Softdevice was disabled before going into reset. Inform bootloader to skip CRC on next boot.
        nrf_power_gpregret2_set(BOOTLOADER_DFU_SKIP_CRC);

        /* ble_dfu_buttonless_bootloader_start_finalize() already started a
         * GOTO_DFU shutdown.  Its first pass is blocked while the SoftDevice
         * disables.  Re-submitting GOTO_DFU is ignored because the power
         * manager marks the shutdown as already started; continue that exact
         * request so it reaches NVIC_SystemReset() and the Bootloader. */
#if defined(INKTAG_NRF_BOARD_NRF52832_PRICETAG)
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_CONTINUE);
#else
        /* Keep the already hardware-qualified nRF52811 binary path compact;
         * its existing GOTO_DFU continuation remains part of that board's
         * regression contract. */
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_DFU);
#endif
    }
}

/* nrf_sdh state observer. */
NRF_SDH_STATE_OBSERVER(m_buttonless_dfu_state_obs, 0) = {
    .handler = buttonless_dfu_sdh_state_observer,
};

#if !INKTAG_NRF_MINIMAL_ADVERTISING
static void advertising_config_get(ble_adv_modes_config_t* p_config) {
    memset(p_config, 0, sizeof(ble_adv_modes_config_t));

    p_config->ble_adv_fast_enabled = true;
    p_config->ble_adv_fast_interval = APP_ADV_INTERVAL;
    p_config->ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS * 100;
}
#endif

static void ble_dfu_evt_handler(ble_dfu_buttonless_evt_type_t event) {
    switch (event) {
        case BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE: {
            NRF_LOG_INFO("Device is preparing to enter bootloader mode.");

            // Prevent device from advertising on disconnect.
#if INKTAG_NRF_MINIMAL_ADVERTISING
            m_advertising_disconnect_disabled = true;
#else
            ble_adv_modes_config_t config;
            advertising_config_get(&config);
            config.ble_adv_on_disconnect_disabled = true;
            ble_advertising_modes_config_set(&m_advertising, &config);
#endif

            // Disconnect all other bonded devices that currently are connected.
            // This is required to receive a service changed indication
            // on bootup after a successful (or aborted) Device Firmware Update.
            /* V2 clients may already have disconnected after the accepted
             * ACK. That is a valid handoff, not a reason to reset into App. */
            if (m_conn_handle != BLE_CONN_HANDLE_INVALID) {
                uint32_t result = sd_ble_gap_disconnect(
                    m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
                if (result != NRF_ERROR_INVALID_STATE) APP_ERROR_CHECK(result);
            }
            break;
        }

        case BLE_DFU_EVT_BOOTLOADER_ENTER:
            NRF_LOG_INFO("Device will enter bootloader mode.");
            break;

        case BLE_DFU_EVT_BOOTLOADER_ENTER_FAILED:
            NRF_LOG_ERROR("Request to enter bootloader mode failed asynchroneously.");
            APP_ERROR_CHECK(false);
            break;

        case BLE_DFU_EVT_RESPONSE_SEND_ERROR:
            NRF_LOG_ERROR("Request to send a response to client failed.");
            APP_ERROR_CHECK(false);
            break;

        default:
            NRF_LOG_ERROR("Unknown event from ble_dfu_buttonless.");
            break;
    }
}
#else
static void ble_dfu_evt_handler(ble_dfu_t* p_dfu, ble_dfu_evt_t* p_evt) {
    switch (p_evt->type) {
        case BLE_DFU_EVT_INDICATION_DISABLED:
            NRF_LOG_INFO("Indication for BLE_DFU is disabled\r\n");
            break;

        case BLE_DFU_EVT_INDICATION_ENABLED:
            NRF_LOG_INFO("Indication for BLE_DFU is enabled\r\n");
            break;

        case BLE_DFU_EVT_ENTERING_BOOTLOADER:
            NRF_LOG_INFO("Device is entering bootloader mode!\r\n");
            break;
        default:
            NRF_LOG_INFO("Unknown event from ble_dfu\r\n");
            break;
    }
}
#endif

static void clock_timer_timeout_handler(void* p_context) {
    uint32_t previous_timestamp = m_display_timestamp;
    UNUSED_PARAMETER(p_context);

    (void)clock_sample();
    /* Keep the independent runtime clock sampled across the 24-bit RTC wrap.
     * Status effects and Find windows must not inherit the one-second wall
     * clock callback cadence. */
    (void)app_monotonic_now_ms();
    if (m_time_valid)
        ble_epd_on_timer(&m_epd, previous_timestamp, m_timestamp, false);
    m_display_timestamp = m_timestamp;
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    clock_idle(true);
#endif
}

/**@brief Function for the Event Scheduler initialization.
 */
static void scheduler_init(void) { APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE); }

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module. This creates and starts application timers.
 */
static void timers_init(void) {
    // Initialize timer module.
#if defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(app_timer_init());
#else
    APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);
#endif
    // Create timers.
    APP_ERROR_CHECK(app_timer_create(&m_clock_timer_id, APP_TIMER_MODE_REPEATED, clock_timer_timeout_handler));
#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
    APP_ERROR_CHECK(app_timer_create(&m_button_timer_id, APP_TIMER_MODE_REPEATED,
                                     button_timer_timeout_handler));
#endif
}

/**@brief Function for starting application timers.
 */
static void application_timers_start(void) {
    uint32_t current_tick = app_timer_cnt_get();
    APP_ERROR_CHECK_BOOL(inktag_clock_rate_set(&m_clock_rate,
                                              INKTAG_BOARD_CLOCK_RATE_DEFAULT_PPM));
    m_wall_milliseconds = 0u;
    // Start application timers.
    APP_ERROR_CHECK_BOOL(inktag_monotonic_clock_init(
        &m_monotonic_clock, current_tick, APP_TIMER_COUNTER_FREQUENCY, 24u));
    APP_ERROR_CHECK_BOOL(inktag_monotonic_clock_init(
        &m_runtime_monotonic_clock, current_tick,
        APP_TIMER_COUNTER_FREQUENCY, 24u));
    inktag_time_checkpoint_init(&m_time_checkpoint, m_runtime_uptime_ms, false);
    APP_ERROR_CHECK(app_timer_start(m_clock_timer_id, CLOCK_TIMER_INTERVAL, NULL));
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
void sleep_mode_enter(void) {
    NRF_LOG_DEBUG("Entering deep sleep mode\n");
    NRF_LOG_FINAL_FLUSH();
    nrf_delay_ms(100);

    ble_epd_sleep_prepare(&m_epd);
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void) {
    // Initialize EPD Service.
    memset(&m_epd, 0, sizeof(ble_epd_t));
    APP_ERROR_CHECK(ble_epd_init(&m_epd));

#if defined(INKTAG_NRF_SDK17)
    ble_dfu_buttonless_init_t dfus_init = {0};
    dfus_init.evt_handler = ble_dfu_evt_handler;
    APP_ERROR_CHECK(ble_dfu_buttonless_init(&dfus_init));
#else
    // Initialize the Device Firmware Update Service.
    ble_dfu_init_t dfus_init;
    memset(&dfus_init, 0, sizeof(dfus_init));
    dfus_init.evt_handler = ble_dfu_evt_handler;
    dfus_init.ctrl_point_security_req_write_perm = SEC_SIGNED;
    dfus_init.ctrl_point_security_req_cccd_write_perm = SEC_SIGNED;
    APP_ERROR_CHECK(ble_dfu_init(&m_dfus, &dfus_init));
#endif
}

/**@brief Function for the GAP initialization.
 *
 * @details This function will set up all the necessary GAP (Generic Access Profile) parameters of
 *          the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void) {
    char device_name[20];
    ble_gap_addr_t addr;
    ble_gap_conn_params_t gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
#if defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(inktag_nrf_ble_address_read(&addr));
#else
    APP_ERROR_CHECK(sd_ble_gap_address_get(&addr));
#endif

    NRF_LOG_INFO("Bluetooth MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n", addr.addr[5], addr.addr[4], addr.addr[3],
                 addr.addr[2], addr.addr[1], addr.addr[0]);
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 557 "firmware/ports/nordic/common/src/main.c"
#endif

#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED && \
    INKTAG_NRF_DEVICE_NAME_WRITABLE
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_ble_name_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
#endif
    {
        app_factory_device_name(device_name);
    }
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&sec_mode,
                                               (const uint8_t*)device_name,
                                               strlen(device_name)));

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

    APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&gap_conn_params));
}

/**@brief Function for handling an event from the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module
 *          which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply setting
 *       the disconnect_on_fail config parameter, but instead we use the event handler
 *       mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
#if defined(NRF52832_XXAA)
static void on_conn_params_evt(ble_conn_params_evt_t* p_evt) {
    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        NRF_LOG_WARNING("Connection parameter negotiation failed; keeping browser BLE link alive.\n");
    }
}

/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) { APP_ERROR_HANDLER(nrf_error); }

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail = false;
    cp_init.evt_handler = on_conn_params_evt;
    cp_init.error_handler = conn_params_error_handler;

    APP_ERROR_CHECK(ble_conn_params_init(&cp_init));
}
#endif

#if defined(INKTAG_NRF_SDK17) && INKTAG_NRF_MINIMAL_ADVERTISING
static uint32_t advertising_configure_normal(void) {
    ble_gap_adv_data_t data = {0};
    ble_gap_adv_params_t params = {0};
    data.adv_data.p_data = m_adv_encoded;
    data.adv_data.len = (uint16_t)(m_adv_encoded[3] + 10u);
    data.scan_rsp_data.p_data = m_scan_response_encoded;
    data.scan_rsp_data.len = (uint16_t)(m_scan_response_encoded[0] + 1u);
    params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    params.filter_policy = BLE_GAP_ADV_FP_ANY;
    params.interval = APP_ADV_INTERVAL;
    params.duration = BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED;
    params.primary_phy = BLE_GAP_PHY_1MBPS;
    return sd_ble_gap_adv_set_configure(&m_adv_handle, &data, &params);
}
#endif

#if !INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
static void advertising_start(void) {
    NRF_LOG_INFO("advertising start\n");
#if defined(INKTAG_NRF_SDK17) && INKTAG_NRF_MINIMAL_ADVERTISING
    APP_ERROR_CHECK(sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG));
#elif defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST));
#else
    APP_ERROR_CHECK(ble_advertising_start(BLE_ADV_MODE_FAST));
#endif
}

#endif

#if defined(INKTAG_NRF_SDK17)
static void advertising_restart_if_needed(void) {
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    if (!app_advertising_visible()) return;
#endif
#if INKTAG_NRF_MINIMAL_ADVERTISING
    uint32_t err_code;
    if (m_advertising_disconnect_disabled) return;
    /* Find changes the same handle to an Apple non-connectable payload. */
    err_code = advertising_configure_normal();
    if (err_code == NRF_SUCCESS)
        err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
#else
    uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
#endif
    if (err_code != NRF_SUCCESS && err_code != NRF_ERROR_INVALID_STATE && err_code != NRF_ERROR_BUSY) {
        APP_ERROR_CHECK(err_code);
    }
}

#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 3
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 810 "firmware/ports/nordic/common/src/main.c"
#endif

#endif

#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
#include "inktag_nrf_main_advertising_pro.inc"
#endif

#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
static void gpiote_init_if_needed(void) {
    if (!nrf_drv_gpiote_is_init()) APP_ERROR_CHECK(nrf_drv_gpiote_init());
}

static bool button_pressed(void) {
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    return (nrf_gpio_pin_read(INKTAG_BOARD_BUTTON_PIN) != 0u) ^
        (INKTAG_BOARD_BUTTON_ACTIVE_LOW != 0);
}

static bool button_state_active(void) {
    return m_button_state.raw_pressed || m_button_state.stable_pressed ||
           m_button_state.click_count != 0u ||
           m_button_state.pending_gesture != INKTAG_BUTTON_GESTURE_NONE;
}

static void button_timer_start(void);
static void button_gesture_dispatch(void* p_event_data, uint16_t event_size) {
    inktag_button_gesture_t gesture;
    if (p_event_data == NULL || event_size != sizeof(gesture)) return;
    memcpy(&gesture, p_event_data, sizeof(gesture));
    if (ble_epd_input_simulation_busy()) {
        /* This is an already sampled physical gesture, never a synthetic one.
         * Keep the existing one-entry latch; do not overwrite a newer gesture. */
        if (m_button_state.pending_gesture == INKTAG_BUTTON_GESTURE_NONE)
            m_button_state.pending_gesture = (uint8_t)gesture;
        button_timer_start();
        return;
    }
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    app_advertising_activity();
#endif
    ble_epd_handle_button_gesture(&m_epd, gesture);
}

static void button_timer_start(void) {
    if (m_button_timer_running) return;
    if (app_timer_start(m_button_timer_id, BUTTON_TIMER_INTERVAL, NULL) == NRF_SUCCESS)
        m_button_timer_running = true;
}

static void button_timer_timeout_handler(void* p_context) {
    inktag_button_gesture_t gesture;
    inktag_elapsed_time_t elapsed;
    uint32_t previous_timestamp = m_timestamp;
    (void)p_context;
    elapsed = clock_sample();
    m_button_now_ms += elapsed.milliseconds;
    if (elapsed.seconds != 0u && m_time_valid)
        ble_epd_on_timer(&m_epd, previous_timestamp, m_timestamp, false);
    inktag_button_sample(&m_button_state, button_pressed(), m_button_now_ms);
    if (!ble_epd_input_simulation_busy() &&
        inktag_button_take_gesture(&m_button_state, &gesture))
        (void)app_sched_event_put(&gesture, sizeof(gesture), button_gesture_dispatch);
    if (!button_state_active() && !button_pressed()) {
        (void)app_timer_stop(m_button_timer_id);
        m_button_timer_running = false;
    }
}

static void button_gpiote_evt_handler(nrf_drv_gpiote_pin_t pin,
                                      nrf_gpiote_polarity_t action) {
    (void)pin;
    (void)action;
    button_timer_start();
}

bool app_button_configuration_idle(void) {
    return !m_button_timer_running && !button_state_active() && !button_pressed();
}

void app_button_configuration_apply(void) {
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
    nrf_drv_gpiote_in_event_disable(INKTAG_BOARD_BUTTON_PIN);
    nrf_gpio_cfg_input(INKTAG_BOARD_BUTTON_PIN,
        INKTAG_BOARD_BUTTON_ACTIVE_LOW ? NRF_GPIO_PIN_PULLUP : NRF_GPIO_PIN_PULLDOWN);
    nrf_drv_gpiote_in_event_enable(INKTAG_BOARD_BUTTON_PIN, true);
    if (button_pressed()) button_timer_start();
}

static void button_init(void) {
    nrf_drv_gpiote_in_config_t config = GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
#if INKTAG_BOARD_BUTTON_ACTIVE_LOW
    config.pull = NRF_GPIO_PIN_PULLUP;
#else
    config.pull = NRF_GPIO_PIN_PULLDOWN;
#endif
    inktag_button_state_init(&m_button_state);
    gpiote_init_if_needed();
    APP_ERROR_CHECK(nrf_drv_gpiote_in_init(INKTAG_BOARD_BUTTON_PIN, &config,
                                           button_gpiote_evt_handler));
    /* Reserve the same fitted P0.02 channel once, even if slot1 is disabled.
     * An admitted later settings save cannot fail allocating a new channel. */
    app_button_configuration_apply();
}
#else
bool app_button_configuration_idle(void) { return true; }
void app_button_configuration_apply(void) {}
#endif

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
#if !defined(INKTAG_NRF_SDK17) || !INKTAG_NRF_MINIMAL_ADVERTISING
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
    switch (ble_adv_evt) {
        case BLE_ADV_EVT_FAST:
            break;
        case BLE_ADV_EVT_IDLE:
            NRF_LOG_INFO("advertising timeout\n");
            /* Ordinary InkTag operation must remain connectable. The timeout
             * is zero, but restart defensively if the SDK still reports idle. */
            advertising_start();
            break;
        default:
            break;
    }
}
#endif

/**@brief Function for the application's SoftDevice event handler.
 *
 * @param[in] p_ble_evt SoftDevice event.
 */
static void on_ble_evt(ble_evt_t* p_ble_evt) {
    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            NRF_LOG_INFO("CONNECTED\n");
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
            inktag_soak_telemetry_connection_started();
#endif
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 963 "firmware/ports/nordic/common/src/main.c"
#endif
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("DISCONNECTED\n");
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
#if defined(INKTAG_NRF_SDK17)
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 5
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 983 "firmware/ports/nordic/common/src/main.c"
#endif
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
            app_advertising_activity();
#endif
            advertising_restart_if_needed();
#if defined(INKTAG_NRF_PROTOCOL_V2) && INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 6
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 988 "firmware/ports/nordic/common/src/main.c"
#endif
#else
            advertising_start();
#endif
            break;
#if defined(INKTAG_NRF_SDK17)
        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys = {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
        } break;
#endif

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported
            APP_ERROR_CHECK(
                sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL));
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            APP_ERROR_CHECK(sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0));
            break;

#if defined(INKTAG_NRF_SDK17) && !defined(NRF52832_XXAA)
        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST: {
            uint16_t peer_mtu =
                p_ble_evt->evt.gatts_evt.params.exchange_mtu_request.client_rx_mtu;
            uint16_t effective_mtu =
                MIN(peer_mtu, (uint16_t)NRF_SDH_BLE_GATT_MAX_MTU_SIZE);

            APP_ERROR_CHECK(sd_ble_gatts_exchange_mtu_reply(
                p_ble_evt->evt.gatts_evt.conn_handle,
                NRF_SDH_BLE_GATT_MAX_MTU_SIZE));
            m_epd.max_data_len = effective_mtu - 3u;
        } break;
#endif

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            APP_ERROR_CHECK(
                sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            APP_ERROR_CHECK(
                sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
            break;

        default:
            // No implementation needed.
            break;
    }
}

#if defined(INKTAG_NRF_SDK17)
/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const* p_ble_evt, void* p_context) {
    UNUSED_PARAMETER(p_context);

    on_ble_evt((ble_evt_t*)p_ble_evt);
}

#else
/**@brief Function for dispatching a SoftDevice event to all modules with a SoftDevice
 *        event handler.
 *
 * @details This function is called from the SoftDevice event interrupt handler after a
 *          SoftDevice event has been received.
 *
 * @param[in] p_ble_evt  SoftDevice event.
 */
static void ble_evt_dispatch(ble_evt_t* p_ble_evt) {
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_epd_on_ble_evt(&m_epd, p_ble_evt);
    on_ble_evt(p_ble_evt);
    ble_advertising_on_ble_evt(p_ble_evt);
    ble_dfu_on_ble_evt(&m_dfus, p_ble_evt);
}

/**@brief Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in] sys_evt  System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt) {
    // Dispatch the system event to the fstorage backends used by the settings
    // journal and typed image store.
    fs_sys_event_handler(sys_evt);

    // Dispatch to the Advertising module last, since it will check if there are any
    // pending flash operations in fstorage. Let fstorage process system events first,
    // so that it can report correctly to the Advertising module.
    ble_advertising_on_sys_evt(sys_evt);
}
#endif

/**@brief Function for the SoftDevice initialization.
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
#if defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(nrf_sdh_enable_request());

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));

    // Enable BLE stack.
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
#else
    nrf_clock_lf_cfg_t clock_lf_cfg = NRF_CLOCK_LFCLKSRC;

    // Initialize the SoftDevice handler module.
    SOFTDEVICE_HANDLER_INIT(&clock_lf_cfg, NULL);

    ble_enable_params_t ble_enable_params;
    APP_ERROR_CHECK(
        softdevice_enable_get_default_config(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT, &ble_enable_params));
    ble_enable_params.common_enable_params.vs_uuid_count = 2;

    // Check the ram settings against the used number of links
    CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);

    // Enable BLE stack.
    APP_ERROR_CHECK(softdevice_enable(&ble_enable_params));

    // Subscribe for BLE events.
    APP_ERROR_CHECK(softdevice_ble_evt_handler_set(ble_evt_dispatch));

    // Subscribe for System events.
    APP_ERROR_CHECK(softdevice_sys_evt_handler_set(sys_evt_dispatch));
#endif
}

#if defined(INKTAG_NRF_SDK17)
#if defined(NRF52832_XXAA)
/**@brief Function for handling events from the GATT library. */
void gatt_evt_handler(nrf_ble_gatt_t* p_gatt, nrf_ble_gatt_evt_t const* p_evt) {
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
        m_epd.max_data_len = p_evt->params.att_mtu_effective - 3;
        NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_epd.max_data_len, m_epd.max_data_len);
    }
    NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x", p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
}

/**@brief Function for initializing the GATT library. */
void gatt_init(void) {
    APP_ERROR_CHECK(nrf_ble_gatt_init(&m_gatt, gatt_evt_handler));
    APP_ERROR_CHECK(nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE));
}
#endif
#else
// Set BW Config to HIGH.
static void ble_options_set(void) {
    ble_opt_t ble_opt;

    memset(&ble_opt, 0, sizeof(ble_opt));

    ble_opt.common_opt.conn_bw.role = BLE_GAP_ROLE_PERIPH;
    ble_opt.common_opt.conn_bw.conn_bw.conn_bw_rx = BLE_CONN_BW_HIGH;
    ble_opt.common_opt.conn_bw.conn_bw.conn_bw_tx = BLE_CONN_BW_HIGH;

    APP_ERROR_CHECK(sd_ble_opt_set(BLE_COMMON_OPT_CONN_BW, &ble_opt));
}
#endif

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void) {
    /* Same normal-App marker as the qualified shared discovery contract.
     * SDK encoding copies these bytes during init; no stack pointer is kept. */
    static const uint8_t marker[] = {5u, 0xFFu, 0xF7u, 0x04u, 0x02u, 0x03u};
#if defined(INKTAG_NRF_SDK17) && INKTAG_NRF_MINIMAL_ADVERTISING
    ble_gap_adv_data_t data;
    ble_gap_adv_params_t params;
    uint16_t name_length = INKTAG_BLE_DEVICE_NAME_MAX_BYTES;
    uint8_t uuid_length = (uint8_t)(sizeof(m_scan_response_encoded) - 2u);

    m_adv_encoded[0] = 2u;
    m_adv_encoded[1] = BLE_GAP_AD_TYPE_FLAGS;
    m_adv_encoded[2] = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    APP_ERROR_CHECK(sd_ble_gap_device_name_get(&m_adv_encoded[5], &name_length));
    m_adv_encoded[3] = (uint8_t)(name_length + 1u);
    m_adv_encoded[4] = BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&m_adv_encoded[name_length + 5u], marker, sizeof(marker));

    APP_ERROR_CHECK(sd_ble_uuid_encode(&m_adv_uuids[0], &uuid_length,
                                       &m_scan_response_encoded[2]));
    m_scan_response_encoded[0] = (uint8_t)(uuid_length + 1u);
    m_scan_response_encoded[1] = uuid_length == 16u ?
        BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE :
        BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE;

    memset(&data, 0, sizeof(data));
    data.adv_data.p_data = m_adv_encoded;
    data.adv_data.len = (uint16_t)(name_length + 5u + sizeof(marker));
    data.scan_rsp_data.p_data = m_scan_response_encoded;
    data.scan_rsp_data.len = (uint16_t)(uuid_length + 2u);

    memset(&params, 0, sizeof(params));
    params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    params.filter_policy = BLE_GAP_ADV_FP_ANY;
    params.interval = APP_ADV_INTERVAL;
    params.duration = BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED;
    params.primary_phy = BLE_GAP_PHY_1MBPS;
    APP_ERROR_CHECK(sd_ble_gap_adv_set_configure(&m_adv_handle, &data, &params));
#elif defined(INKTAG_NRF_SDK17)
    ble_advertising_init_t init;
    ble_advdata_manuf_data_t manufacturer = {
        .company_identifier = 0x04F7u,
        .data = {.p_data = (uint8_t *)&marker[4], .size = 2u}
    };

    memset(&init, 0, sizeof(init));

    init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = false;
    init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    init.advdata.p_manuf_specific_data = &manufacturer;

    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids = m_adv_uuids;

    init.config.ble_adv_fast_enabled = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS * 100;
    init.evt_handler = on_adv_evt;

    APP_ERROR_CHECK(ble_advertising_init(&m_advertising, &init));

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
#else
    ble_advdata_t advdata;
    ble_advdata_t scanrsp;
    ble_adv_modes_config_t options;
    ble_advdata_manuf_data_t manufacturer = {
        .company_identifier = 0x04F7u,
        .data = {.p_data = (uint8_t *)&marker[4], .size = 2u}
    };

    // Build advertising data struct to pass into @ref ble_advertising_init.
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    advdata.p_manuf_specific_data = &manufacturer;

    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids = m_adv_uuids;

    memset(&options, 0, sizeof(options));
    options.ble_adv_fast_enabled = true;
    options.ble_adv_fast_interval = APP_ADV_INTERVAL;
    options.ble_adv_fast_timeout = APP_ADV_TIMEOUT_IN_SECONDS;

    APP_ERROR_CHECK(ble_advertising_init(&advdata, &scanrsp, &options, on_adv_evt, NULL));
#endif
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void) {
    APP_ERROR_CHECK(NRF_LOG_INIT(timestamp));
#if defined(INKTAG_NRF_SDK17)
    NRF_LOG_DEFAULT_BACKENDS_INIT();
#endif
}

/**@brief Function for initializing power management.
 */
static void power_management_init(void) {
#if defined(INKTAG_NRF_SDK17)
    APP_ERROR_CHECK(nrf_pwr_mgmt_init());
#else
    APP_ERROR_CHECK(nrf_pwr_mgmt_init(APP_TIMER_TICKS(1000, APP_TIMER_PRESCALER)));
#endif
}

/**@brief Function for handling the idle state (main loop).
 *
 * @details If there is no pending log operation, then sleep until next the next event occurs.
 */
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
#include "inktag_nrf_idle_clock_pro.inc"
#endif

static void idle_state_handle(void) {
    app_feed_wdt();
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    (void)app_advertising_visible();
#endif
    time_checkpoint_try();
#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    clock_idle(false);
#endif
#if defined(INKTAG_NRF_PROTOCOL_V2)
#if INKTAG_NRF_FIND_ENABLED
#define INKTAG_NRF_PRO_SECTION 7
#include "inktag_nrf_main_find_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#line 1294 "firmware/ports/nordic/common/src/main.c"
#endif
    inktag_nrf_health_process(m_uptime_ms);
#endif

    if (NRF_LOG_PROCESS() == false) {
#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
        /* These are scheduler/maintenance turns and event-aware sleep
         * entries, never estimates of over-the-air packet counts. */
        inktag_soak_telemetry_radio_maintenance();
        inktag_soak_telemetry_sleep_entered();
#endif
        /* Ordinary advertising and connected idle remain in attach-preserving
         * System ON. Explicit System OFF is a separate lifecycle request. */
        nrf_pwr_mgmt_run();
    }
}

/**
 * @brief WDT events handler.
 */
void wdt_event_handler(void) {
    // NOTE: The max amount of time we can spend in WDT interrupt is two cycles of 32768[Hz] clock - after that, reset
    // occurs
    NRF_LOG_ERROR("WDT Rest!\r\n");
#if defined(INKTAG_NRF_PROTOCOL_V2)
    inktag_nrf_health_watchdog_capture();
#endif
    NRF_LOG_FINAL_FLUSH();
}

/* Do not keep the startup's SDK configuration and factory-read temporaries
 * on the stack beneath every scheduled crypto/display command forever. */
#define STARTUP_DISPLAY_PENDING 1u
#define STARTUP_FIRST_DISPLAY 2u
#define STARTUP_OTA_INSTALL 4u
static void time_restore_display_reference(void) {
    uint32_t saved = 0u;
    // The checked journal supplies a usable estimate, not cold-boot accuracy.
    // An unsuccessful read must not inherit any previous/factory value.
    m_time_valid = epd_time_read(&saved) && timestamp_is_valid(saved);
    m_time_calibrated = false;
    m_timestamp = m_time_valid ? saved : 0u;
    m_display_timestamp = m_timestamp;
    // A verified existing record starts the next hour at this boot anchor;
    // restoring it is not a new Flash commit or a calibrated SET_TIME.
    inktag_time_checkpoint_init(&m_time_checkpoint, app_monotonic_now_ms(),
                               m_time_valid);
}

static uint8_t __attribute__((noinline, noclone)) application_initialize(void) {
    bool first_display_requested = false;
    bool ota_install_boot = false;
    bool boot_display_context_valid;
    log_init();

    // Save reset reason.
    m_resetreas = NRF_POWER->RESETREAS;
    NRF_POWER->RESETREAS = m_resetreas;
    NRF_LOG_DEBUG("== RESET REASON: %d ===\n", m_resetreas);

    NRF_LOG_DEBUG("init..\n");

    // Configure WDT.
    nrf_drv_wdt_config_t config = NRF_DRV_WDT_DEAFULT_CONFIG;
    /* The nrfx SDK default is 2 seconds, while this application deliberately
     * feeds at a 30-second cadence and may spend seconds refreshing EPD. */
    config.reload_value = 60000u;
    APP_ERROR_CHECK(nrf_drv_wdt_init(&config, wdt_event_handler));
    APP_ERROR_CHECK(nrf_drv_wdt_channel_alloc(&m_wdt_channel_id));
    nrf_drv_wdt_enable();

    timers_init();
    power_management_init();
    ble_stack_init();
    scheduler_init();
#if defined(INKTAG_NRF_PROTOCOL_V2)
    /* Establish the stable startup voltage before settings recovery can
     * program Flash and before services initialize status LEDs or EPD work. */
    inktag_nrf_health_init(m_resetreas, 0x4E524230u); /* NRB0 */
#endif
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
    /* Initialize the settings journal before GAP so the persisted Bluetooth
     * name is used in the first advertising packet after boot. */
    epd_config_init(NULL);
#endif
    gap_params_init();
#if defined(INKTAG_NRF_SDK17)
#if defined(NRF52832_XXAA)
    gatt_init();
#endif
    ble_dfu_buttonless_async_svci_init();
#else
    ble_options_set();
#endif
    services_init();
#if defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
    button_init();
#endif
    // Continue from a valid saved estimate for pages, Find and hourly saves.
    // RTC1 restarts at boot: absence duration is unknown, never calibrated.
    time_restore_display_reference();
    advertising_init();
#if defined(NRF52832_XXAA)
    conn_params_init();
#endif

    NRF_LOG_DEBUG("start..\n");

    // Start execution.
    application_timers_start();

#if defined(INKTAG_ENABLE_SOAK_TELEMETRY) && INKTAG_ENABLE_SOAK_TELEMETRY
    inktag_soak_telemetry_init(
        INKTAG_SOAK_FLAG_FIND_WINDOWS |
            INKTAG_SOAK_FLAG_CONNECTION_SESSIONS |
            INKTAG_SOAK_FLAG_RADIO_MAINTENANCE |
            INKTAG_SOAK_FLAG_SLEEP_ENTRIES,
        app_monotonic_now_ms());
#endif

#if INKTAG_NRF_DELAYED_ADVERTISING_ENABLED
    app_advertising_activity();
#else
    advertising_start();
#endif

    NRF_LOG_DEBUG("done.\n");

    boot_display_context_valid = inktag_nrf_boot_display_context(
        &first_display_requested, &ota_install_boot,
        ble_epd_screen_control_allowed(&m_epd));
    return (boot_display_context_valid ? STARTUP_DISPLAY_PENDING : 0u) |
        (first_display_requested ? STARTUP_FIRST_DISPLAY : 0u) |
        (ota_install_boot ? STARTUP_OTA_INSTALL : 0u);
}

/**@brief Function for application main entry. */
int main(void) {
    uint8_t startup = application_initialize();
    bool first_display_requested = (startup & STARTUP_FIRST_DISPLAY) != 0u;
    bool ota_install_boot = (startup & STARTUP_OTA_INSTALL) != 0u;
    bool boot_display_pending = (startup & STARTUP_DISPLAY_PENDING) != 0u;
    for (;;) {
        ble_epd_process(&m_epd);
        /* A readiness-confirmation reset queued by the boot checkpoint must
         * reach Recovery before the application success indication. */
        app_sched_execute();
#if INKTAG_BUILD_PRO && defined(INKTAG_BOARD_BUTTON_PRESENT) && INKTAG_BOARD_BUTTON_PRESENT
#define INKTAG_NRF_PRO_SECTION 4
#include "inktag_nrf_input_custom_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#endif
        if (boot_display_pending && ble_epd_startup_complete()) {
            if (ble_epd_restore_display(
                    &m_epd, m_timestamp, first_display_requested,
                    ota_install_boot, true)) {
                boot_display_pending = false;
                ble_epd_application_ready();
            }
        }
        ble_epd_process(&m_epd);
        idle_state_handle();
    }
}
