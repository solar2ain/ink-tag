#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "inktag_wall_clock.h"

#define TEST_TICKS_PER_SECOND 1600u
#define TEST_TICK_MASK 0x07FFFFFFu

static void test_fractional_and_delayed_samples(void)
{
    inktag_wall_clock_t clock;
    inktag_wall_clock_sample_t sample;
    uint32_t timestamp = 10u;

    assert(inktag_wall_clock_init(&clock, 100u, TEST_TICKS_PER_SECOND,
                                  TEST_TICK_MASK));
    sample = inktag_wall_clock_sample(&clock, 1000u, &timestamp);
    assert(sample.elapsed_seconds == 0u);
    assert(timestamp == 10u);

    /* A delayed callback keeps the earlier 900 fractional ticks. */
    sample = inktag_wall_clock_sample(
        &clock, 1000u + 5u * TEST_TICKS_PER_SECOND + 700u, &timestamp);
    assert(sample.elapsed_seconds == 6u);
    assert(timestamp == 16u);
    assert(clock.remainder_ticks == 0u);
}

static void test_counter_wrap(void)
{
    inktag_wall_clock_t clock;
    inktag_wall_clock_sample_t sample;
    uint32_t timestamp = 100u;
    uint32_t start = TEST_TICK_MASK - 799u;

    assert(inktag_wall_clock_init(&clock, start, TEST_TICKS_PER_SECOND,
                                  TEST_TICK_MASK));
    sample = inktag_wall_clock_sample(&clock, 800u, &timestamp);
    assert(sample.elapsed_seconds == 1u);
    assert(timestamp == 101u);
    assert(clock.remainder_ticks == 0u);
}

static void test_boundary_events_are_reported_once(void)
{
    inktag_wall_clock_t clock;
    inktag_wall_clock_sample_t sample;
    uint32_t timestamp = 58u;

    assert(inktag_wall_clock_init(&clock, 0u, TEST_TICKS_PER_SECOND,
                                  TEST_TICK_MASK));
    sample = inktag_wall_clock_sample(
        &clock, 3u * TEST_TICKS_PER_SECOND, &timestamp);
    assert(timestamp == 61u);
    assert(sample.minute_crossed);
    assert(!sample.day_crossed);

    sample = inktag_wall_clock_sample(
        &clock, 3u * TEST_TICKS_PER_SECOND, &timestamp);
    assert(sample.elapsed_seconds == 0u);
    assert(!sample.minute_crossed);
    assert(!sample.day_crossed);

    timestamp = 86399u;
    inktag_wall_clock_reset(&clock, 50u);
    sample = inktag_wall_clock_sample(
        &clock, 50u + 2u * TEST_TICKS_PER_SECOND, &timestamp);
    assert(timestamp == 86401u);
    assert(sample.minute_crossed);
    assert(sample.day_crossed);
}

static void test_invalid_configuration_fails_closed(void)
{
    inktag_wall_clock_t clock;
    assert(!inktag_wall_clock_init(NULL, 0u, TEST_TICKS_PER_SECOND,
                                   TEST_TICK_MASK));
    assert(!inktag_wall_clock_init(&clock, 0u, 0u, TEST_TICK_MASK));
    assert(!inktag_wall_clock_init(&clock, 0u, TEST_TICKS_PER_SECOND,
                                   0x07FFFFFEu));
}

int main(void)
{
    test_fractional_and_delayed_samples();
    test_counter_wrap();
    test_boundary_events_are_reported_once();
    test_invalid_configuration_fails_closed();
    return 0;
}
