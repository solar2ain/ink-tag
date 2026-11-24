#ifndef INKTAG_OM_FINDMY_H
#define INKTAG_OM_FINDMY_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy.h"
#include "inktag_findmy_public.h"

#define INKTAG_OM_FINDMY_ROTATION_PERIOD_SECONDS \
    INKTAG_FINDMY_PUBLIC_SLOT_SECONDS
#define INKTAG_OM_FINDMY_INVALID_SLOT_TOKEN 0xffu

typedef enum {
    INKTAG_OM_FIND_PUBLIC_EMPTY = 0,
    INKTAG_OM_FIND_PUBLIC_PRESENT_V2,
    INKTAG_OM_FIND_PUBLIC_LEGACY_V1,
    INKTAG_OM_FIND_PUBLIC_CORRUPT,
} inktag_om_find_public_state_t;

void inktag_om_findmy_init(bool *enabled);
bool inktag_om_findmy_write(bool enabled);
/* Clearing the runtime policy deliberately keeps the protected P/K record. */
bool inktag_om_findmy_clear(bool *enabled);

inktag_om_find_public_state_t inktag_om_findmy_public_state(void);
bool inktag_om_findmy_identity_present(void);
bool inktag_om_findmy_owner_nonce(
    uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES]);
bool inktag_om_findmy_public_fingerprint_read(uint8_t fingerprint[16]);
bool inktag_om_findmy_owner_stage_replace(
    inktag_findmy_public_owner_stage_t *stage,
    const uint8_t payload[INKTAG_FINDMY_PUBLIC_REPLACE_STAGE_BYTES],
    const uint8_t activation_id[8]);
/* First registration requires an empty logical tag. Replacement checks the
 * expected old fingerprint immediately before the atomic NVDS put. Neither
 * operation claims forensic erasure of obsolete physical generations. */
bool inktag_om_findmy_public_provision(
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint64_t unix_seconds, uint64_t *slot,
    uint8_t public_x[INKTAG_FINDMY_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[16]);
bool inktag_om_findmy_public_replace(
    const uint8_t old_fingerprint[16],
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint64_t unix_seconds, uint64_t *slot,
    uint8_t public_x[INKTAG_FINDMY_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[16]);
bool inktag_om_findmy_select_identity(
    uint64_t unix_seconds, bool time_valid, uint8_t *slot_token,
    uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);

#endif
