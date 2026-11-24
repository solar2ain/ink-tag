#include "inktag_tlsr_recovery.h"

#include <string.h>

#include "inktag_recovery.h"
#include "inktag_tlsr.h"
#include "inktag_tlsr_ota_policy.h"
#include "inktag_tlsr_power.h"

#define INKTAG_TLSR_SECTOR_SIZE 0x1000u
#define INKTAG_TLSR_VALID_MARKER 0x4Bu
#define INKTAG_TLSR_CONTROLLED_RECOVERY_MARKER 0x00u

static RAM bool s_confirmation_pending;

static bool tlsr_read(void *context, uint32_t address, void *data,
                      uint32_t length) {
    (void)context;
    flash_read_page(address, length, (uint8_t *)data);
    return true;
}

static bool tlsr_write(void *context, uint32_t address, const void *data,
                       uint32_t length) {
    uint8_t verify[32];
    const uint8_t *source = (const uint8_t *)data;
    (void)context;
    while (length != 0u) {
        uint32_t count = length > sizeof(verify) ? sizeof(verify) : length;
        if (!inktag_tlsr_flash_write_page(address, count, source,
                INKTAG_POWER_OPERATION_FLASH)) return false;
        flash_read_page(address, count, verify);
        if (memcmp(source, verify, count) != 0) return false;
        address += count;
        source += count;
        length -= count;
    }
    return true;
}

static bool tlsr_erase(void *context, uint32_t address, uint32_t length) {
    uint8_t verify[32];
    (void)context;
    if ((address & (INKTAG_TLSR_SECTOR_SIZE - 1u)) != 0u ||
        (length & (INKTAG_TLSR_SECTOR_SIZE - 1u)) != 0u) return false;
    while (length != 0u) {
        uint32_t offset;
        if (!inktag_tlsr_flash_erase_sector(address,
                INKTAG_POWER_OPERATION_FLASH)) return false;
        for (offset = 0u; offset < INKTAG_TLSR_SECTOR_SIZE;
             offset += sizeof(verify)) {
            uint8_t i;
            flash_read_page(address + offset, sizeof(verify), verify);
            for (i = 0u; i < sizeof(verify); ++i)
                if (verify[i] != 0xFFu) return false;
        }
        address += INKTAG_TLSR_SECTOR_SIZE;
        length -= INKTAG_TLSR_SECTOR_SIZE;
    }
    return true;
}

static inktag_recovery_t make_recovery(void) {
    inktag_recovery_t recovery = {
        .layout = {
            .control_a = INKTAG_RECOVERY_STATE_A,
            .control_b = INKTAG_RECOVERY_STATE_B,
            .sector_size = INKTAG_TLSR_SECTOR_SIZE,
            .layout_signature = INKTAG_TLSR_RECOVERY_JOURNAL_SIGNATURE,
        },
        .io = {
            .context = NULL,
            .read = tlsr_read,
            .write = tlsr_write,
            .erase = tlsr_erase,
        },
    };
    return recovery;
}

static uint8_t marker_read(void) {
    uint8_t marker = 0xFFu;
    flash_read_page(INKTAG_APPLICATION_MARKER, 1u, &marker);
    return marker;
}

static bool marker_write(uint8_t marker,
                         inktag_power_operation_t operation) {
    uint8_t verify = 0xFFu;
    if (!inktag_tlsr_flash_write_page(INKTAG_APPLICATION_MARKER, 1u, &marker,
            operation)) return false;
    flash_read_page(INKTAG_APPLICATION_MARKER, 1u, &verify);
    return verify == marker;
}

bool inktag_tlsr_application_marker_valid(void) {
    return marker_read() == INKTAG_TLSR_VALID_MARKER;
}

bool inktag_tlsr_application_marker_commit(void) {
    return marker_write(INKTAG_TLSR_VALID_MARKER,
                        INKTAG_POWER_OPERATION_FLASH);
}

inktag_status_v2_t inktag_tlsr_recovery_prepare_entry_status(void) {
    uint8_t marker = INKTAG_TLSR_CONTROLLED_RECOVERY_MARKER;
    uint8_t verify = 0xFFu;
    /* Keep Recovery's existing boolean commit path.
     * This App-only entry preserves the reason from its one write/readback. */
    if (!inktag_tlsr_flash_write_page(INKTAG_APPLICATION_MARKER, 1u, &marker,
            INKTAG_POWER_OPERATION_OTA)) return INKTAG_STATUS_LOW_POWER;
    flash_read_page(INKTAG_APPLICATION_MARKER, 1u, &verify);
    return verify == marker ? INKTAG_STATUS_OK : INKTAG_STATUS_IO_ERROR;
}

