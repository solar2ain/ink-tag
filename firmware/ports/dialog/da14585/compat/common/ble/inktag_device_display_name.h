#ifndef INKTAG_DEVICE_DISPLAY_NAME_H
#define INKTAG_DEVICE_DISPLAY_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INKTAG_DEVICE_LABEL_MAX_BYTES 24u
#define INKTAG_DEVICE_DEFAULT_NAME_BYTES 11u

/* New writes only. The existing persisted-record UTF-8 validator remains
 * unchanged: historical labels must not invalidate the whole settings record.
 * Zero length clears the custom label and selects the display default. */
static inline bool inktag_device_label_write_valid(
    const uint8_t *label, uint8_t length)
{
    uint8_t i;
    if (length > INKTAG_DEVICE_LABEL_MAX_BYTES ||
        (length != 0u && label == 0)) return false;
    for (i = 0u; i < length; ++i) {
        if (label[i] < 0x20u || label[i] > 0x7Eu) return false;
    }
    return true;
}

/* Display/readback only, NEVER a GAP rename or a storage mutation.
 * A non-empty label must already have passed the port's existing record/read
 * validation. Its bytes (including a valid legacy UTF-8 label) are preserved.
 * id4 is the immutable ordinary-App suffix, not Recovery/Find address state.
 * Fail closed rather than truncate a label or split a UTF-8 sequence. */
static inline bool inktag_device_display_name(
    char *output, size_t capacity, const uint8_t *label, uint8_t length,
    uint16_t id4)
{
    static const char prefix[] = "InkTag ";
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    size_t required = length != 0u ? length : INKTAG_DEVICE_DEFAULT_NAME_BYTES;
    if (output == 0 || capacity == 0u) return false;
    if (length > INKTAG_DEVICE_LABEL_MAX_BYTES ||
        (length != 0u && label == 0) || capacity <= required) {
        output[0] = '\0';
        return false;
    }
    if (length != 0u) {
        for (i = 0u; i < length; ++i) output[i] = (char)label[i];
    } else {
        for (i = 0u; i < sizeof(prefix) - 1u; ++i) output[i] = prefix[i];
        for (i = 0u; i < 4u; ++i)
            output[sizeof(prefix) - 1u + i] = hex[(id4 >> (12u - 4u * i)) & 0xFu];
    }
    output[required] = '\0';
    return true;
}

#endif
