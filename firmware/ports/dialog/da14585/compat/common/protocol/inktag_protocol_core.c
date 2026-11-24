#include "inktag_protocol_core.h"

#include <stddef.h>
#include <string.h>

#include "inktag_protocol_v2.h"

#ifndef INKTAG_PROTOCOL_FREE_ONLY_TARGET
#define INKTAG_PROTOCOL_FREE_ONLY_TARGET 0
#endif

bool inktag_capability_available(const inktag_capability_gate_t *gate) {
    return gate != NULL && gate->implemented && gate->backend_available &&
           gate->board_qualified && gate->runtime_available;
}

uint32_t inktag_capability_add(uint32_t capabilities, uint32_t capability,
                               const inktag_capability_gate_t *gate) {
    return inktag_capability_available(gate) ? capabilities | capability :
                                               capabilities;
}

bool inktag_protocol_command_mutates_persistent_state(
    uint8_t opcode, const uint8_t *payload, uint16_t payload_length) {
    switch (opcode) {
        case INKTAG_CMD_SET_TIME:
        case INKTAG_CMD_SET_SCREEN_PROFILE:
        case INKTAG_CMD_SET_SETTINGS:
#if !INKTAG_PROTOCOL_FREE_ONLY_TARGET
        case INKTAG_CMD_SET_INPUT_CONFIG:
        case INKTAG_CMD_SET_IO_CONFIG:
        case INKTAG_CMD_SET_INDICATOR_CONFIG:
        case INKTAG_CMD_SET_NFC_NDEF:
        case INKTAG_CMD_SET_ACTIVATION_CODE:
        case INKTAG_CMD_ACTIVATE_LAYOUT:
        case INKTAG_CMD_SET_FIND_CONFIG:
        case INKTAG_CMD_FIND_ACTION:
        case INKTAG_CMD_BEGIN_FIND_OWNER_ROTATION:
        case INKTAG_CMD_STAGE_FIND_OWNER_ROTATION:
        case INKTAG_CMD_COMMIT_FIND_OWNER_ROTATION:
        case INKTAG_CMD_ABORT_FIND_OWNER_ROTATION:
#endif
            return true;
        case INKTAG_CMD_SYSTEM_ACTION:
            if (payload == NULL || payload_length == 0u) return false;
            return payload[0] == INKTAG_SYSTEM_ACTION_SLEEP ||
                   payload[0] == INKTAG_SYSTEM_ACTION_FACTORY_RESET ||
                   payload[0] == INKTAG_SYSTEM_ACTION_RESET_SETTINGS ||
                   payload[0] == INKTAG_SYSTEM_ACTION_CLEAR_USER_DATA;
        default:
            return false;
    }
}

inktag_status_v2_t inktag_protocol_parse_request(
    const uint8_t *packet, uint16_t packet_length,
    inktag_protocol_request_t *request) {
    if (packet == NULL || request == NULL ||
        packet_length < INKTAG_PROTOCOL_V2_HEADER_SIZE) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    uint8_t flags;
    if (packet[1] == 0u ||
        !inktag_release_request_flags(packet[2], &flags)) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    request->opcode = packet[0];
    request->transaction_id = packet[1];
    request->flags = flags;
    request->payload = &packet[INKTAG_PROTOCOL_V2_HEADER_SIZE];
    request->payload_length =
        (uint16_t)(packet_length - INKTAG_PROTOCOL_V2_HEADER_SIZE);
    return INKTAG_STATUS_OK;
}

