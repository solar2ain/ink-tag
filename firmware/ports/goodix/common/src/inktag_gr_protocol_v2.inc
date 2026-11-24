#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#line 1 "../Src/user/inktag_gr_protocol_v2.inc"
/* Included by inktag_gr_app.c so the protocol adapter can reuse the board's
 * private display, persistence and image-store state without exporting it. */

#include "inktag_gr_device_info_qr.h"
#if INKTAG_BUILD_PRO
#include "inktag_gr_p224_public_pro.h"
#endif
#include "inktag_gr_settings_store.h"
#include "inktag_image_upload_v5.h"

#ifndef INKTAG_GR_EXPERIMENTAL_DEVICE_PASSWORD
#define INKTAG_GR_EXPERIMENTAL_DEVICE_PASSWORD 0
#endif

#if INKTAG_BUILD_PRO
#include "inktag_gr_find_owner_state_pro.inc"
#else
static void v2_find_owner_clear(void) {}
static void v2_find_owner_service(void) {}
#endif
#line 45 "../Src/user/inktag_gr_protocol_v2.inc"
static inktag_status_v2_t v2_store_status(inktag_image_store_result_t result) {
    switch (result) {
        case INKTAG_IMAGE_STORE_OK: return INKTAG_STATUS_OK;
        case INKTAG_IMAGE_STORE_UNSUPPORTED: return INKTAG_STATUS_UNSUPPORTED;
        case INKTAG_IMAGE_STORE_BAD_ARGUMENT:
        case INKTAG_IMAGE_STORE_SIZE_MISMATCH: return INKTAG_STATUS_INVALID_REQUEST;
        case INKTAG_IMAGE_STORE_BUSY: return INKTAG_STATUS_BUSY;
        case INKTAG_IMAGE_STORE_NO_SPACE: return INKTAG_STATUS_NO_SPACE;
        case INKTAG_IMAGE_STORE_IO_ERROR: return INKTAG_STATUS_IO_ERROR;
        case INKTAG_IMAGE_STORE_CRC_MISMATCH: return INKTAG_STATUS_CHECKSUM_ERROR;
        case INKTAG_IMAGE_STORE_NOT_FOUND: return INKTAG_STATUS_NOT_FOUND;
        case INKTAG_IMAGE_STORE_LOW_POWER: return INKTAG_STATUS_LOW_POWER;
        default: return INKTAG_STATUS_INTERNAL_ERROR;
    }
}

static void v2_transfer_reset(bool erase_store) {
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE && erase_store) {
        inktag_image_store_abort();
    }
    if (s_v2_transfer.active &&
        (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
         s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)) {
        inktag_gr_device_info_qr_abort();
        if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            inktag_device_password_session_clear(&s_password);
    }
#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_abort_pro.inc"
#endif
#line 78 "../Src/user/inktag_gr_protocol_v2.inc"
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        if (s_epd && s_epd_gpio_ready) s_epd->drv->sleep(s_epd);
        epd_gpio_close_if_needed();
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_picture_io_active = false;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             false, s_status_now_ms);
    inktag_gr_health_stage(0x47524944u, INKTAG_GR_HEALTH_OPERATION_BLE); /* GRID */
}

static uint8_t v2_current_display_mode(void) {
    if (!s_display_state_loaded) return INKTAG_DISPLAY_MODE_DEVICE_INFO;
    switch (s_display_state.mode) {
        case EPD_DISPLAY_STATE_CALENDAR: return INKTAG_DISPLAY_MODE_CALENDAR;
        case EPD_DISPLAY_STATE_CLOCK: return INKTAG_DISPLAY_MODE_CLOCK;
        case EPD_DISPLAY_STATE_CLEAR: return INKTAG_DISPLAY_MODE_CLEAR;
        case EPD_DISPLAY_STATE_SLIDESHOW: return INKTAG_DISPLAY_MODE_SLIDESHOW;
        case EPD_DISPLAY_STATE_DEVICE_INFO: return INKTAG_DISPLAY_MODE_DEVICE_INFO;
        case EPD_DISPLAY_STATE_PICTURE:
        default:
            return s_display_state.argument == EPD_DISPLAY_STATE_NO_SOURCE ?
                   INKTAG_DISPLAY_MODE_DIRECT_IMAGE : INKTAG_DISPLAY_MODE_RESOURCE;
    }
}

static bool v2_schedule_work(uint8_t opcode, uint8_t transaction_id,
                             epd_work_type_t type, uint8_t argument,
                             uint32_t draw_timestamp) {
    if (s_epd_work_active || s_diag_scan_active ||
        (s_epd_work_pending && !s_epd_work_background)) {
        return false;
    }
    /* A foreground request owns the display once a client is connected. It
     * may replace a queued boot/clock/slideshow refresh that has not started;
     * otherwise every reconnect can keep returning BUSY while the same
     * background restore is repeatedly deferred. */
    if (!schedule_epd_work(type, argument, NULL)) {
        return false;
    }
    s_v2_work_opcode = opcode;
    s_v2_work_transaction = transaction_id;
    s_v2_work_type = type;
    s_epd_work_timestamp = draw_timestamp;
    uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY, 0};
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK, opcode, transaction_id,
                  progress, sizeof(progress));
    return true;
}

static bool v2_command_requires_idle_display(uint8_t opcode) {
    switch (opcode) {
        case INKTAG_CMD_SET_SCREEN_PROFILE:
        case INKTAG_CMD_SET_IO_CONFIG:
        case INKTAG_CMD_SET_DISPLAY_MODE:
        case INKTAG_CMD_DISPLAY_ACTION:
        case INKTAG_CMD_TRANSFER_BEGIN:
        case INKTAG_CMD_DELETE_RESOURCE:
        case INKTAG_CMD_CLEAR_RESOURCES:
        case INKTAG_CMD_SET_ACTIVATION_CODE:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_RUN_TEST:
        case INKTAG_CMD_SYSTEM_ACTION:
            return true;
        default:
            return false;
    }
}

static bool v2_aux_io_available(void) {
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE && INKTAG_BOARD_AUX_IO_QUALIFIED
    return inktag_gr_aux_io_supported();
#else
    return false;
#endif
}

static bool v2_activation_available(void) {
    return INKTAG_GR_ACTIVATION_IMPLEMENTED != 0 &&
           INKTAG_GR_ACTIVATION_BACKEND_AVAILABLE != 0 &&
           INKTAG_BOARD_ACTIVATION_QUALIFIED != 0 &&
           s_activation.supported;
}

static bool v2_password_available(void) {
    /* An older image may have installed a password before this build disabled
     * new enrollment. Keep AUTH visible for that retained authority; an
     * unreadable record must expose an explicit persistence error instead of
     * silently hiding the only explanation for the fail-closed command gate. */
    return (INKTAG_GR_EXPERIMENTAL_DEVICE_PASSWORD != 0 ||
            s_password_config.enabled || !s_password_config.valid) &&
        inktag_gr_board_identity() != INKTAG_GR_BOARD_UNKNOWN &&
#if INKTAG_BUILD_PRO
        v2_activation_available();
#else
        /* Retained ordinary V AUTH binds immutable identity, not Pro rights. */
        s_activation.identity_available &&
        INKTAG_GR_DEVICE_INFO_QR_IMPLEMENTED != 0 &&
        INKTAG_GR_DEVICE_INFO_QR_BACKEND_AVAILABLE != 0;
#endif
}

static bool v2_find_available(void) {
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
    return INKTAG_GR_FIND_BACKEND_AVAILABLE != 0 &&
           (INKTAG_BOARD_FIND_QUALIFIED != 0 ||
            INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0);
#else
    return false;
#endif
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_available_pro.inc"
#else
static bool v2_gui_asset_update_available(void) { return false; }

#endif
#line 198 "../Src/user/inktag_gr_protocol_v2.inc"
static inktag_status_v2_t v2_find_configuration_access(void) {
    return product_access(
        INKTAG_PRODUCT_FEATURE_FIND_CONFIGURATION,
        INKTAG_GR_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_QUALIFIED != 0 ||
            INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0,
        true);
}

static uint32_t v2_capabilities(void) {
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_DEVICE_NAME |
                            INKTAG_CAPABILITY_DEVICE_LABEL |
                            INKTAG_CAPABILITY_TELEMETRY |
                            INKTAG_CAPABILITY_TEST |
                            INKTAG_CAPABILITY_HEALTH;
    bool aux_available = v2_aux_io_available();
#if defined(INKTAG_GR_CLOCK_PARTIAL) && INKTAG_GR_CLOCK_PARTIAL
    /* Current exact configuration, independently of Pro. Actual foreground
     * and background refresh still use the common Pro admission rule. */
    if (gr_clock_partial_supported())
        capabilities |= INKTAG_CAPABILITY_CLOCK_PARTIAL_REFRESH;
#endif
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    /* A verified applied snapshot is observable without qualifying mutation.
     * Its report masks every editable role until the existing write gate opens. */
    if (inktag_gr_pin_routes_drive_ready())
        capabilities |= INKTAG_CAPABILITY_PIN_CONFIG;
#endif
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    if (gr_panel_diag_available()) capabilities |= INKTAG_CAPABILITY_DEBUG;
#endif
    bool nfc_available = false;
#if INKTAG_GR_NFC_IMPLEMENTED
#include "inktag_gr_nfc_protocol_capability_1_pro.inc"
#line 229 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
    const inktag_capability_gate_t activation_gate = {
        INKTAG_GR_ACTIVATION_IMPLEMENTED != 0,
        INKTAG_GR_ACTIVATION_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_ACTIVATION_QUALIFIED != 0,
        s_activation.supported,
    };
    const inktag_capability_gate_t lifecycle_gate = {
        INKTAG_GR_LIFECYCLE_IMPLEMENTED != 0,
        INKTAG_GR_LIFECYCLE_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        true,
    };
    const inktag_capability_gate_t input_gate = {
        true,
        INKTAG_GR_AUX_IO_BACKEND_AVAILABLE != 0 ||
            INKTAG_GR_NFC_IMPLEMENTED != 0,
        aux_available || nfc_available,
        aux_available || nfc_available,
    };
    const inktag_capability_gate_t io_gate = {
        true,
        INKTAG_GR_AUX_IO_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_AUX_IO_QUALIFIED != 0,
        aux_available,
    };
    const inktag_capability_gate_t nfc_gate = {
        INKTAG_GR_NFC_IMPLEMENTED != 0,
        INKTAG_GR_NFC_IMPLEMENTED != 0,
        INKTAG_BOARD_NFC_VARIANT_QUALIFIED != 0,
        nfc_available,
    };
    const inktag_capability_gate_t find_gate = {
        true,
        INKTAG_GR_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_QUALIFIED != 0,
        v2_find_available(),
    };
    const inktag_capability_gate_t experimental_find_gate = {
        true,
        INKTAG_GR_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0 &&
            INKTAG_BOARD_FIND_QUALIFIED == 0,
        v2_find_available(),
    };
    const inktag_capability_gate_t indicator_gate = {
        INKTAG_GR_INDICATOR_IMPLEMENTED != 0,
        INKTAG_GR_INDICATOR_BACKEND_AVAILABLE != 0,
        runtime_rgb_led_available(),
        status_led_available_colors() != INKTAG_STATUS_COLOR_OFF,
    };
    const inktag_capability_gate_t device_info_qr_gate = {
        INKTAG_GR_DEVICE_INFO_QR_IMPLEMENTED != 0,
        INKTAG_GR_DEVICE_INFO_QR_BACKEND_AVAILABLE != 0,
        true,
        true,
    };
    const inktag_capability_gate_t gui_asset_gate = {
        INKTAG_GR_GUI_ASSET_UPDATE_IMPLEMENTED != 0,
        INKTAG_GR_GUI_ASSET_UPDATE_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED != 0,
        v2_gui_asset_update_available(),
    };
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_ACTIVATION, &activation_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE, &lifecycle_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE_V2, &lifecycle_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INPUT, &input_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_IO, &io_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_NFC, &nfc_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_FIND, &find_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_EXPERIMENTAL_FIND,
        &experimental_find_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INDICATOR, &indicator_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_DEVICE_INFO_QR,
        &device_info_qr_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_GUI_ASSET_UPDATE,
        &gui_asset_gate);
#if INKTAG_TRANSFER_PACKBITS_ENABLED
    if (INKTAG_GR_TRANSFER_PACKBITS_IMPLEMENTED != 0 &&
        INKTAG_GR_TRANSFER_PACKBITS_BACKEND_AVAILABLE != 0 &&
        INKTAG_BOARD_TRANSFER_PACKBITS_RLE_QUALIFIED != 0)
        capabilities |= INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE;
#endif
    if (inktag_image_store_supported()) {
        capabilities |= INKTAG_CAPABILITY_RESOURCES | INKTAG_CAPABILITY_SLIDESHOW |
                        INKTAG_CAPABILITY_IMAGE_METADATA;
    }
#if defined(INKTAG_GOODIX_OTA)
    capabilities |= INKTAG_CAPABILITY_OTA_RECOVERY;
#endif
    if (v2_password_available()) capabilities |= INKTAG_CAPABILITY_DEVICE_PASSWORD;
    return capabilities;
}

