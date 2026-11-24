#ifndef INKTAG_OM_VOLTAGE_H
#define INKTAG_OM_VOLTAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

/* Every caller attempts a fresh ADC conversion. A zero result stays UNKNOWN;
 * common policy does not veto a supported operation for an unavailable sample. */
uint16_t inktag_om_voltage_sample_mv(void);
bool inktag_om_voltage_operation_check(inktag_power_operation_t operation,
                                       uint16_t *sample_mv,
                                       bool *sample_valid);
bool inktag_om_voltage_operation_allowed(inktag_power_operation_t operation);
void inktag_om_voltage_last_operation_sample(uint16_t *sample_mv,
                                             bool *sample_valid);

#endif
