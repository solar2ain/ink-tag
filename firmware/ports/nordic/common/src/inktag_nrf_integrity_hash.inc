/* Shared SDK SHA provider; no signature/key implementation. */
#line 12 "firmware/ports/nordic/common/src/inktag_nrf_dfu_crypto.c"
#if !defined(INKTAG_BUILD_PRO) || INKTAG_BUILD_PRO
#include "inktag_nrf_integrity_hash_pro.inc"
#endif

INKTAG_GUARD_B static bool sha256_le(uint8_t output[32], uint8_t const *data,
                      uint32_t length)
{
    sha256_context_t context;

    /* All three arguments are owned, non-null buffers here. Nordic's software
     * SHA implementation can fail only its public null-argument guards. */
    (void)sha256_init(&context);
    (void)sha256_update(&context, data, length);
    (void)sha256_final(&context, output, true);
    return true;
}

INKTAG_GUARD_B bool inktag_nrf_dfu_sha256_matches_le(uint8_t const expected[32],
                                      uint8_t const *data,
                                      uint32_t length)
{
    uint8_t actual[32];
    return sha256_le(actual, data, length) &&
           memcmp(actual, expected, sizeof(actual)) == 0;
}

