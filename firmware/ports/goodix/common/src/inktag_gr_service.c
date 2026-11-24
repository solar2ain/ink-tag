#include "inktag_gr_service.h"

#include <string.h>
#include "ble_prf_types.h"
#include "ble_prf_utils.h"
#include "utility.h"

#define ATT_128_PRIMARY_SERVICE BLE_ATT_16_TO_128_ARRAY(BLE_ATT_DECL_PRIMARY_SERVICE)
#define ATT_128_CHARACTERISTIC  BLE_ATT_16_TO_128_ARRAY(BLE_ATT_DECL_CHARACTERISTIC)
#define ATT_128_CLIENT_CHAR_CFG BLE_ATT_16_TO_128_ARRAY(BLE_ATT_DESC_CLIENT_CHAR_CFG)

enum inktag_attr_idx_t {
    INKTAG_IDX_SVC,
    INKTAG_IDX_EPD_CHAR,
    INKTAG_IDX_EPD_VAL,
    INKTAG_IDX_EPD_CFG,
    INKTAG_IDX_APP_VER_CHAR,
    INKTAG_IDX_APP_VER_VAL,
    INKTAG_IDX_NB,
};

typedef struct {
    inktag_gr_service_init_t init;
    uint16_t start_hdl;
    uint16_t epd_ntf_cfg[10];
    ble_gatts_create_db_t gatts_db;
} inktag_env_t;

static inktag_env_t s_env;
static const uint16_t s_features = 0xFFFF;
static const uint8_t s_service_uuid[] = { INKTAG_GR_SERVICE_UUID };
static const uint8_t s_app_version[] = INKTAG_VERSION_GATT_BYTES;

static const ble_gatts_attm_desc_128_t s_attr_tab[INKTAG_IDX_NB] = {
    [INKTAG_IDX_SVC] = {ATT_128_PRIMARY_SERVICE, BLE_GATTS_READ_PERM_UNSEC, 0, 0},
    [INKTAG_IDX_EPD_CHAR] = {ATT_128_CHARACTERISTIC, BLE_GATTS_READ_PERM_UNSEC, 0, 0},
    [INKTAG_IDX_EPD_VAL] = { {INKTAG_GR_EPD_CHAR_UUID},
                             BLE_GATTS_NOTIFY_PERM_UNSEC | BLE_GATTS_WRITE_REQ_PERM_UNSEC | BLE_GATTS_WRITE_CMD_PERM_UNSEC,
                             (BLE_GATTS_ATT_VAL_LOC_USER | BLE_GATTS_ATT_UUID_TYPE_SET(BLE_GATTS_UUID_TYPE_128)),
                             INKTAG_GR_MAX_DATA_LEN },
    [INKTAG_IDX_EPD_CFG] = {ATT_128_CLIENT_CHAR_CFG,
                            BLE_GATTS_READ_PERM_UNSEC | BLE_GATTS_WRITE_REQ_PERM_UNSEC,
                            0,
                            0},
    [INKTAG_IDX_APP_VER_CHAR] = {ATT_128_CHARACTERISTIC, BLE_GATTS_READ_PERM_UNSEC, 0, 0},
    [INKTAG_IDX_APP_VER_VAL] = { {INKTAG_GR_APP_VER_CHAR_UUID},
                                BLE_GATTS_READ_PERM_UNSEC,
                                (BLE_GATTS_ATT_VAL_LOC_USER | BLE_GATTS_ATT_UUID_TYPE_SET(BLE_GATTS_UUID_TYPE_128)),
                                sizeof(s_app_version) },
};

static uint8_t find_idx(uint16_t handle) {
    return prf_find_idx_by_handle(handle, s_env.start_hdl, INKTAG_IDX_NB, (uint8_t *)&s_features);
}

static void emit(inktag_gr_evt_t *event) {
    if (event->evt_type != INKTAG_GR_EVT_INVALID && s_env.init.evt_handler) {
        s_env.init.evt_handler(event);
    }
}

static void read_att_evt_handler(uint8_t conn_idx, const ble_gatts_evt_read_t *p_param) {
    ble_gatts_read_cfm_t cfm;
    uint8_t idx = find_idx(p_param->handle);

    cfm.handle = p_param->handle;
    cfm.status = BLE_SUCCESS;
    cfm.length = 0;
    cfm.value = NULL;

    switch (idx) {
        case INKTAG_IDX_EPD_CFG:
            cfm.length = sizeof(uint16_t);
            cfm.value = (uint8_t *)&s_env.epd_ntf_cfg[conn_idx];
            break;
        case INKTAG_IDX_APP_VER_VAL:
            cfm.length = sizeof(s_app_version);
            cfm.value = (uint8_t *)s_app_version;
            break;
        default:
            cfm.status = BLE_ATT_ERR_INVALID_HANDLE;
            break;
    }

    ble_gatts_read_cfm(conn_idx, &cfm);
}

