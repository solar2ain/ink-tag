#ifndef INKTAG_NRF_BUTTONLESS_H
#define INKTAG_NRF_BUTTONLESS_H

#include <stdbool.h>
#include <stdint.h>

/* Continue one V2 request admitted before its ACK. The caller retains fresh
 * power checks; this does not authorize another BLE connection or request. */
uint32_t inktag_nrf_buttonless_recovery_start_accepted(bool accepted);

#endif
