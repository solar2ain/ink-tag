#include "inktag_gr_settings_store.h"
#include "inktag_gr_nvds_guard.h"

#include <stddef.h>

#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_nvds.h"
#elif defined(INKTAG_GOODIX_GR5331)
#include "gr533x_nvds.h"
#else
#error "InkTag Goodix Settings Store requires an exact MCU selection"
#endif

#if INKTAG_BUILD_PRO
#include "inktag_gr_ble_address.h"
_Static_assert(NV_TAG_APP(INKTAG_GR_ADVERTISING_NVDS_INDEX) !=
                   INKTAG_GR_BLE_ADDRESS_NVDS_TAG,
               "Advertising settings must not overwrite the durable BLE identity");
#endif

static bool nvds_tag_for(inktag_gr_settings_slot_t slot, NvdsTag_t *tag) {
    inktag_gr_settings_mapping_t mapping;
    if (tag == NULL || !inktag_gr_settings_mapping(slot, &mapping)) return false;
    *tag = NV_TAG_APP(mapping.nvds_index);
    return true;
}

inktag_gr_settings_read_result_t inktag_gr_settings_get(
    inktag_gr_settings_slot_t slot, uint16_t *length, uint8_t *data) {
    NvdsTag_t tag;
    uint8_t result;
    if (!inktag_gr_nvds_ready() || length == NULL || data == NULL || !nvds_tag_for(slot, &tag)) {
        return INKTAG_GR_SETTINGS_READ_ERROR;
    }
    result = nvds_get(tag, length, data);
    if (result == NVDS_SUCCESS) return INKTAG_GR_SETTINGS_READ_OK;
    if (result == NVDS_TAG_NOT_EXISTED) return INKTAG_GR_SETTINGS_READ_EMPTY;
    return INKTAG_GR_SETTINGS_READ_ERROR;
}

bool inktag_gr_settings_put(inktag_gr_settings_slot_t slot, uint16_t length,
                            const uint8_t *data) {
    return inktag_gr_settings_put_result(slot, length, data) ==
           INKTAG_GR_SETTINGS_WRITE_OK;
}

inktag_gr_settings_write_result_t inktag_gr_settings_put_result(
    inktag_gr_settings_slot_t slot, uint16_t length, const uint8_t *data) {
    NvdsTag_t tag;
    uint8_t result;
    if (!inktag_gr_nvds_ready() || length == 0u || data == NULL || !nvds_tag_for(slot, &tag))
        return INKTAG_GR_SETTINGS_WRITE_ERROR;
    result = nvds_put(tag, length, data);
    if (result == NVDS_SUCCESS) return INKTAG_GR_SETTINGS_WRITE_OK;
    if (result == NVDS_SPACE_NOT_ENOUGH) return INKTAG_GR_SETTINGS_WRITE_NO_SPACE;
    return INKTAG_GR_SETTINGS_WRITE_ERROR;
}

bool inktag_gr_settings_delete(inktag_gr_settings_slot_t slot) {
    NvdsTag_t tag;
    uint8_t result;
    if (!inktag_gr_nvds_ready() || !nvds_tag_for(slot, &tag)) return false;
    result = nvds_del(tag);
    return result == NVDS_SUCCESS || result == NVDS_TAG_NOT_EXISTED;
}

bool inktag_gr_settings_absent(inktag_gr_settings_slot_t slot) {
    NvdsTag_t tag;
    uint8_t probe = 0u;
    uint16_t length = sizeof(probe);
    if (!inktag_gr_nvds_ready() || !nvds_tag_for(slot, &tag)) return false;
    return nvds_get(tag, &length, &probe) == NVDS_TAG_NOT_EXISTED;
}
