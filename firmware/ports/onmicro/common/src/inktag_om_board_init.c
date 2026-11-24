/* Shared App/Recovery electrical defaults, not the SDK EVB LED/button demo.
 * Product LED, panel, Flash, NFC IRQ/callback and SWD mux remain with their
 * existing exact-board owners; do not register GPIO interrupts here. */
#include "om_driver.h"

void board_init(void) {
    drv_pmu_dcdc_enable(true);
    drv_pmu_select_32k(PMU_32K_SEL_RC);
    /* Preserve the established startup pull-up and normal-drive defaults.
     * RC32K uses neither external-crystal pad24 nor pad25. */
    drv_pmu_pin_mode_set(PMU_PIN_ALL_MASK, PMU_PIN_MODE_PU);
    drv_pmu_pin_driven_current_set(PMU_PIN_ALL_MASK,
                                  PMU_PIN_DRIVER_CURRENT_NORMAL);
}
