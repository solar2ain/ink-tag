#include "inktag_display_settings.h"

#include <string.h>

#include "inktag_protocol_v2.h"

static bool display_name_valid(const uint8_t* text, uint8_t length) {
    uint8_t offset = 0u;
    uint8_t codepoints = 0u;
    if (text == NULL || length == 0u ||
        length > INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES) return false;
    while (offset < length) {
        uint8_t lead = text[offset++];
        uint8_t continuation;
        if (lead < 0x20u || lead == 0x7fu) return false;
        if (lead < 0x80u) continuation = 0u;
        else if (lead >= 0xc2u && lead <= 0xdfu) continuation = 1u;
        else if (lead >= 0xe0u && lead <= 0xefu) continuation = 2u;
        else if (lead >= 0xf0u && lead <= 0xf4u) continuation = 3u;
        else return false;
        if (continuation > length - offset) return false;
        if (continuation > 0u &&
            ((lead == 0xe0u && text[offset] < 0xa0u) ||
             (lead == 0xedu && text[offset] >= 0xa0u) ||
             (lead == 0xf0u && text[offset] < 0x90u) ||
             (lead == 0xf4u && text[offset] >= 0x90u))) return false;
        while (continuation-- > 0u) {
            if ((text[offset++] & 0xc0u) != 0x80u) return false;
        }
        if (++codepoints > INKTAG_DISPLAY_SETTINGS_MAX_NAME_CODEPOINTS)
            return false;
    }
    return true;
}

bool inktag_display_settings_decode(const uint8_t* payload, size_t length,
                                    inktag_display_settings_t* settings) {
    uint8_t name_length;
    uint32_t target_day;
    if (payload == NULL || settings == NULL ||
        length < INKTAG_DISPLAY_SETTINGS_BASE_SIZE ||
        payload[0] != INKTAG_SETTINGS_SECTION_DISPLAY ||
        payload[1] != INKTAG_DISPLAY_SETTINGS_WIRE_VERSION ||
        payload[2] > 1u ||
        (payload[3] & ~INKTAG_DISPLAY_SETTINGS_KNOWN_FLAGS) != 0u)
        return false;
    name_length = payload[8];
    if (length != INKTAG_DISPLAY_SETTINGS_BASE_SIZE + name_length ||
        name_length > INKTAG_DISPLAY_SETTINGS_MAX_NAME_BYTES) return false;
    target_day = (uint32_t)payload[4] |
                 ((uint32_t)payload[5] << 8u) |
                 ((uint32_t)payload[6] << 16u) |
                 ((uint32_t)payload[7] << 24u);
    if ((target_day == 0u) != (name_length == 0u) ||
        (name_length > 0u && !display_name_valid(payload + 9u, name_length)))
        return false;
    settings->week_start = payload[2];
    settings->flags = payload[3];
    settings->target_day = target_day;
    settings->name_length = name_length;
    memcpy(settings->name, payload + 9u, name_length);
    settings->name[name_length] = '\0';
    return true;
}

size_t inktag_display_settings_encode(const inktag_display_settings_t* settings,
                                      uint8_t* payload, size_t capacity) {
    size_t length;
    if (settings == NULL || payload == NULL || settings->week_start > 1u ||
        (settings->flags & ~INKTAG_DISPLAY_SETTINGS_KNOWN_FLAGS) != 0u ||
        ((settings->target_day == 0u) != (settings->name_length == 0u)) ||
        (settings->name_length > 0u &&
         !display_name_valid((const uint8_t*)settings->name,
                             settings->name_length))) return 0u;
    length = INKTAG_DISPLAY_SETTINGS_BASE_SIZE + settings->name_length;
    if (capacity < length) return 0u;
    payload[0] = INKTAG_SETTINGS_SECTION_DISPLAY;
    payload[1] = INKTAG_DISPLAY_SETTINGS_WIRE_VERSION;
    payload[2] = settings->week_start;
    payload[3] = settings->flags;
    payload[4] = (uint8_t)settings->target_day;
    payload[5] = (uint8_t)(settings->target_day >> 8u);
    payload[6] = (uint8_t)(settings->target_day >> 16u);
    payload[7] = (uint8_t)(settings->target_day >> 24u);
    payload[8] = settings->name_length;
    memcpy(payload + 9u, settings->name, settings->name_length);
    return length;
}
