#include "inktag_da_ota_security.h"

#include <stddef.h>
#include <string.h>

#include "inktag_da_health.h"
#include "inktag_da_ota_policy.h"
#include "inktag_da_recovery.h"
#include "inktag_firmware_signature.h"
#include "inktag_power.h"
#include "spi_flash.h"

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t application_size;
    uint32_t compatibility_epoch;
    uint32_t board_signature;
    uint32_t layout_signature;
    uint32_t verifier_signature;
} da_pq_preflight_t;

typedef char da_pq_preflight_size_check[
    sizeof(da_pq_preflight_t) == 24u ? 1 : -1];

static da_pq_preflight_t s_preflight;
static bool s_preflight_valid;
static bool s_reject_pending;
static inktag_ota_reject_info_t s_reject;

/* Keep verification workspaces out of the small Recovery call stack. */
static inktag_firmware_signature_trailer_t s_trailer;

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void inktag_da_ota_reject_clear(void)
{
    s_reject_pending = false;
    memset(&s_reject, 0, sizeof(s_reject));
}

bool inktag_da_ota_reject_pending(void)
{
    return s_reject_pending;
}

void inktag_da_ota_reject_set(
    inktag_ota_reject_reason_t reason, inktag_ota_reject_stage_t stage,
    uint8_t flags, uint32_t detail, uint16_t sampled_mv,
    uint16_t threshold_mv)
{
    s_reject.reason = reason;
    s_reject.stage = stage;
    s_reject.flags = flags;
    s_reject.detail = detail;
    s_reject.sampled_mv = sampled_mv;
    s_reject.threshold_mv = threshold_mv;
    s_reject_pending = true;
}

void inktag_da_ota_reject_set_default(
    inktag_ota_reject_reason_t reason, inktag_ota_reject_stage_t stage,
    uint8_t flags, uint32_t detail)
{
    if (!s_reject_pending)
        inktag_da_ota_reject_set(
            reason, stage, flags, detail, 0u, 0u);
}

void inktag_da_ota_reject_power(
    inktag_ota_reject_stage_t stage, uint32_t detail,
    uint16_t threshold_mv, bool detail_is_address)
{
    bool valid = false;
    uint16_t sampled_mv =
        inktag_da_health_last_operation_voltage_mv(&valid);
    uint8_t flags = INKTAG_OTA_REJECT_FLAG_THRESHOLD_MV_VALID |
        (detail_is_address ? INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS :
                             INKTAG_OTA_REJECT_FLAG_DETAIL_IS_OFFSET);
    if (valid) flags |= INKTAG_OTA_REJECT_FLAG_SAMPLED_MV_VALID;
    inktag_da_ota_reject_set(
        valid ? INKTAG_OTA_REJECT_REASON_VOLTAGE_BELOW_MIN :
                INKTAG_OTA_REJECT_REASON_POWER_SAMPLE_INVALID,
        stage, flags, detail, sampled_mv, threshold_mv);
}

uint8_t inktag_da_ota_status_encode(
    uint8_t status, uint8_t *output, uint8_t capacity)
{
    uint8_t length;
    if (output == NULL || capacity == 0u) return 0u;
    if (!s_reject_pending) {
        output[0] = status;
        return 1u;
    }
    length = inktag_ota_reject_encode(output, capacity, status, &s_reject);
    inktag_da_ota_reject_clear();
    if (length == 0u) {
        output[0] = status;
        return 1u;
    }
    return length;
}

