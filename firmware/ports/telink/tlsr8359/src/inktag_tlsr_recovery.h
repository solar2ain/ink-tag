#ifndef INKTAG_TLSR_RECOVERY_H
#define INKTAG_TLSR_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_protocol_v2.h"

/* Application-side fixed-Recovery contract. */
bool inktag_tlsr_recovery_boot(void);
bool inktag_tlsr_recovery_confirm(void);
bool inktag_tlsr_recovery_confirmation_pending(void);
bool inktag_tlsr_recovery_prepare_entry(void);
/* Preserve the execution-site denial or marker readback failure. */
inktag_status_v2_t inktag_tlsr_recovery_prepare_entry_status(void);

/* Recovery-side commit contract. */
bool inktag_tlsr_recovery_arm_compatibility_epoch(
    uint32_t compatibility_epoch);
bool inktag_tlsr_application_marker_valid(void);
bool inktag_tlsr_application_marker_commit(void);
bool inktag_tlsr_recovery_controlled_request_intact(void);
bool inktag_tlsr_recovery_return_ready(bool trial);

#endif
