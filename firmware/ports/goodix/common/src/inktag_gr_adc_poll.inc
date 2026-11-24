#include "inktag_gr_adc.h"
#if defined(INKTAG_GOODIX_GR5331)
#include "gr533x_hal_pwr_mgmt.h"
/* The SDK's clock helper is local to its archive member. Retain the public
 * LL ACK semantics and its exact exported clock-setting hook instead. */
extern void (*p_clock_setting_hook)(uint32_t clock);
static bool adc_clock_set(uint32_t clock) {
    if (ll_adc_get_clock() == clock) return true;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    ll_adc_set_clock(clock);
    if (p_clock_setting_hook) p_clock_setting_hook(clock);
    uint32_t budget = 65536u;
    while (ll_adc_get_clock() != clock && --budget != 0u) {}
    __set_PRIMASK(irq);
    return budget != 0u;
}
#else
/* Fixed SDK export used by its ADC power/clock bookkeeping. */
extern bool hal_adc_clk_en_flag;
#endif

/* Independent product poller using SDK register accessors and clock hooks.
 * A work budget remains finite before timers start and with IRQs masked.
 * HAL init/deinit and the existing voltage/temperature calibration stay owned
 * by the SDK. GR551x retains the vendor's four discarded startup samples. */
hal_status_t inktag_gr_adc_sample16(adc_handle_t *adc, uint16_t samples[16]) {
    if (!adc || !samples) return HAL_ERROR;
    if (adc->lock == HAL_LOCKED || adc->state != HAL_ADC_STATE_READY)
        return HAL_BUSY;
    adc->lock = HAL_LOCKED;
    adc->state = HAL_ADC_STATE_BUSY;
    adc->error_code = HAL_ADC_ERROR_NONE;
#if defined(INKTAG_GOODIX_GR5331)
    hal_pwr_mgmt_set_device_renew_flag(PERIPH_DEVICE_NUM_SNSADC);
    hal_pwr_mgmt_set_device_state(PERIPH_DEVICE_NUM_SNSADC, ACTIVE);
#endif
    hal_status_t status = HAL_TIMEOUT;
    uint32_t budget = 65536u;
    ll_adc_disable_clock();
    /* Clock is off: at most the physical FIFO depth can remain. A stuck
     * not-empty flag must not trap boot or the next transaction. */
    for (uint32_t drain = 0u; ll_adc_is_fifo_notempty(); ++drain) {
        if (drain == 64u) goto finish;
        (void)ll_adc_read_fifo();
    }
#if defined(INKTAG_GOODIX_GR5515)
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    ll_adc_enable_clock();
    hal_adc_clk_en_flag = 1u;
    __set_PRIMASK(irq);
    const uint32_t discard = 2u;
#else
    if (!adc_clock_set(adc->init.clock)) goto finish;
    const uint32_t discard = 0u;
#endif
    for (uint32_t word = 0u; word < discard + 8u; ++word) {
        while (ll_adc_get_fifo_count() == 0u) {
            if (--budget == 0u) goto finish;
        }
        uint32_t pair = ll_adc_read_fifo();
        if (word >= discard) {
            uint32_t offset = (word - discard) * 2u;
            samples[offset] = (uint16_t)pair;
            samples[offset + 1u] = (uint16_t)(pair >> 16);
        }
    }
    status = HAL_OK;
finish:
#if defined(INKTAG_GOODIX_GR5515)
    irq = __get_PRIMASK();
    __disable_irq();
    ll_adc_disable_clock();
    hal_adc_clk_en_flag = 0u;
    __set_PRIMASK(irq);
#else
    (void)adc_clock_set(0u);
#endif
    adc->state = HAL_ADC_STATE_READY;
    adc->lock = HAL_UNLOCKED;
    if (status != HAL_OK) adc->error_code = HAL_ADC_ERROR_TIMEOUT;
#if defined(INKTAG_GOODIX_GR5331)
    hal_pwr_mgmt_set_device_state(PERIPH_DEVICE_NUM_SNSADC, IDLE);
#endif
    return status;
}
