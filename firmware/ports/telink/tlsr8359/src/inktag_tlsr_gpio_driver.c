#include "main.h"
/* Keep all other GPIO initialization/IRQ/pull functions as the exact SDK
 * implementation. Only equivalent mux selection gets a compact adapter. */
#define gpio_set_func inktag_sdk_gpio_set_func
#define gpio_set_func2 inktag_sdk_gpio_set_func2
#include "drivers/8258/gpio_8258.c"
#undef gpio_set_func
#undef gpio_set_func2
#include "inktag_tlsr_gpio_mux_compact.h"
void gpio_set_func(GPIO_PinTypeDef pin,GPIO_FuncTypeDef func) {
    inktag_tlsr_gpio_func_compact(pin,func);
}
void gpio_set_func2(GPIO_PinTypeDef pin,GPIO_FuncTypeDef func) {
    inktag_tlsr_gpio_func2_compact(pin,func);
}
