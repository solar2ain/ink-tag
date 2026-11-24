#ifndef INKTAG_DEVICE_PASSWORD_H
#define INKTAG_DEVICE_PASSWORD_H

#include <stdbool.h>
#include <stdint.h>
#include "inktag_release_v1.h"

#define INKTAG_DEVICE_PASSWORD_VERSION 1u
#define INKTAG_DEVICE_PASSWORD_CHALLENGE_BYTES 22u
#define INKTAG_DEVICE_PASSWORD_STATUS_BYTES 12u
#define INKTAG_DEVICE_PASSWORD_BUNDLE_VERSION INKTAG_RELEASE_VERSION
#define INKTAG_DEVICE_PASSWORD_PLAIN_HEADER_BYTES INKTAG_RELEASE_PASSWORD_OFF_HEADER_BYTES
#define INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES 73u
#define INKTAG_DEVICE_PASSWORD_BUNDLE_MAX_BYTES 480u
#define INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES 420u
#define INKTAG_DEVICE_PASSWORD_LIFETIME_MS 30000u
#define INKTAG_DEVICE_PASSWORD_BOOT_COOLDOWN_MS 5000u
#define INKTAG_DEVICE_PASSWORD_FIRST_FAILURE_COOLDOWN_MS 5000u
#define INKTAG_DEVICE_PASSWORD_MAX_FAILURE_COOLDOWN_MS 60000u

typedef enum {
    INKTAG_PASSWORD_AUTH = 0u,
    INKTAG_PASSWORD_CHANGE = 1u,
    INKTAG_PASSWORD_DISABLE = 2u,
} inktag_device_password_purpose_t;

/* The port loads this from the one atomic QR record; invalid != disabled.
 * V and W are never response fields. A missing ordinary QR is open mode only
 * when the authoritative journal proves absence, not when its read failed. */
typedef struct {
    uint32_t revision;
    uint8_t credential[32];
    uint8_t change_key[32];
    bool valid;
    bool enabled;
} inktag_device_password_config_t;

/* One active central per product connection. Retain failure/cooldown state
 * across disconnect and vendor sleep. session_clear must NOT reset throttle. */
typedef struct {
    uint8_t nonce[16];
    uint8_t change_digest[32];
    uint32_t challenge_deadline, retry_deadline, issue_deadline, show_deadline;
    uint32_t change_deadline, change_revision, authorized_revision;
    uint8_t failures, purpose, change_purpose;
    bool challenge_open, change_open, authorized;
} inktag_device_password_t;

typedef bool (*inktag_device_password_random_fn)(void *, uint8_t *, uint16_t);

void inktag_device_password_init(inktag_device_password_t *, uint32_t now_ms);
void inktag_device_password_session_clear(inktag_device_password_t *);
bool inktag_device_password_allowed(const inktag_device_password_t *,
                                   const inktag_device_password_config_t *);
/* Apply before vendor dispatch, not just before persistence. Only basic
 * non-secret reads and password entry/recovery-display requests are exempt.
 * Resource/Debug/test/QR reads are intentionally not in this allowlist. */
bool inktag_device_password_command_allowed(const inktag_device_password_t *,
    const inktag_device_password_config_t *, uint8_t opcode);
uint32_t inktag_device_password_retry_after(const inktag_device_password_t *, uint32_t);
bool inktag_device_password_begin(inktag_device_password_t *,
    const inktag_device_password_config_t *, uint32_t now_ms, uint8_t purpose,
    inktag_device_password_random_fn, void *, uint8_t response[22]);
/* AUTH: version,purpose,tag16 (18B), keyed by V.
 * CHANGE/DISABLE: version,purpose,SHA256(exact candidate bundle),tag16 (50B),
 * keyed by W. A successful latter proof authorizes ONE matching commit only,
 * not a new session. Require the normal V session before staging a bundle. */
bool inktag_device_password_authorize(inktag_device_password_t *,
    const inktag_device_password_config_t *, uint32_t now_ms,
    const uint8_t immutable_id[8], const uint8_t *, uint16_t);
bool inktag_device_password_consume_change(inktag_device_password_t *,
    const inktag_device_password_config_t *, uint32_t now_ms, uint8_t purpose,
    const uint8_t actual_candidate_digest[32]);
/* Call before ANY reset-settings write. Reuses DISABLE/W and its one-shot
 * grant bound to SHA256("IKT-RESET-SETTINGS-v1" || 0E 01 05), not a QR/V
 * session or a password-disable bundle. Unknown config is never OFF.
 * No new purpose, session state, runtime hash or persistence framework. */
bool inktag_device_password_consume_settings_reset(inktag_device_password_t *,
    const inktag_device_password_config_t *, uint32_t now_ms);
bool inktag_device_password_show_once(inktag_device_password_t *, uint32_t now_ms);
bool inktag_device_password_status(const inktag_device_password_t *,
    const inktag_device_password_config_t *, uint32_t now_ms, uint8_t response[12]);
/* No allocation or mutation. Enabled records reuse these exact bytes at ID5;
 * disabled candidates become the release-v1 9B OFF QR header + matrix.
 * The caller verifies descriptor CRC, computes the candidate SHA256 itself,
 * consumes the W grant, then atomically commits/readbacks ID5 before changing
 * runtime state. Recheck voltage at actual storage mutations. */
bool inktag_device_password_bundle_valid(const uint8_t *, uint16_t,
    const inktag_device_password_config_t *current);
/* Decode only an enabled ID5 record. A disabled candidate is never stored in
 * this form; the port validates the existing ordinary QR schema separately.
 * Clears output on invalid storage, rather than turning corruption into open mode. */
bool inktag_device_password_record_decode(const uint8_t *, uint16_t,
    inktag_device_password_config_t *);

#endif
