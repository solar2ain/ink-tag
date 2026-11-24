/* Compile-time composition. base never opens the Pro implementation. */
#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#if INKTAG_BUILD_PRO
#include "inktag_gr_activation_pro.inc"
#else
#include "inktag_gr_activation.h"
#include "inktag_gr_features.h"
#include "grx_hal.h"
#include "grx_sys.h"
#include <stddef.h>
#include <string.h>
#include "inktag_gr_security_random.inc"
bool inktag_gr_activation_init(inktag_activation_t *activation) {
    if (!activation) return false;
    /* Ordinary immutable identity also binds existing password AUTH. Its
     * shared formula grants no feature and never reads activation storage. */
    uint8_t uid[SYS_CHIP_UID_LEN] = {0};
    inktag_activation_identity_t identity = {0};
    identity.platform_id = INKTAG_GR_ACTIVATION_PLATFORM_ID;
    identity.product_id = INKTAG_ACTIVATION_PRODUCT_STANDARD;
    bool available = sys_device_uid_get(uid) == SDK_SUCCESS &&
        inktag_activation_derive_device_id(identity.platform_id, identity.product_id,
                                         uid, sizeof(uid), identity.device_id);
    memset(uid, 0, sizeof(uid));
    return inktag_activation_init(activation, available ? &identity : NULL, NULL);
}
#endif
