#ifndef INKTAG_FINDMY_OWNER_H
#define INKTAG_FINDMY_OWNER_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy_p224.h"

#define INKTAG_FINDMY_OWNER_ROTATION_VERSION 1u
#define INKTAG_FINDMY_OWNER_NONCE_BYTES 8u
#define INKTAG_FINDMY_OWNER_TAG_BYTES 16u
#define INKTAG_FINDMY_OWNER_STAGE_BYTES 41u
#define INKTAG_FINDMY_OWNER_FIRST_STAGE_BYTES 17u
#define INKTAG_FINDMY_OWNER_BEGIN_RESPONSE_BYTES 9u
#define INKTAG_FINDMY_OWNER_COMMIT_RESPONSE_BYTES 37u
#define INKTAG_FINDMY_OWNER_STATUS_RESPONSE_BYTES 46u

#define INKTAG_FINDMY_OWNER_STATUS_IDENTITY_PRESENT 0x01u
#define INKTAG_FINDMY_OWNER_STATUS_CHALLENGE_OPEN 0x02u
#define INKTAG_FINDMY_OWNER_STATUS_SEED_STAGED 0x04u
#define INKTAG_FINDMY_OWNER_STATUS_TIME_VALID 0x08u

/* Establish or rotate a Find owner through one bounded transaction.
 *
 * A seedless Pro device accepts exactly version || seed (17 bytes) after an
 * explicitly confirmed BEGIN. This is the only first-owner path: activation
 * never creates a seed and there is no factory/debug seed adapter. Once a seed
 * exists, the current seed is the owner proof and STAGE accepts only
 * version || nonce || encrypted-new-seed || AES-CMAC (41 bytes).
 */
bool inktag_findmy_owner_first_stage_decode(
    const uint8_t *payload, uint16_t payload_length,
    uint8_t new_seed[INKTAG_FINDMY_P224_SECRET_BYTES]);

bool inktag_findmy_owner_wrap(
    const uint8_t current_seed[INKTAG_FINDMY_P224_SECRET_BYTES],
    const uint8_t new_seed[INKTAG_FINDMY_P224_SECRET_BYTES],
    const uint8_t nonce[INKTAG_FINDMY_OWNER_NONCE_BYTES],
    inktag_findmy_p224_aes128_encrypt_fn aes128_encrypt,
    uint8_t payload[INKTAG_FINDMY_OWNER_STAGE_BYTES]);

bool inktag_findmy_owner_unwrap(
    const uint8_t current_seed[INKTAG_FINDMY_P224_SECRET_BYTES],
    const uint8_t expected_nonce[INKTAG_FINDMY_OWNER_NONCE_BYTES],
    const uint8_t *payload, uint16_t payload_length,
    inktag_findmy_p224_aes128_encrypt_fn aes128_encrypt,
    uint8_t new_seed[INKTAG_FINDMY_P224_SECRET_BYTES]);

#endif
