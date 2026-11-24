#ifndef INKTAG_FINDMY_P224_H
#define INKTAG_FINDMY_P224_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_FINDMY_P224_DERIVATION_VERSION 1u
#define INKTAG_FINDMY_P224_ROTATION_PERIOD_SECONDS 900u
#define INKTAG_FINDMY_P224_SECRET_BYTES 16u
#define INKTAG_FINDMY_P224_PRIVATE_BYTES 28u
#define INKTAG_FINDMY_P224_PUBLIC_BYTES 56u
#define INKTAG_FINDMY_P224_SEED_RECORD_VERSION 1u
#define INKTAG_FINDMY_P224_SEED_RECORD_BYTES 18u

typedef void (*inktag_findmy_p224_aes128_encrypt_fn)(
    const uint8_t key[INKTAG_FINDMY_P224_SECRET_BYTES],
    const uint8_t input[16], uint8_t output[16]);
typedef bool (*inktag_findmy_p224_public_key_fn)(
    const uint8_t private_key[INKTAG_FINDMY_P224_PRIVATE_BYTES],
    uint8_t public_xy[INKTAG_FINDMY_P224_PUBLIC_BYTES]);

/* Derive d and d*G for one UTC slot. public_xy is X || Y, both big-endian.
 * rejected_candidates may be NULL; when present it receives the independent
 * per-slot retry count r. AES and P-224 are injected vendor backends. */
bool inktag_findmy_p224_derive(
    const uint8_t secret[INKTAG_FINDMY_P224_SECRET_BYTES], uint64_t slot,
    inktag_findmy_p224_aes128_encrypt_fn aes128_encrypt,
    inktag_findmy_p224_public_key_fn compute_public_key,
    uint8_t public_xy[INKTAG_FINDMY_P224_PUBLIC_BYTES],
    uint32_t *rejected_candidates);

/* Persistent protected record: version, seed length, 16-byte seed. The
 * settings backend owns atomicity/integrity; this codec deliberately adds no
 * second checksum. Erased/zero placeholder seeds are rejected. */
bool inktag_findmy_p224_seed_encode(
    const uint8_t seed[INKTAG_FINDMY_P224_SECRET_BYTES],
    uint8_t record[INKTAG_FINDMY_P224_SEED_RECORD_BYTES]);
bool inktag_findmy_p224_seed_decode(
    const uint8_t *record, uint16_t length,
    uint8_t seed[INKTAG_FINDMY_P224_SECRET_BYTES]);

#endif
