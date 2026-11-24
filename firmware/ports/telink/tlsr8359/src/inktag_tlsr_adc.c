#include "main.h"

#include "inktag_tlsr_adc.h"

#define INKTAG_TLSR_EFUSE_SELECT_REG 0x74u
#define INKTAG_TLSR_EFUSE_INFO1_REG  0x4Cu
#define INKTAG_TLSR_EFUSE_INFO1_SEL  0x62u

static uint16_t s_adc_reference_mv;
static bool s_adc_reference_programmed;

static void load_adc_reference(void) {
    uint32_t info;
    uint8_t encoded;
    if (s_adc_reference_mv != 0u) return;

    /* Read-only equivalent of Telink B85 SDK efuse_get_adc_calib_value():
     * select info1, read it, then release the selector.  Bits 7:6 indicate a
     * programmed ADC value; bits 5:0 encode 1015 mV + 5 mV per step. */
    REG_ADDR8(INKTAG_TLSR_EFUSE_SELECT_REG) = INKTAG_TLSR_EFUSE_INFO1_SEL;
    info = REG_ADDR32(INKTAG_TLSR_EFUSE_INFO1_REG);
    REG_ADDR8(INKTAG_TLSR_EFUSE_SELECT_REG) = 0u;
    encoded = (uint8_t)info;
    if ((encoded & 0xC0u) == 0xC0u) {
        s_adc_reference_mv = (uint16_t)(1015u +
            (uint16_t)(encoded & 0x3Fu) * 5u);
        s_adc_reference_programmed = true;
    } else {
        /* Official B85 default obtained from ATE population statistics. */
        s_adc_reference_mv = 1175u;
        s_adc_reference_programmed = false;
    }
}

static void apply_adc_reference(void) {
    load_adc_reference();
    adc_vref_cfg.adc_vref = s_adc_reference_mv;
    adc_vref_cfg.adc_calib_en = s_adc_reference_programmed ? 1u : 0u;
}

bool inktag_tlsr_adc_reference(uint16_t *millivolts) {
    load_adc_reference();
    if (millivolts) *millivolts = s_adc_reference_mv;
    return s_adc_reference_programmed;
}

static void configure_misc_channel(ADC_InputPchTypeDef positive,
                                   ADC_InputNchTypeDef negative,
                                   ADC_PreScalingTypeDef scaler) {
    adc_init();
    adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2);
    adc_set_state_length(240, 0, 10);
    adc_set_vref_vbat_divider(ADC_VBAT_DIVIDER_OFF);
    adc_set_input_mode(ADC_MISC_CHN, DIFFERENTIAL_MODE);
    adc_set_ain_channel_differential_mode(ADC_MISC_CHN, positive, negative);
    adc_set_ref_voltage(ADC_MISC_CHN, ADC_VREF_1P2V);
    adc_set_resolution(ADC_MISC_CHN, RES14);
    adc_set_tsample_cycle(ADC_MISC_CHN, SAMPLING_CYCLES_6);
    adc_set_ain_pre_scaler(scaler);
    adc_set_mode(ADC_NORMAL_MODE);
    adc_power_on_sar_adc(1);
}

bool inktag_tlsr_read_die_temperature(int8_t *celsius,
                                     uint16_t *sensor_raw) {
    uint8_t sensor_power = analog_read(0x07);
    uint16_t sample;

    if (celsius) *celsius = 0;
    /* B85 has no qualified Celsius conversion.  Normal status calls do not
     * need to power the sensor merely to learn that fact; the explicit ADC
     * diagnostic supplies sensor_raw and remains available for research. */
    if (sensor_raw == NULL) return false;

    /* TLSR8359 documents the sensor transfer function, but the official B85
     * SDK deliberately omits temperature mode and its eFuse ADC calibration
     * is specified only for GPIO voltage sampling.  Keep the SDK-filtered
     * result as an opaque, repeatable sample; applying the GPIO reference as
     * a temperature calibration produced the false -4/-8 C readings. */
    analog_write(0x07, sensor_power & (uint8_t)~BIT(4));
    apply_adc_reference();
    configure_misc_channel(TEMSENSORP, TEMSENSORN, ADC_PRESCALER_1);
    sleep_us(1000u);
    (void)adc_sample_and_get_result();
    sample = (uint16_t)adc_sample_and_get_result();
    adc_power_on_sar_adc(0);
    analog_write(0x07, sensor_power);

    *sensor_raw = sample;
    return false;
}

bool inktag_tlsr_read_supply_voltage(uint32_t pin, uint16_t *millivolts) {
    uint16_t reading;

    /* TLSR8359 has no dedicated internal VBAT input in its documented MISC
     * channel map.  The compatible Telink SDK measures VDD by driving an
     * ADC-capable GPIO high and sampling that same pad at 1/8 scale. */
    apply_adc_reference();
    adc_init();
    adc_vbat_init((GPIO_PinTypeDef)pin);
    sleep_us(100u);
    adc_power_on_sar_adc(1);
    (void)adc_sample_and_get_result();
    reading = (uint16_t)adc_sample_and_get_result();
    adc_power_on_sar_adc(0);
    if (millivolts) *millivolts = reading;
    return reading >= 1800u && reading <= 3600u;
}
