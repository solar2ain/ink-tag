/* Protocol V2 adapter for the shared TLSR8359 application.  This file is
 * included by inktag_app.c so it can reuse the port's private display,
 * slideshow, status and persistence state without creating a second product
 * model.  Legacy command bytes are deliberately not dispatched. */

/* Runtime state only: never a transfer descriptor or a persisted memcpy. */
typedef struct __attribute__((aligned(4))) {
    uint32_t total_size;
    uint32_t written;
    uint32_t expected_crc;
    uint32_t running_crc;
    uint32_t segment_size[2];
    bool active;
    uint8_t object_type;
    uint8_t transaction_id;
    uint8_t format;
    uint8_t segment_count;
    uint8_t black_mask;
    uint8_t resource_id;
    uint8_t replace_resource_id;
    uint8_t saved_count;
    uint8_t replace_ordinal;
} tlsr_v2_transfer_t;

static RAM tlsr_v2_transfer_t s_v2_transfer;

#if INKTAG_BUILD_PRO
#include "inktag_find_owner_state_pro.inc"
#endif

static void v2_secret_clear(void *buffer, uint16_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    while (length-- != 0u) *bytes++ = 0u;
}

#if INKTAG_BUILD_PRO
#include "inktag_find_owner_service_pro.inc"
#else
static void v2_find_owner_clear(void) {}
static void v2_find_owner_service(void) {}
#endif

static void v2_display_begin(uint8_t opcode, uint8_t transaction_id) {
    s_gui_led_deferred=false;
    /* Foreground display work replaces a queued clock/calendar tick. */
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_boot_restore_target = INKTAG_BOOT_DISPLAY_NONE;
    s_native_refresh_pending = false;
    s_display_active = true;
    s_display_opcode = opcode;
    s_display_transaction = transaction_id;
    s_cancel_transaction = 0u;
}

static inktag_status_v2_t v2_store_status(inktag_image_store_result_t result) {
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

static inktag_status_v2_t v2_epd_status(void) {
    switch (inktag_epd_last_result()) {
        case INKTAG_EPD_RESULT_OK: return INKTAG_STATUS_OK;
        case INKTAG_EPD_RESULT_NO_BUSY_EDGE:
        case INKTAG_EPD_RESULT_TIMEOUT:
        case INKTAG_EPD_RESULT_PON_TIMEOUT:
        case INKTAG_EPD_RESULT_DRF_TIMEOUT:
        case INKTAG_EPD_RESULT_POF_TIMEOUT: return INKTAG_STATUS_TIMEOUT;
        case INKTAG_EPD_RESULT_STOPPED: return INKTAG_STATUS_CANCELLED;
        case INKTAG_EPD_RESULT_IO_ERROR: return INKTAG_STATUS_IO_ERROR;
        case INKTAG_EPD_RESULT_DENIED: return INKTAG_STATUS_DENIED;
        case INKTAG_EPD_RESULT_UNSUPPORTED: return INKTAG_STATUS_UNSUPPORTED;
        default: return INKTAG_STATUS_INVALID_STATE;
    }
}

static void v2_transfer_reset(bool abort_store) {
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        abort_store) inktag_image_store_abort();
    if (s_v2_transfer.active &&
        (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
         s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) &&
        abort_store) inktag_tlsr_device_info_qr_abort();
#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_abort_pro.inc"
#endif
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        inktag_epd_stop();
        inktag_epd_sleep();
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_picture_active = false;
    status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
}

static uint8_t v2_current_display_mode(void) {
    switch (inktag_settings()->display_mode) {
        case 1u: return INKTAG_DISPLAY_MODE_CALENDAR;
        case 2u: return INKTAG_DISPLAY_MODE_CLOCK;
        case 3u: return INKTAG_DISPLAY_MODE_CLEAR;
        case 4u: return INKTAG_DISPLAY_MODE_SLIDESHOW;
        case 5u: return INKTAG_DISPLAY_MODE_DEVICE_INFO;
        case 6u: return INKTAG_DISPLAY_MODE_DIRECT_IMAGE;
        case 0u:
        default: return INKTAG_DISPLAY_MODE_RESOURCE;
    }
}

static bool v2_activation_available(void) {
    return INKTAG_TLSR_ACTIVATION_IMPLEMENTED != 0 &&
           INKTAG_TLSR_ACTIVATION_BACKEND_AVAILABLE != 0 &&
           inktag_tlsr_board_activation_qualified(inktag_board_detect()) &&
           s_activation.supported && s_activation.identity_available;
}

static bool v2_find_available(void) {
    inktag_board_id_t board = inktag_board_detect();
    return INKTAG_TLSR_FIND_IMPLEMENTED != 0 &&
           INKTAG_TLSR_FIND_BACKEND_AVAILABLE != 0 &&
           (inktag_tlsr_board_find_qualified(board) ||
            inktag_tlsr_board_find_experimental_available(board));
}

static inktag_status_v2_t v2_find_access(bool experiment_enabled) {
    inktag_board_id_t board = inktag_board_detect();
    return inktag_access_check(
        &s_activation, INKTAG_PRODUCT_FEATURE_FIND_RUNTIME,
        INKTAG_TLSR_FIND_IMPLEMENTED != 0 &&
            INKTAG_TLSR_FIND_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_find_qualified(board) ||
            inktag_tlsr_board_find_experimental_available(board),
        inktag_tlsr_board_find_qualified(board) || experiment_enabled);
}

static bool v2_aux_io_available(void) {
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_protocol_aux_hook_1_pro.inc"
#endif
#else
    return false;
#endif
}

static bool v2_device_info_qr_available(void) {
    inktag_board_id_t board = inktag_board_detect();
    return INKTAG_TLSR_DEVICE_INFO_QR_IMPLEMENTED != 0 &&
           INKTAG_TLSR_DEVICE_INFO_QR_BACKEND_AVAILABLE != 0 &&
           inktag_tlsr_board_device_info_qr_qualified(board) &&
           board != INKTAG_BOARD_UNKNOWN &&
           inktag_tlsr_device_info_qr_available();
}

static bool v2_password_available(void) {
#if INKTAG_BUILD_PRO
    return v2_activation_available() &&
           v2_device_info_qr_available();
#else
    return s_activation.identity_available && v2_device_info_qr_available();
#endif
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_available_pro.inc"
#else
static bool v2_gui_asset_update_available(void) { return false; }

#endif

static uint32_t v2_capabilities(void) {
    inktag_board_id_t board = inktag_board_detect();
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_RESOURCES |
                            INKTAG_CAPABILITY_SLIDESHOW |
                            INKTAG_CAPABILITY_DEVICE_NAME |
                            INKTAG_CAPABILITY_DEVICE_LABEL |
                            INKTAG_CAPABILITY_TELEMETRY |
                            INKTAG_CAPABILITY_TEST |
                            INKTAG_CAPABILITY_HEALTH;
    const inktag_capability_gate_t nfc_gate = {
        INKTAG_TLSR_NFC_IMPLEMENTED != 0,
        INKTAG_TLSR_NFC_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_nfc_qualified(board),
        board != INKTAG_BOARD_UNKNOWN,
    };
    const inktag_capability_gate_t input_gate = {
        true, true,
        v2_aux_io_available(), v2_aux_io_available(),
    };
    const inktag_capability_gate_t io_gate = {
        INKTAG_TLSR_AUX_IO_IMPLEMENTED != 0,
        INKTAG_TLSR_AUX_IO_BACKEND_AVAILABLE != 0,
        v2_aux_io_available(), v2_aux_io_available(),
    };
    const inktag_capability_gate_t indicator_gate = {
        INKTAG_TLSR_INDICATOR_IMPLEMENTED != 0,
        INKTAG_TLSR_INDICATOR_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_indicator_qualified(board),
        board != INKTAG_BOARD_UNKNOWN,
    };
    const inktag_capability_gate_t lifecycle_gate = {
        INKTAG_TLSR_LIFECYCLE_IMPLEMENTED != 0,
        INKTAG_TLSR_LIFECYCLE_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_lifecycle_qualified(board),
        board != INKTAG_BOARD_UNKNOWN,
    };
    const inktag_capability_gate_t activation_gate = {
        INKTAG_TLSR_ACTIVATION_IMPLEMENTED != 0,
        INKTAG_TLSR_ACTIVATION_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_activation_qualified(board),
        s_activation.supported && s_activation.identity_available,
    };
    const inktag_capability_gate_t ota_gate = {
        true, true, inktag_tlsr_board_ota_qualified(board),
        board != INKTAG_BOARD_UNKNOWN,
    };
    const inktag_capability_gate_t find_gate = {
        INKTAG_TLSR_FIND_IMPLEMENTED != 0,
        INKTAG_TLSR_FIND_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_find_qualified(board),
        v2_find_available(),
    };
    const inktag_capability_gate_t experimental_find_gate = {
        INKTAG_TLSR_FIND_IMPLEMENTED != 0,
        INKTAG_TLSR_FIND_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_find_experimental_available(board) &&
            !inktag_tlsr_board_find_qualified(board),
        v2_find_available(),
    };
    const inktag_capability_gate_t device_info_qr_gate = {
        INKTAG_TLSR_DEVICE_INFO_QR_IMPLEMENTED != 0,
        INKTAG_TLSR_DEVICE_INFO_QR_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_device_info_qr_qualified(board),
        v2_device_info_qr_available(),
    };
    const inktag_capability_gate_t gui_asset_gate = {
        INKTAG_TLSR_GUI_ASSET_UPDATE_IMPLEMENTED != 0,
        INKTAG_TLSR_GUI_ASSET_UPDATE_BACKEND_AVAILABLE != 0,
        inktag_tlsr_board_gui_asset_update_qualified(board),
        v2_gui_asset_update_available(),
    };
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INPUT, &input_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_IO, &io_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_NFC, &nfc_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INDICATOR, &indicator_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_LIFECYCLE, &lifecycle_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_ACTIVATION, &activation_gate);
    if (v2_password_available())
        capabilities |= INKTAG_CAPABILITY_DEVICE_PASSWORD;
    if (inktag_capability_available(&lifecycle_gate) && v2_password_available())
        capabilities |= INKTAG_CAPABILITY_LIFECYCLE_V2;
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_OTA_RECOVERY, &ota_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_FIND, &find_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_DEVICE_INFO_QR,
        &device_info_qr_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_GUI_ASSET_UPDATE,
        &gui_asset_gate);
    if (board != INKTAG_BOARD_UNKNOWN && inktag_image_store_supported())
        capabilities |= INKTAG_CAPABILITY_IMAGE_METADATA;
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_EXPERIMENTAL_FIND,
        &experimental_find_gate);
#if INKTAG_TRANSFER_PACKBITS_ENABLED
    if (INKTAG_TLSR_TRANSFER_PACKBITS_IMPLEMENTED != 0 &&
        INKTAG_TLSR_TRANSFER_PACKBITS_BACKEND_AVAILABLE != 0 &&
        inktag_tlsr_board_transfer_packbits_qualified(board))
        capabilities |= INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE;
#endif
    if (inktag_tlsr_pin_routes_available()) capabilities |= INKTAG_CAPABILITY_PIN_CONFIG;
    if (inktag_epd_clock_partial_supported())
        capabilities |= INKTAG_CAPABILITY_CLOCK_PARTIAL_REFRESH;
    return capabilities;
}

