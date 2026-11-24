#include <stdbool.h>
#include "inktag_code_guard.h"
#include "inktag_sha256.h"

/* Code guards do not require the publisher verifier or vendor PKC/RSA driver.
 * Preserve standard SHA-256 output with the existing base shared primitive. */
INKTAG_GUARD_B bool inktag_gr_code_sha256(const uint8_t *data, uint32_t size,
                                       uint8_t output[32]) {
    inktag_sha256(data, size, output);
    return true;
}
