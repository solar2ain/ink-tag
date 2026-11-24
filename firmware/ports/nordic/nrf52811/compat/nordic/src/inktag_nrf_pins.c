#include "inktag_nrf_pins.h"
#include "inktag_board.h"
#include "app_util_platform.h"
#include "nrf.h"
#include <string.h>

static uint8_t current[INKTAG_PIN_CONFIG_SIZE];
const uint8_t *const inktag_nrf_pins_wire = current;
/* Version zero is invalid; the validated pin16 version is also the published
 * marker. Publication and withdrawal run with interrupts excluded. */

bool inktag_nrf_pins_valid(const uint8_t wire[INKTAG_PIN_CONFIG_SIZE]) {
    inktag_pin_config_t pins;
    /* decode already applies the unchanged common shape/uniqueness/polarity
     * rules. The port adds only the physical pad and protected-resource gate;
     * do not run the same quadratic shape scan a second time here. */
    if (!inktag_pin_config_decode(wire, INKTAG_PIN_CONFIG_SIZE, &pins)) return false;
    for (unsigned role=0u; role<INKTAG_PIN_TXD; ++role) {
        uint8_t pin=wire[4u+role];
        if (pin!=INKTAG_PIN_UNUSED && (pin>=32u ||
            !(INKTAG_BOARD_CONFIG_GPIO_MASK & (UINT32_C(1)<<pin)))) return false;
    }
    return true;
}

const uint8_t *inktag_nrf_pins_current(void) { return current[0] ? current : NULL; }

static void led_apply(const uint8_t *wire, uint8_t colors, bool release) {
    uint32_t set=0u, clear=0u;
    /* Shared RGB roles occupy polarity bits 7..9. Decode their three levels
     * once instead of selecting a wire byte and bit separately for each pad. */
    uint8_t levels=colors ^ ((wire[2u]>>7u) | (wire[3u]<<1u));
    for (unsigned channel=0u; channel<3u; ++channel) {
        unsigned role=INKTAG_PIN_LED_RED+channel;
        uint8_t pin=wire[4u+role];
        if (pin==INKTAG_PIN_UNUSED) continue;
        uint32_t bit=UINT32_C(1)<<pin;
        if ((levels>>channel)&1u) set|=bit; else clear|=bit;
    }
    /* Set inactive/on latches before enabling drive, including after a route
     * change. A disconnect releases the old pads rather than retaining them. */
    NRF_P0->OUTSET=set; NRF_P0->OUTCLR=clear;
    for (unsigned channel=0u; channel<3u; ++channel) {
        uint8_t pin=wire[4u+INKTAG_PIN_LED_RED+channel];
        if (pin!=INKTAG_PIN_UNUSED) NRF_P0->PIN_CNF[pin]=release ? 2u : 3u;
    }
    if (release) NRF_P0->DIRCLR=set|clear;
    else NRF_P0->DIRSET=set|clear;
}

void inktag_nrf_pins_led_apply(uint8_t colors) {
    if (current[0]) led_apply(current,colors,false);
}

bool inktag_nrf_pins_publish(const uint8_t wire[INKTAG_PIN_CONFIG_SIZE]) {
    if (!inktag_nrf_pins_valid(wire)) return false;
    CRITICAL_REGION_ENTER();
    if (current[0]) led_apply(current,0u,true);
    memcpy(current,wire,sizeof(current));
    led_apply(current,0u,false);
    CRITICAL_REGION_EXIT();
    return true;
}

void inktag_nrf_pins_invalidate(void) {
    CRITICAL_REGION_ENTER();
    if (current[0]) led_apply(current,0u,true);
    current[0]=0u;
    CRITICAL_REGION_EXIT();
}

bool inktag_nrf_pins_enable_low(void) {
    return current[0] && (current[2u] & (1u<<INKTAG_PIN_ENABLE)) != 0u;
}

void inktag_nrf_pins_project(epd_config_t *config) {
    if (!config) return;
    if (!current[0]) { memset(config,255,sizeof(*config)); return; }
    memcpy(config,current+4u,6u);
    config->bs_pin=INKTAG_BOARD_EPD_BS_PIN;
    /* N1P1 assigns the two former unused native slots to red and green. */
    memcpy(&config->reserved_screen_legacy,current+4u+INKTAG_PIN_LED_RED,3u);
    config->en_pin=current[4u+INKTAG_PIN_ENABLE];
}
