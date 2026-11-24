#include "tl_common.h"
#include "stack/ble/ble.h"
#include <stddef.h>
#include <stdint.h>

/* Both product roles are unbonded peripherals. SDK GAP/GATT references
 * resolve here instead of retaining unused SMP and its writable NV pages.
 * No pairing/encryption is accepted. IKS1, IKAC1 and Find crypto are separate. */
_attribute_aligned_(4) smp_mng_t blc_smpMng = {0};
_attribute_aligned_(4) secReq_ctl_t blc_SecReq_ctrl = {0};
int bond_device_flash_cfg_idx = 0;
smp_init_handler_t func_smp_init = NULL;
smp_info_handler_t func_smp_info = NULL;
smp_trigger_cb_t func_smp_trigger = NULL;
u32 smp_timeout_start_tick = 0u;

uint8_t *l2cap_smp_handler(uint16_t connection_handle, uint8_t *packet) {
    (void)connection_handle;
    (void)packet;
    return NULL;
}

int bls_smp_encryption_done(uint16_t connection_handle) {
    (void)connection_handle;
    return 0;
}

void blc_smp_procParingEnd(uint8_t error_reason) {
    (void)error_reason;
}

void bls_smp_peripheral_paring_loop(void) {}
void bls_smp_param_Cleanflash(void) {}
void bond_slave_flash_clean(void) {}

paring_sts_t bls_smp_get_paring_statas(uint16_t connection_handle) {
    (void)connection_handle;
    return No_Bonded_No_LTK;
}
