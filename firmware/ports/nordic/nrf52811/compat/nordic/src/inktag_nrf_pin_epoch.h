#ifndef INKTAG_NRF_PIN_EPOCH_H
#define INKTAG_NRF_PIN_EPOCH_H
#include <stdbool.h>
#include <stddef.h>

/* SDK17 settings v2 CRC header ends at init_command, offset92. The runtime
 * uses actual read-only native settings; this pure checker also admits tests
 * built from the maintained Full encoder's real bytes. */
bool inktag_nrf_pin_epoch_settings_ready(const void *settings, size_t length);
#endif
