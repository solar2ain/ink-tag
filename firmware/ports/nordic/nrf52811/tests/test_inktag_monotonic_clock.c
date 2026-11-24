#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_monotonic_clock.h"

static void test_fractional_ticks_are_preserved(void) {
    inktag_monotonic_clock_t clock;
    inktag_elapsed_time_t elapsed;
    assert(inktag_monotonic_clock_init(&clock, 0u, 32768u, 24u));

    elapsed = inktag_monotonic_clock_sample(&clock, 16384u);
    assert(elapsed.seconds == 0u);
    assert(elapsed.milliseconds == 500u);
    elapsed = inktag_monotonic_clock_sample(&clock, 32768u);
    assert(elapsed.seconds == 1u);
    assert(elapsed.milliseconds == 500u);
}

static void test_delayed_callback_uses_counter_delta(void) {
    inktag_monotonic_clock_t clock;
    inktag_elapsed_time_t elapsed;
    assert(inktag_monotonic_clock_init(&clock, 100u, 32768u, 24u));

    elapsed = inktag_monotonic_clock_sample(&clock, 100u + 5u * 32768u + 8192u);
    assert(elapsed.seconds == 5u);
    assert(elapsed.milliseconds == 5250u);
}

static void test_24_bit_counter_wrap(void) {
    inktag_monotonic_clock_t clock;
    inktag_elapsed_time_t elapsed;
    assert(inktag_monotonic_clock_init(&clock, 0x00FFFF00u, 32768u, 24u));

    elapsed = inktag_monotonic_clock_sample(&clock, 0x00007F00u);
    assert(elapsed.ticks == 32768u);
    assert(elapsed.seconds == 1u);
    assert(elapsed.milliseconds == 1000u);
}

static void test_wall_rebase_does_not_replay_fraction(void) {
    inktag_monotonic_clock_t clock;
    inktag_elapsed_time_t elapsed;
    assert(inktag_monotonic_clock_init(&clock, 0u, 32768u, 24u));
    elapsed = inktag_monotonic_clock_sample(&clock, 24576u);
    assert(elapsed.seconds == 0u);
    inktag_monotonic_clock_rebase_wall(&clock);
    elapsed = inktag_monotonic_clock_sample(&clock, 40960u);
    assert(elapsed.seconds == 0u);
    assert(elapsed.milliseconds == 500u);
}

static void test_prescaled_nordic_rtc_frequency(void) {
    inktag_monotonic_clock_t clock;
    inktag_elapsed_time_t elapsed;

    assert(inktag_monotonic_clock_init(&clock, 0u, 16384u, 24u));
    elapsed = inktag_monotonic_clock_sample(&clock, 16384u);
    assert(elapsed.seconds == 1u);
    assert(elapsed.milliseconds == 1000u);
}

static void test_crossed_boundaries_fire_once_after_delay(void) {
    assert(inktag_time_boundary_crossed(59u, 64u, 60u));
    assert(inktag_time_boundary_crossed(86399u, 86403u, 86400u));
    assert(inktag_time_boundary_crossed(58u, 181u, 60u));
    assert(!inktag_time_boundary_crossed(64u, 65u, 60u));
    assert(!inktag_time_boundary_crossed(65u, 65u, 60u));
    assert(!inktag_time_boundary_crossed(65u, 64u, 60u));
}

int main(void) {
    test_fractional_ticks_are_preserved();
    test_delayed_callback_uses_counter_delta();
    test_24_bit_counter_wrap();
    test_wall_rebase_does_not_replay_fraction();
    test_prescaled_nordic_rtc_frequency();
    test_crossed_boundaries_fire_once_after_delay();
    puts("monotonic clock tests passed");
    return 0;
}
