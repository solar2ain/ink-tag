#ifndef INKTAG_TLSR_GPIO_FUNC_H
#define INKTAG_TLSR_GPIO_FUNC_H
/* Exact specialization of pinned B85 gpio_set_func(): plain GPIO and PA7
 * SWS only. Every other function delegates to the original SDK. This lets
 * constant Recovery LED/ADC calls avoid retaining unrelated peripheral mux
 * dispatch, without removing any mux path from an App or a dynamic caller.
 * SDK provenance/equivalence vectors are checked by the host test. */
static inline __attribute__((always_inline)) void inktag_tlsr_gpio_set_func(
    GPIO_PinTypeDef pin, GPIO_FuncTypeDef func) {
    if (func == AS_GPIO) {
        BM_SET(reg_gpio_func(pin), (uint8_t)pin);
    } else if (pin == GPIO_PA7 && func == AS_SWIRE) {
        BM_CLR(reg_gpio_func(pin), (uint8_t)pin);
        write_reg8(0x5A9u, read_reg8(0x5A9u) & (uint8_t)~0xC0u);
    } else {
        gpio_set_func(pin, func);
    }
}
#endif
