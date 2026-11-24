#ifndef INKTAG_OM_HEALTH_H
#define INKTAG_OM_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"
#include "om_driver.h"

#define INKTAG_OM_HEALTH_WIRE_VERSION 1u
#define INKTAG_OM_HEALTH_WIRE_SIZE 56u

typedef enum {
    INKTAG_OM_HEALTH_OPERATION_BOOT = 0,
    INKTAG_OM_HEALTH_OPERATION_BLE = 1,
    INKTAG_OM_HEALTH_OPERATION_DISPLAY = 2,
    INKTAG_OM_HEALTH_OPERATION_TRANSFER = 3,
    INKTAG_OM_HEALTH_OPERATION_STORAGE = 4,
    INKTAG_OM_HEALTH_OPERATION_OTA = 5,
    INKTAG_OM_HEALTH_OPERATION_NFC = 6,
} inktag_om_health_operation_t;

void inktag_om_health_init(uint8_t reset_reason, uint32_t boot_stage);
/* Register only after drv_wdt_init(): the SDK enable clears CPU interrupt EN. */
void inktag_om_health_watchdog_alarm(void *om_reg, drv_event_t event,
                                   void *param0, void *param1);
void inktag_om_health_process(uint32_t now_ms);
void inktag_om_health_watchdog_feed(void);
void inktag_om_health_stage(uint32_t stage,
                            inktag_om_health_operation_t operation);

uint16_t inktag_om_health_voltage_mv(bool force_sample);
bool inktag_om_health_voltage_valid(void);
inktag_power_state_t inktag_om_health_power_state(void);
bool inktag_om_health_operation_allowed(inktag_power_operation_t operation,
                                        bool force_sample);
void inktag_om_health_display_begin(void);
void inktag_om_health_display_end(void);
bool inktag_om_health_display_continue_allowed(uint32_t now_ms);


uint16_t inktag_om_health_encode(uint8_t record_index, uint8_t *output,
                                 uint16_t output_size);

#endif
