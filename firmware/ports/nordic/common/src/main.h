#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(INKTAG_NRF_PROTOCOL_V2)
#include "inktag_findmy.h"
#endif

uint32_t timestamp(void);
uint32_t app_uptime_ms(void);
uint32_t app_monotonic_now_ms(void);
uint32_t app_lfclk_status(void);
void set_timestamp(uint32_t timestamp);
/* Internal trust only: usable estimate versus completed sync this boot.
 * Existing public wire stays unchanged pending the integration codec pilot. */
bool app_time_valid(void);
bool app_time_calibrated(void);
int16_t app_clock_rate_ppm(void);
int16_t app_clock_rate_default_ppm(void);
bool app_clock_rate_set(int16_t ppm);
/* Reload the immutable factory GAP name after an authorized settings reset. */
bool app_device_name_reset(void);
/* Same immutable suffix as the ordinary factory GAP name. */
uint16_t app_device_id4(void);
/* Existing fitted button channel only; no allocation or persistent writes. */
bool app_button_configuration_idle(void);
void app_button_configuration_apply(void);
void sleep_mode_enter(void);
void app_feed_wdt(void);
#if defined(INKTAG_NRF_PROTOCOL_V2)
bool app_findmy_apply(const inktag_findmy_config_t *config,
                      bool access_allowed);
bool app_findmy_identity_apply(
    const uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);
void app_findmy_identity_clear(void);
bool app_findmy_connected(void);
bool app_findmy_window_active(void);
/* Vendor service state only; never polls/writes storage or mutates Find. */
bool app_time_checkpoint_ready(void);
#endif

#endif
