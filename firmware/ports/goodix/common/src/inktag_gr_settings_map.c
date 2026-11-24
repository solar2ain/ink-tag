#include "inktag_gr_settings_map.h"

#include <stddef.h>

/* Backend-only indices.  NV_TAG_APP() is applied by inktag_gr_settings_store;
 * no product/common schema depends on these Goodix numbers. */
static const inktag_gr_settings_mapping_t s_mappings[] = {
    [INKTAG_GR_SETTINGS_SLOT_EPD_CONFIG] = {
        0x1201u, INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_TIME] = {
        0x1202u, INKTAG_SETTINGS_RECORD_TIME, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE] = {
        0x1203u, INKTAG_SETTINGS_RECORD_SCREEN_PROFILE, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE] = {
        0x1204u, INKTAG_SETTINGS_RECORD_DISPLAY_STATE, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_DEVICE_IDENTITY] = {
        0x1205u, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_ACTIVATION] = {
        0x1206u, INKTAG_SETTINGS_RECORD_ACTIVATION, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_AUX_IO] = {
        0x1207u, INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_NFC] = {
        0x1208u, INKTAG_SETTINGS_RECORD_NFC_BINDING,
        INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY,
    },
    [INKTAG_GR_SETTINGS_SLOT_HEALTH] = {
        0x1209u, INKTAG_SETTINGS_RECORD_HEALTH, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_FIND_CONFIG] = {
        0x120Au, INKTAG_SETTINGS_RECORD_FIND_CONFIG, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_DISPLAY_SETTINGS] = {
        0x120Bu, INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_INDICATOR] = {
        0x120Cu, INKTAG_SETTINGS_RECORD_INDICATOR, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_DEVICE_INFO_QR] = {
        0x120Du, INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_FIND_IDENTITY] = {
        0x120Eu, INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_NFC_URI] = {
        0x120Fu, INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_BOARD_PROFILE] = {
        0x1210u, INKTAG_SETTINGS_RECORD_BOARD_PROFILE, 0,
    },
    [INKTAG_GR_SETTINGS_SLOT_IMAGE_INDEX] = {
        0x1211u, INKTAG_SETTINGS_RECORD_IMAGE_INDEX, 0,
    },
#if INKTAG_BUILD_PRO
    [INKTAG_GR_SETTINGS_SLOT_ADVERTISING] = {
        INKTAG_GR_ADVERTISING_NVDS_INDEX, INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS, 0,
    },
#endif
};

_Static_assert(sizeof(s_mappings) / sizeof(s_mappings[0]) ==
                   INKTAG_GR_SETTINGS_SLOT_COUNT,
               "Goodix Settings Store mapping is incomplete");

bool inktag_gr_settings_mapping(inktag_gr_settings_slot_t slot,
                                inktag_gr_settings_mapping_t *mapping) {
    const inktag_gr_settings_mapping_t *candidate;
    if (mapping == NULL || (uint32_t)slot >= INKTAG_GR_SETTINGS_SLOT_COUNT) {
        return false;
    }
    candidate = &s_mappings[slot];
    if (!inktag_settings_record_id_valid(candidate->primary_record) ||
        (candidate->protected_member != 0 &&
         !inktag_settings_record_id_valid(candidate->protected_member))) {
        return false;
    }
    *mapping = *candidate;
    return true;
}

bool inktag_gr_settings_slot_contains(inktag_gr_settings_slot_t slot,
                                      inktag_settings_record_id_t record_id) {
    inktag_gr_settings_mapping_t mapping;
    return inktag_gr_settings_mapping(slot, &mapping) &&
           (mapping.primary_record == record_id ||
            mapping.protected_member == record_id);
}