static void v2_password_request(uint8_t opcode, uint8_t transaction_id,
    uint8_t flags, const uint8_t *payload, uint16_t length) {
    uint8_t response[INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES];
    uint16_t response_length = INKTAG_DEVICE_PASSWORD_STATUS_BYTES;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!v2_password_available()) status = INKTAG_STATUS_UNSUPPORTED;
    else if (flags != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (!s_password_config.valid) status = INKTAG_STATUS_PERSISTENCE_ERROR;
    else if (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE) {
        if (length != 1u || payload[0] > INKTAG_PASSWORD_DISABLE)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!inktag_device_password_begin(&s_password, &s_password_config,
                s_status_now_ms, payload[0], inktag_gr_security_random, NULL, response))
            status = INKTAG_STATUS_DENIED;
        else response_length = INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES;
    } else if (opcode == INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD) {
        if (!inktag_device_password_authorize(&s_password, &s_password_config,
                s_status_now_ms, s_activation.identity.device_id, payload, length))
            status = INKTAG_STATUS_DENIED;
    } else if (length != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_password_show_protocol_pro.inc"
#else

        status = INKTAG_STATUS_UNSUPPORTED;
#endif
#line 363 "../Src/user/inktag_gr_protocol_v2.inc"
}
    if (status == INKTAG_STATUS_OK &&
        response_length == INKTAG_DEVICE_PASSWORD_STATUS_BYTES &&
        !inktag_device_password_status(&s_password, &s_password_config,
                                      s_status_now_ms, response))
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    send_v2_response(opcode, transaction_id, status,
        status == INKTAG_STATUS_OK ? response : NULL,
        status == INKTAG_STATUS_OK ? response_length : 0u);
}

static void v2_send_gui_asset_info(uint8_t opcode,
                                   uint8_t transaction_id) {
    inktag_gr_gui_asset_info_t info;
    uint8_t response[INKTAG_GUI_ROLE_INFO_SIZE] = {0};
#if INKTAG_BUILD_PRO
    if (!v2_gui_asset_update_available() ||
#else
    if (!inktag_gr_gui_asset_backend_available() ||
#endif
#line 379 "../Src/user/inktag_gr_protocol_v2.inc"
        !inktag_gr_gui_asset_info(&info)) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    inktag_gui_role_info_t wire_info = {
        .base = {INKTAG_RELEASE_FONT_INFO_VERSION, info.schema, info.active_bank, 1u,
            info.generation, info.total_size, INKTAG_GR_USER_FONT_SIZE, info.package_crc32},
        .supported_schemas = INKTAG_RELEASE_FONT_SCHEMA_MASK,
        .role_version = INKTAG_RELEASE_FONT_ROLE_VERSION,
        .flags = INKTAG_RELEASE_FONT_FLAGS,
    };
    if (!inktag_gui_role_encode_info(&wire_info, response, sizeof(response))) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_hello(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[6] = {1u, INKTAG_PROTOCOL_V2_VERSION, 0, 0, 0, 0};
    put_u16_le(&payload[2], INKTAG_GR_MAX_DATA_LEN);
    put_u16_le(&payload[4], INKTAG_GR_MAX_DATA_LEN - 7u);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_capabilities(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[INKTAG_CAPABILITIES_EXTENDED_SIZE] = {0};
    uint16_t mode_mask = 0;
    uint16_t action_mask = 0;
    payload[0] = 1u;
    put_u32_le(&payload[1], v2_capabilities());
    for (uint8_t mode = INKTAG_DISPLAY_MODE_DEVICE_INFO;
         mode <= INKTAG_DISPLAY_MODE_DIRECT_IMAGE; mode++) {
        mode_mask |= (uint16_t)(1u << mode);
    }
    if (inktag_image_store_supported()) {
        mode_mask |= (uint16_t)((1u << INKTAG_DISPLAY_MODE_RESOURCE) |
                                (1u << INKTAG_DISPLAY_MODE_SLIDESHOW));
        action_mask |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_NEXT) |
                                  (1u << INKTAG_DISPLAY_ACTION_PAUSE) |
                                  (1u << INKTAG_DISPLAY_ACTION_RESUME));
    }
    action_mask |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_REFRESH) |
                              (1u << INKTAG_DISPLAY_ACTION_GHOST_CLEAN) |
                              (1u << INKTAG_DISPLAY_ACTION_SLEEP));
    put_u16_le(&payload[5], mode_mask);
    put_u16_le(&payload[7], action_mask);
    put_u16_le(&payload[9], INKTAG_GR_MAX_DATA_LEN);
    put_u16_le(&payload[11], INKTAG_GR_MAX_DATA_LEN - 7u);
    payload[13] = EPD_DEVICE_NAME_MAX_BYTES;
    payload[14] = EPD_DEVICE_LABEL_MAX_BYTES;
    put_u16_le(&payload[15], inktag_image_store_sector_count());
    put_u32_le(&payload[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
               INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION
#if INKTAG_TRANSFER_LZO1X_ENABLED
               | INKTAG_CAPABILITY_EXTENSION_TRANSFER_LZO1X_2000
#endif
    );
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_device_info(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[112] = {0};
    const char *board_id = inktag_firmware_family_id();
    uint8_t board_length = (uint8_t)strlen(board_id);
#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_name_report_1_pro.inc"
#else
    uint8_t name_length = (uint8_t)strlen(s_device_name);
    const uint8_t *name = (const uint8_t *)s_device_name;
#endif
#line 451 "../Src/user/inktag_gr_protocol_v2.inc"
    char label[INKTAG_DEVICE_LABEL_MAX_BYTES + 1u];
    uint8_t label_length = device_display_name(label);
    ble_gap_bdaddr_t address;
    bool address_valid = ble_gap_addr_get(&address) == SDK_SUCCESS;
    uint32_t app_version = ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
                           ((uint32_t)INKTAG_VERSION_MINOR << 8) |
                           INKTAG_VERSION_PATCH;
    uint32_t app_size = 0;
    uint32_t app_checksum = 0;
#if defined(INKTAG_GOODIX_OTA)
    dfu_image_info_t active;
    if (inktag_gr_active_image_info(&active)) {
        app_version = active.version;
        app_size = active.boot_info.bin_size;
        app_checksum = active.boot_info.check_sum;
    }
#endif
    payload[0] = 1u;
    payload[1] = board_length;
    payload[2] = name_length;
    payload[3] = label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        for (uint8_t i = 0; i < 6u; i++) payload[5u + i] = address.gap_addr.addr[5u - i];
    }
    put_u32_le(&payload[11], app_version);
    put_u32_le(&payload[15], app_size);
    put_u32_le(&payload[19], app_checksum);
    uint16_t offset = 23u;
    memcpy(&payload[offset], board_id, board_length);
    offset += board_length;
    memcpy(&payload[offset], name, name_length);
    offset += name_length;
    memcpy(&payload[offset], label, label_length);
    offset += label_length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, offset);
}

static void v2_send_status(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[16u + INKTAG_TEMPERATURE_WIRE_SIZE] = {0};
    uint16_t voltage = EPD_ReadVoltage();
    int8_t temperature = 0;
    gr_temperature_sources_t temperature_sources = read_temperature_sources();
    bool temperature_valid = select_effective_temperature(&temperature_sources,
                                                          &temperature);
    uint8_t flags = 0;
    uint8_t progress = 0;
    if (s_epd_work_active || s_epd_work_pending) flags |= 0x01u;
    if (s_slideshow_active) flags |= 0x02u;
    if (voltage > 0u) flags |= 0x04u;
    if (temperature_valid) flags |= 0x08u;
    if (s_v2_transfer.active) flags |= 0x10u;
    if (s_v2_transfer.active && s_v2_transfer.total_size > 0u) {
        progress = (uint8_t)((s_v2_transfer.written * 100u) / s_v2_transfer.total_size);
    }
    payload[0] = 1u;
    payload[1] = v2_current_display_mode();
    payload[2] = flags;
    payload[3] = progress;
    payload[4] = s_v2_transfer.active ? INKTAG_CMD_TRANSFER_WRITE : s_v2_work_opcode;
    payload[5] = s_v2_transfer.active ? s_v2_transfer.transaction_id : s_v2_work_transaction;
    payload[6] = s_slideshow_current_id;
    payload[7] = temperature_valid ? (uint8_t)temperature : 0u;
    put_u16_le(&payload[8], voltage);
    put_u32_le(&payload[10], s_slideshow_interval);
    put_u16_le(&payload[14], (uint16_t)s_timezone_minutes);
    inktag_temperature_wire_encode(&payload[16],
        temperature_sources.mcu_die.valid,
        temperature_sources.mcu_die.value_sixteenths,
        temperature_sources.panel_controller.valid,
        temperature_sources.panel_controller.value_sixteenths);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_health(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *request, uint16_t request_length) {
    if (request_length > 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST,
                         NULL, 0u);
        return;
    }
    uint8_t record_index = request_length == 1u ? request[0] : 0u;
    uint8_t payload[INKTAG_GR_HEALTH_WIRE_SIZE +
                    INKTAG_SOAK_TELEMETRY_WIRE_SIZE];
    uint16_t length = inktag_gr_health_encode(record_index, payload,
                                               INKTAG_GR_HEALTH_WIRE_SIZE);
    if (length == 0u || (record_index != 0u && record_index >= payload[20])) {
        send_v2_response(opcode, transaction_id,
                         length == 0u ? INKTAG_STATUS_INTERNAL_ERROR :
                                        INKTAG_STATUS_NOT_FOUND,
                         NULL, 0u);
        return;
    }
    length += inktag_soak_telemetry_encode(
        &payload[length], (uint16_t)(sizeof(payload) - length),
        s_status_now_ms);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     payload, length);
}

static void v2_encode_time(uint8_t payload[7]) {
    uint32_t utc_timestamp = s_timestamp - (int32_t)s_timezone_minutes * 60;
    put_u32_le(payload, utc_timestamp);
    put_u16_le(&payload[4], (uint16_t)s_timezone_minutes);
    payload[6] = s_config.week_start;
}

