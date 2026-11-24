#ifndef INKTAG_GR_PIN_GPIO_H
#define INKTAG_GR_PIN_GPIO_H
#include "inktag_epd_screen.h"
bool inktag_gr_epd_pins_idle(void);
bool inktag_gr_epd_pins_park(void);
void inktag_gr_epd_pin_screen(const epd_screen_config_t *screen);
#endif
