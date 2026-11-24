#ifndef INKTAG_TLSR_BUILD_PROFILE_H
#define INKTAG_TLSR_BUILD_PROFILE_H
#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#if INKTAG_BUILD_PRO != 0 && INKTAG_BUILD_PRO != 1
#error "INKTAG_BUILD_PRO must be 0 or 1"
#endif
#ifndef INKTAG_AUX_RESERVED_ENABLED
#define INKTAG_AUX_RESERVED_ENABLED 0
#endif
#if INKTAG_AUX_RESERVED_ENABLED != 0
#error "TLSR reserved AUX output is not enabled"
#endif
#endif
