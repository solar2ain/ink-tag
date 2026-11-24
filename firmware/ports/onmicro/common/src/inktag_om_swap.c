#include "inktag_om_swap.h"
#include "inktag_code_guard.h"

#include <string.h>

#include "inktag_firmware_signature.h"
#include "inktag_recovery.h"
#include "inktag_om_ota_policy.h"
#include "inktag_version.h"
#include "om_driver.h"
#include "om_device.h"

#if defined(INKTAG_OM_FIXED_RECOVERY)
#include "inktag_om_voltage.h"
#else
#include "inktag_om_health.h"
#endif

#define INKTAG_OM_SECTOR_SIZE               0x1000u
#define INKTAG_OM_FACTORY_DEVICE_INFO_MAGIC     0x314C5746u /* "FWL1" */
#define INKTAG_OM_RECOVERY_REQUEST_MAGIC    0x3152434Fu /* "OCR1" */
#define INKTAG_OM_APPLICATION_MUTATED_MAGIC 0x31574D4Fu /* "OMW1" */
#define INKTAG_OM_MUTATION_MARKER_OFFSET    8u

#if defined(INKTAG_OM_FIXED_RECOVERY)
/* The fixed image is emitted only inside the guarded, exact 1 MiB OM6626 Full
 * layout. Keep runtime capacity probing in the application, where it gates
 * OTA entry and capability reporting, without paying for it again here. */
#define INKTAG_OM_SWAP_AVAILABLE() true
#else
#define INKTAG_OM_SWAP_AVAILABLE() inktag_om_swap_supported()
#endif

#if defined(__GNUC__)
#define OM_RECOVERY_RAM_CODE __attribute__((section(".ram_code"), noinline))
#else
#define OM_RECOVERY_RAM_CODE
#endif

static bool ota_entry_allowed(void) {
#if defined(INKTAG_OM_FIXED_RECOVERY)
    return inktag_om_voltage_operation_allowed(INKTAG_POWER_OPERATION_OTA);
#else
    return inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_OTA,
                                               true);
#endif
}

static bool flash_mutation_allowed(void) {
#if defined(INKTAG_OM_FIXED_RECOVERY)
    return inktag_om_voltage_operation_allowed(INKTAG_POWER_OPERATION_FLASH);
#else
    return inktag_om_health_operation_allowed(INKTAG_POWER_OPERATION_FLASH,
                                               true);
#endif
}

