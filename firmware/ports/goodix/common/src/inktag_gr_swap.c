#include "inktag_gr_swap.h"

#include "inktag_gr_dfu_diagnostics.h"
#include "inktag_gr_flash_layout.h"
#include "inktag_gr_ota_policy.h"

#include <stddef.h>
#include <string.h>

#include "dfu_port.h"
#include "hal_flash.h"
#include "inktag_recovery.h"

#if defined(INKTAG_SWAP_BOOTLOADER)
extern bool inktag_dfu_programming_allowed(void);
extern bool inktag_dfu_ota_admission_allowed(void);
extern bool inktag_dfu_safe_resume_clear_allowed(void);
extern bool inktag_gr_bootloader_app_valid(void);
#else
#include "inktag_gr_health.h"
#endif

#if defined(INKTAG_SWAP_BOOTLOADER) && \
    (defined(INKTAG_GOODIX_GR5331) || defined(SOC_GR533X) || \
     defined(INKTAG_GOODIX_GR5515) || defined(SOC_GR5515))
#include "inktag_gr_app_integrity.h"
#endif

#define INKTAG_GR_SECTOR_SIZE 0x1000u
#define INKTAG_GR_RECOVERY_REQUEST_MAGIC 0x31525247u /* GRR1 */
#define INKTAG_GR_APPLICATION_MUTATED_MAGIC 0x31575247u /* GRW1 */

#if defined(INKTAG_GOODIX_GR5331) || defined(SOC_GR533X)
#define INKTAG_GR_CONTROL_A INKTAG_GR_RECOVERY_JOURNAL_A
#define INKTAG_GR_CONTROL_B INKTAG_GR_RECOVERY_JOURNAL_B
#define INKTAG_GR_APP_INFO  0x00202000u
#define INKTAG_GR_LAYOUT_ID INKTAG_RELEASE_LAYOUT_GR5331
#define INKTAG_GR_APP_START 0x00220000u
#define INKTAG_GR_DFU_INFO  0x00203000u
#define INKTAG_GR_FIRST_DISPLAY_ADDRESS 0x00203FF8u
#elif defined(INKTAG_GOODIX_GR5515) || defined(SOC_GR5515)
/* These logical pages alias 0x7A000/0x7B000 on 512 KiB GR5513 boards. */
#define INKTAG_GR_CONTROL_A INKTAG_GR_RECOVERY_JOURNAL_A
#define INKTAG_GR_CONTROL_B INKTAG_GR_RECOVERY_JOURNAL_B
#define INKTAG_GR_APP_INFO  0x01002000u
#define INKTAG_GR_LAYOUT_ID INKTAG_RELEASE_LAYOUT_GR551X
#define INKTAG_GR_APP_START 0x01020000u
#define INKTAG_GR_DFU_INFO  0x01003000u
#define INKTAG_GR_FIRST_DISPLAY_ADDRESS 0x01003FF8u
#else
#error "InkTag Goodix recovery requires a GR5331 or GR551x target"
#endif

#define INKTAG_GR_FIRST_DISPLAY_MAGIC 0x31444649u /* IFD1 */
#define INKTAG_GR_RECOVERY_SESSION_ADDRESS \
    (INKTAG_GR_DFU_INFO + INKTAG_GR_SECTOR_SIZE - 24u)

typedef struct {
    uint32_t magic;
    uint32_t magic_inverse;
} inktag_gr_first_display_record_t;

typedef struct {
    uint32_t request_magic;
    uint32_t request_magic_inverse;
    uint32_t mutation_magic;
    uint32_t mutation_magic_inverse;
} inktag_gr_recovery_session_record_t;

static bool gr_read(void *context, uint32_t address, void *data, uint32_t length) {
    (void)context;
    return hal_flash_read(address, (uint8_t *)data, length) == length;
}

static bool gr_mutation_allowed(void) {
#if defined(INKTAG_SWAP_BOOTLOADER)
    return inktag_dfu_programming_allowed();
#else
    return inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                               true);
