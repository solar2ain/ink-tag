#ifndef INKTAG_LZO1X_H
#define INKTAG_LZO1X_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Preserve the complete API by default. A measured decode-only build may opt
 * out only when it has no validate/PACKED-LUT consumer. Calling the omitted
 * entry then fails to link; it never becomes a successful validation stub. */
#ifndef INKTAG_LZO1X_VALIDATE_ENABLED
#define INKTAG_LZO1X_VALIDATE_ENABLED 1
#endif
#if INKTAG_LZO1X_VALIDATE_ENABLED != 0 && INKTAG_LZO1X_VALIDATE_ENABLED != 1
#error INKTAG_LZO1X_VALIDATE_ENABLED_must_be_zero_or_one
#endif
/* LZO1X block, including its end marker. No dictionary, heap or shared state.
 * Input/output must be disjoint. On failure the caller discards all output. */
bool inktag_lzo1x_decode(const uint8_t *src, size_t length,
                        uint8_t *dst, size_t capacity, size_t *written);
/* Validate the same complete stream and exact output length without writing.
 * Used by immutable LUT banks that require failure-before-output semantics. */
bool inktag_lzo1x_validate(const uint8_t *src, size_t length, size_t decoded_length);
#endif
