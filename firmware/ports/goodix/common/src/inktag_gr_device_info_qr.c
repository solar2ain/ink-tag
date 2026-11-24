#ifndef INKTAG_BUILD_PRO
#define INKTAG_BUILD_PRO 1
#endif
#line 1 "../Src/user/inktag_gr_device_info_qr.c"
#include "inktag_gr_device_info_qr.h"
#include <string.h>
#include "inktag_device_password_store.h"
#include "inktag_sha256.h"
#include "inktag_gr_health.h"
#include "inktag_gr_settings_store.h"
#include "inktag_gui_device_qr.h"

/* One borrowed staging area for ordinary QR and password+QR. No new record,
 * image buffer or Flash region. ID5 uses the frozen common current schema. */
static uint8_t s_record[INKTAG_DEVICE_PASSWORD_BUNDLE_MAX_BYTES];
static bool s_transfer_active;
static uint8_t s_object, s_modules;
static uint16_t s_length, s_received;
static uint32_t s_crc;

static void wipe(void *p, uint16_t n) {
    volatile uint8_t *b = p;
    while (n--) *b++ = 0u;
}

static inktag_device_password_store_result_t record_read(
    void *context, uint8_t *output, uint16_t capacity, uint16_t *length) {
    (void)context;
    *length = capacity;
    inktag_gr_settings_read_result_t result = inktag_gr_settings_get(
        INKTAG_GR_SETTINGS_SLOT_DEVICE_INFO_QR, length, output);
    /* SDK cold filtering can hide an installed password. Only a fully decoded
     * explicit OFF record proves OFF; missing is unknown, never a new device. */
    if (result != INKTAG_GR_SETTINGS_READ_OK) *length = 0u;
    return result == INKTAG_GR_SETTINGS_READ_OK ?
        INKTAG_PASSWORD_STORE_OK : INKTAG_PASSWORD_STORE_ERROR;
}

static bool record_put(void *context, const uint8_t *wire, uint16_t length) {
    (void)context;
    return inktag_gr_settings_put(
        INKTAG_GR_SETTINGS_SLOT_DEVICE_INFO_QR, length, wire);
}

static const inktag_device_password_store_io_t s_record_io = {
    NULL, record_read, record_put
};

static bool load(inktag_device_password_config_t *config,
                 uint8_t *modules, uint16_t *offset, uint16_t *length) {
    inktag_device_password_store_result_t result =
        inktag_device_password_store_load_io(
            &s_record_io, s_record, config, modules, offset, length);
    return result == INKTAG_PASSWORD_STORE_OK;
}

bool inktag_gr_password_load(inktag_device_password_config_t *config) {
    uint8_t modules; uint16_t offset, length;
    if (!config) return false;
    if (s_transfer_active) { memset(config, 0, sizeof(*config)); return false; }
    bool result = load(config, &modules, &offset, &length);
    wipe(s_record, sizeof(s_record));
    return result;
}

static inktag_status_v2_t commit(uint16_t length,
    inktag_device_password_config_t *actual) {
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH, true))
        return INKTAG_STATUS_LOW_POWER;
    /* Common owns validation and exact readback, including false-after-commit
     * reconciliation. Goodix uses one official NVDS record; actual power-cut
     * atomicity remains an independent, currently open qualification gate. */
    return inktag_device_password_store_commit_io(
        &s_record_io, s_record, length, actual) ?
        INKTAG_STATUS_OK : INKTAG_STATUS_PERSISTENCE_ERROR;
}

void inktag_gr_device_info_qr_abort(void) {
    s_transfer_active = false;
    s_received = s_length = 0u;
    wipe(s_record, sizeof(s_record));
}

inktag_status_v2_t inktag_gr_password_reset_settings(
    inktag_device_password_config_t *config) {
    if (!config || !config->valid) return INKTAG_STATUS_PERSISTENCE_ERROR;
    if (s_transfer_active) return INKTAG_STATUS_BUSY;
    uint16_t length;
    inktag_device_password_config_t disabled = {0};
    disabled.valid = true;
    if (!inktag_device_password_store_qr(&disabled,
            INKTAG_DEFAULT_DEVICE_QR_MODULES, inktag_default_device_qr,
            sizeof(inktag_default_device_qr), s_record, &length))
        return INKTAG_STATUS_INVALID_STATE;
    inktag_status_v2_t result = commit(length, config);
    /* commit reloads the actual old/new/unknown config even after a false ACK.
     * The dispatcher clears AUTH on any final-commit attempt, not only OK. */
    inktag_gr_device_info_qr_abort();
    return result;
}