static void v2_send_hello(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[6] = {1u, INKTAG_PROTOCOL_V2_VERSION, 0u, 0u, 0u, 0u};
    put_u16_le(&response[2], MAX_PACKET);
    put_u16_le(&response[4], MAX_PACKET - 7u);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_capabilities(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[INKTAG_CAPABILITIES_EXTENDED_SIZE] = {0};
    uint16_t mode_mask = 0u;
    uint16_t action_mask = 0u;
    uint8_t mode;
    response[0] = 1u;
    put_u32_le(&response[1], v2_capabilities());
    for (mode = INKTAG_DISPLAY_MODE_DEVICE_INFO;
         mode <= INKTAG_DISPLAY_MODE_SLIDESHOW; ++mode)
        mode_mask |= (uint16_t)(1u << mode);
    action_mask = (uint16_t)((1u << INKTAG_DISPLAY_ACTION_REFRESH) |
                             (1u << INKTAG_DISPLAY_ACTION_NEXT) |
                             (1u << INKTAG_DISPLAY_ACTION_PAUSE) |
                             (1u << INKTAG_DISPLAY_ACTION_RESUME) |
                             (1u << INKTAG_DISPLAY_ACTION_GHOST_CLEAN) |
                             (1u << INKTAG_DISPLAY_ACTION_SLEEP));
    put_u16_le(&response[5], mode_mask);
    put_u16_le(&response[7], action_mask);
    put_u16_le(&response[9], MAX_PACKET);
    put_u16_le(&response[11], MAX_PACKET - 7u);
    response[13] = sizeof(inktag_settings()->name);
    response[14] = sizeof(inktag_settings()->label);
    put_u16_le(&response[15], inktag_image_store_sector_count());
    /* No settings extension in this port: bytes17..23 remain zero padding. */
    put_u32_le(&response[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
        INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION
#if INKTAG_TRANSFER_LZO1X_ENABLED && INKTAG_TLSR_IMAGE_CODEC_PROFILE != 0
        | INKTAG_CAPABILITY_EXTENSION_TRANSFER_LZO1X_2000
#endif
        );
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_info_pro.inc"
#else
static void v2_send_gui_asset_info(uint8_t opcode, uint8_t transaction_id) {
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
}

#endif

static void v2_send_device_info(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[112] = {0};
    uint8_t image_field[4];
    uint32_t application_size;
    uint32_t application_crc;
    inktag_settings_t *settings = inktag_settings();
    const char *board = inktag_board_name();
    uint8_t *mac = inktag_ble_mac();
    uint8_t board_length = (uint8_t)strlen(board);
    uint16_t offset = 23u;
    char label[25];
    uint8_t label_length = effective_device_label(label);
    response[0] = 1u;
    response[1] = board_length;
#if INKTAG_BUILD_PRO
    response[2] = settings->name_length;
#else
    uint8_t basic_name_length;
    const uint8_t *basic_name = inktag_ble_basic_name(&basic_name_length);
    response[2] = basic_name_length;
#endif
    response[3] = label_length;
    response[4] = 1u;
    response[5] = mac[5]; response[6] = mac[4]; response[7] = mac[3];
    response[8] = mac[2]; response[9] = mac[1]; response[10] = mac[0];
    put_u32_le(&response[11], ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
                               ((uint32_t)INKTAG_VERSION_MINOR << 8) |
                               INKTAG_VERSION_PATCH);
    flash_read_page(INKTAG_APPLICATION_START + 24u, sizeof(image_field), image_field);
    application_size = read_u32_le(image_field);
    if (application_size >= 32u &&
        application_size <= INKTAG_APPLICATION_SIZE - 4u) {
        flash_read_page(INKTAG_APPLICATION_START + application_size, sizeof(image_field), image_field);
        application_crc = ((uint32_t)image_field[0] << 24) |
                          ((uint32_t)image_field[1] << 16) |
                          ((uint32_t)image_field[2] << 8) | image_field[3];
        put_u32_le(&response[15], application_size);
        put_u32_le(&response[19], application_crc);
    }
    memcpy(&response[offset], board, board_length); offset += board_length;
#if INKTAG_BUILD_PRO
    memcpy(&response[offset], settings->name, settings->name_length);
    offset += settings->name_length;
#else
    memcpy(&response[offset], basic_name, basic_name_length);
    offset += basic_name_length;
#endif
    memcpy(&response[offset], label, label_length);
    offset += label_length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, offset);
}

static void v2_send_status(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[27u + INKTAG_TEMPERATURE_WIRE_SIZE] = {0};
    uint8_t flags = 0u;
    uint8_t progress = 0u;
    uint8_t notify_last_status;
    uint8_t notify_pending_count;
    uint16_t notify_retry_count;
    uint16_t notify_drop_count;
    int8_t temperature = 0;
    int8_t mcu_temperature = 0;
    int16_t panel_sixteenths = 0;
    bool mcu_valid = inktag_tlsr_read_die_temperature(&mcu_temperature, NULL);
    uint8_t panel_temperature = inktag_epd_controller_temperature();
    bool panel_temperature_valid =
        inktag_epd_panel_temperature_sixteenths(&panel_sixteenths);
    bool temperature_valid = inktag_temperature_select_lower(
        mcu_valid, (int16_t)mcu_temperature * 16,
        panel_temperature_valid, panel_sixteenths, &temperature);
    uint16_t voltage = inktag_epd_voltage();
    if (s_display_active || s_native_page_active || s_picture_active ||
        inktag_epd_busy()) flags |= 0x01u;
    if (s_slideshow) flags |= 0x02u;
    if (voltage != 0u) flags |= 0x04u;
    if (temperature_valid) flags |= 0x08u;
    if (s_v2_transfer.active) flags |= 0x10u;
    if (s_v2_transfer.active && s_v2_transfer.total_size != 0u)
        progress = (uint8_t)((s_v2_transfer.written * 100u) /
                             s_v2_transfer.total_size);
    response[0] = 1u;
    response[1] = v2_current_display_mode();
    response[2] = flags;
    response[3] = progress;
    response[4] = s_v2_transfer.active ? INKTAG_CMD_TRANSFER_WRITE : s_display_opcode;
    response[5] = s_v2_transfer.active ? s_v2_transfer.transaction_id :
                                          s_display_transaction;
    response[6] = s_slideshow_id;
    response[7] = (uint8_t)temperature;
    put_u16_le(&response[8], voltage);
    put_u32_le(&response[10], inktag_settings()->slideshow_interval);
    put_u16_le(&response[14], (uint16_t)s_timezone_minutes);
    /* The extensible suffix retains actionable display result and panel TSC
     * telemetry after removing the obsolete GPIO/BUSY bring-up trace. */
    response[16] = 4u;
    response[17] = (uint8_t)inktag_epd_last_result();
    response[18] = panel_temperature;
    response[19] = inktag_epd_controller_temperature_fraction();
    response[20] = panel_temperature_valid ? 0x01u : 0u;
    inktag_ble_notify_diagnostics(&notify_last_status, &notify_retry_count,
                                   &notify_drop_count, &notify_pending_count);
    response[21] = notify_last_status;
    put_u16_le(&response[22], notify_retry_count);
    put_u16_le(&response[24], notify_drop_count);
    response[26] = notify_pending_count;
    inktag_temperature_wire_encode(&response[27], mcu_valid,
        (int16_t)mcu_temperature * 16, panel_temperature_valid, panel_sixteenths);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_health(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *request, uint16_t request_length) {
    uint8_t record_index;
    uint8_t response[INKTAG_TLSR_HEALTH_WIRE_SIZE +
                     INKTAG_SOAK_TELEMETRY_WIRE_SIZE];
    uint16_t response_length;
    uint16_t telemetry_length;

    if (request_length > 1u) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    record_index = request_length == 1u ? request[0] : 0u;
    if (record_index != 0u) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_NOT_FOUND, NULL, 0u);
        return;
    }
    response_length = inktag_tlsr_health_encode(
        record_index, response, INKTAG_TLSR_HEALTH_WIRE_SIZE);
    if (response_length == INKTAG_TLSR_HEALTH_WIRE_SIZE) {
        telemetry_length = inktag_soak_telemetry_encode(
            &response[response_length],
            (uint16_t)(sizeof(response) - response_length),
            inktag_app_monotonic_ms());
        response_length = (uint16_t)(response_length + telemetry_length);
    }
    send_v2_response(opcode, transaction_id,
                     response_length >= INKTAG_TLSR_HEALTH_WIRE_SIZE ?
                         INKTAG_STATUS_OK : INKTAG_STATUS_INTERNAL_ERROR,
                     response_length >= INKTAG_TLSR_HEALTH_WIRE_SIZE ?
                         response : NULL,
                     response_length >= INKTAG_TLSR_HEALTH_WIRE_SIZE ?
                         response_length : 0u);
}

static void v2_send_time(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[7];
    update_timestamp_from_clock();
    uint32_t utc = inktag_settings()->timestamp -
                   (int32_t)s_timezone_minutes * 60;
    put_u32_le(response, utc);
    put_u16_le(&response[4], (uint16_t)s_timezone_minutes);
    response[6] = inktag_settings()->display_settings.week_start;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_screen(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[EPD_SCREEN_WIRE_SIZE];
    epd_screen_status_t result = epd_screen_config_encode(inktag_epd_screen(), response);
    send_v2_response(opcode, transaction_id,
                     result == EPD_SCREEN_STATUS_OK ? INKTAG_STATUS_OK :
                                                      INKTAG_STATUS_INTERNAL_ERROR,
                     result == EPD_SCREEN_STATUS_OK ? response : NULL,
                     result == EPD_SCREEN_STATUS_OK ? sizeof(response) : 0u);
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_activation_pro.inc"
#else
static void v2_send_activation_status(uint8_t opcode,
                                      uint8_t transaction_id) {
    uint8_t response[INKTAG_ACTIVATION_STATUS_WIRE_SIZE];
    /* Existing read-only identity wire supports password AUTH, not Pro.
     * InkTag encodes no token scheme, challenge, grant or activation capability. */
    inktag_activation_encode_status(&s_activation, response);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

static void v2_send_activation_request(uint8_t opcode,
                                       uint8_t transaction_id,
                                       bool rotate) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_set_activation_code(uint8_t opcode, uint8_t transaction_id,
                                   const uint8_t *payload, uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
#endif

static bool v2_password_nonce(void *context, uint8_t *output, uint16_t length) {
    (void)context;
    return length == sizeof(s_password.nonce) &&
           inktag_tlsr_findmy_owner_nonce(output);
}

static void v2_password_request(uint8_t opcode, uint8_t transaction_id,
    uint8_t flags, const uint8_t *payload, uint16_t length) {
    uint8_t response[INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES];
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint16_t response_length = INKTAG_DEVICE_PASSWORD_STATUS_BYTES;
    if (!v2_password_available()) status = INKTAG_STATUS_UNSUPPORTED;
    else if (flags != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (!s_password_config.valid) status = INKTAG_STATUS_PERSISTENCE_ERROR;
    else if (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE) {
        if (length != 1u || payload[0] > INKTAG_PASSWORD_DISABLE)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!inktag_device_password_begin(&s_password, &s_password_config,
                s_nfc_now_ms, payload[0], v2_password_nonce, NULL, response))
            status = INKTAG_STATUS_DENIED;
        else response_length = INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES;
    } else if (opcode == INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD) {
        if (!inktag_device_password_authorize(&s_password, &s_password_config,
                s_nfc_now_ms, s_activation.identity.device_id, payload, length))
            status = INKTAG_STATUS_DENIED;
    } else if (length != 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO) {
#if INKTAG_BUILD_PRO
#include "inktag_protocol_password_show_pro.inc"
#else
        status=INKTAG_STATUS_UNSUPPORTED;
#endif
    }
    if (status == INKTAG_STATUS_OK &&
        response_length == INKTAG_DEVICE_PASSWORD_STATUS_BYTES &&
        !inktag_device_password_status(&s_password, &s_password_config,
                                      s_nfc_now_ms, response))
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    send_v2_response(opcode, transaction_id, status,
        status == INKTAG_STATUS_OK ? response : NULL,
        status == INKTAG_STATUS_OK ? response_length : 0u);
}

static void v2_send_identity_settings(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[64] = {0};
    inktag_settings_t *settings = inktag_settings();
    uint8_t *mac = inktag_ble_mac();
    uint16_t offset = 11u;
    char label[25];
    uint8_t label_length = effective_device_label(label);
    response[0] = INKTAG_SETTINGS_SECTION_IDENTITY;
    response[1] = 1u;
#if INKTAG_BUILD_PRO
    response[2] = settings->name_length;
#else
    uint8_t basic_name_length;
    const uint8_t *basic_name = inktag_ble_basic_name(&basic_name_length);
    response[2] = basic_name_length;
#endif
    response[3] = label_length;
    response[4] = 1u;
    response[5] = mac[5]; response[6] = mac[4]; response[7] = mac[3];
    response[8] = mac[2]; response[9] = mac[1]; response[10] = mac[0];
#if INKTAG_BUILD_PRO
    memcpy(&response[offset], settings->name, settings->name_length);
    offset += settings->name_length;
#else
    memcpy(&response[offset], basic_name, basic_name_length);
    offset += basic_name_length;
#endif
    memcpy(&response[offset], label, label_length);
    offset += label_length;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, offset);
}

static void v2_send_resource_info(uint8_t opcode, uint8_t transaction_id) {
    inktag_image_store_status_t status;
    uint32_t frame_bytes = screen_payload_size();
    inktag_image_store_result_t result = image_store_visible_status(
        inktag_epd_screen(), frame_bytes, &status);
    uint8_t response[34] = {0};
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
        return;
    }
    response[0] = 1u;
    put_u16_le(&response[1], inktag_epd_screen()->width);
    put_u16_le(&response[3], inktag_epd_screen()->height);
    put_u32_le(&response[5], frame_bytes);
    put_u16_le(&response[9], status.required_sectors);
    put_u16_le(&response[11], status.total_sectors);
    put_u16_le(&response[13], status.free_sectors);
    put_u16_le(&response[15], status.largest_free_run);
    put_u16_le(&response[17], status.saved_count);
    put_u16_le(&response[19], status.compatible_count);
    put_u16_le(&response[21], status.playable_count);
    put_u16_le(&response[23], status.incompatible_count);
    response[25] = s_slideshow ? 1u : 0u;
    put_u32_le(&response[26], inktag_settings()->slideshow_interval);
    /* Optional extension: older clients consume the original 30-byte prefix. */
    put_u16_le(&response[30], image_store_allocatable(&status));
    put_u16_le(&response[32], inktag_image_store_fixed_slot_count());
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_list_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    uint8_t after_id = length == 1u ? payload[0] : UINT8_MAX;
    if (length > 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    {
        uint8_t logical_id;
        inktag_image_store_header_t header;
        inktag_image_store_result_t result = image_store_next_visible(after_id, &logical_id, &header);
        if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NOT_FOUND) {
            send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
            return;
        }
        if (result == INKTAG_IMAGE_STORE_OK) {
            epd_screen_config_t stored;
            uint8_t response[64] = {0};
            uint8_t name_length = 0u;
            const uint8_t *name = (const uint8_t *)header.name;
            bool decoded = epd_screen_config_decode(header.screen_config,
                EPD_SCREEN_WIRE_SIZE, &stored) == EPD_SCREEN_STATUS_OK;
            while (name_length < INKTAG_IMAGE_STORE_NAME_SIZE &&
                   header.name[name_length] != '\0') ++name_length;
            response[0] = 0u;
            response[1] = logical_id;
            response[2] = header.content_type;
            response[3] = header.format;
            response[4] = header.segment_count;
            response[5] = decoded && screen_matches(header.screen_config) ? 1u : 0u;
            response[6] = name_length;
            put_u16_le(&response[7], decoded ? stored.width : 0u);
            put_u16_le(&response[9], decoded ? stored.height : 0u);
            put_u32_le(&response[11], header.payload_size);
            put_u32_le(&response[15], header.created_at);
            memcpy(&response[19], name, name_length);
            response[19u + name_length] =
                inktag_image_reading_rotation(header.black_mask);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                             response, (uint16_t)(20u + name_length));
            return;
        }
    }
    {
        uint8_t done[34] = {1u, UINT8_MAX};
        uint8_t bytes = (uint8_t)((inktag_image_store_fixed_slot_count() + 7u) / 8u);
        inktag_image_store_result_t result = inktag_image_store_occupancy(&done[2], sizeof(done) - 2u);
        send_v2_response(opcode, transaction_id, v2_store_status(result),
                         result == INKTAG_IMAGE_STORE_OK ? done : NULL,
                         result == INKTAG_IMAGE_STORE_OK ? (uint16_t)(2u + bytes) : 0u);
    }
}

static void v2_read_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    uint8_t response[MAX_PACKET - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    uint32_t offset;
    uint16_t requested;
    if (length != 6u || payload[5] == 0u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
        return;
    }
    if (!image_store_record_visible(payload[0])) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
    offset = read_u32_le(&payload[1]);
    requested = payload[5];
    if (requested > MAX_PACKET - 12u) requested = MAX_PACKET - 12u;
    if (offset >= header.payload_size) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (offset + requested > header.payload_size)
        requested = (uint16_t)(header.payload_size - offset);
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
    inktag_transfer_descriptor_t descriptor;
    epd_screen_t resolved;
    uint8_t expected_segments;
    uint32_t total_size;
    uint32_t segment_size[2];
    uint8_t response[5] = {0u, UINT8_MAX, 0u, 0u, 0u};
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (length < 20u || s_v2_transfer.active || s_picture_active) {
        send_v2_response(opcode, transaction_id,
                         (s_v2_transfer.active || s_picture_active) ?
                         INKTAG_STATUS_BUSY : INKTAG_STATUS_INVALID_REQUEST,
                         NULL, 0u);
        return;
    }
    if (payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
        payload[0] == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.object_type = payload[0];
        descriptor.format = payload[1];
        descriptor.segment_count = payload[2];
        descriptor.black_mask = payload[3];
        descriptor.total_size = read_u32_le(&payload[4]);
        descriptor.expected_crc = read_u32_le(&payload[8]);
        descriptor.segment_size[0] = read_u32_le(&payload[12]);
        descriptor.segment_size[1] = read_u32_le(&payload[16]);
        if (length != 20u ||
            (payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR &&
             !v2_device_info_qr_available()) ||
            (payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE &&
             !v2_password_available()) ||
            (payload[0] == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE &&
             !v2_gui_asset_update_available())) {
            send_v2_response(opcode, transaction_id,
                             length != 20u ? INKTAG_STATUS_INVALID_REQUEST :
                                             INKTAG_STATUS_UNSUPPORTED,
                             NULL, 0u);
            return;
        }
        if ((payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR &&
             !inktag_device_info_qr_transfer_valid(&descriptor)) ||
            (payload[0] == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE &&
             !inktag_tlsr_gui_asset_descriptor_valid(&descriptor))) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
        if (!inktag_tlsr_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_access_pro.inc"
#else

#endif
        if (payload[0] == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE &&
            inktag_access_check(&s_activation,
                INKTAG_PRODUCT_FEATURE_DEVICE_PASSWORD, true, true, true) !=
                    INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
        memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
        s_v2_transfer.active = true;
        s_v2_transfer.object_type = descriptor.object_type;
        s_v2_transfer.transaction_id = transaction_id;
        s_v2_transfer.format = descriptor.format;
        s_v2_transfer.segment_count = descriptor.segment_count;
        s_v2_transfer.black_mask = descriptor.black_mask;
        s_v2_transfer.resource_id = descriptor.black_mask;
        s_v2_transfer.replace_resource_id = UINT8_MAX;
        s_v2_transfer.total_size = descriptor.total_size;
        s_v2_transfer.expected_crc = descriptor.expected_crc;
        s_v2_transfer.running_crc = 0xFFFFFFFFu;
        s_v2_transfer.segment_size[0] = descriptor.segment_size[0];
        if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR &&
            !inktag_tlsr_device_info_qr_begin(&descriptor)) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE, NULL, 0u);
            return;
        }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_password_begin_pro.inc"
#else
        if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
            v2_transfer_reset(false);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            return;
        }
#endif
#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_begin_pro.inc"
#else
        if (descriptor.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
            v2_transfer_reset(false);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            return;
        }
#endif
        status_active(INKTAG_STATUS_SCENE_TRANSFER, true);
        response[0] = descriptor.object_type;
        response[1] = descriptor.black_mask;
        put_u16_le(&response[2], MAX_PACKET - 7u);
        response[4] = descriptor.segment_count;
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        return;
    }
    status = inktag_transfer_descriptor_decode(payload, length, &descriptor);
    if (status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    if (epd_screen_resolve(inktag_epd_screen(), &resolved) != EPD_SCREEN_STATUS_OK) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
        return;
    }
    expected_segments = (resolved.transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
                         resolved.transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
                         resolved.transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) ? 2u : 1u;
    total_size = read_u32_le(&payload[4]);
    segment_size[0] = read_u32_le(&payload[12]);
    segment_size[1] = read_u32_le(&payload[16]);
    if ((payload[0] != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
         payload[0] != INKTAG_TRANSFER_OBJECT_STORED_IMAGE) ||
        payload[1] != resolved.transfer_format || payload[2] != expected_segments ||
        segment_size[0] + (expected_segments == 2u ? segment_size[1] : 0u) != total_size ||
        total_size != resolved.frame_bytes) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_v2_transfer.active = true;
    s_v2_transfer.object_type = payload[0];
    s_v2_transfer.transaction_id = transaction_id;
    s_v2_transfer.format = payload[1];
    s_v2_transfer.segment_count = payload[2];
    s_v2_transfer.black_mask = payload[3];
    s_v2_transfer.resource_id = UINT8_MAX;
    s_v2_transfer.replace_resource_id = UINT8_MAX;
    s_v2_transfer.total_size = total_size;
    s_v2_transfer.expected_crc = read_u32_le(&payload[8]);
    s_v2_transfer.running_crc = 0xFFFFFFFFu;
    s_v2_transfer.segment_size[0] = segment_size[0];
    s_v2_transfer.segment_size[1] = segment_size[1];
    response[0] = payload[0];
    if (payload[0] == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        if (length != 20u) status = INKTAG_STATUS_INVALID_REQUEST;
        else {
            slideshow_stop(false);
            send_diag("picture:start");
            inktag_epd_init();
            status = v2_epd_status();
            if (status == INKTAG_STATUS_OK) {
                s_picture_active = true;
            }
        }
    } else {
        inktag_stored_image_v5_descriptor_t stored;
        epd_screen_config_t requested;
        uint8_t id = UINT8_MAX;
        bool replacing = false;
        status = inktag_stored_image_v5_descriptor_decode(payload, length, &stored);
        if (status != INKTAG_STATUS_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, status, NULL, 0u);
            return;
        }
        if (epd_screen_config_decode(stored.screen_profile, EPD_SCREEN_WIRE_SIZE,
                &requested) != EPD_SCREEN_STATUS_OK ||
            !screen_matches(stored.screen_profile)) {
            status = INKTAG_STATUS_INCOMPATIBLE;
        } else {
            status = image_store_create_access(
                &requested, total_size, stored.replace_slot, &s_v2_transfer.saved_count,
                &s_v2_transfer.replace_ordinal, &replacing);
            if (status == INKTAG_STATUS_OK) {
                status = v2_store_status(inktag_image_store_begin(
                    &requested, payload[1], payload[2],
                    (uint8_t)(payload[3] &
#if INKTAG_TRANSFER_LZO1X_ENABLED
                        INKTAG_IMAGE_STORED_FLAGS_MASK),
#else
                        ~INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE),
#endif
                    stored.content_type,
                    segment_size, total_size, s_v2_transfer.expected_crc,
                    inktag_settings()->timestamp, stored.name, stored.replace_slot, &id));
            }
        }
        s_v2_transfer.resource_id = id;
        s_v2_transfer.replace_resource_id = replacing ? stored.replace_slot : UINT8_MAX;
        response[1] = id;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    status_active(INKTAG_STATUS_SCENE_TRANSFER, true);
    put_u16_le(&response[2], MAX_PACKET - 7u);
    response[4] = expected_segments;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

