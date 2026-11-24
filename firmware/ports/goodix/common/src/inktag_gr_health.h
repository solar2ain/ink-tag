#ifndef INKTAG_GR_HEALTH_H
#define INKTAG_GR_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

#define INKTAG_GR_HEALTH_WIRE_VERSION 1u
#define INKTAG_GR_HEALTH_WIRE_SIZE 56u

typedef enum {
    INKTAG_GR_HEALTH_OPERATION_BOOT = 0,
    INKTAG_GR_HEALTH_OPERATION_BLE = 1,
    INKTAG_GR_HEALTH_OPERATION_DISPLAY = 2,
    INKTAG_GR_HEALTH_OPERATION_TRANSFER = 3,
    INKTAG_GR_HEALTH_OPERATION_STORAGE = 4,
    INKTAG_GR_HEALTH_OPERATION_OTA = 5,
    INKTAG_GR_HEALTH_OPERATION_NFC = 6,
} inktag_gr_health_operation_t;

void inktag_gr_health_init(uint8_t reset_reason, uint32_t boot_stage);
void inktag_gr_health_process(uint32_t now_ms);
void inktag_gr_health_watchdog_feed(void);
void inktag_gr_health_stage(uint32_t stage, inktag_gr_health_operation_t operation);

uint16_t inktag_gr_health_voltage_mv(bool force_sample);
bool inktag_gr_health_voltage_valid(void);
inktag_power_state_t inktag_gr_health_power_state(void);
bool inktag_gr_health_operation_allowed(inktag_power_operation_t operation,
                                        bool force_sample);
bool inktag_gr_flash_mutation_allowed(uint32_t address, uint32_t size);
void inktag_gr_health_display_begin(void);
void inktag_gr_health_display_end(void);
bool inktag_gr_health_display_continue_allowed(uint32_t now_ms);

bool inktag_gr_health_warning_page_pending(void);
void inktag_gr_health_warning_page_attempted(void);
void inktag_gr_health_warning_page_shown(void);

uint16_t inktag_gr_health_encode(uint8_t record_index, uint8_t *output,
                                 uint16_t output_size);

#endif
