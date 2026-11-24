#ifndef INKTAG_CODE_GUARD_H
#define INKTAG_CODE_GUARD_H

/* base ABI attributes only. Private builds select their separate provider. */
#ifndef INKTAG_CODE_GUARDS
#define INKTAG_CODE_GUARDS 0
#endif
#if INKTAG_CODE_GUARDS
#error "Private protection is not part of the base source profile"
#endif
#define INKTAG_GUARD_DATA
#define INKTAG_GUARD_A
#define INKTAG_GUARD_B
#define INKTAG_GUARD_WRAPPER_B
#define INKTAG_GUARD_C

#endif
