#include "main.h"

#include "inktag_status.h"

/* PA7 is also the TLSR SWS pad. Keep it in AS_SWIRE while blue is off so an
 * idle application remains attachable without a reset. The Nebular factory
 * profile uses PD2 as EPD CS and selects the alternate PB4 red output. The
 * 420R RGB population is the normal board contract; only the first bench unit
 * has open red/green series-resistor positions. Those outputs simply produce
 * no light on that unit, with no blue substitution or hardware modification.
 * Each board contract owns its channel mask; this adapter only drives pins. */
static const inktag_tlsr_gpio_profile_t *board_profile(void) {
    return inktag_tlsr_gpio_profile();
}

static void drive_indicator(uint32_t pin, bool active, uint8_t color) {
    if (!pin) return;
    bool low=(inktag_tlsr_pin_routes_led_active_low()&color)!=0u;
    /* gpio_init(false) reloads the digital GPIO registers after every deep
     * retention wake. Reclaim the pin on every status update; writing only
     * the output latch leaves PD2/PD3 as inputs after the first wake. */
    gpio_setup_up_down_resistor(pin, low?PM_PIN_PULLUP_1M:PM_PIN_PULLDOWN_100K);
    gpio_write(pin, active != low);
    gpio_set_func(pin, AS_GPIO);
    gpio_set_output_en(pin, 1);
    gpio_set_input_en(pin, 0);
}

static void release_blue_to_sws(void) {
    const inktag_tlsr_gpio_profile_t *profile = board_profile();
    uint32_t pin = profile && profile->led_blue ? profile->led_blue : GPIO_PA7;
    if (pin!=GPIO_PA7) { drive_indicator(pin,false,INKTAG_STATUS_COLOR_BLUE); return; }
    gpio_write(pin, 1);
    gpio_set_output_en(pin, 0);
    gpio_set_input_en(pin, 1);
    gpio_set_func(pin, AS_SWIRE);
    gpio_setup_up_down_resistor(pin, PM_PIN_PULLUP_1M);
}

static void drive_blue_on(void) {
    const inktag_tlsr_gpio_profile_t *profile = board_profile();
    if (profile) drive_indicator(profile->led_blue,true,INKTAG_STATUS_COLOR_BLUE);
}

void inktag_led_init(void) {
    const inktag_tlsr_gpio_profile_t *profile = board_profile();
    release_blue_to_sws();
    if (!profile ||
        (profile->base->feature_flags & INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED) == 0u)
        return;
    drive_indicator(profile->led_red,false,INKTAG_STATUS_COLOR_RED);
    drive_indicator(profile->led_green,false,INKTAG_STATUS_COLOR_GREEN);
}

void inktag_led_release(void) {
    const inktag_tlsr_gpio_profile_t *profile=board_profile();
    uint32_t pins[3];
    if (!profile) { release_blue_to_sws(); return; }
    pins[0]=profile->led_red; pins[1]=profile->led_green; pins[2]=profile->led_blue;
    for(uint8_t i=0u;i<3u;++i) {
        if (!pins[i]) continue;
        if (pins[i]==GPIO_PA7) { release_blue_to_sws(); continue; }
        gpio_set_output_en(pins[i],0);
        gpio_set_input_en(pins[i],0);
        gpio_setup_up_down_resistor(pins[i],PM_PIN_UP_DOWN_FLOAT);
    }
}

void inktag_led_apply(void *context, uint8_t color_mask) {
    const inktag_tlsr_gpio_profile_t *profile = board_profile();
    (void)context;
    if (!profile ||
        (profile->base->feature_flags & INKTAG_TLSR_BOARD_INDICATOR_QUALIFIED) == 0u) {
        release_blue_to_sws();
        return;
    }
    drive_indicator(profile->led_red,
                     (color_mask & INKTAG_STATUS_COLOR_RED) != 0u,INKTAG_STATUS_COLOR_RED);
    drive_indicator(profile->led_green,
                     (color_mask & INKTAG_STATUS_COLOR_GREEN) != 0u,INKTAG_STATUS_COLOR_GREEN);
    /* PA7 is shared with SWS. Own it only while the active-low blue LED is
     * requested, then immediately restore the debug function when blue is
     * off. Reset-time SWS activation can still take ownership before the
     * application runs. */
    if ((color_mask & INKTAG_STATUS_COLOR_BLUE) != 0u)
        drive_blue_on();
    else
        release_blue_to_sws();
}
