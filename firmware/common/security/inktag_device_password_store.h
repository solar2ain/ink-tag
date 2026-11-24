#ifndef INKTAG_DEVICE_PASSWORD_STORE_H
#define INKTAG_DEVICE_PASSWORD_STORE_H
#include "inktag_device_password.h"
#include "inktag_settings_journal.h"
#include "inktag_protocol_core.h"

typedef enum {
    INKTAG_PASSWORD_STORE_OK,
    INKTAG_PASSWORD_STORE_MISSING,
    INKTAG_PASSWORD_STORE_ERROR,
    INKTAG_PASSWORD_STORE_CORRUPT,
} inktag_device_password_store_result_t;

/* Adapt an existing record backend, not a new journal or storage region.
 * read returns OK for bounded bytes, MISSING only for proven absence and
 * ERROR/CORRUPT for uncertain IO/invalid vendor envelopes. It must respect
 * capacity. put is atomic old-or-new and does not retain its borrowed buffer.
 * Vendor QR envelopes are validated/normalized by the adapter; common alone
 * interprets the existing plain-QR/password wire. No callback logs secrets. */
typedef struct {
    void *context;
    inktag_device_password_store_result_t (*read)(
        void *context, uint8_t *output, uint16_t capacity, uint16_t *length);
    bool (*put_atomic)(void *context, const uint8_t *wire, uint16_t length);
} inktag_device_password_store_io_t;

bool inktag_device_password_store_record_decode(
    const uint8_t *wire, uint16_t length,
    inktag_device_password_config_t *config, uint8_t *modules,
    uint16_t *matrix_offset);
inktag_device_password_store_result_t inktag_device_password_store_load_io(
    const inktag_device_password_store_io_t *, uint8_t scratch[420],
    inktag_device_password_config_t *, uint8_t *modules,
    uint16_t *matrix_offset, uint16_t *record_length);
bool inktag_device_password_store_commit_io(
    const inktag_device_password_store_io_t *, uint8_t scratch[420],
    uint16_t record_length, inktag_device_password_config_t *actual);

/* Caller lends the existing QR scratch area, at least 420 bytes. Never call
 * during an active upload. The output config is invalid on every read error.
 * Neither config nor scratch is a protocol response or logging argument. */
inktag_device_password_store_result_t inktag_device_password_store_load(
    inktag_settings_journal_t *, uint8_t scratch[420],
    inktag_device_password_config_t *, uint8_t *modules,
    uint16_t *matrix_offset, uint16_t *record_length);

/* Rebuild only the matrix while preserving enabled V/W/revision. Input may
 * overlap output, so the same QR staging area serves both schemas. */
bool inktag_device_password_store_qr(const inktag_device_password_config_t *,
    uint8_t modules, const uint8_t *matrix, uint16_t matrix_length,
    uint8_t output[420], uint16_t *record_length);

/* Fully received/CRC-checked candidate in the existing 480-byte transfer
 * scratch. Product entitlement and fresh storage voltage are caller gates.
 * Validate/consume the exact W-bound candidate and normalize disabled QR in
 * place. EVERY attempt clears the session/grant, even malformed/no-space.
 * No backend mutation occurs here; publish/read back before adopting state. */
inktag_status_v2_t inktag_device_password_store_prepare_bundle(
    inktag_device_password_t *, const inktag_device_password_config_t *,
    uint32_t now_ms, uint8_t scratch[480], uint16_t *record_length);

/* Candidate must already be fully authorized (including a consumed W grant
 * for password changes). One generation switch and exact hash/readback.
 * Always reload actual authority, including false-after-commit failures. */
bool inktag_device_password_store_commit(inktag_settings_journal_t *,
    uint8_t scratch[420], uint16_t record_length,
    inktag_device_password_config_t *actual);
#endif
