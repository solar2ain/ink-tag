#include "inktag_gr_pin_recovery_runtime.h"
#include "inktag_gr_pin_recovery.h"
#include "inktag_gr_factory_seed.h"
#include "goodix_pin_backend_facts.h"
#include "grx_sys.h"
#if defined(INKTAG_GOODIX_GR5515)
#include "gr55xx_ll_gpio.h"
#include "gr55xx_ll_msio.h"
#else
#include "gr533x_ll_gpio.h"
#include "gr533x_ll_msio.h"
#endif

static bool s_open;
static inktag_gr_pin_recovery_leds_t s_leds;

bool inktag_gr_pin_recovery_required(void) {
    /* Even a bare/invalid base must fail dark, not drive legacy PCB defaults. */
    return true;
}

static void write_pin(uint8_t pin, bool high) {
    if (pin == 255u) return;
    uint32_t mask = 1u << (pin & 31u);
    if ((pin >> 6u) == 2u) {
#if defined(INKTAG_GOODIX_GR5515)
        if (high) ll_msio_set_output_pin(mask); else ll_msio_reset_output_pin(mask);
#else
        if (high) ll_msio_set_output_pin(MSIOA, mask); else ll_msio_reset_output_pin(MSIOA, mask);
#endif
    } else {
#if defined(INKTAG_GOODIX_GR5515)
        gpio_regs_t *port = pin < 16u ? GPIO0 : GPIO1;
        mask = 1u << (pin & 15u);
#else
        gpio_regs_t *port = GPIO0;
#endif
        if (high) ll_gpio_set_output_pin(port, mask); else ll_gpio_reset_output_pin(port, mask);
    }
}

void inktag_gr_pin_recovery_output(bool red, bool green, bool blue) {
    if (!s_open) return;
    const bool on[3] = {red, green, blue};
    for (unsigned i = 0u; i < 3u; ++i)
        write_pin(s_leds.pins[i], on[i] != !!(s_leds.active_low & (1u << i)));
}

bool inktag_gr_pin_recovery_open(void) {
    uint8_t raw[76];
    if (s_open) inktag_gr_pin_recovery_output(false, false, false);
    s_open = false;
    inktag_gr_factory_config_copy(raw);
#if defined(INKTAG_GOODIX_GR5515)
    package_type_t package = PACKAGE_NONE;
    if (sys_device_package_get(&package) != SDK_SUCCESS) return false;
    uint8_t mcu = package == PACKAGE_GR5515I0ND ? 0x15u :
        ((package == PACKAGE_GR5513BEND || package == PACKAGE_GR5513BENDU) ? 0x13u : 0u);
    const inktag_gr_pin_facts_t *facts = inktag_gr_pin_backend(0x55u, raw[6], mcu);
#else
    const inktag_gr_pin_facts_t *facts = inktag_gr_pin_backend(0x33u, raw[6], 0x31u);
#endif
    if (!facts || !inktag_gr_pin_recovery_leds(facts, raw, sizeof(raw), &s_leds)) return false;
    /* Full tuple was admitted before any GPIO. AON rail cannot be an LED. */
    for (unsigned i = 0u; i < 3u; ++i) {
        uint8_t pin = s_leds.pins[i];
        if (pin == 255u) continue;
        uint32_t mask = 1u << (pin & 31u);
        write_pin(pin, !!(s_leds.active_low & (1u << i)));
        if ((pin >> 6u) == 2u) {
#if defined(INKTAG_GOODIX_GR5515)
            ll_msio_set_pin_mux(mask, LL_MSIO_MUX_7);
            ll_msio_set_pin_mode(mask, LL_MSIO_MODE_DIGITAL);
            ll_msio_set_pin_pull(mask, LL_MSIO_PULL_NO);
            ll_msio_set_pin_direction(mask, LL_MSIO_DIRECTION_OUTPUT);
#else
            ll_msio_set_pin_mux(MSIOA, mask, IO_MUX_GPIO);
            ll_msio_set_pin_mode(MSIOA, mask, LL_MSIO_MODE_DIGITAL);
            ll_msio_set_pin_pull(MSIOA, mask, LL_MSIO_PULL_NO);
            ll_msio_set_pin_direction(MSIOA, mask, LL_MSIO_DIRECTION_OUTPUT);
#endif
        } else {
#if defined(INKTAG_GOODIX_GR5515)
            gpio_regs_t *port = pin < 16u ? GPIO0 : GPIO1;
            mask = 1u << (pin & 15u);
            if ((pin & 15u) < 8u) ll_gpio_set_mux_pin_0_7(port, mask, LL_GPIO_MUX_7);
            else ll_gpio_set_mux_pin_8_15(port, mask, LL_GPIO_MUX_7);
#else
            gpio_regs_t *port = GPIO0;
            ll_gpio_set_pin_mux(port, mask, IO_MUX_GPIO);
#endif
            ll_gpio_set_pin_pull(port, mask, LL_GPIO_PULL_NO);
            ll_gpio_set_pin_mode(port, mask, LL_GPIO_MODE_OUTPUT);
        }
    }
    s_open = true;
    return true;
}
