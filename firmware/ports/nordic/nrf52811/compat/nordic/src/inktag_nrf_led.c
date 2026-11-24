#include "inktag_nrf_led.h"
#include "inktag_nrf_pins.h"

/* The native board/pin record supplies each channel and its polarity. */
void inktag_nrf_led_apply(uint8_t colors) {
    inktag_nrf_pins_led_apply(colors);
}
