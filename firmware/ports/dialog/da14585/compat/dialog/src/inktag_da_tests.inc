/* Bounded release tests. Reuse native persistence and the ordinary GUI worker;
 * no debug session, raw GPIO access, new RAM state or alternate renderer. */
__attribute__((noinline)) static void v2_set_time(
    uint8_t opcode, uint8_t transaction_id, const uint8_t *wire, uint8_t mode)
{
    uint32_t local = inktag_read_u32_le(wire) +
        (int32_t)(int16_t)read_u16_le(wire + 4u) * 60;
    int16_t timezone = (int16_t)read_u16_le(wire + 4u);
    uint8_t status = INKTAG_STATUS_PERSISTENCE_ERROR;
    if (wire[6] > 1u || timezone < -840 || timezone > 840 ||
        local < DIALOG_TIME_MIN_VALID || local > DIALOG_TIME_MAX_VALID) {
        status = INKTAG_STATUS_INVALID_REQUEST;
    } else if (mode != 0u && (!inktag_da_health_operation_allowed(
                   INKTAG_POWER_OPERATION_FLASH, true) ||
               !inktag_da_health_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, true))) {
        status = INKTAG_STATUS_LOW_POWER;
    } else {
        inktag_display_settings_t next_settings = s_display_settings;
        epd_display_state_t next_state = s_display_state;
        next_settings.week_start = wire[6];
        next_state.reserved = (uint16_t)((next_state.reserved &
            ~EPD_DISPLAY_STATE_WEEK_START_MASK) |
            ((uint16_t)wire[6] << EPD_DISPLAY_STATE_WEEK_START_SHIFT));
        if (epd_dialog_display_settings_write(&next_state, &next_settings)) {
            if (!epd_time_state_write(local, timezone)) {
                (void)epd_dialog_display_settings_write(
                    &s_display_state, &s_display_settings);
            } else if (mode != 0u && !v2_schedule_work(opcode, transaction_id,
                           EPD_WORK_GUI, mode == INKTAG_DISPLAY_MODE_CALENDAR ?
                           MODE_CALENDAR : MODE_CLOCK, local)) {
                /* Scheduling is last. A refusal never publishes the new RAM
                 * clock, and no worker was queued. Failed persistent rollback
                 * is an uncertain storage outcome, never a successful test. */
                bool restored = epd_time_state_write(s_timestamp, s_timezone_minutes);
                if (!epd_dialog_display_settings_write(
                        &s_display_state, &s_display_settings)) restored = false;
                status = restored ? INKTAG_STATUS_BUSY : INKTAG_STATUS_PERSISTENCE_ERROR;
            } else {
                s_display_settings = next_settings;
                s_display_state = next_state;
                s_config.week_start = wire[6];
                epd_config_write(&s_config);
                s_timezone_minutes = timezone;
                set_timestamp(local);
                v2_send_time(opcode, transaction_id);
                return;
            }
        }
    }
    send_v2_status(opcode, transaction_id, status);
}

static void v2_list_tests(uint8_t opcode, uint8_t transaction_id)
{
    uint8_t response[5] = {1u, 2u, INKTAG_TEST_TIME_SHIFT, INKTAG_TEST_TIME_SET};
    if (inktag_da_voltage_telemetry_available()) {
        response[1] = 3u;
        response[4] = INKTAG_TEST_ADC_READ;
    }
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                     response, (uint16_t)(2u + response[1]));
}

__attribute__((noinline)) static void v2_run_test(
    uint8_t opcode, uint8_t transaction_id, const uint8_t *payload, uint16_t length)
{
    uint8_t status = INKTAG_STATUS_UNSUPPORTED;
    if (length == 0u) status = INKTAG_STATUS_INVALID_REQUEST;
    else if (payload[0] == INKTAG_TEST_ADC_READ) {
        if (length != 2u || payload[1] > 2u) status = INKTAG_STATUS_INVALID_REQUEST;
        else if (payload[1] == 1u && inktag_da_voltage_telemetry_available()) {
            if (v2_display_busy() || s_v2_transfer.active || s_system_action_pending)
                status = INKTAG_STATUS_BUSY;
            else {
                uint16_t value = inktag_da_read_battery_mv();
                uint8_t response[6] = {1u, 1u, value != 0u, 0u, 0u, 0u};
                put_u16_le(response + 3u, value);
                send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK,
                                 response, sizeof(response));
                send_v2_event(INKTAG_EVENT_COMPLETE, INKTAG_STATUS_OK,
                              opcode, transaction_id, payload + 1u, 1u);
                return;
            }
        }
    } else if (payload[0] == INKTAG_TEST_TIME_SHIFT || payload[0] == INKTAG_TEST_TIME_SET) {
        if (length != (payload[0] == INKTAG_TEST_TIME_SHIFT ? 6u : 9u) ||
            (payload[1] != INKTAG_DISPLAY_MODE_CALENDAR &&
             payload[1] != INKTAG_DISPLAY_MODE_CLOCK)) {
            status = INKTAG_STATUS_INVALID_REQUEST;
        } else if (v2_display_busy() || s_v2_transfer.active || s_system_action_pending) {
            status = INKTAG_STATUS_BUSY;
        } else if (!epd_ensure_screen()) {
            status = epd_unavailable_status();
        } else if (payload[0] == INKTAG_TEST_TIME_SET) {
            v2_set_time(opcode, transaction_id, payload + 2u, payload[1]);
            return;
        } else if (!s_time_valid) {
            status = INKTAG_STATUS_INVALID_STATE;
        } else {
            int32_t delta = (int32_t)inktag_read_u32_le(payload + 2u);
            if (delta == 0 || delta < -(366 * 86400) || delta > 366 * 86400) {
                status = INKTAG_STATUS_INVALID_REQUEST;
            } else {
                uint8_t wire[7];
                v2_encode_time(wire);
                put_u32_le(wire, inktag_read_u32_le(wire) + delta);
                v2_set_time(opcode, transaction_id, wire, payload[1]);
                return;
            }
        }
    }
    send_v2_status(opcode, transaction_id, status);
}

static void v2_send_test_status(uint8_t opcode, uint8_t transaction_id)
{
    const uint8_t response[3] = {1u, v2_display_busy() ? 1u : 0u, v2_current_display_mode()};
    send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
}
