#ifndef INKTAG_WALL_CLOCK_H
#define INKTAG_WALL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t last_tick;
    uint32_t remainder_ticks;
    uint32_t ticks_per_second;
    uint32_t tick_mask;
    bool initialized;
} inktag_wall_clock_t;

typedef struct {
    uint32_t elapsed_seconds;
    bool minute_crossed;
    bool day_crossed;
} inktag_wall_clock_sample_t;

/* tick_mask must describe a wrapping low-bit counter (for example 0x07FFFFFF).
 * The caller must sample at least once per complete hardware-counter period. */
bool inktag_wall_clock_init(inktag_wall_clock_t *clock,
                            uint32_t current_tick,
                            uint32_t ticks_per_second,
                            uint32_t tick_mask);

/* Re-anchor after a host time synchronization without losing the clock source
 * configuration. Fractional ticks from the former time value are discarded. */
void inktag_wall_clock_reset(inktag_wall_clock_t *clock,
                             uint32_t current_tick);

/* Advance a caller-owned local timestamp from the monotonic counter. Boundary
 * flags are raised once for this sample even when delayed work crosses more
 * than one minute or day. */
inktag_wall_clock_sample_t inktag_wall_clock_sample(
    inktag_wall_clock_t *clock,
    uint32_t current_tick,
    uint32_t *local_timestamp);

/* A persisted wall-clock value alone does not establish current time after a
 * reboot. Only a reset path that deliberately armed a retained anchor may
 * restore it, and only while the counter delta remains within the caller's
 * bounded, verified continuity window. */
static inline bool inktag_wall_clock_restore_bounded(
    bool anchor_armed, uint32_t saved_timestamp, uint32_t anchor_tick,
    uint32_t current_tick, uint32_t ticks_per_second,
    uint32_t max_elapsed_seconds, uint32_t max_timestamp,
    uint32_t *restored_timestamp)
{
    uint32_t elapsed_seconds;
    if (!anchor_armed || !restored_timestamp || ticks_per_second == 0u) return false;
    elapsed_seconds = (current_tick - anchor_tick) / ticks_per_second;
    if (elapsed_seconds > max_elapsed_seconds ||
        elapsed_seconds > max_timestamp ||
        saved_timestamp > max_timestamp - elapsed_seconds) return false;
    *restored_timestamp = saved_timestamp + elapsed_seconds;
    return true;
}

#endif
