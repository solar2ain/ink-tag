#include "inktag_om_pins.h"
#include <string.h>
#include "nvds.h"
#if !defined(INKTAG_OM_FIXED_RECOVERY)
#include "inktag_screen_busy.h"
#include "inktag_epd_config.h"
#endif
#include "inktag_protocol_v2.h"
static inktag_pin_config_t s_default,s_effective;
static bool s_valid,s_override,s_busy_known,s_busy_invert;
static const char s_pin_contract[]="INKTAG-OM-RELEASE-V1";
static const bool s_hs =
#if defined(INKTAG_OM_HSELA)
 true;
#else
 false;
#endif
bool inktag_om_pins_prepare(const epd_screen_config_t *screen,
                           inktag_om_pin_candidate_t *candidate) {
    inktag_om_pin_candidate_t next={0};
    uint8_t raw[INKTAG_OM_PREF_SIZE],board=0u;
    nvds_tag_len_t n=sizeof(raw);
    if(!candidate || nvds_get(NVDS_TAG_USER_BEGIN+8,&n,raw)!=NVDS_OK)return false;
    if(!inktag_om_pin_factory_decode(raw,n,s_hs,&board,&next.defaults))return false;
    next.effective=next.defaults;n=sizeof(raw);
    uint8_t status=nvds_get(NVDS_TAG_USER_BEGIN+4,&n,raw);
    if(status!=NVDS_TAG_NOT_DEFINED) {
        if(status!=NVDS_OK || !inktag_om_pin_pref_decode(raw,n,s_hs,&next.overridden,&next.effective))return false;
        if(!next.overridden)next.effective=next.defaults;
        const uint8_t aux[]={raw[12],raw[14],raw[16],raw[19],raw[20]};
        for(unsigned i=0;i<sizeof(aux);++i)for(unsigned j=0;j<12u;++j)
            if(aux[i]!=255u && aux[i]==next.effective.pins[j])return false;
    }
#if !defined(INKTAG_OM_FIXED_RECOVERY)
    bool high;
    next.busy_known=epd_screen_busy_active_high(screen,&high);
    if(next.busy_known) {
        /* Derived BUSY is report capability, not editable/stored polarity.
         * The controller consumes its original physical signal below. */
        /* Existing SSD1680 BW waits for logical false, not physical low. */
        next.busy_invert=screen->driver_id==EPD_SCREEN_DRIVER_SSD1680 &&
                         screen->color_id==EPD_SCREEN_COLOR_BW;
    }
#else
    (void)screen;
#endif
    *candidate=next;return true;
}
void inktag_om_pins_publish(const inktag_om_pin_candidate_t *next) {
    s_default=next->defaults;s_effective=next->effective;
    s_override=next->overridden;s_busy_known=next->busy_known;
    s_busy_invert=next->busy_invert;s_valid=true;
}
#if !defined(INKTAG_OM_FIXED_RECOVERY)
bool inktag_om_pins_prepare_override(bool present,const uint8_t *wire,
                                     inktag_om_pin_candidate_t *next) {
    epd_screen_config_t screen={0};
    (void)epd_screen_config_read(&screen);
    inktag_pin_config_t pins;
    if(!inktag_om_pins_prepare(&screen,next))return false;
    if(present) {
        if(!inktag_pin_config_decode(wire,16u,&pins))return false;
    } else pins=next->defaults;
    if(!inktag_om_pin_valid(&pins,s_hs))return false;
    if(!next->busy_known)
        for(unsigned i=0;i<=INKTAG_PIN_ENABLE;++i)
            if(pins.pins[i]!=next->effective.pins[i] ||
               ((pins.active_low^next->effective.active_low)&(1u<<i)))return false;
    next->effective=pins;next->overridden=present;return true;
}
#endif
void inktag_om_pins_reload(void) {
    inktag_om_pin_candidate_t next;
    epd_screen_config_t screen={0};
    __asm__ volatile("" : : "r"(s_pin_contract) : "memory");
    s_valid=false;
#if !defined(INKTAG_OM_FIXED_RECOVERY)
    (void)epd_screen_config_read(&screen);
#endif
    if(inktag_om_pins_prepare(&screen,&next))inktag_om_pins_publish(&next);
}
bool inktag_om_pins_available(void){return s_valid;}
bool inktag_om_pins_screen_available(void){return s_valid&&s_busy_known;}
bool inktag_om_pins_busy_level(bool raw){return raw!=s_busy_invert;}
bool inktag_om_pins_active_low(inktag_pin_role_t role){return s_valid&&role<INKTAG_PIN_ROLE_COUNT&&(s_effective.active_low&(1u<<role));}
void inktag_om_pins_invalidate(void){s_valid=false;}
bool inktag_om_pins_has_override(void){return s_valid&&s_override;}
bool inktag_om_pins_effective(inktag_pin_config_t *out){if(!out||!s_valid)return false;*out=s_effective;return true;}
uint8_t inktag_om_pins_led(uint8_t channel){return s_valid&&channel<3u?s_effective.pins[7u+channel]:255u;}
bool inktag_om_pins_info(uint8_t out[36]) {
    if(!out||!s_valid)return false;
    out[0]=1u;out[1]=(s_override?3u:2u)|(s_busy_known?INKTAG_PIN_REPORT_RECIPE_BUSY_DERIVED:0u);
    out[2]=(uint8_t)INKTAG_OM_PIN_EDITABLE_MASK;out[3]=INKTAG_OM_PIN_EDITABLE_MASK>>8;
    return inktag_pin_config_encode(&s_default,out+4,16u)==16u &&
           inktag_pin_config_encode(&s_effective,out+20,16u)==16u;
}
