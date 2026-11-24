#include <assert.h>
#include <stdint.h>

#include "inktag_ble_identity.h"

int main(void) {
    static const uint8_t ascii[] = "InkTag_OM_63CF";
    static const uint8_t chinese[] = {
        'I','n','k','T','a','g','_', 0xE5,0xAE,0xA2, 0xE5,0x8E,0x85
    };
    static const uint8_t emoji[] = {
        'I','n','k','T','a','g','_', 0xF0,0x9F,0x8F,0xA0
    };
    static const uint8_t cjk_a_first[] = {
        'I','n','k','T','a','g','_', 0xE3,0x90,0x80
    };
    static const uint8_t cjk_a_last[] = {
        'I','n','k','T','a','g','_', 0xE4,0xB6,0xBF
    };
    static const uint8_t cjk_first[] = {
        'I','n','k','T','a','g','_', 0xE4,0xB8,0x80
    };
    static const uint8_t cjk_last[] = {
        'I','n','k','T','a','g','_', 0xE9,0xBF,0xBF
    };
    static const uint8_t ideographic_zero[] = {
        'I','n','k','T','a','g','_', 0xE3,0x80,0x87
    };
    static const uint8_t ascii_control[] = {
        'I','n','k','T','a','g','_', 'a', 0x1F
    };
    static const uint8_t max19[] = "InkTag_123456789012";
    static const uint8_t too_long[] = "InkTag_1234567890123";

    assert(inktag_ble_device_name_valid(ascii, sizeof(ascii) - 1u));
    assert(inktag_ble_device_name_valid(chinese, sizeof(chinese)));
    assert(inktag_ble_device_name_valid(cjk_a_first, sizeof(cjk_a_first)));
    assert(inktag_ble_device_name_valid(cjk_a_last, sizeof(cjk_a_last)));
    assert(inktag_ble_device_name_valid(cjk_first, sizeof(cjk_first)));
    assert(inktag_ble_device_name_valid(cjk_last, sizeof(cjk_last)));
    assert(inktag_ble_device_name_valid(ideographic_zero, sizeof(ideographic_zero)));
    assert(inktag_ble_device_name_valid(max19, sizeof(max19) - 1u));
    assert(!inktag_ble_device_name_valid((const uint8_t *)"InkTag_", 7u));
    assert(!inktag_ble_device_name_valid((const uint8_t *)"inktag_name", 11u));
    /* UTF-8 validity and the product whitelist are official-client policy;
     * firmware deliberately accepts high bytes to keep this guard tiny. */
    assert(inktag_ble_device_name_valid(emoji, sizeof(emoji)));
    assert(!inktag_ble_device_name_valid(ascii_control, sizeof(ascii_control)));
    assert(!inktag_ble_device_name_valid(too_long, sizeof(too_long) - 1u));
    return 0;
}
