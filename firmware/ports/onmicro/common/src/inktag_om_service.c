#include "inktag_om_service.h"

#include <string.h>

#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum {
    INKTAG_IDX_EPD_CHAR,
    INKTAG_IDX_EPD_VAL,
    INKTAG_IDX_EPD_CFG,
    INKTAG_IDX_APP_VER_CHAR,
    INKTAG_IDX_APP_VER_VAL,
    INKTAG_IDX_NB,
};

typedef struct {
    inktag_om_service_init_t init;
    uint16_t start_handle;
    uint16_t notify_enabled;
    uint8_t mtu_payload;
} inktag_env_t;

static inktag_env_t s_env;
static const uint8_t s_service_uuid[16] = { INKTAG_OM_SERVICE_UUID };
static const uint8_t s_epd_uuid[16] = { INKTAG_OM_EPD_CHAR_UUID };
static const uint8_t s_app_ver_uuid[16] = { INKTAG_OM_APP_VER_CHAR_UUID };
static const uint8_t s_app_version[] = INKTAG_VERSION_GATT_BYTES;

static const ob_gatt_item_t s_attrs[] = {
    [INKTAG_IDX_EPD_CHAR] = { ob_att_char_def, OB_UUID_16BIT, OB_ATT_PROP_READ },
    [INKTAG_IDX_EPD_VAL] = { s_epd_uuid, OB_UUID_128BIT, OB_ATT_PROP_WRITE | OB_ATT_PROP_WRITE_CMD | OB_ATT_PROP_NTF, 0, 0 },
    [INKTAG_IDX_EPD_CFG] = { ob_att_cccd_def, OB_UUID_16BIT, OB_ATT_PROP_READ | OB_ATT_PROP_WRITE },
    [INKTAG_IDX_APP_VER_CHAR] = { ob_att_char_def, OB_UUID_16BIT, OB_ATT_PROP_READ },
    [INKTAG_IDX_APP_VER_VAL] = { s_app_ver_uuid, OB_UUID_128BIT, OB_ATT_PROP_READ, 0, 0 },
};

static const ob_gatt_serv_t s_service = {
    s_service_uuid,
    OB_UUID_128BIT,
    ARRAY_SIZE(s_attrs),
    s_attrs,
};

static void emit(inktag_om_evt_t *event) {
    if (event->evt_type != INKTAG_OM_EVT_INVALID && s_env.init.evt_handler) {
        s_env.init.evt_handler(event);
    }
}

static uint16_t attr_handle(uint8_t idx) {
    /* The returned start handle belongs to the Primary Service declaration;
     * the first item in s_attrs is installed at the following handle. */
    return s_env.start_handle + 1u + idx;
}

static void service_event_cb(uint16_t evt_id, const omble_evt_t *evt) {
    if (!evt) return;

    if (evt_id == OB_GATTS_EVT_READ_REQ) {
        const ob_gatts_evt_read_req_t *req = &evt->gatt.read_req;
        if (req->att_hdl == attr_handle(INKTAG_IDX_EPD_CFG)) {
            ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR, (const uint8_t *)&s_env.notify_enabled, sizeof(s_env.notify_enabled));
        } else if (req->att_hdl == attr_handle(INKTAG_IDX_APP_VER_VAL)) {
            ob_gatts_read_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR, s_app_version, sizeof(s_app_version));
        }
    } else if (evt_id == OB_GATTS_EVT_WRITE_REQ) {
        const ob_gatts_evt_write_req_t *req = &evt->gatt.write_req;
        inktag_om_evt_t event;
        memset(&event, 0, sizeof(event));
        event.evt_type = INKTAG_OM_EVT_INVALID;
        event.conn_idx = evt->gatt.conn_idx;

        if (req->att_hdl == attr_handle(INKTAG_IDX_EPD_VAL)) {
            event.evt_type = INKTAG_OM_EVT_RX_DATA;
            event.p_data = req->data;
            event.length = req->len;
            ob_gatts_write_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR);
            emit(&event);
        } else if (req->att_hdl == attr_handle(INKTAG_IDX_EPD_CFG)) {
            s_env.notify_enabled = (req->len >= 2) ? ((uint16_t)req->data[0] | ((uint16_t)req->data[1] << 8)) : 0;
            event.evt_type = s_env.notify_enabled ? INKTAG_OM_EVT_NOTIFY_ENABLED : INKTAG_OM_EVT_NOTIFY_DISABLED;
            ob_gatts_write_response(evt->gatt.conn_idx, OB_GATT_ERR_NO_ERROR);
            emit(&event);
        }
    } else if (evt_id == OB_GAP_EVT_CONNECTED) {
        s_env.notify_enabled = 0;
        s_env.mtu_payload = 20;
    } else if (evt_id == OB_GAP_EVT_DISCONNECTED) {
        s_env.notify_enabled = 0;
    } else if (evt_id == OB_GATT_EVT_MTU_EXCHANGED) {
        s_env.mtu_payload = (evt->gatt.mtu_exchanged.mtu > 3) ? (evt->gatt.mtu_exchanged.mtu - 3) : 20;
    } else if (evt_id == OB_GATT_EVT_TX_COMPLETE) {
        if (evt->gatt.tx_complete.id == s_env.start_handle) {
            inktag_om_evt_t event = {.evt_type = INKTAG_OM_EVT_NOTIFY_COMPLETE, .conn_idx = 0};
            emit(&event);
        }
    }
}

uint32_t inktag_om_service_init(inktag_om_service_init_t *p_init) {
    memset(&s_env, 0, sizeof(s_env));
    s_env.mtu_payload = 20;
    if (p_init) s_env.init = *p_init;

    uint32_t err = ob_gatts_add_service(&s_service, &s_env.start_handle);
    if (err == 0) {
        ob_event_callback_reg(service_event_cb);
    }
    return err;
}

uint32_t inktag_om_notify(uint8_t conn_idx, const uint8_t *p_data, uint16_t length) {
    if (!s_env.notify_enabled) return 1;
    if (!p_data || length == 0 || length > INKTAG_OM_MAX_DATA_LEN) return 2;

    ob_gatts_hvx_t hvx = {
        OB_HANDLE_VALUE_NTF,
        attr_handle(INKTAG_IDX_EPD_VAL),
        p_data,
        length,
        s_env.start_handle,
    };
    return ob_gatts_send_hvx(conn_idx, &hvx);
}