typedef struct {
    uint32_t offset;
    inktag_status_v2_t status;
} v2_transfer_sink_context_t;

static bool v2_transfer_accept_decoded(void *opaque, uint8_t *bytes,
                                       uint16_t data_length) {
    v2_transfer_sink_context_t *context =
        (v2_transfer_sink_context_t *)opaque;
    if (s_v2_transfer.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE)
        s_v2_transfer.running_crc = inktag_crc32_update(
            s_v2_transfer.running_crc, bytes, data_length);
    if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        context->status = v2_store_status(
            inktag_image_store_append(bytes, data_length));
    } else if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
               s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
#if INKTAG_BUILD_PRO
#include "inktag_protocol_password_append_pro.inc"
#else
        if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        context->status = inktag_tlsr_device_info_qr_append(
            context->offset, bytes, data_length) ?
            INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_STATE;
        } else context->status = INKTAG_STATUS_UNSUPPORTED;
#endif
    } else if (s_v2_transfer.object_type ==
               INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_append_pro.inc"
#else
        context->status = INKTAG_STATUS_UNSUPPORTED;

#endif
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
            if (!inktag_brand_overlay_apply_oriented(
                    inktag_epd_screen(), s_v2_transfer.format, segment,
                    absolute - segment_start, &bytes[consumed], chunk,
                    inktag_app_brand_edition(),
                    inktag_image_reading_rotation(s_v2_transfer.black_mask))) {
                context->status = INKTAG_STATUS_INCOMPATIBLE;
                break;
            }
            inktag_epd_write_ram(absolute == segment_start, black,
                                 &bytes[consumed], chunk);
            consumed = (uint16_t)(consumed + chunk);
        }
    }
    if (context->status != INKTAG_STATUS_OK) return false;
    context->offset += data_length;
    return true;
}

