#ifndef INKTAG_TLSR_FEATURE_STORE_H
#define INKTAG_TLSR_FEATURE_STORE_H

#include "inktag_activation.h"
#include "inktag_findmy.h"
#include "inktag_findmy_public.h"
#define INKTAG_TLSR_FINDMY_ROTATION_PERIOD_SECONDS \
    INKTAG_FINDMY_PUBLIC_SLOT_SECONDS
#define INKTAG_FINDMY_FALLBACK_KEY_INDEX 0xffu

#include "inktag_protocol_core.h"
#include "inktag_device_password.h"

inktag_activation_storage_result_t inktag_tlsr_feature_activation_read(
    void *context, inktag_activation_record_t *activation);
bool inktag_tlsr_feature_activation_write(
    void *context, const inktag_activation_record_t *activation);

void inktag_tlsr_findmy_config_init(inktag_findmy_config_t *config);
bool inktag_tlsr_findmy_config_write(const inktag_findmy_config_t *config);
bool inktag_tlsr_findmy_clear(inktag_findmy_config_t *config);

typedef enum {
    INKTAG_TLSR_FIND_PUBLIC_EMPTY = 0,
    INKTAG_TLSR_FIND_PUBLIC_PRESENT_V2 = 2,
    INKTAG_TLSR_FIND_PUBLIC_CORRUPT = 3,
} inktag_tlsr_find_public_state_t;
inktag_tlsr_find_public_state_t inktag_tlsr_findmy_public_state(void);
bool inktag_tlsr_findmy_public_fingerprint_read(uint8_t fingerprint[16]);
bool inktag_tlsr_findmy_public_owner_material(
    uint8_t fingerprint[16], uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES]);
#if INKTAG_BUILD_PRO
bool inktag_tlsr_findmy_unregister_proof_valid(const uint8_t payload[41],
                                               const uint8_t device_id[8]);
bool inktag_tlsr_findmy_unregister(inktag_findmy_config_t *config);
#endif
bool inktag_tlsr_findmy_public_provision(
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint64_t unix_seconds, uint64_t *slot,
    uint8_t public_x[INKTAG_FINDMY_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[16]);
bool inktag_tlsr_findmy_public_replace(
    const uint8_t old_fingerprint[16],
    const uint8_t master_xy[INKTAG_FINDMY_PUBLIC_POINT_BYTES],
    const uint8_t key[INKTAG_FINDMY_PUBLIC_K_BYTES],
    uint64_t unix_seconds, uint64_t *slot,
    uint8_t public_x[INKTAG_FINDMY_PUBLIC_KEY_BYTES],
    uint8_t fingerprint[16]);
bool inktag_tlsr_findmy_owner_nonce(
    uint8_t nonce[INKTAG_FINDMY_PUBLIC_NONCE_BYTES]);
bool inktag_tlsr_findmy_select(
    uint64_t unix_seconds, bool time_valid, uint8_t *key_index,
    uint8_t public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);
bool inktag_tlsr_device_info_qr_available(void);
bool inktag_tlsr_device_info_qr_transfer_active(void);
bool inktag_tlsr_device_info_qr_read(
    uint8_t *data, uint16_t capacity, uint8_t *modules);
bool inktag_tlsr_device_info_qr_begin(
    const inktag_transfer_descriptor_t *descriptor);
bool inktag_tlsr_device_info_qr_append(
    uint32_t offset, const uint8_t *data, uint16_t length);
bool inktag_tlsr_device_info_qr_commit(inktag_device_password_config_t *config);
void inktag_tlsr_device_info_qr_abort(void);
bool inktag_tlsr_password_load(inktag_device_password_config_t *config);
bool inktag_tlsr_password_bundle_begin(
    const inktag_transfer_descriptor_t *descriptor);
inktag_status_v2_t inktag_tlsr_password_bundle_commit(
    inktag_device_password_t *session, inktag_device_password_config_t *config,
    uint32_t now_ms);

#endif
