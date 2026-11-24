#include "main.h"
#include <stddef.h>
#include <string.h>
#define INKTAG_TLSR_PIN_RECORD_PILOT 1
#include "inktag_tlsr_pin_record.h"
#include "inktag_tlsr_pin_backend.h"
#include "inktag_tlsr_aux_backend.h"
#include "inktag_tlsr_factory_screen.h"
#include "inktag_screen_busy.h"
#include "inktag_protocol_v2.h"
#include "tlsr8359_hsel.h"

static RAM inktag_tlsr_gpio_profile_t s_gpio_profile;
static RAM inktag_pin_config_t s_pin_defaults;
static RAM inktag_tlsr_pin_backend_t s_pin_backend;
static RAM bool s_default_valid, s_gpio_valid, s_gpio_overridden;
static RAM uint8_t s_led_active_low;
#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_contract_pro.inc"
#endif
static const char s_pin_contract[]="INKTAG-TL-PINS2";
typedef char gpio_offsets_fit[(sizeof(inktag_tlsr_gpio_profile_t)<=256u)?1:-1];

/* Exactly the inverse of the public single-bit B85 encoding. Invalid/zero
 * immutable facts match no pad, just as the old bounded32-pad search did. */
static uint32_t protected_gpio(uint32_t gpio) {
    uint32_t bit=gpio&255u;
    return gpio<0x400u && bit && !(bit&(bit-1u)) ? bit<<((gpio>>8u)*8u) : 0u;
}
#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_protected_pro.inc"
#endif
static void pin_backend_init(inktag_tlsr_pin_backend_t *backend,
    const inktag_tlsr_board_profile_t *base) {
    backend->usable=UINT32_MAX;
    backend->protected_pins=UINT32_C(0x00ff0060)|protected_gpio(base->supply_adc);
}
/* One checked Factory reader shared by both boot consumers. No cache,
 * additional Flash access, GPIO application or change to startup order. */
static bool __attribute__((noinline)) factory_pins_read(inktag_board_id_t board,
    inktag_tlsr_pin_backend_t *backend,inktag_pin_config_t *out) {
    uint8_t record[INKTAG_TLSR_PIN_RECORD_BYTES];
    inktag_pin_config_t candidate;
    __asm__ volatile ("" : : "r"(s_pin_contract) : "memory");
    flash_read_page(INKTAG_TLSR_PIN_FACTORY_ADDRESS,sizeof(record),record);
    /* This consumer always requires IPN1/32B. AllFF therefore fails the same
     * mandatory header, without a separate erased-record pass. Decode once
     * and run the complete backend (structure/protected nets/polarity) once. */
    if (!(record[0]=='I' && record[1]=='P' && record[2]=='N' && record[3]=='1' &&
        record[4]==1u && record[5]==INKTAG_PIN_CONFIG_SIZE && record[6]==(uint8_t)board && !record[7] &&
        inktag_tlsr_pin_record_u32(record+24u)==inktag_fnv1a(record,24u) &&
        inktag_tlsr_pin_record_u32(record+28u)==UINT32_C(0x314d4350) &&
        inktag_pin_config_decode(record+8u,INKTAG_PIN_CONFIG_SIZE,&candidate) &&
        inktag_tlsr_pin_backend_validate(&candidate,backend))) return false;
    *out=candidate;
    return true;
}
/* No serialization round-trip merely to inspect the effective routing. */
static bool pin_effective_read(inktag_pin_config_t *out) {
    if (!out || !s_default_valid || !s_gpio_valid) return false;
    if (s_gpio_overridden) {
        if (!inktag_pin_config_decode(inktag_settings()->pin_override,16u,out)) return false;
    } else *out=s_pin_defaults;
    return true;
}

