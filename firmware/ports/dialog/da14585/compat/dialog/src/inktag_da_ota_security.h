#ifndef INKTAG_DA_OTA_SECURITY_H
#define INKTAG_DA_OTA_SECURITY_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_ota_reject.h"

#define INKTAG_DA_PQ_HEADER_BYTES 64u
#define INKTAG_DA_PQ_PREFLIGHT_OFFSET 40u
#define INKTAG_DA_PQ_PREFLIGHT_MAGIC 0x31504B49u /* "IKP1" */

/* Validate duplicated target/size/epoch metadata before the first erase.
 * CRC-only Recovery checks native payload CRC after arrival. Historical
 * optional/required implementations are not part of this source profile. */
bool inktag_da_ota_preflight(const uint8_t *pq_header, uint32_t length);
bool inktag_da_ota_verify_and_arm(void);

void inktag_da_ota_reject_clear(void);
bool inktag_da_ota_reject_pending(void);
void inktag_da_ota_reject_set(
    inktag_ota_reject_reason_t reason, inktag_ota_reject_stage_t stage,
    uint8_t flags, uint32_t detail, uint16_t sampled_mv,
    uint16_t threshold_mv);
void inktag_da_ota_reject_set_default(
    inktag_ota_reject_reason_t reason, inktag_ota_reject_stage_t stage,
    uint8_t flags, uint32_t detail);
void inktag_da_ota_reject_power(
    inktag_ota_reject_stage_t stage, uint32_t detail,
    uint16_t threshold_mv, bool detail_is_address);
uint8_t inktag_da_ota_status_encode(
    uint8_t status, uint8_t *output, uint8_t capacity);

#endif
