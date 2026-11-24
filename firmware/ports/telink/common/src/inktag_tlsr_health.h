#ifndef INKTAG_TLSR_HEALTH_H
#define INKTAG_TLSR_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

#define INKTAG_TLSR_HEALTH_WIRE_VERSION 1u
#define INKTAG_TLSR_HEALTH_WIRE_SIZE 56u

typedef uint16_t (*inktag_tlsr_health_voltage_reader_t)(void);

void inktag_tlsr_health_init(
    inktag_tlsr_health_voltage_reader_t voltage_reader,
    const inktag_power_thresholds_t *thresholds,
    bool thresholds_qualified,
    bool watchdog_active);

uint16_t inktag_tlsr_health_voltage_mv(bool force_sample);
bool inktag_tlsr_health_voltage_valid(void);
inktag_power_state_t inktag_tlsr_health_power_state(void);
bool inktag_tlsr_health_operation_allowed(
    inktag_power_operation_t operation, bool force_sample);

uint16_t inktag_tlsr_health_encode(
    uint8_t record_index, uint8_t *output, uint16_t output_size);

#endif
