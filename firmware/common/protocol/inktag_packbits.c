#include "inktag_packbits.h"

#include <string.h>

bool inktag_packbits_validate_block(const uint8_t *source,
                                    uint8_t source_length,
                                    uint16_t decoded_length) {
    const uint8_t *source_end;
    uint32_t produced = 0u;
    if (source == 0 || source_length == 0u || decoded_length == 0u)
        return false;
    source_end = source + source_length;
    while (source < source_end) {
        uint8_t control = *source++;
        uint16_t count = (control & 0x80u) != 0u ?
            (uint16_t)(control & 0x7fu) + 3u :
            (uint16_t)control + 1u;
        if ((control & 0x80u) != 0u) {
            if (source >= source_end) return false;
            ++source;
        } else {
            if (count > (uint16_t)(source_end - source)) return false;
            source += count;
        }
        produced += count;
        if (produced > decoded_length) return false;
    }
    return produced == decoded_length;
}

bool inktag_packbits_stream_block(const uint8_t *source,
                                  uint8_t source_length,
                                  uint16_t decoded_length,
                                  inktag_packbits_sink_t sink,
                                  void *context) {
    const uint8_t *source_end;
    uint8_t decoded[130];
    uint16_t decoded_used = 0u;
    if (sink == 0 ||
        !inktag_packbits_validate_block(source, source_length, decoded_length))
        return false;
    source_end = source + source_length;
    while (source < source_end) {
        uint8_t control = *source++;
        uint16_t count = (control & 0x80u) != 0u ?
            (uint16_t)(control & 0x7fu) + 3u :
            (uint16_t)control + 1u;
        bool repeat = (control & 0x80u) != 0u;
        uint8_t value = repeat ? *source++ : 0u;
        while (count != 0u) {
            uint16_t chunk = (uint16_t)(sizeof(decoded) - decoded_used);
            if (chunk > count) chunk = count;
            if (repeat) {
                memset(&decoded[decoded_used], value, chunk);
            } else {
                memcpy(&decoded[decoded_used], source, chunk);
                source += chunk;
            }
            decoded_used = (uint16_t)(decoded_used + chunk);
            count = (uint16_t)(count - chunk);
            if (decoded_used == sizeof(decoded)) {
                if (!sink(context, decoded, decoded_used)) return false;
                decoded_used = 0u;
            }
        }
    }
    if (decoded_used != 0u && !sink(context, decoded, decoded_used))
        return false;
    return true;
}