static void v2_send_time(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[7];
    /* Query is an existing BLE event, not a new periodic wake. Settle UTC
     * using the AON snapshot so ppm is visible at actual second edges. */
    s_clock_tick_due = true;
    clock_process();
    v2_encode_time(payload);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static bool v2_test_time_mode(uint8_t mode) {
    return mode == INKTAG_DISPLAY_MODE_CALENDAR ||
           mode == INKTAG_DISPLAY_MODE_CLOCK;
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_v2_send_nfc_channel_snapshot_pro.inc"
#else
static void v2_send_nfc_channel_snapshot(uint8_t opcode,
                                         uint8_t transaction_id,
                                         uint16_t length) {
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
#endif
#line 590 "../Src/user/inktag_gr_protocol_v2.inc"


static void v2_list_tests(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[9] = {1u, 0u};
    uint8_t count = 0u;
    if (runtime_led_diag_available()) response[2u + count++] = INKTAG_TEST_LED_CHANNEL;
#if defined(INKTAG_GOODIX_GR5515)
    if (v2_aux_io_available())
        response[2u + count++] = INKTAG_TEST_INPUT_SIMULATE;
#if INKTAG_BUILD_PRO
    if (inktag_gr_nfc_supported())
        response[2u + count++] = INKTAG_TEST_NFC_CHANNEL_SNAPSHOT;
#endif
#endif
    if (runtime_led_diag_available()
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_locate_inventory_pro.inc"


#endif
#line 608 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
       ) response[2u + count++] = INKTAG_TEST_DEVICE_LOCATE;
    response[2u + count++] = INKTAG_TEST_TIME_SHIFT;
    response[2u + count++] = INKTAG_TEST_TIME_SET;
    response[1] = count;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, (uint16_t)(2u + response[1]));
}

#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_input_simulation_busy_pro.inc"
#else
static bool v2_input_simulation_busy(void){return false;}
#endif
#line 626 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_input_simulation_begin_pro.inc"
#else
static void v2_input_simulation_begin(uint8_t transaction,uint8_t selector){(void)selector;send_v2_response(INKTAG_CMD_RUN_TEST,transaction,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 664 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_input_simulation_service_pro.inc"
#else
static void v2_input_simulation_service(void){}
#endif
#line 767 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_input_simulation_cancel_pro.inc"
#else
static void v2_input_simulation_cancel(uint8_t transaction,const uint8_t*payload,uint16_t length){(void)payload;(void)length;send_v2_response(INKTAG_CMD_CANCEL,transaction,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 796 "../Src/user/inktag_gr_protocol_v2.inc"
#endif

static void v2_run_test(uint8_t opcode, uint8_t transaction_id,
                        const uint8_t *payload, uint16_t length) {
    if (length >= 1u && payload[0] == INKTAG_TEST_RESERVED_03) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    if (length >= 1u && payload[0] == INKTAG_TEST_NFC_CHANNEL_SNAPSHOT) {
        v2_send_nfc_channel_snapshot(opcode, transaction_id, length);
        return;
    }
    if (length >= 1u && payload[0] == INKTAG_TEST_LED_CHANNEL) {
        bool accepted = length == 2u && diag_set_led_mask(payload[1]);
        send_v2_response(opcode, transaction_id,
                         accepted ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_REQUEST,
                         NULL, 0);
        if (accepted) {
            send_v2_event(INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK,
                          opcode, transaction_id, &payload[1], 1u);
        }
        return;
    }
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_run_test_pro.inc"
#else
    if (length >= 1u && payload[0] == INKTAG_TEST_INPUT_SIMULATE) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u);
        return;
    }
#endif
#line 848 "../Src/user/inktag_gr_protocol_v2.inc"

#endif
    if (length >= 1u && payload[0] == INKTAG_TEST_DEVICE_LOCATE) {
        bool led_available = runtime_led_diag_available();
        bool accepted;
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_locate_available_pro.inc"


#endif
#line 857 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
        if (length != 2u || payload[1] > 1u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            return;
        }
        if (!led_available) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0);
            return;
        }
        if (payload[1] == 0u) {
            inktag_status_locate_stop(&s_status, s_status_now_ms);
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_locate_stop_pro.inc"


#endif
#line 872 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
#if defined(INKTAG_GOODIX_GR5515)
            status_timer_update();
#endif
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0);
            return;
        }
        inktag_status_locate_start(&s_status, s_status_now_ms);
        accepted = led_available;
#if INKTAG_GR_AUX_IO_BACKEND_AVAILABLE
#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_locate_start_pro.inc"


#endif
#line 887 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
        if (!accepted) inktag_status_locate_stop(&s_status, s_status_now_ms);
#if defined(INKTAG_GOODIX_GR5515)
        status_timer_update();
#endif
        send_v2_response(opcode, transaction_id,
                         accepted ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_STATE,
                         NULL, 0);
        return;
    }
    if (length < 2u || !v2_test_time_mode(payload[1])) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    if (s_epd == NULL) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0);
        return;
    }
    if (s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) ||
        s_diag_scan_active || s_v2_transfer.active) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0);
        return;
    }

    uint32_t next_timestamp = s_timestamp;
    int16_t next_timezone = s_timezone_minutes;
    uint8_t next_week_start = s_config.week_start;
    if (payload[0] == INKTAG_TEST_TIME_SHIFT) {
        if (length != 6u) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            return;
        }
        int32_t delta_seconds = (int32_t)read_u32_le(&payload[2]);
        int64_t candidate = (int64_t)s_timestamp + delta_seconds;
        if (delta_seconds == 0 || delta_seconds < -(366 * 86400) ||
            delta_seconds > 366 * 86400 || candidate < 946684800ll ||
            candidate > 4102444799ll) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            return;
        }
        next_timestamp = (uint32_t)candidate;
    } else if (payload[0] == INKTAG_TEST_TIME_SET) {
        if (length != 9u || payload[8] > 6u) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            return;
        }
        uint32_t utc_timestamp = read_u32_le(&payload[2]);
        next_timezone = (int16_t)read_u16_le(&payload[6]);
        int64_t candidate = (int64_t)utc_timestamp + (int32_t)next_timezone * 60;
        if (next_timezone < -840 || next_timezone > 840 ||
            candidate < 946684800ll || candidate > 4102444799ll) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            return;
        }
        next_timestamp = (uint32_t)candidate;
        next_week_start = payload[8];
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
        return;
    }

    uint32_t previous_timestamp = s_timestamp;
    if (!clock_save_time_candidate(next_timestamp, next_timezone, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0);
        return;
    }
    next_timestamp += s_timestamp - previous_timestamp;
    uint8_t gui_mode = payload[1] == INKTAG_DISPLAY_MODE_CALENDAR ?
                       MODE_CALENDAR : MODE_CLOCK;
    if (!v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                          gui_mode, next_timestamp)) {
        /* No candidate has reached RAM/Find/week-start. Restore the record
         * on the still-running old timeline if late display admission fails. */
        bool restored = clock_save_time_candidate(s_timestamp,
                                                   s_timezone_minutes, true);
        send_v2_response(opcode, transaction_id,
                         restored ? INKTAG_STATUS_BUSY :
                                    INKTAG_STATUS_PERSISTENCE_ERROR,
                         NULL, 0);
        return;
    }
    set_timestamp(next_timestamp);
    s_timezone_minutes = next_timezone;
    s_config.week_start = next_week_start;
    s_time_valid = true;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
    s_findmy_identity_refresh_pending =
        s_findmy_config.enabled && s_findmy_identity_available;
#endif
    if (payload[0] == INKTAG_TEST_TIME_SET) epd_config_write(&s_config);
    uint8_t response[7];
    v2_encode_time(response);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