static void write_att_evt_handler(uint8_t conn_idx, const ble_gatts_evt_write_t *p_param) {
    ble_gatts_write_cfm_t cfm;
    inktag_gr_evt_t event;
    uint8_t idx = find_idx(p_param->handle);
    uint16_t cccd_value = 0;

    memset(&event, 0, sizeof(event));
    event.evt_type = INKTAG_GR_EVT_INVALID;
    event.conn_idx = conn_idx;

    cfm.handle = p_param->handle;
    cfm.status = BLE_SUCCESS;

    switch (idx) {
        case INKTAG_IDX_EPD_VAL:
            event.evt_type = INKTAG_GR_EVT_RX_DATA;
            event.p_data = (uint8_t *)p_param->value;
            event.length = p_param->length;
            break;
        case INKTAG_IDX_EPD_CFG:
            cccd_value = le16toh(&p_param->value[0]);
            s_env.epd_ntf_cfg[conn_idx] = cccd_value;
            event.evt_type = (PRF_CLI_START_NTF == cccd_value) ? INKTAG_GR_EVT_NOTIFY_ENABLED : INKTAG_GR_EVT_NOTIFY_DISABLED;
            break;
        default:
            cfm.status = BLE_ATT_ERR_INVALID_HANDLE;
            break;
    }

    ble_gatts_write_cfm(conn_idx, &cfm);
    // Complete the ATT write transaction before the application emits any
    // notification or starts display work.  Chrome/Web Bluetooth serializes
    // GATT operations strictly and can otherwise wait until its operation
    // timeout when a notification is queued ahead of the write response.
    emit(&event);
}

static void cccd_set_evt_handler(uint8_t conn_idx, uint16_t handle, uint16_t cccd_value) {
    inktag_gr_evt_t event;
    uint8_t idx = find_idx(handle);

    if (!prf_is_cccd_value_valid(cccd_value)) return;

    memset(&event, 0, sizeof(event));
    event.conn_idx = conn_idx;
    event.evt_type = INKTAG_GR_EVT_INVALID;

    if (idx == INKTAG_IDX_EPD_CFG) {
        s_env.epd_ntf_cfg[conn_idx] = cccd_value;
        event.evt_type = (PRF_CLI_START_NTF == cccd_value) ? INKTAG_GR_EVT_NOTIFY_ENABLED : INKTAG_GR_EVT_NOTIFY_DISABLED;
    }

    emit(&event);
}

static void ntf_ind_evt_handler(uint8_t conn_idx, uint8_t status, const ble_gatts_evt_ntf_ind_t *p_ntf_ind) {
    /* Product and OTAS notifications may share one physical connection. */
    if (p_ntf_ind == NULL ||
        find_idx(p_ntf_ind->handle) != INKTAG_IDX_EPD_VAL) return;
    /* Failed submissions also release an in-flight notification. App-owned
     * transactions retry their bounded ACK/terminal only after this drain. */
    inktag_gr_evt_t event = {.evt_type = INKTAG_GR_EVT_NOTIFY_COMPLETE,
                            .conn_idx = conn_idx, .status = status};
    emit(&event);
}

static void ble_evt_handler(const ble_evt_t *p_evt) {
    if (!p_evt) return;

    switch (p_evt->evt_id) {
        case BLE_GATTS_EVT_READ_REQUEST:
            read_att_evt_handler(p_evt->evt.gatts_evt.index, &p_evt->evt.gatts_evt.params.read_req);
            break;
        case BLE_GATTS_EVT_WRITE_REQUEST:
            write_att_evt_handler(p_evt->evt.gatts_evt.index, &p_evt->evt.gatts_evt.params.write_req);
            break;
        case BLE_GATTS_EVT_NTF_IND:
            ntf_ind_evt_handler(p_evt->evt.gatts_evt.index, p_evt->evt_status, &p_evt->evt.gatts_evt.params.ntf_ind_sended);
            break;
        case BLE_GATTS_EVT_CCCD_RECOVERY:
            cccd_set_evt_handler(p_evt->evt.gatts_evt.index, p_evt->evt.gatts_evt.params.cccd_recovery.handle, p_evt->evt.gatts_evt.params.cccd_recovery.cccd_val);
            break;
        default:
            break;
    }
}

sdk_err_t inktag_gr_service_init(inktag_gr_service_init_t *p_init) {
    memset(&s_env, 0, sizeof(s_env));
    if (p_init) s_env.init = *p_init;

    s_env.start_hdl = PRF_INVALID_HANDLE;
    s_env.gatts_db.shdl = &s_env.start_hdl;
    s_env.gatts_db.uuid = s_service_uuid;
    s_env.gatts_db.attr_tab_cfg = (uint8_t *)&s_features;
    s_env.gatts_db.max_nb_attr = INKTAG_IDX_NB;
    s_env.gatts_db.srvc_perm = BLE_GATTS_SRVC_UUID_TYPE_SET(BLE_GATTS_UUID_TYPE_128);
    s_env.gatts_db.attr_tab_type = BLE_GATTS_SERVICE_TABLE_TYPE_128;
    s_env.gatts_db.attr_tab.attr_tab_128 = s_attr_tab;

    return ble_gatts_prf_add(&s_env.gatts_db, ble_evt_handler);
}

sdk_err_t inktag_gr_notify(uint8_t conn_idx, uint8_t *p_data, uint16_t length) {
    ble_gatts_noti_ind_t cmd;

    if (length > INKTAG_GR_MAX_DATA_LEN) return SDK_ERR_INVALID_PARAM;
    if (s_env.epd_ntf_cfg[conn_idx] != PRF_CLI_START_NTF) return SDK_ERR_NTF_DISABLED;

    cmd.type = BLE_GATT_NOTIFICATION;
    cmd.handle = prf_find_handle_by_idx(INKTAG_IDX_EPD_VAL, s_env.start_hdl, (uint8_t *)&s_features);
    cmd.length = length;
    cmd.value = p_data;
    return ble_gatts_noti_ind(conn_idx, &cmd);
}
