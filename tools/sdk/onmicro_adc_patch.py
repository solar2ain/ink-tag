"""Pinned, project-local ADC read timeout; original vendor SDK stays untouched.

The generated copy retains the vendor's complete license. Public blocking reads
and the retained startup gain adjustment get bounded polling. Factory-only
ADC_CALIB_EN routines and all successful conversion/formula code stay unchanged.
"""
import hashlib
import re

ORIGINAL_LF_SHA256 = 'b55e3aafe3997af20024e8941fcc9e61b25eae3988ed0e3f846295a6f685e473'
GET_SHA256 = 'bfd87eab2861c51a6cabc0d77e3ef058c09f34ebb5008b4c26c0c11a9908edd6'
READ_SHA256 = '099f3d037ab791cb10b41caecab533c23e3c649484de4c7773f9116594aed375'
TEMPERATURE_SHA256 = 'f078c93b5515c1cbae5f419e544362e173167b6892d4429da917b4094be671e2'
STARTUP_SHA256 = '1c22f11bfdb389a81fdac4a748b22d69a56b07ba81ce2a5b6f2c98970db9f4bb'
EVENT_TIMEOUT_MS = 100

WAIT = r'''/* InkTag: bounded power-reader wait; no delay, reset or calibration change. */
static __attribute__((noinline)) bool inktag_om_adc_wait_ready(uint32_t mask)
{
    drv_dwt_delay_cycles(0u);
    uint32_t started = drv_dwt_read_running_cycle();
    const uint32_t budget = DRV_DWT_MS_2_CYCLES_CEIL(100u);
    while ((OM_GPADC->INTR & mask) != mask) {
        if ((uint32_t)(drv_dwt_read_running_cycle() - started) >= budget)
            return false;
    }
    return true;
}

'''


