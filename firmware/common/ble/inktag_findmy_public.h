#ifndef INKTAG_FINDMY_PUBLIC_H
#define INKTAG_FINDMY_PUBLIC_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_FINDMY_PUBLIC_VERSION 1u
#define INKTAG_FINDMY_PUBLIC_K_BYTES 16u
#define INKTAG_FINDMY_PUBLIC_SCALAR_BYTES 28u
#define INKTAG_FINDMY_PUBLIC_POINT_BYTES 56u
#define INKTAG_FINDMY_PUBLIC_SLOT_SECONDS 900u
#define INKTAG_FINDMY_PUBLIC_NONCE_BYTES 8u
#define INKTAG_FINDMY_PUBLIC_STAGE_BYTES 81u
#define INKTAG_FINDMY_PUBLIC_REPLACE_STAGE_BYTES 113u
#define INKTAG_FINDMY_PUBLIC_STATUS_BYTES 62u
#define INKTAG_FINDMY_PUBLIC_COMMIT_BYTES 53u
#define INKTAG_FINDMY_PUBLIC_STATUS_IDENTITY_PRESENT 0x01u
#define INKTAG_FINDMY_PUBLIC_STATUS_CHALLENGE_OPEN 0x02u
#define INKTAG_FINDMY_PUBLIC_STATUS_IDENTITY_STAGED 0x04u
#define INKTAG_FINDMY_PUBLIC_STATUS_TIME_VALID 0x08u

typedef enum {
    INKTAG_FINDMY_PUBLIC_COEFFICIENT_U = 0,
    INKTAG_FINDMY_PUBLIC_COEFFICIENT_V = 1,
} inktag_findmy_public_coefficient_t;

typedef enum {
    INKTAG_FINDMY_PUBLIC_POINT_ERROR = 0,
    INKTAG_FINDMY_PUBLIC_POINT_FINITE = 1,
    INKTAG_FINDMY_PUBLIC_POINT_INFINITY = 2,
} inktag_findmy_public_point_result_t;

typedef inktag_findmy_public_point_result_t
    (*inktag_findmy_public_point_combination_fn)(
        const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
        const uint8_t u[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
        const uint8_t v[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
        uint8_t public_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES]);

typedef bool (*inktag_findmy_public_point_valid_fn)(
    const uint8_t public_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES]);

typedef struct {
    bool challenge_open;
    bool staged;
    bool replacing;
    uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES];
    uint8_t old_fingerprint[16];
    uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES];
    uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES];
} inktag_findmy_public_owner_stage_t;

void inktag_findmy_public_owner_clear(
    inktag_findmy_public_owner_stage_t *state);
bool inktag_findmy_public_owner_begin(
    inktag_findmy_public_owner_stage_t *state,
    const uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES]);
bool inktag_findmy_public_owner_stage(
    inktag_findmy_public_owner_stage_t *state,
    const uint8_t *payload, uint16_t length,
    inktag_findmy_public_point_valid_fn valid_point);
#ifdef INKTAG_ONMICRO
/* scratch holds 64 writable bytes followed by the message (32+ bytes). */
void inktag_findmy_public_hmac(
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES], uint8_t *scratch,
    uint16_t message_length, uint8_t digest[32]);
#endif

/* Existing-identity replacement has a fresh nonce and an HMAC under the old
 * K. The host holds the old K; the device never receives the old private d.
 * stage = 02 | nonce8 | oldFingerprint16 | newP56 | newK16 | tag16. */
bool inktag_findmy_public_owner_stage_replace(
    inktag_findmy_public_owner_stage_t *state,
    const uint8_t stage[INKTAG_FINDMY_PUBLIC_REPLACE_STAGE_BYTES],
    const uint8_t old_fingerprint[16],
    const uint8_t old_key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    const uint8_t activation_id[8],
    inktag_findmy_public_point_valid_fn valid_point);
/* Verify the 41-byte proof after the caller validates and consumes its nonce. */
bool inktag_findmy_public_owner_unregister_valid(
    const uint8_t payload[41], const uint8_t old_fingerprint[16],
    const uint8_t old_key[INKTAG_FINDMY_PUBLIC_K_BYTES], const uint8_t device_id[8]);
void inktag_findmy_public_owner_status_encode(
    uint8_t flags, const uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES],
    uint64_t slot, const uint8_t public_x[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
    const uint8_t fingerprint[16],
    uint8_t output[INKTAG_FINDMY_PUBLIC_STATUS_BYTES]);
void inktag_findmy_public_owner_commit_encode(
    uint64_t slot, const uint8_t public_x[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
    const uint8_t fingerprint[16],
    uint8_t output[INKTAG_FINDMY_PUBLIC_COMMIT_BYTES]);

/* HMAC-SHA256(K, "InkTag/Find/public-only/v2" || 00 || role ||
 * BE64(slot) || BE32(retry) || BE32(sample)); take the first 28 bytes BE.
 * Rejection sampling starts at sample zero. u is [1,n-1], v is [0,n-1]. */
bool inktag_findmy_public_coefficient(
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint64_t slot, uint32_t retry,
    inktag_findmy_public_coefficient_t role,
    uint8_t scalar[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
    uint32_t *accepted_sample);

/* Identity discriminator only, not owner proof: SHA256(domain ASCII without
 * terminator || P56 || K16)[0:16]. Never reveal K in status responses. */
bool inktag_findmy_public_fingerprint(
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint8_t fingerprint[16]);

/* The callback distinguishes an expected infinity retry from an ECC/input
 * error. No private scalar is accepted or computed by this API. */
bool inktag_findmy_public_derive(
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES], uint64_t slot,
    inktag_findmy_public_point_combination_fn combine,
    uint8_t public_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    uint32_t *accepted_retry);

#endif
