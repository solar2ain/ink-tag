#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdint.h>
#include <stdbool.h>


uint32_t timestamp(void);
uint32_t app_uptime_ms(void);
uint32_t app_monotonic_now_ms(void);
uint32_t app_lfclk_status(void);
void set_timestamp(uint32_t timestamp);
void sleep_mode_enter(void);
void app_feed_wdt(void);
bool app_time_valid(void);

#endif
