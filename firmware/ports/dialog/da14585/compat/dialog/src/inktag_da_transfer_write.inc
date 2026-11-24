/* Included by the shipping DA dispatcher and its actual-path host harness.
 * Wire offset, size and CRC always refer to decoded bytes. Flash records stay
 * uncompressed. Common PackBits validates the entire block before the sink. */
typedef struct {
    uint32_t offset;
    uint32_t crc;
    inktag_status_v2_t status;
} v2_transfer_sink_context_t;

static bool v2_transfer_accept_decoded(void *user, uint8_t *data,
                                     uint16_t data_length)
{
    v2_transfer_sink_context_t *context = user;
    uint32_t next_crc = v2_crc32_update(context->crc, data, data_length);
    if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) {
        context->status = v2_store_status(inktag_image_store_append(data, data_length));
    } else if (s_epd == NULL) {
        context->status = INKTAG_STATUS_INVALID_STATE;
    } else {
        uint16_t consumed = 0u;
        while (consumed < data_length) {
            uint32_t absolute = context->offset + consumed;
            uint8_t segment = absolute < s_v2_transfer.segment_size[0] ? 0u : 1u;
            uint32_t segment_start = segment == 0u ? 0u : s_v2_transfer.segment_size[0];
            uint32_t segment_end = segment_start + s_v2_transfer.segment_size[segment];
            uint16_t chunk = (uint16_t)(data_length - consumed);
            bool black = (s_v2_transfer.black_mask & (1u << segment)) != 0u;
            if (absolute + chunk > segment_end) chunk = (uint16_t)(segment_end - absolute);
            if (chunk == 0u || !inktag_brand_overlay_apply_oriented(
                    &s_screen_config, s_v2_transfer.format, segment,
                    absolute - segment_start, &data[consumed], chunk,
                    INKTAG_BRAND_EDITION_FREE,
                    inktag_image_reading_rotation(s_v2_transfer.black_mask))) {
                context->status = INKTAG_STATUS_INCOMPATIBLE;
                break;
            }
            if (!EPD_DriverWriteRam(s_epd, absolute == segment_start, black,
                                   &data[consumed], (uint8_t)chunk)) {
                context->status = INKTAG_STATUS_INVALID_STATE;
                break;
            }
            consumed = (uint16_t)(consumed + chunk);
        }
        if (!EPD_DialogSpiReady() || EPD_DialogSpiLastError() != 0)
            context->status = INKTAG_STATUS_IO_ERROR;
    }
    if (context->status != INKTAG_STATUS_OK) return false;
    /* One compressed WRITE may expand to over 13 KiB. Keep the existing
     * bounded sink chunks cooperative, just like native/image-store replay;
     * waiting until the whole WRITE returns can starve BLE and the watchdog.
     * Stored append already services BLE in its Flash sink. */
    if (s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME)
        app_service_ble();
    /* The scheduler/Flash sink can deliver disconnect and abort this transfer.
     * Never decode another block against the cleared descriptor, resurrect its
     * offset/CRC, or acknowledge a WRITE after ownership has been released. */
    if (!s_v2_transfer.active) {
        context->status = INKTAG_STATUS_INVALID_STATE;
        return false;
    }
    context->crc = next_crc;
    context->offset += data_length;
    return true;
}

__attribute__((noinline)) static void v2_transfer_write(uint8_t opcode, uint8_t transaction_id,
                              uint8_t *payload, uint16_t length)
{
    bool compressed;
    uint16_t data_length;
    v2_transfer_sink_context_t context;
    inktag_status_v2_t status = INKTAG_STATUS_OK;
    if (!s_v2_transfer.active || transaction_id != s_v2_transfer.transaction_id) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_STATE);
        return;
    }
    if (!inktag_da_health_operation_allowed(
            s_v2_transfer.object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE ?
                INKTAG_POWER_OPERATION_FLASH : INKTAG_POWER_OPERATION_DISPLAY, false)) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_LOW_POWER);
        return;
    }
    if (length <= 4u || length - 4u > INKTAG_DA_TRANSFER_WRITE_BYTES ||
        inktag_read_u32_le(payload) != s_v2_transfer.written) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    compressed = (s_v2_transfer.black_mask &
                  INKTAG_TRANSFER_DESCRIPTOR_FLAG_PACKBITS_RLE) != 0u;
    if (compressed && !inktag_da_transfer_packbits_available()) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_UNSUPPORTED);
        return;
    }
    data_length = compressed ? (length > 6u ? read_u16_le(&payload[4]) : 0u)
                             : (uint16_t)(length - 4u);
    if (data_length == 0u || s_v2_transfer.written > s_v2_transfer.total_size ||
        data_length > s_v2_transfer.total_size - s_v2_transfer.written) {
        send_v2_status(opcode, transaction_id, INKTAG_STATUS_INVALID_REQUEST);
        return;
    }
    context.offset = s_v2_transfer.written;
    context.crc = s_v2_transfer.running_crc;
    context.status = INKTAG_STATUS_OK;
    if (compressed) {
        if (!inktag_packbits_stream_block(&payload[6], (uint8_t)(length - 6u),
                data_length, v2_transfer_accept_decoded, &context))
            status = context.status != INKTAG_STATUS_OK ? context.status
                                                       : INKTAG_STATUS_INVALID_REQUEST;
    } else if (!v2_transfer_accept_decoded(&context, &payload[4], data_length)) {
        status = context.status;
    }
    if (status != INKTAG_STATUS_OK) {
        v2_transfer_reset(true);
        send_v2_status(opcode, transaction_id, status);
        return;
    }
    /* Publish offset/CRC only after every decoded sink chunk succeeds. */
    s_v2_transfer.running_crc = context.crc;
    s_v2_transfer.written = context.offset;
    s_v2_transfer.last_activity_tick = inktag_da_now_tick();
    {
        uint8_t response[4];
        put_u32_le(response, s_v2_transfer.written);
        send_v2_response(opcode, transaction_id, INKTAG_STATUS_OK, response, sizeof(response));
    }
}
