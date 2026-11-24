#ifndef INKTAG_FINDMY_KEYRING_H
#define INKTAG_FINDMY_KEYRING_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy.h"

#define INKTAG_FINDMY_KEYRING_WIRE_VERSION 1u
#define INKTAG_FINDMY_SCHEDULE_VERSION 1u
#define INKTAG_FINDMY_ROTATION_PERIOD_SECONDS 1800u
#define INKTAG_FINDMY_KEY_COUNT_MIN 20u
#define INKTAG_FINDMY_KEY_COUNT_DEFAULT 40u
#define INKTAG_FINDMY_KEY_COUNT_MAX 100u
#define INKTAG_FINDMY_KEYS_PER_CHUNK_MAX 8u
#define INKTAG_FINDMY_CHUNK_COUNT_MAX 13u
#define INKTAG_FINDMY_POOL_ID_BYTES 8u
#define INKTAG_FINDMY_POOL_DIGEST_BYTES 16u
#define INKTAG_FINDMY_FALLBACK_KEY_INDEX 0xffu

#define INKTAG_FINDMY_KEYRING_BEGIN_WIRE_BYTES 72u
#define INKTAG_FINDMY_KEYRING_CHUNK_HEADER_BYTES 9u
#define INKTAG_FINDMY_KEYRING_CHUNK_TRAILER_BYTES 4u
#define INKTAG_FINDMY_KEYRING_CHUNK_WIRE_BYTES_MAX \
    (INKTAG_FINDMY_KEYRING_CHUNK_HEADER_BYTES + \
     INKTAG_FINDMY_KEYS_PER_CHUNK_MAX * INKTAG_FINDMY_PUBLIC_KEY_BYTES + \
     INKTAG_FINDMY_KEYRING_CHUNK_TRAILER_BYTES)
#define INKTAG_FINDMY_KEYRING_COMMIT_WIRE_BYTES 9u
#define INKTAG_FINDMY_KEYRING_ABORT_WIRE_BYTES 5u
#define INKTAG_FINDMY_KEYRING_BEGIN_RESPONSE_WIRE_BYTES 10u
#define INKTAG_FINDMY_KEYRING_STATUS_WIRE_BYTES 52u

enum {
    INKTAG_FINDMY_KEYRING_STATUS_PRESENT = 0x01u,
    INKTAG_FINDMY_KEYRING_STATUS_STAGING = 0x02u,
    INKTAG_FINDMY_KEYRING_STATUS_TIME_VALID = 0x04u,
    INKTAG_FINDMY_KEYRING_STATUS_RESERVED_RETIRED_MIGRATION = 0x08u,
    INKTAG_FINDMY_KEYRING_STATUS_ENABLED = 0x10u,
};

typedef struct {
    uint8_t wire_version;
    uint8_t schedule_version;
    uint8_t key_count;
    uint8_t chunk_count;
    uint32_t period_seconds;
    uint64_t pool_id;
    uint64_t schedule_salt;
    uint8_t fallback_public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES];
    uint32_t public_keys_crc32;
    uint8_t pool_digest[INKTAG_FINDMY_POOL_DIGEST_BYTES];
} inktag_findmy_keyring_begin_t;

typedef struct {
    uint32_t upload_id;
    uint8_t chunk_index;
    uint8_t chunk_count;
    uint8_t first_key_index;
    uint8_t key_count;
    const uint8_t *public_keys;
    uint32_t public_keys_crc32;
} inktag_findmy_keyring_chunk_t;

typedef struct {
    uint8_t wire_version;
    uint8_t schedule_version;
    uint8_t flags;
    uint8_t key_count;
    uint8_t active_key_index;
    uint8_t max_keys;
    uint16_t received_chunks;
    uint32_t generation;
    uint32_t upload_id;
    uint64_t pool_id;
    uint64_t schedule_salt;
    uint32_t period_seconds;
    uint8_t pool_digest[INKTAG_FINDMY_POOL_DIGEST_BYTES];
} inktag_findmy_keyring_status_t;

bool inktag_findmy_key_count_valid(uint8_t key_count, uint8_t device_max);
bool inktag_findmy_keyring_begin_valid(
    const inktag_findmy_keyring_begin_t *begin, uint8_t device_max);

/* Returns the normal-pool key index for a valid UTC timestamp. When time is
 * invalid, returns INKTAG_FINDMY_FALLBACK_KEY_INDEX without consulting the
 * normal pool. The schedule is byte-identical to the Python and TypeScript
 * implementations and changes only when schedule_version changes. */
uint8_t inktag_findmy_key_index_for_unix(
    uint64_t schedule_salt, uint8_t key_count, uint64_t unix_seconds,
    bool time_valid);

bool inktag_findmy_keyring_decode_begin(
    const uint8_t *payload, uint16_t length, uint8_t device_max,
    inktag_findmy_keyring_begin_t *begin);
bool inktag_findmy_keyring_decode_chunk(
    const uint8_t *payload, uint16_t length,
    inktag_findmy_keyring_chunk_t *chunk);
bool inktag_findmy_keyring_decode_commit(
    const uint8_t *payload, uint16_t length, uint32_t *upload_id,
    uint32_t *public_keys_crc32);
bool inktag_findmy_keyring_decode_abort(
    const uint8_t *payload, uint16_t length, uint32_t *upload_id);
bool inktag_findmy_keyring_encode_begin_response(
    uint32_t upload_id, uint32_t generation, uint8_t max_keys,
    uint8_t response[INKTAG_FINDMY_KEYRING_BEGIN_RESPONSE_WIRE_BYTES]);
bool inktag_findmy_keyring_encode_status(
    const inktag_findmy_keyring_status_t *status,
    uint8_t response[INKTAG_FINDMY_KEYRING_STATUS_WIRE_BYTES]);

#endif
