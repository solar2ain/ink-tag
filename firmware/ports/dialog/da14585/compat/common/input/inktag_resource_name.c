/* DA Free image-name projection. The two historical bool interfaces and
 * UTF-8/reserved-name contract are retained without NFC command machinery. */
#include "inktag_resource_name.h"
#include <string.h>
static bool resource_ascii_equal(const uint8_t *a,const uint8_t *b,size_t n) {
    for(size_t i=0;i<n;i++) {
        uint8_t x=a[i],y=b[i];
        if(x>='a'&&x<='z')x-=32u;
        if(y>='a'&&y<='z')y-=32u;
        if(x!=y)return false;
    }
    return true;
}
static bool resource_slot_suffix(const uint8_t *text,size_t length,size_t prefix) {
    uint16_t value=0;
    if(length<=prefix || text[prefix]=='0')return false;
    for(size_t i=prefix;i<length;i++) {
        if(text[i]<'0'||text[i]>'9')return false;
        value=(uint16_t)(value*10u+text[i]-'0');
        if(value>256u)return false;
    }
    return value!=0;
}
static bool resource_name_reserved(const uint8_t *text,size_t length) {
    static const char *const words[]={"CALENDAR","CLOCK","DEVICE","SLIDESHOW","NEXT","PREVIOUS","CLEAR",
        "\xe6\x97\xa5\xe5\x8e\x86","\xe6\x97\xb6\xe9\x97\xb4","\xe8\xae\xbe\xe5\xa4\x87",
        "\xe8\xbd\xae\xe6\x92\xad","\xe4\xb8\x8b\xe4\xb8\x80\xe9\xa1\xb5",
        "\xe4\xb8\x8a\xe4\xb8\x80\xe9\xa1\xb5","\xe6\xb8\x85\xe5\xb1\x8f"};
    for(size_t i=0;i<sizeof(words)/sizeof(words[0]);i++)
        if(length==strlen(words[i]) && resource_ascii_equal(text,(const uint8_t *)words[i],length))return true;
    if(length>5u && text[4]=='-' && resource_ascii_equal(text,(const uint8_t *)"SLOT",4u))
        return resource_slot_suffix(text,length,5u);
    static const uint8_t slot_prefix[]={0xe5,0x9b,0xbe,0xe6,0xa7,0xbd,'-'};
    return length>sizeof(slot_prefix) && !memcmp(text,slot_prefix,sizeof(slot_prefix)) &&
        resource_slot_suffix(text,length,sizeof(slot_prefix));
}
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

bool inktag_nfc_resource_alias_valid(const uint8_t *alias, size_t length) {
    size_t index;
    if (!alias || length == 0u || length > INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES)
        return false;
    if (alias[0] == ' ' || alias[length - 1u] == ' ') return false;
    /* Envelope prefixes cannot be aliases: malformed IKNC never falls back
     * to human text. Reserve all ASCII case variants like alias matching. */
    if (length >= 4u && (alias[0] | 0x20u) == 'i' &&
        (alias[1] | 0x20u) == 'k' && (alias[2] | 0x20u) == 'n' &&
        (alias[3] | 0x20u) == 'c') return false;
    for (index = 0u; index < length; index++) {
        if (alias[index] < 0x20u || alias[index] == 0x7Fu) return false;
    }
    if (!resource_alias_utf8_valid(alias, length)) return false;
    return !resource_name_reserved(alias, length);
}

bool inktag_nfc_resource_alias_matches(const uint8_t *candidate,
                                       size_t candidate_length,
                                       const uint8_t *alias,
                                       size_t alias_length) {
    size_t index;
    if (!candidate || !alias || candidate_length != alias_length ||
        !inktag_nfc_resource_alias_valid(alias, alias_length)) {
        return false;
    }
    for (index = 0u; index < alias_length; index++) {
        uint8_t left = candidate[index];
        uint8_t right = alias[index];
        if (left >= 'a' && left <= 'z') left = (uint8_t)(left - 32u);
        if (right >= 'a' && right <= 'z') right = (uint8_t)(right - 32u);
        if (left != right) return false;
    }
    return true;
}
