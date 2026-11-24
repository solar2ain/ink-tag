#ifndef INKTAG_DA_RECOVERY_H
#define INKTAG_DA_RECOVERY_H

#include <stdint.h>

#include "inktag_recovery.h"

/* CRC-only Recovery shares this bounded production reader across OTA and
 * resume-app. It is not an App publisher-authentication API. */
bool inktag_da_recovery_application_crc_valid(void);

#define INKTAG_DA_RECOVERY_CONTROL_A 0x00037000u
#define INKTAG_DA_RECOVERY_CONTROL_B 0x0003B000u
#define INKTAG_DA_RECOVERY_SECTOR_SIZE 0x00001000u
#define INKTAG_DA_RECOVERY_LAYOUT_SIGNATURE 0x31564144u /* DAV1 */
#define INKTAG_DA_APPLICATION_START 0x00004000u
#define INKTAG_DA_RECOVERY_START 0x0001C000u
#define INKTAG_DA_APPLICATION_RAW_LIMIT 95744u
#define INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS 0x00035000u
#define INKTAG_DA_STOCK_SELECTOR_ADDRESS 0x00039000u
#define INKTAG_DA_FIRST_DISPLAY_MARKER_MAGIC 0x31444649u /* IFD1 */
#define INKTAG_DA_RECOVERY_REQUEST_ADDRESS \
    (INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS + 8u)
#define INKTAG_DA_RECOVERY_MUTATION_ADDRESS \
    (INKTAG_DA_FIRST_DISPLAY_MARKER_ADDRESS + 12u)
#define INKTAG_DA_RECOVERY_MARKER_ACTIVE 0x00u
#define INKTAG_DA_RECOVERY_MARKER_CLEAR 0xFFu

inktag_recovery_result_t inktag_da_recovery_boot(bool *ota_install_boot);
inktag_recovery_result_t inktag_da_recovery_confirm(void);
inktag_recovery_result_t inktag_da_recovery_arm(void);
inktag_recovery_result_t inktag_da_recovery_confirm_epoch(
    uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_da_recovery_arm_epoch(
    uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_da_recovery_check_epoch(
    uint32_t compatibility_epoch);
bool inktag_da_pin_epoch_confirmed(void);
inktag_recovery_result_t inktag_da_recovery_status(
    inktag_recovery_state_t *state, uint32_t *generation);
/* factory_selector is required and receives 0xFF when its one-byte read fails. */
bool inktag_da_first_display_requested(uint8_t *factory_selector);
bool inktag_da_first_display_consume(void);
bool inktag_da_application_info(uint32_t *raw_size, uint32_t *crc32);
bool inktag_da_enter_fixed_recovery(void);
/* Reload and execute the packaged pP selector without depending on a reset
 * that may preserve DA14585's address-zero SRAM remap. Returns only when the
 * factory loader header preflight is invalid. */
bool inktag_da_restart_via_secondary_bootloader(void);
bool inktag_da_recovery_mark_mutated(void);
/* Lightweight admission only; the full CRC and marker commit are repeated
 * by the confirmed-ACK main-loop continuation. Failures use the public OTA
 * rejection contract and never reset into an unverified application. */
bool inktag_da_recovery_resume_allowed(void);
bool inktag_da_recovery_resume_application(void);
bool inktag_da_recovery_release_request(void);
#if defined(INKTAG_DA_FIXED_RECOVERY)
/* SDK adapter: service a live controller between completed CRC chunk reads. */
void inktag_da_recovery_service_ble(void);
#endif

#endif
