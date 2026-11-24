/* Behavioral fingerprint; uses public ABI fields, never structure padding. */
#include "inktag_calendar.h"
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static uint64_t digest = UINT64_C(14695981039346656037);
static unsigned long count;
static void feed(uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        digest ^= (uint8_t)(value >> (8u * i));
        digest *= UINT64_C(1099511628211);
    }
    ++count;
}
static void civil(uint16_t y, uint8_t m, uint8_t d) {
    struct Lunar_Date lunar;
    uint8_t date = 173, distance = 199;
    LUNAR_SolarToLunar(&lunar, y, m, d);
    feed(lunar.Year); feed(lunar.Month); feed(lunar.Date); feed(lunar.IsLeap);
    feed(LUNAR_GetZodiac(&lunar)); feed(LUNAR_GetStem(&lunar)); feed(LUNAR_GetBranch(&lunar));
    feed(GetJieQi(y, m, d, &date)); feed(date);
    feed(GetJieQiStr(y, m, d, &distance)); feed(distance);
    if (m >= 1 && m <= 12 && d >= 1 && d <= get_last_day(y, m - 1)) {
        feed(day_of_week_get(m, d, y)); feed(iso_week_of_year(y, m, d));
    }
}
static void timestamp(uint32_t value) {
    tm_t time;
    transformTime(value, &time);
    feed(time.tm_year); feed(time.tm_mon); feed(time.tm_mday); feed(time.tm_wday);
    feed(time.tm_hour); feed(time.tm_min); feed(time.tm_sec);
    time.tm_year += YEAR0; ++time.tm_mon;
    feed(transformTimeStruct(&time));
}
int main(void) {
    for (unsigned y = 1998; y <= 2052; ++y)
        for (unsigned m = 0; m <= 13; ++m)
            for (unsigned d = 0; d <= 32; ++d) civil(y, m, d);
    for (unsigned y = 1600; y <= 2400; ++y) {
        feed(is_leap(y));
        for (unsigned m = 1; m <= 12; ++m) {
            feed(get_first_day_week(y, m)); feed(get_last_day(y, m - 1));
            for (unsigned d = 1; d <= get_last_day(y, m - 1); ++d) {
                feed(day_of_week_get(m, d, y)); feed(iso_week_of_year(y, m, d));
            }
        }
    }
    for (unsigned y = 0; y <= 255; ++y)
        for (unsigned m = 1; m <= 12; ++m) feed(thisMonthMaxDays(y, m));
    for (unsigned m = 1; m <= 12; ++m) feed(day_of_week_get(m, 1, 0));
    for (uint32_t i = 0; i < 50000; ++i) timestamp(i * 86400u);
    uint32_t random = 1;
    for (unsigned i = 0; i < 20000; ++i) {
        random = random * 1664525u + 1013904223u;
        timestamp(random);
    }
    timestamp(0); timestamp(UINT32_MAX); timestamp(2147483647u); timestamp(2147483648u);
#ifdef INKTAG_LUNAR_EXPECT_COMPATIBILITY
    assert(digest == UINT64_C(0x28b6c8cda762f6fa));
    assert(count == 1487949ul);
#endif
    printf("%016llx %lu\n", (unsigned long long)digest, count);
    return 0;
}
