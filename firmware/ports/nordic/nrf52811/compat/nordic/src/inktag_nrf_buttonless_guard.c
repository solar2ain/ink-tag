#include "ble_dfu.h"

#include "inktag_nrf_health.h"
#include "nrf_error.h"

/* nrf52.mk renames the SDK implementation in ble_dfu_unbonded.c only. The
 * public symbol remains this guard, so both the normal Nordic control point
 * and InkTag's V2 OTA action share the same 2.6 V entry decision. */
void inktag_nrf_sdk_buttonless_on_ctrl_pt_write(
    ble_gatts_evt_write_t const *p_evt_write);

void ble_dfu_buttonless_on_ctrl_pt_write(
    ble_gatts_evt_write_t const *p_evt_write) {
    if (p_evt_write != NULL && p_evt_write->len > 0u &&
        (p_evt_write->data[0] == DFU_OP_ENTER_BOOTLOADER ||
         p_evt_write->data[0] == DFU_OP_SET_ADV_NAME) &&
        !inktag_nrf_health_operation_allowed(
            INKTAG_POWER_OPERATION_OTA, true)) {
        (void)ble_dfu_buttonless_resp_send(
            (ble_dfu_buttonless_op_code_t)p_evt_write->data[0],
            DFU_RSP_OPERATION_FAILED);
        return;
    }
    inktag_nrf_sdk_buttonless_on_ctrl_pt_write(p_evt_write);
}
