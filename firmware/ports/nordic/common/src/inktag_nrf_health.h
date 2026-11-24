#ifndef INKTAG_NRF_HEALTH_H
#define INKTAG_NRF_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

#define INKTAG_NRF_HEALTH_WIRE_VERSION 1u
#define INKTAG_NRF_HEALTH_WIRE_SIZE 56u

typedef enum {
    INKTAG_NRF_HEALTH_OPERATION_BOOT = 0,
    INKTAG_NRF_HEALTH_OPERATION_BLE = 1,
    INKTAG_NRF_HEALTH_OPERATION_DISPLAY = 2,
    INKTAG_NRF_HEALTH_OPERATION_TRANSFER = 3,
    INKTAG_NRF_HEALTH_OPERATION_STORAGE = 4,
    INKTAG_NRF_HEALTH_OPERATION_OTA = 5,
    INKTAG_NRF_HEALTH_OPERATION_NFC = 6,
} inktag_nrf_health_operation_t;

void inktag_nrf_health_init(uint32_t reset_reason, uint32_t boot_stage);
void inktag_nrf_health_process(uint32_t now_ms);
void inktag_nrf_health_record_complete(bool success);
void inktag_nrf_health_watchdog_capture(void);
void inktag_nrf_health_stage(uint32_t stage,
                             inktag_nrf_health_operation_t operation);

uint16_t inktag_nrf_health_voltage_mv(bool force_sample);
bool inktag_nrf_health_voltage_valid(void);
void inktag_nrf_health_temperature_sample(void);
bool inktag_nrf_health_mcu_temperature(int16_t *sixteenths);
bool inktag_nrf_health_panel_temperature(int16_t *sixteenths);
void inktag_nrf_health_panel_temperature_record(bool valid, int16_t sixteenths);
bool inktag_nrf_health_effective_temperature(int8_t *celsius);
inktag_power_state_t inktag_nrf_health_power_state(void);
bool inktag_nrf_health_operation_allowed(inktag_power_operation_t operation,
                                         bool force_sample);
void inktag_nrf_health_display_begin(void);
void inktag_nrf_health_display_end(void);
uint16_t inktag_nrf_health_encode(uint8_t record_index, uint8_t *output,
                                  uint16_t output_size);

#endif