#if INKTAG_GR_NFC_PACKAGED
#include "inktag_gr_nfc_protocol_packaged_1_pro.inc"
#line 1115 "../Src/user/inktag_gr_protocol_v2.inc"
#else
static void v2_send_nfc_capabilities(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_send_nfc_status(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_get_nfc_ndef(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_set_nfc_ndef(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
#endif

#if defined(INKTAG_GOODIX_GR5515)
static uint16_t v2_button_action_mask(void) {
    return (uint16_t)((1u << INKTAG_INPUT_ACTION_DISABLED) |
                      (1u << INKTAG_INPUT_ACTION_NEXT_PAGE) |
                      (1u << INKTAG_INPUT_ACTION_SHOW_SLOT) |
                      (1u << INKTAG_INPUT_ACTION_SHOW_DEVICE_INFO) |
                      (1u << INKTAG_INPUT_ACTION_SHOW_CALENDAR) |
                      (1u << INKTAG_INPUT_ACTION_SHOW_CLOCK) |
                      (1u << INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW) |
                      (1u << INKTAG_INPUT_ACTION_STOP) |
                      (1u << INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS) |
                      (1u << INKTAG_INPUT_ACTION_PREVIOUS_PAGE));
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_get_input_config_pro.inc"
#else
static void v2_get_input_config(uint8_t opcode,uint8_t transaction_id){send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 1185 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_set_input_config_pro.inc"
#else
static void v2_set_input_config(uint8_t opcode,uint8_t transaction_id,const uint8_t*payload,uint16_t length){(void)payload;(void)length;send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 1229 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_get_aux_io_config_pro.inc"
#else
static void v2_get_aux_io_config(uint8_t opcode,uint8_t transaction_id){send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 1246 "../Src/user/inktag_gr_protocol_v2.inc"

#if INKTAG_BUILD_PRO
#include "inktag_gr_custom_set_aux_io_config_pro.inc"
#else
static void v2_set_aux_io_config(uint8_t opcode,uint8_t transaction_id,const uint8_t*payload,uint16_t length){(void)payload;(void)length;send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u);}
#endif
#line 1277 "../Src/user/inktag_gr_protocol_v2.inc"
#endif

static void v2_get_io_config(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (length == 1u && payload[0] == 2u) {
        uint8_t response[36];
        inktag_status_v2_t status = INKTAG_STATUS_UNSUPPORTED;
        if (inktag_gr_pin_routes_drive_ready())
            status = inktag_gr_pin_routes_status(response) ? INKTAG_STATUS_OK :
                INKTAG_STATUS_PERSISTENCE_ERROR;
        if (status == INKTAG_STATUS_OK && !INKTAG_BOARD_PIN_CONFIG_QUALIFIED)
            response[2] = response[3] = 0u; /* Every SET remains unqualified. */
        send_v2_response(opcode, transaction_id, status,
            status == INKTAG_STATUS_OK ? response : NULL,
            status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        return;
    }
#endif
    if (length != 1u || payload[0] != 3u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
#if !INKTAG_BUILD_PRO
    /* Existing v1 envelope, read-only stock facts. No maintained GR backend
     * admits a stock key; retained custom AUX remains opaque and untouched. */
    uint8_t stock[INKTAG_GR_PIN_AUX_BYTES];
    inktag_gr_pin_aux_default(stock);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, stock, sizeof(stock));
    return;
#endif
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    /* PIN_CONFIG clients need the effective protected AUX tuple to reject
     * display/LED conflicts, even on a part without qualified AUX mutations. */
    if (inktag_gr_pin_routes_required()) {
        uint8_t response[INKTAG_GR_PIN_AUX_BYTES];
        bool valid = inktag_gr_pin_routes_drive_ready() &&
            inktag_gr_pin_routes_aux(response);
        send_v2_response(opcode, transaction_id,
            valid ? INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR,
            valid ? response : NULL, valid ? sizeof(response) : 0u);
        return;
    }
#endif
#if defined(INKTAG_GOODIX_GR5515)
    v2_get_aux_io_config(opcode, transaction_id);
#else
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
}

static void v2_set_io_config(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (length && payload[0] == 2u) {
        inktag_status_v2_t status = INKTAG_STATUS_UNSUPPORTED;
        uint8_t response[36];
        if (!((length == 2u && payload[1] == 0u) ||
              (length == 18u && payload[1] == 1u))) {
            status = INKTAG_STATUS_INVALID_REQUEST;
        } else if (INKTAG_BOARD_PIN_CONFIG_QUALIFIED && inktag_gr_pin_routes_available()) {
            inktag_pin_config_t old = *inktag_gr_pin_routes_peek();
            inktag_gr_pin_storage_result_t result = inktag_gr_pin_routes_set(
                length == 18u ? payload + 2u : NULL, length == 18u ? 16u : 0u,
                pin_config_io_busy());
            switch (result) {
                case INKTAG_GR_PIN_STORAGE_OK: status = INKTAG_STATUS_OK; break;
                case INKTAG_GR_PIN_STORAGE_INVALID: status = INKTAG_STATUS_INVALID_REQUEST; break;
                case INKTAG_GR_PIN_STORAGE_BUSY: status = INKTAG_STATUS_BUSY; break;
                case INKTAG_GR_PIN_STORAGE_LOW_POWER: status = INKTAG_STATUS_LOW_POWER; break;
                case INKTAG_GR_PIN_STORAGE_NO_SPACE: status = INKTAG_STATUS_NO_SPACE; break;
                default: status = INKTAG_STATUS_PERSISTENCE_ERROR; break;
            }
            const inktag_pin_config_t *next = inktag_gr_pin_routes_peek();
            bool changed = next && (old.active_low != next->active_low ||
                memcmp(old.pins, next->pins, sizeof(old.pins)));
            if ((status == INKTAG_STATUS_OK && changed) || !next) {
                if (!pin_config_apply(&old)) status = INKTAG_STATUS_PERSISTENCE_ERROR;
            } else inktag_gr_pin_routes_mark_applied(true);
            if (status == INKTAG_STATUS_OK && !inktag_gr_pin_routes_status(response))
                status = INKTAG_STATUS_PERSISTENCE_ERROR;
        }
        send_v2_response(opcode, transaction_id, status,
            status == INKTAG_STATUS_OK ? response : NULL,
            status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        return;
    }
    if (length != INKTAG_AUX_IO_WIRE_SIZE || payload[0] != INKTAG_AUX_IO_VERSION) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (inktag_gr_pin_routes_required() && pin_config_io_busy()) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
#endif
#if defined(INKTAG_GOODIX_GR5515)
    v2_set_aux_io_config(opcode, transaction_id, payload, length);
#else
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
#endif
}

static void v2_send_test_status(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[3] = {
        1u,
        (s_epd_work_active || s_epd_work_pending) ? 1u : 0u,
        v2_current_display_mode(),
    };
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_screen(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[EPD_SCREEN_WIRE_SIZE];
    epd_screen_status_t status = epd_screen_config_encode(&s_screen_config, payload);
    send_v2_response(opcode, transaction_id,
                     status == EPD_SCREEN_STATUS_OK ? INKTAG_STATUS_OK : INKTAG_STATUS_INTERNAL_ERROR,
                     status == EPD_SCREEN_STATUS_OK ? payload : NULL,
                     status == EPD_SCREEN_STATUS_OK ? sizeof(payload) : 0u);
}

static inktag_status_v2_t v2_screen_commit(const epd_screen_config_t *requested,
                                        bool *reply) {
    inktag_image_store_profile_view_t images;
    inktag_gr_pin_candidate_t routes;
    inktag_status_v2_t status = INKTAG_STATUS_INCOMPATIBLE;
    *reply = true;
    status = screen_control_access(requested,
        inktag_gr_board_identity() != INKTAG_GR_BOARD_UNKNOWN);
    if (status != INKTAG_STATUS_OK) return status;
    status = INKTAG_STATUS_INCOMPATIBLE;
    if (s_screen_save_active || s_screen_save_owned || s_epd_work_active || (s_epd_work_pending && !s_epd_work_background) ||
        s_v2_transfer.active || s_picture_io_active || s_diag_scan_active ||
        s_system_action_pending || inktag_image_store_busy() ||
        !inktag_gr_epd_pins_idle()
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
        || s_panel_temperature_sample_active
#endif
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        || gr_panel_diag_busy()
#endif
        ) return INKTAG_STATUS_BUSY;
    s_screen_save_active = s_screen_save_owned = true;
    if (!inktag_image_store_profile_allowed(requested)) goto done;
    const bool configured = inktag_gr_pin_routes_required();
    if (configured) {
        if (!inktag_gr_pin_routes_prepare(requested, &routes)) {
            status = INKTAG_STATUS_IO_ERROR;
            goto done;
        }
        if (!routes.busy_known) goto done;
    }
    inktag_image_store_result_t prepared =
        inktag_image_store_prepare_profile(requested, &images);
    bool publish_images = prepared == INKTAG_IMAGE_STORE_OK ||
                          prepared == INKTAG_IMAGE_STORE_NO_SPACE;
    /* profile_allowed already established unchanged record stride for an
     * unavailable store. Persist the screen without publishing a fabricated
     * empty/successful image view. Fresh IO failures on a ready store still
     * abort, and BUSY/argument errors are never bypassed. */
    if (!publish_images && (inktag_image_store_supported() ||
        (prepared != INKTAG_IMAGE_STORE_UNSUPPORTED &&
         prepared != INKTAG_IMAGE_STORE_IO_ERROR))) {
        status = prepared == INKTAG_IMAGE_STORE_BUSY ? INKTAG_STATUS_BUSY :
            prepared == INKTAG_IMAGE_STORE_IO_ERROR ? INKTAG_STATUS_IO_ERROR :
            INKTAG_STATUS_INCOMPATIBLE;
        goto done;
    }
    if (!s_screen_save_owned) goto done;
    epd_gpio_close_if_needed();
    status = inktag_gr_screen_commit(requested);
    if (status == INKTAG_STATUS_PERSISTENCE_ERROR || !s_screen_save_owned) {
        inktag_image_store_invalidate();
        inktag_gr_pin_routes_invalidate();
        memset(&s_screen_config, 0, sizeof(s_screen_config));
        s_epd = NULL;
        s_boot_restore_pending = s_boot_restore_due = false;
        goto done;
    }
    if (status != INKTAG_STATUS_OK) goto done;
    /* Only memory publication follows the one durable screen transaction.
     * Never refresh routes or rescan Flash after commit, nor rewrite ID4. */
    if (s_epd_work_pending && s_epd_work_background) {
        s_epd_work_pending = false;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_background = false;
    }
    s_boot_restore_pending = s_boot_restore_due = s_native_refresh_pending = false;
    slideshow_stop(false);
    s_screen_config = *requested;
    if (configured) inktag_gr_pin_routes_publish(&routes);
    inktag_gr_epd_pin_screen(&s_screen_config);
    s_epd = epd_find(&s_screen_config);
    if (publish_images) inktag_image_store_publish_profile(&images);
#if INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE
    s_panel_temperature_cache.valid = false;
    s_panel_temperature_sample_pending = true;
#endif
done:
    *reply = s_screen_save_owned;
    s_screen_save_owned = false;
    s_screen_save_active = false;
    return status;
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_activation_protocol_pro.inc"
#else
static void v2_send_activation_status(uint8_t opcode, uint8_t transaction_id) {
    /* Ordinary immutable ID is read-only metadata for password AUTH. The
     * base encoder cannot expose a Pro grant, token or challenge. */
    uint8_t payload[INKTAG_ACTIVATION_STATUS_WIRE_SIZE];
    inktag_activation_encode_status(&s_activation, payload);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}
static void v2_send_activation_request(uint8_t opcode, uint8_t transaction_id, bool rotate) {
    (void)rotate;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_set_activation_code(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
#endif
#line 1528 "../Src/user/inktag_gr_protocol_v2.inc"
static void v2_send_identity_settings(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[64] = {0};
    char label[INKTAG_DEVICE_LABEL_MAX_BYTES + 1u];
    uint8_t label_length = device_display_name(label);
#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_name_report_2_pro.inc"
#else
    uint8_t name_length = (uint8_t)strlen(s_device_name);
    const uint8_t *name = (const uint8_t *)s_device_name;
#endif
#line 1536 "../Src/user/inktag_gr_protocol_v2.inc"
    ble_gap_bdaddr_t address;
    bool address_valid = ble_gap_addr_get(&address) == SDK_SUCCESS;
    payload[0] = INKTAG_SETTINGS_SECTION_IDENTITY;
    payload[1] = EPD_DEVICE_SETTINGS_VERSION;
    payload[2] = name_length;
    payload[3] = label_length;
    payload[4] = address_valid ? 1u : 0u;
    if (address_valid) {
        for (uint8_t i = 0; i < 6u; i++) payload[5u + i] = address.gap_addr.addr[5u - i];
    }
    uint16_t offset = 11u;
    memcpy(&payload[offset], name, name_length);
    offset += name_length;
    memcpy(&payload[offset], label, label_length);
    offset += label_length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, offset);
}

static void v2_send_resource_info(uint8_t opcode, uint8_t transaction_id) {
    epd_model_t *model = image_store_model();
    if (!model) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
        return;
    }
    uint32_t frame_bytes = image_store_expected_payload(model);
    inktag_image_store_status_t status;
    uint16_t allocatable;
    inktag_image_store_result_t result = image_store_visible_status(
        &s_screen_config, frame_bytes, &status, &allocatable);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0);
        return;
    }
    uint8_t payload[34] = {0};
    payload[0] = 1u;
    put_u16_le(&payload[1], model->width);
    put_u16_le(&payload[3], model->height);
    put_u32_le(&payload[5], frame_bytes);
    put_u16_le(&payload[9], status.required_sectors);
    put_u16_le(&payload[11], status.total_sectors);
    put_u16_le(&payload[13], status.free_sectors);
    put_u16_le(&payload[15], status.largest_free_run);
    put_u16_le(&payload[17], status.saved_count);
    put_u16_le(&payload[19], status.compatible_count);
    put_u16_le(&payload[21], status.playable_count);
    put_u16_le(&payload[23], status.incompatible_count);
    payload[25] = s_slideshow_active ? 1u : 0u;
    put_u32_le(&payload[26], s_slideshow_interval);
    put_u16_le(&payload[30], allocatable);
    put_u16_le(&payload[32], inktag_image_store_fixed_slot_count());
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_list_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    uint8_t after_id = length > 0u ? payload[0] : UINT8_MAX;
    uint8_t record_id;
    inktag_image_store_header_t header;
    inktag_image_store_result_t next = image_store_next_visible(after_id, &record_id, &header);
    if (next == INKTAG_IMAGE_STORE_OK) {
        epd_screen_config_t stored;
        bool decoded = epd_screen_config_decode(header.screen_config, EPD_SCREEN_WIRE_SIZE,
                                                &stored) == EPD_SCREEN_STATUS_OK;
        uint8_t name_length = 0u;
        while (name_length < INKTAG_IMAGE_STORE_NAME_SIZE && header.name[name_length] != '\0') {
            name_length++;
        }
        uint8_t response[64] = {0};
        response[0] = 0u;
        response[1] = record_id;
        response[2] = header.content_type;
        response[3] = header.format;
        response[4] = header.segment_count;
        response[5] = decoded && screen_config_matches(header.screen_config) ? 1u : 0u;
        response[6] = name_length;
        put_u16_le(&response[7], decoded ? stored.width : 0u);
        put_u16_le(&response[9], decoded ? stored.height : 0u);
        put_u32_le(&response[11], header.payload_size);
        put_u32_le(&response[15], header.created_at);
        memcpy(&response[19], header.name, name_length);
        response[19u + name_length] = inktag_image_reading_rotation(header.black_mask);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)(20u + name_length));
        return;
    }
    if (next != INKTAG_IMAGE_STORE_NOT_FOUND) {
        send_v2_response(opcode, transaction_id, v2_store_status(next), NULL, 0u);
        return;
    }
    uint8_t done[34] = {1u, UINT8_MAX};
    inktag_image_store_result_t occupied = inktag_image_store_occupancy(done + 2u, 32u);
    if (occupied != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(occupied), NULL, 0u);
        return;
    }
    uint16_t done_length = 2u + (inktag_image_store_fixed_slot_count() + 7u) / 8u;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, done, done_length);
}

static void v2_read_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    if (length != 6u || payload[5] == 0u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    inktag_image_store_header_t header;
    inktag_image_store_result_t result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0);
        return;
    }
    if (!image_store_record_visible(payload[0])) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0);
        return;
    }
    uint32_t offset = read_u32_le(&payload[1]);
    uint16_t requested = payload[5];
    if (requested > INKTAG_GR_MAX_DATA_LEN - 12u) requested = INKTAG_GR_MAX_DATA_LEN - 12u;
    if (offset >= header.payload_size) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    if (offset + requested > header.payload_size) {
        requested = (uint16_t)(header.payload_size - offset);
    }
    uint8_t response[INKTAG_GR_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    response[0] = payload[0];
    put_u32_le(&response[1], offset);
    put_u32_le(&response[5], header.payload_size);
    result = inktag_image_store_read(payload[0], offset, &response[9], requested);
    send_v2_response(opcode, transaction_id, v2_store_status(result),
                     result == INKTAG_IMAGE_STORE_OK ? response : NULL,
                     result == INKTAG_IMAGE_STORE_OK ? (uint16_t)(9u + requested) : 0u);
}

static void v2_transfer_begin(uint8_t opcode, uint8_t transaction_id,
                              const uint8_t *payload, uint16_t length) {
    bool foreground_work_pending = s_epd_work_pending && !s_epd_work_background;
    inktag_transfer_descriptor_t descriptor;
    if (length < 20u || s_v2_transfer.active || s_epd_work_active ||
        foreground_work_pending) {
        send_v2_response(opcode, transaction_id,
                         s_v2_transfer.active || s_epd_work_active || foreground_work_pending ?
                         INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST,
                         NULL, 0);
        return;
    }
    uint8_t object_type = payload[0];
    inktag_stored_image_v5_descriptor_t stored;
    epd_screen_config_t stored_screen;
    if (object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
        object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        object_type != INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR &&
        object_type != INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE &&
        object_type != INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED,
                         NULL, 0u);
        return;
    }
    inktag_status_v2_t descriptor_status = inktag_transfer_descriptor_decode(payload, length, &descriptor);
    if (descriptor_status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, descriptor_status, NULL, 0u);
        return;
    }
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        inktag_status_v2_t decoded = inktag_stored_image_v5_descriptor_decode(payload, length, &stored);
        if (decoded != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, decoded, NULL, 0u);
            return;
        }
        if (stored.content_type > INKTAG_IMAGE_CONTENT_MAX ||
            epd_screen_config_decode(stored.screen_profile, EPD_SCREEN_WIRE_SIZE, &stored_screen) != EPD_SCREEN_STATUS_OK ||
            !screen_config_matches(stored.screen_profile)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        decoded = image_store_write_access(&stored_screen, descriptor.total_size, stored.replace_slot);
        if (decoded != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, decoded, NULL, 0u); return;
        }
    }
    inktag_power_operation_t power_operation =
        object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
        object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
        object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ?
            INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY;
    if (!inktag_gr_health_operation_allowed(power_operation, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER,
                         NULL, 0u);
        return;
    }
    epd_model_t *model = NULL;
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_admit_pro.inc"
#else

        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
