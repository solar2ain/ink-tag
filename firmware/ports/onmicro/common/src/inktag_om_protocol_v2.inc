/* Included by inktag_om_app.c so the protocol adapter can reuse the board's
 * private display, persistence and image-store state without exporting it. */

/* User-dispatched exact-board test access. Keep physical qualification facts
 * separate: implemented paths must be reachable for their device matrix.
 * Known-board/backend, Pro, voltage and transaction validation still apply.
 * This candidate does not qualify an unknown board or a release artifact. */
#ifndef INKTAG_OM_ENABLE_GUI_ASSET_QUALIFICATION
#define INKTAG_OM_ENABLE_GUI_ASSET_QUALIFICATION 1
#endif
#ifndef INKTAG_OM_ENABLE_PACKBITS_QUALIFICATION
#define INKTAG_OM_ENABLE_PACKBITS_QUALIFICATION 1
#endif

static void v2_find_owner_clear(void) {}
static void v2_find_owner_service(void) {}

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

static void v2_transfer_reset(bool erase_store) {
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE && erase_store) {
        image_store_abort();
    }
    if (s_v2_transfer.active &&
        (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
         s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)) {
        inktag_om_device_info_qr_abort();
        if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE)
            inktag_device_password_session_clear(&s_password);
    }
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE &&
        erase_store) {
        inktag_om_gui_asset_abort();
    }
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        epd_release_owned_panel();
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_picture_io_active = false;
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             false, s_status_now_ms);
    inktag_om_health_stage(0x47524944u, INKTAG_OM_HEALTH_OPERATION_BLE); /* GRID */
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
    if (s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background)) {
        return false;
    }
    /* A foreground request owns the display once a client is connected. It
     * may replace a queued boot/clock/slideshow refresh that has not started;
     * otherwise every reconnect can keep returning BUSY while the same
     * background restore is repeatedly deferred. */
    s_v2_work_opcode = opcode;
    s_v2_work_transaction = transaction_id;
    if (!schedule_epd_work_client(type, argument)) {
        s_v2_work_opcode = 0;
        s_v2_work_transaction = 0;
        return false;
    }
    s_epd_work_timestamp = draw_timestamp;
    uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY, 0};
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK, opcode, transaction_id,
                  progress, sizeof(progress));
    return true;
}

/* These exact contiguous wire groups have no base provider. Identity/status
 * 0x40 and ordinary password AUTH remain outside these groups. */
__attribute__((noinline)) static bool v2_base_unsupported_opcode(uint8_t opcode) {
    return opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO ||
           (unsigned)(opcode - INKTAG_CMD_GET_INPUT_CONFIG) <=
               INKTAG_CMD_SET_INPUT_CONFIG - INKTAG_CMD_GET_INPUT_CONFIG ||
           (unsigned)(opcode - INKTAG_CMD_GET_NFC_CAPABILITIES) <=
               INKTAG_CMD_SET_NFC_NDEF - INKTAG_CMD_GET_NFC_CAPABILITIES ||
           (unsigned)(opcode - INKTAG_CMD_GET_LAYOUT_CAPABILITIES) <=
               INKTAG_CMD_GET_LAYOUT_STATUS - INKTAG_CMD_GET_LAYOUT_CAPABILITIES ||
           (unsigned)(opcode - INKTAG_CMD_SET_ACTIVATION_CODE) <=
               INKTAG_CMD_AUTHORIZE_OWNER_ACTION - INKTAG_CMD_SET_ACTIVATION_CODE ||
           (unsigned)(opcode - INKTAG_CMD_GET_FIND_CAPABILITIES) <=
               INKTAG_CMD_ABORT_FIND_OWNER_ROTATION - INKTAG_CMD_GET_FIND_CAPABILITIES;
}

static bool v2_base_unsupported_request(const uint8_t *packet, uint16_t length) {
    if (v2_base_unsupported_opcode(packet[0])) return true;
    if (packet[0] == INKTAG_CMD_TRANSFER_BEGIN && length > 3u &&
        packet[3] == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) return true;
    /* Ordinary screen/LED PIN_CONFIG remains available under selector 2. */
    if (packet[0] == INKTAG_CMD_SET_IO_CONFIG)
        return length < 4u || packet[3] != 2u;
    return packet[0] == INKTAG_CMD_RUN_TEST && length > 3u &&
        (packet[3] == INKTAG_TEST_RESERVED_03 || packet[3] == INKTAG_TEST_INPUT_SIMULATE);
}

static bool v2_command_requires_idle_display(uint8_t opcode) {
    switch (opcode) {
        case INKTAG_CMD_SET_SCREEN_PROFILE:
        case INKTAG_CMD_SET_DISPLAY_MODE:
        case INKTAG_CMD_DISPLAY_ACTION:
        case INKTAG_CMD_TRANSFER_BEGIN:
        case INKTAG_CMD_DELETE_RESOURCE:
        case INKTAG_CMD_CLEAR_RESOURCES:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_RUN_TEST:
        case INKTAG_CMD_SYSTEM_ACTION:
            return true;
        default:
            return false;
    }
}

static bool v2_aux_io_available(void) {
#if INKTAG_OM_AUX_IO_BACKEND_AVAILABLE && INKTAG_BOARD_AUX_IO_QUALIFIED
    return inktag_om_aux_io_supported();
#else
    return false;
#endif
}

static bool v2_activation_available(void) {
    return INKTAG_OM_ACTIVATION_IMPLEMENTED != 0 &&
           INKTAG_OM_ACTIVATION_BACKEND_AVAILABLE != 0 &&
           inktag_om_activation_board_available() &&
           s_activation.supported;
}

static bool v2_password_available(void) {
    /* The selected QR store uses the real NVDS backend. Ordinary retained V
     * authentication needs its original immutable identity, not Pro admission.
     * A failed/corrupt storage read is reported separately by the handler. */
    return s_activation.identity_available;
}

static bool v2_indicator_available(void) {
#if defined(INKTAG_BOARD_MANAGED_STATUS_LED) && \
    (INKTAG_BOARD_MANAGED_STATUS_LED != 0)
    return status_led_available_colors() != 0u;
#else
    return false;
#endif
}

static bool v2_find_available(void) {
#if INKTAG_EXPERIMENTAL_FINDMY_ENABLED
    return INKTAG_OM_FIND_BACKEND_AVAILABLE != 0 &&
           (INKTAG_BOARD_FIND_QUALIFIED != 0 ||
            INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0);
#else
    return false;
#endif
}

static inktag_status_v2_t v2_find_configuration_access(void) {
    return product_access(
        INKTAG_PRODUCT_FEATURE_FIND_CONFIGURATION,
        INKTAG_OM_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_QUALIFIED != 0 ||
            INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0,
        true);
}

static bool v2_password_nonce(void *context, uint8_t *output, uint16_t n) {
    (void)context;
    if (!output || n != 16u) return false;
    for (uint16_t i = 0u; i < n; i += 4u) {
        uint32_t value = drv_rng_get();
        memcpy(output + i, &value, 4u);
    }
    return true; /* Common rejects an all-zero hardware nonce. */
}

static void v2_password_request(uint8_t opcode, uint8_t transaction_id,
    uint8_t flags, const uint8_t *payload, uint16_t length) {
    uint8_t response[INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES];
    uint16_t n = INKTAG_DEVICE_PASSWORD_STATUS_BYTES;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!v2_password_available())
        status = INKTAG_STATUS_UNSUPPORTED;
    else if (flags) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (!s_password_config.valid) status = INKTAG_STATUS_PERSISTENCE_ERROR;
    else if (opcode == INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE) {
        if (length != 1u || payload[0] > INKTAG_PASSWORD_DISABLE)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (payload[0] != INKTAG_PASSWORD_AUTH)
            status = INKTAG_STATUS_UNSUPPORTED;
        else if (!inktag_device_password_begin(&s_password, &s_password_config,
                s_status_now_ms, payload[0], v2_password_nonce, NULL, response))
            status = INKTAG_STATUS_DENIED;
        else n = INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES;
    } else if (opcode == INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD) {
        if (!inktag_device_password_authorize(&s_password, &s_password_config,
                s_status_now_ms, s_activation.identity.device_id, payload, length))
            status = INKTAG_STATUS_DENIED;
    } else if (length) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (opcode != INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS)
        status = INKTAG_STATUS_UNSUPPORTED;
    if (status == INKTAG_STATUS_OK && n == INKTAG_DEVICE_PASSWORD_STATUS_BYTES &&
        !inktag_device_password_status(&s_password, &s_password_config,
                                       s_status_now_ms, response))
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    send_v2_response(opcode, transaction_id, status,
        status == INKTAG_STATUS_OK ? response : NULL,
        status == INKTAG_STATUS_OK ? n : 0u);
}

static bool v2_gui_asset_update_available(void) {
    return false;
}

static bool v2_transfer_compression_available(void) {
    return INKTAG_OM_TRANSFER_PACKBITS_IMPLEMENTED != 0 &&
           INKTAG_OM_TRANSFER_PACKBITS_BACKEND_AVAILABLE != 0 &&
           (inktag_om_transfer_packbits_qualified() ||
            (INKTAG_OM_ENABLE_PACKBITS_QUALIFICATION != 0 &&
             inktag_om_board_identity() != INKTAG_OM_BOARD_UNKNOWN));
}

