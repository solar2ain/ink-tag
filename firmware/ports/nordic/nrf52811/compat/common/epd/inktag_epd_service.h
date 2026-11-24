/* Copyright (c) 2012 Nordic Semiconductor. All Rights Reserved.
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

/* Nordic EPD service ABI. Wire identities and field layout are retained;
 * transport behavior is specified in the nRF32 service replacement record. */
#ifndef __EPD_SERVICE_H__
#define __EPD_SERVICE_H__

#include <inttypes.h>
#include <stdbool.h>
#include "ble.h"
#include "ble_srv_common.h"
#include "inktag_board.h"
#include "inktag_epd_config.h"
#include "inktag_epd_driver.h"
#include "inktag_gui.h"
#include "inktag_version.h"
#include "sdk_config.h"
#if defined(INKTAG_NRF_SDK17)
#include "nrf_sdh_ble.h"
#endif

/* Persistent wire identity; byte order is the SoftDevice UUID-base order. */
#define BLE_UUID_EPD_SVC_BASE \
    {{0XEC, 0X5A, 0X67, 0X1C, 0XC1, 0XB6, 0X46, 0XFB, 0X8D, 0X91, 0X28, 0XD8, 0X22, 0X36, 0X75, 0X62}}
#define BLE_UUID_EPD_SVC 0x0001
#define BLE_UUID_EPD_CHAR 0x0002
#define BLE_UUID_APP_VER 0x0003
#define EPD_SVC_UUID_TYPE BLE_UUID_TYPE_VENDOR_BEGIN

#if defined(INKTAG_NRF_SDK17)
#define BLE_EPD_MAX_DATA_LEN (NRF_SDH_BLE_GATT_MAX_MTU_SIZE - 3)
#else
#define BLE_EPD_MAX_DATA_LEN (GATT_MTU_SIZE_DEFAULT - 3)
#endif

/* Historical command selectors remain reserved at their existing values.
 * Normal V2 packets are decoded by the shared protocol adapter. */
enum EPD_CMDS {
    EPD_CMD_SET_PINS = 0x00,
    EPD_CMD_INIT = 0x01,
    EPD_CMD_CLEAR = 0x02,
    EPD_CMD_SEND_COMMAND = 0x03,
    EPD_CMD_SEND_DATA = 0x04,
    EPD_CMD_REFRESH = 0x05,
    EPD_CMD_SLEEP = 0x06,
    EPD_CMD_STOP = 0x07,
    EPD_CMD_SET_SCREEN_CONFIG = 0x08,
    EPD_CMD_GET_SCREEN_CONFIG = 0x09,

    EPD_CMD_SET_TIME = 0x20,
    EPD_CMD_SET_WEEK_START = 0x21,

    EPD_CMD_WRITE_IMAGE = 0x30,
    EPD_CMD_WRITE_BLOCK = 0x31,
    EPD_CMD_QUERY_STATUS = 0x32,
    EPD_CMD_RESET_TRANSFER = 0x33,

    EPD_CMD_IMAGE_STORE_INFO = 0x40,
    EPD_CMD_IMAGE_STORE_BEGIN = 0x41,
    EPD_CMD_IMAGE_STORE_COMMIT = 0x42,
    EPD_CMD_IMAGE_STORE_LIST = 0x43,
    EPD_CMD_IMAGE_STORE_SHOW = 0x44,
    EPD_CMD_IMAGE_STORE_DELETE = 0x45,
    EPD_CMD_IMAGE_STORE_CLEAR_INCOMPATIBLE = 0x46,
    EPD_CMD_IMAGE_STORE_CLEAR_ALL = 0x47,
    EPD_CMD_IMAGE_STORE_ABORT = 0x48,
    EPD_CMD_IMAGE_SLIDESHOW_START = 0x49,
    EPD_CMD_IMAGE_SLIDESHOW_STOP = 0x4A,

