#ifndef INKTAG_GR_SWAP_H
#define INKTAG_GR_SWAP_H

#include <stdbool.h>
#include <stdint.h>

#include "gr5xx_dfu.h"
#include "inktag_swap.h"

/* Bootloader entry points. */
inktag_swap_result_t inktag_gr_swap_boot(void);
bool inktag_gr_recovery_required(void);

/* Full images carry an at-most-once first-display request in the otherwise
 * reserved tail of the vendor DFU-info page. OTA rewrites that page without
 * the request, so an installed trial can never be mistaken for a Full boot. */
bool inktag_gr_first_display_request_pending(void);
bool inktag_gr_first_display_request_consume(void);
bool inktag_gr_ota_install_boot(void);

/* Layout v3 Full images and every later v3 OTA carry a signed marker in the
 * otherwise reserved final eight bytes of the 48-byte application image-info.
 * A v2 Recovery copies only the 40-byte vendor struct, so a device cannot
 * silently become a mixed v2-Recovery/v3-application installation. */
bool inktag_gr_layout_v3_installed(void);

/* The application writes this record only after the vendor DFU request has
 * been committed at a valid >=2.6 V sample. Recovery changes the second pair
 * before the first application erase/program, making safe resume fail closed
 * across resets and partial writes. */
bool inktag_dfu_recovery_request_mark(void);
bool inktag_dfu_application_mutation_mark(void);

/* Shared by automatic pre-mutation rejection and the explicit Recovery
 * command. This never confirms a trial or changes the compatibility epoch. */
bool inktag_dfu_safe_resume_allowed(void);
/* Revalidate and clear the controlled request only after the response's TX
 * completion and from the native main loop, never from a BLE callback. */
bool inktag_dfu_safe_resume_commit(void);

/* Application health acknowledgement. Safe to call on every normal boot. */
inktag_swap_result_t inktag_gr_swap_confirm(void);

/* Read-only application identity used by host-side OTA attestation. */
bool inktag_gr_active_image_info(dfu_image_info_t *active);
inktag_swap_result_t inktag_gr_swap_status(inktag_swap_state_t *state,
                                           uint32_t *generation);

#endif
