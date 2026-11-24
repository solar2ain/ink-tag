#include "recovery.h"

#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "inktag_tlsr_clock.h"
#include "inktag_recovery_version.h"
#include "inktag_recovery_integrity_policy.h"

/* One read-only identity for physical and ATT readback; OTA formats stay fixed. */
const uint8_t inktag_tlsr_recovery_version_info[14] = {
    'I','K','R','V',INKTAG_RECOVERY_VERSION_MAJOR,INKTAG_RECOVERY_VERSION_MINOR,
    INKTAG_RECOVERY_VERSION_PATCH,INKTAG_RECOVERY_EPOCH,
    'I','K','S','P',1u,INKTAG_RECOVERY_POLICY_CRC_ONLY,
};

_attribute_ram_code_ void irq_handler(void) {
    irq_blt_sdk_handler();
}

_attribute_ram_code_ int main(void) {
    inktag_tlsr_clock_boot_select();
#if !defined(INKTAG_TLSR_TRUSTED_ENTRY)
    bls_ota_set_fwSize_and_fwBootAddr(INKTAG_APPLICATION_SIZE / 1024u, INKTAG_RECOVERY_START);
#endif
    cpu_wakeup_init();
    rf_drv_init(RF_MODE_BLE_1M);
    gpio_init(1);
    clock_init(INKTAG_TLSR_CPU_CLOCK_SOURCE);
    blc_app_loadCustomizedParameters();
    random_generator_init();
    inktag_recovery_ota_init();
#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
    /* Stage0 always enters here, before a mutable App instruction executes.
     * Failure falls through to ordinary BLE Recovery, never directly to App. */
    (void)inktag_tlsr_trusted_boot();
#endif
    inktag_recovery_ble_init();
    irq_enable();
    while (1) {
        blt_sdk_main_loop();
        inktag_recovery_ble_process();
        inktag_recovery_att_process();
        inktag_recovery_ota_process();
        /* Recovery is intentionally fully awake while it is the only bootable
         * role. A complete or host-restored image returns to normal power
         * management after validation. */
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
}