static uint32_t v2_capabilities(void) {
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_DEVICE_NAME |
                             INKTAG_CAPABILITY_DEVICE_LABEL |
                             INKTAG_CAPABILITY_DEVICE_INFO_QR |
                             INKTAG_CAPABILITY_TELEMETRY |
                            INKTAG_CAPABILITY_TEST |
                            INKTAG_CAPABILITY_HEALTH;
    bool aux_available = v2_aux_io_available();
    bool nfc_available = false;
#if INKTAG_OM_NFC_IMPLEMENTED
    nfc_available = INKTAG_BOARD_NFC_VARIANT_QUALIFIED != 0 &&
                    inktag_om_nfc_supported();
#endif
    const inktag_capability_gate_t activation_gate = {
        INKTAG_OM_ACTIVATION_IMPLEMENTED != 0,
        INKTAG_OM_ACTIVATION_BACKEND_AVAILABLE != 0,
        inktag_om_activation_board_available(),
        s_activation.supported,
    };
    const inktag_capability_gate_t lifecycle_gate = {
        INKTAG_OM_LIFECYCLE_IMPLEMENTED != 0,
        INKTAG_OM_LIFECYCLE_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        true,
    };
    const inktag_capability_gate_t input_gate = {
        true,
        INKTAG_OM_AUX_IO_BACKEND_AVAILABLE != 0 ||
            INKTAG_OM_NFC_IMPLEMENTED != 0,
        aux_available || nfc_available,
        aux_available || nfc_available,
    };
    const inktag_capability_gate_t io_gate = {
        true,
        INKTAG_OM_AUX_IO_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_AUX_IO_QUALIFIED != 0,
        aux_available &&
            product_access(INKTAG_PRODUCT_FEATURE_BUTTON_BINDING,
                           true, aux_available, true) == INKTAG_STATUS_OK,
    };
    const inktag_capability_gate_t nfc_gate = {
        INKTAG_OM_NFC_IMPLEMENTED != 0,
        INKTAG_OM_NFC_IMPLEMENTED != 0,
        INKTAG_BOARD_NFC_VARIANT_QUALIFIED != 0,
        nfc_available,
    };
    const inktag_capability_gate_t indicator_gate = {
        true,
        true,
#if defined(INKTAG_BOARD_MANAGED_STATUS_LED) && \
    (INKTAG_BOARD_MANAGED_STATUS_LED != 0)
        true,
#else
        false,
#endif
        v2_indicator_available(),
    };
    const inktag_capability_gate_t find_gate = {
        true,
        INKTAG_OM_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_QUALIFIED != 0,
        v2_find_available() &&
            v2_find_configuration_access() == INKTAG_STATUS_OK,
    };
    const inktag_capability_gate_t experimental_find_gate = {
        true,
        INKTAG_OM_FIND_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE != 0 &&
            INKTAG_BOARD_FIND_QUALIFIED == 0,
        v2_find_available() &&
            v2_find_configuration_access() == INKTAG_STATUS_OK,
    };
    const inktag_capability_gate_t gui_asset_gate = {
        INKTAG_OM_GUI_ASSET_UPDATE_IMPLEMENTED != 0,
        INKTAG_OM_GUI_ASSET_UPDATE_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_GUI_ASSET_UPDATE_QUALIFIED != 0 ||
            INKTAG_OM_ENABLE_GUI_ASSET_QUALIFICATION != 0,
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
#if defined(INKTAG_OM_NEXT_LAYOUT)
    /* Explicit development layout only; exact-board hardware gate is open. */
    if(inktag_om_pins_available())capabilities |= INKTAG_CAPABILITY_PIN_CONFIG;
#endif
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_NFC, &nfc_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_INDICATOR, &indicator_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_FIND, &find_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_EXPERIMENTAL_FIND,
        &experimental_find_gate);
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_GUI_ASSET_UPDATE,
        &gui_asset_gate);
#if INKTAG_OM_FULL_FEATURES
#if INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED
    const inktag_capability_gate_t sleep_gate = {
        true,
        INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE != 0,
        INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        product_access(INKTAG_PRODUCT_FEATURE_TIMED_SLEEP, true,
                       INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
                       INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED != 0) ==
            INKTAG_STATUS_OK,
    };
    capabilities = inktag_capability_add(
        capabilities, INKTAG_CAPABILITY_EXPERIMENTAL_TIMED_SLEEP, &sleep_gate);
#endif
#endif
    if (inktag_image_store_supported()) {
        capabilities |= INKTAG_CAPABILITY_RESOURCES | INKTAG_CAPABILITY_SLIDESHOW;
        capabilities |= INKTAG_CAPABILITY_IMAGE_METADATA;
    }
    if (v2_password_available())
        capabilities |= INKTAG_CAPABILITY_DEVICE_PASSWORD;
#if INKTAG_TRANSFER_PACKBITS_ENABLED && INKTAG_OM_IMAGE_CODEC_PROFILE != 2
    if (v2_transfer_compression_available()) {
        capabilities |= INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE;
    }
#endif
#if defined(INKTAG_ONMICRO_OTA)
    capabilities |= INKTAG_CAPABILITY_OTA_RECOVERY;
#endif
    /* Neither OM executable family has a qualified native-clock partial
     * backend. Controller recipes and Pro alone do not establish one. Keep
     * the global technical bit clear for every board/profile/font/pin state;
     * ordinary base full-refresh pages remain unchanged. */
    return capabilities & ~(uint32_t)INKTAG_CAPABILITY_CLOCK_PARTIAL_REFRESH;
}

static void v2_send_hello(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[6] = {1u, INKTAG_PROTOCOL_V2_VERSION, 0, 0, 0, 0};
    put_u16_le(&payload[2], INKTAG_OM_MAX_DATA_LEN);
    put_u16_le(&payload[4], INKTAG_OM_MAX_DATA_LEN - 7u);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static bool v2_ghost_clean_available(void) {
    /* OM's existing three-pass adapter calls clear(refresh), not an E6
     * black/white pigment writer. Do not advertise or queue a fake E6 cycle.
     * Ordinary E6 clear/native/image refresh keep their real shared backend. */
    return s_screen_config.driver_id != EPD_SCREEN_DRIVER_IST7513 &&
           s_screen_config.driver_id != EPD_SCREEN_DRIVER_IST7601;
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
        action_mask |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_PAUSE) |
                                  (1u << INKTAG_DISPLAY_ACTION_RESUME));
    }
    action_mask |= (uint16_t)((1u << INKTAG_DISPLAY_ACTION_NEXT) |
                              (1u << INKTAG_DISPLAY_ACTION_REFRESH) |
                              (1u << INKTAG_DISPLAY_ACTION_SLEEP));
    if (v2_ghost_clean_available())
        action_mask |= (uint16_t)(1u << INKTAG_DISPLAY_ACTION_GHOST_CLEAN);
    put_u16_le(&payload[5], mode_mask);
    put_u16_le(&payload[7], action_mask);
    put_u16_le(&payload[9], INKTAG_OM_MAX_DATA_LEN);
    put_u16_le(&payload[11], INKTAG_OM_MAX_DATA_LEN - 7u);
    payload[13] = EPD_DEVICE_NAME_MAX_BYTES;
    payload[14] = EPD_DEVICE_LABEL_MAX_BYTES;
    put_u16_le(&payload[15], inktag_image_store_sector_count());
    /* Bytes17..23 are reserved, not invented settings. OM has no qualified
     * GPIO snapshot backend: its extension stays zero in normal firmware. */
    put_u32_le(&payload[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
               INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION
#if INKTAG_OM_IMAGE_CODEC_PROFILE != 0
               | (v2_transfer_compression_available() ?
                    INKTAG_CAPABILITY_EXTENSION_TRANSFER_LZO1X_2000 : 0u)
#endif
               );
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_gui_asset_info(uint8_t opcode,
                                   uint8_t transaction_id) {
    inktag_om_gui_asset_info_t info;
    uint8_t response[INKTAG_GUI_ROLE_INFO_SIZE] = {0};
    if (!v2_gui_asset_update_available() ||
        !inktag_om_gui_asset_info(&info)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    inktag_gui_role_info_t native = {
        .base = {INKTAG_RELEASE_FONT_INFO_VERSION, info.schema, info.active_bank, 1u, info.generation,
                 info.total_size, INKTAG_OM_GUI_ASSET_BANK_SIZE, info.package_crc32},
        .supported_schemas = INKTAG_RELEASE_FONT_SCHEMA_MASK,
        .role_version = INKTAG_RELEASE_FONT_ROLE_VERSION, .flags = INKTAG_RELEASE_FONT_FLAGS,
    };
    if (!inktag_gui_role_encode_info(&native, response, sizeof(response))) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_device_info(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[112] = {0};
    const char *board_id = inktag_om_board_id();
    uint8_t board_length = (uint8_t)strlen(board_id);
    uint8_t name_length = s_device_settings.name_length > 0u ?
                          s_device_settings.name_length : (uint8_t)strlen(s_device_name);
    const uint8_t *name = s_device_settings.name_length > 0u ?
                          s_device_settings.name : (const uint8_t *)s_device_name;
    char label[EPD_DEVICE_LABEL_MAX_BYTES + 1u];
    if (!effective_device_display_name(label)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    uint8_t label_length = (uint8_t)strlen(label);
    inktag_om_gap_address_t address;
    bool address_valid = inktag_om_gap_addr_get(&address);
    uint32_t app_version = ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
                           ((uint32_t)INKTAG_VERSION_MINOR << 8) |
                           INKTAG_VERSION_PATCH;
    uint32_t app_size =
        (uint32_t)((uintptr_t)&__inktag_image_end__ - INKTAG_OM_APP_XIP_ADDRESS);
    uint32_t app_checksum = 0;
    inktag_swap_state_t swap_state = INKTAG_SWAP_STATE_NONE;
    uint32_t swap_generation = 0u;
    inktag_swap_result_t swap_status =
        inktag_om_swap_status(&swap_state, &swap_generation);
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
    /* Optional schema-1 OTA attestation tail. Older V2 clients stop at the
     * three length-delimited strings; OTA clients use this bounded extension
     * instead of reviving the removed legacy INIT/build-text command. */
    payload[offset++] = 1u;
    payload[offset++] = (uint8_t)swap_status;
    payload[offset++] = (uint8_t)swap_state;
    payload[offset++] = 0u;
    put_u32_le(&payload[offset], INKTAG_OTA_VERSION);
    offset += 4u;
    put_u32_le(&payload[offset], swap_generation);
    offset += 4u;
    put_u32_le(&payload[offset], s_startup_swap_confirm_status);
    offset += 4u;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, offset);
}

static void v2_send_status(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *request, uint16_t request_length) {
#if defined(INKTAG_OM_DIAGNOSTICS)
    if (request_length == 5u && request != NULL && request[0] == 'F') {
        uint32_t offset = read_u32_le(&request[1]);
        uint8_t raw[225] = {0xF1u};
        uint16_t requested = (uint16_t)(sizeof(raw) - 5u);
        if (offset >= INKTAG_OM_EXTERNAL_FLASH_CAPACITY) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (requested > INKTAG_OM_EXTERNAL_FLASH_CAPACITY - offset) {
            requested = (uint16_t)(INKTAG_OM_EXTERNAL_FLASH_CAPACITY - offset);
        }
        put_u32_le(&raw[1], offset);
        if (inktag_om_external_flash_read(offset, &raw[5], requested) != requested) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_IO_ERROR);
            return;
        }
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         raw, (uint16_t)(5u + requested));
        return;
    }
#else
    (void)request;
#endif
    if (request_length != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    uint8_t payload[37u + INKTAG_TEMPERATURE_WIRE_SIZE] = {0};
    inktag_om_external_flash_probe_t external;
    uint16_t voltage = EPD_ReadVoltage();
    int8_t temperature = 0;
    om_temperature_sources_t temperature_sources = read_temperature_sources();
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
    if (inktag_om_external_flash_probe(&external)) {
        payload[16] = 0xF1u;
        payload[17] = (external.board_candidate ? 0x01u : 0u) |
                      (external.matched ? 0x02u : 0u);
        payload[18] = (uint8_t)(external.jedec_id >> 16);
        payload[19] = (uint8_t)(external.jedec_id >> 8);
        payload[20] = (uint8_t)external.jedec_id;
        put_u32_le(&payload[21], external.matched ?
                   INKTAG_OM_EXTERNAL_FLASH_CAPACITY : 0u);
        memcpy(&payload[25], external.sfdp, 8u);
        payload[33] = external.miso_pin;
        payload[34] = external.mosi_pin;
        payload[35] = external.cs_pin;
        payload[36] = external.sclk_pin;
        inktag_temperature_wire_encode(&payload[37],
            temperature_sources.mcu_die.valid,
            temperature_sources.mcu_die.value_sixteenths,
            temperature_sources.panel_controller.valid,
            temperature_sources.panel_controller.value_sixteenths);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         payload, sizeof(payload));
        return;
    }
    inktag_temperature_wire_encode(&payload[16],
        temperature_sources.mcu_die.valid,
        temperature_sources.mcu_die.value_sixteenths,
        temperature_sources.panel_controller.valid,
        temperature_sources.panel_controller.value_sixteenths);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     payload, 16u + INKTAG_TEMPERATURE_WIRE_SIZE);
}