#endif
}

static bool gr_write(void *context, uint32_t address, const void *data,
                     uint32_t length) {
    (void)context;
    if (!gr_mutation_allowed()) return false;
    return hal_flash_write(address, (const uint8_t *)data, length) == length;
}

static bool gr_erase(void *context, uint32_t address, uint32_t length) {
    (void)context;
    if (!gr_mutation_allowed()) return false;
    return hal_flash_erase(address, length);
}

static inktag_recovery_t make_recovery(void) {
    inktag_recovery_t recovery = {
        .layout = {
            .control_a = INKTAG_GR_CONTROL_A,
            .control_b = INKTAG_GR_CONTROL_B,
            .sector_size = INKTAG_GR_SECTOR_SIZE,
            .layout_signature = INKTAG_GR_LAYOUT_ID,
        },
        .io = {
            .context = NULL,
            .read = gr_read,
            .write = gr_write,
            .erase = gr_erase,
        },
    };
    return recovery;
}

static uint32_t flash_mutation_begin(bool *was_enabled) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *was_enabled = hal_flash_get_security();
    if (*was_enabled) hal_flash_set_security(false);
    return primask;
}

static void flash_mutation_end(bool was_enabled, uint32_t primask) {
    if (was_enabled) hal_flash_set_security(true);
    if ((primask & 1u) == 0u) __enable_irq();
}

static bool marker_pair_matches(uint32_t value, uint32_t inverse,
                                uint32_t magic) {
    return value == magic && inverse == ~magic;
}

static bool recovery_session_read(inktag_gr_recovery_session_record_t *record) {
    return record != NULL &&
           gr_read(NULL, INKTAG_GR_RECOVERY_SESSION_ADDRESS, record,
                   sizeof(*record));
}

bool inktag_dfu_recovery_request_mark(void) {
    inktag_gr_recovery_session_record_t record;
#if defined(INKTAG_SWAP_BOOTLOADER)
    if (!inktag_dfu_ota_admission_allowed()) return false;
#else
    if (!inktag_gr_health_operation_allowed(INKTAG_POWER_OPERATION_OTA,
                                             true)) {
        return false;
    }
#endif
    if (!recovery_session_read(&record)) return false;
    if (marker_pair_matches(record.request_magic,
                            record.request_magic_inverse,
                            INKTAG_GR_RECOVERY_REQUEST_MAGIC) &&
        record.mutation_magic == UINT32_MAX &&
        record.mutation_magic_inverse == UINT32_MAX) {
        return true;
    }
    if (record.request_magic != UINT32_MAX ||
        record.request_magic_inverse != UINT32_MAX ||
        record.mutation_magic != UINT32_MAX ||
        record.mutation_magic_inverse != UINT32_MAX) {
        return false;
    }
    uint32_t marker[2] = {
        INKTAG_GR_RECOVERY_REQUEST_MAGIC,
        ~INKTAG_GR_RECOVERY_REQUEST_MAGIC,
    };
    return gr_write(NULL, INKTAG_GR_RECOVERY_SESSION_ADDRESS, marker,
                    sizeof(marker)) &&
           recovery_session_read(&record) &&
           marker_pair_matches(record.request_magic,
                               record.request_magic_inverse,
                               INKTAG_GR_RECOVERY_REQUEST_MAGIC) &&
           record.mutation_magic == UINT32_MAX &&
           record.mutation_magic_inverse == UINT32_MAX;
}

