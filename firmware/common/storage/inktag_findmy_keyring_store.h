#ifndef INKTAG_FINDMY_KEYRING_STORE_H
#define INKTAG_FINDMY_KEYRING_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy_keyring.h"
#include "inktag_settings_journal.h"

typedef enum {
    INKTAG_FINDMY_STORE_OK = 0,
    INKTAG_FINDMY_STORE_INVALID_ARGUMENT,
    INKTAG_FINDMY_STORE_INVALID_STATE,
    INKTAG_FINDMY_STORE_NOT_FOUND,
    INKTAG_FINDMY_STORE_CHECKSUM_ERROR,
    INKTAG_FINDMY_STORE_IO_ERROR,
} inktag_findmy_store_result_t;

typedef struct {
    uint32_t public_keys_crc32;
    uint8_t key_count;
    uint8_t chunk_count;
} inktag_findmy_keyring_staging_t;

typedef struct {
    inktag_settings_journal_t *journal;
    inktag_settings_journal_stage_t stage;
    inktag_findmy_keyring_begin_t active;
    inktag_findmy_keyring_staging_t staging;
    uint16_t received_chunks;
    uint32_t staging_upload_id;
    bool active_present;
    bool staging_present;
} inktag_findmy_keyring_store_t;

bool inktag_findmy_keyring_store_init(
    inktag_findmy_keyring_store_t *store,
    inktag_settings_journal_t *journal);
inktag_findmy_store_result_t inktag_findmy_keyring_store_begin(
    inktag_findmy_keyring_store_t *store,
    const inktag_findmy_keyring_begin_t *begin,
    uint32_t upload_id, uint32_t *generation);
inktag_findmy_store_result_t inktag_findmy_keyring_store_write_chunk(
    inktag_findmy_keyring_store_t *store,
    const inktag_findmy_keyring_chunk_t *chunk);
inktag_findmy_store_result_t inktag_findmy_keyring_store_commit(
    inktag_findmy_keyring_store_t *store, uint32_t upload_id,
    uint32_t public_keys_crc32);
inktag_findmy_store_result_t inktag_findmy_keyring_store_abort(
    inktag_findmy_keyring_store_t *store, uint32_t upload_id);
inktag_findmy_store_result_t inktag_findmy_keyring_store_clear(
    inktag_findmy_keyring_store_t *store);

bool inktag_findmy_keyring_store_status(
    const inktag_findmy_keyring_store_t *store,
    inktag_findmy_keyring_status_t *status);
bool inktag_findmy_keyring_store_read_key(
    inktag_findmy_keyring_store_t *store, uint8_t key_index,
    uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);
bool inktag_findmy_keyring_store_read_fallback(
    const inktag_findmy_keyring_store_t *store,
    uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);

#endif
