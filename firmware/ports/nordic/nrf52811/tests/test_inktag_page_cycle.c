#include <assert.h>
#include <stdio.h>

#include "inktag_page_cycle.h"

static epd_display_state_t state(uint8_t mode, uint8_t argument) {
    epd_display_state_t value = {mode, argument, 0u, 0u};
    return value;
}

int main(void) {
    epd_display_state_t current;
    inktag_boot_display_plan_t plan;

    inktag_display_state_default(&current);
    assert(current.mode == EPD_DISPLAY_STATE_CALENDAR);
    assert(current.argument == EPD_DISPLAY_STATE_NO_SOURCE);
    assert(current.reserved == 0u);
    assert(current.interval_seconds == 0u);

    plan = inktag_boot_display_plan(false, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);
    assert(!plan.consume_first_display_request);
    assert(!plan.defer_until_startup_complete);

    plan = inktag_boot_display_plan(true, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO);
    assert(plan.consume_first_display_request);
    assert(plan.defer_until_startup_complete);
    /* The caller consumed the one-shot request before scheduling. A later
     * ordinary boot therefore preserves the panel. */
    plan = inktag_boot_display_plan(false, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);

    plan = inktag_boot_display_plan(true, true, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);
    assert(plan.consume_first_display_request);

    current.reserved = EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH;
    plan = inktag_boot_display_plan(false, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_REPLAY);
    assert(plan.defer_until_startup_complete);
    plan = inktag_boot_display_plan(false, true, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);

    /* A fresh one-shot request overrides opt-in replay; OTA still suppresses
     * both and consumes the request. No persisted display record is needed. */
    current.mode = EPD_DISPLAY_STATE_CLOCK;
    plan = inktag_boot_display_plan(true, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO);
    assert(plan.consume_first_display_request);
    plan = inktag_boot_display_plan(true, true, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);
    assert(plan.consume_first_display_request);
    plan = inktag_boot_display_plan(true, false, NULL);
    assert(plan.target == INKTAG_BOOT_DISPLAY_DEVICE_INFO);
    plan = inktag_boot_display_plan(false, false, NULL);
    assert(plan.target == INKTAG_BOOT_DISPLAY_NONE);

    current.reserved |= EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED;
    plan = inktag_boot_display_plan(false, false, &current);
    assert(plan.target == INKTAG_BOOT_DISPLAY_REPLAY);
    assert(current.reserved == (EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH |
                                EPD_DISPLAY_STATE_FLAG_AUTO_REFRESH_SUSPENDED));

    assert(inktag_page_cycle_next(NULL, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_CALENDAR, 0u);
    assert(inktag_page_cycle_next(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CLOCK);
    current = state(EPD_DISPLAY_STATE_CLOCK, 0u);
    assert(inktag_page_cycle_next(&current, true, true) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    current = state(EPD_DISPLAY_STATE_DEVICE_INFO, 0u);
    assert(inktag_page_cycle_next(&current, true, false) ==
           INKTAG_PAGE_CYCLE_SLIDESHOW);
    assert(inktag_page_cycle_next(&current, false, false) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_PICTURE, 7u);
    assert(inktag_page_cycle_next(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    assert(inktag_page_cycle_next(&current, true, false) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_SLIDESHOW, 7u);
    assert(inktag_page_cycle_next(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    assert(inktag_page_cycle_next(&current, true, false) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_CLEAR, 0u);
    assert(inktag_page_cycle_next(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);

    assert(inktag_page_cycle_previous(NULL, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_CALENDAR, 0u);
    assert(inktag_page_cycle_previous(&current, true, false) ==
           INKTAG_PAGE_CYCLE_SLIDESHOW);
    assert(inktag_page_cycle_previous(&current, false, false) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    current = state(EPD_DISPLAY_STATE_CLOCK, 0u);
    assert(inktag_page_cycle_previous(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CALENDAR);
    current = state(EPD_DISPLAY_STATE_DEVICE_INFO, 0u);
    assert(inktag_page_cycle_previous(&current, true, true) ==
           INKTAG_PAGE_CYCLE_CLOCK);
    current = state(EPD_DISPLAY_STATE_PICTURE, 7u);
    assert(inktag_page_cycle_previous(&current, true, true) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    assert(inktag_page_cycle_previous(&current, true, false) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    current = state(EPD_DISPLAY_STATE_SLIDESHOW, 7u);
    assert(inktag_page_cycle_previous(&current, true, true) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    assert(inktag_page_cycle_previous(&current, true, false) ==
           INKTAG_PAGE_CYCLE_DEVICE_INFO);
    /* Neither direction may step an image manually, including a single-image
     * slideshow or a stale saved image argument. Rotation belongs to timers. */
    for (unsigned image = 0u; image <= 255u; ++image) {
        current = state(EPD_DISPLAY_STATE_SLIDESHOW, (uint8_t)image);
        for (unsigned available = 0u; available < 4u; ++available) {
            assert(inktag_page_cycle_next(&current, available & 1u, available & 2u)
                   == INKTAG_PAGE_CYCLE_CALENDAR);
            assert(inktag_page_cycle_previous(&current, available & 1u, available & 2u)
                   == INKTAG_PAGE_CYCLE_DEVICE_INFO);
        }
    }
    puts("InkTag page-cycle tests passed");
    return 0;
}
