#ifndef INKTAG_RECOVERY_H
#define INKTAG_RECOVERY_H

#include <stdbool.h>
#include <stdint.h>

typedef bool (*inktag_recovery_read_fn)(void *context, uint32_t address,
                                        void *data, uint32_t length);
typedef bool (*inktag_recovery_write_fn)(void *context, uint32_t address,
                                         const void *data, uint32_t length);
typedef bool (*inktag_recovery_erase_fn)(void *context, uint32_t address,
                                         uint32_t length);

typedef struct {
    uint32_t control_a;
    uint32_t control_b;
    uint32_t sector_size;
    uint32_t layout_signature;
} inktag_recovery_layout_t;

typedef struct {
    void *context;
    inktag_recovery_read_fn read;
    inktag_recovery_write_fn write;
    inktag_recovery_erase_fn erase;
} inktag_recovery_io_t;

typedef struct {
    inktag_recovery_layout_t layout;
    inktag_recovery_io_t io;
} inktag_recovery_t;

typedef enum {
    INKTAG_RECOVERY_STATE_NONE = 0,
    INKTAG_RECOVERY_STATE_ARMED = 1,
    INKTAG_RECOVERY_STATE_TRIAL_STARTED = 2,
    INKTAG_RECOVERY_STATE_CONFIRMED = 3,
    INKTAG_RECOVERY_STATE_REQUIRED = 4,
#if defined(INKTAG_RECOVERY_SESSION_RECORDS)
    /* Opt-in Full-only layouts may keep controlled entry and mutation state
     * in the same two-page transaction log. Old layouts keep their schema. */
    INKTAG_RECOVERY_STATE_REQUESTED = 5,
    INKTAG_RECOVERY_STATE_MUTATED = 6,
    INKTAG_RECOVERY_STATE_FACTORY_DISPLAY = 7,
    INKTAG_RECOVERY_STATE_IDLE = 8,
#endif
} inktag_recovery_state_t;

typedef enum {
    INKTAG_RECOVERY_OK = 0,
    INKTAG_RECOVERY_NOT_PENDING = 1,
    INKTAG_RECOVERY_ENTER = 2,
    INKTAG_RECOVERY_BAD_LAYOUT = 3,
    INKTAG_RECOVERY_IO_ERROR = 4,
    INKTAG_RECOVERY_VERIFY_ERROR = 5,
    INKTAG_RECOVERY_ROLLBACK_ERROR = 6,
} inktag_recovery_result_t;

/* Compatibility epochs are deliberately much coarser than release versions.
 * The common on-device representation is 32-bit, but the product contract is
 * kept at the 16-bit floor shared with fixed Recoveries that have narrower
 * signed metadata fields. */
#define INKTAG_RECOVERY_COMPATIBILITY_EPOCH_MAX 0xFFFFu

inktag_recovery_result_t inktag_recovery_arm(inktag_recovery_t *recovery);
/* Arm a signed single-slot candidate while preserving the highest
 * compatibility epoch which reached durable application confirmation.
 * Release versions are intentionally not compared: arbitrary upgrade,
 * downgrade and same-version reinstall remain valid within one epoch. */
inktag_recovery_result_t inktag_recovery_arm_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch);
/* Read-only floor check; OK is not App authentication or boot/resume admission.
 * No valid confirmed floor has the same meaning as in arm_compatibility_epoch.
 * IO failure is never treated as an empty journal. No page mutation occurs. */
inktag_recovery_result_t inktag_recovery_check_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_recovery_boot(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_confirm(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_confirm_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_recovery_status(
    inktag_recovery_t *recovery, inktag_recovery_state_t *state,
    uint32_t *generation);

#if defined(INKTAG_RECOVERY_SESSION_RECORDS)
/* Voltage, authentication and installed-App validation remain caller-owned.
 * A successful mutation mark is mandatory BEFORE the first App erase/write.
 * Every transition preserves the confirmed epoch and uses normal append/GC. */
inktag_recovery_result_t inktag_recovery_request(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_mark_mutated(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_cancel_request(inktag_recovery_t *recovery);
/* Full-only seeding refuses any existing valid record. No App boot repair. */
inktag_recovery_result_t inktag_recovery_seed_factory_display(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_consume_factory_display(inktag_recovery_t *recovery);
#endif

#endif
