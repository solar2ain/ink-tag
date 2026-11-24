#include "inktag_om_nvds.h"

#include "inktag_om_health.h"
#include "inktag_protocol_core.h"

/* Same SDK ABI/result, no 512-byte CRC table retained in the application.
 * The ordinary SDK implementation remains unchanged in fixed Recovery. */
uint16_t __wrap_om_crc16_ccitt(uint16_t initial,const void *data,unsigned length) {
    return inktag_crc16_ccitt_update(initial,data,length);
}

uint8_t inktag_om_nvds_put(nvds_tag_id_t tag, nvds_tag_len_t length,
                           const void *buffer) {
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                             true)) {
        return NVDS_FAIL;
    }
    return nvds_put(tag, length, buffer);
}

uint8_t inktag_om_nvds_del(nvds_tag_id_t tag) {
    if (!inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                             true)) {
        return NVDS_FAIL;
    }
    return nvds_del(tag);
}

bool inktag_om_nvds_delete_verified(nvds_tag_id_t tag) {
    uint8_t byte = 0u;
    nvds_tag_len_t length = sizeof(byte);
    uint8_t result = inktag_om_nvds_del(tag);
    return (result == NVDS_OK || result == NVDS_TAG_NOT_DEFINED) &&
           nvds_get(tag, &length, &byte) == NVDS_TAG_NOT_DEFINED;
}
