#if INKTAG_BUILD_PRO
#include "inktag_tlsr_code_guard_pro.inc"
#else
/* ELF-only build identity. No persistent entitlement or private execution. */
__asm__(".global inktag_tlsr_source_profile\n.set inktag_tlsr_source_profile,0\n");
#ifdef INKTAG_CODE_RECOVERY
__asm__(".global inktag_tlsr_recovery_integrity_policy\n.set inktag_tlsr_recovery_integrity_policy,2\n");
#endif
#endif
