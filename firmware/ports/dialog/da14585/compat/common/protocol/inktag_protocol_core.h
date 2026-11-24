#ifndef INKTAG_PROTOCOL_CORE_H
#define INKTAG_PROTOCOL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Existing image flags: plane bits0..1, reading CW quarter turns2..3.
 * PackBits bit7 is transfer-only and is never persisted. */
#define INKTAG_IMAGE_READING_ROTATION_MASK 0x0cu
#define INKTAG_IMAGE_READING_ROTATION_SHIFT 2u
#define INKTAG_IMAGE_STORED_FLAGS_MASK 0x0fu
#define INKTAG_CAPABILITIES_EXTENSION_OFFSET 24u
#define INKTAG_CAPABILITIES_EXTENDED_SIZE 28u
static inline uint8_t inktag_image_reading_rotation(uint8_t flags) {
    return (uint8_t)((flags & INKTAG_IMAGE_READING_ROTATION_MASK) >>
                     INKTAG_IMAGE_READING_ROTATION_SHIFT);
}

#include "inktag_protocol_v2.h"
#include "inktag_release_v1.h"

typedef struct {
    bool implemented;
    bool backend_available;
    bool board_qualified;
    bool runtime_available;
} inktag_capability_gate_t;

typedef struct {
    uint8_t opcode;
    uint8_t transaction_id;
    uint8_t flags;
    const uint8_t *payload;
    uint16_t payload_length;
} inktag_protocol_request_t;

typedef struct {
    uint8_t object_type;
    uint8_t format;
    uint8_t segment_count;
    uint8_t black_mask;
    uint32_t total_size;
    uint32_t expected_crc;
    uint32_t segment_size[2];
} inktag_transfer_descriptor_t;

#define INKTAG_STORED_IMAGE_NAME_BYTES 20u
#define INKTAG_STORED_IMAGE_PROFILE_BYTES 17u
typedef struct {
    inktag_transfer_descriptor_t transfer;
    uint8_t content_type;
    const uint8_t *screen_profile;
    char name[INKTAG_STORED_IMAGE_NAME_BYTES];
    uint8_t replace_slot; /* 0xff = create; logical IDs are 0..254 */
} inktag_stored_image_descriptor_t;

#define INKTAG_DEVICE_INFO_QR_SLOT_COUNT 1u
#define INKTAG_DEVICE_INFO_QR_MIN_MODULES 21u
#define INKTAG_DEVICE_INFO_QR_MAX_MODULES 57u
#define INKTAG_DEVICE_INFO_QR_MAX_BYTES 407u

bool inktag_capability_available(const inktag_capability_gate_t *gate);
uint32_t inktag_capability_add(uint32_t capabilities, uint32_t capability,
                               const inktag_capability_gate_t *gate);
bool inktag_protocol_command_mutates_persistent_state(
    uint8_t opcode, const uint8_t *payload, uint16_t payload_length);
inktag_status_v2_t inktag_protocol_parse_request(
    const uint8_t *packet, uint16_t packet_length,
    inktag_protocol_request_t *request);
inktag_status_v2_t inktag_transfer_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_transfer_descriptor_t *descriptor);
bool inktag_transfer_descriptor_matches(
    const inktag_transfer_descriptor_t *descriptor, uint8_t expected_format,
    uint8_t expected_segments, uint32_t expected_size);
bool inktag_device_info_qr_transfer_valid(
    const inktag_transfer_descriptor_t *descriptor);
/* Exact existing stored-image wire: base20/content1/profile17/nameLength1/
 * name0..19/[optional replacement1]. No fallback to create for invalid or
 * unknown replacement. Ports still decode/admit profile, content and quota. */
inktag_status_v2_t inktag_stored_image_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_stored_image_descriptor_t *descriptor);
uint16_t inktag_read_u16_le(const uint8_t *data);
uint32_t inktag_read_u32_le(const uint8_t *data);
void inktag_write_u16_le(uint8_t *data, uint16_t value);
void inktag_write_u32_le(uint8_t *data, uint32_t value);
uint32_t inktag_crc32_update(uint32_t crc, const uint8_t *data,
                             uint16_t length);

#endif
