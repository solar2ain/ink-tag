#ifndef INKTAG_SWAP_H
#define INKTAG_SWAP_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_SWAP_METADATA_SIZE 64u

typedef bool (*inktag_swap_read_fn)(void *context, uint32_t address,
                                    void *data, uint32_t length);
typedef bool (*inktag_swap_write_fn)(void *context, uint32_t address,
                                     const void *data, uint32_t length);
typedef bool (*inktag_swap_erase_fn)(void *context, uint32_t address,
                                     uint32_t length);
typedef bool (*inktag_swap_activate_fn)(void *context,
                                        const uint8_t metadata[INKTAG_SWAP_METADATA_SIZE]);
typedef bool (*inktag_swap_clear_pending_fn)(void *context);
typedef void (*inktag_swap_watchdog_fn)(void *context);

typedef struct {
    uint32_t slot_a;
    uint32_t slot_b;
    uint32_t slot_size;
    uint32_t sector_size;
    uint32_t control_a;
    uint32_t control_b;
    uint32_t scratch;
    uint32_t layout_signature;
} inktag_swap_layout_t;

typedef struct {
    void *context;
    inktag_swap_read_fn read;
    inktag_swap_write_fn write;
    inktag_swap_erase_fn erase;
    inktag_swap_activate_fn activate;
    inktag_swap_clear_pending_fn clear_pending;
    inktag_swap_watchdog_fn feed_watchdog;
} inktag_swap_io_t;

typedef struct {
    inktag_swap_layout_t layout;
    inktag_swap_io_t io;
} inktag_swap_t;

typedef enum {
    INKTAG_SWAP_OK = 0,
    INKTAG_SWAP_RESET_REQUIRED = 1,
    INKTAG_SWAP_NOT_PENDING = 2,
    INKTAG_SWAP_BUSY = 3,
    INKTAG_SWAP_BAD_LAYOUT = 4,
    INKTAG_SWAP_IO_ERROR = 5,
    INKTAG_SWAP_METADATA_ERROR = 6,
    INKTAG_SWAP_VERIFY_ERROR = 7,
    INKTAG_SWAP_LOG_FULL = 8,
} inktag_swap_result_t;

typedef enum {
    INKTAG_SWAP_STATE_NONE = 0,
    INKTAG_SWAP_STATE_SWAPPING = 1,
    INKTAG_SWAP_STATE_TEST_PENDING = 2,
    INKTAG_SWAP_STATE_TRIAL_STARTED = 3,
    INKTAG_SWAP_STATE_CONFIRMED = 4,
    INKTAG_SWAP_STATE_RECOVERY_REQUIRED = 5,
} inktag_swap_state_t;

/* Record a verified candidate already stored in slot B without touching either
 * slot.  This is the safe entry point for XIP applications: reboot into the
 * fixed bootloader, which will resume the recorded transaction and perform the
 * exchange from RAM. */
inktag_swap_result_t inktag_swap_prepare(
    inktag_swap_t *swap,
    const uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE],
    const uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE]);

/* Start a one-image update. The candidate must already be verified in slot B.
 * The complete slot is exchanged so slot B becomes an exact rollback image. */
inktag_swap_result_t inktag_swap_install(
    inktag_swap_t *swap,
    const uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE],
    const uint8_t candidate_metadata[INKTAG_SWAP_METADATA_SIZE]);

/* Resume an interrupted swap, arm one trial boot, or revert an unconfirmed
 * trial. Call this from the bootloader before normal image selection. */
inktag_swap_result_t inktag_swap_boot(inktag_swap_t *swap);

/* Confirm the trial image after the application has initialized its BLE
 * service and started advertising. A reset before this call causes rollback. */
inktag_swap_result_t inktag_swap_confirm(inktag_swap_t *swap);

inktag_swap_result_t inktag_swap_status(inktag_swap_t *swap,
                                        inktag_swap_state_t *state,
                                        uint32_t *generation);

#endif