#endif
#line 1747 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_password_protocol_begin_1_pro.inc"
#else

        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
#endif
#line 1760 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        if (length != 20u ||
            !inktag_device_info_qr_transfer_valid(&descriptor)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        if (!s_password_config.valid ||
            (s_password_config.enabled && descriptor.total_size +
             INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES >
             INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES)) {
            send_v2_response(opcode, transaction_id,
                s_password_config.valid ? INKTAG_STATUS_NO_SPACE :
                                          INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
    } else {
        model = image_store_model();
        if (!model) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
    }
    uint8_t expected_segments = model != NULL &&
        (model->transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
         model->transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
         model->transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) ? 2u : 1u;
#if INKTAG_EPD_ENABLE_JD79665CA
    bool invalid_e6_segments = model != NULL &&
        model->transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP &&
        ((descriptor.black_mask & 0x03u) != 0x01u ||
         descriptor.segment_size[0] != descriptor.total_size / 2u ||
         descriptor.segment_size[1] != descriptor.total_size / 2u);
#else
    bool invalid_e6_segments = false;
#endif
    if (model != NULL &&
        (descriptor.format != model->transfer_format ||
         descriptor.segment_count != expected_segments ||
         descriptor.segment_count == 0u || descriptor.segment_count > 2u ||
         descriptor.segment_size[0] +
                 (descriptor.segment_count > 1u ?
                      descriptor.segment_size[1] : 0u) !=
             descriptor.total_size ||
         descriptor.total_size != image_store_expected_payload(model) ||
         invalid_e6_segments)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE, NULL, 0);
        return;
    }
    /* A valid explicit direct/store/QR transfer owns the foreground. It may
     * replace a queued boot/clock/slideshow refresh, which must never leak a
     * BUSY result or later steal the client's requested display. */
    discard_pending_background_epd_work();
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_v2_transfer.active = true;
    s_v2_transfer.object_type = object_type;
    s_v2_transfer.transaction_id = transaction_id;
    s_v2_transfer.format = descriptor.format;
    s_v2_transfer.segment_count = descriptor.segment_count;
    s_v2_transfer.black_mask = descriptor.black_mask;
    s_v2_transfer.resource_id = UINT8_MAX;
    s_v2_transfer.total_size = descriptor.total_size;
    s_v2_transfer.expected_crc = descriptor.expected_crc;
    s_v2_transfer.running_crc = 0xFFFFFFFFu;
    s_v2_transfer.segment_size[0] = descriptor.segment_size[0];
    s_v2_transfer.segment_size[1] = descriptor.segment_size[1];
    s_v2_transfer.last_activity_ms = s_status_now_ms;
    inktag_gr_health_stage(
                           object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
                           object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
                           object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
                           object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ?
                               0x47525354u : 0x47525458u,
                           object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
                           object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
                           object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
                           object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ?
                               INKTAG_GR_HEALTH_OPERATION_STORAGE :
                               INKTAG_GR_HEALTH_OPERATION_TRANSFER); /* GRST/GRTX */
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint8_t response[5] = {object_type, UINT8_MAX, 0, 0, 0};
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_begin_pro.inc"
#else

        status = INKTAG_STATUS_UNSUPPORTED;
#endif
#line 1854 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_password_protocol_begin_2_pro.inc"
#else

        status = INKTAG_STATUS_UNSUPPORTED;
#endif
#line 1856 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        status = inktag_gr_device_info_qr_begin(&descriptor);
        response[1] = descriptor.black_mask;
    } else if (object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        slideshow_stop(false);
        s_picture_io_active = true;
        if (!epd_gpio_open_if_needed()) status = epd_control_access();
        else {




            s_epd = epd_init(&s_screen_config);

            if (!s_epd) status = INKTAG_STATUS_INCOMPATIBLE;
            else init_panel_for_display(s_epd);
        }
    } else {
        uint8_t id = UINT8_MAX;
        inktag_image_store_status_t admitted;
        inktag_image_store_result_t checked = inktag_image_store_status(
            &stored_screen, descriptor.total_size, &admitted);
        if (checked != INKTAG_IMAGE_STORE_OK || !s_v2_transfer.active) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, checked == INKTAG_IMAGE_STORE_OK
                ? INKTAG_STATUS_BUSY : v2_store_status(checked), NULL, 0u);
            return;
        }
        s_v2_transfer.image_saved_count = admitted.saved_count;
        s_v2_transfer.replacing_existing = stored.replace_slot != UINT8_MAX &&
            image_store_record_ordinal(stored.replace_slot, &s_v2_transfer.image_existing_ordinal);
        inktag_image_store_result_t result = inktag_image_store_begin_replace(
            &stored_screen, descriptor.format,
            descriptor.segment_count,
            (uint8_t)(descriptor.black_mask & INKTAG_IMAGE_STORED_FLAGS_MASK),
            stored.content_type, descriptor.segment_size,
            descriptor.total_size, s_v2_transfer.expected_crc,
            s_timestamp, stored.name, stored.replace_slot, &id);
        status = v2_store_status(result);
        s_v2_transfer.resource_id = id;
        response[1] = id;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, status, NULL, 0);
        return;
    }
    /* Store-only is still a user-visible transfer. Keep the same bounded
     * transfer scene for both object types until commit, abort, timeout,
     * disconnect or an I/O failure resets the transaction. */
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             true, s_status_now_ms);
    put_u16_le(&response[2], INKTAG_GR_MAX_DATA_LEN - 7u);
    response[4] = descriptor.segment_count;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

typedef struct {
    uint32_t offset;
    inktag_status_v2_t status;
} v2_transfer_sink_context_t;

static bool v2_transfer_accept_decoded(void *opaque, uint8_t *data,
                                       uint16_t data_length) {
    v2_transfer_sink_context_t *context =
        (v2_transfer_sink_context_t *)opaque;
    if (s_v2_transfer.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE)
        s_v2_transfer.running_crc = inktag_crc32_update(
            s_v2_transfer.running_crc, data, data_length);
    if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_append_pro.inc"
#else

        context->status = INKTAG_STATUS_UNSUPPORTED;
#endif
#line 1928 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
               s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
        context->status = inktag_gr_device_info_qr_write(
            context->offset, data, data_length);
    } else if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        context->status = v2_store_status(
            inktag_image_store_append(data, data_length));
    } else if (!s_epd) {
        context->status = INKTAG_STATUS_INVALID_STATE;
    } else {
        uint16_t consumed = 0u;
        while (consumed < data_length) {
            uint32_t absolute = context->offset + consumed;
            uint8_t segment = absolute < s_v2_transfer.segment_size[0] ? 0u : 1u;
            uint32_t segment_start = segment == 0u ?
                0u : s_v2_transfer.segment_size[0];
            uint32_t segment_end = segment_start +
                s_v2_transfer.segment_size[segment];
            uint16_t chunk = (uint16_t)(data_length - consumed);
            bool black = (s_v2_transfer.black_mask & (1u << segment)) != 0u;
            if (absolute + chunk > segment_end)
                chunk = (uint16_t)(segment_end - absolute);
            /* Decoded LZO blocks are wider than the native byte-writer ABI. */
            if (chunk > UINT8_MAX) chunk = UINT8_MAX;
            if (!inktag_brand_overlay_apply_oriented(
                    &s_screen_config, s_v2_transfer.format, segment,
                    absolute - segment_start, &data[consumed], chunk,
                    image_brand_edition(),
                    inktag_image_reading_rotation(s_v2_transfer.black_mask))) {
                context->status = INKTAG_STATUS_INTERNAL_ERROR;
                break;
            }
            if (!EPD_DriverWriteRam(
                    s_epd, absolute == segment_start, black,
                    &data[consumed], (uint8_t)chunk)) {
                context->status = INKTAG_STATUS_INVALID_STATE;
                break;
            }
            consumed = (uint16_t)(consumed + chunk);
        }
    }
    if (context->status != INKTAG_STATUS_OK) return false;
    context->offset += data_length;
    return true;
}

static void v2_transfer_write(uint8_t opcode, uint8_t transaction_id,
                              uint8_t *payload, uint16_t length) {
    uint8_t *data;
    uint16_t data_length;
    bool compressed;
#if INKTAG_TRANSFER_LZO1X_ENABLED
    bool lzo;
#endif
    v2_transfer_sink_context_t context;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0);
        return;
    }
    inktag_power_operation_t power_operation =
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ?
            INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY;
    if (!inktag_gr_health_operation_allowed(power_operation, false)) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER,
                         NULL, 0u);
        return;
    }
    if (length <= 4u || read_u32_le(payload) != s_v2_transfer.written) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    data = &payload[4];
    data_length = (uint16_t)(length - 4u);
    compressed = false;
#if INKTAG_TRANSFER_PACKBITS_ENABLED
    compressed = (s_v2_transfer.black_mask &
                  INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u;
#endif
#if INKTAG_TRANSFER_LZO1X_ENABLED
    lzo = (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
           s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) &&
          (s_v2_transfer.black_mask & INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X) != 0u;
    compressed = compressed || lzo;
#endif
    if (compressed)
        data_length = length > 6u ? read_u16_le(&payload[4]) : 0u;
    if (s_v2_transfer.written + data_length > s_v2_transfer.total_size) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    context.offset = s_v2_transfer.written;
    context.status = INKTAG_STATUS_OK;
#if INKTAG_TRANSFER_LZO1X_ENABLED
    if (lzo) {
        size_t produced;
        uint8_t *decoded = NULL;
        /* Transactions serialize display/storage. Borrow at most one bounded
         * block from the existing heap, never a new retained framebuffer.
         * Complete decoding precedes CRC, Flash and panel sink mutations. */
        if (length <= 6u || !data_length || data_length > 2000u)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if ((decoded = malloc(data_length)) == NULL)
            status = INKTAG_STATUS_NO_SPACE;
        else if (!inktag_lzo1x_decode(payload+6u, length-6u, decoded, data_length, &produced) ||
                 produced != data_length)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!v2_transfer_accept_decoded(&context, decoded, data_length))
            status = context.status;
        free(decoded);
    } else
#endif
#if INKTAG_TRANSFER_PACKBITS_ENABLED
    if (compressed) {
        if (length <= 6u || data_length == 0u ||
            !inktag_packbits_stream_block(
                &payload[6], (uint8_t)(length - 6u), data_length,
                v2_transfer_accept_decoded, &context)) {
            status = context.status != INKTAG_STATUS_OK ?
                context.status : INKTAG_STATUS_INVALID_REQUEST;
        }
    } else
#endif
    {
        if (!v2_transfer_accept_decoded(&context, data, data_length))
            status = context.status;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, status, NULL, 0);
        return;
    }
    s_v2_transfer.written += data_length;
    s_v2_transfer.last_activity_ms = s_status_now_ms;
    uint8_t response[4];
    put_u32_le(response, s_v2_transfer.written);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

static void v2_transfer_commit(uint8_t opcode, uint8_t transaction_id) {
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id ||
        s_v2_transfer.written != s_v2_transfer.total_size) {
        if (s_v2_transfer.active && transaction_id == s_v2_transfer.transaction_id &&
            s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            v2_transfer_reset(true); /* Failed commit consumes the W grant. */
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0);
        return;
    }
    uint32_t crc = s_v2_transfer.running_crc ^ 0xFFFFFFFFu;
    if (s_v2_transfer.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        crc != s_v2_transfer.expected_crc) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_CHECKSUM_ERROR, NULL, 0);
        return;
    }
    uint8_t object_type = s_v2_transfer.object_type;
    uint8_t response[1] = {UINT8_MAX};
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_password_protocol_commit_pro.inc"
#else

        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
#endif
#line 2107 "../Src/user/inktag_gr_protocol_v2.inc"
}
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_font_protocol_commit_pro.inc"
#else

        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