static void v2_send_health(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *request, uint16_t request_length) {
    if (request_length > 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    uint8_t record_index = request_length == 1u ? request[0] : 0u;
    uint8_t payload[INKTAG_OM_HEALTH_WIRE_SIZE +
                    INKTAG_SOAK_TELEMETRY_WIRE_SIZE];
    uint16_t length = inktag_om_health_encode(record_index, payload,
                                               sizeof(payload));
    if (length == 0u || (record_index != 0u && record_index >= payload[20])) {
        send_v2_status(opcode, transaction_id, length == 0u ? INKTAG_STATUS_INTERNAL_ERROR :
                                        INKTAG_STATUS_NOT_FOUND);
        return;
    }
    length += inktag_soak_telemetry_encode(
        &payload[length], (uint16_t)(sizeof(payload) - length),
        s_status_now_ms);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     payload, length);
}

static void v2_encode_time(uint8_t payload[7]) {
    uint32_t utc_timestamp = timestamp() - (int32_t)s_timezone_minutes * 60;
    put_u32_le(payload, utc_timestamp);
    put_u16_le(&payload[4], (uint16_t)s_timezone_minutes);
    payload[6] = s_config.week_start;
}

static void v2_send_time(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[7];
    v2_encode_time(payload);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static bool v2_test_time_mode(uint8_t mode) {
    return mode == INKTAG_DISPLAY_MODE_CALENDAR ||
           mode == INKTAG_DISPLAY_MODE_CLOCK;
}

static void v2_list_tests(uint8_t opcode, uint8_t transaction_id) {
#if INKTAG_OM_FULL_FEATURES
    uint8_t response[9] = {1u, 0u};
    uint8_t count = 0u;
    if (runtime_led_diag_available()) response[2u + count++] = INKTAG_TEST_LED_CHANNEL;
    if (runtime_led_diag_available())
        response[2u + count++] = INKTAG_TEST_DEVICE_LOCATE;
    response[2u + count++] = INKTAG_TEST_TIME_SHIFT;
    response[2u + count++] = INKTAG_TEST_TIME_SET;
    response[1] = count;
#else
    const uint8_t response[] = {
        1u, 2u, INKTAG_TEST_TIME_SHIFT, INKTAG_TEST_TIME_SET,
    };
#endif
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, (uint16_t)(2u + response[1]));
}

