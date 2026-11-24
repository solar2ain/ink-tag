#include "recovery.h"
#include "inktag_tlsr_recovery_pins.h"
#include "inktag_tlsr_pin_layout.h"
#include "inktag_tlsr_flash_layout.h"
#include "inktag_settings_journal.h"
#include "inktag_tlsr_pin_backend.h"

uint32_t inktag_fnv1a(const uint8_t *data, uint16_t length);

static uint32_t led_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8u) |
           ((uint32_t)p[2]<<16u) | ((uint32_t)p[3]<<24u);
}

/* Decode only the maintenance role. Non-LED routes are opaque, except that
 * none may alias an LED output. Screen completeness/SPI polarity and user
 * input semantics belong to App, not Recovery. */
static bool led_decode(const uint8_t *block, uint16_t adc,
                       const uint8_t *aux, inktag_tlsr_recovery_led_pins_t *out) {
    inktag_tlsr_recovery_led_pins_t candidate;
    uint16_t polarity=(uint16_t)block[2] | ((uint16_t)block[3]<<8u);
    if (block[0]!=INKTAG_PIN_CONFIG_VERSION || block[1] ||
        (polarity & ~INKTAG_PIN_ACTIVE_LOW_MASK)) return false;
    candidate.active_low=(uint8_t)((polarity>>INKTAG_PIN_LED_RED)&7u);
    for (uint8_t i=0u;i<3u;++i) {
        uint8_t role=(uint8_t)(INKTAG_PIN_LED_RED+i), pin=block[4u+role];
        candidate.pins[i]=inktag_tlsr_pin_gpio(pin);
        if (pin==INKTAG_PIN_UNUSED) {
            if (candidate.active_low & (1u<<i)) return false;
            continue;
        }
        if (pin>=32u || (UINT32_C(0x00ff0060) & (UINT32_C(1)<<pin)) ||
            candidate.pins[i]==adc ||
            (pin==7u && (i!=2u || !(candidate.active_low & 4u)))) return false;
        for (uint8_t other=0u;other<INKTAG_PIN_ROLE_COUNT;++other)
            if (other!=role && block[4u+other]==pin) return false;
        if (aux && (aux[7]==pin || aux[10]==pin || aux[11]==pin ||
            aux[56]==pin || aux[58]==pin)) return false;
    }
    *out=candidate;
    return true;
}

/* The scanner is read-only; even its context cannot reach an erase/program
 * callback. The complete committed generation must be clean before ID7 is
 * interpreted. Opaque unrelated records stay opaque and are never copied. */
static bool recovery_pin_read(void *context,uint32_t address,void *out,uint16_t n) {
    (void)context;
    if (!out || address<INKTAG_SETTINGS_SECTOR ||
        address>INKTAG_SETTINGS_SECTOR+INKTAG_SETTINGS_JOURNAL_SIZE ||
        n>INKTAG_SETTINGS_SECTOR+INKTAG_SETTINGS_JOURNAL_SIZE-address) return false;
    flash_read_page(address,n,out);
    return true;
}

bool inktag_tlsr_recovery_led_pins(inktag_tlsr_recovery_led_pins_t *out) {
    uint8_t record[INKTAG_TLSR_PIN_RECORD_BYTES];
    uint8_t settings[INKTAG_TLSR_PIN_SETTINGS_BYTES];
    uint16_t length=0u;
    inktag_tlsr_recovery_led_pins_t candidate;
    inktag_settings_journal_read_result_t result;
    inktag_board_id_t board=inktag_board_detect();
    const inktag_tlsr_recovery_profile_t *facts=inktag_tlsr_board_recovery_profile(board);
    const inktag_settings_journal_io_t io={NULL,INKTAG_SETTINGS_SECTOR,
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS),
        recovery_pin_read,NULL,NULL};
    if (!out || !facts || !inktag_tlsr_board_pin_record_required()) return false;
    flash_read_page(INKTAG_TLSR_PIN_FACTORY_ADDRESS,sizeof(record),record);
    if (record[0]!='I' || record[1]!='P' || record[2]!='N' || record[3]!='1' ||
        record[4]!=1u || record[5]!=INKTAG_PIN_CONFIG_SIZE ||
        record[6]!=(uint8_t)board || record[7] ||
        led_u32(record+24u)!=inktag_fnv1a(record,24u) ||
        led_u32(record+28u)!=UINT32_C(0x314d4350) ||
        !led_decode(record+8u,facts->supply_adc,NULL,&candidate)) return false;
    result=inktag_settings_journal_readonly_result(&io,
        INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,settings,sizeof(settings),&length);
    if (result==INKTAG_SETTINGS_JOURNAL_READ_ERROR) return false;
    if (result==INKTAG_SETTINGS_JOURNAL_READ_FOUND) {
        if (length!=sizeof(settings) || settings[0]!=INKTAG_TLSR_PIN_SETTINGS_VERSION ||
            settings[INKTAG_TLSR_PIN_SETTINGS_PRESENCE_OFFSET]>1u) return false;
        for (uint8_t i=INKTAG_TLSR_PIN_SETTINGS_RESERVED_OFFSET;i<sizeof(settings);++i)
            if (settings[i]) return false;
        if (settings[INKTAG_TLSR_PIN_SETTINGS_PRESENCE_OFFSET]) {
            if (!led_decode(settings+INKTAG_TLSR_PIN_SETTINGS_BLOCK_OFFSET,
                    facts->supply_adc,settings,&candidate)) return false;
        } else {
            for (uint8_t i=INKTAG_TLSR_PIN_SETTINGS_BLOCK_OFFSET;
                    i<INKTAG_TLSR_AUX_SETTINGS_EXTRA_OFFSET;++i)
                if (settings[i]) return false;
            if (!led_decode(record+8u,facts->supply_adc,settings,&candidate)) return false;
        }
    }
    *out=candidate;
    return true;
}