bool inktag_da_ota_preflight(const uint8_t *pq_header, uint32_t length)
{
    da_pq_preflight_t candidate;
    uint32_t code_size;
    inktag_recovery_result_t epoch_result;
    s_preflight_valid = false;
    inktag_da_ota_reject_clear();
    if (pq_header == NULL || length < INKTAG_DA_PQ_HEADER_BYTES) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, length, 0u, 0u);
        return false;
    }
    memcpy(&candidate, pq_header + INKTAG_DA_PQ_PREFLIGHT_OFFSET,
           sizeof(candidate));
    code_size = read_u32_le(&pq_header[4]);
    if (candidate.magic != INKTAG_DA_PQ_PREFLIGHT_MAGIC ||
        candidate.application_size == 0u ||
        candidate.application_size > INKTAG_DA_APPLICATION_RAW_LIMIT -
                INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES ||
        candidate.application_size +
                INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES != code_size ||
        code_size > INKTAG_DA_APPLICATION_RAW_LIMIT ||
        candidate.compatibility_epoch >
                INKTAG_RECOVERY_COMPATIBILITY_EPOCH_MAX ||
        candidate.board_signature != INKTAG_DA_OTA_BOARD_SIGNATURE ||
        candidate.layout_signature != INKTAG_DA_OTA_LAYOUT_SIGNATURE ||
        (candidate.verifier_signature != INKTAG_DA_OTA_VERIFIER_SIGNATURE
         && candidate.verifier_signature != INKTAG_DA_OTA_UNSIGNED_VERIFIER
         )) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, code_size, 0u, 0u);
        return false;
    }
    epoch_result = inktag_da_recovery_check_epoch(
        candidate.compatibility_epoch);
    /* The common journal adapter parks external Flash at transaction end;
     * this preflight is called from an already active SUOTA session. */
    (void)spi_flash_release_from_power_down();
    if (epoch_result != INKTAG_RECOVERY_OK) {
        inktag_da_ota_reject_set(
            epoch_result == INKTAG_RECOVERY_ROLLBACK_ERROR ?
                INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID :
                INKTAG_OTA_REJECT_REASON_INTERNAL,
            epoch_result == INKTAG_RECOVERY_ROLLBACK_ERROR ?
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START :
                INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT,
            0u, candidate.compatibility_epoch, 0u, 0u);
        return false;
    }
    s_preflight = candidate;
    s_preflight_valid = true;
    return true;
}


static bool flash_read_exact(uint32_t address, void *data, uint32_t length)
{
    uint32_t actual = 0u;
    return spi_flash_read_data(data, address, length, &actual) ==
               SPI_FLASH_ERR_OK && actual == length;
}

bool inktag_da_ota_verify_and_arm(void)
{
    uint8_t header[INKTAG_DA_PQ_HEADER_BYTES];
    bool is_unsigned;
    bool verified;
    uint32_t address;
    inktag_recovery_result_t arm_result;
    if (!s_preflight_valid ||
        !flash_read_exact(INKTAG_DA_APPLICATION_START, header,
                          sizeof(header))) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_APPLICATION_START, 0u, 0u);
        return false;
    }
    is_unsigned = s_preflight.verifier_signature == INKTAG_DA_OTA_UNSIGNED_VERIFIER;
    address = INKTAG_DA_APPLICATION_START + INKTAG_DA_PQ_HEADER_BYTES +
        s_preflight.application_size;
    if (read_u32_le(&header[4]) != s_preflight.application_size +
            INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES ||
        memcmp(header + INKTAG_DA_PQ_PREFLIGHT_OFFSET, &s_preflight,
               sizeof(s_preflight)) != 0 ||
        !flash_read_exact(address, &s_trailer, sizeof(s_trailer)) ||
        s_trailer.magic != (is_unsigned ? INKTAG_DA_OTA_UNSIGNED_MAGIC :
                                          INKTAG_FIRMWARE_SIGNATURE_MAGIC) ||
        s_trailer.version != INKTAG_FIRMWARE_SIGNATURE_VERSION ||
        s_trailer.algorithm != (is_unsigned ? INKTAG_DA_OTA_UNSIGNED_ALGORITHM :
                                              INKTAG_FIRMWARE_SIGNATURE_ECDSA_P256) ||
        s_trailer.trailer_size != INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES ||
        s_trailer.application_size != s_preflight.application_size ||
        s_trailer.compatibility_epoch != s_preflight.compatibility_epoch ||
        s_trailer.signed_context.board_signature != s_preflight.board_signature ||
        s_trailer.signed_context.layout_signature != s_preflight.layout_signature ||
        s_trailer.signed_context.verifier_signature != s_preflight.verifier_signature
        ) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, address, 0u, 0u);
        return false;
    }

    /* Same production CRC reader as resume-app, covering the complete native
     * payload including the opaque publisher trailer. No ECC/SHA SDK path. */
    verified = inktag_da_recovery_application_crc_valid();
    if (is_unsigned) {
        for (uint8_t i = 32u; i < 64u; ++i)
            if (s_trailer.signature[i] != 0u) verified = false;
    }
    if (!verified) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_APPLICATION_START, 0u, 0u);
        return false;
    }
    arm_result = inktag_da_recovery_arm_epoch(
        s_trailer.compatibility_epoch);
    if (arm_result != INKTAG_RECOVERY_OK) {
        inktag_da_ota_reject_set(
            arm_result == INKTAG_RECOVERY_ROLLBACK_ERROR ?
                INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID :
                INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u,
            s_trailer.compatibility_epoch, 0u, 0u);
        return false;
    }
    if (!inktag_da_recovery_release_request()) {
        inktag_da_ota_reject_set(
            INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_DA_RECOVERY_REQUEST_ADDRESS, 0u, 0u);
        return false;
    }
    s_preflight_valid = false;
    return true;
}