static void v2_transfer_write(uint8_t opcode, uint8_t transaction_id,
                              uint8_t *payload, uint16_t length) {
    uint8_t *bytes;
    uint16_t data_length;
    bool compressed;
#if INKTAG_TRANSFER_LZO1X_ENABLED
    bool lzo;
#endif
    v2_transfer_sink_context_t context;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    if (length <= 4u || read_u32_le(payload) != s_v2_transfer.written) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    bytes = &payload[4];
    data_length = (uint16_t)(length - 4u);
    compressed = (s_v2_transfer.black_mask &
                  INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u;
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
        uint32_t capacity;
        size_t produced;
        uint8_t *decoded = inktag_epd_transfer_workspace(&capacity);
        /* Decode the entire bounded block before any sink-visible mutation.
         * The existing transaction gates serialize this shared render scratch. */
        if (length <= 6u || !data_length || data_length > 2000u ||
            data_length > capacity ||
            !inktag_lzo1x_decode(payload+6u,length-6u,decoded,data_length,&produced) ||
            produced != data_length) status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!v2_transfer_accept_decoded(&context,decoded,data_length))
            status = context.status;
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
        if (!v2_transfer_accept_decoded(&context, bytes, data_length))
            status = context.status;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    s_v2_transfer.written += data_length;
    {
        uint8_t response[4];
        put_u32_le(response, s_v2_transfer.written);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
    }
}

static void v2_send_complete(uint8_t opcode, uint8_t transaction_id,
                             inktag_status_v2_t status, uint8_t argument) {
    uint8_t response[1] = {argument};
    send_v2_event(INKTAG_EVENT_COMPLETE, status, opcode, transaction_id,
                  response, sizeof(response));
}

static void v2_display_complete_payload(uint8_t opcode, uint8_t transaction_id,
                                        inktag_status_v2_t status,
                                        const uint8_t *payload,
                                        uint16_t payload_length) {
    uint8_t cancel_transaction = s_cancel_transaction;
    bool quiet=s_gui_led_deferred;
    s_gui_led_deferred=false;
    bool reply_owned = s_display_active && s_display_opcode == opcode &&
                       s_display_transaction == transaction_id;
    if (cancel_transaction != 0u) status = INKTAG_STATUS_CANCELLED;
    s_display_active = false;
    s_display_opcode = 0u;
    s_display_transaction = 0u;
    s_cancel_transaction = 0u;
    if (!reply_owned) return; /* Disconnect revoked the vanished link. */
    if (cancel_transaction != 0u)
        send_v2_response(INKTAG_CMD_CANCEL, cancel_transaction,
                         INKTAG_STATUS_OK, NULL, 0u);
    if (status == INKTAG_STATUS_OK) {
        if (!quiet) status_trigger(INKTAG_STATUS_SCENE_SUCCESS);
    } else if (status != INKTAG_STATUS_CANCELLED)
        status_trigger(INKTAG_STATUS_SCENE_ERROR);
    send_v2_event(INKTAG_EVENT_COMPLETE, status, opcode, transaction_id,
                  payload, payload_length);
}

static void v2_display_complete(uint8_t opcode, uint8_t transaction_id,
                                inktag_status_v2_t status, uint8_t argument) {
    v2_display_complete_payload(opcode, transaction_id, status,
                                &argument, sizeof(argument));
}

static void v2_drain_reply_before_synchronous_refresh(void) {
    uint16_t elapsed_ms = 0u;

    /* Telink notifications are queued, not transmitted by the send call.
     * A large direct frame can enter a 30+ second EPD pass immediately after
     * TRANSFER_COMMIT. Offer the authoritative reply at real connection
     * events before that synchronous work starts, otherwise the host's commit
     * timeout disconnects and the disconnect callback cancels Area6. */
    bls_pm_setSuspendMask(SUSPEND_DISABLE);
    while (!inktag_ble_notifications_drained() && elapsed_ms < 1000u) {
        blt_sdk_main_loop();
        inktag_soak_telemetry_radio_maintenance();
        inktag_app_status_service();
        inktag_ble_process();
        wd_clear();
        WaitMs(1u);
        elapsed_ms++;
    }
}

static void v2_transfer_commit(uint8_t opcode, uint8_t transaction_id) {
    uint8_t object_type;
    uint32_t crc;
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id ||
        s_v2_transfer.written != s_v2_transfer.total_size) {
        if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            inktag_device_password_session_clear(&s_password);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    crc = s_v2_transfer.running_crc ^ 0xFFFFFFFFu;
    if (s_v2_transfer.object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        crc != s_v2_transfer.expected_crc) {
        if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            inktag_device_password_session_clear(&s_password);
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_CHECKSUM_ERROR, NULL, 0u);
        return;
    }
    object_type = s_v2_transfer.object_type;
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        uint8_t slot = s_v2_transfer.black_mask;
        if (s_password_config.enabled &&
            s_v2_transfer.total_size + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES >
                INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_NO_SPACE, NULL, 0u);
            return;
        }
        if (!inktag_tlsr_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, false)) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        if (!inktag_tlsr_device_info_qr_commit(&s_password_config)) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
        status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         &slot, 1u);
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, &slot, 1u);
        return;
    }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_password_commit_pro.inc"
#else
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
#if INKTAG_BUILD_PRO
#include "inktag_protocol_font_commit_pro.inc"
#else
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        v2_transfer_reset(true);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
#endif
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        const epd_screen_config_t *screen = inktag_epd_screen();
        bool replacing = s_v2_transfer.replace_resource_id != UINT8_MAX;
        uint8_t id = UINT8_MAX;
        /* Recheck current entitlement, using the bounded begin-time occupancy
         * facts: the replacement head is intentionally already erased. No
         * other image mutation can run while this transfer owns the lease. */
        inktag_status_v2_t access_status = inktag_access_image_write(
            &s_activation, s_v2_transfer.saved_count, replacing,
            s_v2_transfer.replace_ordinal, true, screen->visible_width,
            screen->visible_height);
        if (access_status != INKTAG_STATUS_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, access_status, NULL, 0u);
            return;
        }
        inktag_image_store_result_t result = inktag_image_store_commit(&id);
        if (result != INKTAG_IMAGE_STORE_OK) {
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
            return;
        }
        if (replacing) slideshow_stop(false);
        memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
        status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, &id, 1u);
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, &id, 1u);
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    status_active(INKTAG_STATUS_SCENE_TRANSFER, false);
    status_active(INKTAG_STATUS_SCENE_REFRESH, true);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     (const uint8_t[]){UINT8_MAX}, 1u);
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  opcode, transaction_id,
                  (const uint8_t[]){INKTAG_BUSY_PHASE_DISPLAY, 0u}, 2u);
    v2_display_begin(opcode, transaction_id);
    v2_drain_reply_before_synchronous_refresh();
    if (s_display_transaction != transaction_id || s_cancel_transaction != 0u) {
        /* Reply draining can dispatch disconnect/CANCEL. Do not resurrect
         * discarded host work or start a new panel pass after that boundary. */
        inktag_epd_sleep();
        s_picture_active = false;
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
        v2_display_complete(opcode, transaction_id,
                            INKTAG_STATUS_CANCELLED, UINT8_MAX);
        return;
    }
    (void)inktag_epd_refresh();
    inktag_epd_sleep();
    send_diag(inktag_epd_last_result() == INKTAG_EPD_RESULT_OK ?
              "picture:done" : "picture:error");
    s_picture_active = false;
    status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    if (inktag_epd_last_result() == INKTAG_EPD_RESULT_OK)
        record_display(6u, UINT8_MAX, 0u);
    v2_display_complete(opcode, transaction_id, v2_epd_status(), UINT8_MAX);
}

static void v2_run_display(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *payload, uint16_t length) {
    uint8_t mode;
    uint8_t argument = UINT8_MAX;
    bool ok = false;
    bool resume_slideshow;
    if (length < 1u || s_v2_transfer.active || s_picture_active) {
        send_v2_response(opcode, transaction_id,
                         length < 1u ? INKTAG_STATUS_INVALID_REQUEST : INKTAG_STATUS_BUSY,
                         NULL, 0u);
        return;
    }
    mode = payload[0];
    if ((mode == INKTAG_DISPLAY_MODE_RESOURCE && length != 2u) ||
        (mode == INKTAG_DISPLAY_MODE_SLIDESHOW && length != 1u && length != 5u)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    if (mode == INKTAG_DISPLAY_MODE_RESOURCE && length == 2u) {
        inktag_image_store_header_t header;
        inktag_image_store_result_t lookup =
            inktag_image_store_get(payload[1], &header);
        if (lookup != INKTAG_IMAGE_STORE_OK) {
            send_v2_response(opcode, transaction_id,
                             v2_store_status(lookup), NULL, 0u);
            return;
        }
        if (!image_store_record_visible(payload[1])) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_DENIED, NULL, 0u);
            return;
        }
        if (!stored_image_replayable(payload[1], false)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            return;
        }
    }
    if (mode > INKTAG_DISPLAY_MODE_SLIDESHOW) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  opcode, transaction_id,
                  (const uint8_t[]){INKTAG_BUSY_PHASE_DISPLAY, 0u}, 2u);
    v2_display_begin(opcode, transaction_id);
    /* A user-selected resource becomes the current slide when slideshow is
     * already active. Native pages and an explicit new slideshow still own
     * their existing stop/restart semantics. */
    resume_slideshow = mode == INKTAG_DISPLAY_MODE_RESOURCE && s_slideshow;
    if (!resume_slideshow) slideshow_stop(false);
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO)
        ok = show_gui(MODE_DEVICE_INFO, 5u, "device-info:done");
    else if (mode == INKTAG_DISPLAY_MODE_CALENDAR)
        ok = show_gui(MODE_CALENDAR, 1u, "calendar:done");
    else if (mode == INKTAG_DISPLAY_MODE_CLOCK)
        ok = show_gui(MODE_CLOCK, 2u, "clock:done");
    else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        argument = length > 1u && payload[1] <= 2u ? payload[1] : 1u;
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
        ok = clear_with_diag(argument);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
        if (ok) record_display(3u, argument, 0u);
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        argument = payload[1];
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
        ok = show_stored_with_diag(argument);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
        if (ok && resume_slideshow) {
            s_slideshow_id = argument;
            slideshow_arm_next_after_refresh();
            record_display(4u, argument,
                           inktag_settings()->slideshow_interval);
        } else if (ok) {
            record_display(0u, argument, 0u);
        }
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint32_t interval = length == 5u ? read_u32_le(&payload[1]) : 3600u;
        uint8_t first;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        if (image_store_next_visible_compatible(UINT8_MAX, true, &first)) {
            inktag_settings()->slideshow_interval = interval;
            s_slideshow = true;
            s_next_slide = 0u;
            argument = first;
            status_active(INKTAG_STATUS_SCENE_REFRESH, true);
            ok = show_stored_with_diag(first);
            status_active(INKTAG_STATUS_SCENE_REFRESH, false);
            if (s_slideshow) slideshow_arm_next_after_refresh();
            if (ok) {
                s_slideshow_id = first;
                if (s_slideshow) record_display(4u, first, interval);
            }
        }
    }
    v2_display_complete(opcode, transaction_id,
                        ok ? INKTAG_STATUS_OK :
                        (v2_epd_status() == INKTAG_STATUS_INVALID_STATE ?
                         INKTAG_STATUS_NOT_FOUND : v2_epd_status()), argument);
}

