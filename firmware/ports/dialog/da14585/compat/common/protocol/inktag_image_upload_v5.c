#include "inktag_image_upload_v5.h"
#include "inktag_resource_name.h"

#include <string.h>

/* Keep the shared name bound checked without requiring C11 in vendor builds. */
typedef char inktag_upload_v5_name_bound_check[
    INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES == 35u ? 1 : -1];

inktag_status_v2_t inktag_stored_image_v5_descriptor_decode(
    const uint8_t *payload, uint16_t payload_length,
    inktag_stored_image_v5_descriptor_t *descriptor) {
    inktag_stored_image_v5_descriptor_t parsed;
    uint16_t end;
    uint8_t length;
    inktag_status_v2_t status;
    if (!payload || !descriptor || payload_length < 39u ||
        payload[0] != INKTAG_TRANSFER_OBJECT_STORED_IMAGE)
        return INKTAG_STATUS_INVALID_REQUEST;
    length = payload[38];
    end = (uint16_t)(39u + length);
    if (length >= INKTAG_IMAGE_UPLOAD_V5_NAME_BYTES || payload[20] > 5u ||
        (payload_length != end && payload_length != end + 1u) ||
        (length && !inktag_nfc_resource_alias_valid(payload + 39u, length)))
        return INKTAG_STATUS_INVALID_REQUEST;
    memset(&parsed, 0, sizeof(parsed));
    status = inktag_transfer_descriptor_decode(payload, 20u, &parsed.transfer);
    if (status != INKTAG_STATUS_OK) return status;
    parsed.replace_slot = payload_length == end ? UINT8_MAX : payload[end];
    if (payload_length != end && parsed.replace_slot == UINT8_MAX)
        return INKTAG_STATUS_NOT_FOUND;
    parsed.content_type = payload[20];
    parsed.screen_profile = payload + 21u;
    memcpy(parsed.name, payload + 39u, length);
    *descriptor = parsed;
    return INKTAG_STATUS_OK;
}
