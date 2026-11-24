#ifndef INKTAG_GR_SETTINGS_MAP_H
#define INKTAG_GR_SETTINGS_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_settings_records.h"

#if INKTAG_BUILD_PRO
/* 0x1212 maps to the reserved durable BLE identity tag0x5212. */
#define INKTAG_GR_ADVERTISING_NVDS_INDEX 0x1213u
#endif

/* These slots name Goodix backend records.  Their 0x120x NVDS indices are
 * intentionally kept in the mapping implementation; product code uses the
 * stable common record IDs above instead of treating a vendor tag as schema. */
typedef enum {
    INKTAG_GR_SETTINGS_SLOT_EPD_CONFIG = 0,
    INKTAG_GR_SETTINGS_SLOT_TIME,
    INKTAG_GR_SETTINGS_SLOT_SCREEN_PROFILE,
    INKTAG_GR_SETTINGS_SLOT_DISPLAY_STATE,
    INKTAG_GR_SETTINGS_SLOT_DEVICE_IDENTITY,
    INKTAG_GR_SETTINGS_SLOT_ACTIVATION,
    INKTAG_GR_SETTINGS_SLOT_AUX_IO,
    INKTAG_GR_SETTINGS_SLOT_NFC,
    INKTAG_GR_SETTINGS_SLOT_HEALTH,
    INKTAG_GR_SETTINGS_SLOT_FIND_CONFIG,
    INKTAG_GR_SETTINGS_SLOT_DISPLAY_SETTINGS,
    INKTAG_GR_SETTINGS_SLOT_INDICATOR,
    INKTAG_GR_SETTINGS_SLOT_DEVICE_INFO_QR,
    INKTAG_GR_SETTINGS_SLOT_FIND_IDENTITY,
    INKTAG_GR_SETTINGS_SLOT_NFC_URI,
    INKTAG_GR_SETTINGS_SLOT_BOARD_PROFILE,
    INKTAG_GR_SETTINGS_SLOT_IMAGE_INDEX,
#if INKTAG_BUILD_PRO
    INKTAG_GR_SETTINGS_SLOT_ADVERTISING,
#endif
    INKTAG_GR_SETTINGS_SLOT_COUNT,
} inktag_gr_settings_slot_t;

typedef struct {
    uint16_t nvds_index;
    inktag_settings_record_id_t primary_record;
    /* The NFC backend record contains both the binding and protected replay
     * state. Images-only clearing preserves this entire composite; generic
     * ordinary-settings deletion must never erase its protected member. */
    inktag_settings_record_id_t protected_member;
} inktag_gr_settings_mapping_t;

bool inktag_gr_settings_mapping(inktag_gr_settings_slot_t slot,
                                inktag_gr_settings_mapping_t *mapping);
bool inktag_gr_settings_slot_contains(inktag_gr_settings_slot_t slot,
                                      inktag_settings_record_id_t record_id);

#endif