static inktag_status_v2_t v2_refresh_current(uint8_t *argument) {
    inktag_settings_t *settings = inktag_settings();
    bool ok = false;
    *argument = settings->display_argument;
    if (settings->display_mode == 5u)
        ok = show_gui(MODE_DEVICE_INFO, 5u, "device-info:done");
    else if (settings->display_mode == 1u)
        ok = show_gui(MODE_CALENDAR, 1u, "calendar:done");
    else if (settings->display_mode == 2u)
        ok = show_gui(MODE_CLOCK, 2u, "clock:done");
    else if (settings->display_mode == 3u) {
        uint8_t color = settings->display_argument <= 2u ?
                        settings->display_argument : 1u;
        *argument = color;
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
        ok = clear_with_diag(color);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    } else if ((settings->display_mode == 0u || settings->display_mode == 4u) &&
               settings->display_argument != UINT8_MAX) {
        inktag_image_store_header_t header;
        if (inktag_image_store_get(settings->display_argument, &header) ==
                INKTAG_IMAGE_STORE_OK &&
            !image_store_record_visible(settings->display_argument))
            return INKTAG_STATUS_DENIED;
        if (!stored_image_replayable(settings->display_argument, false))
            return INKTAG_STATUS_NOT_FOUND;
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
        ok = show_stored_with_diag(settings->display_argument);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
    } else {
        return INKTAG_STATUS_INVALID_STATE;
    }
    return ok ? INKTAG_STATUS_OK : v2_epd_status();
}

static void v2_ghost_clean_progress(uint8_t opcode, uint8_t transaction_id,
                                    uint8_t completed_passes,
                                    uint8_t total_passes) {
    uint8_t progress[2] = {
        INKTAG_BUSY_PHASE_DISPLAY,
        (uint8_t)(((uint16_t)completed_passes * 100u) / total_passes),
    };
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  opcode, transaction_id, progress, sizeof(progress));
}

static inktag_status_v2_t v2_ghost_clean(uint8_t opcode,
                                         uint8_t transaction_id,
                                         uint8_t cycles) {
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint8_t cycle;
    uint8_t completed_passes = 0u;
    uint8_t total_passes = (uint8_t)(cycles * 3u);
    bool panel_white = false;

    for (cycle = 0u; cycle < cycles; ++cycle) {
        panel_white = false;
        if (!inktag_epd_clear_pass(1u)) {
            status = v2_epd_status();
            break;
        }
        panel_white = true;
        v2_ghost_clean_progress(opcode, transaction_id,
                                ++completed_passes, total_passes);
        if (s_cancel_transaction != 0u) break;

        if (!inktag_epd_clear_pass(0u)) {
            panel_white = false;
            status = v2_epd_status();
            break;
        }
        panel_white = false;
        v2_ghost_clean_progress(opcode, transaction_id,
                                ++completed_passes, total_passes);
        if (s_cancel_transaction != 0u) break;

        if (!inktag_epd_clear_pass(1u)) {
            status = v2_epd_status();
            break;
        }
        panel_white = true;
        v2_ghost_clean_progress(opcode, transaction_id,
                                ++completed_passes, total_passes);
        if (s_cancel_transaction != 0u) break;
    }

    /* CANCEL can interrupt RAM streaming or BUSY wait inside a black pass.
     * Once that call has parked the controller, clear() starts a fresh full
     * transaction and therefore gives the panel one bounded best-effort
     * return to white. Keep the original failure result: a successful cleanup
     * must not disguise the pass that failed. */
    if (!panel_white) {
        inktag_status_v2_t retained = status;
        if (!inktag_epd_clear(1u) && retained == INKTAG_STATUS_OK)
            retained = v2_epd_status();
        status = retained;
    } else inktag_epd_sleep();
    if (s_slideshow) slideshow_arm_next_after_refresh();
    return status;
}

static void v2_display_action(uint8_t opcode, uint8_t transaction_id,
                              uint8_t flags, const uint8_t *payload,
                              uint16_t length) {
    if (length != 0u && payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN) {
        inktag_status_v2_t status;
        uint8_t cycles;
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
        cycles = payload[1];
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
        send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                      opcode, transaction_id,
                      (const uint8_t[]){INKTAG_BUSY_PHASE_DISPLAY, 0u}, 2u);
        v2_display_begin(opcode, transaction_id);
        status_active(INKTAG_STATUS_SCENE_REFRESH, true);
        status = v2_ghost_clean(opcode, transaction_id, cycles);
        status_active(INKTAG_STATUS_SCENE_REFRESH, false);
        v2_display_complete_payload(
            opcode, transaction_id, status,
            (const uint8_t[]){INKTAG_DISPLAY_ACTION_GHOST_CLEAN, cycles}, 2u);
    } else if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT) {
        bool advanced;
        inktag_status_v2_t status;
        uint8_t argument = UINT8_MAX;
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
        send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                      opcode, transaction_id,
                      (const uint8_t[]){INKTAG_BUSY_PHASE_DISPLAY, 0u}, 2u);
        v2_display_begin(opcode, transaction_id);
        advanced = page_cycle_advance(&argument);
        status = v2_epd_status();
        if (!advanced && status == INKTAG_STATUS_OK) status = INKTAG_STATUS_NOT_FOUND;
        v2_display_complete(opcode, transaction_id, status, argument);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && s_slideshow) {
        uint8_t id = s_slideshow_id;
        s_slideshow = false;
        s_next_slide = 0u;
        if (id != UINT8_MAX) record_display(0u, id, 0u);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_RESUME &&
               s_slideshow_id != UINT8_MAX) {
        s_slideshow = true;
        slideshow_arm_next_after_refresh();
        record_display(4u, s_slideshow_id, inktag_settings()->slideshow_interval);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        uint8_t argument = UINT8_MAX;
        inktag_status_v2_t status;
        if (inktag_settings()->display_mode == 6u ||
            (inktag_settings()->display_mode == 0u &&
             inktag_settings()->display_argument == UINT8_MAX)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_STATE, NULL, 0u);
            return;
        }
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
        send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                      opcode, transaction_id,
                      (const uint8_t[]){INKTAG_BUSY_PHASE_DISPLAY, 0u}, 2u);
        v2_display_begin(opcode, transaction_id);
        status = v2_refresh_current(&argument);
        v2_display_complete(opcode, transaction_id, status, argument);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        inktag_epd_sleep();
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE, NULL, 0u);
    }
}

static void v2_clock_rate_settings(uint8_t opcode, uint8_t transaction_id,
                                   const uint8_t *payload, uint16_t length) {
    int16_t ppm;
    uint8_t response[INKTAG_CLOCK_RATE_REPORT_BYTES];
    if (!inktag_tlsr_board_profile(inktag_board_detect())) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }
    if (opcode == INKTAG_CMD_SET_SETTINGS) {
        /* Admission/AUTH already ran. Invalid input must not touch time/rate. */
        if (!inktag_clock_rate_decode(payload, length, &ppm)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        update_timestamp_from_clock(); /* Consume pending elapsed at OLD rate. */
        (void)inktag_clock_rate_set(&s_utc_rate, ppm);
        /* Keep UTC and its millisecond remainder, resetting correction only. */
    }
    inktag_clock_rate_encode(s_utc_rate.ppm,
        inktag_tlsr_board_clock_rate_default(inktag_board_detect()), response);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_get_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        v2_send_identity_settings(opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        v2_clock_rate_settings(opcode, transaction_id, payload, length);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        inktag_display_settings_t display =
            inktag_settings()->display_settings;
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        display.flags &= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
        if (inktag_app_brand_edition() == INKTAG_BRAND_EDITION_HIDDEN) {
            display.flags |= INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN;
        }
        response_length = inktag_display_settings_encode(
            &display, response, sizeof(response));
        send_v2_response(
            opcode, transaction_id,
            response_length != 0u ? INKTAG_STATUS_OK :
                                    INKTAG_STATUS_INTERNAL_ERROR,
            response_length != 0u ? response : NULL,
            (uint16_t)response_length);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

static void v2_set_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    inktag_settings_t *settings = inktag_settings();
    if (length < 2u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (payload[0] == INKTAG_SETTINGS_SECTION_CLOCK_RATE) {
        v2_clock_rate_settings(opcode, transaction_id, payload, length);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        if (payload[1] != 1u ||
            length != (uint16_t)(4u + name_length + label_length) ||
#if INKTAG_BUILD_PRO
            !valid_name(&payload[4], name_length) ||
#else
            name_length != 0u ||
#endif
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
#if INKTAG_BUILD_PRO
        {
#include "inktag_protocol_name_compare_pro.inc"
            inktag_status_v2_t access = inktag_access_bluetooth_name(
                &s_activation, name_changed);
            if (access != INKTAG_STATUS_OK) {
                send_v2_response(opcode, transaction_id, access, NULL, 0u);
                return;
            }
        }
#include "inktag_protocol_identity_write_pro.inc"
#else
        settings->label_length = label_length;
        memset(settings->label, 0, sizeof(settings->label));
        memcpy(settings->label, &payload[4u + name_length], label_length);
        if (!inktag_settings_save()) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         (const uint8_t[]){0u}, 1u);
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, NULL, 0u);
#endif
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        inktag_display_settings_t requested;
        inktag_display_settings_t previous = settings->display_settings;
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        bool hidden_requested;
        inktag_status_v2_t access;
        if (payload[1] != INKTAG_DISPLAY_SETTINGS_WIRE_VERSION) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            return;
        }
        if (!inktag_display_settings_decode(payload, length, &requested) ||
            (requested.name_length != 0u &&
             !GUI_CountdownNameSupported(requested.name,
                                         requested.name_length))) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        hidden_requested =
            (requested.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN) != 0u;
        access = inktag_access_brand_hidden_setting(
            &s_activation, hidden_requested);
        if (access != INKTAG_STATUS_OK) {
            send_v2_response(opcode, transaction_id, access, NULL, 0u);
            return;
        }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_display_write_pro.inc"
#else
        /* Preserve an inactive old Pro preference; ordinary InkTag calendar
         * writes cannot create/change it or apply it to rendering. */
        requested.flags = (requested.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH) |
            (previous.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN);
        settings->display_settings = requested;
        if ((requested.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH) == 0u) {
            s_boot_restore_pending = false;
            s_boot_restore_due = false;
        }
        if (!inktag_settings_save()) {
            settings->display_settings = previous;
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
        } else {
            requested.flags &= INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH;
            response_length = inktag_display_settings_encode(
                &requested, response, sizeof(response));
            send_v2_response(
                opcode, transaction_id,
                response_length != 0u ? INKTAG_STATUS_OK :
                                        INKTAG_STATUS_INTERNAL_ERROR,
                response_length != 0u ? response : NULL,
                (uint16_t)response_length);
        }
#endif
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_nfc_access_pro.inc"
#include "inktag_protocol_nfc_pro.inc"
#else
static void v2_send_nfc_capabilities(uint8_t opcode, uint8_t transaction_id) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
static void v2_send_nfc_status(uint8_t opcode, uint8_t transaction_id) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
static void v2_get_nfc_ndef(uint8_t opcode, uint8_t transaction_id) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
static void v2_set_nfc_ndef(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
#endif

#if INKTAG_BUILD_PRO
#include "inktag_protocol_button_mask_pro.inc"
#endif

#if INKTAG_BUILD_PRO
#include "inktag_protocol_button_get_pro.inc"
#else
static void v2_get_input_config(uint8_t opcode,uint8_t transaction_id) {
    /* No maintained TLSR board defines a qualified factory-fitted key.
     * Report an empty read-only capability, never a saved custom binding. */
    const uint8_t response[14]={1u};
    send_v2_response(opcode,transaction_id,INKTAG_STATUS_OK,response,sizeof(response));
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_protocol_button_set_pro.inc"
#else
static void v2_set_input_config(uint8_t opcode,uint8_t transaction_id,const uint8_t *payload,uint16_t length) { (void)payload;(void)length;send_v2_status_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED); }
#endif

static void v2_get_pin_config(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[36];
    bool ok=inktag_tlsr_pin_routes_status(response);
    send_v2_response(opcode,transaction_id,
        ok?INKTAG_STATUS_OK:INKTAG_STATUS_UNSUPPORTED,ok?response:NULL,ok?36u:0u);
}

static void v2_set_pin_config(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload,uint16_t length) {
    uint8_t status[36];
    inktag_tlsr_pin_candidate_t prepared;
    bool override, committed;
    const uint8_t *candidate;
    if (!inktag_tlsr_pin_routes_available()) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED,NULL,0u); return;
    }
    if (length==2u && payload[1]==0u) {
        if (!inktag_tlsr_pin_routes_status(status)) {
            send_v2_response(opcode,transaction_id,INKTAG_STATUS_INVALID_STATE,NULL,0u); return;
        }
        candidate=&status[4]; override=false;
    } else if (length==18u && payload[1]==1u) {
        candidate=&payload[2]; override=true;
    } else {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_INVALID_REQUEST,NULL,0u); return;
    }
    if (!inktag_tlsr_pin_routes_prepare_override(candidate,override,&prepared)) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_INVALID_REQUEST,NULL,0u); return;
    }
    if (input_action_busy() || s_slideshow || s_nfc_action_pending ||
        inktag_nfc_service_pending() || inktag_app_requires_awake()) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_BUSY,NULL,0u); return;
    }
    if (!inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,true)) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_LOW_POWER,NULL,0u); return;
    }
    /* No active job is cancelled. Park the admitted old routes before the
     * atomic update; on uncertainty do not drive guessed old/new outputs. */
    inktag_epd_sleep();
    inktag_led_release();
    committed=inktag_settings_pin_commit(candidate,override);
    if (committed) inktag_tlsr_pin_routes_apply(&prepared);
    else inktag_tlsr_pin_routes_invalidate();
    /* Also clear the EPD's old cached pointer on uncertainty. */
    inktag_epd_configure(inktag_board_detect());
    inktag_led_init();
    status_reconfigure();
    if (!committed) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_PERSISTENCE_ERROR,NULL,0u); return;
    }
    v2_get_pin_config(opcode,transaction_id);
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_aux_get_pro.inc"
#else
static void v2_get_io_config(uint8_t opcode,uint8_t transaction_id) {
    /* Existing wire namespace: FF means unused. Screen/LED PIN_CONFIG is
     * separate; old custom input bytes never reserve ordinary screen/LED pins. */
    const uint8_t response[INKTAG_AUX_IO_WIRE_SIZE]={
        1u,255u,0u,255u,0u,255u,0u,0u,255u,255u,0xd0u,7u};
    send_v2_response(opcode,transaction_id,INKTAG_STATUS_OK,response,sizeof(response));
}
#endif

