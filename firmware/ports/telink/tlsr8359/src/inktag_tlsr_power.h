#ifndef INKTAG_TLSR_POWER_H
#define INKTAG_TLSR_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_power.h"

/* Fresh exact-board VDD sample used both for telemetry and for the final
 * execution-site check immediately before a destructive Flash primitive. */
bool inktag_tlsr_supply_voltage(uint16_t *millivolts);
bool inktag_tlsr_power_operation_allowed(inktag_power_operation_t operation);

bool inktag_tlsr_flash_write_page(uint32_t address, uint32_t length,
                                  const uint8_t *data,
                                  inktag_power_operation_t operation);
bool inktag_tlsr_flash_erase_sector(uint32_t address,
                                    inktag_power_operation_t operation);

#endif
