#include "main.h"

#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "inktag_tlsr_clock.h"

extern void user_init_normal(void);
extern void user_init_deepRetn(void);
extern void main_loop(void);

volatile RAM uint8_t inktag_boot_stage;
volatile RAM uint32_t inktag_boot_heartbeat;

_attribute_ram_code_ void irq_handler(void) {
    if (irq_get_src() & FLD_IRQ_GPIO_EN) {
        inktag_nfc_latch_irq();
        /* Unlike the BLE IRQ sources handled by irq_blt_sdk_handler(), the
         * B85 GPIO source is write-one-to-clear and belongs to the application.
         * Leaving it asserted re-enters this handler on every return and makes
         * one NFC edge look like an endless stream of contact requests. */
        reg_irq_src = FLD_IRQ_GPIO_EN;
    }
    irq_blt_sdk_handler();
}

_attribute_ram_code_ int main(void) {
    int deep_retention;
    inktag_boot_stage = 1u;
    inktag_tlsr_clock_boot_select();
    /* Layout v7 keeps the low App in its 256-KiB partition and fixed Recovery
     * at the official 0x40000 boot point. The SDK-safe signed payload limit
     * is 252 KiB, not the full partition: 256 wraps its 8-bit boot mapping.
     * Near-limit execution and Recovery
     * growth beyond64KiB still require exact-device qualification. */
#if !defined(INKTAG_TLSR_TRUSTED_ENTRY)
    bls_ota_set_fwSize_and_fwBootAddr(INKTAG_APPLICATION_SIZE / 1024u, INKTAG_RECOVERY_START);
#endif
    cpu_wakeup_init();
    inktag_boot_stage = 2u;
    deep_retention = pm_is_MCU_deepRetentionWakeup();
    rf_drv_init(RF_MODE_BLE_1M);
    gpio_init(!deep_retention);
    clock_init(INKTAG_TLSR_CPU_CLOCK_SOURCE);
    blc_app_loadCustomizedParameters();
    inktag_boot_stage = 3u;
    if (deep_retention) {
        inktag_boot_stage = 0x30u;
        user_init_deepRetn();
    } else user_init_normal();
    irq_enable();
    /* A panel or peripheral fault must not permanently strand both BLE and
     * PA7/SWS. Normal Nowa/Nebular full-refresh waits are bounded below this
     * interval; a stalled application is reset back to its recoverable boot
     * path without requiring the board RESET pad. The CPU-clock timer pauses in
     * suspend/deep retention, so idle advertising does not consume the
     * watchdog interval. */
    wd_set_interval_ms(INKTAG_WATCHDOG_TIMEOUT_MS, CLOCK_SYS_CLOCK_1MS);
    wd_start();
    inktag_boot_stage = 0x19u;
    while (1) {
        wd_clear();
        main_loop();
        inktag_boot_heartbeat++;
    }
}