static void v2_run_test(uint8_t opcode, uint8_t transaction_id,
                        const uint8_t *payload, uint16_t length) {
#if INKTAG_OM_FULL_FEATURES
    if (length >= 1u && payload[0] == INKTAG_TEST_NFC_CHANNEL_SNAPSHOT) {
        send_v2_status(opcode, transaction_id, length == 1u ?
            INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (length >= 1u && payload[0] == INKTAG_TEST_LED_CHANNEL) {
        bool accepted = length == 2u && diag_set_led_mask(payload[1]);
        send_v2_status(opcode, transaction_id, accepted ? INKTAG_STATUS_OK : INKTAG_STATUS_INVALID_REQUEST);
        if (accepted) {
            send_v2_event(INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK,
                          opcode, transaction_id, &payload[1], 1u);
        }
        return;
    }
    if (length >= 1u && payload[0] == INKTAG_TEST_RESERVED_03) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
}
    if (length >= 1u && payload[0] == INKTAG_TEST_INPUT_SIMULATE) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
}
    if (length >= 1u && payload[0] == INKTAG_TEST_DEVICE_LOCATE) {
        bool led_available = runtime_led_diag_available();
        bool accepted;
        if (length != 2u || payload[1] > 1u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (!led_available) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        if (payload[1] == 0u) {
            inktag_status_locate_stop(&s_status, s_status_now_ms);
            status_timer_update();
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
            return;
        }
        inktag_status_locate_start(&s_status, s_status_now_ms);
        accepted = led_available;

        if (!accepted)
            inktag_status_locate_stop(&s_status, s_status_now_ms);
        status_timer_update();
        send_v2_status(opcode, transaction_id, accepted ? INKTAG_STATUS_OK :
                                    INKTAG_STATUS_INVALID_STATE);
        return;
    }
#endif
    if (length < 2u || !v2_test_time_mode(payload[1])) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (s_epd == NULL) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
        return;
    }
    if (s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) ||
        s_v2_transfer.active) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }

    uint32_t next_timestamp = timestamp();
    int16_t next_timezone = s_timezone_minutes;
    uint8_t next_week_start = s_config.week_start;
    if (payload[0] == INKTAG_TEST_TIME_SHIFT) {
        if (length != 6u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        int32_t delta_seconds = (int32_t)read_u32_le(&payload[2]);
        int64_t candidate = (int64_t)s_timestamp + delta_seconds;
        if (delta_seconds == 0 || delta_seconds < -(366 * 86400) ||
            delta_seconds > 366 * 86400 || candidate < 946684800ll ||
            candidate > 4102444799ll) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        next_timestamp = (uint32_t)candidate;
    } else if (payload[0] == INKTAG_TEST_TIME_SET) {
        if (length != 9u || payload[8] > 6u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        uint32_t utc_timestamp = read_u32_le(&payload[2]);
        next_timezone = (int16_t)read_u16_le(&payload[6]);
        int64_t candidate = (int64_t)utc_timestamp + (int32_t)next_timezone * 60;
        if (next_timezone < -840 || next_timezone > 840 ||
            candidate < 946684800ll || candidate > 4102444799ll) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        next_timestamp = (uint32_t)candidate;
        next_week_start = payload[8];
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }

    uint32_t previous_timestamp = s_timestamp;
    inktag_elapsed_clock_t previous_clock = s_elapsed_clock;
    int16_t previous_timezone = s_timezone_minutes;
    uint8_t previous_week_start = s_config.week_start;
    bool previous_time_valid = s_time_valid;
    set_timestamp(next_timestamp);
    s_timezone_minutes = next_timezone;
    s_config.week_start = next_week_start;
    s_time_valid = true;
    if (!persist_time_snapshot()) {
        s_timestamp = previous_timestamp;
        s_elapsed_clock = previous_clock;
        s_timezone_minutes = previous_timezone;
        s_config.week_start = previous_week_start;
        s_time_valid = previous_time_valid;
        wall_clock_update();
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    if (payload[0] == INKTAG_TEST_TIME_SET) epd_config_write(&s_config);

    uint8_t gui_mode = payload[1] == INKTAG_DISPLAY_MODE_CALENDAR ?
                       MODE_CALENDAR : MODE_CLOCK;
    if (!v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                          gui_mode, s_timestamp)) {
        /* A background timer can claim the display after the initial BUSY
         * guard but before the foreground work is queued. RUN_TEST must remain
         * transactional: a rejected request must not advance the device clock
         * or persist a new week-start value without the matching refresh. */
        s_timestamp = previous_timestamp;
        s_elapsed_clock = previous_clock;
        s_timezone_minutes = previous_timezone;
        s_config.week_start = previous_week_start;
        s_time_valid = previous_time_valid;
        bool restored = persist_time_snapshot();
        if (payload[0] == INKTAG_TEST_TIME_SET) epd_config_write(&s_config);
        send_v2_status(opcode, transaction_id, restored ? INKTAG_STATUS_BUSY :
                                    INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    uint8_t response[7];
    v2_encode_time(response);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

#if INKTAG_OM_NFC_PACKAGED

#endif

#if INKTAG_OM_FULL_FEATURES
static uint16_t v2_button_action_mask(void) {
    if (product_access(INKTAG_PRODUCT_FEATURE_BUTTON_BINDING, true,
                       v2_aux_io_available(), true) != INKTAG_STATUS_OK)
        return 0u;
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

static void v2_get_input_config(uint8_t opcode, uint8_t transaction_id) {
    bool aux_available = v2_aux_io_available();
    uint8_t response[14] = {1u};
    if (!aux_available) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (aux_available && v2_button_action_mask() != 0u) {
        response[1] = 0x07u;
        put_u16_le(&response[2], v2_button_action_mask());
        response[4] = s_device_settings.button.single.action;
        response[5] = s_device_settings.button.single.slot_id;
        response[6] = s_device_settings.button.double_click.action;
        response[7] = s_device_settings.button.double_click.slot_id;
        response[8] = s_device_settings.button.long_press.action;
        response[9] = s_device_settings.button.long_press.slot_id;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

/* Keep this record transaction out of the packet dispatcher. */
static void v2_set_input_config(uint8_t opcode, uint8_t transaction_id,
                                const uint8_t *payload, uint16_t length)
    __attribute__((noinline));
static void v2_set_input_config(uint8_t opcode, uint8_t transaction_id,
                                const uint8_t *payload, uint16_t length) {
    (void)payload; (void)length; send_v2_status(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED);
}

static void v2_pin_config(uint8_t opcode,uint8_t transaction_id,const uint8_t *payload,
                          uint16_t length,bool write) {
    uint8_t response[36];const char *error=NULL;
#if !defined(INKTAG_OM_NEXT_LAYOUT)
    (void)payload;(void)length;(void)write;(void)response;(void)error;
    send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#else
    if(!inktag_om_pins_info(response)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);return;
    }
    if((!write && length!=1u) || (write &&
       !((length==2u && payload[1]==0u) || (length==18u && payload[1]==1u)))) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);return;
    }
    if(write) {
        if(s_epd_work_active || s_epd_work_pending || s_v2_transfer.active || s_slideshow_active ||
           s_led_test_active || inktag_status_locate_active(&s_status) ||
           inktag_om_device_settings_io_busy()) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);return;
        }
        /* Dispatch above already enforces AUTH, lifecycle lock and fresh power;
         * backend guards voltage again at each real SDK write/GC point. */
        if(!inktag_om_device_settings_set_pins(payload[1]!=0u,payload+2,&error)) {
            send_v2_response(opcode,transaction_id,error && !strcmp(error,"flash")?
                INKTAG_STATUS_PERSISTENCE_ERROR:INKTAG_STATUS_INVALID_REQUEST,NULL,0u);return;
        }
        /* The setter parked the old mapping before the durable commit. */
        (void)pins_apply_to_config();status_led_update_available();
        if(!inktag_om_pins_info(response)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);return;
        }
    }
    send_v2_response(opcode,transaction_id,INKTAG_STATUS_OK,response,sizeof(response));
#endif
}

static void v2_get_io_config(uint8_t opcode, uint8_t transaction_id) {
    uint8_t response[INKTAG_AUX_IO_WIRE_SIZE] = {1u};
    /* PIN_CONFIG clients need the stored conflict snapshot even without a
     * qualified AUX actuator. Readback grants no GPIO or AUX SET capability. */
    if (!v2_aux_io_available() && !inktag_om_pins_available()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    inktag_status_v2_t access = product_access(
        INKTAG_PRODUCT_FEATURE_BASIC, true, true, true);
    if (access != INKTAG_STATUS_OK) {
        send_v2_status(opcode, transaction_id, access);
        return;
    }
    inktag_aux_io_t next;
    if(!inktag_om_device_settings_get_aux3(&next) ||
       !inktag_aux_io_encode(&next,response,sizeof(response))) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_set_io_config(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    if(length && payload[0]==2u) {v2_pin_config(opcode,transaction_id,payload,length,true);return;}
    send_v2_status(opcode,transaction_id,INKTAG_STATUS_UNSUPPORTED);
}
#endif

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


static void v2_send_identity_settings(uint8_t opcode, uint8_t transaction_id) {
    uint8_t payload[64] = {0};
    uint8_t name_length = s_device_settings.name_length > 0u ?
                          s_device_settings.name_length : (uint8_t)strlen(s_device_name);
    const uint8_t *name = s_device_settings.name_length > 0u ?
                          s_device_settings.name : (const uint8_t *)s_device_name;
    char label[EPD_DEVICE_LABEL_MAX_BYTES + 1u];
    if (!effective_device_display_name(label)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    uint8_t label_length = (uint8_t)strlen(label);
    inktag_om_gap_address_t address;
    bool address_valid = inktag_om_gap_addr_get(&address);
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
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    uint32_t frame_bytes = image_store_expected_payload(model);
    inktag_image_store_status_t status;
    inktag_image_store_result_t result = image_store_visible_status(
        &s_screen_config, frame_bytes, &status);
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
    /* Optional extension understood by new clients. Older clients consume
     * the original 30-byte prefix and derive a conservative fallback. */
    uint16_t allocatable = status.required_sectors
        ? status.free_sectors / status.required_sectors : 0u;
    put_u16_le(&payload[30], allocatable);
    put_u16_le(&payload[32], inktag_image_store_fixed_slot_count());
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_list_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    if (length > 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    uint8_t after_id = length > 0u ? payload[0] : UINT8_MAX;
    uint8_t record_id;
    inktag_image_store_header_t header;
    inktag_image_store_result_t result = image_store_next_visible(
        after_id, &record_id, &header);
    if (result != INKTAG_IMAGE_STORE_OK && result != INKTAG_IMAGE_STORE_NOT_FOUND) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0u);
        return;
    }
    if (result == INKTAG_IMAGE_STORE_OK) {
        epd_screen_config_t stored;
        bool decoded = epd_screen_config_decode(header.screen_config, EPD_SCREEN_WIRE_SIZE,
                                                &stored) == EPD_SCREEN_STATUS_OK;
        uint8_t name_length = 0u;
        while (name_length < INKTAG_IMAGE_STORE_NAME_SIZE && header.name[name_length] != '\0') {
            name_length++;
        }
        const uint8_t *name = (const uint8_t *)header.name;
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
        memcpy(&response[19], name, name_length);
        response[19u + name_length] =
            inktag_image_reading_rotation(header.black_mask);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)(20u + name_length));
        return;
    }
    uint8_t done[34] = {1u, UINT8_MAX};
    uint8_t bitmap_bytes = (uint8_t)((inktag_image_store_fixed_slot_count() + 7u) / 8u);
    result = inktag_image_store_occupancy(done + 2u, sizeof(done) - 2u);
    send_v2_response(opcode, transaction_id, v2_store_status(result),
        result == INKTAG_IMAGE_STORE_OK ? done : NULL,
        result == INKTAG_IMAGE_STORE_OK ? (uint16_t)(2u + bitmap_bytes) : 0u);
}

static void v2_read_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length) {
    if (length != 6u || payload[5] == 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    inktag_image_store_header_t header;
    inktag_image_store_result_t result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_response(opcode, transaction_id, v2_store_status(result), NULL, 0);
        return;
    }
    if (!image_store_record_visible(payload[0])) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
        return;
    }
    uint32_t offset = read_u32_le(&payload[1]);
    uint16_t requested = payload[5];
    if (requested > INKTAG_OM_MAX_DATA_LEN - 12u) requested = INKTAG_OM_MAX_DATA_LEN - 12u;
    if (offset >= header.payload_size) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (offset + requested > header.payload_size) {
        requested = (uint16_t)(header.payload_size - offset);
    }
    uint8_t response[INKTAG_OM_MAX_DATA_LEN - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    response[0] = payload[0];
    put_u32_le(&response[1], offset);
    put_u32_le(&response[5], header.payload_size);
    result = inktag_image_store_read(payload[0], offset, &response[9], requested);
    send_v2_response(opcode, transaction_id, v2_store_status(result),
                     result == INKTAG_IMAGE_STORE_OK ? response : NULL,
                     result == INKTAG_IMAGE_STORE_OK ? (uint16_t)(9u + requested) : 0u);
}

