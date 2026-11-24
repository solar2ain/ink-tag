#ifndef INKTAG_NRF_BLE_ADDRESS_H
#define INKTAG_NRF_BLE_ADDRESS_H

#include <stdint.h>
#include "ble_gap.h"

/* Read/preserve the existing SDK identity at App/Recovery initialization.
 * Not for a Find window: its transient address has a different owner.
 * No record, UID fallback, SDK setter or persistent write. Output stays
 * unchanged on read/validation failure; absence is never inferred here. */
uint32_t inktag_nrf_ble_address_read(ble_gap_addr_t *output);

#endif