#endif
#line 2146 "../Src/user/inktag_gr_protocol_v2.inc"
}
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        inktag_status_v2_t status;
        if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                                 true)) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        response[0] = s_v2_transfer.black_mask;
        status = inktag_gr_device_info_qr_commit(&s_password_config);
        if (status != INKTAG_STATUS_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, status, NULL, 0u);
            return;
        }
        v2_transfer_reset(false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, response, sizeof(response));
        return;
    }
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        inktag_status_v2_t access = inktag_access_image_write(&s_activation,
            s_v2_transfer.image_saved_count, s_v2_transfer.replacing_existing,
            s_v2_transfer.image_existing_ordinal, true,
            s_screen_config.visible_width, s_screen_config.visible_height);
        if (access != INKTAG_STATUS_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, access, NULL, 0);
            return;
        }
        uint8_t id = s_v2_transfer.resource_id;
        inktag_image_store_result_t result = inktag_gr_image_store_wait(
            inktag_image_store_commit(), INKTAG_IMAGE_STORE_EVENT_COMMIT, NULL);
        if (result != INKTAG_IMAGE_STORE_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0);
            return;
        }
        response[0] = id;
        s_pending_picture_store_id = id;
        v2_transfer_reset(false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, response, sizeof(response));
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_picture_io_active = false;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             false, s_status_now_ms);
    if (!v2_schedule_work(opcode, transaction_id, EPD_WORK_REFRESH, 0, 0)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

static void v2_set_display_mode(uint8_t opcode, uint8_t transaction_id,
                                const uint8_t *payload, uint16_t length) {
    if (length < 1u || s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) || s_v2_transfer.active) {
        send_v2_response(opcode, transaction_id,
                         length < 1u ? INKTAG_STATUS_INVALID_REQUEST : INKTAG_STATUS_BUSY,
                         NULL, 0);
        return;
    }
    uint8_t mode = payload[0];
    bool scheduled = false;
    inktag_status_v2_t failure = INKTAG_STATUS_NOT_FOUND;
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO) {
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                     MODE_DEVICE_INFO, s_timestamp);
        if (scheduled) slideshow_stop(false);
    } else if (mode == INKTAG_DISPLAY_MODE_CALENDAR ||
               mode == INKTAG_DISPLAY_MODE_CLOCK) {
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                     mode == INKTAG_DISPLAY_MODE_CALENDAR ?
                                     MODE_CALENDAR : MODE_CLOCK, s_timestamp);
        if (scheduled) slideshow_stop(false);
    } else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_CLEAR,
                                     length > 1u ? (payload[1] != 0u) : 1u, 0);
        if (scheduled) slideshow_stop(false);
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        scheduled = length == 2u && stored_image_replayable(payload[1], false) &&
                    v2_schedule_work(opcode, transaction_id, EPD_WORK_IMAGE_SHOW,
                                     payload[1], 0);
        if (scheduled && s_slideshow_active) {
            s_slideshow_next_at = 0;
            s_slideshow_advance_pending = false;
        }
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint32_t interval = length == 5u ? read_u32_le(&payload[1]) : 3600u;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        uint8_t first_id = UINT8_MAX;
        if (image_store_model() != NULL &&
            image_store_next_visible_compatible_no_wrap(UINT8_MAX, true, &first_id) &&
            v2_schedule_work(opcode, transaction_id, EPD_WORK_IMAGE_SHOW,
                              first_id, 0)) {
            s_slideshow_interval = interval;
            s_slideshow_active = true;
            s_slideshow_next_at = 0u;
            s_slideshow_advance_pending = false;
            scheduled = true;
        }
    } else if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        failure = INKTAG_STATUS_INVALID_STATE;
    } else {
        failure = INKTAG_STATUS_UNSUPPORTED;
    }
    send_v2_response(opcode, transaction_id,
                     scheduled ? INKTAG_STATUS_OK : failure, NULL, 0);
}

static void v2_display_action(uint8_t opcode, uint8_t transaction_id,
                              uint8_t flags, const uint8_t *payload,
                              uint16_t length) {
    if (length > 0u && payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN) {
        if (length != 2u || payload[1] < 1u || payload[1] > 10u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        if ((flags & INKTAG_REQUEST_FLAG_CONFIRM) == 0u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
        {
            bool ok = v2_schedule_work(opcode, transaction_id, EPD_WORK_GHOST_CLEAN,
                                       payload[1], 0u);
            send_v2_response(opcode, transaction_id,
                             ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY,
                             NULL, 0u);
        }
        return;
    }
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT && s_slideshow_active) {
        uint8_t id = UINT8_MAX;
        bool ok = slideshow_select_next(&id) &&
                  v2_schedule_work(opcode, transaction_id, EPD_WORK_IMAGE_SHOW, id, 0);
        if (ok) {
            s_slideshow_next_at = 0u;
            s_slideshow_advance_pending = false;
        }
        send_v2_response(opcode, transaction_id,
                         ok ? INKTAG_STATUS_OK : INKTAG_STATUS_NOT_FOUND, NULL, 0);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && s_slideshow_active) {
        s_slideshow_active = false;
        s_slideshow_next_at = 0;
        s_slideshow_advance_pending = false;
        /* PAUSE also owns a due frame queued behind discovery/IO. Do not let
         * that old automatic frame execute after the client disconnects. */
        if (s_epd_work_pending && s_epd_work_background &&
            s_epd_work_type == EPD_WORK_IMAGE_SHOW)
            discard_pending_background_epd_work();
        if (s_slideshow_current_id != UINT8_MAX) {
            record_display_state(EPD_DISPLAY_STATE_PICTURE, s_slideshow_current_id, 0);
        }
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_RESUME &&
               s_slideshow_current_id != UINT8_MAX) {
        s_slideshow_active = true;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
        record_display_state(EPD_DISPLAY_STATE_SLIDESHOW,
                             s_slideshow_current_id, s_slideshow_interval);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        bool ok = v2_schedule_work(opcode, transaction_id, EPD_WORK_REFRESH, 0, 0);
        send_v2_response(opcode, transaction_id,
                         ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY, NULL, 0);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        if (s_epd) s_epd->drv->sleep(s_epd);
        epd_gpio_close_if_needed();
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0);
    }
}

static void v2_cancel(uint8_t opcode, uint8_t transaction_id,
                       const uint8_t *payload, uint16_t length) {
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    if (length == 0u && gr_panel_diag_busy()) {
        inktag_status_v2_t status = gr_panel_diag_close();
        if (status != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, status, NULL, 0u);
            return;
        }
    }
#endif
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_cancel_pro.inc"
#endif
#line 2351 "../Src/user/inktag_gr_protocol_v2.inc"

#endif
    if (length != 0u) {
#if defined(INKTAG_GOODIX_GR5515)
        v2_input_simulation_cancel(transaction_id, payload, length);
#else
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
#endif
        return;
    }
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_protocol_bulk_1_pro.inc"
#line 2336 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
    if (s_v2_transfer.active) v2_transfer_reset(true);
    slideshow_stop(false);
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_native_refresh_pending = false;
    s_slideshow_advance_pending = false;
    s_pending_picture_store_id = UINT8_MAX;
    if (s_diag_scan_active) diag_scan_finish(NULL);
    if (s_epd_work_pending) {
        uint8_t argument = s_epd_work_arg;
        s_epd_work_pending = false;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        s_epd_work_background = false;
        send_v2_work_complete(INKTAG_STATUS_CANCELLED, argument);
    }
    if (s_epd_work_active) {
        s_epd_cancel_requested = true;
        s_v2_cancel_transaction = transaction_id;
    } else {
        s_v2_work_opcode = 0u;
        s_v2_work_transaction = 0u;
        s_v2_work_type = EPD_WORK_NONE;
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
    }
}

static void v2_clock_rate_settings(uint8_t opcode, uint8_t transaction_id,
                                    const uint8_t *payload, uint16_t length,
                                    bool set) {
    uint8_t response[INKTAG_CLOCK_RATE_REPORT_BYTES];
    int16_t ppm;
    if (set) {
        if (!inktag_clock_rate_decode(payload, length, &ppm)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        /* Settle at the previous coefficient before changing correction
         * phase. Neither the timestamp nor its millisecond carry is reset. */
        s_clock_tick_due = true;
        clock_process();
        (void)inktag_clock_rate_set(&s_clock_rate, ppm);
    }
    inktag_clock_rate_encode(s_clock_rate.ppm, clock_rate_default_ppm(), response);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_get_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        v2_send_identity_settings(opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        v2_clock_rate_settings(opcode, transaction_id, payload, length, false);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        inktag_display_settings_t response_settings = s_display_settings;
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        response_settings.flags &= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
        if (!inktag_access_brand_should_render(&s_activation,
                                               brand_hidden_requested())) {
            response_settings.flags |=
                INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
        }
        response_length = inktag_display_settings_encode(
            &response_settings, response, sizeof(response));
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)response_length);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
    }
}