static void v2_indicator_config(uint8_t opcode, uint8_t transaction_id,
                                bool write, const uint8_t *payload,
                                uint16_t length) {
    uint8_t response[INKTAG_STATUS_WIRE_SIZE];
    inktag_status_config_t config;
    if (!v2_indicator_available()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (write) {
        const char *error = NULL;
        inktag_status_v2_t access;
        if (!inktag_status_config_decode(&config, payload, length)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        access = inktag_access_indicator_policy(&s_activation, config.policy);
        if (access != INKTAG_STATUS_OK) {
            send_v2_status(opcode, transaction_id, access);
            return;
        }
        if (!inktag_om_device_settings_set_led(&config, &error)) {
            send_v2_response(opcode, transaction_id,
                             error && strcmp(error, "value") == 0 ?
                                 INKTAG_STATUS_INVALID_REQUEST :
                                 INKTAG_STATUS_PERSISTENCE_ERROR,
                             NULL, 0u);
            return;
        }
        inktag_status_set_config(&s_status, &config, s_status_now_ms);
    } else if (length != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    } else {
        inktag_om_device_settings_get_led(&config);
    }
    if (!inktag_status_config_encode(&config, response)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_transfer_begin(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    inktag_transfer_descriptor_t descriptor;
    inktag_status_v2_t descriptor_status =
        inktag_transfer_descriptor_decode(payload, length, &descriptor);
    if (descriptor_status != INKTAG_STATUS_OK || s_v2_transfer.active ||
        s_epd_work_active || s_epd_work_pending) {
        send_v2_status(opcode, transaction_id, s_v2_transfer.active || s_epd_work_active || s_epd_work_pending ?
                         INKTAG_STATUS_BUSY : descriptor_status);
        return;
    }
    uint8_t object_type = descriptor.object_type;
    if (object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    bool qr_transfer = object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR;
    bool password_transfer = object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE;
    if (password_transfer) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }

    /* Both use the one existing QR staging area; no panel/image semantics. */
    bool qr_object = qr_transfer || password_transfer;
    bool gui_asset_transfer =
        object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE;
    bool image_object = object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
                        object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE;
    uint8_t codec_flags = (uint8_t)(descriptor.black_mask &
        (INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE |
         (image_object ? INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X : 0u)));
    bool compressed_transfer = codec_flags != 0u;
    uint8_t decoded_black_mask = (uint8_t)(descriptor.black_mask & ~codec_flags);
    if (compressed_transfer &&
        (qr_object || gui_asset_transfer ||
         !v2_transfer_compression_available()
#if !INKTAG_TRANSFER_PACKBITS_ENABLED || INKTAG_OM_IMAGE_CODEC_PROFILE == 2
         || (codec_flags & INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE)
#endif
#if INKTAG_OM_IMAGE_CODEC_PROFILE == 0
         || (codec_flags & INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X)
#endif
        )) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    inktag_power_operation_t power_operation =
        object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE || qr_object ||
        gui_asset_transfer ?
            INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY;
    if (!inktag_om_health_operation_allowed(power_operation, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    uint8_t segment_count = descriptor.segment_count;
    uint32_t total_size = descriptor.total_size;
    uint32_t segment_size[2] = {
        descriptor.segment_size[0], descriptor.segment_size[1]
    };
    epd_model_t *model = qr_object || gui_asset_transfer ? NULL :
                         image_store_model();
    if (qr_transfer &&
        (length != 20u || !inktag_device_info_qr_transfer_valid(&descriptor))) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (qr_transfer && s_password_config.enabled &&
        descriptor.total_size + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES >
            INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_NO_SPACE);
        return;
    }
    if (gui_asset_transfer &&
        (length != 20u || !v2_gui_asset_update_available())) {
        send_v2_status(opcode, transaction_id, length != 20u ? INKTAG_STATUS_INVALID_REQUEST :
                                         INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (gui_asset_transfer && !inktag_om_gui_asset_descriptor_valid(&descriptor)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
        return;
    }
    if (gui_asset_transfer && !inktag_access_is_pro(&s_activation)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
        return;
    }
    if (!qr_object && !gui_asset_transfer &&
        (!model || (object_type != INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
                    object_type != INKTAG_TRANSFER_OBJECT_STORED_IMAGE))) {
        send_v2_status(opcode, transaction_id, model ? INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_INCOMPATIBLE);
        return;
    }
    uint8_t expected_segments = qr_object || gui_asset_transfer ? 1u :
        ((model->transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP ||
          model->transfer_format == EPD_SCREEN_TRANSFER_PLANAR_2X1BPP ||
          model->transfer_format == EPD_SCREEN_TRANSFER_INDEXED_2X2BPP) ? 2u : 1u);
    bool invalid_e6_segments = false;
    if (!qr_object && !gui_asset_transfer &&
        (payload[1] != model->transfer_format || segment_count != expected_segments ||
        segment_count == 0u || segment_count > 2u ||
        segment_size[0] + (segment_count > 1u ? segment_size[1] : 0u) != total_size ||
        total_size != image_store_expected_payload(model) || invalid_e6_segments)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_v2_transfer.active = true;
    s_v2_transfer.object_type = object_type;
    s_v2_transfer.transaction_id = transaction_id;
    s_v2_transfer.format = payload[1];
    s_v2_transfer.segment_count = segment_count;
    s_v2_transfer.black_mask = payload[3];
    s_v2_transfer.resource_id = UINT8_MAX;
    s_v2_transfer.total_size = total_size;
    s_v2_transfer.expected_crc = descriptor.expected_crc;
    s_v2_transfer.running_crc = 0xFFFFFFFFu;
    s_v2_transfer.segment_size[0] = segment_size[0];
    s_v2_transfer.segment_size[1] = segment_size[1];
    s_v2_transfer.last_activity_ms = s_status_now_ms;
    inktag_om_health_stage((object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
                               qr_object || gui_asset_transfer) ?
                               0x47525354u : 0x47525458u,
                           (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
                               qr_object || gui_asset_transfer) ?
                               INKTAG_OM_HEALTH_OPERATION_STORAGE :
                               INKTAG_OM_HEALTH_OPERATION_TRANSFER); /* GRST/GRTX */
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    uint8_t response[5] = {object_type, UINT8_MAX, 0, 0, 0};
    if (object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        slideshow_stop(false);
        s_picture_io_active = true;
        epd_gpio_open_if_needed();
        s_epd = epd_init(&s_screen_config);
        if (!s_epd) status = INKTAG_STATUS_INCOMPATIBLE;
         else epd_panel_initialize();
    } else if (qr_transfer) {
        const char *error = NULL;
        response[1] = descriptor.black_mask;
        if (!inktag_om_device_info_qr_begin(
                descriptor.black_mask, descriptor.format,
                (uint16_t)descriptor.total_size, descriptor.expected_crc,
                &error)) {
            status = INKTAG_STATUS_INVALID_REQUEST;
        }
    } else {
        inktag_stored_image_v5_descriptor_t stored;
        status = inktag_stored_image_v5_descriptor_decode(payload, length, &stored);
        if (status == INKTAG_STATUS_OK) {
            epd_screen_config_t requested_screen;
            if (epd_screen_config_decode(stored.screen_profile,
                    EPD_SCREEN_WIRE_SIZE, &requested_screen) != EPD_SCREEN_STATUS_OK ||
                !screen_config_matches(stored.screen_profile)) {
                status = INKTAG_STATUS_INCOMPATIBLE;
            } else {
                status = image_store_create_access(&requested_screen, total_size,
                                                    stored.replace_slot);
                if (status == INKTAG_STATUS_OK) {
                    uint8_t id = UINT8_MAX;
                    if (stored.replace_slot != UINT8_MAX &&
                        !inktag_image_store_replace_slot(stored.replace_slot)) {
                        status = INKTAG_STATUS_BUSY;
                    } else {
                        status = v2_store_status(image_store_finish(inktag_image_store_begin(
                            &requested_screen, stored.transfer.format, segment_count,
                            decoded_black_mask, stored.content_type, segment_size,
                            total_size, s_v2_transfer.expected_crc,
                            s_timestamp, stored.name, &id),
                            INKTAG_IMAGE_STORE_EVENT_BEGIN, NULL));
                    }
                    s_v2_transfer.resource_id = id;
                    response[1] = id;
                }
            }
        }
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    /* A stored-image upload is still a user-visible transfer even when the
     * client requests store-only and no panel refresh follows. Keep the same
     * blue transfer scene as a direct-frame upload until commit, abort,
     * timeout, disconnect or an I/O failure resets the transaction. */
    inktag_status_set_active(&s_status, INKTAG_STATUS_SCENE_TRANSFER,
                             true, s_status_now_ms);
    put_u16_le(&response[2], INKTAG_OM_MAX_DATA_LEN - 7u);
    response[4] = segment_count;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

typedef struct {
    uint32_t offset;
    inktag_status_v2_t status;
} v2_transfer_sink_context_t;

static bool v2_transfer_accept_decoded(void *opaque, uint8_t *bytes,
                                       uint16_t data_length) {
    v2_transfer_sink_context_t *context =
        (v2_transfer_sink_context_t *)opaque;
    if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        context->status = v2_store_status(
            inktag_image_store_append(bytes, data_length));
    } else if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        const char *error = NULL;
        if (!inktag_om_device_info_qr_write(
                (uint8_t)(s_v2_transfer.black_mask &
                    ~INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE),
                (uint16_t)context->offset, bytes, data_length, &error)) {
            context->status = INKTAG_STATUS_INVALID_REQUEST;
        }
    } else if (!s_epd) {
        context->status = INKTAG_STATUS_INVALID_STATE;
    } else {
        uint16_t consumed = 0u;
        while (consumed < data_length) {
            uint32_t absolute = context->offset + consumed;
            uint8_t segment = absolute < s_v2_transfer.segment_size[0] ?
                0u : 1u;
            uint32_t segment_start = segment == 0u ?
                0u : s_v2_transfer.segment_size[0];
            uint32_t segment_end = segment_start +
                s_v2_transfer.segment_size[segment];
            uint16_t chunk = (uint16_t)(data_length - consumed);
            bool black = (s_v2_transfer.black_mask &
                          (1u << segment)) != 0u;
            bool begin = absolute == segment_start;
            if (absolute + chunk > segment_end) {
                chunk = (uint16_t)(segment_end - absolute);
            }
            if (chunk > sizeof(s_v2_brand_chunk)) chunk = sizeof(s_v2_brand_chunk);
            memcpy(s_v2_brand_chunk, &bytes[consumed], chunk);
            if (!inktag_om_brand_apply_oriented(
                    &s_screen_config, &s_activation,
                    display_state_flag(EPD_DISPLAY_STATE_FLAG_BRAND_HIDDEN),
                    s_v2_transfer.format, segment,
                    absolute - segment_start, s_v2_brand_chunk, chunk,
                    inktag_image_reading_rotation(s_v2_transfer.black_mask))) {
                context->status = INKTAG_STATUS_INTERNAL_ERROR;
                break;
            }
            if (!EPD_DriverWriteRam(s_epd, begin, black,
                                    s_v2_brand_chunk, chunk)) {
                context->status = INKTAG_STATUS_INTERNAL_ERROR;
                break;
            }
            consumed = (uint16_t)(consumed + chunk);
        }
    }
    if (context->status != INKTAG_STATUS_OK) return false;
    s_v2_transfer.running_crc = inktag_crc32_update(
        s_v2_transfer.running_crc, bytes, data_length);
    context->offset += data_length;
    return true;
}

static void v2_transfer_write(uint8_t opcode, uint8_t transaction_id,
                              const uint8_t *payload, uint16_t length) {
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
        return;
    }
    inktag_power_operation_t power_operation =
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE ||
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_GUI_ASSET_PACKAGE ?
            INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY;
    if (!inktag_om_health_operation_allowed(power_operation, false)) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (length <= 4u || read_u32_le(payload) != s_v2_transfer.written) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    bool compressed = (s_v2_transfer.black_mask &
        INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u;
#if INKTAG_OM_IMAGE_CODEC_PROFILE != 0
    bool lzo = (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
                s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) &&
        (s_v2_transfer.black_mask & INKTAG_TRANSFER_DESCRIPTOR_FLAG_LZO1X) != 0u;
    compressed = compressed || lzo;
#endif
    uint16_t data_length = compressed && length > 6u ?
        read_u16_le(&payload[4]) : (uint16_t)(length - 4u);
    if (s_v2_transfer.written + data_length > s_v2_transfer.total_size) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    v2_transfer_sink_context_t context = {
        .offset = s_v2_transfer.written,
        .status = INKTAG_STATUS_OK,
    };
    inktag_status_v2_t status = INKTAG_STATUS_OK;
#if INKTAG_OM_IMAGE_CODEC_PROFILE != 0
    if (lzo) {
        size_t produced;
        /* Exclusive image transaction borrows the existing render workspace.
         * Validate the complete block before any Flash/panel sink mutation. */
        if (length <= 6u || !data_length || data_length > 2000u ||
            data_length > sizeof(s_gui_frame_buffer) ||
            !inktag_lzo1x_decode(payload+6u,length-6u,s_gui_frame_buffer,
                                data_length,&produced) || produced != data_length)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if (!v2_transfer_accept_decoded(&context,s_gui_frame_buffer,data_length))
            status = context.status;
    } else
#endif
#if INKTAG_TRANSFER_PACKBITS_ENABLED && INKTAG_OM_IMAGE_CODEC_PROFILE != 2
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
    if (!v2_transfer_accept_decoded(
                   &context, (uint8_t *)&payload[4], data_length)) {
        status = context.status;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    s_v2_transfer.written += data_length;
    s_v2_transfer.last_activity_ms = s_status_now_ms;
    uint8_t response[4];
    put_u32_le(response, s_v2_transfer.written);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_transfer_commit(uint8_t opcode, uint8_t transaction_id) {
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id ||
        s_v2_transfer.written != s_v2_transfer.total_size) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
        return;
    }
    uint32_t crc = s_v2_transfer.running_crc ^ 0xFFFFFFFFu;
    if (crc != s_v2_transfer.expected_crc) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_CHECKSUM_ERROR);
        return;
    }
    uint8_t object_type = s_v2_transfer.object_type;
    uint8_t response[1] = {UINT8_MAX};
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_PASSWORD_BUNDLE) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (object_type == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR) {
        const char *error = NULL;
        uint8_t slot = s_v2_transfer.black_mask;
        if (!inktag_om_device_info_qr_commit(slot, &s_password_config, &error)) {
            inktag_device_password_session_clear(&s_password);
            v2_transfer_reset(true);
            send_v2_response(opcode, transaction_id,
                             error && strcmp(error, "crc") == 0 ?
                                 INKTAG_STATUS_CHECKSUM_ERROR :
                                 INKTAG_STATUS_PERSISTENCE_ERROR,
                             NULL, 0u);
            return;
        }
        response[0] = slot;
        inktag_device_password_session_clear(&s_password);
        v2_transfer_reset(false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, response, sizeof(response));
        return;
    }
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        inktag_status_v2_t access = inktag_access_image_write(&s_activation,
                                         s_v2_transfer.image_saved_count,
                                         s_v2_transfer.image_replacing,
                                         s_v2_transfer.image_replacement_ordinal, true,
                                         s_screen_config.visible_width,
                                         s_screen_config.visible_height);
        if (access != INKTAG_STATUS_OK) {
            v2_transfer_reset(true);
            send_v2_status(opcode, transaction_id, access);
            return;
        }
        uint8_t id = s_v2_transfer.resource_id;
        inktag_image_store_result_t result = image_store_finish(
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
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

static void v2_set_display_mode(uint8_t opcode, uint8_t transaction_id,
                                const uint8_t *payload, uint16_t length) {
    if (length < 1u || s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) || s_v2_transfer.active) {
        send_v2_status(opcode, transaction_id, length < 1u ? INKTAG_STATUS_INVALID_REQUEST : INKTAG_STATUS_BUSY);
        return;
    }
    uint8_t mode = payload[0];
    bool scheduled = false;
    inktag_status_v2_t failure = INKTAG_STATUS_NOT_FOUND;
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO) {
        slideshow_stop(false);
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                     MODE_DEVICE_INFO, s_timestamp);
    } else if (mode == INKTAG_DISPLAY_MODE_CALENDAR ||
               mode == INKTAG_DISPLAY_MODE_CLOCK) {
        slideshow_stop(false);
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                     mode == INKTAG_DISPLAY_MODE_CALENDAR ?
                                     MODE_CALENDAR : MODE_CLOCK, s_timestamp);
    } else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        slideshow_stop(false);
        scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_CLEAR,
                                     length > 1u ? (payload[1] != 0u) : 1u, 0);
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        scheduled = length == 2u && stored_image_replayable(payload[1], false) &&
                    v2_schedule_work(opcode, transaction_id, EPD_WORK_IMAGE_SHOW,
                                     payload[1], 0);
        if (scheduled && s_slideshow_active) {
            slideshow_arm_next_after_refresh();
        }
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint32_t interval = length == 5u ? read_u32_le(&payload[1]) : 3600u;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        uint8_t first_id = UINT8_MAX;
        if (image_store_model() != NULL &&
            image_store_next_visible_compatible(UINT8_MAX, true, &first_id)) {
            scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_IMAGE_SHOW,
                                         first_id, 0);
            if (scheduled) {
                s_slideshow_interval = interval;
                s_slideshow_active = true;
                slideshow_arm_next_after_refresh();
            }
        }
    } else if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        failure = INKTAG_STATUS_INVALID_STATE;
    } else {
        failure = INKTAG_STATUS_UNSUPPORTED;
    }
    send_v2_status(opcode, transaction_id, scheduled ? INKTAG_STATUS_OK : failure);
}

