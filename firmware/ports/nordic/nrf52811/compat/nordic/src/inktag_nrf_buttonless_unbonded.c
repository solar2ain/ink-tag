#include <stddef.h>
#include <string.h>

#include "ble_dfu.h"
#include "ble_srv_common.h"
#include "nrf_error.h"

#if !NRF_DFU_BLE_BUTTONLESS_SUPPORTS_BONDS

static ble_dfu_buttonless_t *m_dfu;

uint32_t ble_dfu_buttonless_backend_init(ble_dfu_buttonless_t *dfu) {
    if (dfu == NULL) return NRF_ERROR_NULL;
    m_dfu = dfu;
    return NRF_SUCCESS;
}

uint32_t ble_dfu_buttonless_async_svci_init(void) {
    /* Recovery derives InkTag_NRF_<ID4>_OTA directly from the factory device
     * identity. No settings-page write is needed merely to enter Recovery. */
    return NRF_SUCCESS;
}

uint32_t ble_dfu_buttonless_char_add(ble_dfu_buttonless_t *dfu) {
    ble_add_char_params_t params;
    if (dfu == NULL) return NRF_ERROR_NULL;
    memset(&params, 0, sizeof(params));
    params.uuid = BLE_DFU_BUTTONLESS_CHAR_UUID;
    params.uuid_type = dfu->uuid_type;
    params.char_props.indicate = 1u;
    params.char_props.write = 1u;
    params.is_defered_write = true;
    params.is_var_len = true;
    params.max_len = BLE_GATT_ATT_MTU_DEFAULT;
    params.cccd_write_access = SEC_OPEN;
    params.write_access = SEC_OPEN;
    params.read_access = SEC_OPEN;
    return characteristic_add(dfu->service_handle, &params,
                              &dfu->control_point_char);
}

void ble_dfu_buttonless_on_ctrl_pt_write(
    const ble_gatts_evt_write_t *write) {
    ble_dfu_buttonless_op_code_t opcode;
    uint32_t result;
    if (m_dfu == NULL || write == NULL || write->len == 0u) return;
    opcode = (ble_dfu_buttonless_op_code_t)write->data[0];
    if (opcode != DFU_OP_ENTER_BOOTLOADER) {
        (void)ble_dfu_buttonless_resp_send(opcode,
                                          DFU_RSP_OP_CODE_NOT_SUPPORTED);
        return;
    }
    if (m_dfu->is_waiting_for_reset) {
        (void)ble_dfu_buttonless_resp_send(opcode, DFU_RSP_BUSY);
        return;
    }
    m_dfu->is_waiting_for_reset = true;
    result = ble_dfu_buttonless_resp_send(opcode, DFU_RSP_SUCCESS);
    if (result != NRF_SUCCESS) {
        m_dfu->is_waiting_for_reset = false;
        m_dfu->evt_handler(BLE_DFU_EVT_RESPONSE_SEND_ERROR);
    }
}

uint32_t ble_dfu_buttonless_bootloader_start_prepare(void) {
    if (m_dfu == NULL) return NRF_ERROR_INVALID_STATE;
    m_dfu->evt_handler(BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE);
    return ble_dfu_buttonless_bootloader_start_finalize();
}

#endif
