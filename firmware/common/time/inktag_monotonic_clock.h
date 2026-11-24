#ifndef INKTAG_MONOTONIC_CLOCK_H
#define INKTAG_MONOTONIC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_tick;
    uint32_t counter_mask;
    uint32_t ticks_per_second;
    uint32_t second_remainder;
    uint32_t millisecond_remainder;
    bool initialized;
} inktag_monotonic_clock_t;

typedef struct {
    uint32_t ticks;
    uint32_t seconds;
    uint32_t milliseconds;
} inktag_elapsed_time_t;

bool inktag_monotonic_clock_init(inktag_monotonic_clock_t *clock,
                                 uint32_t current_tick,
                                 uint32_t ticks_per_second,
                                 uint8_t counter_bits);
inktag_elapsed_time_t inktag_monotonic_clock_sample(inktag_monotonic_clock_t *clock,
                                                    uint32_t current_tick);
void inktag_monotonic_clock_rebase_wall(inktag_monotonic_clock_t *clock);
bool inktag_time_boundary_crossed(uint32_t previous_timestamp,
                                  uint32_t current_timestamp,
                                  uint32_t interval_seconds);

#endif