static bool pin_aux_disjoint(const inktag_pin_config_t *pins) {
#if !INKTAG_STELLARPRO154Q_PANEL_AVAILABLE
    if (inktag_board_detect()==INKTAG_BOARD_STELLAR_PRO_154Q_N)
        for (uint8_t i=0u;i<INKTAG_PIN_ROLE_COUNT;++i)
            if ((i<INKTAG_PIN_LED_RED || i>INKTAG_PIN_LED_BLUE) &&
                pins->pins[i]!=INKTAG_PIN_UNUSED) return false;
#endif
#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_conflict_pro.inc"
#else
    return pins != NULL;
#endif
}

#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_gpio_pro.inc"
#endif
#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_config_pro.inc"
#endif
#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_ready_pro.inc"
#endif
#if INKTAG_BUILD_PRO
#endif

#if INKTAG_BUILD_PRO
#include "inktag_pin_aux_factory_pro.inc"
#endif

static bool pin_candidate_valid(const inktag_pin_config_t *pins) {
    return pin_aux_disjoint(pins) && inktag_tlsr_pin_backend_validate(pins,&s_pin_backend);
}

bool inktag_tlsr_factory_screen(uint8_t wire[EPD_SCREEN_WIRE_SIZE]) {
    uint8_t record[INKTAG_TLSR_SCREEN_FACTORY_BYTES];
    if (!inktag_tlsr_board_pin_record_required()) return false;
    flash_read_page(INKTAG_TLSR_SCREEN_FACTORY_ADDRESS,sizeof(record),record);
    return inktag_tlsr_factory_screen_decode(record,(uint8_t)inktag_board_detect(),wire);
}

static void pin_profile_apply(const inktag_pin_config_t *pins) {
    const inktag_tlsr_board_profile_t *base=inktag_tlsr_board_profile(inktag_board_detect());
    s_gpio_profile.base=base;
    /* Byte offsets refer to the enclosing object, not pointer arithmetic
     * across independent struct fields. Keep SDK GPIO conversion identical. */
#define ROUTE(field) offsetof(inktag_tlsr_gpio_profile_t,field)
    static const uint8_t offsets[]={
        ROUTE(epd_mosi),ROUTE(epd_clk),ROUTE(epd_cs),ROUTE(epd_dc),
        ROUTE(epd_reset),ROUTE(epd_busy),ROUTE(epd_enable),
        ROUTE(led_red),ROUTE(led_green),ROUTE(led_blue)};
#undef ROUTE
    for (uint8_t role=0u;role<sizeof(offsets);++role) {
        uint16_t gpio=inktag_tlsr_pin_gpio(pins->pins[role]);
        memcpy((uint8_t *)&s_gpio_profile+offsets[role],&gpio,sizeof(gpio));
    }
    s_gpio_profile.epd_hardware_spi=base->epd_hardware_spi &&
        inktag_tlsr_pin_bus_is_hardware_spi(pins);
    s_gpio_profile.epd_enable_active_low=(pins->active_low&(1u<<INKTAG_PIN_ENABLE))!=0u;
    s_gpio_profile.indicator_colors=base->indicator_colors &
        ((s_gpio_profile.led_red?1u:0u)|(s_gpio_profile.led_green?2u:0u)|
         (s_gpio_profile.led_blue?4u:0u));
    s_led_active_low=(uint8_t)((pins->active_low>>INKTAG_PIN_LED_RED)&7u);
}

bool inktag_tlsr_pin_routes_validate(const uint8_t wire[16]) {
    inktag_pin_config_t pins;
    /* Electrical screen semantics are not mutable routing bits. */
    return s_default_valid && inktag_settings_device_valid() &&
        s_gpio_valid && inktag_pin_config_decode(wire,16u,&pins) &&
        pin_candidate_valid(&pins);
}

bool inktag_tlsr_pin_routes_prepare(const epd_screen_config_t *screen,
                                    inktag_tlsr_pin_candidate_t *candidate) {
    inktag_tlsr_pin_candidate_t next;
    const inktag_settings_journal_io_t *io=inktag_settings_gpio_readonly_io();
    if (!candidate || !s_default_valid || !io) return false;
    next.defaults=s_pin_defaults;
    /* Defaults and effective wire blocks contain only EN/LED polarity. */
    if (!inktag_tlsr_pin_settings_effective_prevalidated(io,&next.defaults,
            inktag_tlsr_pin_allowed,&s_pin_backend,&next.effective,&next.overridden) ||
        !pin_candidate_valid(&next.effective)) return false;
    next.busy_active_high=false;
    next.busy_known=epd_screen_busy_active_high(screen,&next.busy_active_high);
    *candidate=next;
    return true;
}

