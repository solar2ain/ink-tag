#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "inktag_display_settings.h"

int main(void) {
    const char name[] = "国庆";
    uint8_t wire[32];
    inktag_display_settings_t input = {
        .week_start = 1u,
        .flags = INKTAG_DISPLAY_SETTINGS_FLAG_BOOT_REFRESH,
        .target_day = 20727u,
        .name_length = sizeof(name) - 1u,
    };
    inktag_display_settings_t output;
    size_t length;

    assert(INKTAG_DISPLAY_SETTINGS_DEFAULT_WEEK_START == 1u);

    memcpy(input.name, name, sizeof(name));
    length = inktag_display_settings_encode(&input, wire, sizeof(wire));
    assert(length == INKTAG_DISPLAY_SETTINGS_BASE_SIZE + sizeof(name) - 1u);
    assert(wire[1] == 1u);
    assert(inktag_display_settings_decode(wire, length, &output));

    wire[1] = 3u;
    assert(!inktag_display_settings_decode(wire, length, &output));
    wire[1] = 1u;
    assert(output.week_start == input.week_start);
    assert(output.flags == input.flags);
    assert(output.target_day == input.target_day);
    assert(output.name_length == sizeof(name) - 1u);
    assert(strcmp(output.name, name) == 0);

    input.target_day = 0u;
    input.name_length = 0u;
    length = inktag_display_settings_encode(&input, wire, sizeof(wire));
    assert(length == INKTAG_DISPLAY_SETTINGS_BASE_SIZE);
    assert(inktag_display_settings_decode(wire, length, &output));

    wire[2] = 2u;
    assert(!inktag_display_settings_decode(wire, length, &output));
    wire[2] = 1u;
    wire[8] = 1u;
    assert(!inktag_display_settings_decode(wire, length, &output));
    assert(!inktag_display_settings_decode(wire, 4u, &output));
    return 0;
}
