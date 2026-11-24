/* Role-specific compilation of the unchanged pinned SDK ADC driver.
 * Never copy/edit its algorithm, calibration, timing or voltage conversion.
 * Both SDK GPIO calls are literal AS_GPIO; other functions retain fallback. */
#include "main.h"
#include "inktag_tlsr_gpio_func.h"
#define gpio_set_func(pin, func) inktag_tlsr_gpio_set_func(pin, func)
#include "drivers/8258/adc.c"
#undef gpio_set_func
