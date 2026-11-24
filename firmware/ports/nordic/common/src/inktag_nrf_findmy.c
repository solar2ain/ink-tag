#include "inktag_nrf_build_profile.h"
#if INKTAG_BUILD_PRO
#include "inktag_nrf_findmy_pro.c"
#else
#include "inktag_nrf_findmy.h"
#include <string.h>
bool inktag_nrf_findmy_public_safe(bool first){(void)first;return false;}
inktag_find_public_store_result_t inktag_nrf_findmy_public_state(void){return INKTAG_FIND_PUBLIC_STORE_EMPTY;}
bool inktag_nrf_findmy_public_material(uint8_t f[16],uint8_t k[16]){(void)f;(void)k;return false;}
bool inktag_nrf_findmy_select(uint64_t t,bool v,uint64_t *s,uint8_t x[28]){(void)t;(void)v;(void)s;(void)x;return false;}
bool inktag_nrf_findmy_public_install(const uint8_t o[16],const uint8_t m[56],const uint8_t k[16],uint64_t t,uint64_t *s,uint8_t x[28],uint8_t f[16],bool(*a)(void*),void *c){(void)o;(void)m;(void)k;(void)t;(void)s;(void)x;(void)f;(void)a;(void)c;return false;}
void inktag_nrf_findmy_init(inktag_findmy_config_t *c){if(c)memset(c,0,sizeof(*c));}
bool inktag_nrf_findmy_write(const inktag_findmy_config_t *c){(void)c;return false;}
bool inktag_nrf_findmy_clear(void){return false;}
#endif
