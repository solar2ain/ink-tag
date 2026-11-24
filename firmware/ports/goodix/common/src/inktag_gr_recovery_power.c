#include <stdbool.h>
#include <stdint.h>

#include "inktag_board.h"
#include "inktag_gr_dfu_diagnostics.h"
#include "inktag_power.h"

#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx.h"
#include "gr55xx_hal.h"
#include "gr55xx_sys.h"
#else
#include "gr533x.h"
#include "gr533x_hal.h"
#endif

#include "inktag_gr_adc_poll.inc"

/* Recovery has no application scheduler or health service. Sample VBAT at
 * candidate admission and again before the first application mutation. It
 * also samples before every sector erase and buffered Flash write; once the
 * durable mutation marker closes safe resume, those ordinary mutations use
 * the common 2.4 V floor. Unavailable ADC stays UNKNOWN without a veto. */
static uint16_t recovery_voltage_mv(void) {
    adc_handle_t adc = {0};
    uint16_t samples[16] = {0};
    uint32_t sum = 0u;
    uint16_t result = 0u;

    adc.init.channel_n = ADC_INPUT_SRC_BAT;
    adc.init.channel_p = ADC_INPUT_SRC_BAT;
    adc.init.input_mode = ADC_INPUT_SINGLE;
    adc.init.ref_source = ADC_REF_SRC_BUF_INT;
    adc.init.ref_value = ADC_REF_VALUE_0P8;
#if defined(INKTAG_GOODIX_GR5515)
    adc.init.clock = ADC_CLK_1P6M;
#else
    adc.init.clock = ADC_CLK_1M;
#endif
    if (hal_adc_init(&adc) != HAL_OK) return 0u;

#if defined(INKTAG_GOODIX_GR5515)
    SET_BITS(AON->SNSADC_CFG, AON_SNSADC_CFG_VBAT_EN_Msk);
#else
    SET_BITS(AON_PMU->SNSADC_CFG, AON_PMU_SNSADC_CFG_VBAT_EN_Msk);
#endif
    hal_status_t conversion = inktag_gr_adc_sample16(&adc, samples);
#if defined(INKTAG_GOODIX_GR5515)
    CLEAR_BITS(AON->SNSADC_CFG, AON_SNSADC_CFG_VBAT_EN_Msk);
#else
    CLEAR_BITS(AON_PMU->SNSADC_CFG, AON_PMU_SNSADC_CFG_VBAT_EN_Msk);
#endif

    if (conversion == HAL_OK) {
        for (uint8_t i = 8u; i < 16u; i++) sum += samples[i];
        uint16_t average = (uint16_t)(sum / 8u);
#if defined(INKTAG_GOODIX_GR5515)
        adc_trim_info_t trim = {0};
        int32_t offset = 8362;
        int32_t slope = 4754;
        if (sys_adc_trim_get(&trim) == SDK_SUCCESS && trim.slope_int_0p8 > 0) {
            offset = trim.offset_int_0p8;
            slope = trim.slope_int_0p8;
        }
        int32_t numerator = (offset - (int32_t)average) * 27000;
        int32_t denominator = slope * 7;
        if (numerator > 0 && denominator > 0) {
            result = (uint16_t)((numerator + denominator / 2) / denominator);
        }
#else
        double converted = 0.0;
        hal_adc_vbat_conv(&adc, &average, &converted, 1u);
        if (converted > 0.0 && converted < 6.0) {
            result = (uint16_t)(converted * 1000.0 + 0.5);
        }
#endif
    }
    hal_adc_deinit(&adc);
    return result >= 1500u && result <= 6000u ? result : 0u;
}

static bool recovery_operation_allowed(inktag_power_operation_t operation) {
    uint16_t voltage_mv = recovery_voltage_mv();
    bool valid = voltage_mv != 0u;
    inktag_power_thresholds_t thresholds =
        INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;
    bool allowed = inktag_power_operation_allowed(
        operation, voltage_mv, valid, &thresholds);
    if (allowed) {
        inktag_gr_dfu_reject_clear();
    } else {
        inktag_gr_dfu_reject_set_power_threshold(
            voltage_mv, valid,
            operation == INKTAG_POWER_OPERATION_OTA
                ? INKTAG_POWER_POLICY_OTA_MIN_MV
                : INKTAG_POWER_POLICY_FLASH_MIN_MV);
    }
    return allowed;
}

bool inktag_dfu_ota_admission_allowed(void) {
    return recovery_operation_allowed(INKTAG_POWER_OPERATION_OTA);
}

bool inktag_dfu_programming_allowed(void) {
    return recovery_operation_allowed(INKTAG_POWER_OPERATION_FLASH);
}

bool inktag_dfu_safe_resume_clear_allowed(void) {
    return recovery_operation_allowed(INKTAG_POWER_OPERATION_FLASH);
}

bool inktag_dfu_mode_switch_allowed(void) {
    return inktag_dfu_ota_admission_allowed();
}
