#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "inktag_calendar.h"

static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t result = days[month - 1];
    if (month == 2 && is_leap(year)) result++;
    return result;
}

int main(void) {
    setenv("TZ", "UTC0", 1);
    tzset();

    for (uint16_t year = 1900; year <= 2399; year++) {
        for (uint8_t month = 1; month <= 12; month++) {
            for (uint8_t day = 1; day <= days_in_month(year, month); day++) {
                struct tm value = {0};
                char expected_text[3] = {0};

                value.tm_year = year - 1900;
                value.tm_mon = month - 1;
                value.tm_mday = day;
                value.tm_hour = 12;
                value.tm_isdst = -1;

                if (mktime(&value) == (time_t)-1 ||
                    strftime(expected_text, sizeof(expected_text), "%V", &value) != 2) {
                    fprintf(stderr, "libc failed for %u-%02u-%02u\n", year, month, day);
                    return 2;
                }

                uint8_t expected = (uint8_t)((expected_text[0] - '0') * 10 + expected_text[1] - '0');
                uint8_t actual = iso_week_of_year(year, month, day);
                if (actual != expected) {
                    fprintf(stderr, "%u-%02u-%02u: expected week %u, got %u\n", year, month, day,
                            expected, actual);
                    return 1;
                }
            }
        }
    }

    puts("ISO week validation passed for every date from 1900 through 2399.");
    return 0;
}
