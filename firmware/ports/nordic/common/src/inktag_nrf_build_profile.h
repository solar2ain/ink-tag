#ifndef INKTAG_NRF_BUILD_PROFILE_H
#define INKTAG_NRF_BUILD_PROFILE_H
/* Native entries always pass 0/1; legacy host carriers use the complete profile. */
#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#ifndef INKTAG_AUX_RESERVED_ENABLED
#define INKTAG_AUX_RESERVED_ENABLED 0
#endif
#if INKTAG_AUX_RESERVED_ENABLED != 0
#error "Nordic reserved AUX backend is disabled"
#endif
#if INKTAG_BUILD_PRO != 0 && INKTAG_BUILD_PRO != 1
#error "INKTAG_BUILD_PRO must be 0 or 1"
#endif
#if !INKTAG_BUILD_PRO
#if (defined(INKTAG_NRF_ACTIVATION_ENABLED) && INKTAG_NRF_ACTIVATION_ENABLED) || (defined(INKTAG_NRF_FIND_ENABLED) && INKTAG_NRF_FIND_ENABLED) || (defined(INKTAG_NRF_CLOCK_PARTIAL_400X300) && INKTAG_NRF_CLOCK_PARTIAL_400X300)
#error "Base source profile cannot enable private Nordic implementations"
#endif
#endif
#endif
