/* Permanent-base compatibility surface. No token verification, storage IO,
 * challenge generation or activation state is compiled into this source.
 * The explicit Pro build selects common_pro/security/inktag_activation.c.
 */
#include "inktag_activation.h"
#include <string.h>

#include "inktag_sha256.h"

/* Ordinary immutable identity, shared with password AUTH; not a Pro grant. */
static const uint8_t k_device_id_domain[] = "INKTAG-DEVICE-ID-V1";
static void put_u16_be(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}
#include "inktag_device_identity.inc"

static bool ordinary_identity_valid(const inktag_activation_identity_t *identity) {
    uint8_t nonzero = 0u;
    if (!identity || !identity->platform_id || !identity->product_id) return false;
    for (unsigned i = 0; i < INKTAG_ACTIVATION_ID_SIZE; ++i)
        nonzero |= identity->device_id[i];
    return nonzero != 0u;
}

bool inktag_activation_init(inktag_activation_t *activation,
    const inktag_activation_identity_t *identity, const inktag_activation_io_t *io) {
    (void)io;
    if (!activation) return false;
    memset(activation, 0, sizeof(*activation));
    /* Password AUTH uses the ordinary device ID even without entitlement IO.
     * Preserve the caller's verified identity, never its Pro grant fields. */
    if (ordinary_identity_valid(identity)) {
        activation->identity.platform_id = identity->platform_id;
        activation->identity.product_id = identity->product_id;
        activation->identity.security_epoch = identity->security_epoch;
        memcpy(activation->identity.device_id, identity->device_id,
               INKTAG_ACTIVATION_ID_SIZE);
        activation->identity_available = true;
    }
    return true;
}

void inktag_activation_invalidate_request(inktag_activation_t *activation) {
    if (!activation) return;
    memset(activation->challenge, 0, sizeof(activation->challenge));
    activation->challenge_valid = false;
    activation->challenge_expires_at_ms = 0;
}

void inktag_activation_expire(inktag_activation_t *activation, uint32_t now) {
    (void)now;
    inktag_activation_invalidate_request(activation);
}

inktag_activation_result_v2_t inktag_activation_request(inktag_activation_t *a,
    uint32_t now, uint32_t ttl, bool rotate,
    uint8_t output[INKTAG_ACTIVATION_REQUEST_WIRE_SIZE]) {
    (void)a; (void)now; (void)ttl; (void)rotate;
    if (output) memset(output, 0, INKTAG_ACTIVATION_REQUEST_WIRE_SIZE);
    return INKTAG_ACTIVATION_RESULT_UNSUPPORTED_FEATURE;
}

inktag_activation_result_v2_t inktag_activation_install(inktag_activation_t *a,
    const uint8_t *token, uint16_t length, uint32_t now) {
    (void)a; (void)token; (void)length; (void)now;
    return INKTAG_ACTIVATION_RESULT_UNSUPPORTED_FEATURE;
}

inktag_activation_result_v2_t inktag_activation_install_payload(inktag_activation_t *a,
    const uint8_t *token, uint16_t length, uint32_t now) {
    return inktag_activation_install(a, token, length, now);
}

void inktag_activation_encode_status(const inktag_activation_t *a,
    uint8_t output[INKTAG_ACTIVATION_STATUS_WIRE_SIZE]) {
    if (!output) return;
    memset(output, 0, INKTAG_ACTIVATION_STATUS_WIRE_SIZE);
    output[0] = 2u;
    /* Existing read-only identity wire surface also serves ordinary AUTH.
     * Scheme, token size, challenge, sequence and entitlement remain absent. */
    if (a && a->identity_available && ordinary_identity_valid(&a->identity)) {
        output[1] = INKTAG_ACTIVATION_STATE_DEFAULT;
        memcpy(output + 16u, a->identity.device_id, INKTAG_ACTIVATION_ID_SIZE);
    }
}

bool inktag_activation_is_pro(const inktag_activation_t *a) {
    (void)a;
    return false;
}

bool inktag_activation_has_feature(const inktag_activation_t *a, uint32_t mask) {
    (void)a; (void)mask;
    return false;
}

bool inktag_activation_deactivate_for_test(inktag_activation_t *a) {
    (void)a;
    return false;
}
