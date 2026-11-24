#ifndef INKTAG_PAGE_CYCLE_H
#define INKTAG_PAGE_CYCLE_H

#include <stdbool.h>

#include "inktag_epd_config.h"

typedef enum {
    INKTAG_PAGE_CYCLE_NONE = 0,
    INKTAG_PAGE_CYCLE_CALENDAR,
    INKTAG_PAGE_CYCLE_CLOCK,
    INKTAG_PAGE_CYCLE_DEVICE_INFO,
    INKTAG_PAGE_CYCLE_FIRST_IMAGE,
    INKTAG_PAGE_CYCLE_NEXT_IMAGE,
    INKTAG_PAGE_CYCLE_LAST_IMAGE,
    INKTAG_PAGE_CYCLE_PREVIOUS_IMAGE,
    INKTAG_PAGE_CYCLE_SLIDESHOW,
} inktag_page_cycle_target_t;

typedef enum {
    INKTAG_BOOT_DISPLAY_NONE = 0,
    INKTAG_BOOT_DISPLAY_DEVICE_INFO,
    INKTAG_BOOT_DISPLAY_REPLAY,
} inktag_boot_display_target_t;

typedef struct {
    inktag_boot_display_target_t target;
    bool consume_first_display_request;
    bool defer_until_startup_complete;
} inktag_boot_display_plan_t;

/* Product ordering is fixed across ports:
 *   calendar -> clock -> device info -> slideshow -> calendar.
 * A slideshow is ONE page, not one page per image. NEXT exits it to calendar.
 * SLIDESHOW means start automatic image rotation using the saved interval;
 * with no compatible visible images that page is skipped. The third parameter
 * is retained for source compatibility, not used for manual image stepping.
 * Button and NFC next use this same policy; timer-driven rotation is separate.
 * Adapters stop rotation only after successful native-page admission, before
 * its draw begins. A refused request preserves the current rotation. */
inktag_page_cycle_target_t inktag_page_cycle_next(
    const epd_display_state_t *current,
    bool first_image_available,
    bool next_image_available);

/* Reverse the same four logical pages, skipping slideshow if empty. Previous
 * from slideshow (or an explicitly shown picture) returns to device info,
 * never to another image. No image payload or settings are deleted. */
inktag_page_cycle_target_t inktag_page_cycle_previous(
    const epd_display_state_t *current,
    bool last_image_available,
    bool previous_image_available);

/* Native display defaults and automatic boot work are product policy, not
 * board data. A complete-flashing first-display marker is consumed once and
 * draws device information. Ordinary cold boot preserves the panel unless
 * BOOT_REFRESH is enabled. The first application boot after OTA never refreshes.
 * Callers must consume the marker before scheduling, and must not start the
 * returned background display until the startup status transient completes. */
void inktag_display_state_default(epd_display_state_t *state);
inktag_boot_display_plan_t inktag_boot_display_plan(
    bool first_display_requested,
    bool ota_install_boot,
    const epd_display_state_t *persisted_state);

#endif