def _function(source, declaration):
    start = source.index(declaration)
    brace = source.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def _patch_functions(source):
    """Transform exact pinned functions; used independently by host fixtures."""
    get = _function(source, 'static void drv_adc_get_channel_data(')
    read = _function(source, 'om_error_t drv_adc_read(')
    temperature = _function(source, 'static int drv_adc_temperature_read(void)')
    startup = _function(source, 'static void drv_adc_set_new_calibrate_param(void)')
    for value, digest in ((get, GET_SHA256), (read, READ_SHA256),
                          (temperature, TEMPERATURE_SHA256), (startup, STARTUP_SHA256)):
        if hashlib.sha256(value.encode()).hexdigest() != digest:
            raise ValueError('Unsupported OM ADC function bytes')
    bounded = get.replace('static void drv_adc_get_channel_data(',
                          'static om_error_t inktag_om_adc_get_channel_data(', 1)
    bounded = bounded.replace('uint16_t num)\n{', 'uint16_t num, bool bounded)\n{\n    om_error_t result = OM_ERROR_OK;', 1)
    poll = 'while ((OM_GPADC->INTR & (1 << j)) != (1 << j));'
    if bounded.count(poll) != 2:
        raise ValueError('OM ADC polling sites changed')
    bounded = bounded.replace(poll, '''if (bounded) {
                    if (!inktag_om_adc_wait_ready(1u << j)) {
                        result = OM_ERROR_TIMEOUT;
                        goto inktag_adc_power_down;
                    }
                } else {
                    while ((OM_GPADC->INTR & (1 << j)) != (1 << j));
                }''')
    bounded = bounded.replace('    /* power-down ADC */', 'inktag_adc_power_down:\n    /* power-down ADC */', 1)
    bounded = bounded[:-1] + '    return result;\n}'
    claim = '''    if (adc_env.busy) {
        return OM_ERROR_BUSY;
    }

    adc_env.busy = channel;'''
    if read.count(claim) != 1:
        raise ValueError('OM ADC ownership entry changed')
    public = read.replace(claim, '''    uint32_t saved_irq = __get_PRIMASK();
    __disable_irq();
    if (adc_env.busy) {
        if (saved_irq == 0u) __enable_irq();
        return OM_ERROR_BUSY;
    }
    adc_env.busy = channel;
    if (saved_irq == 0u) __enable_irq();''', 1)
    public = public.replace('    drv_adc_get_channel_data(channel, data, num);', '''    om_error_t result = inktag_om_adc_get_channel_data(channel, data, num, true);
    if (result != OM_ERROR_OK) {
        adc_env.busy = 0u;
        return result;
    }''', 1)
    result = source.replace(get, WAIT + bounded, 1).replace(read, public, 1)
    # Public temperature control shares this reader. Do not turn timeout into
    # an apparently valid 0 C sample or reset an existing owner's conversion.
    checked = temperature.replace('    int16_t ret = 0;',
        '    if (adc_env.busy) return INT16_MIN;\n    int16_t ret = 0;', 1)
    checked = checked.replace('    drv_adc_init(&config);\n\n    drv_adc_read(ADC_CH_TEMPERATURE, &ret, 1);',
        '    om_error_t status = drv_adc_init(&config);\n'
        '    if (status == OM_ERROR_OK)\n'
        '        status = drv_adc_read(ADC_CH_TEMPERATURE, &ret, 1);', 1)
    checked = checked.replace('    return (ret / 10);',
        '    return status == OM_ERROR_OK ? (ret / 10) : INT16_MIN;', 1)
    result = result.replace(temperature, checked, 1)
    # This trim adjustment is reachable through the normal SET_PARAM control,
    # unlike ADC_CALIB_EN factory routines. Never consume partial samples or
    # publish a partial coefficient. Keep the supplied factory gain on failure.
    trim = startup.replace('    if (adc_env.use_flash_ex_2) {',
        '    if (adc_env.busy) return;\n    if (adc_env.use_flash_ex_2) {', 1)
    trim = trim.replace('        drv_adc_init(&config_vbat);\n        drv_adc_read(ADC_CH_VBAT, &vbat_out[0], GPADC_CALIB_VBG_NUM);',
        '        om_error_t status = drv_adc_init(&config_vbat);\n'
        '        if (status == OM_ERROR_OK)\n'
        '            status = drv_adc_read(ADC_CH_VBAT, &vbat_out[0], GPADC_CALIB_VBG_NUM);', 1)
    marker = '        drv_adc_control(ADC_CONTROL_DISABLE_CLOCK, NULL);'
    trim = trim.replace(marker, marker+'\n        if (status != OM_ERROR_OK) return;', 1)
    trim = trim.replace('        REGW(&OM_GPADC->CALI_CFG, MASK_1REG(GPADC_AUTO_COMPEN, 0));',
        '        uint32_t saved_calibration_register = OM_GPADC->CALI_CFG;\n'
        '        REGW(&OM_GPADC->CALI_CFG, MASK_1REG(GPADC_AUTO_COMPEN, 0));', 1)
    for name in ('vbg_code_trim_1', 'vbg_code_trim_3'):
        call = f'        drv_adc_get_channel_data(1<<2U, &{name}[0], GPADC_CALIB_VBG_NUM);'
        if trim.count(call) != 1:
            raise ValueError('OM startup trim polling call changed')
        trim = trim.replace(call,
            f'        status = inktag_om_adc_get_channel_data(1<<2U, &{name}[0], GPADC_CALIB_VBG_NUM, true);\n'
            '            if (status != OM_ERROR_OK) goto inktag_adc_trim_cleanup;', 1)
    trim = trim.replace('        REGW(&OM_GPADC->ADC_CFG0, MASK_1REG(GPADC_PMU_TRIM_CTRL, pmu_trim_store));',
        'inktag_adc_trim_cleanup:\n'
        '        REGW(&OM_GPADC->ADC_CFG0, MASK_1REG(GPADC_PMU_TRIM_CTRL, pmu_trim_store));', 1)
    trim = trim.replace('        adc_env.ft_calib_value.data[ADC_GAIN_1_3].gain_error = gain_error_new;',
        '        if (status != OM_ERROR_OK) {\n'
        '            OM_GPADC->CALI_CFG = saved_calibration_register;\n'
        '            return;\n'
        '        }\n'
        '        adc_env.ft_calib_value.data[ADC_GAIN_1_3].gain_error = gain_error_new;', 1)
    result = result.replace(startup, trim, 1)
    # Only explicitly optional factory ADC_CALIB_EN routines retain old waits.
    result, legacy = re.subn(r'\bdrv_adc_get_channel_data\((.*?)\);',
                            r'(void)inktag_om_adc_get_channel_data(\1, false);',
                            result, flags=re.S)
    return result, legacy


def patch_adc_source(source):
    source = source.replace('\r\n', '\n')
    if hashlib.sha256(source.encode()).hexdigest() != ORIGINAL_LF_SHA256:
        raise ValueError('Unsupported original OM ADC SDK source')
    result, legacy = _patch_functions(source)
    if legacy != 2:
        raise ValueError('OM ADC calibration call closure changed')
    return result
