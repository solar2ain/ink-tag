#include "inktag_sha256.h"

#include <string.h>

void inktag_hmac_sha256_32_inplace(const uint8_t key[32], uint8_t *scratch,
                                 uint16_t length, uint8_t output[32]) {
    uint8_t inner[32], i;
    memset(scratch, 0x36, 64u);
    for (i = 0u; i < 32u; ++i) scratch[i] ^= key[i];
    inktag_sha256(scratch, (uint32_t)length + 64u, inner);
    memset(scratch, 0x5c, 64u);
    for (i = 0u; i < 32u; ++i) scratch[i] ^= key[i];
    memcpy(scratch + 64u, inner, 32u);
    inktag_sha256(scratch, 96u, output);
    volatile uint8_t *p = inner;
    for (i = 0u; i < 32u; ++i) p[i] = 0u;
}

#if !defined(INKTAG_SHA256_ENABLE_STREAMING)
typedef struct {
    uint32_t state[8];
    uint32_t bits_high;
    uint32_t bits_low;
    uint8_t block[64];
    uint8_t used;
} inktag_sha256_context_t;
#endif

static const uint32_t k_sha256[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

static uint32_t rotate_right(uint32_t value, uint8_t shift) {
    return (value >> shift) | (value << (32u - shift));
}

static uint32_t read_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void transform(inktag_sha256_context_t *context,
                      const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    for (uint8_t i = 0u; i < 16u; i++) words[i] = read_be32(&block[i * 4u]);
    for (uint8_t i = 16u; i < 64u; i++) {
        uint32_t s0 = rotate_right(words[i - 15u], 7u) ^
                      rotate_right(words[i - 15u], 18u) ^
                      (words[i - 15u] >> 3u);
        uint32_t s1 = rotate_right(words[i - 2u], 17u) ^
                      rotate_right(words[i - 2u], 19u) ^
                      (words[i - 2u] >> 10u);
        words[i] = words[i - 16u] + s0 + words[i - 7u] + s1;
    }
    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (uint8_t i = 0u; i < 64u; i++) {
        uint32_t s1 = rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice + k_sha256[i] + words[i];
        uint32_t s0 = rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
    memset(words, 0, sizeof(words));
}

static void sha256_update(inktag_sha256_context_t *context,
                          const uint8_t *data, uint32_t length) {
    uint32_t added_low;
    uint32_t old_low;
    added_low = length << 3u;
    old_low = context->bits_low;
    context->bits_low += added_low;
    context->bits_high += (length >> 29u) +
                          (context->bits_low < old_low ? 1u : 0u);
    while (length > 0u) {
        uint8_t available = (uint8_t)(64u - context->used);
        uint8_t take = length < available ? (uint8_t)length : available;
        memcpy(&context->block[context->used], data, take);
        context->used = (uint8_t)(context->used + take);
        data += take;
        length -= take;
        if (context->used == 64u) {
            transform(context, context->block);
            context->used = 0u;
        }
    }
}

#if defined(INKTAG_SHA256_ENABLE_STREAMING)
void inktag_sha256_init(inktag_sha256_context_t *context) {
    static const uint32_t initial_state[8] = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
    };
    if (context == NULL) return;
    memset(context, 0, sizeof(*context));
    memcpy(context->state, initial_state, sizeof(initial_state));
}

void inktag_sha256_update(inktag_sha256_context_t *context,
                          const uint8_t *data, uint32_t length) {
    if (context == NULL || (data == NULL && length != 0u)) return;
    sha256_update(context, data, length);
}

void inktag_sha256_final(inktag_sha256_context_t *context,
                         uint8_t output[32]) {
    uint32_t bits_high;
    uint32_t bits_low;
    if (context == NULL || output == NULL) return;
    bits_high = context->bits_high;
    bits_low = context->bits_low;
    context->block[context->used++] = 0x80u;
    if (context->used > 56u) {
        memset(&context->block[context->used], 0, 64u - context->used);
        transform(context, context->block);
        context->used = 0u;
    }
    memset(&context->block[context->used], 0, 56u - context->used);
    context->block[56] = (uint8_t)(bits_high >> 24);
    context->block[57] = (uint8_t)(bits_high >> 16);
    context->block[58] = (uint8_t)(bits_high >> 8);
    context->block[59] = (uint8_t)bits_high;
    context->block[60] = (uint8_t)(bits_low >> 24);
    context->block[61] = (uint8_t)(bits_low >> 16);
    context->block[62] = (uint8_t)(bits_low >> 8);
    context->block[63] = (uint8_t)bits_low;
    transform(context, context->block);
    for (uint8_t i = 0u; i < 8u; i++) {
        output[i * 4u] = (uint8_t)(context->state[i] >> 24);
        output[i * 4u + 1u] = (uint8_t)(context->state[i] >> 16);
        output[i * 4u + 2u] = (uint8_t)(context->state[i] >> 8);
        output[i * 4u + 3u] = (uint8_t)context->state[i];
    }
    memset(context, 0, sizeof(*context));
}
#endif

void inktag_sha256(const uint8_t *data, uint32_t length, uint8_t output[32]) {
#if defined(INKTAG_SHA256_ENABLE_STREAMING)
    inktag_sha256_context_t context;
    inktag_sha256_init(&context);
    inktag_sha256_update(&context, data, length);
    inktag_sha256_final(&context, output);
#else
    inktag_sha256_context_t context = {
        {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
         0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u},
        0u, 0u, {0}, 0u
    };
    uint32_t bits_high;
    uint32_t bits_low;
    if (data != NULL && length > 0u) sha256_update(&context, data, length);
    bits_high = context.bits_high;
    bits_low = context.bits_low;
    context.block[context.used++] = 0x80u;
    if (context.used > 56u) {
        memset(&context.block[context.used], 0, 64u - context.used);
        transform(&context, context.block);
        context.used = 0u;
    }
    memset(&context.block[context.used], 0, 56u - context.used);
    context.block[56] = (uint8_t)(bits_high >> 24);
    context.block[57] = (uint8_t)(bits_high >> 16);
    context.block[58] = (uint8_t)(bits_high >> 8);
    context.block[59] = (uint8_t)bits_high;
    context.block[60] = (uint8_t)(bits_low >> 24);
    context.block[61] = (uint8_t)(bits_low >> 16);
    context.block[62] = (uint8_t)(bits_low >> 8);
    context.block[63] = (uint8_t)bits_low;
    transform(&context, context.block);
    for (uint8_t i = 0u; i < 8u; i++) {
        output[i * 4u] = (uint8_t)(context.state[i] >> 24);
        output[i * 4u + 1u] = (uint8_t)(context.state[i] >> 16);
        output[i * 4u + 2u] = (uint8_t)(context.state[i] >> 8);
        output[i * 4u + 3u] = (uint8_t)context.state[i];
    }
    memset(&context, 0, sizeof(context));
#endif
}
