#include "inktag_om_voltage.h"

#include <stddef.h>

#include "om_driver.h"

#define INKTAG_OM_VBAT_SAMPLE_COUNT 8u
#define INKTAG_OM_VBAT_WARMUP_SAMPLES 2u
#define INKTAG_OM_VBAT_MIN_VALID_MV 1500
#define INKTAG_OM_VBAT_MAX_VALID_MV 5000
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint16_t s_last_operation_sample_mv;
static bool s_last_operation_sample_valid;

uint16_t inktag_om_voltage_sample_mv(void) {
    drv_adc_config_t config = {
        .channel = ADC_CH_VBAT,
        .mode = ADC_MODE_SINGLE,
        .gain = ADC_GAIN_1_3,
        .sum_num = ADC_SUM_NUM_256,
        .sampling_cycles = ADC_SAMPLING_CYCLES_128,
    };
    int16_t samples[INKTAG_OM_VBAT_SAMPLE_COUNT] = {0};
    /* Do not reset another ADC owner's state. Keep only the check/setup
     * atomic; the bounded polling read runs with the caller's IRQ state. */
    uint32_t saved_irq = __get_PRIMASK();
    __disable_irq();
    om_error_t initialized = OM_ERROR_BUSY;
    if ((intptr_t)drv_adc_control(ADC_CONTROL_IS_BUSY, NULL) == 0)
        initialized = drv_adc_init(&config);
    if (saved_irq == 0u) __enable_irq();
    if (initialized != OM_ERROR_OK) return 0u;
    om_error_t status = drv_adc_read(config.channel, samples,
                                     ARRAY_SIZE(samples));
    saved_irq = __get_PRIMASK();
    __disable_irq();
    if (status != OM_ERROR_BUSY &&
        (intptr_t)drv_adc_control(ADC_CONTROL_IS_BUSY, NULL) == 0)
        (void)drv_adc_control(ADC_CONTROL_DISABLE_CLOCK, NULL);
    if (saved_irq == 0u) __enable_irq();
    if (status != OM_ERROR_OK) return 0u;

    uint32_t total = 0u;
    uint8_t valid = 0u;
    for (uint8_t i = INKTAG_OM_VBAT_WARMUP_SAMPLES;
         i < ARRAY_SIZE(samples); i++) {
        if (samples[i] < INKTAG_OM_VBAT_MIN_VALID_MV ||
            samples[i] > INKTAG_OM_VBAT_MAX_VALID_MV) {
            continue;
        }
        total += (uint16_t)samples[i];
        valid++;
    }
    return valid ? (uint16_t)(total / valid) : 0u;
}

bool inktag_om_voltage_operation_check(inktag_power_operation_t operation,
                                       uint16_t *sample_mv,
                                       bool *sample_valid) {
    static const inktag_power_thresholds_t thresholds =
        INKTAG_POWER_POLICY_THRESHOLDS_INITIALIZER;
    uint16_t voltage_mv = inktag_om_voltage_sample_mv();
    bool valid = voltage_mv != 0u;
    s_last_operation_sample_mv = valid ? voltage_mv : 0u;
    s_last_operation_sample_valid = valid;
    if (sample_mv) *sample_mv = s_last_operation_sample_mv;
    if (sample_valid) *sample_valid = valid;
    return inktag_power_operation_allowed(operation, voltage_mv, valid,
                                           &thresholds);
}

bool inktag_om_voltage_operation_allowed(inktag_power_operation_t operation) {
    return inktag_om_voltage_operation_check(operation, NULL, NULL);
}

void inktag_om_voltage_last_operation_sample(uint16_t *sample_mv,
                                             bool *sample_valid) {
    if (sample_mv) *sample_mv = s_last_operation_sample_mv;
    if (sample_valid) *sample_valid = s_last_operation_sample_valid;
}
