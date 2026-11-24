/* base: ordinary AUTH and safe existing-record reads only. Password
 * creation/change/disable/display management is provided by common_pro. */
#include "inktag_device_password.h"
#include "inktag_sha256.h"
#include "../epd/inktag_protocol_v2.h"
#include <string.h>

static void wipe(void *p, uint16_t n) {
    volatile uint8_t *b = (volatile uint8_t *)p;
    while (n--) *b++ = 0u;
}
static bool nonzero(const uint8_t *b, uint16_t n) {
    uint8_t v = 0u;
    while (n--) v |= *b++;
    return v != 0u;
}
static bool equal(const uint8_t *a, const uint8_t *b, uint16_t n) {
    uint8_t v = 0u;
    while (n--) v |= *a++ ^ *b++;
    return v == 0u;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
           (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static void put32(uint8_t *p, uint32_t v) {
    uint8_t i;
    for (i = 0u; i < 4u; ++i) { p[i] = (uint8_t)v; v >>= 8u; }
}
static uint32_t remaining(uint32_t now, uint32_t deadline, uint32_t maximum) {
    int32_t delta = (int32_t)(deadline - now);
    /* A stale deadline must not become a multi-day cooldown after half-wrap. */
    return delta > 0 && (uint32_t)delta <= maximum ? (uint32_t)delta : 0u;
}
static bool config_valid(const inktag_device_password_config_t *c) {
    return c && c->valid && (!c->enabled ||
        (c->revision && nonzero(c->credential, 32u) && nonzero(c->change_key, 32u)));
}
void inktag_device_password_session_clear(inktag_device_password_t *s) {
    if (!s) return;
    wipe(s->nonce, 16u); wipe(s->change_digest, 32u);
    s->challenge_open = s->change_open = s->authorized = false;
}
void inktag_device_password_init(inktag_device_password_t *s, uint32_t now) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->retry_deadline = now + INKTAG_DEVICE_PASSWORD_BOOT_COOLDOWN_MS;
    s->issue_deadline = s->show_deadline = now;
}
bool inktag_device_password_allowed(const inktag_device_password_t *s,
                                    const inktag_device_password_config_t *c) {
    return s && config_valid(c) && (!c->enabled ||
        (s->authorized && s->authorized_revision == c->revision));
}
bool inktag_device_password_command_allowed(const inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint8_t opcode) {
    switch (opcode) {
    case INKTAG_CMD_HELLO:
    case INKTAG_CMD_GET_CAPABILITIES:
    case INKTAG_CMD_GET_DEVICE_INFO:
    case INKTAG_CMD_GET_STATUS:
    case INKTAG_CMD_GET_TIME:
    case INKTAG_CMD_GET_SCREEN_PROFILE:
    case INKTAG_CMD_GET_ACTIVATION_STATUS:
    case INKTAG_CMD_GET_SECURITY_INFO:
    case INKTAG_CMD_GET_DEVICE_PASSWORD_STATUS:
    case INKTAG_CMD_GET_DEVICE_PASSWORD_CHALLENGE:
    case INKTAG_CMD_AUTHORIZE_DEVICE_PASSWORD:
    case INKTAG_CMD_SHOW_PASSWORD_DEVICE_INFO:
        return true;
    default:
        return inktag_device_password_allowed(s, c);
    }
}
uint32_t inktag_device_password_retry_after(const inktag_device_password_t *s, uint32_t now) {
    return s ? remaining(now, s->retry_deadline,
                        INKTAG_DEVICE_PASSWORD_MAX_FAILURE_COOLDOWN_MS) : 0u;
}
bool inktag_device_password_begin(inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint32_t now, uint8_t purpose,
    inktag_device_password_random_fn random, void *context, uint8_t response[22]) {
    if (!s || !config_valid(c) || !c->enabled || purpose != INKTAG_PASSWORD_AUTH ||
        !random || !response || inktag_device_password_retry_after(s, now) ||
        remaining(now, s->issue_deadline, 1000u)) return false;
    /* A rate-limited new challenge invalidates every previous proof/grant. */
    wipe(s->nonce, 16u); wipe(s->change_digest, 32u);
    s->challenge_open = s->change_open = false;
    /* A fresh logical client must prove V on a retained physical BLE link. */
    s->authorized = false;
    s->issue_deadline = now + 1000u;
    if (!random(context, s->nonce, 16u) || !nonzero(s->nonce, 16u)) {
        wipe(s->nonce, 16u); return false;
    }
    s->purpose = purpose;
    s->challenge_deadline = now + INKTAG_DEVICE_PASSWORD_LIFETIME_MS;
    s->challenge_open = true;
    response[0] = INKTAG_DEVICE_PASSWORD_VERSION; response[1] = purpose;
    memcpy(response + 2u, s->nonce, 16u); put32(response + 18u, c->revision);
    return true;
}
static void failure(inktag_device_password_t *s, uint32_t now) {
    if (s->failures != 255u) ++s->failures;
    if (s->failures < 5u) return;
    uint8_t step = s->failures - 5u;
    /* Bound the shift before evaluating it; saturated failure counts cannot
     * overflow or wrap a long cooldown into an unlocked state. */
    s->retry_deadline = now + (step < 4u ?
        INKTAG_DEVICE_PASSWORD_FIRST_FAILURE_COOLDOWN_MS << step :
        INKTAG_DEVICE_PASSWORD_MAX_FAILURE_COOLDOWN_MS);
}
static void tag(const uint8_t key[32], const uint8_t id[8], uint32_t revision,
    const uint8_t nonce[16], uint8_t output[32]) {
    static const uint8_t domain[] = "IKT-DEVICE-PASSWORD-PROOF-v1";
    uint8_t input[64u + sizeof(domain) - 1u + 8u + 4u + 16u + 1u];
    uint16_t offset = 64u;
    memcpy(input + offset, domain, sizeof(domain) - 1u); offset += sizeof(domain) - 1u;
    memcpy(input + offset, id, 8u); offset += 8u;
    put32(input + offset, revision); offset += 4u;
    memcpy(input + offset, nonce, 16u); offset += 16u;
    input[offset++] = INKTAG_PASSWORD_AUTH;
    inktag_hmac_sha256_32_inplace(key, input, offset - 64u, output);
    wipe(input, sizeof(input));
}
bool inktag_device_password_authorize(inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint32_t now, const uint8_t id[8],
    const uint8_t *request, uint16_t length) {
    uint8_t expected[32];
    bool ok;
    uint8_t purpose;
    if (!s) return false;
    if (inktag_device_password_retry_after(s, now)) {
        s->challenge_open = false; wipe(s->nonce, 16u); return false;
    }
    purpose = s->purpose;
    ok = config_valid(c) && c->enabled && id && nonzero(id, 8u) && request &&
         s->challenge_open && remaining(now, s->challenge_deadline, INKTAG_DEVICE_PASSWORD_LIFETIME_MS) &&
         purpose == INKTAG_PASSWORD_AUTH &&
         length == 18u && request[0] == 1u;
    /* Check length before examining any fields in a short untrusted request. */
    if (ok) ok = request[1] == INKTAG_PASSWORD_AUTH;
    s->challenge_open = s->change_open = false;
    wipe(s->change_digest, 32u);
    if (ok) {
        tag(c->credential, id, c->revision, s->nonce, expected);
        ok = equal(expected, request + 2u, 16u);
    }
    wipe(expected, sizeof(expected)); wipe(s->nonce, 16u);
    if (!ok) { failure(s, now); return false; }
    s->failures = 0u; s->retry_deadline = now;
    s->authorized = true; s->authorized_revision = c->revision;
    return true;
}
bool inktag_device_password_consume_change(inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint32_t now, uint8_t purpose,
    const uint8_t digest[32]) {
    (void)s; (void)c; (void)now; (void)purpose; (void)digest;
    return false;
}
bool inktag_device_password_consume_settings_reset(inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint32_t now) {
    (void)s; (void)now;
    return config_valid(c) && !c->enabled;
}
bool inktag_device_password_show_once(inktag_device_password_t *s, uint32_t now) {
    (void)s; (void)now;
    return false;
}
bool inktag_device_password_status(const inktag_device_password_t *s,
    const inktag_device_password_config_t *c, uint32_t now, uint8_t response[12]) {
    if (!s || !config_valid(c) || !response) return false;
    response[0] = 1u; response[1] = c->enabled ? 1u : 0u;
    response[2] = c->enabled && inktag_device_password_allowed(s, c) ? 1u : 0u; response[3] = s->failures;
    put32(response + 4u, c->enabled ? inktag_device_password_retry_after(s, now) : 0u);
    put32(response + 8u, c->enabled ? c->revision : 0u);
    return true;
}
/* Record reads and proposed updates have the same structural rules. Keep the
 * revision transition separate: loading must not invent a previous authority
 * or copy both secret keys merely to validate the stored bytes. */
static bool bundle_shape_valid(const uint8_t *b, uint16_t length) {
    uint16_t matrix;
    uint8_t modules, padding;
    if (!b || length < 73u || length > 480u ||
        b[0] != INKTAG_DEVICE_PASSWORD_BUNDLE_VERSION || b[1] > 1u) return false;
    modules = b[2]; matrix = (uint16_t)b[3] | (uint16_t)b[4] << 8u;
    if (modules < 21u || modules > 57u || (modules - 21u) % 4u ||
        matrix != (modules * modules + 7u) / 8u || length != 73u + matrix ||
        !le32(b + 5u)) return false;
    if (b[1]) {
        if (length > INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES ||
            !nonzero(b + 9u, 32u) || !nonzero(b + 41u, 32u) ||
            equal(b + 9u, b + 41u, 32u)) return false;
    } else if (nonzero(b + 9u, 64u) || INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES + matrix > INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES)
        return false;
    padding = (uint8_t)(matrix * 8u - modules * modules);
    return !padding || !(b[length - 1u] & ((1u << padding) - 1u));
}

bool inktag_device_password_bundle_valid(const uint8_t *b, uint16_t length,
    const inktag_device_password_config_t *c) {
    (void)b; (void)length; (void)c;
    return false;
}

bool inktag_device_password_record_decode(const uint8_t *b, uint16_t length,
    inktag_device_password_config_t *out) {
    if (!out) return false;
    wipe(out, sizeof(*out));
    if (!bundle_shape_valid(b, length) || b[1] != 1u)
        return false;
    uint32_t revision = le32(b + 5u);
    out->valid = out->enabled = true;
    out->revision = revision;
    memcpy(out->credential, b + 9u, 32u);
    memcpy(out->change_key, b + 41u, 32u);
    return true;
}
