#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inktag_power.h"

int main(void) {
    const inktag_power_thresholds_t thresholds =
        INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;

    assert(inktag_power_classify(0, false, &thresholds) == INKTAG_POWER_UNKNOWN);
    assert(inktag_power_classify(0, true, &thresholds) == INKTAG_POWER_CRITICAL);
    assert(inktag_power_classify(1999, true, &thresholds) == INKTAG_POWER_CRITICAL);
    assert(inktag_power_classify(2000, true, &thresholds) == INKTAG_POWER_WARNING);
    assert(inktag_power_classify(2399, true, &thresholds) == INKTAG_POWER_WARNING);
    assert(inktag_power_classify(2400, true, &thresholds) == INKTAG_POWER_NORMAL);

    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, 0, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, 2399, true, &thresholds));
    assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 2399, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 2400, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 2599, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 2600, true, &thresholds));
    assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_OTA, 2599, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_OTA, 2600, true, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, 3300, false, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH, 3300, false, &thresholds));
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_OTA, 3300, false, &thresholds));
    assert(!inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY, 3300, true, NULL));
    assert(!inktag_power_operation_allowed((inktag_power_operation_t)99, 3300, true, &thresholds));

    assert(!inktag_power_warning_cleared(2549, true, &thresholds));
    assert(inktag_power_warning_cleared(2550, true, &thresholds));

    /* A healthy action-entry sample remains the product-policy value even
     * when the EPD rail sags below the warning threshold during refresh. */
    inktag_power_sample_state_t samples;
    inktag_power_sample_state_reset(&samples);
    inktag_power_policy_sample_record(&samples, 2560u, true);
    assert(inktag_power_classify(samples.policy_mv, samples.policy_valid,
                                 &thresholds) == INKTAG_POWER_NORMAL);
    inktag_power_operation_sample_begin(&samples);
    assert(samples.operation_active);
    assert(samples.operation_valid && samples.operation_mv == 2560u);
    /* Model EPD_ReadVoltage()/diagnostic code accidentally reaching the
     * policy recorder while the panel operation is active.  The common state
     * machine must reject that write even before a vendor guard is applied. */
    inktag_power_policy_sample_record(&samples, 2330u, true);
    assert(samples.policy_valid && samples.policy_mv == 2560u);
    inktag_power_operation_sample_record(&samples, 2330u, true);
    assert(samples.operation_valid && samples.operation_mv == 2330u);
    assert(samples.operation_min_valid && samples.operation_min_mv == 2330u);
    assert(samples.policy_valid && samples.policy_mv == 2560u);
    assert(inktag_power_operation_allowed(INKTAG_POWER_OPERATION_DISPLAY,
                                          samples.operation_mv,
                                          samples.operation_valid,
                                          &thresholds));
    assert(inktag_power_classify(samples.policy_mv, samples.policy_valid,
                                 &thresholds) == INKTAG_POWER_NORMAL);

    /* A real action-entry sample below 2.4 V does update policy and therefore
     * selects the low-battery path before an automatic display starts. */
    inktag_power_operation_sample_end(&samples);
    assert(!samples.operation_active);
    inktag_power_policy_sample_record(&samples, 2330u, true);
    assert(inktag_power_classify(samples.policy_mv, samples.policy_valid,
                                 &thresholds) == INKTAG_POWER_WARNING);
    return 0;
}
