#ifndef INKTAG_DA_HEALTH_H
#define INKTAG_DA_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

#define INKTAG_DA_HEALTH_WIRE_VERSION 1u
#define INKTAG_DA_HEALTH_WIRE_SIZE 56u

/* Return the calibrated battery supply in millivolts. A zero result means
 * that the ADC backend could not provide a usable sample. */
uint16_t inktag_da_read_battery_mv(void);
void inktag_da_health_init(void);
uint16_t inktag_da_health_policy_voltage_mv(void);
uint16_t inktag_da_health_last_operation_voltage_mv(bool *valid);
void inktag_da_health_display_active(bool active);
bool inktag_da_health_operation_allowed(
    inktag_power_operation_t operation, bool force_sample);
uint16_t inktag_da_health_encode(
    uint8_t record_index, uint8_t *output, uint16_t output_size);

#endif
