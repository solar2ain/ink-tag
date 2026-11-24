#ifndef INKTAG_DA_CLOCK_H
#define INKTAG_DA_CLOCK_H

#include <stddef.h>
#include "inktag_wall_clock.h"

/* At most one successful idle Settings append per hour of powered
 * elapsed time; failed attempts back off 60 seconds. This is a checkpoint,
 * not an RTC: it never makes a cold-boot timestamp trustworthy. */
#define INKTAG_DA_TIME_CHECKPOINT_SECONDS 3600u

/* DA samples the 27-bit BLE clock at least once per radio/main-loop event.
 * A gap >= half its period (about 11.65 h) is not a valid elapsed observation
 * for this adapter: it is ambiguous with a backward/reset/correction sample.
 * Re-anchor without advancing wall time or firing calendar work. Keep the
 * shared accumulator unchanged for platforms with different sleep contracts.
 * The next ordinary sample resumes fractional/forward-wrap accumulation. */
static inline inktag_wall_clock_sample_t inktag_da_clock_sample(
    inktag_wall_clock_t *clock, uint32_t now_tick, uint32_t *timestamp)
{
    if (clock != NULL && timestamp != NULL && clock->initialized &&
        ((now_tick - clock->last_tick) & clock->tick_mask) >
            (clock->tick_mask >> 1)) {
        inktag_wall_clock_sample_t rejected = {0u, false, false};
        inktag_wall_clock_reset(clock, now_tick);
        return rejected;
    }
    return inktag_wall_clock_sample(clock, now_tick, timestamp);
}

#endif
