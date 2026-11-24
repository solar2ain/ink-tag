#include "inktag_resource_name.h"

#if !INKTAG_BUILD_PRO
#include <string.h>

static bool resource_alias_utf8_valid(const uint8_t *text, size_t length) {
    size_t index = 0u;
    while (index < length) {
        uint8_t first = text[index++];
        uint8_t continuation_count;
        uint32_t codepoint;
        uint32_t minimum;
        if (first < 0x80u) continue;
        if (first >= 0xC2u && first <= 0xDFu) {
            continuation_count = 1u;
            codepoint = (uint32_t)(first & 0x1Fu);
            minimum = 0x80u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            continuation_count = 2u;
            codepoint = (uint32_t)(first & 0x0Fu);
            minimum = 0x800u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            continuation_count = 3u;
            codepoint = (uint32_t)(first & 0x07u);
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (index + continuation_count > length) return false;
        while (continuation_count-- > 0u) {
            uint8_t next = text[index++];
            if ((next & 0xC0u) != 0x80u) return false;
            codepoint = (codepoint << 6u) | (uint32_t)(next & 0x3Fu);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            return false;
        }
    }
    return true;
}

static uint8_t upper_ascii(uint8_t c) {
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - 32u) : c;
}

static bool equal_name(const uint8_t *s, size_t n, const char *reserved) {
    size_t len = strlen(reserved);
    if (n != len) return false;
    for (size_t i=0; i<n; ++i)
        if (upper_ascii(s[i]) != upper_ascii((uint8_t)reserved[i])) return false;
    return true;
}

static bool reserved_name(const uint8_t *s, size_t n) {
    /* Stable storage naming rules: reserved UI/page names are not commands.
     * No radio, NDEF parsing, opcode construction or dispatch lives here. */
    static const char *const words[] = {
        "CALENDAR", "CLOCK", "DEVICE", "SLIDESHOW", "NEXT", "PREVIOUS", "CLEAR",
        "\xe6\x97\xa5\xe5\x8e\x86", "\xe6\x97\xb6\xe9\x97\xb4",
        "\xe8\xae\xbe\xe5\xa4\x87", "\xe8\xbd\xae\xe6\x92\xad",
        "\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa1\xb5", "\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa1\xb5",
        "\xe6\xb8\x85\xe5\xb1\x8f",
    };
    for (size_t i=0; i<sizeof(words)/sizeof(words[0]); ++i)
        if (equal_name(s,n,words[i])) return true;
    size_t prefix;
    if (n>5u && upper_ascii(s[0])=='S' && upper_ascii(s[1])=='L' &&
        upper_ascii(s[2])=='O' && upper_ascii(s[3])=='T' && s[4]=='-') prefix=5u;
    else if (n>7u && !memcmp(s,"\xe5\x9b\xbe\xe6\xa7\xbd-",7u)) prefix=7u;
    else return false;
    if (s[prefix]=='0') return false;
    unsigned value=0;
    for (size_t i=prefix;i<n;++i) {
        if (s[i]<'0' || s[i]>'9') return false;
        value=value*10u+(unsigned)(s[i]-'0');
        if (value>256u) return false;
    }
    return value!=0u;
}

bool inktag_resource_name_valid(const uint8_t *s, size_t n) {
    if (!s || !n || n>INKTAG_RESOURCE_NAME_MAX_BYTES || s[0]==' ' || s[n-1]==' ') return false;
    if (n>=4u && upper_ascii(s[0])=='I' && upper_ascii(s[1])=='K' &&
        upper_ascii(s[2])=='N' && upper_ascii(s[3])=='C') return false;
    for (size_t i=0;i<n;++i) if (s[i]<0x20u || s[i]==0x7fu) return false;
    return resource_alias_utf8_valid(s,n) && !reserved_name(s,n);
}

bool inktag_resource_name_matches(const uint8_t *candidate, size_t candidate_length,
                                 const uint8_t *name, size_t name_length) {
    if (!candidate || candidate_length!=name_length || !inktag_resource_name_valid(name,name_length)) return false;
    for (size_t i=0;i<name_length;++i)
        if (upper_ascii(candidate[i])!=upper_ascii(name[i])) return false;
    return true;
}
#endif