inktag_status_v2_t inktag_gr_device_info_qr_begin(
    const inktag_transfer_descriptor_t *descriptor) {
    if (s_transfer_active) return INKTAG_STATUS_BUSY;
    if (!inktag_device_info_qr_transfer_valid(descriptor))
        return INKTAG_STATUS_INVALID_REQUEST;
    wipe(s_record, sizeof(s_record));
    s_object = descriptor->object_type; s_modules = descriptor->format;
    s_length = (uint16_t)descriptor->total_size;
    s_crc = descriptor->expected_crc; s_received = 0u;
    s_transfer_active = true;
    return INKTAG_STATUS_OK;
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_password_bundle_begin_pro.inc"
#else
inktag_status_v2_t inktag_gr_password_bundle_begin(const inktag_transfer_descriptor_t *descriptor) { (void)descriptor; return INKTAG_STATUS_UNSUPPORTED; }
#endif
#line 123 "../Src/user/inktag_gr_device_info_qr.c"


inktag_status_v2_t inktag_gr_device_info_qr_write(
    uint32_t offset, const uint8_t *data, uint16_t length) {
    uint16_t header = s_object == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ?
        INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES : 0u;
    if (!s_transfer_active) return INKTAG_STATUS_INVALID_STATE;
    if (!data || !length || offset != s_received || s_received > s_length ||
        length > s_length - s_received ||
        header + offset + length > sizeof(s_record))
        return INKTAG_STATUS_INVALID_REQUEST;
    memcpy(s_record + header + offset, data, length);
    s_received += length;
    return INKTAG_STATUS_OK;
}

inktag_status_v2_t inktag_gr_device_info_qr_commit(
    inktag_device_password_config_t *config) {
    uint16_t stored;
    inktag_status_v2_t result = INKTAG_STATUS_INVALID_STATE;
    if (s_transfer_active && s_object == INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR &&
        s_received == s_length) {
        if ((inktag_crc32_update(0xffffffffu,
                s_record + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES,
                s_length) ^ 0xffffffffu) != s_crc)
            result = INKTAG_STATUS_CHECKSUM_ERROR;
        else if (!inktag_device_password_store_qr(config, s_modules,
                s_record + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES,
                s_length, s_record, &stored)) result = INKTAG_STATUS_NO_SPACE;
        else result = commit(stored, config);
    }
    inktag_gr_device_info_qr_abort();
    return result;
}

#if INKTAG_BUILD_PRO
#include "inktag_gr_password_bundle_commit_pro.inc"
#else
inktag_status_v2_t inktag_gr_password_bundle_commit(inktag_device_password_t *session, inktag_device_password_config_t *config, uint32_t now_ms) { (void)session; (void)config; (void)now_ms; return INKTAG_STATUS_UNSUPPORTED; }
#endif
#line 197 "../Src/user/inktag_gr_device_info_qr.c"


bool inktag_gr_device_info_qr_read(uint8_t slot, uint8_t *modules,
    const uint8_t **data, uint16_t *length) {
    inktag_device_password_config_t config;
    uint16_t offset, stored;
    if (slot >= INKTAG_DEVICE_INFO_QR_SLOT_COUNT || !modules || !data ||
        !length || s_transfer_active) return false;
    bool ok = load(&config, modules, &offset, &stored) && stored != 0u;
    wipe(&config, sizeof(config));
    if (!ok) { wipe(s_record, sizeof(s_record)); return false; }
    *length = stored - offset;
    memmove(s_record, s_record + offset, *length);
    wipe(s_record + *length, sizeof(s_record) - *length);
    *data = s_record; /* Matrix only: no V/W bytes can escape this boundary. */
    return true;
}
