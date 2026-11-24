#ifndef INKTAG_NRF_PASSWORD_H
#define INKTAG_NRF_PASSWORD_H

#include "inktag_device_password_store.h"
#include "inktag_epd_config.h"

/* Borrow the existing GUI/transfer scratch; no persistent QR/bundle buffer.
 * Common interprets V/W and publication. This adapter normalizes only the
 * Nordic legacy four-byte ordinary QR envelope. Unknown bytes fail closed. */
inktag_device_password_store_result_t inktag_nrf_password_load(
    uint8_t scratch[420], inktag_device_password_config_t *config,
    uint8_t *modules, uint16_t *matrix_offset, uint16_t *record_length);
bool inktag_nrf_password_commit(uint8_t scratch[420], uint16_t record_length,
    inktag_device_password_config_t *actual);
bool inktag_nrf_password_read_qr(epd_device_info_qr_t *qr);
void inktag_nrf_password_wipe(void *data, uint16_t length);
/* Application-only DFU entry fence; implementation shares V2 session state. */
bool inktag_nrf_password_recovery_allowed(void);

#endif
