#ifndef INKTAG_OM_FEATURES_H
#define INKTAG_OM_FEATURES_H

#include "inktag_om_profile.h"
#include "inktag_activation.h"
#include "inktag_board.h"
#include "inktag_om_board_identity.h"

/* Product behavior lives in firmware/common.  These switches only describe
 * which OM6626 adapters are compiled into the shared exact-board image. */
#define INKTAG_OM_FULL_FEATURES 1
#define INKTAG_OM_ACTIVATION_IMPLEMENTED INKTAG_BUILD_PRO
#define INKTAG_OM_ACTIVATION_BACKEND_AVAILABLE INKTAG_BUILD_PRO
#define INKTAG_OM_ACTIVATION_PLATFORM_ID INKTAG_ACTIVATION_PLATFORM_OM6626
#define INKTAG_OM_LIFECYCLE_IMPLEMENTED 1
#define INKTAG_OM_LIFECYCLE_BACKEND_AVAILABLE 1
#define INKTAG_OM_AUX_IO_BACKEND_AVAILABLE 1
#define INKTAG_OM_FIND_BACKEND_AVAILABLE INKTAG_BUILD_PRO
#define INKTAG_OM_GUI_ASSET_UPDATE_IMPLEMENTED 1
#define INKTAG_OM_GUI_ASSET_UPDATE_BACKEND_AVAILABLE 1
#define INKTAG_OM_TRANSFER_PACKBITS_IMPLEMENTED 1
#define INKTAG_OM_TRANSFER_PACKBITS_BACKEND_AVAILABLE 1
#define INKTAG_OM_TIMED_SLEEP_BACKEND_AVAILABLE 0
#define INKTAG_OM_NFC_PACKAGED (INKTAG_BUILD_PRO && INKTAG_BOARD_HAS_NFC)
#define INKTAG_OM_NFC_IMPLEMENTED (INKTAG_BUILD_PRO && INKTAG_BOARD_HAS_NFC)

#ifndef INKTAG_BOARD_ACTIVATION_EXPERIMENTAL_AVAILABLE
#define INKTAG_BOARD_ACTIVATION_EXPERIMENTAL_AVAILABLE 0
#endif
static inline bool inktag_om_activation_board_available(void) {
    return INKTAG_BOARD_ACTIVATION_QUALIFIED != 0 ||
           INKTAG_BOARD_ACTIVATION_EXPERIMENTAL_AVAILABLE != 0;
}

static inline bool inktag_om_transfer_packbits_qualified(void) {
    switch (inktag_om_board_identity()) {
        case INKTAG_OM_BOARD_ZKC21B:
            return INKTAG_BOARD_ZKC21B_TRANSFER_PACKBITS_QUALIFIED != 0;
        case INKTAG_OM_BOARD_ZKC42B:
            return INKTAG_BOARD_ZKC42B_TRANSFER_PACKBITS_QUALIFIED != 0;
        case INKTAG_OM_BOARD_ZKC21V:
            return INKTAG_BOARD_ZKC21V_TRANSFER_PACKBITS_QUALIFIED != 0;
        default:
            return false;
    }
}

#endif
