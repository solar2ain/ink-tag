#ifndef INKTAG_ELAPSED_CLOCK_H
#define INKTAG_ELAPSED_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Product-level wall-clock accumulator.
 *
 * A periodic callback is only a chance to sample time.  It is not itself a
 * clock: cooperative schedulers and masked interrupts may coalesce several
 * expirations while an EPD operation is active.  Each port supplies an
 * monotonic counter that accounts for low-power intervals and its rate.
 */
typedef struct {
    uint32_t last_tick;
    uint32_t remainder_ticks;
    uint32_t millisecond_remainder;
    uint32_t counter_mask;
    uint32_t ticks_per_second;
} inktag_elapsed_clock_t;

typedef struct {
    uint32_t ticks;
    uint32_t seconds;
    uint32_t milliseconds;
} inktag_elapsed_time_t;

static inline uint32_t inktag_elapsed_clock_gcd(uint32_t left,
                                                uint32_t right)
{
    while (right != 0u) {
        uint32_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

static inline void inktag_elapsed_clock_reset(inktag_elapsed_clock_t *clock,
                                               uint32_t now_tick,
                                               uint32_t counter_mask,
                                               uint32_t ticks_per_second)
{
    clock->last_tick = now_tick & counter_mask;
    clock->remainder_ticks = 0u;
    clock->millisecond_remainder = 0u;
    clock->counter_mask = counter_mask;
    clock->ticks_per_second = ticks_per_second;
}

static inline inktag_elapsed_time_t inktag_elapsed_clock_sample_time(
    inktag_elapsed_clock_t *clock, uint32_t now_tick)
{
    inktag_elapsed_time_t elapsed = {0u, 0u, 0u};
    uint32_t delta;
    uint32_t whole_seconds;
    uint32_t subsecond_ticks;
    uint32_t millisecond_total;
    uint32_t millisecond_gcd;
    uint32_t millisecond_multiplier;
    uint32_t millisecond_divisor;

    if (clock->ticks_per_second == 0u) return elapsed;
    now_tick &= clock->counter_mask;
    delta = (now_tick - clock->last_tick) & clock->counter_mask;
    clock->last_tick = now_tick;
    elapsed.ticks = delta;
    whole_seconds = delta / clock->ticks_per_second;
    subsecond_ticks = delta % clock->ticks_per_second;
    elapsed.seconds = whole_seconds;
    if (subsecond_ticks >=
        clock->ticks_per_second - clock->remainder_ticks) {
        elapsed.seconds++;
        clock->remainder_ticks = subsecond_ticks -
            (clock->ticks_per_second - clock->remainder_ticks);
    } else {
        clock->remainder_ticks += subsecond_ticks;
    }
    /* Reduce 1000/ticks_per_second before multiplying. This supports both
     * low-frequency counters and recovered high-frequency system counters
     * without pulling 64-bit division into constrained MCU toolchains. */
    millisecond_gcd = inktag_elapsed_clock_gcd(
        clock->ticks_per_second, 1000u);
    millisecond_multiplier = 1000u / millisecond_gcd;
    millisecond_divisor = clock->ticks_per_second / millisecond_gcd;
    if (subsecond_ticks >
        (UINT32_MAX - clock->millisecond_remainder) /
            millisecond_multiplier) return elapsed;
    millisecond_total = clock->millisecond_remainder +
                        subsecond_ticks * millisecond_multiplier;
    elapsed.milliseconds = whole_seconds * 1000u +
        millisecond_total / millisecond_divisor;
    clock->millisecond_remainder =
        millisecond_total % millisecond_divisor;
    return elapsed;
}

static inline uint32_t inktag_elapsed_clock_sample(inktag_elapsed_clock_t *clock,
                                                    uint32_t now_tick)
{
    return inktag_elapsed_clock_sample_time(clock, now_tick).seconds;
}

/* Optional out-of-line entry point for non-LTO ports with repeated copies.
 * The inline API remains unchanged for ports that have not adopted it. */
inktag_elapsed_time_t inktag_elapsed_clock_sample_time_shared(
    inktag_elapsed_clock_t *clock, uint32_t now_tick);

static inline uint32_t inktag_elapsed_clock_sample_shared(
    inktag_elapsed_clock_t *clock, uint32_t now_tick)
{
    return inktag_elapsed_clock_sample_time_shared(clock, now_tick).seconds;
}

static inline bool inktag_elapsed_clock_crossed_period(uint32_t before,
                                                        uint32_t after,
                                                        uint32_t period_seconds)
{
    return period_seconds != 0u && before / period_seconds != after / period_seconds;
}

#endif
