#include "inktag_tlsr_build_profile.h"
#if INKTAG_BUILD_PRO
#include "inktag_tlsr_activation_pro.inc"
#else
#include "inktag_tlsr_activation.h"
#include <string.h>

/* Ordinary hardware identity is shared with password AUTH. No entitlement
 * record, grant, signature verifier or activation IO callback is consulted. */
bool inktag_tlsr_activation_init(inktag_activation_t *activation,
    const inktag_tlsr_activation_config_t *config) {
    uint8_t uid[INKTAG_ACTIVATION_RAW_UID_MAX_SIZE];
    uint16_t length = 0u;
    inktag_activation_identity_t identity;
    memset(&identity, 0, sizeof(identity));
    memset(uid, 0, sizeof(uid));
    if (config && config->read_uid &&
        config->read_uid(config->context, uid, sizeof(uid), &length)) {
        identity.platform_id = config->platform_id;
        identity.product_id = config->product_id;
        identity.security_epoch = config->security_epoch;
        if (!inktag_activation_derive_device_id(identity.platform_id,
                identity.product_id, uid, length, identity.device_id))
            memset(&identity, 0, sizeof(identity));
    }
    memset(uid, 0, sizeof(uid));
    return inktag_activation_init(activation, &identity, NULL);
}
#endif
