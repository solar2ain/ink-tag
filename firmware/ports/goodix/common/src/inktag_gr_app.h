#ifndef INKTAG_GR_APP_H
#define INKTAG_GR_APP_H

#include "gr_includes.h"

void ble_evt_handler(const ble_evt_t *p_evt);
void ble_app_init(void);
void inktag_gr_boot_stage_set(uint32_t stage);
uint32_t inktag_gr_boot_stage_get(void);
void inktag_gr_boot_irq_state_capture(void);
void inktag_gr_hw_info_capture(void);
void inktag_gr_app_process(void);
void inktag_gr_app_before_sleep(void);
void inktag_gr_app_after_sleep(void);
void inktag_gr_epd_trace(const char *tag);
void inktag_gr_status_led_poll(void);
void inktag_gr_cooperative_maintenance(void);
void app_feed_wdt(void);
uint32_t timestamp(void);
void set_timestamp(uint32_t value);

#endif
