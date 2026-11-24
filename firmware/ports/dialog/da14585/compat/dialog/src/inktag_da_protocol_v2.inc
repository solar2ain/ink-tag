/* DA normal source projection v1 */
/* Included by user_custs1_impl.c. Product command semantics live in the
 * common registry; this file adapts them to Dialog display and storage APIs. */

static inktag_status_v2_t v2_store_status(inktag_image_store_result_t result)
{
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

static uint32_t v2_crc32_update(uint32_t crc, const uint8_t *data, uint16_t length)
{
    return inktag_crc32_update(crc, data, length);
}

__attribute__((noinline)) static uint32_t v2_elapsed_ms(uint32_t start_tick)
{
    uint32_t ticks = (inktag_da_now_tick() - start_tick) & BLE_BASETIMECNT_MASK;
    return (ticks * 5u) / 8u;
}

__attribute__((noinline)) static bool v2_display_busy(void)
{
    return s_work_active || s_work_pending || s_refresh_waiting;
}

static bool pin_config_busy(void)
{
    return v2_display_busy() || s_epd_gpio_ready || s_picture_session_active ||
        s_v2_transfer.active || s_system_action_pending != 0u;
}

static bool pin_config_publish(epd_config_t *next)
{
    if (!EPD_DialogPinsRelease()) return false;
    memcpy(&s_config, next, offsetof(epd_config_t, display_mode));
    s_config.enable_active_low = next->enable_active_low;
    EPD_GPIO_Load(&s_config);
    s_panel_temperature_valid = false;
    return true;
}

static void service_pin_config(void)
{
    epd_config_t defaults, effective;
    bool has_override;
    if (!s_pin_reconcile_pending || pin_config_busy()) return;
    /* One fresh read per connection, outside GET/GATT and only at safe idle.
     * A failed/unknown commit is never replayed. */
    s_pin_reconcile_pending = false;
    if (epd_dialog_pin_config_load(&defaults, &effective, &has_override) &&
        (!has_override || s_pin_writes_allowed) &&
        pin_config_publish(&effective)) {
        s_pin_defaults = defaults;
        s_pin_override = has_override;
        s_pin_unconfirmed = false;
    }
}

static bool v2_image_store_usable(void)
{
    return inktag_image_store_supported() &&
           inktag_image_store_fixed_slot_count() != 0u;
}

static void v2_transfer_reset(bool erase_store)
{
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE &&
        erase_store)
        inktag_da_store_abort();
    if (s_v2_transfer.active &&
        s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        if (s_epd != NULL) s_epd->drv->sleep(s_epd);
        epd_gpio_close_if_needed();
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_picture_session_active = false;
}

static uint8_t v2_current_display_mode(void)
{
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

static uint32_t v2_capabilities(void)
{
    uint32_t capabilities = INKTAG_CAPABILITY_TIME |
                            INKTAG_CAPABILITY_SCREEN_PROFILE |
                            INKTAG_CAPABILITY_DIRECT_TRANSFER |
                            INKTAG_CAPABILITY_DEVICE_LABEL |
                            INKTAG_CAPABILITY_TEST;
    /* The other three capability gates are constant true on this port.
     * Keep the independent board qualification predicates unchanged. */
    if (inktag_da_voltage_telemetry_available())
        capabilities |= INKTAG_CAPABILITY_TELEMETRY;
    if (inktag_da_battery_telemetry_available())
        capabilities |= INKTAG_CAPABILITY_HEALTH;
#if INKTAG_BOARD_LIFECYCLE_V2_IMPLEMENTED
    capabilities |= INKTAG_CAPABILITY_LIFECYCLE | INKTAG_CAPABILITY_LIFECYCLE_V2;
#endif
    if (v2_image_store_usable())
        capabilities |= INKTAG_CAPABILITY_RESOURCES | INKTAG_CAPABILITY_SLIDESHOW |
                        INKTAG_CAPABILITY_IMAGE_METADATA;
    capabilities |= INKTAG_CAPABILITY_OTA_RECOVERY;
    if (s_runtime_board_kind == INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR ||
        s_runtime_board_kind == INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR)
        capabilities |= INKTAG_CAPABILITY_PIN_CONFIG;
    if (inktag_da_transfer_packbits_available())
        capabilities |= INKTAG_CAPABILITY_TRANSFER_PACKBITS_RLE;
    return capabilities;
}

static bool v2_schedule_work(uint8_t opcode, uint8_t transaction_id,
                             epd_work_type_t type, uint8_t argument,
                             uint32_t draw_timestamp)
{
    uint8_t progress[2] = {INKTAG_BUSY_PHASE_DISPLAY, 0u};
    if (v2_display_busy()) return false;
    s_v2_work_opcode = opcode;
    s_v2_work_transaction = transaction_id;
    if (!schedule_epd_work(type, argument, false)) {
        s_v2_work_opcode = 0u;
        s_v2_work_transaction = 0u;
        return false;
    }
    s_work_timestamp = draw_timestamp;
    send_v2_event(INKTAG_EVENT_PROGRESS, INKTAG_STATUS_OK,
                  opcode, transaction_id, progress, sizeof(progress));
    return true;
}

static void v2_send_hello(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[6];
    payload[0] = 1u;
    payload[1] = INKTAG_PROTOCOL_V2_VERSION;
    put_u16_le(&payload[2], INKTAG_DA_PROTOCOL_PACKET_BYTES);
    put_u16_le(&payload[4], INKTAG_DA_TRANSFER_WRITE_BYTES);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_free_status(uint8_t opcode, uint8_t transaction_id,
                                uint16_t length)
{
    /* Canonical status v2, permanently Free with no activation scheme,
     * credential, challenge or entitlement. This read-only query does not
     * advertise activation and does not link the activation engine. */
    const uint8_t response[INKTAG_ACTIVATION_STATUS_WIRE_SIZE] = {
        2u, INKTAG_ACTIVATION_STATE_FREE, INKTAG_ACTIVATION_SCHEME_NONE,
    };
    if (length != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static void v2_send_capabilities(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[INKTAG_CAPABILITIES_EXTENDED_SIZE] = {0};
    uint16_t mode_mask = 0u;
    uint16_t action_mask = 0u;
    uint8_t mode;
    payload[0] = 1u;
    put_u32_le(&payload[1], v2_capabilities());
    for (mode = INKTAG_DISPLAY_MODE_DEVICE_INFO;
         mode <= INKTAG_DISPLAY_MODE_DIRECT_IMAGE; ++mode)
        mode_mask |= (uint16_t)(1u << mode);
    if (v2_image_store_usable()) {
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
    put_u16_le(&payload[9], INKTAG_DA_PROTOCOL_PACKET_BYTES);
    put_u16_le(&payload[11], INKTAG_DA_TRANSFER_WRITE_BYTES);
    payload[13] = EPD_DEVICE_NAME_MAX_BYTES;
    payload[14] = EPD_DEVICE_LABEL_MAX_BYTES;
    put_u16_le(&payload[15], v2_image_store_usable() ?
                             inktag_image_store_fixed_slot_count() : 0u);
    put_u32_le(&payload[INKTAG_CAPABILITIES_EXTENSION_OFFSET],
               INKTAG_CAPABILITY_EXTENSION_IMAGE_READING_ORIENTATION);
    /* ATT23 retains the published17B prefix. Advertise the optional extension
     * only after the actual SDK link has room for the complete response. */
    uint16_t length = gattc_get_mtu(s_connection_idx) >=
        sizeof(payload) + INKTAG_PROTOCOL_V2_HEADER_SIZE + 3u ? sizeof(payload) : 17u;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, length);
}

/* Do not keep this command's response/settings scratch live while an unrelated
 * transfer handler cooperatively enters the SDK's ROM BLE scheduler. */
__attribute__((noinline)) static void v2_send_device_info(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[112]; /* Initialize only the conditional address field. */
    epd_device_settings_t settings;
    const char *board = inktag_da_board_id();
    const char *runtime_name = inktag_da_device_name();
    uint8_t board_length = (uint8_t)strlen(board);
    uint8_t runtime_name_length = (uint8_t)strlen(runtime_name);
    uint8_t address[6];
    uint32_t app_size = 0u;
    uint32_t app_crc32 = 0u;
    uint16_t offset;
    load_device_settings(&settings);
    payload[0] = 1u;
    payload[1] = board_length;
    payload[2] = runtime_name_length;
    payload[3] = settings.label_length;
    payload[4] = inktag_da_get_address(address) ? 1u : 0u;
    if (payload[4] != 0u) memcpy(&payload[5], address, sizeof(address));
    else memset(&payload[5], 0, sizeof(address));
    put_u32_le(&payload[11], ((uint32_t)INKTAG_VERSION_MAJOR << 16) |
                              ((uint32_t)INKTAG_VERSION_MINOR << 8) |
                              INKTAG_VERSION_PATCH);
    (void)inktag_da_application_info(&app_size, &app_crc32);
    put_u32_le(&payload[15], app_size);
    put_u32_le(&payload[19], app_crc32);
    offset = 23u;
    memcpy(&payload[offset], board, board_length);
    offset = (uint16_t)(offset + board_length);
    memcpy(&payload[offset], runtime_name, runtime_name_length);
    offset = (uint16_t)(offset + runtime_name_length);
    memcpy(&payload[offset], settings.label, settings.label_length);
    offset = (uint16_t)(offset + settings.label_length);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, offset);
}

__attribute__((noinline)) static void v2_send_status(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[16u + INKTAG_TEMPERATURE_WIRE_SIZE];
    bool temperature_valid = panel_temperature_fresh_at(inktag_da_now_tick());
    uint16_t voltage = inktag_da_voltage_telemetry_available() ?
                       inktag_da_health_policy_voltage_mv() : 0u;
    uint8_t flags = 0u;
    uint8_t progress = 0u;
    if (v2_display_busy()) flags |= 0x01u;
    if (s_slideshow_active) flags |= 0x02u;
    if (voltage > 0u) flags |= 0x04u;
    if (temperature_valid) flags |= 0x08u;
    if (s_v2_transfer.active) flags |= 0x10u;
    if (s_v2_transfer.active && s_v2_transfer.total_size > 0u)
        progress = (uint8_t)((s_v2_transfer.written * 100u) /
                             s_v2_transfer.total_size);
    payload[0] = 1u;
    payload[1] = v2_current_display_mode();
    payload[2] = flags;
    payload[3] = progress;
    payload[4] = s_v2_transfer.active ? INKTAG_CMD_TRANSFER_WRITE : s_v2_work_opcode;
    payload[5] = s_v2_transfer.active ? s_v2_transfer.transaction_id : s_v2_work_transaction;
    payload[6] = s_slideshow_current_id;
    payload[7] = temperature_valid ?
        (uint8_t)s_panel_temperature : 0x80u;
    put_u16_le(&payload[8], voltage);
    put_u32_le(&payload[10], s_slideshow_interval);
    put_u16_le(&payload[14], (uint16_t)s_timezone_minutes);
    /* Keep the legacy core unchanged. Only identify the existing fresh EPD
     * cache; GET_STATUS neither powers the panel nor acquires a new sample.
     * DA has no qualified MCU temperature source. */
    inktag_temperature_wire_encode(&payload[16], false, 0,
                                   temperature_valid,
                                   (int16_t)s_panel_temperature * 16);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

static void v2_send_health(uint8_t opcode, uint8_t transaction_id,
                           const uint8_t *payload, uint16_t length)
{
    uint8_t response[INKTAG_DA_HEALTH_WIRE_SIZE];
    uint8_t record_index = length == 0u ? 0u : payload[0];
    uint16_t response_length;
    if (!inktag_da_battery_telemetry_available()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (length > 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    response_length = inktag_da_health_encode(
        record_index, response, sizeof(response));
    send_v2_response(opcode, transaction_id,
                     response_length != 0u ? INKTAG_STATUS_OK : INKTAG_STATUS_NOT_FOUND,
                     response_length != 0u ? response : NULL, response_length);
}

static void v2_encode_time(uint8_t payload[7])
{
    uint32_t utc = s_time_valid ?
        s_timestamp - (int32_t)s_timezone_minutes * 60 : 0u;
    put_u32_le(payload, utc);
    put_u16_le(&payload[4], (uint16_t)s_timezone_minutes);
    payload[6] = s_config.week_start;
}

static void v2_send_time(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[7];
    v2_encode_time(payload);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, sizeof(payload));
}

#include "inktag_da_tests.inc"

/* GUI yields run in the foreground, after SDK callbacks have unwound. Only
 * cached HEALTH and TIME may borrow the existing validated RX mailbox here:
 * never recurse through the full dispatcher or run Flash/EPD/settings work on
 * the renderer stack. Keep its lease until the response is fully constructed. */
__attribute__((noinline)) static void v2_service_display_read(void)
{
    if (s_in_gatt_callback || s_image_processing ||
        s_image_count == 0u) return;
    packet_t *packet = &s_image_queue[s_image_tail];
    uint8_t opcode = packet->data[0];
    if (opcode != INKTAG_CMD_GET_TIME && opcode != INKTAG_CMD_GET_HEALTH) return;
    s_image_processing = true;
    if (opcode == INKTAG_CMD_GET_TIME) {
        service_clock();
        v2_send_time(opcode, packet->data[1]);
    } else {
        v2_send_health(opcode, packet->data[1], &packet->data[3],
                       (uint16_t)(packet->length - 3u));
    }
    s_image_processing = false;
    s_image_tail = (uint8_t)((s_image_tail + 1u) % INKTAG_DA_IMAGE_QUEUE_DEPTH);
    s_image_count--;
}

static void v2_send_screen(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[EPD_SCREEN_WIRE_SIZE];
    epd_screen_status_t status = epd_screen_config_encode(&s_screen_config, payload);
    send_v2_response(opcode, transaction_id,
                     status == EPD_SCREEN_STATUS_OK ? INKTAG_STATUS_OK :
                                                      INKTAG_STATUS_INTERNAL_ERROR,
                     status == EPD_SCREEN_STATUS_OK ? payload : NULL,
                     status == EPD_SCREEN_STATUS_OK ? sizeof(payload) : 0u);
}

static void v2_send_identity_settings(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[64]; /* Only the absent address needs a zero fallback. */
    epd_device_settings_t settings;
    const char *name = inktag_da_device_name();
    uint8_t name_length = (uint8_t)strlen(name);
    uint8_t address[6];
    uint16_t offset;
    load_device_settings(&settings);
    payload[0] = INKTAG_SETTINGS_SECTION_IDENTITY;
    payload[1] = EPD_DEVICE_SETTINGS_VERSION;
    payload[2] = name_length;
    payload[3] = settings.label_length;
    payload[4] = inktag_da_get_address(address) ? 1u : 0u;
    if (payload[4] != 0u) memcpy(&payload[5], address, sizeof(address));
    else memset(&payload[5], 0, sizeof(address));
    offset = 11u;
    memcpy(&payload[offset], name, name_length);
    offset = (uint16_t)(offset + name_length);
    memcpy(&payload[offset], settings.label, settings.label_length);
    offset = (uint16_t)(offset + settings.label_length);
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, payload, offset);
}

__attribute__((noinline)) static void v2_get_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length)
{
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        v2_send_identity_settings(opcode, transaction_id);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        s_display_settings.week_start = s_config.week_start;
        /* ID7 can retain preferences after clear-images removes replay ID4.
         * A missing replay target is not permission to clear the boot setting. */
        response_length = inktag_display_settings_encode(
            &s_display_settings, response, sizeof(response));
        send_v2_response(
            opcode, transaction_id,
            response_length != 0u ? INKTAG_STATUS_OK : INKTAG_STATUS_INTERNAL_ERROR,
            response_length != 0u ? response : NULL,
            (uint16_t)response_length);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
}

__attribute__((noinline)) static void v2_set_settings(uint8_t opcode, uint8_t transaction_id,
                            const uint8_t *payload, uint16_t length)
{
    if (length < 2u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (payload[0] == INKTAG_SETTINGS_SECTION_IDENTITY) {
        epd_device_settings_t next;
        const char *current_name = inktag_da_device_name();
        uint8_t name_length = length >= 4u ? payload[2] : 0u;
        uint8_t label_length = length >= 4u ? payload[3] : 0u;
        if (payload[1] != 1u ||
            length != (uint16_t)(4u + name_length + label_length) ||
            (name_length != 0u &&
             !epd_device_name_valid(&payload[4], name_length)) ||
            !epd_device_label_valid(&payload[4u + name_length], label_length) ||
            !inktag_device_label_write_valid(&payload[4u + name_length], label_length)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        /* Read-only GAP clients use NameLen=0 for a label-only write. An
         * unchanged name echo remains valid; neither form can rename GAP. */
        if (name_length != 0u &&
            (name_length != strlen(current_name) ||
             memcmp(current_name, &payload[4], name_length) != 0)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
            return;
        }
        load_device_settings(&next);
        next.label_length = label_length;
        memset(next.label, 0, sizeof(next.label));
        memcpy(next.label, &payload[4u + name_length], label_length);
        if (!epd_device_settings_write(&next)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        {
            uint8_t reboot_required[1] = {0u};
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                             reboot_required, sizeof(reboot_required));
        }
        send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, NULL, 0u);
    } else if (payload[0] == INKTAG_SETTINGS_SECTION_DISPLAY) {
        inktag_display_settings_t next;
        epd_display_state_t next_state;
        uint8_t response[INKTAG_DISPLAY_SETTINGS_BASE_SIZE +
                         INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES];
        size_t response_length;
        if (!inktag_display_settings_decode(payload, length, &next)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if ((next.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN) != 0u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
            return;
        }
        if (next.name_length != 0u) {
            bool supported = false;
            if (inktag_gui_assets_begin()) {
                supported = GUI_CountdownNameSupported(
                    next.name, next.name_length);
                inktag_gui_assets_end();
            }
            if (!supported) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
                return;
            }
        }
        next_state = s_display_state;
        next_state.reserved &=
            (uint16_t)~(EPD_DISPLAY_STATE_WEEK_START_MASK |
                        EPD_DISPLAY_STATE_USER_FLAGS_MASK |
                        EPD_DISPLAY_STATE_PRO_FLAGS_MASK);
        next_state.reserved |=
            (uint16_t)next.week_start << EPD_DISPLAY_STATE_WEEK_START_SHIFT;
        if ((next.flags & INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH) != 0u)
            next_state.reserved |= EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH;
        if (!epd_dialog_display_settings_write(&next_state, &next)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        s_display_settings = next;
        s_display_state = next_state;
        /* ID7 preferences do not create or persist a replay target. */
        s_config.week_start = next.week_start;
        epd_config_write(&s_config);
        response_length = inktag_display_settings_encode(
            &s_display_settings, response, sizeof(response));
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, (uint16_t)response_length);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
}

static void v2_send_resource_info(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t payload[34]; /* Every transmitted field is assigned below. */
    inktag_image_store_status_t status;
    uint32_t frame_bytes = expected_frame_payload();
    inktag_image_store_result_t result = image_store_visible_status(
        &s_screen_config, frame_bytes, &status);
    if (result != INKTAG_IMAGE_STORE_OK || s_epd == NULL) {
        send_v2_status(opcode, transaction_id, result == INKTAG_IMAGE_STORE_OK ? INKTAG_STATUS_INCOMPATIBLE :
                                                          v2_store_status(result));
        return;
    }
    payload[0] = 1u;
    put_u16_le(&payload[1], s_epd->width);
    put_u16_le(&payload[3], s_epd->height);
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
    put_u16_le(&payload[30], status.required_sectors != 0u ?
                              status.free_sectors / status.required_sectors : 0u);
    /* Optional shared fixed-slot tail: old clients ignore these two bytes. */
    put_u16_le(&payload[32], inktag_image_store_fixed_slot_count());
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     payload, sizeof(payload));
}

__attribute__((noinline)) static void v2_list_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length)
{
    if (length > 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    {
        inktag_image_store_header_t header;
        epd_screen_config_t stored;
        uint8_t record_id;
        uint8_t response[64]; /* Only the fully assigned prefix is sent. */
        uint8_t name_length = 0u;
        bool decoded;
        inktag_image_store_result_t result = image_store_next_visible(
                length > 0u ? payload[0] : UINT8_MAX,
                &record_id, &header);
        if (result == INKTAG_IMAGE_STORE_NOT_FOUND)
            goto done;
        if (result != INKTAG_IMAGE_STORE_OK) {
            send_v2_status(opcode, transaction_id, v2_store_status(result));
            return;
        }
        decoded = epd_screen_config_decode_exact(
            header.screen_config, EPD_SCREEN_WIRE_SIZE, &stored) ==
            EPD_SCREEN_STATUS_OK;
        while (name_length < INKTAG_IMAGE_STORE_NAME_SIZE &&
               header.name[name_length] != '\0') ++name_length;
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
done:
    {
        /* DA's 17-sector extent cannot contain more than 17 fixed slots. */
        uint8_t done[5] = {1u, UINT8_MAX};
        uint8_t bitmap_bytes = (uint8_t)((inktag_image_store_fixed_slot_count() + 7u) / 8u);
        inktag_image_store_result_t result = inktag_image_store_occupancy(&done[2], 3u);
        if (result != INKTAG_IMAGE_STORE_OK) {
            send_v2_status(opcode, transaction_id, v2_store_status(result));
            return;
        }
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         done, (uint16_t)(2u + bitmap_bytes));
    }
}

/* Read-back owns a large response buffer; other opcodes never need that frame. */
__attribute__((noinline)) static void v2_read_resource(uint8_t opcode, uint8_t transaction_id,
                             const uint8_t *payload, uint16_t length)
{
    inktag_image_store_header_t header;
    inktag_image_store_result_t result;
    uint8_t response[INKTAG_DA_PROTOCOL_PACKET_BYTES - INKTAG_PROTOCOL_V2_HEADER_SIZE];
    uint32_t offset;
    uint16_t requested;
    if (length != 6u || payload[5] == 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    result = inktag_image_store_get(payload[0], &header);
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_status(opcode, transaction_id, v2_store_status(result));
        return;
    }
    offset = inktag_read_u32_le(&payload[1]);
    requested = payload[5];
    if (requested > INKTAG_DA_READ_RESOURCE_BYTES)
        requested = INKTAG_DA_READ_RESOURCE_BYTES;
    if (offset >= header.payload_size) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
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
                     result == INKTAG_IMAGE_STORE_OK ?
                         (uint16_t)(9u + requested) : 0u);
}

__attribute__((noinline)) static void v2_transfer_begin(
    uint8_t opcode, uint8_t transaction_id,
    const uint8_t *payload, uint16_t length)
{
    uint8_t object_type;
    uint8_t expected_segments;
    inktag_stored_image_v5_descriptor_t stored_descriptor;
    const inktag_transfer_descriptor_t *descriptor;
    uint8_t response[5];
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (length < 20u || s_v2_transfer.active || v2_display_busy()) {
        send_v2_status(opcode, transaction_id, s_v2_transfer.active || v2_display_busy() ? INKTAG_STATUS_BUSY :
                                                                    INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    object_type = payload[0];
    if (object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME && length != 20u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if ((payload[3] & INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u &&
        !inktag_da_transfer_packbits_available()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (!inktag_da_transfer_object_supported(object_type)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        status = inktag_stored_image_v5_descriptor_decode(payload, length, &stored_descriptor);
    } else {
        status = inktag_transfer_descriptor_decode(payload, length, &stored_descriptor.transfer);
    }
    descriptor = &stored_descriptor.transfer;
    if (status != INKTAG_STATUS_OK) {
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    {
        if (!epd_ensure_screen()) {
            send_v2_status(opcode, transaction_id, epd_unavailable_status());
            return;
        }
        expected_segments =
            (s_epd->transfer_format == EPD_SCREEN_TRANSFER_DUAL_1BPP) ?
                2u : 1u;
        if ((payload[3] & ~(INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE |
                           INKTAG_IMAGE_READING_ROTATION_MASK |
                           ((1u << expected_segments) - 1u))) != 0u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        if (payload[1] != s_epd->transfer_format ||
            descriptor->segment_count != expected_segments ||
            descriptor->total_size != expected_frame_payload()) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
            return;
        }
    }
    if (!inktag_da_health_operation_allowed(
            object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ?
                INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY,
            true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    s_v2_transfer.active = true;
    s_v2_transfer.object_type = object_type;
    s_v2_transfer.transaction_id = transaction_id;
    s_v2_transfer.format = descriptor->format;
    s_v2_transfer.segment_count = descriptor->segment_count;
    s_v2_transfer.black_mask = payload[3];
    s_v2_transfer.resource_id = UINT8_MAX;
    s_v2_transfer.total_size = descriptor->total_size;
    s_v2_transfer.expected_crc = descriptor->expected_crc;
    s_v2_transfer.running_crc = 0xFFFFFFFFu;
    memcpy(s_v2_transfer.segment_size, descriptor->segment_size,
           sizeof(s_v2_transfer.segment_size));
    s_v2_transfer.last_activity_tick = inktag_da_now_tick();
    response[0] = object_type;
    response[1] = UINT8_MAX;
    if (object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME) {
        slideshow_stop();
        s_picture_session_active = true;
        epd_gpio_open_if_needed();
        if (!epd_init_with_load_check())
            status = INKTAG_STATUS_LOW_POWER;
        else if (!EPD_DialogSpiReady() || EPD_DialogSpiLastError() != 0)
            status = INKTAG_STATUS_IO_ERROR;
    } else {
        uint8_t id = UINT8_MAX;
        inktag_image_store_result_t result;
        /* Matching the encoded active profile already resolves/validates it
         * through the common codec. Do not decode and resolve the identical
         * wire bytes a second time just to encode them again in store_begin. */
        if (!screen_config_matches(stored_descriptor.screen_profile)) {
            status = INKTAG_STATUS_INVALID_REQUEST;
        } else {
                result = inktag_da_store_begin(
                    &s_screen_config, descriptor->format, descriptor->segment_count,
                    payload[3] & (uint8_t)~INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE,
                    stored_descriptor.content_type, descriptor->segment_size, descriptor->total_size,
                    s_v2_transfer.expected_crc, s_timestamp, stored_descriptor.name,
                    stored_descriptor.replace_slot, &id);
                status = v2_store_status(result);
                s_v2_transfer.resource_id = id;
                response[1] = id;
        }
    }
    /* Cooperative initialization/storage can consume a disconnect. Its
     * cleanup owns the old session; never acknowledge it to a later peer. */
    if (!s_v2_transfer.active) return;
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    put_u16_le(&response[2], INKTAG_DA_TRANSFER_WRITE_BYTES);
    response[4] = descriptor->segment_count;
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}

#include "inktag_da_transfer_write.inc"

__attribute__((noinline)) static void v2_transfer_commit(
    uint8_t opcode, uint8_t transaction_id)
{
    uint8_t object_type;
    uint8_t response[1] = {UINT8_MAX};
    uint32_t crc;
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id ||
        s_v2_transfer.written != s_v2_transfer.total_size) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
        return;
    }
    if (!inktag_da_health_operation_allowed(
            s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ?
                INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY,
            false)) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    crc = s_v2_transfer.running_crc ^ 0xFFFFFFFFu;
    if (crc != s_v2_transfer.expected_crc) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_CHECKSUM_ERROR);
        return;
    }
    object_type = s_v2_transfer.object_type;
    if (object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        uint8_t id = UINT8_MAX;
        inktag_image_store_result_t result;
        /* The permanently Free DA target exposes the complete qualified
         * extent.  The transaction ID still proves that begin allocated a
         * bounded run; commit does not need a second commercial-quota scan. */
        if (s_v2_transfer.resource_id == UINT8_MAX) {
            v2_transfer_reset(true);
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_DENIED);
            return;
        }
        result = inktag_da_store_commit(&id);
        if (result != INKTAG_IMAGE_STORE_OK) {
            v2_transfer_reset(true);
            send_v2_status(opcode, transaction_id, v2_store_status(result));
            return;
        }
        response[0] = id;
        v2_transfer_reset(false);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, response, sizeof(response));
        return;
    }
    memset(&s_v2_transfer, 0, sizeof(s_v2_transfer));
    if (!v2_schedule_work(opcode, transaction_id, EPD_WORK_REFRESH, 0u, 0u)) {
        s_picture_session_active = false;
        epd_gpio_close_if_needed();
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, sizeof(response));
}

static bool v2_resource_replayable(uint8_t id)
{
    inktag_image_store_header_t header;
    return inktag_image_store_get(id, &header) == INKTAG_IMAGE_STORE_OK &&
           header.content_type == INKTAG_IMAGE_CONTENT_IMAGE &&
           screen_config_matches(header.screen_config);
}

__attribute__((noinline)) static void v2_set_display_mode(uint8_t opcode, uint8_t transaction_id,
                                const uint8_t *payload, uint16_t length)
{
    bool scheduled = false;
    inktag_status_v2_t failure = INKTAG_STATUS_NOT_FOUND;
    uint8_t mode;
    if (length < 1u || v2_display_busy() || s_v2_transfer.active) {
        send_v2_status(opcode, transaction_id, length < 1u ? INKTAG_STATUS_INVALID_REQUEST : INKTAG_STATUS_BUSY);
        return;
    }
    mode = payload[0];
    if (!inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_DISPLAY, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (mode == INKTAG_DISPLAY_MODE_DEVICE_INFO) {
        if (length != 1u) failure = INKTAG_STATUS_INVALID_REQUEST;
        else {
            scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                         MODE_DEVICE_INFO, s_timestamp);
        }
    } else if (mode == INKTAG_DISPLAY_MODE_CALENDAR ||
               mode == INKTAG_DISPLAY_MODE_CLOCK) {
        if (length != 1u) failure = INKTAG_STATUS_INVALID_REQUEST;
        else {
            scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_GUI,
                                         mode == INKTAG_DISPLAY_MODE_CALENDAR ?
                                         MODE_CALENDAR : MODE_CLOCK, s_timestamp);
        }
    } else if (mode == INKTAG_DISPLAY_MODE_CLEAR) {
        if (length > 2u) failure = INKTAG_STATUS_INVALID_REQUEST;
        else {
            scheduled = v2_schedule_work(opcode, transaction_id, EPD_WORK_CLEAR,
                                         length > 1u ? (payload[1] != 0u) : 1u, 0u);
        }
    } else if (mode == INKTAG_DISPLAY_MODE_RESOURCE) {
        if (length != 2u) failure = INKTAG_STATUS_INVALID_REQUEST;
        else {
            scheduled = v2_resource_replayable(payload[1]) &&
                        v2_schedule_work(opcode, transaction_id,
                                         EPD_WORK_IMAGE_SHOW, payload[1], 0u);
        }
    } else if (mode == INKTAG_DISPLAY_MODE_SLIDESHOW) {
        uint8_t first = UINT8_MAX;
        uint32_t interval;
        if (length != 1u && length != 5u) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            return;
        }
        interval = length == 5u ? inktag_read_u32_le(&payload[1]) : 3600u;
        if (interval < 30u) interval = 30u;
        if (interval > 86400u) interval = 86400u;
        if (image_store_next_visible_compatible(UINT8_MAX, &first)) {
            scheduled = v2_schedule_work(opcode, transaction_id,
                                         EPD_WORK_IMAGE_SHOW, first, 0u);
            if (scheduled) {
                s_slideshow_interval = interval;
                s_slideshow_active = true;
                s_slideshow_next_at = s_timestamp + interval;
            }
        }
    } else if (mode == INKTAG_DISPLAY_MODE_DIRECT_IMAGE) {
        failure = length == 1u ? INKTAG_STATUS_INVALID_STATE :
                                 INKTAG_STATUS_INVALID_REQUEST;
    } else {
        failure = INKTAG_STATUS_UNSUPPORTED;
    }
    /* Only an accepted explicit mode switch replaces the old show intent.
     * Image admission never claims success: finish_epd_work owns current_id. */
    if (scheduled && mode <= INKTAG_DISPLAY_MODE_CLEAR) slideshow_stop();
    send_v2_status(opcode, transaction_id, scheduled ? INKTAG_STATUS_OK : failure);
}

__attribute__((noinline)) static void v2_display_action(uint8_t opcode, uint8_t transaction_id,
                              uint8_t flags, const uint8_t *payload, uint16_t length)
{
    if (length >= 1u && payload[0] == INKTAG_DISPLAY_ACTION_GHOST_CLEAN) {
        inktag_status_v2_t status;
        if (length != 2u || payload[1] < 1u || payload[1] > 10u)
            status = INKTAG_STATUS_INVALID_REQUEST;
        else if ((flags & INKTAG_REQUEST_FLAG_CONFIRM) == 0u)
            status = INKTAG_STATUS_DENIED;
        else if (s_v2_transfer.active || v2_display_busy())
            status = INKTAG_STATUS_BUSY;
        else if (!inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true))
            status = INKTAG_STATUS_LOW_POWER;
        else if (!v2_schedule_work(opcode, transaction_id, EPD_WORK_GHOST_CLEAN, payload[1], 0u))
            status = INKTAG_STATUS_BUSY;
        else {
            slideshow_stop();
            status = INKTAG_STATUS_OK;
        }
        send_v2_status(opcode, transaction_id, status);
    } else if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_NEXT && s_slideshow_active) {
        uint8_t next = UINT8_MAX;
        bool ok = image_store_next_visible_compatible(
                      s_slideshow_current_id, &next) &&
                  v2_schedule_work(opcode, transaction_id,
                                   EPD_WORK_IMAGE_SHOW, next, 0u);
        send_v2_status(opcode, transaction_id, ok ? INKTAG_STATUS_OK : INKTAG_STATUS_NOT_FOUND);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_PAUSE && s_slideshow_active) {
        s_slideshow_active = false;
        s_slideshow_next_at = 0u;
        if (s_slideshow_current_id != UINT8_MAX)
            record_display_state(EPD_DISPLAY_STATE_PICTURE,
                                 s_slideshow_current_id);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_RESUME &&
               s_slideshow_current_id != UINT8_MAX &&
               v2_resource_replayable(s_slideshow_current_id)) {
        s_slideshow_active = true;
        s_slideshow_next_at = s_timestamp + s_slideshow_interval;
        record_display_state(EPD_DISPLAY_STATE_SLIDESHOW,
                             s_slideshow_current_id);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_REFRESH) {
        if (!inktag_da_health_operation_allowed(
                INKTAG_POWER_OPERATION_DISPLAY, true)) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
            return;
        }
        bool ok = v2_schedule_work(opcode, transaction_id,
                                   EPD_WORK_REFRESH, 0u, 0u);
        send_v2_status(opcode, transaction_id, ok ? INKTAG_STATUS_OK : INKTAG_STATUS_BUSY);
    } else if (payload[0] == INKTAG_DISPLAY_ACTION_SLEEP) {
        if (s_epd != NULL) s_epd->drv->sleep(s_epd);
        epd_gpio_close_if_needed();
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
    }
}

static void v2_cancel(uint8_t opcode, uint8_t transaction_id)
{
    if (s_v2_transfer.active) v2_transfer_reset(true);
    slideshow_stop();
    /* Cancel the current native retry, not future minute/day boundaries. */
    s_clock_minute_refresh_pending = s_clock_day_refresh_pending = false;
    s_automatic_display_retry_tick = 0u;
    if (s_work_pending) {
        s_work_pending = false;
        s_work_type = EPD_WORK_NONE;
        s_work_background = false;
        if (s_v2_work_transaction != 0u)
            send_v2_event(INKTAG_EVENT_COMPLETE, INKTAG_STATUS_CANCELLED,
                          s_v2_work_opcode, s_v2_work_transaction, NULL, 0u);
        s_v2_work_opcode = 0u;
        s_v2_work_transaction = 0u;
    }
    if (s_work_active || s_refresh_waiting) s_cancel_requested = true;
    send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
}

static void v2_delete_resource(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length)
{
    inktag_image_store_result_t result;
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (s_v2_transfer.active) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    result = inktag_image_store_get(
        payload[0], &(inktag_image_store_header_t){0});
    if (result != INKTAG_IMAGE_STORE_OK) {
        send_v2_status(opcode, transaction_id, v2_store_status(result));
        return;
    }
    slideshow_stop();
    result = inktag_da_store_delete(payload[0]);
    send_v2_status(opcode, transaction_id, v2_store_status(result));
    if (result == INKTAG_IMAGE_STORE_OK)
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, payload, 1u);
}

