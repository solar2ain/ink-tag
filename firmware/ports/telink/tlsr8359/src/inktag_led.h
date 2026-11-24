#ifndef INKTAG_TLSR_LED_H
#define INKTAG_TLSR_LED_H

#include <stdint.h>

void inktag_led_init(void);
void inktag_led_release(void);
void inktag_led_apply(void *context, uint8_t color_mask);

#endif
