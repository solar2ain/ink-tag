#ifndef INKTAG_SHA256_H
#define INKTAG_SHA256_H

#include <stdint.h>

#if defined(INKTAG_SHA256_ENABLE_STREAMING)
typedef struct {
    uint32_t state[8];
    uint32_t bits_high;
    uint32_t bits_low;
    uint8_t block[64];
    uint8_t used;
} inktag_sha256_context_t;

void inktag_sha256_init(inktag_sha256_context_t *context);
void inktag_sha256_update(inktag_sha256_context_t *context,
                          const uint8_t *data, uint32_t length);
void inktag_sha256_final(inktag_sha256_context_t *context,
                         uint8_t output[32]);
#endif
void inktag_sha256(const uint8_t *data, uint32_t length, uint8_t output[32]);
/* Bounded internal HMAC for a 32-byte key. The caller puts the message at
 * scratch+64, provides max(64+length,96) bytes and securely wipes scratch after
 * use. scratch/key/output must not overlap. Avoids two proof implementations
 * each embedding the same pad/hash operations or allocating another buffer. */
void inktag_hmac_sha256_32_inplace(const uint8_t key[32], uint8_t *scratch,
                                 uint16_t length, uint8_t output[32]);

#endif
