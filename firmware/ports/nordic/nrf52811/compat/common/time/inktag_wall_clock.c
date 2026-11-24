#include "inktag_wall_clock.h"

#include <stddef.h>

#define INKTAG_SECONDS_PER_MINUTE 60u
#define INKTAG_SECONDS_PER_DAY 86400u

static bool mask_is_wrapping_low_bits(uint32_t mask)
{
    return mask != 0u && (mask & (mask + 1u)) == 0u;
}

static bool period_crossed(uint32_t before, uint32_t elapsed,
                           uint32_t period)
{
    uint32_t position;
    if (elapsed == 0u) return false;
    if (elapsed >= period) return true;
    position = before % period;
    return position >= period - elapsed;
}

bool inktag_wall_clock_init(inktag_wall_clock_t *clock,
                            uint32_t current_tick,
                            uint32_t ticks_per_second,
                            uint32_t tick_mask)
{
    if (clock == NULL || ticks_per_second == 0u ||
        !mask_is_wrapping_low_bits(tick_mask)) {
        return false;
    }
    clock->last_tick = current_tick & tick_mask;
    clock->remainder_ticks = 0u;
    clock->ticks_per_second = ticks_per_second;
    clock->tick_mask = tick_mask;
    clock->initialized = true;
    return true;
}

void inktag_wall_clock_reset(inktag_wall_clock_t *clock,
                             uint32_t current_tick)
{
    if (clock == NULL || !clock->initialized) return;
    clock->last_tick = current_tick & clock->tick_mask;
    clock->remainder_ticks = 0u;
}

inktag_wall_clock_sample_t inktag_wall_clock_sample(
    inktag_wall_clock_t *clock,
    uint32_t current_tick,
    uint32_t *local_timestamp)
{
    inktag_wall_clock_sample_t sample = {0u, false, false};
    uint32_t delta;
    uint32_t remainder;
    uint32_t before;

    if (clock == NULL || local_timestamp == NULL || !clock->initialized) {
        return sample;
    }

    current_tick &= clock->tick_mask;
    delta = (current_tick - clock->last_tick) & clock->tick_mask;
    clock->last_tick = current_tick;

    sample.elapsed_seconds = delta / clock->ticks_per_second;
    remainder = delta % clock->ticks_per_second;
    remainder += clock->remainder_ticks;
    if (remainder >= clock->ticks_per_second) {
        remainder -= clock->ticks_per_second;
        sample.elapsed_seconds++;
    }
    clock->remainder_ticks = remainder;

    before = *local_timestamp;
    sample.minute_crossed = period_crossed(
        before, sample.elapsed_seconds, INKTAG_SECONDS_PER_MINUTE);
    sample.day_crossed = period_crossed(
        before, sample.elapsed_seconds, INKTAG_SECONDS_PER_DAY);
    *local_timestamp += sample.elapsed_seconds;
    return sample;
}
