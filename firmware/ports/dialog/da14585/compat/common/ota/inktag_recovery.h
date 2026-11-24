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
/* Read-only admission check for a signed candidate. Single-slot transports
 * call this before their first application erase; the durable arm still
 * happens only after the complete publisher signature has been verified. */
inktag_recovery_result_t inktag_recovery_check_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_recovery_boot(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_confirm(inktag_recovery_t *recovery);
inktag_recovery_result_t inktag_recovery_confirm_compatibility_epoch(
    inktag_recovery_t *recovery, uint32_t compatibility_epoch);
inktag_recovery_result_t inktag_recovery_status(
    inktag_recovery_t *recovery, inktag_recovery_state_t *state,
    uint32_t *generation);

/* Read-only exact durable floor, including the latest CONFIRMED state. */
bool inktag_recovery_confirmed_epoch(inktag_recovery_t *recovery, uint32_t epoch);

#endif
