#ifndef INKTAG_TLSR_P224_UECC_H
#define INKTAG_TLSR_P224_UECC_H

#include "inktag_tlsr_build_profile.h"
#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy_public.h"

/* Public-only rolling identity primitive: u*P + v*G. All coordinates and
 * scalars use fixed-width big-endian wire order. P must be a valid finite
 * secp224r1 point; u is nonzero, v may be zero. Infinity is rejected. */
inktag_findmy_public_point_result_t
inktag_tlsr_p224_linear_combination_result(
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t u[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
    const uint8_t v[INKTAG_FINDMY_PUBLIC_SCALAR_BYTES],
    uint8_t public_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES]);

bool inktag_tlsr_p224_valid_public_point(
    const uint8_t public_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES]);

#endif
