/* Fixed-offset bytes inside the publisher-signed App body. The same source
 * also binds the native Recovery link. Not an unsigned manifest authority. */
#include "profile.h"
#include <stdint.h>
#define LE32(v) (uint8_t)(v),(uint8_t)((v)>>8),(uint8_t)((v)>>16),(uint8_t)((v)>>24)
__attribute__((used,section(".inktag_boot_binding")))
const uint8_t inktag_tlsr_boot_binding[20] = {
    'I','A','B','1', LE32(INKTAG_APPLICATION_START), LE32(INKTAG_APPLICATION_SIZE),
    LE32(INKTAG_RECOVERY_START), LE32(INKTAG_RECOVERY_SIZE)
};
#undef LE32
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
bool inktag_tlsr_boot_binding_matches(const uint8_t *p) {
    return p[0]=='I'&&p[1]=='A'&&p[2]=='B'&&p[3]=='1'&&
        word(p+4)==INKTAG_APPLICATION_START && word(p+8)==INKTAG_APPLICATION_SIZE &&
        word(p+12)==INKTAG_RECOVERY_START && word(p+16)==INKTAG_RECOVERY_SIZE;
}
