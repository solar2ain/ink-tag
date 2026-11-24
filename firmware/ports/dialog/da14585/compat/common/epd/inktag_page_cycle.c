#include "inktag_page_cycle.h"

void inktag_display_state_default(epd_display_state_t *state) {
    if (!state) return;
    state->mode = EPD_DISPLAY_STATE_CALENDAR;
    state->argument = EPD_DISPLAY_STATE_NO_SOURCE;
    state->reserved = 0u;
    state->interval_seconds = 0u;
}

inktag_boot_display_plan_t inktag_boot_display_plan(
    bool first_display_requested,
    bool ota_install_boot,
    const epd_display_state_t *persisted_state) {
    inktag_boot_display_plan_t plan = {
        .target = INKTAG_BOOT_DISPLAY_NONE,
        .consume_first_display_request = first_display_requested,
        .defer_until_startup_complete = false,
    };

    /* OTA wins over the one-shot installation request and opt-in replay.
     * The request is consumed so it cannot cause a redundant refresh on the
     * next ordinary boot. */
    if (ota_install_boot) return plan;
    if (first_display_requested) {
        plan.target = INKTAG_BOOT_DISPLAY_DEVICE_INFO;
        plan.defer_until_startup_complete = true;
        return plan;
    }
    if (persisted_state != NULL &&
        (persisted_state->reserved & EPD_DISPLAY_STATE_FLAG_BOOT_REFRESH) != 0u) {
        plan.target = INKTAG_BOOT_DISPLAY_REPLAY;
        plan.defer_until_startup_complete = true;
    }
    return plan;
}

inktag_page_cycle_target_t inktag_page_cycle_next(
    const epd_display_state_t *current,
    bool first_image_available,
    bool next_image_available) {
    (void)next_image_available;
    if (!current) return INKTAG_PAGE_CYCLE_CALENDAR;
    switch (current->mode) {
        case EPD_DISPLAY_STATE_CALENDAR:
            return INKTAG_PAGE_CYCLE_CLOCK;
        case EPD_DISPLAY_STATE_CLOCK:
            return INKTAG_PAGE_CYCLE_DEVICE_INFO;
        case EPD_DISPLAY_STATE_DEVICE_INFO:
            return first_image_available ? INKTAG_PAGE_CYCLE_SLIDESHOW
                                         : INKTAG_PAGE_CYCLE_CALENDAR;
        case EPD_DISPLAY_STATE_PICTURE:
        case EPD_DISPLAY_STATE_SLIDESHOW:
            return INKTAG_PAGE_CYCLE_CALENDAR;
        default:
            return INKTAG_PAGE_CYCLE_CALENDAR;
    }
}

inktag_page_cycle_target_t inktag_page_cycle_previous(
    const epd_display_state_t *current,
    bool last_image_available,
    bool previous_image_available) {
    (void)previous_image_available;
    if (!current) return INKTAG_PAGE_CYCLE_CALENDAR;
    switch (current->mode) {
        case EPD_DISPLAY_STATE_CALENDAR:
            return last_image_available ? INKTAG_PAGE_CYCLE_SLIDESHOW
                                        : INKTAG_PAGE_CYCLE_DEVICE_INFO;
        case EPD_DISPLAY_STATE_CLOCK:
            return INKTAG_PAGE_CYCLE_CALENDAR;
        case EPD_DISPLAY_STATE_DEVICE_INFO:
            return INKTAG_PAGE_CYCLE_CLOCK;
        case EPD_DISPLAY_STATE_PICTURE:
        case EPD_DISPLAY_STATE_SLIDESHOW:
            return INKTAG_PAGE_CYCLE_DEVICE_INFO;
        default:
            return INKTAG_PAGE_CYCLE_DEVICE_INFO;
    }
}