static void v2_display_action(uint8_t opcode, uint8_t transaction_id,
                              uint8_t flags, const uint8_t *payload,
                              uint16_t length) {
    if (length > 0u && payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN) {
        bool scheduled;
        if (length != 2u || payload[1] < 1u || payload[1] > 10u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (!v2_ghost_clean_available()) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        if ((flags & INKTAG_REQUEST_FLAG_CONFIRM) == 0u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
            return;
        }
        slideshow_stop(false);
        scheduled = v2_schedule_work(opcode, transaction_id,
                                     EPD_WORK_GHOST_CLEAN, payload[1], 0u);
        send_v2_status(opcode, transaction_id, scheduled ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY);
    } else if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT) {
        epd_work_type_t type;
        uint8_t arg;
        bool ok = page_cycle_resolve(false, &type, &arg) &&
                  v2_schedule_work(opcode, transaction_id, type, arg, s_timestamp);
        if (ok) page_cycle_scheduled(type, arg);
        send_v2_status(opcode, transaction_id, ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && s_slideshow_active) {
        s_slideshow_active = false;
        s_slideshow_next_at = 0;
        s_slideshow_advance_pending = false;
        /* A due frame can already be queued behind a connection. PAUSE must
         * not let that autonomous frame start later on disconnect. */
        if (s_epd_work_pending && s_epd_work_background &&
            s_epd_work_type == EPD_WORK_IMAGE_SHOW) {
            s_epd_work_pending = false;
            s_epd_work_type = EPD_WORK_NONE;
            s_epd_work_arg = 0u;
            s_epd_work_timestamp = 0u;
            s_epd_work_background = false;
            s_epd_work_client_owned = false;
        }
        if (s_slideshow_current_id != UINT8_MAX) {
            record_display_state(EPD_DISPLAY_STATE_PICTURE, s_slideshow_current_id, 0);
        }
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_RESUME &&
               s_slideshow_current_id != UINT8_MAX) {
        s_slideshow_active = true;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
        record_display_state(EPD_DISPLAY_STATE_SLIDESHOW,
                             s_slideshow_current_id, s_slideshow_interval);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        bool ok = v2_schedule_work(opcode, transaction_id, EPD_WORK_REFRESH, 0, 0);
        send_v2_status(opcode, transaction_id, ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        epd_release_owned_panel();
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
    }
}

static void v2_cancel(uint8_t opcode, uint8_t transaction_id) {
    if (s_v2_transfer.active) v2_transfer_reset(true);
    slideshow_stop(false);
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_slideshow_advance_pending = false;
    s_pending_picture_store_id = UINT8_MAX;
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
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    }
}

static uint8_t v2_effective_display_flags(void) {
    return (uint8_t)(s_display_state.reserved & EPD_DISPLAY_STATE_USER_FLAGS_MASK);
}

static void v2_send_display_settings(uint8_t opcode, uint8_t transaction_id) {
    inktag_display_settings_t settings;
    uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                     INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
    inktag_om_device_settings_get_display(&settings);
    settings.flags = v2_effective_display_flags();
    size_t length = inktag_display_settings_encode(
        &settings, response, sizeof(response));
    if (length == 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INTERNAL_ERROR);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, (uint16_t)length);
}

static void v2_get_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        v2_send_identity_settings(opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        v2_send_display_settings(opcode, transaction_id);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
}

