#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "inktag_epd_temperature.h"

static void test_uc_internal_r40(void) {
    int16_t value = 1234;
    const uint8_t plus[] = {24u, 0x80u};
    const uint8_t minus[] = {0xFFu, 0u};
    const uint8_t invalid[] = {0xFFu, 0xFFu};
    const uint8_t out_of_range[] = {86u, 0u};
    assert(inktag_uc_internal_r40_temperature_sixteenths(true, plus, &value));
    assert(value == 24 * 16); /* internal sensor has no second-byte fraction */
    assert(inktag_uc_internal_r40_temperature_sixteenths(true, minus, &value));
    assert(value == -16);
    assert(!inktag_uc_internal_r40_temperature_sixteenths(false, plus, &value));
    assert(!inktag_uc_internal_r40_temperature_sixteenths(true, invalid, &value));
    assert(!inktag_uc_internal_r40_temperature_sixteenths(true, out_of_range, &value));
    assert(!inktag_uc_internal_r40_temperature_sixteenths(true, NULL, &value));
    assert(!inktag_uc_internal_r40_temperature_sixteenths(true, plus, NULL));
    assert(value == -16);
}

static void test_jd_r40(void) {
    int16_t value = 1234;
    const uint8_t plus[] = {25u, 0x80u};
    const uint8_t minus[] = {0xFFu, 0xC0u};
    const uint8_t invalid[] = {0xFFu, 0xFFu};
    const uint8_t out_of_range[] = {86u, 0u};
    assert(inktag_jd_r40_temperature_sixteenths(true, plus, &value));
    assert(value == 25 * 16 + 8);
    assert(inktag_jd_r40_temperature_sixteenths(true, minus, &value));
    assert(value == -4); /* -1 + 0.75 = -0.25 C */
    assert(inktag_epd_temperature_floor_celsius(value) == -1);
    assert(!inktag_jd_r40_temperature_sixteenths(false, plus, &value));
    assert(!inktag_jd_r40_temperature_sixteenths(true, invalid, &value));
    assert(!inktag_jd_r40_temperature_sixteenths(true, out_of_range, &value));
    assert(value == -4); /* failures never overwrite the prior valid sample */
}

static void test_ssd1680_r1b(void) {
    int16_t value = 1234;
    const uint8_t plus[] = {0x19u, 0x00u};
    const uint8_t minus[] = {0xFFu, 0xC0u};
    const uint8_t por[] = {0x7Fu, 0xF0u};
    const uint8_t malformed[] = {0x19u, 0x01u};
    assert(inktag_ssd1680_r1b_temperature_sixteenths(true, plus, &value));
    assert(value == 25 * 16);
    assert(inktag_ssd1680_r1b_temperature_sixteenths(true, minus, &value));
    assert(value == -4); /* 0xFFC = -0.25 C */
    assert(!inktag_ssd1680_r1b_temperature_sixteenths(false, plus, &value));
    assert(!inktag_ssd1680_r1b_temperature_sixteenths(true, por, &value));
    assert(!inktag_ssd1680_r1b_temperature_sixteenths(true, malformed, &value));
    assert(value == -4);
}

static void test_sample_freshness(void) {
    const uint32_t max_age_ms = 15u * 60u * 1000u;
    assert(!inktag_epd_temperature_sample_fresh(false, 100u, 100u,
                                                max_age_ms));
    assert(inktag_epd_temperature_sample_fresh(true, 100u, 100u + max_age_ms,
                                               max_age_ms));
    assert(!inktag_epd_temperature_sample_fresh(true, 100u,
                                                101u + max_age_ms,
                                                max_age_ms));
    assert(inktag_epd_temperature_sample_fresh(true, UINT32_MAX - 100u,
                                               200u, max_age_ms));
    assert(!inktag_epd_temperature_sample_fresh(true, UINT32_MAX - 100u,
                                                max_age_ms + 200u,
                                                max_age_ms));
    assert(!inktag_epd_temperature_sample_fresh(true, 0u, 1u,
                                                0x80000000u));
}

int main(void) {
    test_uc_internal_r40();
    test_jd_r40();
    test_ssd1680_r1b();
    test_sample_freshness();
    puts("EPD temperature wire decoders passed");
    return 0;
}
