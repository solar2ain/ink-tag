#ifndef INKTAG_OM_SWAP_H
#define INKTAG_OM_SWAP_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_swap.h"

#define INKTAG_OM_FLASH_REQUIRED_SIZE 0x00100000u
#define INKTAG_OM_RECOVERY_OFFSET     0x00004000u
#if defined(INKTAG_OM_NEXT_LAYOUT)
#define INKTAG_OM_RECOVERY_SIZE       0x00026000u
#define INKTAG_OM_SLOT_A              0x0002A000u
#define INKTAG_OM_SLOT_SIZE           0x0004E000u
#define INKTAG_OM_CONTROL_A           0x00082000u
#define INKTAG_OM_CONTROL_B           0x00083000u
#define INKTAG_OM_APP_XIP_ADDRESS     0x0042A000u
#define INKTAG_OM_LAUNCH_RECORD_XIP   0x0042A100u
#else
#define INKTAG_OM_RECOVERY_SIZE       0x00027000u
#define INKTAG_OM_SLOT_A              0x0002B000u
#define INKTAG_OM_SLOT_SIZE           0x0004A000u
#define INKTAG_OM_CONTROL_A           0x0007D000u
#define INKTAG_OM_CONTROL_B           0x0007E000u
#define INKTAG_OM_REQUEST_SECTOR      0x0007F000u
#define INKTAG_OM_APP_XIP_ADDRESS     0x0042B000u
#define INKTAG_OM_LAUNCH_RECORD_XIP   0x0042B100u
#endif
#define INKTAG_OM_FIXED_BOOT_XIP      0x00404000u
#define INKTAG_OM_FIXED_BOOT_ENTRY_XIP 0x00404101u
#define INKTAG_OM_LAUNCH_RECORD_MAGIC 0x314C4D4Fu
#define INKTAG_OM_LAUNCH_RECORD_GUARD 0xB007A55Au

typedef struct {
    uint32_t magic;
    uint32_t reset_handler;
    uint32_t guard;
} inktag_om_launch_record_t;

typedef enum {
    INKTAG_OM_RECOVERY_REQUEST_OK = 0,
    INKTAG_OM_RECOVERY_REQUEST_LOW_POWER = 1,
    INKTAG_OM_RECOVERY_REQUEST_UNSUPPORTED = 2,
} inktag_om_recovery_request_result_t;

bool inktag_om_swap_supported(void);
#if defined(INKTAG_OM_FIXED_RECOVERY)
/* Authenticates current Flash bytes and checks the durable epoch floor.
 * Lifecycle/journal admission and transport completion remain caller-owned. */
bool inktag_om_swap_installed_app_valid(void);
/* Private fixed-Recovery data flow; never a wire version or persisted floor. */
uint32_t inktag_om_swap_verified_app_size(void);
/* Read-only session guard, before an ACK grants permission to cancel it. */
bool inktag_om_swap_unmodified_recovery(void);
#endif
inktag_om_recovery_request_result_t inktag_om_recovery_request(void);
bool inktag_om_recovery_requested(void);
/* Abandon a controlled Recovery entry only while the previously running
 * application is still untouched and the durable journal has no candidate
 * which must remain in Recovery. The caller must separately validate the
 * installed application before rebooting into it. */
bool inktag_om_swap_cancel_unmodified_recovery(void);
/* Persistently close the safe-resume path before the first application-sector
 * erase or program. Repeated calls are idempotent; an uncertain/partial marker
 * fails closed. */
bool inktag_om_swap_mark_application_mutated(void);
inktag_swap_result_t inktag_om_swap_prepare_candidate(uint32_t image_size,
                                                       uint32_t release_version,
                                                       uint32_t image_crc32);
inktag_swap_result_t inktag_om_swap_boot(inktag_swap_watchdog_fn progress,
                                         void *context);
inktag_swap_result_t inktag_om_swap_confirm(void);
inktag_swap_result_t inktag_om_swap_status(inktag_swap_state_t *state,
                                           uint32_t *generation);
/* Board-specific full images place a one-shot device_info request in the shared
 * request sector. The application replaces it with a Recovery request before
 * a single-slot OTA; Recovery clears that request only after arming a trial. */
/* Keep the existing Full marker until NFC authority and EEPROM readback have
 * completed. This reuses the request sector, not a new transaction format. */
bool inktag_om_swap_factory_device_info_requested(void);
bool inktag_om_swap_finish_factory_device_info_request(void);
/* Enter the vendor reboot-sleep path so BLE SRAM is power-cycled, then reboot
 * through ROM/MBR into fixed Recovery before selecting the application. */
void inktag_om_swap_enter_fixed_boot(void);

#endif
