#ifndef INKTAG_NRF_DFU_POWER_H
#define INKTAG_NRF_DFU_POWER_H

#include <stdbool.h>

/* Admit a new, fully prevalidated Secure DFU transaction at the product OTA
 * threshold. This is deliberately separate from physical Flash writes so an
 * accepted update can survive a temporary 2.40-2.60 V supply sag. */
bool inktag_nrf_dfu_ota_admission_allowed(void);

/* Take a fresh VDD sample at the physical Flash-write floor for every Secure
 * DFU erase/write submission, including settings and confirmation metadata. */
bool inktag_nrf_dfu_power_allows_write(void);

#endif