static OM_RECOVERY_RAM_CODE bool om_read(void *context, uint32_t address,
                                          void *data, uint32_t length) {
    (void)context;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_read(address, data, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static OM_RECOVERY_RAM_CODE bool om_write(void *context, uint32_t address,
                                           const void *data, uint32_t length) {
    (void)context;
    if (!flash_mutation_allowed()) return false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_unlock_all();
    drv_sfs_write(address, data, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static OM_RECOVERY_RAM_CODE bool om_erase(void *context, uint32_t address,
                                           uint32_t length) {
    (void)context;
    if (!flash_mutation_allowed()) return false;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_unlock_all();
    drv_sfs_erase(address, length);
    __DSB();
    __ISB();
    if (primask == 0u) __enable_irq();
    return true;
}

static inktag_recovery_t make_recovery(void) {
    inktag_recovery_t recovery = {
        .layout = {
            .control_a = INKTAG_OM_CONTROL_A,
            .control_b = INKTAG_OM_CONTROL_B,
            .sector_size = INKTAG_OM_SECTOR_SIZE,
            .layout_signature = INKTAG_OM_OTA_LAYOUT_SIGNATURE,
        },
        .io = {
            .context = NULL,
            .read = om_read,
            .write = om_write,
            .erase = om_erase,
        },
    };
    return recovery;
}

#if !defined(INKTAG_OM_NEXT_LAYOUT)
static bool marker_pair_matches(const uint32_t marker[2], uint32_t magic) {
    return marker[0] == magic && marker[1] == ~magic;
}

static bool request_record_read(uint32_t record[4]) {
    return om_read(NULL, INKTAG_OM_REQUEST_SECTOR, record,
                   sizeof(uint32_t) * 4u);
}

static bool marker_matches(uint32_t magic) {
    uint32_t record[4];
    return request_record_read(record) &&
           marker_pair_matches(record, magic);
}

static bool write_marker(uint32_t magic) {
    uint32_t marker[2] = {magic, ~magic};
    return om_erase(NULL, INKTAG_OM_REQUEST_SECTOR, INKTAG_OM_SECTOR_SIZE) &&
           om_write(NULL, INKTAG_OM_REQUEST_SECTOR, marker, sizeof(marker));
}

static bool clear_request(void) {
    return om_erase(NULL, INKTAG_OM_REQUEST_SECTOR, INKTAG_OM_SECTOR_SIZE);
}
#else
#if !defined(INKTAG_RECOVERY_SESSION_RECORDS)
#error "Next OM layout requires the shared session journal contract"
#endif
static bool marker_matches(uint32_t magic) {
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    inktag_recovery_result_t result = inktag_recovery_status(&recovery, &state, NULL);
    if (result != INKTAG_RECOVERY_OK && result != INKTAG_RECOVERY_ENTER)
        return false;
    return magic == INKTAG_OM_FACTORY_DEVICE_INFO_MAGIC ?
        state == INKTAG_RECOVERY_STATE_FACTORY_DISPLAY :
        magic == INKTAG_OM_RECOVERY_REQUEST_MAGIC &&
            (state == INKTAG_RECOVERY_STATE_REQUESTED ||
             state == INKTAG_RECOVERY_STATE_MUTATED);
}
static bool write_marker(uint32_t magic) {
    if (magic != INKTAG_OM_RECOVERY_REQUEST_MAGIC) return false;
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_request(&recovery) == INKTAG_RECOVERY_OK;
}
static bool clear_request(void) {
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_consume_factory_display(&recovery) == INKTAG_RECOVERY_OK;
}
#endif

#define OM_INTEGRITY_GUARD

#define INKTAG_OM_INTEGRITY_CODE OM_INTEGRITY_GUARD
#include "inktag_om_app_integrity.h"

OM_INTEGRITY_GUARD static bool verify_candidate_integrity(uint32_t image_size,
    uint32_t release_version, uint32_t *compatibility_epoch) {
    const uint8_t *body = (const uint8_t *)INKTAG_OM_APP_XIP_ADDRESS;
    inktag_om_app_integrity_t descriptor = {0};
    if (image_size > INKTAG_OM_SLOT_SIZE ||
        !om_app_integrity_locate(body, image_size,
            INKTAG_OM_OTA_BOARD_SIGNATURE, INKTAG_OM_OTA_LAYOUT_SIGNATURE,
            &descriptor) || descriptor.release != release_version ||
        descriptor.body_bytes + INKTAG_OM_APP_INTEGRITY_BYTES >
            INKTAG_OM_SLOT_SIZE - INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES) return false;
    if (!om_app_launch_valid(body, descriptor.body_bytes, INKTAG_OM_APP_XIP_ADDRESS))
        return false;
    uint32_t unsigned_size = descriptor.body_bytes + INKTAG_OM_APP_INTEGRITY_BYTES;
    if (image_size != unsigned_size) {
        /* New Recovery checks native wrapper format/context only. Pro's normal
         * producer still signs this exact body+IKCI for old signature Recovery. */
        inktag_firmware_signature_trailer_t trailer;
        if (image_size - unsigned_size != INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES)
            return false;
        memcpy(&trailer, body + unsigned_size, sizeof(trailer));
        if (trailer.magic != INKTAG_FIRMWARE_SIGNATURE_MAGIC ||
            trailer.version != INKTAG_FIRMWARE_SIGNATURE_VERSION ||
            trailer.algorithm != INKTAG_FIRMWARE_SIGNATURE_ECDSA_P256 ||
            trailer.trailer_size != INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES ||
            trailer.application_size != unsigned_size ||
            trailer.compatibility_epoch != descriptor.epoch ||
            trailer.signed_context.board_signature != descriptor.board ||
            trailer.signed_context.layout_signature != descriptor.layout ||
            trailer.signed_context.verifier_signature != INKTAG_OM_OTA_VERIFIER_SIGNATURE ||
            trailer.signed_context.release_version != descriptor.release)
            return false;
    }
    *compatibility_epoch = descriptor.epoch;
    return true;
}

#if defined(INKTAG_OM_FIXED_RECOVERY)
OM_INTEGRITY_GUARD bool inktag_om_swap_installed_app_valid(void) {
    inktag_om_app_integrity_t descriptor = {0};
    if (!om_app_integrity_locate((const uint8_t *)INKTAG_OM_APP_XIP_ADDRESS,
            INKTAG_OM_SLOT_SIZE, INKTAG_OM_OTA_BOARD_SIGNATURE,
            INKTAG_OM_OTA_LAYOUT_SIGNATURE, &descriptor) ||
        descriptor.body_bytes + INKTAG_OM_APP_INTEGRITY_BYTES >
            INKTAG_OM_SLOT_SIZE - INKTAG_FIRMWARE_SIGNATURE_TRAILER_BYTES) return false;
    uint32_t entry;
    memcpy(&entry, (const void *)(INKTAG_OM_LAUNCH_RECORD_XIP + 4u), sizeof(entry));
    if ((entry & 1u) == 0u || (entry & ~1u) < INKTAG_OM_APP_XIP_ADDRESS ||
        (entry & ~1u) >= INKTAG_OM_APP_XIP_ADDRESS + descriptor.body_bytes)
        return false;
    inktag_recovery_t recovery = make_recovery();
    if (inktag_recovery_check_compatibility_epoch(&recovery, descriptor.epoch) !=
        INKTAG_RECOVERY_OK) return false;
    return true;
}
#endif

#if defined(INKTAG_OM_FIXED_RECOVERY)
bool inktag_om_swap_unmodified_recovery(void) {
#if defined(INKTAG_OM_NEXT_LAYOUT)
    if (!INKTAG_OM_SWAP_AVAILABLE()) return false;
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    return inktag_recovery_status(&recovery, &state, NULL) ==
               INKTAG_RECOVERY_ENTER &&
           state == INKTAG_RECOVERY_STATE_REQUESTED;
#else
    return false;
#endif
}
#endif

bool inktag_om_swap_supported(void) {
    return drv_sfs_capacity() >= INKTAG_OM_FLASH_REQUIRED_SIZE;
}

inktag_om_recovery_request_result_t inktag_om_recovery_request(void) {
    if (!INKTAG_OM_SWAP_AVAILABLE()) {
        return INKTAG_OM_RECOVERY_REQUEST_UNSUPPORTED;
    }
    if (!ota_entry_allowed() ||
        !write_marker(INKTAG_OM_RECOVERY_REQUEST_MAGIC)) {
        return INKTAG_OM_RECOVERY_REQUEST_LOW_POWER;
    }
    return INKTAG_OM_RECOVERY_REQUEST_OK;
}

bool inktag_om_recovery_requested(void) {
    return INKTAG_OM_SWAP_AVAILABLE() &&
           marker_matches(INKTAG_OM_RECOVERY_REQUEST_MAGIC);
}

bool inktag_om_swap_cancel_unmodified_recovery(void) {
#if defined(INKTAG_OM_NEXT_LAYOUT)
    /* The caller independently authenticates the untouched App. Common only
     * permits REQUESTED -> IDLE and cannot discard MUTATED/trial state. */
    if (!INKTAG_OM_SWAP_AVAILABLE()) return false;
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_cancel_request(&recovery) == INKTAG_RECOVERY_OK;
#else
    uint32_t record[4];
    if (!INKTAG_OM_SWAP_AVAILABLE() || !request_record_read(record) ||
        !marker_pair_matches(record, INKTAG_OM_RECOVERY_REQUEST_MAGIC) ||
        record[2] != UINT32_MAX || record[3] != UINT32_MAX) {
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
    return clear_request();
#endif
}

bool inktag_om_swap_mark_application_mutated(void) {
#if defined(INKTAG_OM_NEXT_LAYOUT)
    if (!INKTAG_OM_SWAP_AVAILABLE()) return false;
    inktag_recovery_t recovery = make_recovery();
    return inktag_recovery_mark_mutated(&recovery) == INKTAG_RECOVERY_OK;
#else
    uint32_t address = INKTAG_OM_REQUEST_SECTOR +
                       INKTAG_OM_MUTATION_MARKER_OFFSET;
    uint32_t record[4];
    if (!INKTAG_OM_SWAP_AVAILABLE() || !request_record_read(record) ||
        !marker_pair_matches(record, INKTAG_OM_RECOVERY_REQUEST_MAGIC)) {
        return false;
    }
    if (marker_pair_matches(&record[2],
                            INKTAG_OM_APPLICATION_MUTATED_MAGIC)) {
        return true;
    }
    if (record[2] != UINT32_MAX || record[3] != UINT32_MAX) return false;
    uint32_t marker[2] = {
        INKTAG_OM_APPLICATION_MUTATED_MAGIC,
        ~INKTAG_OM_APPLICATION_MUTATED_MAGIC,
    };
    return om_write(NULL, address, marker, sizeof(marker)) &&
           request_record_read(record) &&
           marker_pair_matches(&record[2],
                               INKTAG_OM_APPLICATION_MUTATED_MAGIC);
#endif
}

inktag_swap_result_t inktag_om_swap_prepare_candidate(uint32_t image_size,
                                                       uint32_t release_version,
                                                       uint32_t image_crc32) {
    (void)image_crc32;
#if !defined(INKTAG_OM_FIXED_RECOVERY)
    if (!INKTAG_OM_SWAP_AVAILABLE() || image_size == 0u ||
        image_size > INKTAG_OM_SLOT_SIZE) {
        return INKTAG_SWAP_BAD_LAYOUT;
    }
#endif
    uint32_t compatibility_epoch;
    if (!verify_candidate_integrity(image_size, release_version,
                                    &compatibility_epoch)) {
        return INKTAG_SWAP_VERIFY_ERROR;
    }
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result =
        inktag_recovery_arm_compatibility_epoch(&recovery,
                                                compatibility_epoch);
    if (result == INKTAG_RECOVERY_ROLLBACK_ERROR ||
        result == INKTAG_RECOVERY_BAD_LAYOUT) {
        return INKTAG_SWAP_VERIFY_ERROR;
    }
    if (result != INKTAG_RECOVERY_OK) return INKTAG_SWAP_IO_ERROR;
#if !defined(INKTAG_OM_NEXT_LAYOUT)
    /* Keep Recovery sticky until a complete image and durable trial record
     * both exist. A failed clear is reported to the DFU client and the next
     * reset remains in Recovery rather than attempting an untracked image. */
    if (!clear_request()) return INKTAG_SWAP_IO_ERROR;
#endif
    return INKTAG_SWAP_RESET_REQUIRED;
}

inktag_swap_result_t inktag_om_swap_boot(inktag_swap_watchdog_fn progress,
                                         void *context) {
    (void)progress;
    (void)context;
    if (!INKTAG_OM_SWAP_AVAILABLE()) return INKTAG_SWAP_BAD_LAYOUT;
    if (inktag_om_recovery_requested()) return INKTAG_SWAP_BUSY;
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result = inktag_recovery_boot(&recovery);
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    return result == INKTAG_RECOVERY_OK ? INKTAG_SWAP_OK : INKTAG_SWAP_IO_ERROR;
}

inktag_swap_result_t inktag_om_swap_confirm(void) {
    if (!INKTAG_OM_SWAP_AVAILABLE()) return INKTAG_SWAP_BAD_LAYOUT;
    inktag_recovery_t recovery = make_recovery();
    inktag_recovery_result_t result =
        inktag_recovery_confirm_compatibility_epoch(
            &recovery, INKTAG_OM_OTA_COMPATIBILITY_EPOCH);
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    return result == INKTAG_RECOVERY_OK ? INKTAG_SWAP_OK : INKTAG_SWAP_IO_ERROR;
}

inktag_swap_result_t inktag_om_swap_status(inktag_swap_state_t *state,
                                           uint32_t *generation) {
    if (!INKTAG_OM_SWAP_AVAILABLE()) return INKTAG_SWAP_BAD_LAYOUT;
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
    if (inktag_om_recovery_requested()) {
        if (state) *state = INKTAG_SWAP_STATE_RECOVERY_REQUIRED;
        return INKTAG_SWAP_BUSY;
    }
    if (result == INKTAG_RECOVERY_NOT_PENDING) return INKTAG_SWAP_NOT_PENDING;
    if (result == INKTAG_RECOVERY_ENTER) return INKTAG_SWAP_BUSY;
    return result == INKTAG_RECOVERY_OK ? INKTAG_SWAP_OK : INKTAG_SWAP_IO_ERROR;
}

bool inktag_om_swap_factory_device_info_requested(void) {
    return INKTAG_OM_SWAP_AVAILABLE() &&
        marker_matches(INKTAG_OM_FACTORY_DEVICE_INFO_MAGIC);
}

bool inktag_om_swap_finish_factory_device_info_request(void) {
    return inktag_om_swap_factory_device_info_requested() && clear_request();
}

void inktag_om_swap_enter_fixed_boot(void) {
    /* drv_pmu_force_reboot() is only a Cortex-M system reset on OM6626. The
     * BLE SRAM/radio domain can survive it and leave the next role unable to
     * advertise. Wake from the vendor reboot-sleep path instead: it powers
     * down all SRAM (including BLE SRAM) and restarts through ROM/MBR. VAL1 is
     * independent of evt_timer, which owns PMU timer compare VAL0. */
    uint32_t wake_tick = drv_pmu_timer_cnt_get() + PMU_TIMER_MS2TICK(10u);
    drv_pmu_32k_enable_in_deep_sleep(true);
    (void)drv_pmu_timer_control(PMU_TIMER_TRIG_VAL1,
                                PMU_TIMER_CONTROL_SET_TIMER_VAL,
                                (void *)wake_tick);
    drv_pmu_sleep_enter(PM_STATUS_DEEP_SLEEP, true);
    while (1) { }
}
