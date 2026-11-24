/* Current native-page geometry classifier and holiday schedule support.
 * Kept separate from the historical V0 renderer so production firmware does
 * not preprocess or retain that renderer. Included only by inktag_gui.c. */
static bool small_layout(gui_data_t* data) {
    uint16_t min_dim = data->width < data->height ? data->width : data->height;
    uint16_t max_dim = data->width > data->height ? data->width : data->height;
    return max_dim <= 384 && min_dim <= 200;
}

#define HOLIDAY_YEAR 2026
#ifndef INKTAG_GUI_MAX_RUNTIME_HOLIDAYS
#define INKTAG_GUI_MAX_RUNTIME_HOLIDAYS 128
#endif

static const uint16_t holidays[] = {
    0x0101, 0x0102, 0x0103, 0x1104, 0x120E, 0x020F, 0x0210, 0x0211, 0x0212,
    0x0213, 0x0214, 0x0215, 0x0216, 0x0217, 0x121C, 0x0404, 0x0405, 0x0406,
    0x0501, 0x0502, 0x0503, 0x0504, 0x0505, 0x1509, 0x0613, 0x0614, 0x0615,
    0x0919, 0x091A, 0x091B, 0x1914, 0x0A01, 0x0A02, 0x0A03, 0x0A04, 0x0A05,
    0x0A06, 0x0A07, 0x1A0A,
};

static uint16_t runtime_holidays[INKTAG_GUI_MAX_RUNTIME_HOLIDAYS];
static uint16_t runtime_holiday_year;
static uint8_t runtime_holiday_count;

void GUI_SetHolidaySchedule(uint16_t year, const uint16_t* schedule,
                            uint8_t count) {
    if (schedule == NULL || count == 0u) {
        GUI_ClearHolidaySchedule();
        return;
    }
    if (count > INKTAG_GUI_MAX_RUNTIME_HOLIDAYS)
        count = INKTAG_GUI_MAX_RUNTIME_HOLIDAYS;
    runtime_holiday_year = year;
    runtime_holiday_count = count;
    memcpy(runtime_holidays, schedule, count * sizeof(uint16_t));
}

void GUI_ClearHolidaySchedule(void) {
    runtime_holiday_year = 0u;
    runtime_holiday_count = 0u;
    memset(runtime_holidays, 0, sizeof(runtime_holidays));
}

static bool FindHoliday(const uint16_t* schedule, uint8_t count, uint8_t mon,
                        uint8_t day, bool* work) {
    for (uint8_t i = 0u; i < count; ++i) {
        if (((schedule[i] >> 8) & 0x0Fu) == mon &&
            (schedule[i] & 0xFFu) == day) {
            *work = ((schedule[i] >> 12) & 0x0Fu) != 0u;
            return true;
        }
    }
    return false;
}

static bool GetHoliday(uint16_t year, uint8_t mon, uint8_t day, bool* work) {
    if (year == runtime_holiday_year &&
        FindHoliday(runtime_holidays, runtime_holiday_count, mon, day, work))
        return true;
    return year == HOLIDAY_YEAR &&
           FindHoliday(holidays, (uint8_t)(sizeof(holidays) / sizeof(holidays[0])),
                       mon, day, work);
}
