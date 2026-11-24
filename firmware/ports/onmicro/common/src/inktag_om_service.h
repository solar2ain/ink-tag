#ifndef INKTAG_OM_SERVICE_H
#define INKTAG_OM_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "omble.h"
#include "inktag_version.h"

#define INKTAG_OM_MAX_DATA_LEN 244

#define INKTAG_OM_SERVICE_UUID \
    0xEC, 0x5A, 0x67, 0x1C, 0xC1, 0xB6, 0x46, 0xFB, \
    0x8D, 0x91, 0x28, 0xD8, 0x01, 0x00, 0x75, 0x62
#define INKTAG_OM_EPD_CHAR_UUID \
    0xEC, 0x5A, 0x67, 0x1C, 0xC1, 0xB6, 0x46, 0xFB, \
    0x8D, 0x91, 0x28, 0xD8, 0x02, 0x00, 0x75, 0x62
#define INKTAG_OM_APP_VER_CHAR_UUID \
    0xEC, 0x5A, 0x67, 0x1C, 0xC1, 0xB6, 0x46, 0xFB, \
    0x8D, 0x91, 0x28, 0xD8, 0x03, 0x00, 0x75, 0x62

typedef enum {
    INKTAG_OM_EVT_INVALID,
    INKTAG_OM_EVT_NOTIFY_ENABLED,
    INKTAG_OM_EVT_NOTIFY_DISABLED,
    INKTAG_OM_EVT_RX_DATA,
    INKTAG_OM_EVT_NOTIFY_COMPLETE,
} inktag_om_evt_type_t;

typedef struct {
    inktag_om_evt_type_t evt_type;
    uint8_t conn_idx;
    const uint8_t *p_data;
    uint16_t length;
} inktag_om_evt_t;

typedef void (*inktag_om_evt_handler_t)(inktag_om_evt_t *p_evt);

typedef struct {
    inktag_om_evt_handler_t evt_handler;
} inktag_om_service_init_t;

uint32_t inktag_om_service_init(inktag_om_service_init_t *p_init);
uint32_t inktag_om_notify(uint8_t conn_idx, const uint8_t *p_data, uint16_t length);

#endif