static void v2_set_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    bool display_request = length >= 1u &&
        payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY;
    if (length < 2u || (!display_request && payload[1] != 1u)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    if (payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        v2_clock_rate_settings(opcode, transaction_id, payload, length, true);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_name_settings_pro.inc"
#else

        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        if (length != (uint16_t)(4u + name_length + label_length) ||
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        if (name_length != 0u) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
        epd_device_settings_t next = s_device_settings; /* Preserve opaque old BLE override. */
        next.label_length = label_length;
        memset(next.label, 0, sizeof(next.label));
        memcpy(next.label, &payload[4u + name_length], label_length);
        if (!epd_device_settings_write(&next)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        s_device_settings = next;
        bool applied = apply_runtime_device_name(); /* base inert ABI, no GAP mutation. */
        uint8_t response[1] = {applied ? 0u : 1u};
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK, opcode, transaction_id, NULL, 0u);
#endif
#line 2467 "../Src/user/inktag_gr_protocol_v2.inc"
} else if (display_request) {
        inktag_display_settings_t next;
        inktag_display_settings_t response_settings;
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        inktag_status_v2_t access;
        bool hidden;
        if (!inktag_display_settings_decode(payload, length, &next) ||
            (next.name_length != 0u &&
             !GUI_CountdownNameSupported(next.name, next.name_length))) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
#if INKTAG_BUILD_PRO
#include "inktag_gr_brand_hidden_settings_pro.inc"
#else
        hidden = (next.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN) != 0u;
        access = hidden ? INKTAG_STATUS_DENIED : INKTAG_STATUS_OK;
        if (!hidden) next.flags |= s_display_settings.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
#endif
#line 2485 "../Src/user/inktag_gr_protocol_v2.inc"
        if (access != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, access, NULL, 0u);
            return;
        }
        if (!display_settings_write(&next)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        s_display_settings = next;
        display_settings_apply_runtime(&s_display_settings);
        if (!display_state_flag(EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH)) {
            s_boot_restore_pending = false;
            s_boot_restore_due = false;
        }
        response_settings = s_display_settings;
        response_settings.flags &= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
        if (!inktag_access_brand_should_render(&s_activation,
                                               brand_hidden_requested())) {
            response_settings.flags |=
                INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
        }
        response_length = inktag_display_settings_encode(
            &response_settings, response, sizeof(response));
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)response_length);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
    }
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_find_config_protocol_pro.inc"
#else
static void v2_send_find_capabilities(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_send_find_config(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_set_find_config(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
#endif
#line 2629 "../Src/user/inktag_gr_protocol_v2.inc"
static void v2_indicator_config(uint8_t opcode, uint8_t transaction_id,
                                bool write, const uint8_t *payload,
                                uint16_t length) {
    uint8_t response[INKTAG_STATUS_WIRE_SIZE];
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!runtime_rgb_led_available() ||
        status_led_available_colors() == INKTAG_STATUS_COLOR_OFF) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED,
                         NULL, 0u);
        return;
    }
    if (write) {
        status = status_led_set_configuration(payload, length);
        if (status != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, status, NULL, 0u);
            return;
        }
    } else if (length != 0u) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    status_led_enforce_entitlement();
    if (!inktag_status_config_encode(&s_status.config, response)) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INTERNAL_ERROR, NULL, 0u);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
    if (write) {
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, NULL, 0u);
    }
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_find_owner_protocol_pro.inc"
#else
static void v2_find_action(uint8_t opcode, uint8_t transaction_id, uint8_t flags, const uint8_t *payload, uint16_t length) {
    (void)flags;
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_send_find_owner_status(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_begin_find_owner_rotation(uint8_t opcode, uint8_t transaction_id, uint8_t flags, const uint8_t *payload, uint16_t length) {
    (void)flags;
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_stage_find_owner_rotation(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_commit_find_owner_rotation(uint8_t opcode, uint8_t transaction_id, uint8_t flags, const uint8_t *payload, uint16_t length) {
    (void)flags;
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
static void v2_abort_find_owner_rotation(uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length) {
    (void)payload;
    (void)length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}
#endif
#line 3007 "../Src/user/inktag_gr_protocol_v2.inc"
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
#include "inktag_gr_advertising_protocol_pro.inc"
#endif

static bool v2_apply_settings_action(inktag_settings_clear_action_t action) {
    /* The image store owns ALL content clearing. QR/NFC and ordinary settings
     * are no longer members of that action. Only settings reset reaches here. */
    if (action != INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) return false;
    for (uint8_t raw_slot = 0u;
         raw_slot < (uint8_t)INKTAG_GR_SETTINGS_SLOT_COUNT; raw_slot++) {
        inktag_gr_settings_slot_t slot = (inktag_gr_settings_slot_t)raw_slot;
        inktag_gr_settings_mapping_t mapping;
        if (!inktag_gr_settings_mapping(slot, &mapping)) return false;
        if ((INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK &
             INKTAG_SETTINGS_RECORD_BIT(mapping.primary_record)) == 0u) continue;

        if (slot == INKTAG_GR_SETTINGS_SLOT_NFC &&
            mapping.protected_member == INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY) {
#if INKTAG_BUILD_PRO
            if (!inktag_gr_nfc_reset_preferences()) return false;
#endif
            continue; /* ID13 only: preserve composite ID14 anti-replay state. */
        }
        if (mapping.protected_member != 0u) return false;
        /* ID5 is committed as explicit OFF only AFTER all these deletes pass. */
        if (slot == INKTAG_GR_SETTINGS_SLOT_DEVICE_INFO_QR) continue;
        /* ID30 belongs to the image store's publish-before-reclaim lifecycle.
         * Deleting it here could resurrect a partially cleared v3 store. */
        if (slot == INKTAG_GR_SETTINGS_SLOT_IMAGE_INDEX) continue;
        if (!inktag_gr_settings_delete(slot) ||
            !inktag_gr_settings_absent(slot)) return false;
    }
    return true;
}

static void v2_reset_preferences_runtime(void) {
    memset(&s_config, 0xFF, sizeof(s_config));
    epd_device_settings_default(&s_device_settings);
    s_epd = NULL;
    epd_gpio_close_if_needed();
}

static inktag_status_v2_t v2_clear_user_data(uint16_t *removed) {
    uint16_t count = 0u;
    slideshow_stop(false);
    s_pending_picture_store_id = UINT8_MAX;
    inktag_image_store_result_t result =
        inktag_gr_image_store_wait(inktag_image_store_clear_all(),
            INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, &count);
    /* Unfitted storage/zero-capacity geometry has no current image slots.
     * A fitted Flash read failure is not an empty, successfully cleared store. */
    if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_UNSUPPORTED &&
        result != INKTAG_IMAGE_STORE_NO_SPACE) return v2_store_status(result);
    /* Current fixed-v5 slots have no persistent index. Remove the old ID30
     * record only on this explicit images-only action, along with ID4 state. */
    if (!inktag_gr_settings_delete(INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE) ||
        !inktag_gr_settings_absent(INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE) ||
        !inktag_gr_settings_delete(INKTAG_GR_SETTINGS_SLOT_IMAGE_INDEX) ||
        !inktag_gr_settings_absent(INKTAG_GR_SETTINGS_SLOT_IMAGE_INDEX))
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    s_display_state_loaded = false;
#if defined(INKTAG_GOODIX_GR5515)
    s_input_slideshow_return_valid = false;
    s_clear_return_valid = false;
#endif
    if (removed) *removed = count;
    return INKTAG_STATUS_OK;
}

static void v2_schedule_system_action(uint8_t action) {
    s_system_action_pending = action;
    s_system_action_not_before_ms = s_status_now_ms + 250u;
}

static void v2_system_action(uint8_t opcode, uint8_t transaction_id,
                             uint8_t flags, const uint8_t *payload, uint16_t length) {
    if (!inktag_device_password_command_allowed(&s_password, &s_password_config,
                                                 INKTAG_CMD_SYSTEM_ACTION)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
    const inktag_lifecycle_capabilities_t lifecycle = {
        .reset = true,
        .timed_sleep = false, /* Retired offline action; ordinary idle is unchanged. */
        .factory_reset = false, /* Retired product action; wire ID stays reserved. */
#if defined(INKTAG_GOODIX_OTA)
        .ota_recovery = true,
#endif
        .reset_settings = INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        .clear_user_data = INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &lifecycle, &plan);
    if (status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    if (s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) ||
        s_diag_scan_active || s_v2_transfer.active || s_picture_io_active ||
        s_system_action_pending != 0u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0);
        return;
    }
    if ((plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS ||
         plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) &&
        !inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS &&
        !inktag_device_password_consume_settings_reset(
            &s_password, &s_password_config, s_status_now_ms)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
        return; /* No tag delete, background cancellation or runtime mutation. */
    }
    if (s_epd_work_pending && s_epd_work_background) {
        s_epd_work_pending = false;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        s_epd_work_background = false;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET) {
        (void)clock_save_time();
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
        v2_schedule_system_action(plan.action);
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        uint16_t removed = 0;
        status = v2_clear_user_data(&removed);
        uint8_t response[2];
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK) {
            send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                          opcode, transaction_id, NULL, 0u);
        }
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) {
        uint16_t removed = 0u;
        if (!v2_apply_settings_action(INKTAG_SETTINGS_CLEAR_RESET_SETTINGS)) {
            status = INKTAG_STATUS_PERSISTENCE_ERROR;
        } else {
            status = inktag_gr_password_reset_settings(&s_password_config);
            inktag_device_password_session_clear(&s_password);
        }
        uint8_t response[2];
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK) {
            v2_reset_preferences_runtime();
            v2_schedule_system_action(plan.action);
        }
#if defined(INKTAG_GOODIX_OTA)
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY) {
        if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, true)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        } else {
            goodix_ota_enter();
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
        }
#endif
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED,
                         NULL, 0u);
    }
}

static void v2_delete_resource(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    inktag_image_store_header_t header;
    inktag_image_store_result_t lookup =
        inktag_image_store_get(payload[0], &header);
    if (lookup != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(lookup), NULL, 0);
        return;
    }
    if (!image_store_record_visible(payload[0])) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0);
        return;
    }
    slideshow_stop(false);
    inktag_image_store_result_t result = inktag_gr_image_store_wait(
        inktag_image_store_delete(payload[0]), INKTAG_IMAGE_STORE_EVENT_DELETE, NULL);
    send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0);
    if (result == INKTAG_IMAGE_STORE_OK) {
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, payload, 1u);
    }
}

static void v2_clear_resources(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    slideshow_stop(false);
    uint16_t removed = 0;
    inktag_image_store_result_t result =
        payload[0] == INKTAG_RESOURCE_CLEAR_INCOMPATIBLE ?
        image_store_clear_visible(true, &removed) :
        payload[0] == INKTAG_RESOURCE_CLEAR_ALL ?
        image_store_clear_visible(false, &removed) :
        INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    uint8_t response[2];
    put_u16_le(response, removed);
    send_v2_response(opcode, transaction_id, v2_store_status(result),
                     result == INKTAG_IMAGE_STORE_OK ? response : NULL,
                     result == INKTAG_IMAGE_STORE_OK ? sizeof(response) : 0u);
}

/* One common E6 decision at RX and dispatch. Do not gate readback, storage-
 * only writes or mandatory sleep/cancel/transfer-abort cleanup. */
static inktag_status_v2_t v2_panel_request_access(
    uint8_t opcode, const uint8_t *payload, uint16_t length) {
    bool drives = false;
    if (opcode == INKTAG_CMD_SET_SCREEN_PROFILE) {
        epd_screen_config_t requested;
        if (epd_screen_config_decode_exact(payload, length, &requested) == EPD_SCREEN_STATUS_OK)
            return screen_control_access(&requested,
                inktag_gr_board_identity() != INKTAG_GR_BOARD_UNKNOWN);
    } else if (opcode == INKTAG_CMD_SET_DISPLAY_MODE)
        drives = length != 0u && payload[0] <= INKTAG_DISPLAY_MODE_SLIDESHOW &&
                 payload[0] != INKTAG_DISPLAY_MODE_DIRECT_IMAGE;
    else if (opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO)
        drives = v2_password_available();
    else if (opcode == INKTAG_CMD_DISPLAY_ACTION && length != 0u)
        drives = payload[0] == INKTAG_DISPLAY_ACTION_REFRESH ||
                 payload[0] == INKTAG_DISPLAY_ACTION_NEXT ||
                 payload[0] == INKTAG_DISPLAY_ACTION_RESUME ||
                 payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN;
    else if (opcode == INKTAG_CMD_TRANSFER_BEGIN && length != 0u)
        drives = payload[0] == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
    else if (opcode == INKTAG_CMD_TRANSFER_WRITE || opcode == INKTAG_CMD_TRANSFER_COMMIT)
        drives = s_v2_transfer.active &&
                 s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
    else if (opcode == INKTAG_CMD_RUN_TEST && length != 0u)
        drives = payload[0] == INKTAG_TEST_TIME_SHIFT || payload[0] == INKTAG_TEST_TIME_SET;
    return drives ? epd_control_access() : INKTAG_STATUS_OK;
}

#if !INKTAG_BUILD_PRO
static bool v2_base_request_unsupported(uint8_t opcode,
    const uint8_t *payload, uint16_t length) {
    return (opcode >= INKTAG_CMD_GET_NFC_CAPABILITIES && opcode <= INKTAG_CMD_SET_NFC_NDEF) ||
        (opcode >= INKTAG_CMD_GET_LAYOUT_CAPABILITIES && opcode <= INKTAG_CMD_GET_LAYOUT_STATUS) ||
        (opcode >= INKTAG_CMD_SET_ACTIVATION_CODE && opcode <= INKTAG_CMD_AUTHORIZE_OWNER_ACTION) ||
        opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO ||
        (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE && length != 0u &&
            payload[0] != INKTAG_PASSWORD_AUTH) ||
        opcode == INKTAG_CMD_GET_INPUT_CONFIG || opcode == INKTAG_CMD_SET_INPUT_CONFIG ||
        (opcode == INKTAG_CMD_SET_IO_CONFIG && length != 0u && payload[0] != 2u) ||
        (opcode >= INKTAG_CMD_GET_FIND_CAPABILITIES && opcode <= INKTAG_CMD_ABORT_FIND_OWNER_ROTATION) ||
#if !INKTAG_PANEL_DIAGNOSTIC_ENABLED
        (opcode >= INKTAG_CMD_OPEN_DEBUG_SESSION && opcode <= INKTAG_CMD_ABORT_DEBUG_OPERATION) ||
#endif
        (length != 0u &&
            ((opcode == INKTAG_CMD_RUN_TEST && payload[0] == INKTAG_TEST_NFC_CHANNEL_SNAPSHOT) ||
             (opcode == INKTAG_CMD_RUN_TEST &&
                (payload[0] == INKTAG_TEST_RESERVED_03 || payload[0] == INKTAG_TEST_INPUT_SIMULATE)) ||
             (opcode == INKTAG_CMD_TRANSFER_BEGIN &&
                (payload[0] == INKTAG_TRANSFER_OBJECT_NFC_DATA || payload[0] == INKTAG_TRANSFER_OBJECT_NATIVE_LAYOUT))));
}
#endif

static inktag_status_v2_t v2_image_transfer_request_status(uint8_t opcode,
    const uint8_t *payload, uint16_t length) {
    if (opcode == INKTAG_CMD_RUN_TEST && length != 0u &&
        payload[0] == INKTAG_TEST_RESERVED_03) return INKTAG_STATUS_UNSUPPORTED;
    if (opcode == INKTAG_CMD_TRANSFER_BEGIN && length != 0u &&
        (payload[0] == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
         payload[0] == INKTAG_TRANSFER_OBJECT_STORED_IMAGE)) {
        inktag_transfer_descriptor_t descriptor;
        return inktag_transfer_descriptor_decode(payload, length, &descriptor);
    }
    return INKTAG_STATUS_OK;
}

