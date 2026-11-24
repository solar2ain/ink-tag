#include "inktag_text.h"
#include <stddef.h>

bool inktag_text_utf8_valid(const uint8_t *text, uint8_t length,
                           uint8_t maximum_codepoints) {
    uint8_t offset = 0u, codepoints = 0u;
    if (length && text == NULL) return false;
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
        if (continuation &&
            ((lead == 0xe0u && text[offset] < 0xa0u) ||
             (lead == 0xedu && text[offset] >= 0xa0u) ||
             (lead == 0xf0u && text[offset] < 0x90u) ||
             (lead == 0xf4u && text[offset] >= 0x90u))) return false;
        while (continuation-- > 0u)
            if ((text[offset++] & 0xc0u) != 0x80u) return false;
        if (maximum_codepoints && ++codepoints > maximum_codepoints) return false;
    }
    return true;
}
