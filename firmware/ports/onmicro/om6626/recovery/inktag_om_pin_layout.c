#include "inktag_om_pin_layout.h"
#include "inktag_release_v1.h"
#include <string.h>
#if !defined(INKTAG_OM_FIXED_RECOVERY)
#include "inktag_epd_screen.h"
#endif

_Static_assert(INKTAG_PIN_CONFIG_VERSION==INKTAG_RELEASE_PIN_VERSION,
               "OM release-v1 requires the matching common pin codec");
_Static_assert(INKTAG_AUX_IO_VERSION==INKTAG_RELEASE_AUX_VERSION,
               "OM release-v1 requires the matching complete common AUX codec");

bool inktag_om_pin_board_valid(uint8_t board, bool hs) {
    return hs ? board==4u : board>=1u && board<=3u;
}

void inktag_om_pin_defaults(bool hs, inktag_pin_config_t *out) {
    const uint8_t z[12]={21,15,22,20,16,3,7,4,18,17,255,255};
    const uint8_t h[12]={21,15,22,2,16,20,7,8,9,10,255,255};
    memcpy(out->pins,hs?h:z,12);out->active_low=0x0380u;
}

bool inktag_om_pin_allowed(void *context,inktag_pin_role_t role,uint8_t pin) {
    bool hs=*(const bool *)context;
    inktag_pin_config_t d;inktag_om_pin_defaults(hs,&d);
    if(role==INKTAG_PIN_DC || role==INKTAG_PIN_BUSY)
        return pin==d.pins[INKTAG_PIN_DC] || pin==d.pins[INKTAG_PIN_BUSY];
    if(role==INKTAG_PIN_LED_RED || role==INKTAG_PIN_LED_GREEN)
        return pin==d.pins[INKTAG_PIN_LED_RED] || pin==d.pins[INKTAG_PIN_LED_GREEN];
    return pin==d.pins[role] && role<INKTAG_PIN_TXD;
}

bool inktag_om_pin_valid(const inktag_pin_config_t *pins,bool hs) {
    if(!pins || (pins->active_low&~INKTAG_RELEASE_EDITABLE_ACTIVE_LOW_MASK) ||
       !inktag_pin_config_validate(pins,inktag_om_pin_allowed,&hs))return false;
    inktag_pin_config_t d;inktag_om_pin_defaults(hs,&d);
    for(unsigned i=0;i<12u;++i) {
        if(!(INKTAG_OM_PIN_EDITABLE_MASK&(1u<<i)) && pins->pins[i]!=d.pins[i])return false;
        /* Only EN/LED polarity is stored. Screen signalling remains solely
         * the exact driver/recipe/backend's authority, not a wire snapshot. */
        if(i!=INKTAG_PIN_BUSY && i!=INKTAG_PIN_ENABLE &&
           (i<INKTAG_PIN_LED_RED || i>INKTAG_PIN_LED_BLUE)) {
            bool low=pins->pins[i]!=255u && (d.active_low&(1u<<i));
            if(((pins->active_low>>i)&1u)!=(unsigned)low)return false;
        }
    }
    return true;
}

uint32_t inktag_om_pin_hash(const uint8_t *data,unsigned n) {
#if !defined(INKTAG_OM_FIXED_RECOVERY)
    /* App already links this identical FNV-1a loop. Keep Recovery standalone. */
    return epd_record_checksum(data,n);
#else
    uint32_t h=2166136261u;
    while(n--){h^=*data++;h*=16777619u;}return h;
#endif
}

bool inktag_om_pin_factory_decode(const uint8_t *data,unsigned n,
    bool hs,uint8_t *board,inktag_pin_config_t *pins) {
    if(!data || !board || !pins || n!=28u || memcmp(data,"OMF1",4) || data[4]!=1u ||
       !inktag_om_pin_board_valid(data[5],hs) || data[6]!=1u || data[7] || data[8]!=1u)return false;
    uint32_t hash=(uint32_t)data[24]|((uint32_t)data[25]<<8)|
        ((uint32_t)data[26]<<16)|((uint32_t)data[27]<<24);
    if(hash!=inktag_om_pin_hash(data,24u) ||
       !inktag_pin_config_decode(data+8,16u,pins) || !inktag_om_pin_valid(pins,hs))return false;
    *board=data[5];return true;
}

bool inktag_om_pin_pref_decode(const uint8_t *data,unsigned n,
    bool hs,bool *present,inktag_pin_config_t *pins) {
    if(!data || !present || !pins || n!=INKTAG_OM_PREF_SIZE ||
       memcmp(data,"OMU1",4) || data[4]!=INKTAG_OM_PREF_VERSION)return false;
    uint8_t crc=0;for(unsigned i=0;i+1<n;++i)crc^=data[i];
    const unsigned led=23u;
    if(data[n-1]!=crc || data[led]!=1u ||
       (data[led+1]!=0u && data[led+1]!=1u && data[led+1]!=3u))return false;
    /* Bytes 5..22 are opaque Pro customization; retain them without applying. */
    unsigned tail=led+2u;
    if(data[tail]>1u)return false;
    if(!data[tail]) {
        for(unsigned i=tail+1u;i<n-1u;++i)if(data[i])return false;
        *present=false;return true;
    }
    if(data[tail+1u]!=1u || !inktag_pin_config_decode(data+tail+1u,16u,pins) || !inktag_om_pin_valid(pins,hs))return false;
    *present=true;return true;
}
