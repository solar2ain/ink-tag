#include "inktag_monotonic_clock.h"

#include <limits.h>
#include <stddef.h>

bool inktag_monotonic_clock_init(inktag_monotonic_clock_t *clock,
                                 uint32_t current_tick,
                                 uint32_t ticks_per_second,
                                 uint8_t counter_bits) {
    uint32_t mask;
    if (clock == NULL || ticks_per_second == 0u ||
        ticks_per_second > UINT32_MAX / 1000u ||
        counter_bits == 0u || counter_bits > 32u)
        return false;

    mask = counter_bits == 32u ? UINT32_MAX : ((1u << counter_bits) - 1u);
    clock->last_tick = current_tick & mask;
    clock->counter_mask = mask;
    clock->ticks_per_second = ticks_per_second;
    clock->second_remainder = 0u;
    clock->millisecond_remainder = 0u;
    clock->initialized = true;
    return true;
}

inktag_elapsed_time_t inktag_monotonic_clock_sample(inktag_monotonic_clock_t *clock,
                                                    uint32_t current_tick) {
    inktag_elapsed_time_t elapsed = {0u, 0u, 0u};
    uint32_t whole_seconds;
    uint32_t subsecond_ticks;
    uint32_t scaled_ticks;

    if (clock == NULL || !clock->initialized) return elapsed;

    current_tick &= clock->counter_mask;
    elapsed.ticks = (current_tick - clock->last_tick) & clock->counter_mask;
    clock->last_tick = current_tick;

    whole_seconds = elapsed.ticks / clock->ticks_per_second;
    subsecond_ticks = elapsed.ticks % clock->ticks_per_second;

    scaled_ticks = clock->second_remainder + subsecond_ticks;
    elapsed.seconds = whole_seconds + scaled_ticks / clock->ticks_per_second;
    clock->second_remainder = scaled_ticks % clock->ticks_per_second;

    scaled_ticks = clock->millisecond_remainder + subsecond_ticks * 1000u;
    elapsed.milliseconds = whole_seconds * 1000u +
                           scaled_ticks / clock->ticks_per_second;
    clock->millisecond_remainder = scaled_ticks % clock->ticks_per_second;
    return elapsed;
}

void inktag_monotonic_clock_rebase_wall(inktag_monotonic_clock_t *clock) {
    if (clock != NULL) clock->second_remainder = 0u;
}

bool inktag_time_boundary_crossed(uint32_t previous_timestamp,
                                  uint32_t current_timestamp,
                                  uint32_t interval_seconds) {
    if (interval_seconds == 0u || current_timestamp <= previous_timestamp)
        return false;
    return previous_timestamp / interval_seconds !=
           current_timestamp / interval_seconds;
}