void inktag_tlsr_pin_routes_apply(const inktag_tlsr_pin_candidate_t *candidate) {
    s_pin_defaults=candidate->defaults;
    s_gpio_overridden=candidate->overridden;
    pin_profile_apply(&candidate->effective);
    s_gpio_profile.epd_busy_known=candidate->busy_known;
    s_gpio_profile.epd_busy_active_high=candidate->busy_active_high;
    s_gpio_valid=true;
}

bool inktag_tlsr_pin_routes_prepare_override(const uint8_t wire[16], bool override,
                                             inktag_tlsr_pin_candidate_t *candidate) {
    epd_screen_config_t screen={0};
    (void)epd_screen_config_decode(inktag_settings()->screen_config,EPD_SCREEN_WIRE_SIZE,&screen);
    if (!inktag_tlsr_pin_routes_validate(wire) ||
        !inktag_tlsr_pin_routes_prepare(&screen,candidate) ||
        !inktag_pin_config_decode(wire,16u,&candidate->effective)) return false;
    candidate->overridden=override;
    return true;
}

bool inktag_tlsr_pin_routes_refresh(void) {
    epd_screen_config_t screen={0};
    inktag_tlsr_pin_candidate_t candidate;
    s_gpio_valid=false;
    (void)epd_screen_config_decode(inktag_settings()->screen_config,EPD_SCREEN_WIRE_SIZE,&screen);
    if (!inktag_tlsr_pin_routes_prepare(&screen,&candidate)) return false;
    inktag_tlsr_pin_routes_apply(&candidate);
    return true;
}
void inktag_tlsr_pin_routes_invalidate(void) { s_gpio_valid=false; }

void inktag_tlsr_pin_routes_init(inktag_board_id_t board) {
    const inktag_tlsr_board_profile_t *base=inktag_tlsr_board_profile(board);
    s_default_valid=s_gpio_valid=s_gpio_overridden=false;
    if (!base || !inktag_tlsr_board_pin_record_required()) return;
    /* The public namespace excludes port E/MSPI. Conservatively reserve the
     * complete C bank (fixed NFC and stock wake/clock nets), USB/debug PA5/6
     * and the safety ADC. This is a backend ownership boundary, not a claim
     * that all remaining pads on a newly wired PCB are electrically qualified. */
    pin_backend_init(&s_pin_backend,base);
    if (!factory_pins_read(board,&s_pin_backend,&s_pin_defaults) ||
        !pin_aux_disjoint(&s_pin_defaults)) return;
    s_default_valid=true;
    (void)inktag_tlsr_pin_routes_refresh();
}

const inktag_tlsr_gpio_profile_t *inktag_tlsr_gpio_profile(void) {
    return s_gpio_valid?&s_gpio_profile:NULL;
}
bool inktag_tlsr_pin_routes_available(void) { return s_default_valid && s_gpio_valid; }
uint8_t inktag_tlsr_pin_routes_led_active_low(void) { return s_gpio_valid?s_led_active_low:0u; }

bool inktag_tlsr_pin_routes_status(uint8_t wire[36]) {
    inktag_pin_config_t effective;
    if (!wire || !pin_effective_read(&effective)) return false;
    wire[0]=1u; wire[1]=(s_gpio_overridden?1u:0u)|2u|INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED;
    wire[2]=0xffu; wire[3]=3u; /* roles0..9 only; UART remains unavailable */
    return inktag_pin_config_encode(&s_pin_defaults,&wire[4],16u) &&
        inktag_pin_config_encode(&effective,&wire[20],16u);
}