#if INKTAG_BUILD_PRO
#include "inktag_protocol_aux_set_pro.inc"
#else
static void v2_set_io_config(uint8_t opcode,uint8_t transaction_id,const uint8_t *payload,uint16_t length) {
    if (length && payload[0]==2u) {
        v2_set_pin_config(opcode,transaction_id,payload,length);
        return;
    }
    send_v2_status_response(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED);
}
#endif

static void v2_indicator_config(uint8_t opcode, uint8_t transaction_id,
                                bool write, const uint8_t *payload, uint16_t length) {
    uint8_t response[INKTAG_STATUS_WIRE_SIZE];
    if (!inktag_tlsr_board_indicator_qualified(inktag_board_detect())) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED,
                         NULL, 0u);
        return;
    }
    if (write) {
        inktag_status_config_t next;
        inktag_status_config_t previous;
        if (!inktag_status_config_decode(&next, payload, length)) {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        {
            inktag_status_v2_t access =
                inktag_access_indicator_policy(&s_activation, next.policy);
            if (access != INKTAG_STATUS_OK) {
                send_v2_response(opcode, transaction_id, access, NULL, 0u);
                return;
            }
        }
        previous = inktag_settings()->led;
        inktag_settings()->led = next;
        if (!inktag_settings_save()) {
            inktag_settings()->led = previous;
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        status_reconfigure();
    } else if (length != 0u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (!inktag_status_config_encode(&inktag_settings()->led, response)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR, NULL, 0u);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_find_pro.inc"
#else
static void v2_send_find_capabilities(uint8_t opcode,
                                      uint8_t transaction_id) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_send_find_config(uint8_t opcode, uint8_t transaction_id) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_set_find_config(uint8_t opcode, uint8_t transaction_id,
                               uint8_t flags, const uint8_t *payload,
                               uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static bool v2_clear_find_config(void) { return true; }

static void v2_find_action(uint8_t opcode, uint8_t transaction_id,
                           uint8_t flags, const uint8_t *payload,
                           uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static inktag_status_v2_t v2_find_owner_access(void) { return INKTAG_STATUS_UNSUPPORTED; }

static void v2_send_find_owner_status(uint8_t opcode,
                                      uint8_t transaction_id,
                                      const uint8_t *payload,
                                      uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_begin_find_owner_rotation(uint8_t opcode,
                                         uint8_t transaction_id,
                                         uint8_t flags,
                                         const uint8_t *payload,
                                         uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_stage_find_owner_rotation(uint8_t opcode,
                                         uint8_t transaction_id,
                                         const uint8_t *payload,
                                         uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_commit_find_owner_rotation(uint8_t opcode,
                                          uint8_t transaction_id,
                                          uint8_t flags,
                                          const uint8_t *payload,
                                          uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }

static void v2_abort_find_owner_rotation(uint8_t opcode,
                                         uint8_t transaction_id,
                                         const uint8_t *payload,
                                         uint16_t length) { send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED); }
#endif

static inktag_status_v2_t v2_clear_user_data(inktag_board_id_t board,
                                             uint16_t *removed) {
    uint16_t count = 0u;
    inktag_image_store_result_t result;
    slideshow_stop(false);
    v2_transfer_reset(true);
    result = inktag_image_store_clear_all(&count);
    if (result != INKTAG_IMAGE_STORE_OK) return v2_store_status(result);
    if (!inktag_settings_clear(board, INKTAG_SETTINGS_CLEAR_USER_DATA))
        return INKTAG_STATUS_PERSISTENCE_ERROR;
    if (removed != NULL) *removed = count;
    return INKTAG_STATUS_OK;
}

static void v2_system_action(uint8_t opcode, uint8_t transaction_id,
                             uint8_t flags, const uint8_t *payload, uint16_t length) {
    inktag_board_id_t board = inktag_board_detect();
    const inktag_lifecycle_capabilities_t capabilities = {
        .reset = true,
        .timed_sleep = false,
        .factory_reset = false,
        .ota_recovery = inktag_tlsr_board_ota_qualified(board),
        .reset_settings = inktag_tlsr_board_lifecycle_qualified(board),
        .clear_user_data = inktag_tlsr_board_lifecycle_qualified(board),
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &capabilities, &plan);
    if (status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    if (s_display_active || s_picture_active || s_v2_transfer.active ||
        s_system_reboot_pending) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0u);
        return;
    }
    if ((plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA ||
         plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) &&
        !inktag_tlsr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_LOW_POWER, NULL, 0u);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET) {
        (void)save_explicit_time();
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
        s_system_reboot_pending = true;
        s_system_reboot_reply_drained = false;
        s_system_reboot_at = s_nfc_now_ms + 250u;
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY) {
        if (!inktag_tlsr_health_operation_allowed(
                INKTAG_POWER_OPERATION_OTA, true)) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_LOW_POWER, NULL, 0u);
            return;
        }
        (void)save_explicit_time();
        status = inktag_tlsr_recovery_prepare_entry_status();
        if (status != INKTAG_STATUS_OK) {
            /* Preserve the fresh execution-site decision or readback failure;
             * another ADC sample cannot identify an earlier I/O failure. */
            send_v2_response(opcode, transaction_id, status,
                             NULL, 0u);
            status_trigger(INKTAG_STATUS_SCENE_ERROR);
        } else {
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
            status_active(INKTAG_STATUS_SCENE_OTA, true);
            /* Let the BLE loop transmit the V2 acknowledgement before the
             * application disappears and the same address returns as
             * Recovery. The invalid marker already makes a power loss safe. */
            s_system_reboot_pending = true;
            s_system_reboot_reply_drained = false;
            s_system_reboot_at = s_nfc_now_ms + 500u;
        }
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        uint16_t removed = 0u;
        uint8_t response[2];
        status = v2_clear_user_data(board, &removed);
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK)
            send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                          opcode, transaction_id, NULL, 0u);
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) {
        uint16_t removed = 0u;
        uint8_t response[2];
        if (!inktag_device_password_consume_settings_reset(
                &s_password, &s_password_config, s_nfc_now_ms)) {
            send_v2_response(opcode, transaction_id,
                s_password_config.valid ? INKTAG_STATUS_DENIED :
                    INKTAG_STATUS_PERSISTENCE_ERROR, NULL, 0u);
            return;
        }
        /* Park old outputs before removing their override. A false return
         * may follow commit, so uncertainty must not revive cached routes. */
        inktag_epd_sleep();
        inktag_led_release();
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_protocol_aux_hook_2_pro.inc"
#endif
#endif
        bool cleared = inktag_settings_clear(board, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS);
        inktag_tlsr_pin_routes_invalidate();
        bool routes = cleared && inktag_tlsr_pin_routes_refresh();
        inktag_epd_configure(board);
        inktag_led_init();
        status_reconfigure();
        if (cleared) cleared = inktag_settings_password_clear();
        /* Reconcile failure too: page magic may already have committed.
         * Never synthesize OFF from an unreadable/malformed authority. */
        bool loaded = inktag_tlsr_password_load(&s_password_config);
        inktag_device_password_session_clear(&s_password);
        status = cleared && routes && loaded && s_password_config.valid &&
            !s_password_config.enabled ? INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR;
        if (status == INKTAG_STATUS_OK) {
            inktag_ble_apply_settings_name();
            inktag_epd_configure(board);
#if INKTAG_TLSR_AUX_IO_BUILD_ENABLED
#if INKTAG_BUILD_PRO
#include "inktag_protocol_aux_hook_3_pro.inc"
#endif
#endif
        }
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK) {
            s_system_reboot_pending = true;
            s_system_reboot_reply_drained = false;
            s_system_reboot_at = s_nfc_now_ms + 250u;
        }
    } else {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

static void v2_delete_resource(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    inktag_image_store_result_t result;
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    {
        inktag_image_store_header_t header;
        result = inktag_image_store_get(payload[0], &header);
        if (result != INKTAG_IMAGE_STORE_OK) {
            send_v2_response(opcode, transaction_id,
                             v2_store_status(result), NULL, 0u);
            return;
        }
    }
    if (!image_store_record_visible(payload[0])) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
    slideshow_stop(false);
    result = inktag_image_store_delete(payload[0]);
    send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
    if (result == INKTAG_IMAGE_STORE_OK)
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, payload, 1u);
}

static void v2_clear_resources(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    uint16_t removed = 0u;
    uint8_t response[2];
    inktag_image_store_result_t result;
    if (length != 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    slideshow_stop(false);
    result = payload[0] == INKTAG_RESOURCE_CLEAR_INCOMPATIBLE ?
        image_store_clear_visible(true, &removed) :
        payload[0] == INKTAG_RESOURCE_CLEAR_ALL ?
        image_store_clear_visible(false, &removed) :
        INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    put_u16_le(response, removed);
    send_v2_response(opcode, transaction_id, v2_store_status(result),
                     result == INKTAG_IMAGE_STORE_OK ? response : NULL,
                     result == INKTAG_IMAGE_STORE_OK ? sizeof(response) : 0u);
}


#if INKTAG_BUILD_PRO
/* Shared transaction; quiet NFC corrections do not queue a redraw for the
 * jump itself. Existing timer work and subsequent natural boundaries survive. */
static inktag_status_v2_t apply_time(uint32_t timestamp,
    int16_t timezone, uint8_t week_start, bool quiet) {
    inktag_settings_t *settings = inktag_settings();
    update_timestamp_from_clock();
    uint32_t previous_timestamp = settings->timestamp;
    int16_t previous_timezone = s_timezone_minutes;
    uint8_t previous_week_start = settings->display_settings.week_start;
    inktag_elapsed_clock_t previous_clock = s_utc_elapsed_clock;
    inktag_clock_rate_t previous_rate = s_utc_rate;
    uint16_t previous_remainder = s_utc_remainder_ms;
    bool previous_refresh = s_native_refresh_pending;
#if INKTAG_BUILD_PRO
    bool previous_find_refresh = s_findmy_refresh_pending;
#endif

    settings->timestamp = (uint32_t)timestamp;
    settings->timezone_minutes = timezone;
    settings->display_settings.week_start = week_start;
    s_timezone_minutes = timezone;
    rebase_timestamp_clock(quiet ? timestamp : previous_timestamp,
                           quiet ? week_start : previous_week_start);
    if (save_explicit_time()) {
        if (quiet && s_slideshow)
            s_next_slide += timestamp - previous_timestamp;
        mark_findmy_time_valid();
        return INKTAG_STATUS_OK;
    }

    /* Raw deadlines/checkpoint backoff already include Flash service time.
     * Replay only UTC sampling on the old timeline, including its fractions;
     * a refused candidate must not leave a redraw or Find rotation pending. */
    settings->timestamp = previous_timestamp;
    settings->timezone_minutes = previous_timezone;
    settings->display_settings.week_start = previous_week_start;
    s_timezone_minutes = previous_timezone;
    s_utc_elapsed_clock = previous_clock;
    s_utc_rate = previous_rate;
    s_utc_remainder_ms = previous_remainder;
    s_native_refresh_pending = previous_refresh;
#if INKTAG_BUILD_PRO
    s_findmy_refresh_pending = previous_find_refresh;
#endif
    update_timestamp_from_clock();
    return INKTAG_STATUS_PERSISTENCE_ERROR;
}

/* Preserve the BLE entry's original local date/time admission unchanged. */
static inktag_status_v2_t v2_apply_time(int64_t timestamp,
                                     int16_t timezone, uint8_t week_start) {
    if (timestamp < 1704067200ll || timestamp > 4102444800ll ||
        timezone < -840 || timezone > 840 || week_start > 1u)
        return INKTAG_STATUS_INVALID_REQUEST;
    return apply_time((uint32_t)timestamp,timezone,week_start,false);
}

#else
/* The wire can represent 2000..2099, but this port's unchanged time record
 * admits local 2024-01-01..2100-01-01. Reject before touching any live clock. */
static inktag_status_v2_t v2_apply_time(int64_t timestamp,
                                     int16_t timezone, uint8_t week_start) {
    inktag_settings_t *settings = inktag_settings();
    if (timestamp < 1704067200ll || timestamp > 4102444800ll ||
        timezone < -840 || timezone > 840 || week_start > 1u)
        return INKTAG_STATUS_INVALID_REQUEST;

    update_timestamp_from_clock();
    uint32_t previous_timestamp = settings->timestamp;
    int16_t previous_timezone = s_timezone_minutes;
    uint8_t previous_week_start = settings->display_settings.week_start;
    inktag_elapsed_clock_t previous_clock = s_utc_elapsed_clock;
    inktag_clock_rate_t previous_rate = s_utc_rate;
    uint16_t previous_remainder = s_utc_remainder_ms;
    bool previous_refresh = s_native_refresh_pending;
#if INKTAG_BUILD_PRO
    bool previous_find_refresh = s_findmy_refresh_pending;
#endif

    settings->timestamp = (uint32_t)timestamp;
    settings->timezone_minutes = timezone;
    settings->display_settings.week_start = week_start;
    s_timezone_minutes = timezone;
    rebase_timestamp_clock(previous_timestamp, previous_week_start);
    if (save_explicit_time()) {
        mark_findmy_time_valid();
        return INKTAG_STATUS_OK;
    }

    /* Raw deadlines/checkpoint backoff already include Flash service time.
     * Replay only UTC sampling on the old timeline, including its fractions;
     * a refused candidate must not leave a redraw or Find rotation pending. */
    settings->timestamp = previous_timestamp;
    settings->timezone_minutes = previous_timezone;
    settings->display_settings.week_start = previous_week_start;
    s_timezone_minutes = previous_timezone;
    s_utc_elapsed_clock = previous_clock;
    s_utc_rate = previous_rate;
    s_utc_remainder_ms = previous_remainder;
    s_native_refresh_pending = previous_refresh;
#if INKTAG_BUILD_PRO
    s_findmy_refresh_pending = previous_find_refresh;
#endif
    update_timestamp_from_clock();
    return INKTAG_STATUS_PERSISTENCE_ERROR;
}

#endif

static void v2_set_time(uint8_t opcode, uint8_t transaction_id,
                        const uint8_t *payload, uint16_t length) {
    inktag_status_v2_t status = INKTAG_STATUS_INVALID_REQUEST;
    if (length == 7u) {
        int16_t timezone = (int16_t)read_u16_le(&payload[4]);
        status = v2_apply_time((int64_t)read_u32_le(payload) +
                                  (int32_t)timezone * 60,
                              timezone, payload[6]);
    }
    if (status == INKTAG_STATUS_OK) v2_send_time(opcode, transaction_id);
    else send_v2_response(opcode, transaction_id, status, NULL, 0u);
}

static bool v2_test_time_mode(uint8_t mode) {
    return mode == INKTAG_DISPLAY_MODE_CALENDAR ||
           mode == INKTAG_DISPLAY_MODE_CLOCK;
}

static void v2_list_tests(uint8_t opcode, uint8_t transaction_id) {
    inktag_board_id_t board = inktag_board_detect();
    uint8_t response[10] = {1u, 0u};
    uint8_t count = 0u;

    if (v2_aux_io_available())
        response[2u + count++] = INKTAG_TEST_INPUT_SIMULATE;
    if (inktag_tlsr_board_led_diagnostic_available(board))
        response[2u + count++] = INKTAG_TEST_LED_CHANNEL;
    if (inktag_tlsr_board_nfc_type4_ndef_available(board))
        response[2u + count++] = INKTAG_TEST_NFC_CHANNEL_SNAPSHOT;
    if (inktag_tlsr_board_led_diagnostic_available(board))
        response[2u + count++] = INKTAG_TEST_DEVICE_LOCATE;
    response[2u + count++] = INKTAG_TEST_ADC_READ;
    response[2u + count++] = INKTAG_TEST_TIME_SHIFT;
    response[2u + count++] = INKTAG_TEST_TIME_SET;
    response[1] = count;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, (uint16_t)(2u + count));
}

static void v2_run_time_test(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    inktag_settings_t *settings = inktag_settings();
    uint32_t previous_timestamp = settings->timestamp;
    int16_t previous_timezone = s_timezone_minutes;
    uint8_t previous_week_start = settings->display_settings.week_start;
    uint32_t next_timestamp = previous_timestamp;
    int16_t next_timezone = previous_timezone;
    uint8_t next_week_start = previous_week_start;
    uint8_t response[7];
    bool shown;

    if (length < 2u || !v2_test_time_mode(payload[1])) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    if (inktag_epd_screen() == NULL) {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_INVALID_STATE, NULL, 0u);
        return;
    }
    if (payload[0] == INKTAG_TEST_TIME_SHIFT) {
        int32_t delta_seconds;
        int64_t candidate;
        if (length != 6u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        delta_seconds = (int32_t)read_u32_le(&payload[2]);
        candidate = (int64_t)previous_timestamp + delta_seconds;
        if (delta_seconds == 0 || delta_seconds < -(366 * 86400) ||
            delta_seconds > 366 * 86400 || candidate < 946684800ll ||
            candidate > 4102444799ll) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        next_timestamp = (uint32_t)candidate;
    } else if (payload[0] == INKTAG_TEST_TIME_SET) {
        uint32_t utc_timestamp;
        int64_t candidate;
        if (length != 9u || payload[8] > 1u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        utc_timestamp = read_u32_le(&payload[2]);
        next_timezone = (int16_t)read_u16_le(&payload[6]);
        candidate = (int64_t)utc_timestamp + (int32_t)next_timezone * 60;
        if (next_timezone < -840 || next_timezone > 840 ||
            candidate < 946684800ll || candidate > 4102444799ll) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        next_timestamp = (uint32_t)candidate;
        next_week_start = payload[8];
    } else {
        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
        return;
    }

    inktag_status_v2_t status = v2_apply_time(
        next_timestamp, next_timezone, next_week_start);
    if (status != INKTAG_STATUS_OK) {
        send_v2_response(opcode, transaction_id, status, NULL, 0u);
        return;
    }
    put_u32_le(response, settings->timestamp -
               (int32_t)s_timezone_minutes * 60);
    put_u16_le(&response[4], (uint16_t)s_timezone_minutes);
    response[6] = settings->display_settings.week_start;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));

    v2_display_begin(opcode, transaction_id);
    shown = show_gui(payload[1] == INKTAG_DISPLAY_MODE_CALENDAR ?
                         MODE_CALENDAR : MODE_CLOCK,
                     payload[1] == INKTAG_DISPLAY_MODE_CALENDAR ? 1u : 2u,
                     payload[1] == INKTAG_DISPLAY_MODE_CALENDAR ?
                         "calendar:done" : "clock:done");
    v2_display_complete(opcode, transaction_id,
                        shown ? INKTAG_STATUS_OK : v2_epd_status(),
                        payload[1]);
}