static void v2_set_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length) {
    if (length < 2u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        if (payload[1] != 1u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        if (length != (uint16_t)(4u + name_length + label_length) ||
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (name_length != 0u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        epd_device_settings_t next = s_device_settings;
        next.label_length = label_length;
        memset(next.label, 0, sizeof(next.label));
        memcpy(next.label, &payload[4u + name_length], label_length);
        if (!epd_device_settings_write(&next)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        s_device_settings = next;
        bool applied = apply_runtime_device_name();
        uint8_t response[1] = {applied ? 0u : 1u};
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, NULL, 0);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        inktag_display_settings_t requested;
        const char *error = NULL;
        if (!inktag_display_settings_decode(payload, length, &requested) ||
            (requested.name_length > 0u &&
             !GUI_CountdownNameSupported(requested.name,
                                         requested.name_length))) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (requested.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            return;
        }
        if (!inktag_om_device_settings_set_display(&requested, &error)) {
            send_v2_response(opcode, transaction_id,
                             error && strcmp(error, "flash") == 0 ?
                                 INKTAG_STATUS_PERSISTENCE_ERROR :
                                 INKTAG_STATUS_INVALID_REQUEST,
                             NULL, 0u);
            return;
        }
        apply_display_settings_runtime(&requested);
        if (!display_state_flag(EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH)) {
            s_boot_restore_pending = false;
            s_boot_restore_due = false;
        }
        v2_send_display_settings(opcode, transaction_id);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
}


#define OM_LIFECYCLE_PENDING_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 19u))

static inktag_lifecycle_pending_read_t om_lifecycle_read(
    void *context, uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES],
    uint16_t *length) {
    nvds_tag_len_t size = INKTAG_LIFECYCLE_PENDING_BYTES;
    (void)context;
    uint8_t result = nvds_get(OM_LIFECYCLE_PENDING_TAG, &size, wire);
    if (result == NVDS_TAG_NOT_DEFINED)
        return INKTAG_LIFECYCLE_PENDING_READ_MISSING;
    if (result == NVDS_LENGTH_OUT_OF_RANGE) {
        *length = INKTAG_LIFECYCLE_PENDING_BYTES + 1u;
        return INKTAG_LIFECYCLE_PENDING_READ_FOUND;
    }
    if (result != NVDS_OK) return INKTAG_LIFECYCLE_PENDING_READ_ERROR;
    *length = size;
    return INKTAG_LIFECYCLE_PENDING_READ_FOUND;
}

static bool om_lifecycle_write(void *context,
    const uint8_t wire[INKTAG_LIFECYCLE_PENDING_BYTES]) {
    (void)context;
    return inktag_om_nvds_put(OM_LIFECYCLE_PENDING_TAG,
        INKTAG_LIFECYCLE_PENDING_BYTES, wire) == NVDS_OK;
}

static bool om_lifecycle_remove(void *context) {
    (void)context;
    return inktag_om_nvds_del(OM_LIFECYCLE_PENDING_TAG) == NVDS_OK;
}

static void om_lifecycle_lock(void *context,
    inktag_lifecycle_pending_scope_t scope) {
    (void)context;
    s_lifecycle_content_locked = true;
    s_lifecycle_pending_scope = scope;
    v2_find_owner_clear();
    slideshow_stop(false);
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_pending_picture_store_id = UINT8_MAX;
}

static bool om_lifecycle_clear_content(void) {
    uint16_t removed = 0u;
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true) ||
        !record_display_state(EPD_DISPLAY_STATE_CALENDAR,
                              EPD_DISPLAY_STATE_NO_SOURCE, 0u) ||
        !inktag_image_store_supported() ||
        image_store_finish(inktag_image_store_clear_all(),
            INKTAG_IMAGE_STORE_EVENT_CLEAR_ALL, &removed) != INKTAG_IMAGE_STORE_OK)
        return false;
    return true;
}

static bool om_lifecycle_clear(void *context,
    inktag_lifecycle_pending_scope_t scope) {
    (void)context;
    if (scope == INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA)
        return om_lifecycle_clear_content();
    /* A historical third-action marker is quarantined, never reinterpreted as
     * either new action and never allowed to delete activation or Find. */
    return false;
}

static bool om_lifecycle_verify(void *context,
    inktag_lifecycle_pending_scope_t scope) {
    epd_display_state_t state;
    inktag_image_store_status_t images;
    (void)context;
    if (scope != INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA) return false;
    return
        epd_display_state_read(&state) &&
        state.mode == EPD_DISPLAY_STATE_CALENDAR &&
        state.argument == EPD_DISPLAY_STATE_NO_SOURCE &&
        inktag_image_store_status(&s_screen_config, 0u, &images) ==
            INKTAG_IMAGE_STORE_OK &&
        images.saved_count == 0u && images.dirty_sectors == 0u;
}

static const inktag_lifecycle_pending_io_t s_om_lifecycle_io = {
    NULL, om_lifecycle_read, om_lifecycle_write, om_lifecycle_remove,
    om_lifecycle_lock, om_lifecycle_clear, om_lifecycle_verify,
};

static inktag_lifecycle_pending_result_t om_lifecycle_resume(void) {
    inktag_lifecycle_pending_result_t result =
        inktag_lifecycle_pending_resume(&s_om_lifecycle_io);
    if (result == INKTAG_LIFECYCLE_PENDING_RESULT_DONE ||
        result == INKTAG_LIFECYCLE_PENDING_RESULT_NONE) {
        s_lifecycle_content_locked = false;
        s_lifecycle_pending_scope = INKTAG_LIFECYCLE_PENDING_INVALID;
    }
    return result;
}

static bool v2_reset_user_settings(void) {
    /* The admitted command has consumed AUTH + fresh W. Ordinary preferences
     * and passive public NFC restore precede the existing atomic ID5 OFF
     * commit. Failures retain the password authority, not an inferred OFF. */
    if (!epd_user_settings_clear_and_verify(&s_config)) return false;
    epd_device_settings_default(&s_device_settings);
    (void)apply_runtime_device_name();
    bool password_reset = inktag_om_password_reset_settings(&s_password_config);
    inktag_device_password_session_clear(&s_password);
    if (!password_reset) return false;
    s_display_state_loaded = false;
    s_boot_restore_pending = false;
    s_boot_restore_due = false;
    s_epd = NULL;
    epd_gpio_close_if_needed();
    return true;
}

static void v2_schedule_system_action(uint8_t action) {
    /* Storage can outlive the cached process timestamp. Start the reply's
     * existing grace after that work; the TX_COMPLETE gate is still required. */
    runtime_clock_update();
    s_system_action_pending = action;
    s_system_action_not_before_ms = s_status_now_ms + 250u;
}

static void v2_system_action(uint8_t opcode, uint8_t transaction_id,
                             uint8_t flags, const uint8_t *payload, uint16_t length) {
    const inktag_lifecycle_capabilities_t lifecycle = {
        .reset = true,
#if INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE && INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED
        .timed_sleep = INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE != 0,
        .sleep_wake_mask = INKTAG_SLEEP_WAKE_TIMER,
        .minimum_sleep_seconds = TIMED_SLEEP_MIN_SECONDS,
        .maximum_sleep_seconds = TIMED_SLEEP_MAX_SECONDS,
#endif
        .factory_reset = false, /* Retired action3; never reuse its backend. */
#if defined(INKTAG_ONMICRO_OTA)
        .ota_recovery = true,
#endif
        .reset_settings = INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
        .clear_user_data = INKTAG_BOARD_CLEAR_USER_DATA_QUALIFIED != 0,
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &lifecycle, &plan);
    if (status != INKTAG_STATUS_OK) {
        send_v2_status(opcode, transaction_id, status);
        return;
    }

    if (s_epd_work_active ||
        (s_epd_work_pending && !s_epd_work_background) ||
        s_v2_transfer.active ||
        inktag_om_gui_asset_transfer_active() ||
        s_system_action_pending != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    /* Refused maintenance must leave queued background display work intact. */
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS &&
        !inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS &&
        !inktag_device_password_consume_settings_reset(
            &s_password, &s_password_config, s_status_now_ms)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
        return;
    }
    bool accepted = false;
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET) {
        (void)epd_time_state_write_reboot_anchor(
            s_timestamp, s_timezone_minutes);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
        v2_schedule_system_action(plan.action);
        accepted = true;
#if INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE && INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_TIMED_SLEEP) {
        status = product_access(
            INKTAG_PRODUCT_FEATURE_TIMED_SLEEP,
            INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE != 0,
            INKTAG_BOARD_LIFECYCLE_QUALIFIED != 0,
            INKTAG_EXPERIMENTAL_TIMED_SLEEP_ENABLED != 0);
        if (status != INKTAG_STATUS_OK) {
            send_v2_status(opcode, transaction_id, status);
            return;
        }
        slideshow_stop(false);
        s_boot_restore_pending = false;
        s_boot_restore_due = false;
        epd_release_owned_panel();
        (void)epd_time_state_write(s_timestamp, s_timezone_minutes);
        s_timed_sleep_seconds = plan.sleep_seconds;
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
        v2_schedule_system_action(plan.action);
        accepted = true;
#endif
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        uint16_t removed = 0u;
        inktag_image_store_status_t before;
        if (!inktag_om_health_operation_allowed(
                INKTAG_POWER_OPERATION_FLASH, true)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
            return;
        }
        if (inktag_image_store_status(&s_screen_config, 0u, &before) ==
            INKTAG_IMAGE_STORE_OK) removed = before.saved_count;
        inktag_lifecycle_pending_result_t clear_result =
            inktag_lifecycle_pending_begin(&s_om_lifecycle_io,
                INKTAG_LIFECYCLE_PENDING_CLEAR_USER_DATA);
        status = clear_result == INKTAG_LIFECYCLE_PENDING_RESULT_DONE ?
            INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR;
        if (status == INKTAG_STATUS_OK) {
            s_lifecycle_content_locked = false;
            s_lifecycle_pending_scope = INKTAG_LIFECYCLE_PENDING_INVALID;
        }
        uint8_t response[2];
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK) {
            send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                          opcode, transaction_id, NULL, 0u);
            accepted = true;
        }
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) {
        uint16_t removed = 0u;
        status = INKTAG_STATUS_OK;
        if (status == INKTAG_STATUS_OK && !v2_reset_user_settings()) {
            status = INKTAG_STATUS_PERSISTENCE_ERROR;
        }
        uint8_t response[2];
        put_u16_le(response, removed);
        send_v2_response(opcode, transaction_id, status,
                         status == INKTAG_STATUS_OK ? response : NULL,
                         status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
        if (status == INKTAG_STATUS_OK) {
            /* Do not restore the deleted time preference with a reboot anchor. */
            v2_schedule_system_action(plan.action);
            accepted = true;
        }
#if defined(INKTAG_ONMICRO_OTA)
    } else if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY) {
        if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_OTA, true)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        } else if (!epd_time_state_write_reboot_anchor(
                       s_timestamp, s_timezone_minutes)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
        } else {
            inktag_status_v2_t ota_status = onmicro_ota_enter();
            send_v2_status(opcode, transaction_id, ota_status);
            accepted = ota_status == INKTAG_STATUS_OK;
        }
#endif
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
    if (accepted && s_epd_work_pending && s_epd_work_background) {
        s_epd_work_pending = false;
        s_epd_work_type = EPD_WORK_NONE;
        s_epd_work_arg = 0u;
        s_epd_work_timestamp = 0u;
        s_epd_work_background = false;
    }
}