bool inktag_tlsr_recovery_prepare_entry(void) {
    return marker_write(INKTAG_TLSR_CONTROLLED_RECOVERY_MARKER,
                        INKTAG_POWER_OPERATION_OTA);
}

bool inktag_tlsr_recovery_controlled_request_intact(void) {
    inktag_recovery_t recovery;
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result;
    if (marker_read() != INKTAG_TLSR_CONTROLLED_RECOVERY_MARKER) return false;
    recovery = make_recovery();
    result = inktag_recovery_status(&recovery, &state, NULL);
    return (result == INKTAG_RECOVERY_NOT_PENDING &&
            state == INKTAG_RECOVERY_STATE_NONE) ||
           (result == INKTAG_RECOVERY_OK &&
            state == INKTAG_RECOVERY_STATE_CONFIRMED);
}

bool inktag_tlsr_recovery_arm_compatibility_epoch(
    uint32_t compatibility_epoch) {
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_arm_compatibility_epoch(
               &recovery, compatibility_epoch) == INKTAG_RECOVERY_OK;
}

bool inktag_tlsr_recovery_return_ready(bool trial) {
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result = inktag_recovery_status(&recovery, &state, NULL);
    if (inktag_recovery_check_compatibility_epoch(&recovery,
            INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH) != INKTAG_RECOVERY_OK)
        return false;
    if (trial) return result == INKTAG_RECOVERY_OK && state == INKTAG_RECOVERY_STATE_ARMED;
    return (result == INKTAG_RECOVERY_NOT_PENDING && state == INKTAG_RECOVERY_STATE_NONE) ||
           (result == INKTAG_RECOVERY_OK && state == INKTAG_RECOVERY_STATE_CONFIRMED);
}

#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
bool inktag_tlsr_trusted_epoch_allowed(uint32_t epoch) {
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_check_compatibility_epoch(&recovery,epoch) ==
        INKTAG_RECOVERY_OK;
}

bool inktag_tlsr_trusted_journal_boot(void) {
    inktag_recovery_t recovery=make_recovery();
    inktag_recovery_state_t state=INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result=inktag_recovery_status(&recovery,&state,NULL);
    /* A confirmed/empty boot is read-only. Do not add a Flash-programming
     * voltage requirement to ordinary low-battery App execution. */
    if ((result==INKTAG_RECOVERY_NOT_PENDING && state==INKTAG_RECOVERY_STATE_NONE) ||
        (result==INKTAG_RECOVERY_OK && state==INKTAG_RECOVERY_STATE_CONFIRMED))
        return true;
    if (result!=INKTAG_RECOVERY_OK && result!=INKTAG_RECOVERY_ENTER) return false;
    if (!inktag_tlsr_power_operation_allowed(INKTAG_POWER_OPERATION_FLASH))
        return false;
    result=inktag_recovery_boot(&recovery);
    return result==INKTAG_RECOVERY_OK || result==INKTAG_RECOVERY_NOT_PENDING;
}
#endif

bool inktag_tlsr_recovery_boot(void) {
    inktag_recovery_t recovery = make_recovery();
#if defined(INKTAG_TLSR_TRUSTED_ENTRY)
    /* Trusted Recovery already executed the trial transition. Calling boot
     * twice would misclassify this first App start as an unconfirmed reboot. */
    inktag_recovery_result_t result=INKTAG_RECOVERY_OK;
#else
    inktag_recovery_result_t result = inktag_recovery_boot(&recovery);
#endif
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    s_confirmation_pending = false;
    if (result == INKTAG_RECOVERY_ENTER) {
        (void)inktag_tlsr_recovery_prepare_entry();
        start_reboot();
        return false;
    }
    if (result != INKTAG_RECOVERY_OK &&
        result != INKTAG_RECOVERY_NOT_PENDING) {
        /* Fail closed. Even if the marker write fails, rebooting retries the
         * journal decision instead of continuing with an unknown trial. */
        (void)inktag_tlsr_recovery_prepare_entry();
        start_reboot();
        return false;
    }
    if (inktag_recovery_status(&recovery, &state, NULL) ==
            INKTAG_RECOVERY_OK &&
        state == INKTAG_RECOVERY_STATE_TRIAL_STARTED) {
        s_confirmation_pending = true;
    }
    return true;
}

bool inktag_tlsr_recovery_confirmation_pending(void) {
    return s_confirmation_pending;
}

bool inktag_tlsr_recovery_confirm(void) {
    inktag_recovery_t recovery;
    inktag_recovery_result_t result;
    if (!s_confirmation_pending) return true;
    recovery = make_recovery();
    result = inktag_recovery_confirm_compatibility_epoch(
        &recovery, INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH);
    if (result != INKTAG_RECOVERY_OK) return false;
    s_confirmation_pending = false;
    return true;
}
