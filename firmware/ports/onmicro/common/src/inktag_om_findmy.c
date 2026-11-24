/* base Find interface: no protected storage or positive derivation. */
#include "inktag_om_findmy.h"
#include <string.h>
void inktag_om_findmy_init(bool *enabled) {if(enabled)*enabled=false;}
bool inktag_om_findmy_write(bool enabled) {(void)enabled;return false;}
bool inktag_om_findmy_clear(bool *enabled) {(void)enabled;return false;}
inktag_om_find_public_state_t inktag_om_findmy_public_state(void) {return INKTAG_OM_FIND_PUBLIC_CORRUPT;}
bool inktag_om_findmy_identity_present(void) {return false;}
bool inktag_om_findmy_owner_nonce(uint8_t n[INKTAG_FINDMY_PUBLIC_NONCE_BYTES]) {(void)n;return false;}
bool inktag_om_findmy_public_fingerprint_read(uint8_t f[16]) {(void)f;return false;}
bool inktag_om_findmy_owner_stage_replace(inktag_findmy_public_owner_stage_t *s,const uint8_t p[INKTAG_FINDMY_PUBLIC_REPLACE_STAGE_BYTES],const uint8_t a[8]) {(void)s;(void)p;(void)a;return false;}
bool inktag_om_findmy_public_provision(const uint8_t p[56],const uint8_t k[16],uint64_t t,uint64_t *s,uint8_t x[28],uint8_t f[16]) {(void)p;(void)k;(void)t;(void)s;(void)x;(void)f;return false;}
bool inktag_om_findmy_public_replace(const uint8_t o[16],const uint8_t p[56],const uint8_t k[16],uint64_t t,uint64_t *s,uint8_t x[28],uint8_t f[16]) {(void)o;(void)p;(void)k;(void)t;(void)s;(void)x;(void)f;return false;}
bool inktag_om_findmy_select_identity(uint64_t t,bool v,uint8_t *s,uint8_t p[28]) {(void)t;(void)v;if(s)*s=INKTAG_OM_FINDMY_INVALID_SLOT_TOKEN;if(p)memset(p,0,28);return false;}