#if INKTAG_BUILD_PRO
#include "inktag_protocol_input_sim_begin_pro.inc"
#else
static void v2_input_simulation_begin(uint8_t transaction,uint8_t selector) { (void)selector;send_v2_status_response(INKTAG_CMD_RUN_TEST,transaction,INKTAG_STATUS_UNSUPPORTED); }
#endif

#if INKTAG_BUILD_PRO
#include "inktag_protocol_input_sim_service_pro.inc"
#else
static void v2_input_simulation_service(void) {}
#endif

static void v2_cancel(uint8_t transaction, const uint8_t *payload, uint16_t length) {
    inktag_status_v2_t status;
    if (length == 0u) {
        v2_transfer_reset(true);
        slideshow_stop(false);
        s_native_refresh_paused = true;
        s_native_refresh_pending = false;
        send_v2_response(INKTAG_CMD_CANCEL, transaction, INKTAG_STATUS_OK, NULL, 0u);
        return;
    }
    if (length != 2u || payload[0] != INKTAG_CMD_RUN_TEST) {
        send_v2_response(INKTAG_CMD_CANCEL, transaction,
                         INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_input_sim_cancel_pro.inc"
#else
    status = INKTAG_STATUS_UNSUPPORTED;
#endif
    if (status != INKTAG_STATUS_OK)
        send_v2_response(INKTAG_CMD_CANCEL, transaction, status, NULL, 0u);
    /* A matched cancel replies only after normal dispatch cleanup/drain. */
}

static void v2_run_test(uint8_t opcode, uint8_t transaction_id,
                         const uint8_t *payload, uint16_t length) {
    if (length < 1u) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
    } else if (payload[0] == INKTAG_TEST_INPUT_SIMULATE) {
        if (length != 2u || payload[1] > 3u)
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        else v2_input_simulation_begin(transaction_id, payload[1]);
    } else if (payload[0] == INKTAG_TEST_RESERVED_03) {

        send_v2_response(opcode, transaction_id,
                         INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    } else if (payload[0] == INKTAG_TEST_DEVICE_LOCATE) {
        bool led_available = inktag_tlsr_board_led_diagnostic_available(
            inktag_board_detect());
        bool accepted;
        if (length != 2u || payload[1] > 1u) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            return;
        }
        if (!led_available) {
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            return;
        }
        if (payload[1] == 0u) {
            inktag_status_locate_stop(&s_status, s_nfc_now_ms);
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_OK, NULL, 0u);
            return;
        }
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
        update_timestamp_from_clock();
        inktag_status_locate_start(&s_status, s_nfc_now_ms);
        accepted = led_available;
        if (!accepted) inktag_status_locate_stop(&s_status, s_nfc_now_ms);
        send_v2_response(opcode, transaction_id,
                         accepted ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_STATE,
                         NULL, 0u);
    } else if (payload[0] == INKTAG_TEST_LED_CHANNEL && length == 2u &&
               inktag_tlsr_board_led_diagnostic_available(
                   inktag_board_detect()) &&
               payload[1] <= INKTAG_STATUS_COLOR_WHITE) {
        status_test_led(payload[1]);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
        v2_send_complete(opcode, transaction_id, INKTAG_STATUS_OK, payload[1] & 0x07u);
    } else if (payload[0] == INKTAG_TEST_ADC_READ && length == 2u &&
               payload[1] <= 2u) {
        uint8_t response[6] = {1u, payload[1], 0u, 0u, 0u, 0u};
        uint16_t reading = 0u;
        int8_t temperature = 0;
        bool valid;
        if (payload[1] == 0u) {
            valid = inktag_tlsr_read_die_temperature(&temperature, &reading);
        } else if (payload[1] == 1u) {
            reading = inktag_epd_voltage();
            valid = reading != 0u;
        } else {
            valid = inktag_tlsr_adc_reference(&reading);
        }
        response[2] = valid ? 1u : 0u;
        put_u16_le(&response[3], reading);
        response[5] = payload[1] == 0u ? (uint8_t)temperature : 0u;
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        v2_send_complete(opcode, transaction_id, INKTAG_STATUS_OK, payload[1]);
    } else if (payload[0] == INKTAG_TEST_NFC_CHANNEL_SNAPSHOT &&
               (length == 1u
#if INKTAG_BUILD_PRO
                || (length == 2u && payload[1] == 1u)
                || (length == 27u && (payload[1] == 2u || payload[1] == 3u))
#endif
               ) &&
               inktag_tlsr_board_nfc_type4_ndef_available(
                   inktag_board_detect())) {
#if INKTAG_BUILD_PRO
#include "inktag_protocol_nfc_diagnostic_pro.inc"
#else
        send_v2_status_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
    } else if (payload[0] == INKTAG_TEST_TIME_SHIFT ||
               payload[0] == INKTAG_TEST_TIME_SET) {
        v2_run_time_test(opcode, transaction_id, payload, length);
    } else {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
    }
}

