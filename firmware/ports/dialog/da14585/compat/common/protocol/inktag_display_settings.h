#ifndef INKTAG_DISPLAY_SETTINGS_H
#define INKTAG_DISPLAY_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INKTAG_DISPLAY_SETTINGS_WIRE_VERSION 1u
#define INKTAG_DISPLAY_SETTINGS_BASE_SIZE 9u
#define INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES 12u
#define INKTAG_DISPLAY_SETTINGS_MAX_NAME_CODEPOINTS 4u
#define INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START 1u

#define INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH 0x01u
#define INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN 0x10u
#define INKTAG_DISPLAY_SETTINGS_KNOWN_FLAGS \
    (INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH | \
     INKTAG_DISPLAY_SETTINGS_FLAG_BRAND_HIDDEN)

/* target_day is the local civil-day number since 1970-01-01.  Zero together
 * with an empty name disables the shared calendar/clock countdown. */
typedef struct {
    uint8_t week_start;
    uint8_t flags;
    uint32_t target_day;
    uint8_t name_length;
    char name[INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES + 1u];
} inktag_display_settings_t;

bool inktag_display_settings_decode(const uint8_t* payload, size_t length,
                                    inktag_display_settings_t* settings);
size_t inktag_display_settings_encode(const inktag_display_settings_t* settings,
                                      uint8_t* payload, size_t capacity);

#endif
