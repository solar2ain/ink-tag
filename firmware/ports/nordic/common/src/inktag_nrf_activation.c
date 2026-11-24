#include "inktag_nrf_build_profile.h"
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 1
#include "inktag_nrf_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
#include "inktag_nrf_activation.h"
#include <stddef.h>
#include "nrf_error.h"
#include "nrf_soc.h"
#include "nrf.h"
#include <string.h>
#endif
bool inktag_nrf_random_bytes(void *context, uint8_t *output, uint16_t length) {
    uint8_t available = 0u;
    (void)context;
    if (output == NULL || length == 0u || length > UINT8_MAX ||
        sd_rand_application_bytes_available_get(&available) != NRF_SUCCESS ||
        available < length) return false;
    return sd_rand_application_vector_get(output, (uint8_t)length) == NRF_SUCCESS;
}
#if INKTAG_BUILD_PRO
#define INKTAG_NRF_PRO_SECTION 2
#include "inktag_nrf_activation_pro.inc"
#undef INKTAG_NRF_PRO_SECTION
#else
bool inktag_nrf_activation_init(inktag_activation_t *a) {
    inktag_activation_identity_t identity;
    uint8_t uid[8];
    uint32_t word0 = NRF_FICR->DEVICEID[0], word1 = NRF_FICR->DEVICEID[1];
    memcpy(uid, &word0, sizeof(word0));
    memcpy(uid + 4u, &word1, sizeof(word1));
    memset(&identity, 0, sizeof(identity));
    identity.platform_id = INKTAG_ACTIVATION_PLATFORM_NRF52832;
    identity.product_id = INKTAG_ACTIVATION_PRODUCT_STANDARD;
    if (!inktag_activation_derive_device_id(identity.platform_id,
            identity.product_id, uid, sizeof(uid), identity.device_id))
        return inktag_activation_init(a, NULL, NULL);
    return inktag_activation_init(a, &identity, NULL);
}
#endif