    EPD_CMD_DEVICE_CAPABILITIES = 0x60,
    EPD_CMD_DEVICE_SETTINGS = 0x61,
    EPD_CMD_SET_BLUETOOTH_NAME = 0x62,
    EPD_CMD_SET_INPUT_ACTION = 0x63,
    EPD_CMD_SHOW_DEVICE_INFO = 0x67,
    EPD_CMD_BUTTON_SIMULATE = 0x69,
    EPD_CMD_SET_DEVICE_LABEL = 0x6C,

    EPD_CMD_SET_CONFIG = 0x90,
    EPD_CMD_SYS_RESET = 0x91,
    EPD_CMD_SYS_SLEEP = 0x92,
    EPD_CMD_DIAG = 0x93,
    EPD_CMD_OTA_RECOVERY = 0x94,
    EPD_CMD_CFG_ERASE = 0x99,
    EPD_CMD_SET_HOLIDAYS = 0xB6,
    EPD_CMD_GHOSTING_CLEAR = 0xB7,
    EPD_CMD_GHOSTING_STOP = 0xB8,
};

/* Caller-owned service state; preserve field order for existing consumers. */
typedef struct {
    uint16_t service_handle;
    ble_gatts_char_handles_t char_handles;
    ble_gatts_char_handles_t app_ver_handles;
    uint16_t conn_handle;
    uint16_t max_data_len;
    bool is_notification_enabled;
    epd_model_t *epd;
    epd_config_t config;
    epd_screen_config_t screen_config;
} ble_epd_t;

/* Scheduler copies this event value; the service outlives queued work. */
typedef struct {
    ble_epd_t *p_epd;
    uint32_t timestamp;
} epd_gui_update_event_t;
#define EPD_GUI_SCHD_EVENT_DATA_SIZE sizeof(epd_gui_update_event_t)

#if defined(INKTAG_NRF_SDK17)
void ble_epd_evt_handler(ble_evt_t const *p_ble_evt, void *p_context);
#define BLE_EPD_BLE_OBSERVER_PRIO 2
#define BLE_EPD_DEF(_name) \
    static ble_epd_t _name; \
    NRF_SDH_BLE_OBSERVER(_name##_obs, BLE_EPD_BLE_OBSERVER_PRIO, ble_epd_evt_handler, &_name)
#else
#define BLE_EPD_DEF(_name) static ble_epd_t _name;
#endif

/* Init restores configuration in RAM, then registers GATT. Vendor failures
 * propagate unchanged. NULL service returns NRF_ERROR_NULL. */
uint32_t ble_epd_init(ble_epd_t *p_epd);
void ble_epd_sleep_prepare(ble_epd_t *p_epd);
void ble_epd_on_ble_evt(ble_epd_t *p_epd, ble_evt_t *p_ble_evt);

/* Immediate vendor HVX admission result; success is not peer delivery.
 * Disconnected/unsubscribed takes precedence over the negotiated size limit. */
uint32_t ble_epd_string_send(ble_epd_t *p_epd, uint8_t *p_string, uint16_t length);

void ble_epd_process(ble_epd_t *p_epd);
void ble_epd_on_timer(ble_epd_t *p_epd, uint32_t previous_timestamp,
                      uint32_t timestamp, bool force_update);
bool ble_epd_startup_complete(void);
void ble_epd_application_ready(void);
bool ble_epd_screen_control_allowed(const ble_epd_t *p_epd);
bool ble_epd_restore_display(ble_epd_t *p_epd, uint32_t timestamp,
                             bool first_display_requested, bool ota_install_boot,
                             bool startup_complete);
#if defined(INKTAG_DEVICE_SETTINGS_ENABLED) && INKTAG_DEVICE_SETTINGS_ENABLED
void ble_epd_handle_button_gesture(ble_epd_t *p_epd, inktag_button_gesture_t gesture);
#endif

#endif /* __EPD_SERVICE_H__ */