static void v2_delete_resource(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length) {
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
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
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
        return;
    }
    slideshow_stop(false);
    inktag_image_store_result_t result = image_store_finish(
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
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
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

/* Readback/download, store-only objects, generic pins and mandatory cleanup
 * keep their existing policy. RX and deferred dispatch call this same gate. */
static inktag_status_v2_t v2_panel_request_access(
    uint8_t opcode, const uint8_t *payload, uint16_t length) {
    bool drives = false;
    if (opcode == INKTAG_CMD_SET_SCREEN_PROFILE) {
        epd_screen_config_t requested;
        if (epd_screen_config_decode_exact(payload, length, &requested) ==
                EPD_SCREEN_STATUS_OK)
            return screen_control_access(&requested, inktag_om_pins_available());
    } else if (opcode == INKTAG_CMD_SET_DISPLAY_MODE)
        drives = length != 0u && payload[0] <= INKTAG_DISPLAY_MODE_SLIDESHOW &&
                 payload[0] != INKTAG_DISPLAY_MODE_DIRECT_IMAGE;
    else if (opcode == INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO)
        drives = v2_password_available();
    else if (opcode == INKTAG_CMD_DISPLAY_ACTION && length != 0u) {
        if (payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN &&
            !v2_ghost_clean_available())
            return length == 2u && payload[1] >= 1u && payload[1] <= 10u ?
                INKTAG_STATUS_UNSUPPORTED : INKTAG_STATUS_INVALID_REQUEST;
        drives = payload[0] == INKTAG_DISPLAY_ACTION_REFRESH ||
                 payload[0] == INKTAG_DISPLAY_ACTION_NEXT ||
                 payload[0] == INKTAG_DISPLAY_ACTION_RESUME ||
                 payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN;
    }
    else if (opcode == INKTAG_CMD_TRANSFER_BEGIN && length != 0u)
        drives = payload[0] == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
    else if (opcode == INKTAG_CMD_TRANSFER_WRITE || opcode == INKTAG_CMD_TRANSFER_COMMIT)
        drives = s_v2_transfer.active &&
                 s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME;
    else if (opcode == INKTAG_CMD_RUN_TEST && length != 0u)
        drives = payload[0] == INKTAG_TEST_TIME_SHIFT || payload[0] == INKTAG_TEST_TIME_SET;
    return drives ? epd_control_access() : INKTAG_STATUS_OK;
}

static void process_packet(const uint8_t *p_data, uint16_t length) {
    if (p_data == NULL || length < INKTAG_PROTOCOL_V2_HEADER_SIZE) return;
    uint8_t opcode = p_data[0];
    uint8_t transaction_id = p_data[1];
    uint8_t flags;
    const uint8_t *payload = &p_data[INKTAG_PROTOCOL_V2_HEADER_SIZE];
    uint16_t payload_length = length - INKTAG_PROTOCOL_V2_HEADER_SIZE;
    if (transaction_id == 0u || !inktag_release_request_flags(p_data[2],&flags)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (!inktag_device_password_command_allowed(&s_password, &s_password_config,
                                                 opcode)) {
        send_v2_status(opcode, transaction_id, s_password_config.valid ? INKTAG_STATUS_DENIED : INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    if (s_lifecycle_content_locked &&
        opcode != INKTAG_CMD_HELLO &&
        opcode != INKTAG_CMD_GET_CAPABILITIES &&
        opcode != INKTAG_CMD_GET_DEVICE_INFO &&
        opcode != INKTAG_CMD_GET_HEALTH &&
        opcode != INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS &&
        opcode != INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE &&
        opcode != INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD &&
        !(opcode == INKTAG_CMD_SYSTEM_ACTION && payload_length == 1u &&
          payload[0] == INKTAG_SYSTEM_ACTION_OTA_RECOVERY)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_RETRY_REQUIRED);
        return;
    }
    {
        inktag_status_v2_t access = v2_panel_request_access(opcode, payload, payload_length);
        if (access != INKTAG_STATUS_OK) {
            if (s_v2_transfer.active &&
                s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME &&
                (opcode == INKTAG_CMD_TRANSFER_WRITE || opcode == INKTAG_CMD_TRANSFER_COMMIT))
                v2_transfer_reset(true);
            send_v2_status(opcode, transaction_id, access);
            return;
        }
    }
    if (inktag_protocol_command_mutates_persistent_state(
            opcode, payload, payload_length) &&
        !inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }

    switch (opcode) {
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
            v2_send_status(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_HEALTH:
            v2_send_health(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TIME:
            v2_send_time(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_TIME:
            if (payload_length != 7u || payload[6] > 6u) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            } else {
                uint32_t utc_timestamp = read_u32_le(payload);
                int16_t next_timezone = (int16_t)read_u16_le(&payload[4]);
                int64_t local_timestamp = (int64_t)utc_timestamp +
                    (int32_t)next_timezone * 60;
                uint32_t previous_timestamp = timestamp();
                inktag_elapsed_clock_t previous_clock = s_elapsed_clock;
                int16_t previous_timezone = s_timezone_minutes;
                uint8_t previous_week_start = s_config.week_start;
                bool previous_time_valid = s_time_valid;
                if (next_timezone < -840 || next_timezone > 840 ||
                    local_timestamp < 946684800ll ||
                    local_timestamp > 4102444799ll) {
                    send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
                    break;
                }
                s_timezone_minutes = next_timezone;
                set_timestamp((uint32_t)local_timestamp);
                s_config.week_start = payload[6];
                if (!persist_time_snapshot()) {
                    s_timestamp = previous_timestamp;
                    s_elapsed_clock = previous_clock;
                    s_timezone_minutes = previous_timezone;
                    s_config.week_start = previous_week_start;
                    s_time_valid = previous_time_valid;
                    wall_clock_update();
                    send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
                } else {
                    epd_config_write(&s_config);
                    v2_send_time(opcode, transaction_id);
                }
            }
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE:
            v2_send_screen(opcode, transaction_id);
            break;
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            if (s_epd_work_active ||
                (s_epd_work_pending && !s_epd_work_background) ||
                s_v2_transfer.active || s_led_test_active ||
                inktag_status_locate_active(&s_status) || inktag_om_device_settings_io_busy()) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
                break;
            }
            epd_screen_config_t requested;
            epd_screen_status_t screen_status = epd_screen_config_decode_exact(
                payload, payload_length, &requested);
            if (screen_status != EPD_SCREEN_STATUS_OK || epd_find(&requested) == NULL) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
            } else if (!inktag_image_store_profile_allowed(&requested)) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
            } else {
                inktag_om_pin_candidate_t routes;
                inktag_image_store_profile_view_t view;
                if(!inktag_om_pins_prepare(&requested,&routes) || !routes.busy_known) {
                    send_v2_status(opcode, transaction_id, INKTAG_STATUS_IO_ERROR);break;
                }
                inktag_status_v2_t access = screen_control_access(&requested, true);
                if (access != INKTAG_STATUS_OK) {
                    send_v2_status(opcode, transaction_id, access);break;
                }
                inktag_image_store_result_t prepared=inktag_image_store_prepare_profile(&requested,&view);
                if(prepared!=INKTAG_IMAGE_STORE_OK && prepared!=INKTAG_IMAGE_STORE_NO_SPACE) {
                    send_v2_status(opcode, transaction_id, prepared==INKTAG_IMAGE_STORE_BUSY?
                        INKTAG_STATUS_BUSY:INKTAG_STATUS_IO_ERROR);break;
                }
                inktag_om_app_configuration_park();
                if(!epd_screen_config_write(&requested)) {
                    inktag_image_store_invalidate();inktag_om_pins_invalidate();s_epd=NULL;
                    memset(&s_screen_config,0,sizeof(s_screen_config));
                    send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);break;
                }
                /* No fallible reads after commit. The foreground queue owns
                 * this prepare/commit/publication interval; BLE jobs enqueue. */
                inktag_om_pins_publish(&routes);
                inktag_image_store_publish_profile(&view);
                if (s_epd_work_pending && s_epd_work_background) {
                    s_epd_work_pending = false;
                    s_epd_work_type = EPD_WORK_NONE;
                    s_epd_work_background = false;
                }
                s_boot_restore_pending = false;
                s_boot_restore_due = false;
                slideshow_stop(false);
                s_screen_config = requested;
                (void)pins_apply_to_config();
                s_epd = epd_find(&s_screen_config);
                v2_send_screen(opcode, transaction_id);
                send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                              opcode, transaction_id, NULL, 0);
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
            v2_cancel(opcode, transaction_id);
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
            v2_transfer_write(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_TRANSFER_COMMIT:
            if (payload_length != 0u) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            } else {
                v2_transfer_commit(opcode, transaction_id);
            }
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!s_v2_transfer.active) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
            } else {
                v2_transfer_reset(true);
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
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
            if (payload_length != 0u) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            } else {
                v2_send_gui_asset_info(opcode, transaction_id);
            }
            break;
        case INKTAG_CMD_GET_RESOURCE_INFO:
            v2_send_resource_info(opcode, transaction_id);
            break;
        case INKTAG_CMD_LIST_RESOURCES:
            v2_list_resource(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_SET_RESOURCE_ALIAS:
        case INKTAG_CMD_SET_IMAGE_METADATA:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
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
#if INKTAG_OM_FULL_FEATURES
            v2_get_input_config(opcode, transaction_id);
#else
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_SET_INPUT_CONFIG:
#if INKTAG_OM_FULL_FEATURES
            v2_set_input_config(opcode, transaction_id, payload, payload_length);
#else
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_GET_IO_CONFIG:
#if INKTAG_OM_FULL_FEATURES
            if(payload_length && payload[0]==2u)
                v2_pin_config(opcode,transaction_id,payload,payload_length,false);
            else if(payload_length==1u && payload[0]==INKTAG_RELEASE_AUX_GET_SELECTOR)
                v2_get_io_config(opcode,transaction_id);
            else send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
#else
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_SET_IO_CONFIG:
#if INKTAG_OM_FULL_FEATURES
            v2_set_io_config(opcode, transaction_id, payload, payload_length);
#else
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
#endif
            break;
        case INKTAG_CMD_GET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, false,
                                payload, payload_length);
            break;
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
            v2_indicator_config(opcode, transaction_id, true,
                                payload, payload_length);
            break;
        case INKTAG_CMD_GET_NFC_CAPABILITIES:
        case INKTAG_CMD_GET_NFC_STATUS:
        case INKTAG_CMD_GET_NFC_NDEF:
        case INKTAG_CMD_SET_NFC_NDEF:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        case INKTAG_CMD_GET_LAYOUT_CAPABILITIES:
        case INKTAG_CMD_VALIDATE_LAYOUT:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_GET_LAYOUT_STATUS:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        case INKTAG_CMD_GET_ACTIVATION_STATUS: {
            /* Ordinary password AUTH reads the existing immutable ID through
             * this reserved ABI; base never grants activation capabilities. */
            uint8_t response[INKTAG_ACTIVATION_STATUS_WIRE_SIZE];
            inktag_activation_encode_status(&s_activation, response);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
            break;
        }
        case INKTAG_CMD_SET_ACTIVATION_CODE:
        case INKTAG_CMD_GET_ACTIVATION_REQUEST:
        case INKTAG_CMD_GET_FIND_CAPABILITIES:
        case INKTAG_CMD_GET_OWNER_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_OWNER_ACTION:
        case INKTAG_CMD_GET_FIND_CONFIG:
        case INKTAG_CMD_SET_FIND_CONFIG:
        case INKTAG_CMD_FIND_ACTION:
        case INKTAG_CMD_GET_FIND_OWNER_STATUS:
        case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION:
        case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
        case INKTAG_CMD_ABORT_FIND_OWNER_ROTATION:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        case INKTAG_CMD_GET_SECURITY_INFO:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
        case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
        case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
        case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
            v2_password_request(opcode, transaction_id, flags, payload, payload_length);
            break;
        case INKTAG_CMD_LIST_TESTS:
            v2_list_tests(opcode, transaction_id);
            break;
        case INKTAG_CMD_RUN_TEST:
            v2_run_test(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TEST_STATUS:
            v2_send_test_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_OPEN_DEBUG_SESSION:
        case INKTAG_CMD_DEBUG_READ:
        case INKTAG_CMD_CLOSE_DEBUG_SESSION:
        case INKTAG_CMD_ABORT_DEBUG_OPERATION:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        case INKTAG_CMD_DEBUG_EXECUTE:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            break;
        default:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNKNOWN_COMMAND);
            break;
    }
}