/* One admission decision at RX and deferred dispatch. Readback, storage-only
 * transfer and passive diagnostics remain usable; SLEEP/PAUSE/ABORT/CANCEL
 * must always reach their existing safety/authentication paths. */
static inktag_status_v2_t v2_panel_request_access(
    uint8_t opcode, const uint8_t *payload, uint16_t length) {
#if !INKTAG_BUILD_PRO
    /* Reject known absent work before queue, boot ownership or busy state.
     * Framing and device AUTH are checked first by both callers. */
    switch (opcode) {
        case INKTAG_CMD_SET_INPUT_CONFIG:
        case INKTAG_CMD_GET_NFC_CAPABILITIES: case INKTAG_CMD_GET_NFC_STATUS:
        case INKTAG_CMD_GET_NFC_NDEF: case INKTAG_CMD_SET_NFC_NDEF:
        case INKTAG_CMD_GET_FIND_CAPABILITIES: case INKTAG_CMD_GET_FIND_CONFIG:
        case INKTAG_CMD_SET_FIND_CONFIG: case INKTAG_CMD_FIND_ACTION:
        case INKTAG_CMD_GET_FIND_OWNER_STATUS: case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION: case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
        case INKTAG_CMD_ABORT_FIND_OWNER_ROTATION:
        case INKTAG_CMD_SET_ACTIVATION_CODE: case INKTAG_CMD_GET_ACTIVATION_REQUEST:
        case INKTAG_CMD_GET_OWNER_CHALLENGE: case INKTAG_CMD_AUTHORIZE_OWNER_ACTION:
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
        case INKTAG_CMD_GET_GUI_ASSET_INFO: case INKTAG_CMD_GET_LAYOUT_CAPABILITIES:
        case INKTAG_CMD_VALIDATE_LAYOUT: case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_GET_LAYOUT_STATUS: case INKTAG_CMD_GET_SECURITY_INFO:
        case INKTAG_CMD_GET_TEST_STATUS: case INKTAG_CMD_OPEN_DEBUG_SESSION:
        case INKTAG_CMD_DEBUG_EXECUTE: case INKTAG_CMD_DEBUG_READ:
        case INKTAG_CMD_CLOSE_DEBUG_SESSION: case INKTAG_CMD_ABORT_DEBUG_OPERATION:
            return INKTAG_STATUS_UNSUPPORTED;
        default: break;
    }
    if (opcode==INKTAG_CMD_SET_IO_CONFIG && (!length || payload[0]!=2u))
        return INKTAG_STATUS_UNSUPPORTED;
    if (length && ((opcode==INKTAG_CMD_TRANSFER_BEGIN &&
            (payload[0]==INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ||
             payload[0]==INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)) ||
        (opcode==INKTAG_CMD_RUN_TEST &&
            (payload[0]==INKTAG_TEST_NFC_CHANNEL_SNAPSHOT ||
             payload[0]==INKTAG_TEST_INPUT_SIMULATE || payload[0]==INKTAG_TEST_RESERVED_03))))
        return INKTAG_STATUS_UNSUPPORTED;
#endif
    bool drives = false;
    if (opcode == INKTAG_CMD_SET_SCREEN_PROFILE) {
        epd_screen_config_t requested;
        if (epd_screen_config_decode_exact(payload, length, &requested) ==
                EPD_SCREEN_STATUS_OK)
            return inktag_app_screen_access(&requested, inktag_tlsr_pin_routes_available());
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
        drives = payload[0] == INKTAG_TEST_TIME_SHIFT ||
                 payload[0] == INKTAG_TEST_TIME_SET;
    return drives ? inktag_epd_control_access(inktag_epd_screen()) : INKTAG_STATUS_OK;
}

#if INKTAG_BUILD_PRO
#include "inktag_app_advertising_pro.inc"
#endif

static void process_packet(uint8_t *data, uint16_t length) {
    uint8_t opcode;
    uint8_t transaction_id;
    uint8_t flags;
    uint8_t *payload;
    uint16_t payload_length;
    if (!data || length < INKTAG_PROTOCOL_V2_HEADER_SIZE) return;
    opcode = data[0];
    transaction_id = data[1];
#if INKTAG_TLSR_NFC_FMSC_PILOT
    if (nfc_image_blocks_opcode(opcode)) {
        send_v2_response(opcode,transaction_id,INKTAG_STATUS_BUSY,NULL,0u);return;
    }
#endif
    payload = &data[3];
    payload_length = length - 3u;
    if (transaction_id == 0u ||
        !inktag_release_request_flags(data[2], &flags)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
        return;
    }
    /* Capability/backend failure cannot downgrade any board into open mode.
     * Admission uses the loaded authority even when reporting is unavailable. */
    if (!inktag_device_password_command_allowed(&s_password, &s_password_config, opcode)) {
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_DENIED, NULL, 0u);
        return;
    }
#if INKTAG_BUILD_PRO
#include "inktag_protocol_nfc_admission_pro.inc"
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
    switch (opcode) {
        case INKTAG_CMD_HELLO: v2_send_hello(opcode, transaction_id); break;
        case INKTAG_CMD_GET_CAPABILITIES: v2_send_capabilities(opcode, transaction_id); break;
        case INKTAG_CMD_GET_DEVICE_INFO: v2_send_device_info(opcode, transaction_id); break;
        case INKTAG_CMD_GET_STATUS: v2_send_status(opcode, transaction_id); break;
        case INKTAG_CMD_GET_HEALTH:
            v2_send_health(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_GET_TIME: v2_send_time(opcode, transaction_id); break;
        case INKTAG_CMD_SET_TIME:
            v2_set_time(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE: v2_send_screen(opcode, transaction_id); break;
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            epd_screen_config_t requested;
            epd_screen_status_t screen_status = epd_screen_config_decode_exact(
                payload, payload_length, &requested);
            if (s_v2_transfer.active || s_picture_active)
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_BUSY, NULL, 0u);
            else if (screen_status != EPD_SCREEN_STATUS_OK)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INCOMPATIBLE, NULL, 0u);
            else {
                inktag_status_v2_t status=inktag_epd_set_screen(&requested);
                if (status!=INKTAG_STATUS_OK)
                    send_v2_response(opcode,transaction_id,status,NULL,0u);
                else {
                    v2_send_screen(opcode, transaction_id);
                    send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                                  opcode, transaction_id, NULL, 0u);
                }
            }
            break;
        }
        case INKTAG_CMD_SET_DISPLAY_MODE:
            v2_run_display(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_DISPLAY_ACTION:
            v2_display_action(opcode, transaction_id, flags,
                              payload, payload_length); break;
        case INKTAG_CMD_CANCEL:
            if (flags != 0u)
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_cancel(transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_SETTINGS:
            v2_get_settings(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_SET_SETTINGS:
            v2_set_settings(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_GET_OWNER_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_OWNER_ACTION:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
            v2_password_request(opcode, transaction_id, flags, payload, payload_length);
            break;
        case INKTAG_CMD_SYSTEM_ACTION:
            v2_system_action(opcode, transaction_id, flags, payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_BEGIN:
            v2_transfer_begin(opcode, transaction_id,
                              payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_WRITE:
            v2_transfer_write(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_COMMIT:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_transfer_commit(opcode, transaction_id);
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!s_v2_transfer.active)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_STATE, NULL, 0u);
            else {
                v2_transfer_reset(true);
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, NULL, 0u);
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
        case INKTAG_CMD_GET_RESOURCE_INFO: v2_send_resource_info(opcode, transaction_id); break;
        case INKTAG_CMD_LIST_RESOURCES:
            v2_list_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_READ_RESOURCE:
            v2_read_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_DELETE_RESOURCE:
            v2_delete_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_CLEAR_RESOURCES:
            v2_clear_resources(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_SET_RESOURCE_ALIAS:
        case INKTAG_CMD_SET_IMAGE_METADATA:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_GET_INPUT_CONFIG:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_get_input_config(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_INPUT_CONFIG:
            v2_set_input_config(opcode, transaction_id,
                                payload, payload_length);
            break;
#if INKTAG_BUILD_PRO
        case INKTAG_CMD_GET_ADVERTISING_CONFIG:
        case INKTAG_CMD_SET_ADVERTISING_CONFIG:
            v2_advertising_config(opcode, transaction_id, payload, payload_length);
            break;
#endif
        case INKTAG_CMD_GET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, false, payload, payload_length); break;
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, true, payload, payload_length); break;
        case INKTAG_CMD_LIST_TESTS:
            v2_list_tests(opcode, transaction_id); break;
        case INKTAG_CMD_RUN_TEST:
            if (flags != 0u)
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_run_test(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TEST_STATUS:
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u); break;
        case INKTAG_CMD_GET_NFC_CAPABILITIES:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_send_nfc_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_STATUS:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_send_nfc_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_NFC_NDEF:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else v2_get_nfc_ndef(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_NFC_NDEF:
            v2_set_nfc_ndef(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_IO_CONFIG:
            if (payload_length==1u && payload[0]==2u)
                v2_get_pin_config(opcode,transaction_id);
            else if (payload_length==1u && payload[0]==3u)
                v2_get_io_config(opcode,transaction_id);
            else
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            break;
        case INKTAG_CMD_SET_IO_CONFIG:
            v2_set_io_config(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_LAYOUT_CAPABILITIES:
        case INKTAG_CMD_VALIDATE_LAYOUT:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_GET_LAYOUT_STATUS:
        case INKTAG_CMD_GET_SECURITY_INFO:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_GET_FIND_CAPABILITIES:
            v2_send_find_capabilities(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_FIND_CONFIG:
            v2_send_find_config(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_FIND_CONFIG:
            v2_set_find_config(opcode, transaction_id, flags,
                               payload, payload_length);
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
        case INKTAG_CMD_OPEN_DEBUG_SESSION:
        case INKTAG_CMD_DEBUG_READ:
        case INKTAG_CMD_CLOSE_DEBUG_SESSION:
        case INKTAG_CMD_ABORT_DEBUG_OPERATION:
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_DEBUG_EXECUTE:
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNSUPPORTED, NULL, 0u);
            break;
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
            v2_send_activation_status(opcode, transaction_id); break;
        case INKTAG_CMD_SET_ACTIVATION_CODE:
            v2_set_activation_code(opcode, transaction_id,
                                   payload, payload_length); break;
        case INKTAG_CMD_GET_ACTIVATION_REQUEST:
            if (payload_length != 0u)
                send_v2_response(opcode, transaction_id,
                                 INKTAG_STATUS_INVALID_REQUEST, NULL, 0u);
            else
                v2_send_activation_request(
                    opcode, transaction_id,
                    (flags & INKTAG_REQUEST_FLAG_FORCE) != 0u);
            break;
        default:
            send_v2_response(opcode, transaction_id,
                             INKTAG_STATUS_UNKNOWN_COMMAND, NULL, 0u);
            break;
    }
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
        case INKTAG_CMD_SET_RESOURCE_ALIAS:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_RUN_TEST:
        case INKTAG_CMD_SYSTEM_ACTION:
            return true;
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
            return true;
        default:
            return false;
    }
}
