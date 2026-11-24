#include "main.h"

#include <string.h>

#include "inktag_feature_store.h"
#include "inktag_tlsr_p224_uecc.h"
#include "inktag_settings_records.h"
#include "inktag_tlsr_settings_backend.h"
#include "inktag_sha256.h"

#define TLSR_FIND_PUBLIC_RECORD_BYTES 73u

#if INKTAG_BUILD_PRO
#include "inktag_find_storage_state_pro.inc"
#endif

/* QR upload/render work is explicitly kept awake by the application. Unlike
 * BLE state, this bounded scratch record therefore does not consume the B85
 * low-32-KiB retention bank. */
static uint8_t s_device_info_qr[INKTAG_DEVICE_PASSWORD_BUNDLE_MAX_BYTES];
static uint16_t s_device_info_qr_length;
static uint32_t s_device_info_qr_crc;
static uint8_t s_device_info_qr_modules;
static uint8_t s_device_info_qr_object;
static uint16_t s_device_info_qr_received;
static RAM bool s_device_info_qr_active;

static void findmy_secure_zero(void *buffer, uint16_t length) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    while (length-- > 0u) *bytes++ = 0u;
}

#if INKTAG_BUILD_PRO
#include "inktag_find_storage_pro.inc"
#else
inktag_activation_storage_result_t inktag_tlsr_feature_activation_read(
    void *context, inktag_activation_record_t *record) {
    (void)context; (void)record; return INKTAG_ACTIVATION_STORAGE_EMPTY;
}
bool inktag_tlsr_feature_activation_write(void *context, const inktag_activation_record_t *record) {
    (void)context; (void)record; return false;
}
void inktag_tlsr_findmy_config_init(inktag_findmy_config_t *config) {
    memset(config, 0, sizeof(*config));
}
inktag_tlsr_find_public_state_t inktag_tlsr_findmy_public_state(void) {
    return INKTAG_TLSR_FIND_PUBLIC_EMPTY;
}
#endif

bool inktag_tlsr_findmy_owner_nonce(
    uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES]) {
    uint8_t attempt;
    if (nonce == NULL) return false;
    for (attempt = 0u; attempt < 4u; ++attempt) {
        uint8_t any = 0u;
        uint8_t index;
        generateRandomNum(INKTAG_FINDMY_PUBLIC_NONCE_BYTES, nonce);
        for (index = 0u; index < INKTAG_FINDMY_PUBLIC_NONCE_BYTES; ++index)
            any |= nonce[index];
        if (any != 0u) return true;
    }
    findmy_secure_zero(nonce, INKTAG_FINDMY_PUBLIC_NONCE_BYTES);
    return false;
}

#if INKTAG_BUILD_PRO
#include "inktag_find_select_pro.inc"
#endif

bool inktag_tlsr_device_info_qr_available(void) {
    return inktag_settings_device_info_qr_available();
}

bool inktag_tlsr_device_info_qr_transfer_active(void) {
    return s_device_info_qr_active;
}

bool inktag_tlsr_password_load(inktag_device_password_config_t *config) {
    uint8_t modules;
    uint16_t offset, length;
    if (s_device_info_qr_active) {
        memset(config, 0, sizeof(*config));
        return false;
    }
    inktag_device_password_store_result_t result = inktag_settings_password_load(
        s_device_info_qr, config, &modules, &offset, &length);
    findmy_secure_zero(s_device_info_qr, sizeof(s_device_info_qr));
    return result == INKTAG_PASSWORD_STORE_OK ||
           result == INKTAG_PASSWORD_STORE_MISSING;
}

bool inktag_tlsr_device_info_qr_read(
    uint8_t *data, uint16_t capacity, uint8_t *modules) {
    uint16_t stored_length = 0u, offset = 0u;
    inktag_device_password_config_t config;
    if (data == NULL || modules == NULL || s_device_info_qr_active) return false;
    if (inktag_settings_password_load(s_device_info_qr, &config, modules,
                                     &offset, &stored_length) !=
            INKTAG_PASSWORD_STORE_OK || stored_length - offset > capacity) {
        findmy_secure_zero(&config, sizeof(config));
        findmy_secure_zero(s_device_info_qr, sizeof(s_device_info_qr));
        return false;
    }
    /* The record buffer is also the upload staging area. Never lend it to a
     * multi-page renderer that services BLE between pages: a new transfer may
     * legally reuse the staging record before drawing has completed. */
    memcpy(data, s_device_info_qr + offset, stored_length - offset);
    findmy_secure_zero(&config, sizeof(config));
    findmy_secure_zero(s_device_info_qr, sizeof(s_device_info_qr));
    return true;
}

bool inktag_tlsr_device_info_qr_begin(
    const inktag_transfer_descriptor_t *descriptor) {
    if (s_device_info_qr_active ||
        !inktag_device_info_qr_transfer_valid(descriptor)) return false;
    memset(s_device_info_qr, 0, sizeof(s_device_info_qr));
    s_device_info_qr_modules = descriptor->format;
    s_device_info_qr_object = descriptor->object_type;
    s_device_info_qr_length = (uint16_t)descriptor->total_size;
    s_device_info_qr_crc = descriptor->expected_crc;
    s_device_info_qr_received = 0u;
    s_device_info_qr_active = true;
    return true;
}

#if INKTAG_BUILD_PRO
#include "inktag_password_bundle_begin_pro.inc"
#else
bool inktag_tlsr_password_bundle_begin(const inktag_transfer_descriptor_t *descriptor) { (void)descriptor;return false; }
#endif

bool inktag_tlsr_device_info_qr_append(
    uint32_t offset, const uint8_t *data, uint16_t length) {
    if (!s_device_info_qr_active || data == NULL || length == 0u ||
        offset != s_device_info_qr_received ||
        offset + length > s_device_info_qr_length) return false;
    uint16_t header = s_device_info_qr_object ==
        INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ?
            INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES : 0u;
    memcpy(s_device_info_qr + header + offset, data, length);
    s_device_info_qr_received = (uint16_t)(offset + length);
    return true;
}

bool inktag_tlsr_device_info_qr_commit(inktag_device_password_config_t *config) {
    uint16_t stored_length;
    if (!s_device_info_qr_active ||
        s_device_info_qr_object != INKTAG_TRANSFER_OBJECT_DEVICE_INFO_QR ||
        s_device_info_qr_received != s_device_info_qr_length ||
        (inktag_crc32_update(0xffffffffu,
            s_device_info_qr + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES,
            s_device_info_qr_length) ^ 0xffffffffu) != s_device_info_qr_crc ||
        !inktag_device_password_store_qr(config, s_device_info_qr_modules,
            s_device_info_qr + INKTAG_DEVICE_PASSWORD_BUNDLE_HEADER_BYTES,
            s_device_info_qr_length, s_device_info_qr, &stored_length))
        return false;
    bool saved = inktag_settings_password_commit(
        s_device_info_qr, stored_length, config);
    inktag_tlsr_device_info_qr_abort();
    return saved;
}

#if INKTAG_BUILD_PRO
#include "inktag_password_bundle_commit_pro.inc"
#else
inktag_status_v2_t inktag_tlsr_password_bundle_commit(inktag_device_password_t *session,inktag_device_password_config_t *config,uint32_t now_ms) { (void)session;(void)config;(void)now_ms;return INKTAG_STATUS_UNSUPPORTED; }
#endif

void inktag_tlsr_device_info_qr_abort(void) {
    s_device_info_qr_active = false;
    s_device_info_qr_received = 0u;
    findmy_secure_zero(s_device_info_qr, sizeof(s_device_info_qr));
}
