#ifndef INKTAG_ACTIVATION_H
#define INKTAG_ACTIVATION_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_protocol_v2.h"

#define INKTAG_ACTIVATION_FORMAT_VERSION 1u
#define INKTAG_ACTIVATION_BODY_BYTES 32u
#define INKTAG_ACTIVATION_TOKEN_BYTES 96u
#define INKTAG_ACTIVATION_RESTORE_MODE 1u
#define INKTAG_ACTIVATION_RESTORE_PAYLOAD_BYTES (1u + INKTAG_ACTIVATION_TOKEN_BYTES)
#define INKTAG_ACTIVATION_SIGNATURE_BYTES 64u
#define INKTAG_ACTIVATION_CODE_CHARS 128u
#define INKTAG_ACTIVATION_ID_SIZE 8u
#define INKTAG_ACTIVATION_CHALLENGE_SIZE 8u
#define INKTAG_ACTIVATION_STATUS_WIRE_SIZE 24u
#define INKTAG_ACTIVATION_REQUEST_WIRE_SIZE 32u
#define INKTAG_ACTIVATION_RAW_UID_MAX_SIZE 32u
#define INKTAG_ACTIVATION_PRODUCT_STANDARD 1u
#define INKTAG_ACTIVATION_FEATURE_ADVANCED 0x00000001u
#define INKTAG_ACTIVATION_PLATFORM_GR551X 0x0101u
#define INKTAG_ACTIVATION_PLATFORM_GR5331 0x0102u
#define INKTAG_ACTIVATION_PLATFORM_TLSR8359 0x0103u
#define INKTAG_ACTIVATION_PLATFORM_OM6626 0x0201u
#define INKTAG_ACTIVATION_PLATFORM_NRF52832 0x0301u
#define INKTAG_ACTIVATION_PLATFORM_NRF52811 0x0302u
#define INKTAG_ACTIVATION_DEFAULT_CHALLENGE_TTL_MS 600000u

typedef enum {
    INKTAG_ACTIVATION_STORAGE_EMPTY = 0,
    INKTAG_ACTIVATION_STORAGE_OK = 1,
    INKTAG_ACTIVATION_STORAGE_ERROR = 2,
} inktag_activation_storage_result_t;

typedef enum {
    INKTAG_ACTIVATION_VERIFY_OK = 0,
    INKTAG_ACTIVATION_VERIFY_INVALID = 1,
    INKTAG_ACTIVATION_VERIFY_KEY_UNAVAILABLE = 2,
    INKTAG_ACTIVATION_VERIFY_INTERNAL_ERROR = 3,
} inktag_activation_verify_result_t;

typedef struct {
    uint16_t platform_id;
    uint16_t product_id;
    uint16_t security_epoch;
    uint32_t feature_mask;
    uint8_t key_id;
    uint8_t device_id[INKTAG_ACTIVATION_ID_SIZE];
} inktag_activation_identity_t;

typedef struct {
    uint8_t token[INKTAG_ACTIVATION_TOKEN_BYTES];
} inktag_activation_record_t;

typedef inktag_activation_storage_result_t (*inktag_activation_read_fn)(
    void *context, inktag_activation_record_t *record);
typedef bool (*inktag_activation_write_fn)(
    void *context, const inktag_activation_record_t *record);
typedef bool (*inktag_activation_clear_fn)(void *context);
typedef inktag_activation_verify_result_t (*inktag_activation_verify_fn)(
    void *context, uint8_t key_id, const uint8_t digest[32],
    const uint8_t signature[INKTAG_ACTIVATION_SIGNATURE_BYTES]);
typedef bool (*inktag_activation_random_fn)(
    void *context, uint8_t *output, uint16_t length);

typedef struct {
    void *context;
    inktag_activation_read_fn read;
    inktag_activation_write_fn write;
    inktag_activation_verify_fn verify;
    inktag_activation_random_fn random;
    /* Optional and used only by INKTAG_ENABLE_ACTIVATION_TEST_API builds. */
    inktag_activation_clear_fn clear;
} inktag_activation_io_t;

typedef struct {
    inktag_activation_identity_t identity;
    inktag_activation_io_t io;
    inktag_activation_result_v2_t last_result;
    uint32_t installed_sequence;
    uint32_t effective_features;
    uint32_t challenge_expires_at_ms;
    uint8_t installed_key_id;
    uint8_t challenge[INKTAG_ACTIVATION_CHALLENGE_SIZE];
    bool supported;
    bool identity_available;
    bool active;
    bool stored_invalid;
    bool challenge_valid;
} inktag_activation_t;

bool inktag_activation_derive_device_id(
    uint16_t platform_id, uint16_t product_id,
    const uint8_t *raw_uid, uint16_t raw_uid_length,
    uint8_t output[INKTAG_ACTIVATION_ID_SIZE]);
bool inktag_activation_init(inktag_activation_t *activation,
                            const inktag_activation_identity_t *identity,
                            const inktag_activation_io_t *io);
inktag_activation_result_v2_t inktag_activation_request(
    inktag_activation_t *activation, uint32_t now_ms, uint32_t ttl_ms,
    bool rotate,
    uint8_t output[INKTAG_ACTIVATION_REQUEST_WIRE_SIZE]);
inktag_activation_result_v2_t inktag_activation_install(
    inktag_activation_t *activation, const uint8_t *token, uint16_t length,
    uint32_t now_ms);
/* SET_ACTIVATION_CODE: normal token96, or mode1 + token96 for explicit
 * same-device restoration into authority-proven empty base storage. The port
 * retains its authenticated-session, power and busy gates. Both modes require
 * an unexpired RAM request; restore does not match the historical signed nonce
 * to that request. It never replaces a present/corrupt entitlement record. */
inktag_activation_result_v2_t inktag_activation_install_payload(
    inktag_activation_t *activation, const uint8_t *payload, uint16_t length,
    uint32_t now_ms);
void inktag_activation_expire(inktag_activation_t *activation, uint32_t now_ms);
void inktag_activation_invalidate_request(inktag_activation_t *activation);
void inktag_activation_encode_status(const inktag_activation_t *activation,
                                     uint8_t output[INKTAG_ACTIVATION_STATUS_WIRE_SIZE]);
bool inktag_activation_is_pro(const inktag_activation_t *activation);
bool inktag_activation_has_feature(const inktag_activation_t *activation,
                                   uint32_t feature_mask);

/*
 * Test-fixture escape hatch.  Production builds always return false.  A test
 * build must also provide io.clear; successful cleanup is acknowledged only
 * after the persistent backend reads back as empty.  Protocol transports must
 * never expose this unless their own test-only gate is enabled.
 */
bool inktag_activation_deactivate_for_test(inktag_activation_t *activation);

#endif
