#include <assert.h>
#include <stdint.h>

#include "inktag_elapsed_clock.h"
#include "inktag_timebase.h"

int main(void)
{
    inktag_elapsed_clock_t clock;
    inktag_elapsed_time_t elapsed;
    const inktag_timebase_profile_t rc_profile = {
        INKTAG_LFCLK_SOURCE_INTERNAL_RC,
        INKTAG_TIMEBASE_QUALITY_NON_PRECISION,
        false,
        32768u,
        32768u,
        UINT32_MAX,
        0u,
    };
    inktag_timebase_profile_t invalid_profile = rc_profile;

    assert(inktag_timebase_profile_valid(&rc_profile));
    invalid_profile.counter_ticks_per_second = 0u;
    assert(!inktag_timebase_profile_valid(&invalid_profile));

    inktag_elapsed_clock_reset(&clock, 100u, UINT32_MAX, 1000u);
    assert(inktag_elapsed_clock_sample(&clock, 1099u) == 0u);
    assert(inktag_elapsed_clock_sample(&clock, 1100u) == 1u);

    /* A delayed callback must account for every elapsed second, not one. */
    assert(inktag_elapsed_clock_sample(&clock, 43100u) == 42u);

    /* Fractional ticks survive adjacent samples. */
    assert(inktag_elapsed_clock_sample(&clock, 43600u) == 0u);
    assert(inktag_elapsed_clock_sample(&clock, 44100u) == 1u);

    /* Platform counters may wrap below 32 bits (Goodix BLE time is 28-bit). */
    inktag_elapsed_clock_reset(&clock, 0x0FFFFFF0u, 0x0FFFFFFFu, 16u);
    assert(inktag_elapsed_clock_sample(&clock, 0x00000010u) == 2u);

    /* One shared sample can drive wall seconds and UI/input millisecond
     * deadlines without pretending that a callback itself is one tick. */
    inktag_elapsed_clock_reset(&clock, 0u, UINT32_MAX, 32768u);
    elapsed = inktag_elapsed_clock_sample_time(&clock, 16384u);
    assert(elapsed.seconds == 0u);
    assert(elapsed.milliseconds == 500u);
    elapsed = inktag_elapsed_clock_sample_time(&clock, 32768u);
    assert(elapsed.seconds == 1u);
    assert(elapsed.milliseconds == 500u);
    elapsed = inktag_elapsed_clock_sample_time(
        &clock, 32768u + 5u * 32768u + 8192u);
    assert(elapsed.seconds == 5u);
    assert(elapsed.milliseconds == 5250u);

    /* A vendor may expose an LFCLK-recovered 16 MHz logical counter. */
    inktag_elapsed_clock_reset(&clock, 0u, UINT32_MAX, 16000000u);
    elapsed = inktag_elapsed_clock_sample_time(&clock, 8000000u);
    assert(elapsed.seconds == 0u);
    assert(elapsed.milliseconds == 500u);

    /* Fractional accounting remains exact across the hardware counter wrap. */
    inktag_elapsed_clock_reset(&clock, 0xFFFFFF00u, UINT32_MAX, 32768u);
    elapsed = inktag_elapsed_clock_sample_time(&clock, 0x00003F00u);
    assert(elapsed.seconds == 0u);
    assert(elapsed.milliseconds == 500u);

    assert(!inktag_elapsed_clock_crossed_period(119u, 119u, 60u));
    assert(inktag_elapsed_clock_crossed_period(119u, 161u, 60u));
    assert(inktag_elapsed_clock_crossed_period(86399u, 86400u, 86400u));
    return 0;
}
