/* Nordic DFU image integrity; no publisher keys or signature implementation. */
#include "inktag_nrf_dfu_hash.h"
#include <string.h>
#include "sha256.h"

bool inktag_nrf_dfu_sha256_matches_le(uint8_t const expected[32],
                                     uint8_t const *data, uint32_t length)
{
    sha256_context_t context;
    uint8_t actual[32];
    return sha256_init(&context) == 0 &&
           sha256_update(&context, data, length) == 0 &&
           sha256_final(&context, actual, true) == 0 &&
           memcmp(actual, expected, sizeof(actual)) == 0;
}
