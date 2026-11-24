#ifndef INKTAG_NRF_DFU_RESUME_H
#define INKTAG_NRF_DFU_RESUME_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_nrf_dfu_policy.h"

/* Native SecureDFU protocol-version query advertises this exact guarded
 * ABORT contract. Standard version 1 / unsupported is never a capability. */

bool inktag_nrf_dfu_resume_boot_valid(void);
bool inktag_nrf_dfu_resume_allowed(void);
void inktag_nrf_dfu_resume_touch(void);
uint8_t inktag_nrf_dfu_resume_prepare(void);
bool inktag_nrf_dfu_resume_pending(void);
void inktag_nrf_dfu_resume_response_sent(uint8_t opcode, uint8_t result,
                                        uint32_t error);
void inktag_nrf_dfu_resume_tx_complete(uint16_t count);
void inktag_nrf_dfu_resume_disconnect(void);
/* Existing init metadata must invalidate BOTH authoritative SDK pages
 * before any physical application mutation; no additional Flash marker. */
bool inktag_nrf_dfu_resume_app_write_allowed(void);

#endif