static void process_packet(uint8_t *p_data, uint16_t length) {
    if (p_data == NULL || length < INKTAG_PROTOCOL_V2_HEADER_SIZE) return;
    uint8_t opcode = p_data[0];
    uint8_t transaction_id = p_data[1];
    /* Retired metadata editing never reaches AUTH, idle or persistence paths.
     * Keep both allocated IDs unavailable, including malformed requests. */
    if (opcode == INKTAG_CMD_SET_RESOURCE_ALIAS || opcode == 0x1Eu) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    /* RX queues retain the versioned transport bytes. Decode again here:
     * byte2 includes wire identity and is not a semantic flags value. */
    inktag_protocol_request_t request;
    if (inktag_protocol_parse_request(p_data, length, &request) != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
        return;
    }
#if INKTAG_GR_NFC_STREAM_PILOT
#include "inktag_gr_nfc_protocol_bulk_2_pro.inc"
#line 3282 "../Src/user/inktag_gr_protocol_v2.inc"
#endif
    uint8_t flags = request.flags;
    const uint8_t *payload = request.payload;
    uint16_t payload_length = request.payload_length;
    if (!inktag_device_password_command_allowed(&s_password, &s_password_config, opcode)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
#if !INKTAG_BUILD_PRO
    if (v2_base_request_unsupported(opcode, payload, payload_length)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
    inktag_status_v2_t transfer_status = v2_image_transfer_request_status(opcode, payload, payload_length);
    if (transfer_status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, transfer_status, NULL, 0u);
        return;
    }
#if defined(INKTAG_GR_PIN_CONFIG_PORT)
    if (inktag_gr_pin_routes_required() && !inktag_gr_pin_routes_available() &&
        v2_command_requires_idle_display(opcode) && opcode != INKTAG_CMD_SYSTEM_ACTION) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        return;
    }
#endif
    {
        inktag_status_v2_t access = v2_panel_request_access(opcode, payload, payload_length);
        if (access != INKTAG_STATUS_OK) {
            if (s_v2_transfer.active &&
                s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
                (opcode == INKTAG_CMD_TRANSFER_WRITE || opcode == INKTAG_CMD_TRANSFER_COMMIT))
                v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, access, NULL, 0u);
            return;
        }
    }
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
    if (gr_panel_diag_busy() && opcode != INKTAG_CMD_CANCEL &&
        (v2_command_requires_idle_display(opcode) ||
         inktag_protocol_command_mutates_persistent_state(opcode, payload, payload_length))) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
#endif
#if defined(INKTAG_GOODIX_GR5515)
#if INKTAG_BUILD_PRO
#include "inktag_gr_simulation_dispatch_busy_pro.inc"
#endif
#line 3332 "../Src/user/inktag_gr_protocol_v2.inc"

#endif
    if ((
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
         opcode == INKTAG_CMD_SET_ADVERTISING_CONFIG ||
#endif
         inktag_protocol_command_mutates_persistent_state(
            opcode, payload, payload_length)) &&
        !inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }

    switch (opcode) {
        /* Ordinary retained-password AUTH is shared by both profiles. */
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
#if INKTAG_BUILD_PRO
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
#endif
            v2_password_request(opcode, transaction_id, flags, payload, payload_length);
            break;
        case INKTAG_CMD_HELLO:
            v2_send_hello(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_CAPABILITIES:
            v2_send_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_DEVICE_INFO:
            v2_send_device_info(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_STATUS:
            v2_send_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_HEALTH:
            v2_send_health(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TIME:
            v2_send_time(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_TIME:
            if (payload_length != 7u || payload[6] > 6u) {
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            } else {
                uint32_t utc_timestamp = read_u32_le(payload);
                int16_t next_timezone = (int16_t)read_u16_le(&payload[4]);
                int64_t local_timestamp = (int64_t)utc_timestamp +
                    (int32_t)next_timezone * 60;
                if (next_timezone < -840 || next_timezone > 840 ||
                    local_timestamp < 946684800ll ||
                    local_timestamp > 4102444799ll) {
                    send_v2_response(opcode, transaction_id,
                                     INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
                    break;
                }
                s_clock_tick_due = true;
                clock_process();
                uint32_t previous_timestamp = s_timestamp;
                uint8_t previous_week_start = s_config.week_start;
                bool previous_time_valid = s_time_valid;
                if (!clock_save_time_candidate((uint32_t)local_timestamp,
                                                next_timezone, true)) {
                    send_v2_response(opcode, transaction_id,
                                     INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0);
                } else {
                    set_timestamp((uint32_t)local_timestamp +
                                  (s_timestamp - previous_timestamp));
                    s_timezone_minutes = next_timezone;
                    s_config.week_start = payload[6];
                    s_time_valid = true;
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
                    s_findmy_identity_refresh_pending =
                        s_findmy_config.enabled && s_findmy_identity_available;
#endif
                    epd_config_write(&s_config);
                    native_time_sync_applied(previous_timestamp,
                        previous_week_start, previous_time_valid);
                    v2_send_time(opcode, transaction_id);
                }
            }
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE:
            v2_send_screen(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            epd_screen_config_t requested;
            epd_screen_status_t screen_status = epd_screen_config_decode_exact(
                payload, payload_length, &requested);
            if (screen_status != EPD_SCREEN_STATUS_OK || epd_find(&requested) == NULL) {
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE, NULL, 0);
            } else {
                bool reply;
                inktag_status_v2_t result = v2_screen_commit(&requested, &reply);
                if (!reply) break;
                if (result == INKTAG_STATUS_OK) {
                    v2_send_screen(opcode, transaction_id);
                    send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                                  opcode, transaction_id, NULL, 0);
                } else send_v2_response(opcode, transaction_id, result, NULL, 0);
            }
            break;
        }
        case INKTAG_CMD_SET_DISPLAY_MODE:
            v2_set_display_mode(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_DISPLAY_ACTION:
            v2_display_action(opcode, transaction_id, flags,
                              payload, payload_length);
            break;
        case INKTAG_CMD_CANCEL:
            v2_cancel(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_SETTINGS:
            v2_get_settings(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_SET_SETTINGS:
            v2_set_settings(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_SYSTEM_ACTION:
            v2_system_action(opcode, transaction_id, flags, payload, payload_length);
            break;
        case INKTAG_CMD_TRANSFER_BEGIN:
            v2_transfer_begin(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_TRANSFER_WRITE:
            /* The generic parser exposes a const view; this dispatcher owns
             * its mutable RX buffer, used in-place by the direct-frame brand. */
            v2_transfer_write(opcode, transaction_id,
                              (uint8_t *)payload, payload_length);
            break;
        case INKTAG_CMD_TRANSFER_COMMIT:
            if (payload_length != 0u) {
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0);
            } else {
                v2_transfer_commit(opcode, transaction_id);
            }
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!s_v2_transfer.active) {
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_STATE, NULL, 0);
            } else {
                v2_transfer_reset(true);
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0);
            }
            break;
        case INKTAG_CMD_GET_TRANSFER_STATUS: {
            uint8_t response[11] = {0};
            response[0] = s_v2_transfer.active ? 1u : 0u;
            response[1] = s_v2_transfer.object_type;
            response[2] = s_v2_transfer.transaction_id;
            put_u32_le(&response[3], s_v2_transfer.written);
            put_u32_le(&response[7], s_v2_transfer.total_size);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
            break;
        }
        case INKTAG_CMD_GET_GUI_ASSET_INFO:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else
                v2_send_gui_asset_info(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_RESOURCE_INFO: {
            v2_send_resource_info(opcode, transaction_id);
            break;
        }
        case INKTAG_CMD_LIST_RESOURCES: {
            v2_list_resource(opcode, transaction_id, payload, payload_length);
            break;
        }
        case INKTAG_CMD_READ_RESOURCE:
            v2_read_resource(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_DELETE_RESOURCE:
            v2_delete_resource(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_CLEAR_RESOURCES:
            v2_clear_resources(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_INPUT_CONFIG:
#if defined(INKTAG_GOODIX_GR5515)
            v2_get_input_config(opcode, transaction_id);
#else
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
#endif
            break;
        case INKTAG_CMD_SET_INPUT_CONFIG:
#if defined(INKTAG_GOODIX_GR5515)
            v2_set_input_config(opcode, transaction_id, payload, payload_length);
#else
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
#endif
            break;
        case INKTAG_CMD_GET_IO_CONFIG:
            v2_get_io_config(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_SET_IO_CONFIG:
            v2_set_io_config(opcode, transaction_id, payload, payload_length);
            break;
#if INKTAG_GR_DELAYED_ADVERTISING_ENABLED
        case INKTAG_CMD_GET_ADVERTISING_CONFIG:
        case INKTAG_CMD_SET_ADVERTISING_CONFIG:
            v2_advertising_config(opcode, transaction_id,
                opcode == INKTAG_CMD_SET_ADVERTISING_CONFIG, payload, payload_length);
            break;
#endif
        case INKTAG_CMD_GET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, false,
                                payload, payload_length);
            break;
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, true,
                                payload, payload_length);
            break;
#if INKTAG_BUILD_PRO
#if defined(INKTAG_GOODIX_GR5515)
        case INKTAG_CMD_GET_NFC_CAPABILITIES:
            v2_send_nfc_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_STATUS:
            v2_send_nfc_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_NDEF:
            v2_get_nfc_ndef(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_NFC_NDEF:
            v2_set_nfc_ndef(opcode, transaction_id, payload, payload_length);
            break;
#else
        case INKTAG_CMD_GET_NFC_CAPABILITIES:
            v2_send_nfc_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_STATUS:
            v2_send_nfc_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_NDEF:
            v2_get_nfc_ndef(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_NFC_NDEF:
            v2_set_nfc_ndef(opcode, transaction_id, payload, payload_length);
            break;
#endif
        case INKTAG_CMD_GET_LAYOUT_CAPABILITIES:
        case INKTAG_CMD_VALIDATE_LAYOUT:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_GET_LAYOUT_STATUS:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
            break;
#endif
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
            v2_send_activation_status(opcode, transaction_id);
            break;
#if INKTAG_BUILD_PRO
        case INKTAG_CMD_SET_ACTIVATION_CODE:
            v2_set_activation_code(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_ACTIVATION_REQUEST:
            if (payload_length != 0u) {
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            } else {
                v2_send_activation_request(
                    opcode, transaction_id,
                    (flags & INKTAG_REQUEST_FLAG_FORCE) != 0u);
            }
            break;
        case INKTAG_CMD_GET_FIND_CAPABILITIES:
            v2_send_find_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_FIND_CONFIG:
            v2_send_find_config(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_FIND_CONFIG:
            v2_set_find_config(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_FIND_ACTION:
            v2_find_action(opcode, transaction_id, flags, payload,
                           payload_length);
            break;
        case INKTAG_CMD_GET_FIND_OWNER_STATUS:
            v2_send_find_owner_status(opcode, transaction_id,
                                      payload, payload_length);
            break;
        case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
            v2_begin_find_owner_rotation(opcode, transaction_id, flags,
                                         payload, payload_length);
            break;
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION:
            v2_stage_find_owner_rotation(opcode, transaction_id,
                                         payload, payload_length);
            break;
        case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
            v2_commit_find_owner_rotation(opcode, transaction_id, flags,
                                          payload, payload_length);
            break;
        case INKTAG_CMD_ABORT_FIND_OWNER_ROTATION:
            v2_abort_find_owner_rotation(opcode, transaction_id,
                                         payload, payload_length);
            break;
        case INKTAG_CMD_GET_SECURITY_INFO:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0);
            break;
#endif
        case INKTAG_CMD_LIST_TESTS:
            v2_list_tests(opcode, transaction_id);
            break;
        case INKTAG_CMD_RUN_TEST:
            v2_run_test(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TEST_STATUS:
            v2_send_test_status(opcode, transaction_id);
            break;
#if INKTAG_BUILD_PRO || INKTAG_PANEL_DIAGNOSTIC_ENABLED
        case INKTAG_CMD_OPEN_DEBUG_SESSION:
        case INKTAG_CMD_DEBUG_READ:
        case INKTAG_CMD_CLOSE_DEBUG_SESSION:
        case INKTAG_CMD_ABORT_DEBUG_OPERATION:
        case INKTAG_CMD_DEBUG_EXECUTE:
#if INKTAG_PANEL_DIAGNOSTIC_ENABLED
        {
            uint8_t response[22u + INKTAG_PANEL_DIAG_MAX_DATA];
            uint16_t written = 0u;
            inktag_status_v2_t status = flags != 0u ? INKTAG_STATUS_INVALID_REQUEST :
                gr_panel_diag_dispatch(opcode, payload, payload_length, response, &written);
            status_timer_update();
            send_v2_response(opcode, transaction_id, status,
                status == INKTAG_STATUS_OK ? response : NULL,
                status == INKTAG_STATUS_OK ? written : 0u);
            break;
        }
#else
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
#endif
#endif
        default:
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNKNOWN_COMMAND, NULL, 0);
            break;
    }
}
