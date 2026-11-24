#ifndef INKTAG_IMAGE_UPLOAD_V5_H
#define INKTAG_IMAGE_UPLOAD_V5_H

#include "inktag_protocol_core.h"

#define INKTAG_IMAGE_UPLOAD_V5_NAME_BYTES 36u

typedef struct {
    inktag_transfer_descriptor_t transfer;
    uint8_t content_type;
    const uint8_t *screen_profile;
    char name[INKTAG_IMAGE_UPLOAD_V5_NAME_BYTES];
    uint8_t replace_slot;
} inktag_stored_image_v5_descriptor_t;

/* Same wire envelope as before: no mutation opcode or version byte is added.
 * Fixed-v5 targets compile the existing common NFC name validator at 35 bytes.
 * Name/type are upload-owned; profile/duplicate/quota checks stay in the backend.
 * The old descriptor ABI remains isolated until all main ports adopt v5. */
inktag_status_v2_t inktag_stored_image_v5_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_stored_image_v5_descriptor_t *descriptor);

#endif