bool inktag_dfu_application_mutation_mark(void) {
    inktag_gr_recovery_session_record_t record;
    if (!recovery_session_read(&record)) return false;
    if (marker_pair_matches(record.mutation_magic,
                            record.mutation_magic_inverse,
                            INKTAG_GR_APPLICATION_MUTATED_MAGIC)) {
        return true;
    }
    if (record.mutation_magic != UINT32_MAX ||
        record.mutation_magic_inverse != UINT32_MAX) {
        return false;
    }
    uint32_t marker[2] = {
        INKTAG_GR_APPLICATION_MUTATED_MAGIC,
        ~INKTAG_GR_APPLICATION_MUTATED_MAGIC,
    };
    return gr_write(NULL, INKTAG_GR_RECOVERY_SESSION_ADDRESS + 8u,
                    marker, sizeof(marker)) &&
           recovery_session_read(&record) &&
           marker_pair_matches(record.mutation_magic,
                               record.mutation_magic_inverse,
                               INKTAG_GR_APPLICATION_MUTATED_MAGIC);
}

#if defined(INKTAG_SWAP_BOOTLOADER)
static bool clear_controlled_recovery_request(void) {
    bool was_enabled;
    if (!inktag_dfu_safe_resume_clear_allowed()) return false;
    uint32_t primask = flash_mutation_begin(&was_enabled);
    bool erased = hal_flash_erase(INKTAG_GR_DFU_INFO,
                                  INKTAG_GR_SECTOR_SIZE);
    flash_mutation_end(was_enabled, primask);
    return erased;
}

static bool unmodified_recovery_request(void) {
    inktag_gr_recovery_session_record_t record;
    if (!recovery_session_read(&record) ||
        !marker_pair_matches(record.request_magic,
                             record.request_magic_inverse,
                             INKTAG_GR_RECOVERY_REQUEST_MAGIC) ||
        record.mutation_magic != UINT32_MAX ||
        record.mutation_magic_inverse != UINT32_MAX) {
        return false;
    }
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result =
        inktag_recovery_status(&recovery, &state, NULL);
    if (!((result == INKTAG_RECOVERY_NOT_PENDING &&
           state == INKTAG_RECOVERY_STATE_NONE) ||
          (result == INKTAG_RECOVERY_OK &&
           state == INKTAG_RECOVERY_STATE_CONFIRMED))) {
        return false;
    }
    return true;
}

bool inktag_dfu_safe_resume_allowed(void) {
    if (!inktag_gr_bootloader_app_valid()) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_BOOT_CONFIRM,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            INKTAG_GR_APP_START, 0u, 0u);
        return false;
    }
    if (!unmodified_recovery_request()) {
        if (!inktag_gr_dfu_reject_pending()) {
            inktag_gr_dfu_reject_set(
                INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                INKTAG_OTA_REJECT_STAGE_BOOT_CONFIRM,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                INKTAG_GR_RECOVERY_SESSION_ADDRESS, 0u, 0u);
        }
        return false;
    }
    return inktag_dfu_safe_resume_clear_allowed();
}

bool inktag_dfu_safe_resume_commit(void) {
    /* The transport calls this only from its main-loop TX-complete poll.
     * Admission is read-only; guard changes or failed ACKs cannot erase the
     * controlled request and accidentally permit an ordinary timeout boot. */
    return inktag_dfu_safe_resume_allowed() && clear_controlled_recovery_request();
}
#else
bool inktag_dfu_safe_resume_allowed(void) {
    return false;
}
bool inktag_dfu_safe_resume_commit(void) {
    return false;
}
#endif

bool inktag_gr_first_display_request_pending(void) {
    inktag_gr_first_display_record_t record;
    return hal_flash_read(INKTAG_GR_FIRST_DISPLAY_ADDRESS,
                          (uint8_t *)&record, sizeof(record)) == sizeof(record) &&
           record.magic == INKTAG_GR_FIRST_DISPLAY_MAGIC &&
           record.magic_inverse == ~INKTAG_GR_FIRST_DISPLAY_MAGIC;
}

