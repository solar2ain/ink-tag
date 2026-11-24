#include "inktag_resource_name.h"
#include "inktag_text.h"
#include <string.h>

/* Pure name-rule subset of common component c25d3a79. No command execution. */
static bool reserved_token_matches(const uint8_t *text, size_t length,
                                const uint8_t *token) {
    uint8_t count = token[0] & 0x7fu;
    uint8_t case_mask = (token[0] & 0x80u) ? 0xdfu : 0xffu;
    if (length != count) return false;
    for (uint8_t index = 0u; index < count; ++index) {
        uint8_t actual = text[index];
        /* Case-folded tokens contain uppercase ASCII letters only. Clearing
         * bit5 accepts exactly their two ASCII cases, without a range branch. */
        actual &= case_mask;
        if (actual != token[1u + index]) return false;
    }
    return true;
}

static bool reserved_slot_suffix(const uint8_t *text, size_t length,
                              size_t prefix_length) {
    uint16_t value = 0u;
    size_t index;
    if (!text || length <= prefix_length ||
        text[prefix_length] == '0') return false;
    for (index = prefix_length; index < length; index++) {
        if (text[index] < '0' || text[index] > '9') return false;
        value = (uint16_t)(value * 10u + (uint16_t)(text[index] - '0'));
        if (value > 256u) return false;
    }
    if (value == 0u) return false;
    return true;
}

static bool resource_name_reserved(const uint8_t *text,size_t length) {
    static const uint8_t slot_cn_prefix[]={0xE5u,0x9Bu,0xBEu,0xE6u,0xA7u,0xBDu,'-'};
    static const uint8_t tokens[]={
        0x88u,0x43u,0x41u,0x4Cu,0x45u,0x4Eu,0x44u,0x41u,0x52u,0x85u,0x43u,0x4Cu,0x4Fu,0x43u,0x4Bu,0x86u,
        0x44u,0x45u,0x56u,0x49u,0x43u,0x45u,0x89u,0x53u,0x4Cu,0x49u,0x44u,0x45u,0x53u,0x48u,0x4Fu,0x57u,
        0x84u,0x4Eu,0x45u,0x58u,0x54u,0x88u,0x50u,0x52u,0x45u,0x56u,0x49u,0x4Fu,0x55u,0x53u,0x85u,0x43u,
        0x4Cu,0x45u,0x41u,0x52u,0x06u,0xE6u,0x97u,0xA5u,0xE5u,0x8Eu,0x86u,0x06u,0xE6u,0x97u,0xB6u,0xE9u,
        0x97u,0xB4u,0x06u,0xE8u,0xAEu,0xBEu,0xE5u,0xA4u,0x87u,0x06u,0xE8u,0xBDu,0xAEu,0xE6u,0x92u,0xADu,
        0x09u,0xE4u,0xB8u,0x8Bu,0xE4u,0xB8u,0x80u,0xE9u,0xA1u,0xB5u,0x09u,0xE4u,0xB8u,0x8Au,0xE4u,0xB8u,
        0x80u,0xE9u,0xA1u,0xB5u,0x06u,0xE6u,0xB8u,0x85u,0xE5u,0xB1u,0x8Fu,0x00u,
    };
    for(const uint8_t *token=tokens;*token;token+=1u+(*token&0x7fu))
        if(reserved_token_matches(text,length,token))return true;
    if(length>5u && (text[0]=='S'||text[0]=='s') && (text[1]=='L'||text[1]=='l') &&
       (text[2]=='O'||text[2]=='o') && (text[3]=='T'||text[3]=='t') && text[4]=='-' &&
       reserved_slot_suffix(text,length,5u))return true;
    return length>sizeof(slot_cn_prefix) && !memcmp(text,slot_cn_prefix,sizeof(slot_cn_prefix)) &&
           reserved_slot_suffix(text,length,sizeof(slot_cn_prefix));
}

bool inktag_resource_name_valid(const uint8_t *alias, size_t length) {
    if (!alias || length == 0u || length > INKTAG_RESOURCE_NAME_MAX_BYTES)
        return false;
    if (alias[0] == ' ' || alias[length - 1u] == ' ') return false;
    /* Preserve the reserved envelope prefix in every ASCII case. */
    if (length >= 4u && (alias[0] | 0x20u) == 'i' &&
        (alias[1] | 0x20u) == 'k' && (alias[2] | 0x20u) == 'n' &&
        (alias[3] | 0x20u) == 'c') return false;
    if (!(length <= UINT8_MAX && inktag_text_utf8_valid(alias, (uint8_t)length, 0u))) return false;
    return !resource_name_reserved(alias, length);
}

bool inktag_resource_name_matches(const uint8_t *candidate,
                                       size_t candidate_length,
                                       const uint8_t *alias,
                                       size_t alias_length) {
    size_t index;
    if (!candidate || !alias || candidate_length != alias_length ||
        !inktag_resource_name_valid(alias, alias_length)) {
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
