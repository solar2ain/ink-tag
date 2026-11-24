#include <stdint.h>

/* The pinned TC32 runtime omits GCC's double-word helpers. Keep the common
 * Find schedule byte-identical across platforms by supplying the standard
 * unsigned ABI locally instead of replacing its 64-bit arithmetic with a
 * TLSR-only algorithm. Every operation below is expressed in 32-bit limbs so
 * the implementation cannot recursively depend on the missing helpers. */
typedef union {
    uint64_t value;
    struct {
        uint32_t low;
        uint32_t high;
    } word;
} tlsr_u64_t;

static void multiply_u32(uint32_t left, uint32_t right,
                         uint32_t *high, uint32_t *low) {
    uint32_t left_low = left & 0xffffu;
    uint32_t left_high = left >> 16;
    uint32_t right_low = right & 0xffffu;
    uint32_t right_high = right >> 16;
    uint32_t low_low = left_low * right_low;
    uint32_t middle_a = left_high * right_low;
    uint32_t middle_b = left_low * right_high;
    uint32_t high_high = left_high * right_high;
    uint32_t carry = (low_low >> 16) +
                     (middle_a & 0xffffu) +
                     (middle_b & 0xffffu);
    *low = (low_low & 0xffffu) | (carry << 16);
    *high = high_high + (middle_a >> 16) + (middle_b >> 16) +
            (carry >> 16);
}

uint64_t __muldi3(uint64_t left_value, uint64_t right_value) {
    tlsr_u64_t left = {left_value};
    tlsr_u64_t right = {right_value};
    tlsr_u64_t result;
    uint32_t product_high;
    multiply_u32(left.word.low, right.word.low,
                 &product_high, &result.word.low);
    result.word.high = product_high +
        left.word.high * right.word.low +
        left.word.low * right.word.high;
    return result.value;
}

uint64_t __ashldi3(uint64_t value, int shift) {
    tlsr_u64_t input = {value};
    tlsr_u64_t result;
    uint8_t count = (uint8_t)shift & 63u;
    if (count == 0u) return value;
    if (count < 32u) {
        result.word.high = (input.word.high << count) |
                           (input.word.low >> (32u - count));
        result.word.low = input.word.low << count;
    } else {
        result.word.high = input.word.low << (count - 32u);
        result.word.low = 0u;
    }
    return result.value;
}

uint64_t __lshrdi3(uint64_t value, int shift) {
    tlsr_u64_t input = {value};
    tlsr_u64_t result;
    uint8_t count = (uint8_t)shift & 63u;
    if (count == 0u) return value;
    if (count < 32u) {
        result.word.low = (input.word.low >> count) |
                          (input.word.high << (32u - count));
        result.word.high = input.word.high >> count;
    } else {
        result.word.low = input.word.high >> (count - 32u);
        result.word.high = 0u;
    }
    return result.value;
}

static int compare_words(const tlsr_u64_t *left, const tlsr_u64_t *right) {
    if (left->word.high != right->word.high)
        return left->word.high > right->word.high ? 1 : -1;
    if (left->word.low != right->word.low)
        return left->word.low > right->word.low ? 1 : -1;
    return 0;
}

static void subtract_words(tlsr_u64_t *left, const tlsr_u64_t *right) {
    uint32_t prior_low = left->word.low;
    left->word.low -= right->word.low;
    left->word.high -= right->word.high +
                       (prior_low < right->word.low ? 1u : 0u);
}

static tlsr_u64_t divide_words(tlsr_u64_t numerator,
                               tlsr_u64_t denominator,
                               tlsr_u64_t *remainder_output) {
    tlsr_u64_t quotient = {0u};
    tlsr_u64_t remainder = {0u};
    int bit;
    if (denominator.word.low == 0u && denominator.word.high == 0u) {
        if (remainder_output != 0) *remainder_output = numerator;
        quotient.word.low = UINT32_MAX;
        quotient.word.high = UINT32_MAX;
        return quotient;
    }
    for (bit = 63; bit >= 0; --bit) {
        uint32_t incoming = bit >= 32 ?
            ((numerator.word.high >> (bit - 32)) & 1u) :
            ((numerator.word.low >> bit) & 1u);
        remainder.word.high = (remainder.word.high << 1) |
                              (remainder.word.low >> 31);
        remainder.word.low = (remainder.word.low << 1) | incoming;
        if (compare_words(&remainder, &denominator) >= 0) {
            subtract_words(&remainder, &denominator);
            if (bit >= 32)
                quotient.word.high |= UINT32_C(1) << (bit - 32);
            else
                quotient.word.low |= UINT32_C(1) << bit;
        }
    }
    if (remainder_output != 0) *remainder_output = remainder;
    return quotient;
}

uint64_t __udivdi3(uint64_t numerator_value, uint64_t denominator_value) {
    tlsr_u64_t numerator = {numerator_value};
    tlsr_u64_t denominator = {denominator_value};
    return divide_words(numerator, denominator, 0).value;
}

uint64_t __umoddi3(uint64_t numerator_value, uint64_t denominator_value) {
    tlsr_u64_t numerator = {numerator_value};
    tlsr_u64_t denominator = {denominator_value};
    tlsr_u64_t remainder;
    (void)divide_words(numerator, denominator, &remainder);
    return remainder.value;
}