bool inktag_gr_first_display_request_consume(void) {
    static const inktag_gr_first_display_record_t consumed = {0u, 0u};
    bool was_enabled;
    uint32_t primask;
    uint32_t written;

    if (!inktag_gr_first_display_request_pending()) return true;
    if (!gr_mutation_allowed()) return false;

    /* Only clear bits in the reserved marker words. If power is lost during
     * this write the pair becomes invalid, which preserves at-most-once
     * behavior without erasing the vendor DFU-info sector. */
    primask = flash_mutation_begin(&was_enabled);
    written = hal_flash_write(INKTAG_GR_FIRST_DISPLAY_ADDRESS,
                              (const uint8_t *)&consumed,
                              sizeof(consumed));
    flash_mutation_end(was_enabled, primask);
    return written == sizeof(consumed) &&
           !inktag_gr_first_display_request_pending();
}

static inktag_recovery_result_t recovery_boot(void) {
    bool was_enabled;
    uint32_t primask = flash_mutation_begin(&was_enabled);
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result = inktag_recovery_boot(&recovery);
    flash_mutation_end(was_enabled, primask);
    return result;
}

static bool recovery_arm(uint32_t compatibility_epoch) {
    bool was_enabled;
    uint32_t primask = flash_mutation_begin(&was_enabled);
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result =
        inktag_recovery_arm_compatibility_epoch(&recovery,
                                                compatibility_epoch);
    flash_mutation_end(was_enabled, primask);
    if (result == INKTAG_RECOVERY_ROLLBACK_ERROR) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u,
            compatibility_epoch, 0u, 0u);
    } else if (result != INKTAG_RECOVERY_OK &&
               !inktag_gr_dfu_reject_pending()) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u,
            compatibility_epoch, 0u, 0u);
    }
    return result == INKTAG_RECOVERY_OK;
}

#if defined(INKTAG_SWAP_BOOTLOADER)
static bool gr_read_integrity(uint32_t address, void *data, uint32_t length) {
    return hal_flash_read(address, data, length) == length;
}

bool inktag_dfu_before_program_start(const dfu_image_info_t *candidate) {
    if (!candidate ||
        candidate->version != INKTAG_GR_OTA_COMPATIBILITY_EPOCH ||
        !inktag_gr_layout_v3_installed() ||
        !inktag_dfu_ota_admission_allowed()) return false;
    return recovery_arm(candidate->version);
}

bool inktag_dfu_before_commit(const dfu_image_info_t *candidate,
                              bool signed_firmware) {
    dfu_image_info_t stored;
    uint32_t layout[2];
    (void)signed_firmware; /* Native wrapper length only; no authentication. */
    inktag_gr_dfu_reject_clear();
    if (!candidate || candidate->pattern != DFU_IMG_INFO_PATTERN ||
        candidate->boot_info.bin_size == 0u) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY, 0u, 0u, 0u, 0u);
        return false;
    }
    if (candidate->boot_info.load_addr != INKTAG_GR_APP_START ||
        candidate->boot_info.run_addr != candidate->boot_info.load_addr ||
        candidate->version != INKTAG_GR_OTA_COMPATIBILITY_EPOCH ||
        candidate->boot_info.bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            candidate->boot_info.load_addr, 0u, 0u);
        return false;
    }
    if (!inktag_gr_app_integrity_verify(gr_read_integrity,
            candidate->boot_info.load_addr, candidate->boot_info.bin_size,
            INKTAG_GR_APP_PAYLOAD_LIMIT, candidate->boot_info.check_sum, INKTAG_GR_NATIVE_BOARD_CONTEXT,
            INKTAG_GR_LAYOUT_MARKER_MAGIC, INKTAG_GR_OTA_COMPATIBILITY_EPOCH)) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
            candidate->boot_info.load_addr, 0u, 0u);
        return false;
    }
    if (hal_flash_read(candidate->boot_info.load_addr + candidate->boot_info.bin_size,
                       (uint8_t *)&stored, sizeof(stored)) != sizeof(stored) ||
        memcmp(&stored, candidate, sizeof(stored)) != 0 ||
        hal_flash_read(candidate->boot_info.load_addr + candidate->boot_info.bin_size +
            INKTAG_GR_IMAGE_INFO_STRUCT_LEN, (uint8_t *)layout, sizeof(layout)) != sizeof(layout) ||
        layout[0] != INKTAG_GR_LAYOUT_MARKER_MAGIC ||
        layout[1] != INKTAG_GR_LAYOUT_MARKER_INVERSE) {
        inktag_gr_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_IMAGE_VERIFY, 0u, candidate->version, 0u, 0u);
        return false;
    }
    return true;
}
#endif

