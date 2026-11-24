#ifndef INKTAG_OM_PROFILE_H
#define INKTAG_OM_PROFILE_H
#ifndef INKTAG_AUX_RESERVED_ENABLED
#define INKTAG_AUX_RESERVED_ENABLED 0
#elif INKTAG_AUX_RESERVED_ENABLED != 0
#error OM delivery currently requires INKTAG_AUX_RESERVED_ENABLED=0
#endif
/* This Basic source tree supports only the maintained base profile. */
#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 0
#endif
#if INKTAG_BUILD_PRO != 0
#error This Basic source tree requires INKTAG_BUILD_PRO=0
#endif
#endif
