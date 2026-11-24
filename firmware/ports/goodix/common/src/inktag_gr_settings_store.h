#ifndef INKTAG_GR_SETTINGS_STORE_H
#define INKTAG_GR_SETTINGS_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_gr_settings_map.h"

typedef enum {
    INKTAG_GR_SETTINGS_READ_OK = 0,
    INKTAG_GR_SETTINGS_READ_EMPTY,
    INKTAG_GR_SETTINGS_READ_ERROR,
} inktag_gr_settings_read_result_t;

typedef enum {
    INKTAG_GR_SETTINGS_WRITE_OK = 0,
    INKTAG_GR_SETTINGS_WRITE_NO_SPACE,
    INKTAG_GR_SETTINGS_WRITE_ERROR,
} inktag_gr_settings_write_result_t;

inktag_gr_settings_read_result_t inktag_gr_settings_get(
    inktag_gr_settings_slot_t slot, uint16_t *length, uint8_t *data);
bool inktag_gr_settings_put(inktag_gr_settings_slot_t slot, uint16_t length,
                            const uint8_t *data);
/* Preserve vendor admission failure; numeric NVDS codes differ by exact SDK. */
inktag_gr_settings_write_result_t inktag_gr_settings_put_result(
    inktag_gr_settings_slot_t slot, uint16_t length, const uint8_t *data);
bool inktag_gr_settings_delete(inktag_gr_settings_slot_t slot);
bool inktag_gr_settings_absent(inktag_gr_settings_slot_t slot);

#endif