inktag_swap_result_t inktag_gr_swap_boot(void) {
    inktag_recovery_result_t result = recovery_boot();
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    if (result == INKTAG_RECOVERY_OK) return INKTAG_SWAP_OK;
    return INKTAG_SWAP_IO_ERROR;
}

bool inktag_gr_recovery_required(void) {
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    return inktag_recovery_status(&recovery, &state, NULL) ==
           INKTAG_RECOVERY_ENTER;
}

inktag_swap_result_t inktag_gr_swap_confirm(void) {
    bool was_enabled;
    uint32_t primask = flash_mutation_begin(&was_enabled);
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result =
        inktag_recovery_confirm_compatibility_epoch(
            &recovery, INKTAG_GR_OTA_COMPATIBILITY_EPOCH);
    flash_mutation_end(was_enabled, primask);
    if (result == INKTAG_RECOVERY_OK) return INKTAG_SWAP_OK;
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    return INKTAG_SWAP_IO_ERROR;
}

bool inktag_gr_layout_v3_installed(void) {
    uint32_t marker[2];
    if (hal_flash_read(INKTAG_GR_APP_INFO + INKTAG_GR_IMAGE_INFO_STRUCT_LEN,
                       (uint8_t *)marker, sizeof(marker)) != sizeof(marker)) {
        return false;
    }
    return marker[0] == INKTAG_GR_LAYOUT_MARKER_MAGIC &&
           marker[1] == INKTAG_GR_LAYOUT_MARKER_INVERSE;
}

bool inktag_gr_active_image_info(dfu_image_info_t *active) {
    if (!active || !inktag_gr_layout_v3_installed()) return false;
    return hal_flash_read(INKTAG_GR_APP_INFO, (uint8_t *)active,
                          sizeof(*active)) == sizeof(*active);
}

inktag_swap_result_t inktag_gr_swap_status(inktag_swap_state_t *state,
                                           uint32_t *generation) {
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t recovery_state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result = inktag_recovery_status(
        &recovery, &recovery_state, generation);
    if (state) {
        switch (recovery_state) {
            case INKTAG_RECOVERY_STATE_ARMED:
                *state = INKTAG_SWAP_STATE_TEST_PENDING;
                break;
            case INKTAG_RECOVERY_STATE_TRIAL_STARTED:
                *state = INKTAG_SWAP_STATE_TRIAL_STARTED;
                break;
            case INKTAG_RECOVERY_STATE_CONFIRMED:
                *state = INKTAG_SWAP_STATE_CONFIRMED;
                break;
            case INKTAG_RECOVERY_STATE_REQUIRED:
                *state = INKTAG_SWAP_STATE_RECOVERY_REQUIRED;
                break;
            default:
                *state = INKTAG_SWAP_STATE_NONE;
                break;
        }
    }
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    return result == INKTAG_RECOVERY_OK ? INKTAG_SWAP_OK : INKTAG_SWAP_IO_ERROR;
}

bool inktag_gr_ota_install_boot(void) {
    inktag_swap_state_t state = INKTAG_SWAP_STATE_NONE;
    inktag_swap_result_t result = inktag_gr_swap_status(&state, NULL);
    return result == INKTAG_SWAP_OK &&
           (state == INKTAG_SWAP_STATE_TEST_PENDING ||
            state == INKTAG_SWAP_STATE_TRIAL_STARTED);
}
