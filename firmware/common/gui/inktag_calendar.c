/* InkTag calendar implementation, 2026.
 * Lunar year records: isee15 Lunar-Solar-Calendar-Converter (MIT), pinned
 * 55d4e547947594736f7c9e4a35521ecc1c7342d8; see licenses/lunar-calendar-MIT.txt.
 * Solar-term resource: tools/calendar/solar-terms-source.json.
 */
#include "inktag_calendar.h"
#include "inktag_gui_fonts.h"

#define LUNAR_YEAR_RECORD_COUNT 55u

const char Lunar_MonthString[13][7] = {"----", "正月", "二月", "三月", "四月", "五月", "六月",
                                       "七月", "八月", "九月", "十月", "冬月", "腊月"};

const char Lunar_MonthLeapString[2][4] = {" ", "闰"};

const char Lunar_DateString[31][7] = {"----", "初一", "初二", "初三", "初四", "初五", "初六", "初七",
                                      "初八", "初九", "初十", "十一", "十二", "十三", "十四", "十五",
                                      "十六", "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三",
                                      "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};

const char Lunar_DayString[7][4] = {"日", "一", "二", "三", "四", "五", "六"};

const char Lunar_ZodiacString[12][4] = {"猴", "鸡", "狗", "猪", "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊"};

const char Lunar_StemStrig[10][4] = {"庚", "辛", "壬", "癸", "甲", "乙", "丙", "丁", "戊", "己"};

const char Lunar_BranchStrig[12][4] = {"申", "酉", "戌", "亥", "子", "丑", "寅", "卯", "辰", "巳", "午", "未"};

const char JieQiStr[24][7] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
    "小暑", "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

#if defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS)
#define solar_1_1 ((const uint32_t*)INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_LUNAR_SOLAR_OFFSET))
#define lunar_month_days ((const uint32_t*)INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_LUNAR_MONTH_OFFSET))
#define YearMonthBit ((const uint8_t*)INKTAG_GUI_EXTERNAL_FONT(INKTAG_GUI_SOLAR_TERM_OFFSET))
#else
const uint32_t lunar_month_days[] = {
    1997,
    0x0000b26d, 0x0000125c, 0x0000192c, 0x00009a95, 0x00001a94, 0x00001b4a, 0x00004b55, 0x00000ad4, 0x0000f55b,
    0x000004ba, 0x0000125a, 0x0000b92b, 0x0000152a, 0x00001694, 0x000096aa, 0x000015aa, 0x00012ab5, 0x00000974,
    0x000014b6, 0x0000ca57, 0x00000a56, 0x00001526, 0x00008e95, 0x00000d54, 0x000015aa, 0x000049b5, 0x0000096c,
    0x0000d4ae, 0x0000149c, 0x00001a4c, 0x0000bd26, 0x00001aa6, 0x00000b54, 0x00006d6a, 0x000012da, 0x0001695d,
    0x0000095a, 0x0000149a, 0x0000da4b, 0x00001a4a, 0x00001aa4, 0x0000bb54, 0x000016b4, 0x00000ada, 0x0000495b,
    0x00000936, 0x0000f497, 0x00001496, 0x0000154a, 0x0000b6a5, 0x00000da4, 0x000015b4, 0x00006ab6, 0x0000126e,
};

const uint32_t solar_1_1[] = {
    1997,
    0x000f9c3c, 0x000f9e50, 0x000fa045, 0x000fa238, 0x000fa44c, 0x000fa641, 0x000fa836, 0x000faa49, 0x000fac3d,
    0x000fae52, 0x000fb047, 0x000fb23a, 0x000fb44e, 0x000fb643, 0x000fb837, 0x000fba4a, 0x000fbc3f, 0x000fbe53,
    0x000fc048, 0x000fc23c, 0x000fc450, 0x000fc645, 0x000fc839, 0x000fca4c, 0x000fcc41, 0x000fce36, 0x000fd04a,
    0x000fd23d, 0x000fd451, 0x000fd646, 0x000fd83a, 0x000fda4d, 0x000fdc43, 0x000fde37, 0x000fe04b, 0x000fe23f,
    0x000fe453, 0x000fe648, 0x000fe83c, 0x000fea4f, 0x000fec44, 0x000fee38, 0x000ff04c, 0x000ff241, 0x000ff436,
    0x000ff64a, 0x000ff83e, 0x000ffa51, 0x000ffc46, 0x000ffe3a, 0x0010004e, 0x00100242, 0x00100437, 0x0010064b,
};

#include "inktag_solar_terms.inc"
#endif

/* Month numbers at the public boundaries intentionally retain the established
 * conventions: get_last_day uses 0..11; civil conversion uses 1..12. */
const uint8_t MonthDayMax[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const uint8_t term_base[24] = {
    6, 20, 4, 19, 6, 21, 5, 20, 6, 21, 6, 21,
    7, 23, 8, 23, 8, 23, 8, 24, 8, 22, 7, 22
};

static uint32_t record_at(const uint32_t* record) {
#if defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS)
    const uint8_t* p = (const uint8_t*)record;
    uint32_t value = 0;
    for (uint8_t i = 0; i != 4; ++i) value |= (uint32_t)u8x8_pgm_read(p + i) << (8u * i);
    return value;
#else
    return *record;
#endif
}

int is_leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

uint8_t get_last_day(uint16_t year, uint8_t month) {
    month %= 12u;
    return MonthDayMax[month] + (month == 1u && is_leap(year));
}

/* Gregorian ordinal relative to 1 January of year 1. Calendar tables only use
 * positive years. The date field may overflow a month, as in the public ABI. */
static uint32_t civil_ordinal(uint16_t year, uint8_t month, uint8_t day) {
    uint32_t prior = (uint32_t)year - 1u;
    uint32_t ordinal = prior * 365u + prior / 4u - prior / 100u + prior / 400u + day - 1u;
    for (uint8_t m = 1; m < month; ++m) ordinal += get_last_day(year, m - 1u);
    return ordinal;
}

void LUNAR_SolarToLunar(struct Lunar_Date* lunar, uint16_t year, uint8_t month, uint8_t day) {
    uint32_t base_year = record_at(solar_1_1);
    uint32_t year_index = (uint32_t)year - base_year;
    if (year_index < 3u || year_index > LUNAR_YEAR_RECORD_COUNT - 2u ||
        month < 1u || month > 12u || day < 1u || day > 31u) {
        lunar->Year = 0;
        lunar->Month = lunar->Date = lunar->IsLeap = 0;
        return;
    }
    uint16_t index = year_index;
    uint32_t packed = ((uint32_t)year << 9) | ((uint32_t)month << 5) | day;
    if (packed < record_at(solar_1_1 + index)) --index;
    uint32_t new_year = record_at(solar_1_1 + index);
    uint16_t days = (uint16_t)(civil_ordinal(year, month, day) -
        civil_ordinal((new_year >> 9) & 4095u, (new_year >> 5) & 15u, new_year & 31u));
    uint32_t months = record_at(lunar_month_days + index);
    uint8_t slot = 0;
    while (slot < 13u) {
        uint8_t length = 29u + ((months >> (12u - slot)) & 1u);
        if (days < length) break;
        days -= length;
        ++slot;
    }
    uint8_t intercalary = (months >> 13) & 15u;
    lunar->Year = index + base_year;
    lunar->Date = days + 1u;
    lunar->IsLeap = intercalary != 0u && slot == intercalary;
    lunar->Month = slot + 1u - (intercalary != 0u && slot >= intercalary);
}

uint8_t LUNAR_GetZodiac(const struct Lunar_Date* lunar) { return lunar->Year % 12u; }
uint8_t LUNAR_GetStem(const struct Lunar_Date* lunar) { return lunar->Year % 10u; }
uint8_t LUNAR_GetBranch(const struct Lunar_Date* lunar) { return lunar->Year % 12u; }

uint8_t GetJieQi(uint16_t year, uint8_t month, uint8_t day, uint8_t* date) {
    if (year < 2000u || year > 2050u || month < 1u || month > 12u) return 0;
    uint8_t term = (month - 1u) * 2u + (day >= 15u);
    uint8_t bits = u8x8_pgm_read(YearMonthBit + (year - 2000u) * 3u + term / 8u);
    uint8_t correction = (bits >> (7u - term % 8u)) & 1u;
    int direction = year < 2044u && (term == 1u || term == 11u || term == 18u || term == 21u) ? 1 : -1;
    *date = term_base[term] + direction * correction;
    return 1;
}

uint8_t GetJieQiStr(uint16_t year, uint8_t month, uint8_t day, uint8_t* distance) {
    uint8_t date;
    if (!GetJieQi(year, month, day, &date)) return 255;
    uint8_t term = (month - 1u) * 2u + (day >= 15u);
    uint8_t span = 0;
    if (day > date) {
        ++term;
        if (day < 15u) {
            GetJieQi(year, month, 15, &date);
        } else {
            span = get_last_day(year, month - 1u);
            GetJieQi(year, month == 12u ? 1u : month + 1u, 1, &date);
        }
    }
    *distance = span + date - day;
    return term;
}

unsigned char day_of_week_get(unsigned char month, unsigned char day, unsigned short year) {
    if (year != 0u) return (civil_ordinal(year, month, day) + 1u) % 7u;
    /* Preserve the uint16 year-wrap convention for year zero before March. */
    static const uint8_t offsets[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint32_t adjusted = month < 3u ? 65535u : 0u;
    return (adjusted + adjusted / 4u - adjusted / 100u + adjusted / 400u + offsets[month - 1u] + day) % 7u;
}

uint8_t get_first_day_week(uint16_t year, uint8_t month) { return day_of_week_get(month, 1, year); }

static uint8_t iso_weeks_in_year(uint16_t year) {
    uint8_t first = get_first_day_week(year, 1);
    return 52u + (first == 4u || (first == 3u && is_leap(year)));
}

uint8_t iso_week_of_year(uint16_t year, uint8_t month, uint8_t day) {
    uint16_t ordinal = day;
    for (uint8_t m = 1; m < month; ++m) ordinal += get_last_day(year, m - 1u);
    uint8_t weekday = day_of_week_get(month, day, year);
    int16_t week = (int16_t)(ordinal + 10u - (weekday ? weekday : 7u)) / 7;
    if (week < 1) return iso_weeks_in_year(year - 1u);
    return week > iso_weeks_in_year(year) ? 1u : (uint8_t)week;
}

void transformTime(uint32_t timestamp, struct devtm* result) {
    memset(result, 0, sizeof(*result));
    uint32_t days = timestamp / SEC_PER_DY;
    uint32_t within_day = timestamp % SEC_PER_DY;
    result->tm_wday = (days + 4u) % 7u;
    uint16_t year = EPOCH_YR;
    while (days >= (uint32_t)(365 + is_leap(year))) days -= 365 + is_leap(year++);
    while (days >= get_last_day(year, result->tm_mon)) days -= get_last_day(year, result->tm_mon++);
    result->tm_year = year - YEAR0;
    result->tm_mday = days + 1u;
    result->tm_hour = within_day / SEC_PER_HR;
    result->tm_min = within_day / 60u % 60u;
    result->tm_sec = within_day % 60u;
}

uint32_t transformTimeStruct(struct devtm* value) {
    uint32_t leaps = 0;
    if (value->tm_year > EPOCH_YR) {
        uint32_t y = value->tm_year - 1u;
        leaps = y / 4u - y / 100u + y / 400u - 477u;
    }
    uint32_t days = ((uint32_t)value->tm_year - EPOCH_YR) * 365u + leaps + value->tm_mday - 1u;
    for (uint8_t m = 1; m < value->tm_mon; ++m) days += get_last_day(value->tm_year, m - 1u);
    return days * SEC_PER_DY + (uint32_t)value->tm_hour * SEC_PER_HR + (uint32_t)value->tm_min * 60u + value->tm_sec;
}

uint8_t thisMonthMaxDays(uint8_t year, uint8_t month) {
    return MonthDayMax[month - 1u] + (month == 2u && year % 4u == 0u);
}