uint16_t inktag_read_u16_le(const uint8_t *data) {
    if (data == NULL) return 0u;
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

uint32_t inktag_read_u32_le(const uint8_t *data) {
    return data == NULL ? 0u :
        (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void inktag_write_u16_le(uint8_t *data, uint16_t value) {
    if (data == NULL) return;
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

void inktag_write_u32_le(uint8_t *data, uint32_t value) {
    if (data == NULL) return;
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

inktag_status_v2_t inktag_transfer_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_transfer_descriptor_t *descriptor) {
    uint32_t segment_total;
    if (payload == NULL || descriptor == NULL || payload_length < 20u) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->object_type = payload[0];
    descriptor->format = payload[1];
    descriptor->segment_count = payload[2];
    descriptor->black_mask = payload[3];
    descriptor->total_size = inktag_read_u32_le(&payload[4]);
    descriptor->expected_crc = inktag_read_u32_le(&payload[8]);
    descriptor->segment_size[0] = inktag_read_u32_le(&payload[12]);
    descriptor->segment_size[1] = inktag_read_u32_le(&payload[16]);
    if ((descriptor->object_type == INKTAG_TRANSFER_OBJECT_DIRECT_FRAME ||
         descriptor->object_type == INKTAG_TRANSFER_OBJECT_STORED_IMAGE) &&
        (descriptor->black_mask & 0x70u) != 0u)
        return INKTAG_STATUS_INVALID_REQUEST;
    if (descriptor->segment_count == 0u || descriptor->segment_count > 2u ||
        descriptor->total_size == 0u ||
        descriptor->segment_size[0] > descriptor->total_size ||
        (descriptor->segment_count == 1u && descriptor->segment_size[1] != 0u) ||
        (descriptor->segment_count == 2u &&
         descriptor->segment_size[1] >
             descriptor->total_size - descriptor->segment_size[0])) {
        return INKTAG_STATUS_INVALID_REQUEST;
    }
    segment_total = descriptor->segment_size[0] +
        (descriptor->segment_count == 2u ? descriptor->segment_size[1] : 0u);
    return segment_total == descriptor->total_size ? INKTAG_STATUS_OK :
                                                     INKTAG_STATUS_INVALID_REQUEST;
}

bool inktag_transfer_descriptor_matches(
    const inktag_transfer_descriptor_t *descriptor, uint8_t expected_format,
    uint8_t expected_segments, uint32_t expected_size) {
    return descriptor != NULL && descriptor->format == expected_format &&
           descriptor->segment_count == expected_segments &&
           descriptor->total_size == expected_size;
}

inktag_status_v2_t inktag_stored_image_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_stored_image_descriptor_t *descriptor) {
    inktag_stored_image_descriptor_t parsed;
    uint16_t end;
    uint8_t i, name_length;
    inktag_status_v2_t status;
    if (!payload || !descriptor || payload_length < 39u ||
        payload[0] != INKTAG_TRANSFER_OBJECT_STORED_IMAGE)
        return INKTAG_STATUS_INVALID_REQUEST;
    name_length = payload[38];
    end = (uint16_t)(39u + name_length);
    if (name_length >= INKTAG_STORED_IMAGE_NAME_BYTES ||
        (payload_length != end && payload_length != end + 1u))
        return INKTAG_STATUS_INVALID_REQUEST;
    memset(&parsed, 0, sizeof(parsed));
    status = inktag_transfer_descriptor_decode(payload, 20u, &parsed.transfer);
    if (status != INKTAG_STATUS_OK) return status;
    parsed.replace_slot = payload_length == end ? UINT8_MAX : payload[end];
    if (payload_length != end && parsed.replace_slot == UINT8_MAX)
        return INKTAG_STATUS_NOT_FOUND;
    parsed.content_type = payload[20];
    parsed.screen_profile = payload + 21u;
    for (i = 0u; i < name_length; ++i) {
        uint8_t c = payload[39u + i];
        parsed.name[i] = ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '-' || c == '_') ? (char)c : '_';
    }
    *descriptor = parsed;
    return INKTAG_STATUS_OK;
}

bool inktag_device_info_qr_transfer_valid(
    const inktag_transfer_descriptor_t *descriptor) {
    uint32_t expected_size;
    if (descriptor == NULL ||
        descriptor->object_type != INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        descriptor->black_mask >= INKTAG_DEVICE_INFO_QR_SLOT_COUNT ||
        descriptor->format < INKTAG_DEVICE_INFO_QR_MIN_MODULES ||
        descriptor->format > INKTAG_DEVICE_INFO_QR_MAX_MODULES ||
        ((descriptor->format - INKTAG_DEVICE_INFO_QR_MIN_MODULES) & 3u) != 0u ||
        descriptor->segment_count != 1u ||
        descriptor->segment_size[1] != 0u) {
        return false;
    }
    expected_size = ((uint32_t)descriptor->format * descriptor->format + 7u) / 8u;
    return expected_size <= INKTAG_DEVICE_INFO_QR_MAX_BYTES &&
           descriptor->total_size == expected_size &&
           descriptor->segment_size[0] == expected_size;
}

uint32_t inktag_crc32_update(uint32_t crc, const uint8_t *data,
                             uint16_t length) {
    if (data == NULL && length != 0u) return crc;
#if defined(INKTAG_CRC32_NIBBLE_TABLE) && INKTAG_CRC32_NIBBLE_TABLE
    /* A 16-entry reflected table is a deliberate size/speed middle ground for
     * serial-XIP MCUs. It removes the eight branch-heavy polynomial steps per
     * byte without imposing a 1 KiB table on size-constrained applications. */
    static const uint32_t table[16] = {
        UINT32_C(0x00000000), UINT32_C(0x1db71064),
        UINT32_C(0x3b6e20c8), UINT32_C(0x26d930ac),
        UINT32_C(0x76dc4190), UINT32_C(0x6b6b51f4),
        UINT32_C(0x4db26158), UINT32_C(0x5005713c),
        UINT32_C(0xedb88320), UINT32_C(0xf00f9344),
        UINT32_C(0xd6d6a3e8), UINT32_C(0xcb61b38c),
        UINT32_C(0x9b64c2b0), UINT32_C(0x86d3d2d4),
        UINT32_C(0xa00ae278), UINT32_C(0xbdbdf21c),
    };
    while (length-- > 0u) {
        crc ^= *data++;
        crc = (crc >> 4) ^ table[crc & 0x0fu];
        crc = (crc >> 4) ^ table[crc & 0x0fu];
    }
#else
    while (length-- > 0u) {
        uint8_t bit;
        crc ^= *data++;
        for (bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1) ^
                  (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
#endif
    return crc;
}