static void v2_clear_resources(uint8_t opcode, uint8_t transaction_id,
                               const uint8_t *payload, uint16_t length,
                               bool clear_user_settings)
{
    uint16_t removed = 0u;
    inktag_image_store_result_t result;
    inktag_status_v2_t status;
    uint8_t response[2];
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (s_v2_transfer.active || v2_display_busy()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    if (clear_user_settings && !epd_user_data_settings_prepare_and_verify()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    if (payload[0] == INKTAG_RESOURCE_CLEAR_ALL) {
        s_display_state_persisted = false;
        record_display_state(EPD_DISPLAY_STATE_DEVICE_INFO,
                             EPD_DISPLAY_STATE_NO_SOURCE);
    }
    if (payload[0] == INKTAG_RESOURCE_CLEAR_ALL &&
        !s_display_state_persisted) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    slideshow_stop();
    result = payload[0] == INKTAG_RESOURCE_CLEAR_ALL ?
             image_store_clear_visible(false, &removed) :
             payload[0] == INKTAG_RESOURCE_CLEAR_INCOMPATIBLE ?
             image_store_clear_visible(true, &removed) :
             INKTAG_IMAGE_STORE_BAD_ARGUMENT;
    status = v2_store_status(result);
    if (result == INKTAG_IMAGE_STORE_OK && clear_user_settings &&
        !epd_user_data_settings_clear_and_verify())
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    if (status == INKTAG_STATUS_OK && clear_user_settings)
        s_display_state_persisted = false;
    put_u16_le(response, removed);
    send_v2_response(opcode, transaction_id, status,
                     status == INKTAG_STATUS_OK ? response : NULL,
                     status == INKTAG_STATUS_OK ? sizeof(response) : 0u);
    if (status == INKTAG_STATUS_OK)
        send_v2_event(INKTAG_EVENT_RESOURCE_CHANGED, INKTAG_STATUS_OK,
                      opcode, transaction_id, NULL, 0u);
}

__attribute__((noinline)) static void v2_system_action(uint8_t opcode, uint8_t transaction_id,
                             uint8_t flags, const uint8_t *payload, uint16_t length)
{
    const inktag_lifecycle_capabilities_t capabilities = {
        .reset = true,
        .timed_sleep = false,
        .factory_reset = false,
        .ota_recovery = true,
        .reset_settings = INKTAG_BOARD_LIFECYCLE_V2_IMPLEMENTED != 0,
        .clear_user_data = INKTAG_BOARD_LIFECYCLE_V2_IMPLEMENTED != 0,
    };
    inktag_lifecycle_plan_t plan;
    inktag_status_v2_t status = inktag_lifecycle_plan_request(
        flags, payload, length, &capabilities, &plan);
    if (status != INKTAG_STATUS_OK) {
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    if (v2_display_busy() || s_v2_transfer.active || s_system_action_pending != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
        return;
    }
    if ((plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA ||
         plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) &&
        !inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, false)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_CLEAR_USER_DATA) {
        const uint8_t clear_all = INKTAG_RESOURCE_CLEAR_ALL;
        v2_clear_resources(opcode, transaction_id, &clear_all, 1u, true);
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_RESET_SETTINGS) {
        if (s_pin_unconfirmed) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        status = epd_settings_reset_and_verify() ? INKTAG_STATUS_OK :
                                                   INKTAG_STATUS_PERSISTENCE_ERROR;
        /* Reset removes ID7, including the pin override. False may follow a
         * committed deletion: invalidate cached routing until a new connection
         * obtains a fresh read at safe idle. Never replay the unknown reset. */
        if (status != INKTAG_STATUS_OK) s_pin_unconfirmed = true;
        send_v2_status(opcode, transaction_id, status);
        if (status == INKTAG_STATUS_OK) {
            slideshow_stop();
            s_system_action_pending = plan.action;
            s_system_action_tick = inktag_da_now_tick();
        }
        return;
    }
    if (plan.effect == INKTAG_LIFECYCLE_EFFECT_OTA_RECOVERY &&
        !inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_OTA, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (s_time_valid &&
        !epd_time_state_write(s_timestamp, s_timezone_minutes)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
        return;
    }
    s_system_action_pending = plan.action;
    s_system_action_tick = inktag_da_now_tick();
    send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
}

/* Native wiring is already loaded and used by the panel backend. This is the
 * shared release-v1 block, not another GPIO store or permission to drive pins.
 * BS remains protected board wiring; LED and UART roles are not available. */
__attribute__((noinline)) static void v2_encode_pin_block(uint8_t *wire, const epd_config_t *config)
{
    wire[0] = INKTAG_RELEASE_PIN_VERSION;
    wire[1] = 0u;
    put_u16_le(&wire[2], config->enable_active_low ? 0x40u : 0u);
    memcpy(&wire[4], config, 6u);
    wire[10] = config->en_pin;
    memset(&wire[11], 0xFF, 5u);
}

__attribute__((noinline)) static void v2_get_io_config(
    uint8_t opcode, uint8_t transaction_id,
    const uint8_t *payload, uint16_t length)
{
    uint8_t response[4u + 2u * INKTAG_RELEASE_PIN_BYTES];
    _Static_assert(offsetof(epd_config_t, en_pin) == 10u,
                   "DA native panel tuple changed");
    if (length != 1u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (s_runtime_board_kind != INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR &&
        s_runtime_board_kind != INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    if (payload[0] == INKTAG_RELEASE_PIN_GET_SELECTOR) {
        if (s_pin_unconfirmed) {
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            return;
        }
        response[0] = INKTAG_RELEASE_PIN_REPORT_VERSION;
        response[1] = INKTAG_PIN_REPORT_DEFAULT_VALID |
                      INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED |
                      (s_pin_override ? INKTAG_PIN_REPORT_OVERRIDE_PRESENT : 0u);
        put_u16_le(&response[2], s_pin_writes_allowed ? 0x7fu : 0u);
        v2_encode_pin_block(&response[4], &s_pin_defaults);
        v2_encode_pin_block(&response[20], &s_config);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         response, sizeof(response));
    } else if (payload[0] == INKTAG_RELEASE_AUX_GET_SELECTOR) {
        /* Shared disabled AUX snapshot for client conflict checks. No IO,
         * button or buzzer actuator capability, new record or SET is added. */
        static const uint8_t disabled[INKTAG_RELEASE_AUX_BYTES] = {
            INKTAG_RELEASE_AUX_VERSION, 0xFFu, 0u, 0xFFu, 0u, 0xFFu,
            0u, 0u, 0xFFu, 0xFFu, 0u, 0u
        };
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                         disabled, sizeof(disabled));
    } else {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
    }
}

__attribute__((noinline)) static void v2_set_io_config(
    uint8_t opcode, uint8_t transaction_id,
    const uint8_t *payload, uint16_t length)
{
    epd_config_t next = s_pin_defaults;
    const uint8_t *wire = NULL;
    uint8_t status = INKTAG_STATUS_OK;
    if (length < 1u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (payload[0] != INKTAG_RELEASE_PIN_GET_SELECTOR)
        status = INKTAG_STATUS_UNSUPPORTED;
    else if (length < 2u || (payload[1] == 0u ? length != 2u :
         payload[1] != 1u || length != 2u + INKTAG_PIN_CONFIG_SIZE))
        status = INKTAG_STATUS_INVALID_REQUEST;
    else if (payload[1] == 1u &&
             !inktag_da_pins_decode(payload + 2u, INKTAG_PIN_CONFIG_SIZE, &next))
        status = INKTAG_STATUS_INVALID_REQUEST;
    else if (s_runtime_board_kind != INKTAG_BOARD_DA_AESL29_STOCK_SELECTOR &&
             s_runtime_board_kind != INKTAG_BOARD_DA_AESL42_STOCK_SELECTOR)
        status = INKTAG_STATUS_UNSUPPORTED;
    else if (!s_pin_writes_allowed)
        status = INKTAG_STATUS_INVALID_STATE;
    else if (s_pin_unconfirmed)
        status = INKTAG_STATUS_PERSISTENCE_ERROR;
    else if (pin_config_busy()) status = INKTAG_STATUS_BUSY;
    else {
        if (payload[1] == 1u) wire = payload + 2u;
        if (!epd_dialog_pin_override_write(wire) || !pin_config_publish(&next)) {
            /* False may mean committed-but-unconfirmed. Keep old RAM tuple,
             * block further writes/display, reconcile on a later connection. */
            s_pin_unconfirmed = true;
            status = INKTAG_STATUS_PERSISTENCE_ERROR;
        } else s_pin_override = wire != NULL;
    }
    if (status == INKTAG_STATUS_OK)
        v2_get_io_config(opcode, transaction_id, payload, 1u);
    else send_v2_status(opcode, transaction_id, status);
}

static void v2_unsupported(uint8_t opcode, uint8_t transaction_id)
{
    send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
}

static bool v2_free_only_command_unsupported(uint8_t opcode)
{
    /* Registered but omitted facilities must not look like unknown wire
     * commands. DA has no password/owner backend. The immutable split font
     * resource is internal, not a user font bank or a no-bank wire service. */
    /* Share one 16-byte set between wire admission and the defensive worker
     * check. Symbolic bits retain the registered IDs without linking services. */
#define OMITTED_BIT(command) (UINT32_C(1) << ((command) & 31u))
    static const uint32_t omitted[4] = {
        OMITTED_BIT(INKTAG_CMD_GET_GUI_ASSET_INFO) |
        OMITTED_BIT(INKTAG_CMD_SET_RESOURCE_ALIAS) |
        OMITTED_BIT(INKTAG_CMD_SET_IMAGE_METADATA),
        OMITTED_BIT(INKTAG_CMD_GET_INPUT_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_SET_INPUT_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_GET_INDICATOR_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_SET_INDICATOR_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_GET_NFC_CAPABILITIES) |
        OMITTED_BIT(INKTAG_CMD_GET_NFC_STATUS) |
        OMITTED_BIT(INKTAG_CMD_GET_NFC_NDEF) |
        OMITTED_BIT(INKTAG_CMD_SET_NFC_NDEF) |
        OMITTED_BIT(INKTAG_CMD_GET_LAYOUT_CAPABILITIES) |
        OMITTED_BIT(INKTAG_CMD_VALIDATE_LAYOUT) |
        OMITTED_BIT(INKTAG_CMD_ACTIVATE_LAYOUT) |
        OMITTED_BIT(INKTAG_CMD_GET_LAYOUT_STATUS),
        OMITTED_BIT(INKTAG_CMD_SET_ACTIVATION_CODE) |
        OMITTED_BIT(INKTAG_CMD_GET_SECURITY_INFO) |
        OMITTED_BIT(INKTAG_CMD_GET_ACTIVATION_REQUEST) |
        OMITTED_BIT(INKTAG_CMD_GET_OWNER_CHALLENGE) |
        OMITTED_BIT(INKTAG_CMD_AUTHORIZE_OWNER_ACTION) |
        OMITTED_BIT(INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS) |
        OMITTED_BIT(INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE) |
        OMITTED_BIT(INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD) |
        OMITTED_BIT(INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO) |
        OMITTED_BIT(INKTAG_CMD_GET_FIND_CAPABILITIES) |
        OMITTED_BIT(INKTAG_CMD_GET_FIND_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_SET_FIND_CONFIG) |
        OMITTED_BIT(INKTAG_CMD_FIND_ACTION) |
        OMITTED_BIT(INKTAG_CMD_GET_FIND_OWNER_STATUS) |
        OMITTED_BIT(INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION) |
        OMITTED_BIT(INKTAG_CMD_STAGE_FIND_OWNER_ROTATION) |
        OMITTED_BIT(INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION) |
        OMITTED_BIT(INKTAG_CMD_ABORT_FIND_OWNER_ROTATION),
        OMITTED_BIT(INKTAG_CMD_OPEN_DEBUG_SESSION) |
        OMITTED_BIT(INKTAG_CMD_DEBUG_EXECUTE) |
        OMITTED_BIT(INKTAG_CMD_DEBUG_READ) |
        OMITTED_BIT(INKTAG_CMD_CLOSE_DEBUG_SESSION) |
        OMITTED_BIT(INKTAG_CMD_ABORT_DEBUG_OPERATION)
    };
#undef OMITTED_BIT
    return opcode < 128u &&
           ((omitted[opcode >> 5] >> (opcode & 31u)) & 1u) != 0u;
}

/* Frame/tag validation precedes this read-only admission. Panel wiring and
 * ordinary identity/display settings remain Free; custom inputs/actuators
 * have no DA backend and must not enter the worker or sample Flash power. */
static bool v2_free_only_request_unsupported(
    uint8_t opcode, uint8_t flags, const uint8_t *payload, uint16_t length)
{
    if (v2_free_only_command_unsupported(opcode)) return true;
    if (length == 0u) return false;
    switch (opcode) {
        case INKTAG_CMD_SYSTEM_ACTION:
            return length == 1u &&
                (payload[0] == INKTAG_SYSTEM_ACTION_FACTORY_RESET ||
                 payload[0] == INKTAG_SYSTEM_ACTION_SLEEP);
        case INKTAG_CMD_SET_IO_CONFIG:
            return payload[0] != INKTAG_RELEASE_PIN_GET_SELECTOR;
        case INKTAG_CMD_GET_SETTINGS:
        case INKTAG_CMD_SET_SETTINGS:
            return (opcode == INKTAG_CMD_GET_SETTINGS ? length == 1u : length >= 2u) &&
                payload[0] != INKTAG_SETTINGS_SECTION_IDENTITY &&
                payload[0] != INKTAG_SETTINGS_SECTION_DISPLAY;
        case INKTAG_CMD_RUN_TEST:
            return flags == 0u &&
                (payload[0] == INKTAG_TEST_BUZZER ||
                 payload[0] == INKTAG_TEST_INPUT_SIMULATE);
        default:
            return false;
    }
}

/* This is a normalized queue dispatcher, not a wire entry point: GATT checks
 * the release tag and rejects omitted facilities before queueing, retaining
 * only semantic flags. Keep
 * protocol dispatch in its own stack frame. The DA14585 application has
 * one small RAM stack shared by protocol and display work.  If LTO folds this
 * large switch into inktag_da_process(), its response scratch stays live while
 * that caller later enters the native renderer, making two mutually exclusive
 * workloads consume stack at the same time. */
__attribute__((noinline)) static void process_packet(uint8_t *data,
                                                     uint16_t length)
{
    uint8_t opcode;
    uint8_t transaction_id;
    uint8_t flags;
    uint8_t *payload;
    uint16_t payload_length;
    if (data == NULL || length < INKTAG_PROTOCOL_V2_HEADER_SIZE) return;
    opcode = data[0];
    transaction_id = data[1];
    flags = data[2];
    payload = &data[INKTAG_PROTOCOL_V2_HEADER_SIZE];
    payload_length = (uint16_t)(length - INKTAG_PROTOCOL_V2_HEADER_SIZE);
    if (transaction_id == 0u ||
        (flags & ~(INKTAG_REQUEST_FLAG_CONFIRM | INKTAG_REQUEST_FLAG_FORCE)) != 0u) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    if (v2_free_only_command_unsupported(opcode) ||
        (opcode == INKTAG_CMD_SYSTEM_ACTION && payload_length == 1u &&
         (payload[0] == INKTAG_SYSTEM_ACTION_FACTORY_RESET ||
          payload[0] == INKTAG_SYSTEM_ACTION_SLEEP))) {
        v2_unsupported(opcode, transaction_id);
        return;
    }
    if ((opcode == INKTAG_CMD_SET_IO_CONFIG ||
         inktag_protocol_command_mutates_persistent_state(
            opcode, payload, payload_length)) &&
        !inktag_da_health_operation_allowed(
            INKTAG_POWER_OPERATION_FLASH, true)) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    switch (opcode) {
        case INKTAG_CMD_HELLO: v2_send_hello(opcode, transaction_id); break;
        case INKTAG_CMD_GET_CAPABILITIES: v2_send_capabilities(opcode, transaction_id); break;
        case INKTAG_CMD_GET_DEVICE_INFO: v2_send_device_info(opcode, transaction_id); break;
        case INKTAG_CMD_GET_STATUS: v2_send_status(opcode, transaction_id); break;
        case INKTAG_CMD_GET_IO_CONFIG:
            v2_get_io_config(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_SET_IO_CONFIG:
            v2_set_io_config(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_GET_ACTIVATION_STATUS:
            v2_send_free_status(opcode, transaction_id, payload_length); break;
        case INKTAG_CMD_GET_TIME: v2_send_time(opcode, transaction_id); break;
        case INKTAG_CMD_SET_TIME:
            if (payload_length != 7u)
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            else v2_set_time(opcode, transaction_id, payload, 0u);
            break;
        case INKTAG_CMD_LIST_TESTS:
            if (payload_length != 0u)
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            else v2_list_tests(opcode, transaction_id);
            break;
        case INKTAG_CMD_RUN_TEST:
            if (flags != 0u)
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            else v2_run_test(opcode, transaction_id, payload, payload_length);
            break;
        case INKTAG_CMD_GET_TEST_STATUS:
            if (payload_length != 0u)
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            else v2_send_test_status(opcode, transaction_id);
            break;
        case INKTAG_CMD_GET_SCREEN_PROFILE: v2_send_screen(opcode, transaction_id); break;
        case INKTAG_CMD_SET_SCREEN_PROFILE: {
            epd_screen_config_t requested;
            if (v2_display_busy() || s_v2_transfer.active) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_BUSY);
            } else if (inktag_da_screen_profile_omitted(payload, payload_length)) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
            } else if (epd_screen_config_decode_exact(payload, payload_length, &requested) !=
                           EPD_SCREEN_STATUS_OK || epd_find(&requested) == NULL) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
            } else if (!inktag_da_store_profile_allowed(&requested)) {
                /* Explicit clear-all must persist invalid heads before a
                 * different stride can expose any new arithmetic addresses. */
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INCOMPATIBLE);
            } else if (!epd_screen_config_write(&requested)) {
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_PERSISTENCE_ERROR);
            } else {
                slideshow_stop();
                epd_gpio_close_if_needed();
                s_screen_config = requested;
                s_epd = epd_find(&s_screen_config);
                (void)inktag_image_store_init();
                s_panel_temperature_valid = false;
                epd_gui_screen_info_update();
                record_display_state(EPD_DISPLAY_STATE_PICTURE,
                                     EPD_DISPLAY_STATE_NO_SOURCE);
                v2_send_screen(opcode, transaction_id);
                send_v2_event(INKTAG_EVENT_DEVICE_CHANGED, INKTAG_STATUS_OK,
                              opcode, transaction_id, NULL, 0u);
            }
        } break;
        case INKTAG_CMD_SET_DISPLAY_MODE:
            v2_set_display_mode(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_DISPLAY_ACTION:
            v2_display_action(opcode, transaction_id, flags, payload, payload_length); break;
        case INKTAG_CMD_CANCEL: v2_cancel(opcode, transaction_id); break;
        case INKTAG_CMD_GET_SETTINGS:
            v2_get_settings(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_SET_SETTINGS:
            v2_set_settings(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_SYSTEM_ACTION:
            v2_system_action(opcode, transaction_id, flags, payload, payload_length); break;
        case INKTAG_CMD_GET_HEALTH:
            v2_send_health(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_BEGIN:
            v2_transfer_begin(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_WRITE:
            v2_transfer_write(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_TRANSFER_COMMIT:
            if (payload_length == 0u) v2_transfer_commit(opcode, transaction_id);
            else send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
            break;
        case INKTAG_CMD_TRANSFER_ABORT:
            if (!s_v2_transfer.active)
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
            else {
                v2_transfer_reset(true);
                send_v2_status(opcode, transaction_id, INKTAG_STATUS_OK);
            }
            break;
        case INKTAG_CMD_GET_TRANSFER_STATUS: {
            uint8_t response[11]; /* All 11 wire bytes are assigned below. */
            response[0] = s_v2_transfer.active ? 1u : 0u;
            response[1] = s_v2_transfer.object_type;
            response[2] = s_v2_transfer.transaction_id;
            put_u32_le(&response[3], s_v2_transfer.written);
            put_u32_le(&response[7], s_v2_transfer.total_size);
            send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                             response, sizeof(response));
        } break;
        case INKTAG_CMD_GET_RESOURCE_INFO:
                       v2_send_resource_info(opcode, transaction_id); break;
        case INKTAG_CMD_LIST_RESOURCES:
            v2_list_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_READ_RESOURCE:
                       v2_read_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_DELETE_RESOURCE:
            v2_delete_resource(opcode, transaction_id, payload, payload_length); break;
        case INKTAG_CMD_CLEAR_RESOURCES:
            v2_clear_resources(opcode, transaction_id, payload, payload_length,
                               false); break;
        default:
            send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNKNOWN_COMMAND);
            break;
    }
}

static void v2_service(void)
{
    if (s_v2_transfer.active &&
        v2_elapsed_ms(s_v2_transfer.last_activity_tick) >= INKTAG_DA_TRANSFER_LEASE_MS) {
        uint8_t opcode = INKTAG_CMD_TRANSFER_ABORT;
        uint8_t transaction = s_v2_transfer.transaction_id;
        v2_transfer_reset(true);
        send_v2_event(INKTAG_EVENT_ERROR, INKTAG_STATUS_TIMEOUT,
                      opcode, transaction, NULL, 0u);
    }
    if (s_system_action_pending == 0u) return;
    /* KEEP_POWERED bypasses SDK6's outer-loop watchdog reload. Notification
     * delivery is asynchronous, so feed while waiting, but never indefinitely.
     * A missing CFM cancels the deferred action; it must not trigger a reset
     * that the host has no evidence was accepted. A disconnect drains CFM as
     * before. The ordinary common TIMEOUT event keeps the wire contract. */
    app_feed_wdt();
    uint32_t elapsed = v2_elapsed_ms(s_system_action_tick);
    if (s_notify_outstanding != 0u && elapsed >= 5000u) {
        s_system_action_pending = 0u;
        send_v2_event(INKTAG_EVENT_ERROR, INKTAG_STATUS_TIMEOUT,
                      INKTAG_CMD_SYSTEM_ACTION, 0u, NULL, 0u);
        return;
    }
    if (elapsed >= 250u && s_notify_outstanding == 0u) {
        /* Reset/recovery entry must not outrun the response notification.
         * SDK6 accepts the application message before the controller has
         * actually transmitted its GATT notification; a fixed delay alone
         * can therefore reset the radio first on Windows connection timing.
         * The confirmation callback drains this counter. A disconnect also
         * resets it, allowing an already accepted destructive action to
         * continue instead of leaving a half-accepted host transaction. */
        uint8_t action = s_system_action_pending;
        s_system_action_pending = 0u;
        if (action == INKTAG_SYSTEM_ACTION_OTA_RECOVERY) {
            if (!inktag_da_enter_fixed_recovery()) {
                send_v2_event(INKTAG_EVENT_ERROR, INKTAG_STATUS_IO_ERROR,
                              INKTAG_CMD_SYSTEM_ACTION, 0u, NULL, 0u);
                return;
            }
        }
        platform_reset(RESET_AFTER_SUOTA_UPDATE);
    }
}
