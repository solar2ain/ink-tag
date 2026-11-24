/* Existing NVDS QR tag/envelope only. Common owns V/W and QR wire semantics. */
#include "inktag_om_device_settings.h"
#include "inktag_device_password_store.h"
#include "inktag_om_nvds.h"
#include "nvds.h"
#define INKTAG_DEVICE_INFO_QR_ASSET_DEFINITION 1
#include "inktag_gui_device_qr.h"
#include <stddef.h>
#include <string.h>

#define QR_TAG ((nvds_tag_id_t)(NVDS_TAG_USER_BEGIN + 6))
#define QR_MAGIC 0x3152514Fu /* OQR1 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version, slot, modules, reserved;
    uint16_t length;
    uint32_t crc32;
    uint8_t data[INKTAG_DEVICE_PASSWORD_RECORD_MAX_BYTES];
} qr_envelope_t;
typedef struct {
    bool active, bundle;
    uint8_t slot, modules;
    uint16_t length, received;
    uint32_t crc32;
    uint8_t data[INKTAG_DEVICE_PASSWORD_BUNDLE_MAX_BYTES];
} qr_transaction_t;
static qr_transaction_t s_qr_transaction;
_Static_assert(offsetof(qr_envelope_t, data) == 14u, "preserve OM QR envelope");
_Static_assert(sizeof(qr_envelope_t) <= 504u, "NVDS atomic record bound");
static void qr_zero(void *buffer, size_t n) {
    volatile uint8_t *p = buffer;
    while (n--) *p++ = 0u;
}
static uint32_t qr_crc(const uint8_t *p, uint16_t n) {
    return inktag_crc32_update(0xffffffffu, p, n) ^ 0xffffffffu;
}
static bool qr_shape(uint8_t slot, uint8_t modules, uint16_t n) {
    const inktag_transfer_descriptor_t d = {
        .object_type = INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR,
        .format = modules, .black_mask = slot, .segment_count = 1u,
        .total_size = n, .segment_size = {n, 0u},
    };
    return inktag_device_info_qr_transfer_valid(&d);
}
static inktag_device_password_store_result_t qr_read_wire(
    void *context, uint8_t *out, uint16_t capacity, uint16_t *n) {
    (void)context;
    qr_envelope_t e;
    memset(&e, 0, sizeof(e));
    nvds_tag_len_t bytes = sizeof(e);
    *n = 0u;
    uint8_t result = nvds_get(QR_TAG, &bytes, &e);
    /* Cold NVDS filtering also hides damaged committed records. Tag-not-found
     * is not proven absence; never turn unknown password authority into OFF. */
    if (result != NVDS_OK) {
        qr_zero(&e,sizeof(e));return INKTAG_PASSWORD_STORE_ERROR;
    }
    const uint16_t header = offsetof(qr_envelope_t, data);
    if (bytes < header || e.magic != QR_MAGIC || e.version != 1u ||
        e.slot != 0u || e.modules != 0u || e.reserved || !e.length ||
        e.length > sizeof(e.data) || bytes != header + e.length ||
        e.length > capacity || qr_crc(e.data, e.length) != e.crc32) {
        qr_zero(&e, sizeof(e));
        return INKTAG_PASSWORD_STORE_CORRUPT;
    }
    memcpy(out, e.data, e.length);
    *n = e.length;
    qr_zero(&e, sizeof(e));
    return INKTAG_PASSWORD_STORE_OK;
}
static bool qr_put_wire(void *context, const uint8_t *wire, uint16_t n) {
    (void)context;
    inktag_device_password_config_t parsed;
    uint8_t modules;
    uint16_t offset;
    if (!inktag_device_password_store_record_decode(wire, n, &parsed,
                                                    &modules, &offset)) return false;
    qr_envelope_t e;
    memset(&e, 0, sizeof(e));
    e.magic = QR_MAGIC;
    e.version = 1u;
    e.length = n;
    memcpy(e.data, wire, e.length);
    e.crc32 = qr_crc(e.data, e.length);
    bool ok = inktag_om_nvds_put(QR_TAG,
        (nvds_tag_len_t)(offsetof(qr_envelope_t, data) + e.length), &e) == NVDS_OK;
    qr_zero(&parsed, sizeof(parsed)); qr_zero(&e, sizeof(e));
    return ok;
}
static const inktag_device_password_store_io_t qr_io = {
    NULL, qr_read_wire, qr_put_wire,
};
bool inktag_om_password_load(inktag_device_password_config_t *config) {
    uint8_t modules;
    uint16_t offset, n;
    if (!config) return false;
    if (s_qr_transaction.active) { memset(config, 0, sizeof(*config)); return false; }
    inktag_device_password_store_result_t r = inktag_device_password_store_load_io(
        &qr_io, s_qr_transaction.data, config, &modules, &offset, &n);
    qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
    return r == INKTAG_PASSWORD_STORE_OK || r == INKTAG_PASSWORD_STORE_MISSING;
}
bool inktag_om_device_info_qr_read(uint8_t slot, inktag_om_device_info_qr_t *qr) {
    inktag_device_password_config_t config;
    uint8_t modules;
    uint16_t offset, n;
    if (!qr || slot != 0u || s_qr_transaction.active) return false;
    inktag_device_password_store_result_t r = inktag_device_password_store_load_io(
        &qr_io, s_qr_transaction.data, &config, &modules, &offset, &n);
    bool ok = r == INKTAG_PASSWORD_STORE_OK &&
        (uint16_t)(n - offset) <= sizeof(qr->data);
    if (ok) {
        qr->modules = modules; qr->length = n - offset;
        memcpy(qr->data, s_qr_transaction.data + offset, qr->length);
    }
    qr_zero(&config, sizeof(config));
    qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
    return ok;
}
static bool qr_begin(bool bundle, uint8_t slot, uint8_t modules,
                     uint16_t n, uint32_t crc, const char **error) {
    if (s_qr_transaction.active) { if (error) *error = "busy"; return false; }
    inktag_om_device_info_qr_abort();
    s_qr_transaction.active = true; s_qr_transaction.bundle = bundle;
    s_qr_transaction.slot = slot; s_qr_transaction.modules = modules;
    s_qr_transaction.length = n; s_qr_transaction.crc32 = crc;
    if (error) *error = NULL;
    return true;
}
bool inktag_om_device_info_qr_begin(uint8_t slot, uint8_t modules, uint16_t n,
                                   uint32_t crc, const char **error) {
    if (!qr_shape(slot, modules, n)) { if (error) *error = "shape"; return false; }
    return qr_begin(false, slot, modules, n, crc, error);
}
bool inktag_om_password_bundle_begin(const inktag_transfer_descriptor_t *d) {
    (void)d; return false;
}
bool inktag_om_device_info_qr_write(uint8_t slot, uint16_t offset,
    const uint8_t *data, uint16_t n, const char **error) {
    if (!s_qr_transaction.active || slot != s_qr_transaction.slot || !data || !n ||
        offset != s_qr_transaction.received ||
        (uint32_t)offset + n > s_qr_transaction.length) {
        if (error) *error = "offset";
        return false;
    }
    memcpy(s_qr_transaction.data + offset, data, n);
    s_qr_transaction.received += n;
    if (error) *error = NULL;
    return true;
}
static bool qr_complete(void) {
    return s_qr_transaction.active &&
        s_qr_transaction.length == s_qr_transaction.received &&
        qr_crc(s_qr_transaction.data, s_qr_transaction.length) == s_qr_transaction.crc32;
}
bool inktag_om_device_info_qr_commit(uint8_t slot,
    inktag_device_password_config_t *config, const char **error) {
    if (!config || !config->valid || config->enabled) { if(error)*error="unsupported"; return false; }
    uint16_t n;
    if (slot || s_qr_transaction.bundle || !qr_complete() ||
        !inktag_device_password_store_qr(config, s_qr_transaction.modules,
            s_qr_transaction.data, s_qr_transaction.length, s_qr_transaction.data, &n)) {
        if (error) *error = "shape";
        return false;
    }
    bool ok = inktag_device_password_store_commit_io(&qr_io,
        s_qr_transaction.data, n, config);
    inktag_om_device_info_qr_abort();
    if (error) *error = ok ? NULL : "flash";
    return ok;
}
inktag_status_v2_t inktag_om_password_bundle_commit(
    inktag_device_password_t *session, inktag_device_password_config_t *config,
    uint32_t now_ms) {
    (void)session; (void)config; (void)now_ms; return INKTAG_STATUS_UNSUPPORTED;
}
void inktag_om_device_info_qr_abort(void) {
    qr_zero(&s_qr_transaction, sizeof(s_qr_transaction));
}
static bool qr_content_needs_no_reset(void) {
    inktag_device_password_config_t config;
    uint8_t modules;
    uint16_t offset, length;
    bool ok = inktag_device_password_store_load_io(&qr_io,
        s_qr_transaction.data, &config, &modules, &offset, &length) ==
        INKTAG_PASSWORD_STORE_OK;
    if (ok && !config.enabled)
        ok = modules == INKTAG_DEFAULT_DEVICE_QR_MODULES &&
            (uint16_t)(length - offset) == sizeof(inktag_default_device_qr) &&
            memcmp(s_qr_transaction.data + offset, inktag_default_device_qr,
                   sizeof(inktag_default_device_qr)) == 0;
    qr_zero(&config, sizeof(config));
    qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
    return ok;
}
static bool password_settings_reset_verified(void) {
    inktag_device_password_config_t config;
    bool valid = inktag_om_password_load(&config) &&
        config.valid && !config.enabled &&
        qr_content_needs_no_reset();
    qr_zero(&config, sizeof(config));
    return valid;
}
static bool password_settings_reset_commit(void) {
    inktag_device_password_config_t before, after;
    uint8_t modules;
    uint16_t offset, length, wire_length;
    inktag_om_device_info_qr_abort();
    if (inktag_device_password_store_load_io(&qr_io, s_qr_transaction.data,
            &before, &modules, &offset, &length) != INKTAG_PASSWORD_STORE_OK) {
        qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
        qr_zero(&before, sizeof(before));
        return false;
    }
    if (before.enabled) {
        qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
        qr_zero(&before, sizeof(before));
        return false;
    }
    if (qr_content_needs_no_reset()) {
        qr_zero(&before, sizeof(before));
        return true;
    }
    qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
    bool encoded = inktag_device_password_store_qr(&before,
        INKTAG_DEFAULT_DEVICE_QR_MODULES, inktag_default_device_qr,
        sizeof(inktag_default_device_qr), s_qr_transaction.data, &wire_length);
    qr_zero(&before, sizeof(before));
    if (!encoded) {
        qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
        return false;
    }
    bool committed = inktag_device_password_store_commit_io(&qr_io,
        s_qr_transaction.data, wire_length, &after);
    qr_zero(&after, sizeof(after));
    qr_zero(s_qr_transaction.data, sizeof(s_qr_transaction.data));
    return committed && password_settings_reset_verified();
}
bool inktag_om_password_reset_settings(inktag_device_password_config_t *actual) {
    if (!actual || !actual->valid || actual->enabled) return false;
    /* base resets an ordinary unprotected QR only; protected records remain
     * unchanged and no credential migration is implemented. */
    bool committed = password_settings_reset_commit();
    bool loaded = inktag_om_password_load(actual);
    return committed && loaded && actual->valid && !actual->enabled;
}
