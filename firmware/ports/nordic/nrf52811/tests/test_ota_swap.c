#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_swap.h"

#define SECTOR_SIZE 4096u
#define SLOT_SIZE   (2u * SECTOR_SIZE)
#define SLOT_A      0x0000u
#define SLOT_B      0x2000u
#define CONTROL_A   0x4000u
#define CONTROL_B   0x5000u
#define SCRATCH     0x6000u
#define FLASH_SIZE  0x7000u

typedef struct {
    uint8_t flash[FLASH_SIZE];
    uint8_t active_metadata[INKTAG_SWAP_METADATA_SIZE];
    bool pending;
    int fail_after;
    int mutations;
    bool cut_triggered;
} fake_flash_t;

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "swap test failed: %s\n", message);
        exit(1);
    }
}

static bool in_range(uint32_t address, uint32_t length) {
    return address <= FLASH_SIZE && length <= FLASH_SIZE - address;
}

static bool cut_now(fake_flash_t *flash) {
    int operation = flash->mutations++;
    if (!flash->cut_triggered && flash->fail_after == operation) {
        flash->cut_triggered = true;
        return true;
    }
    return false;
}

static bool fake_read(void *context, uint32_t address, void *data, uint32_t length) {
    fake_flash_t *flash = context;
    if (!in_range(address, length)) return false;
    memcpy(data, &flash->flash[address], length);
    return true;
}

static bool fake_write(void *context, uint32_t address, const void *data,
                       uint32_t length) {
    fake_flash_t *flash = context;
    if (!in_range(address, length)) return false;
    bool cut = cut_now(flash);
    uint32_t written = cut && length > 1u ? length / 2u : length;
    const uint8_t *source = data;
    for (uint32_t i = 0; i < written; i++) flash->flash[address + i] &= source[i];
    return !cut;
}

static bool fake_erase(void *context, uint32_t address, uint32_t length) {
    fake_flash_t *flash = context;
    if (!in_range(address, length)) return false;
    bool cut = cut_now(flash);
    uint32_t erased = cut && length > 1u ? length / 2u : length;
    memset(&flash->flash[address], 0xFF, erased);
    return !cut;
}

static bool fake_activate(void *context,
                          const uint8_t metadata[INKTAG_SWAP_METADATA_SIZE]) {
    fake_flash_t *flash = context;
    bool cut = cut_now(flash);
    uint32_t written = cut ? INKTAG_SWAP_METADATA_SIZE / 2u
                           : INKTAG_SWAP_METADATA_SIZE;
    memcpy(flash->active_metadata, metadata, written);
    return !cut;
}

static bool fake_clear_pending(void *context) {
    fake_flash_t *flash = context;
    bool cut = cut_now(flash);
    flash->pending = false;
    return !cut;
}

static inktag_swap_t make_swap(fake_flash_t *flash) {
    inktag_swap_t swap = {
        .layout = {
            .slot_a = SLOT_A,
            .slot_b = SLOT_B,
            .slot_size = SLOT_SIZE,
            .sector_size = SECTOR_SIZE,
            .control_a = CONTROL_A,
            .control_b = CONTROL_B,
            .scratch = SCRATCH,
            .layout_signature = 0x54455354u,
        },
        .io = {
            .context = flash,
            .read = fake_read,
            .write = fake_write,
            .erase = fake_erase,
            .activate = fake_activate,
            .clear_pending = fake_clear_pending,
            .feed_watchdog = NULL,
        },
    };
    return swap;
}

static void make_metadata(uint8_t metadata[INKTAG_SWAP_METADATA_SIZE], uint8_t seed) {
    for (uint32_t i = 0; i < INKTAG_SWAP_METADATA_SIZE; i++) {
        metadata[i] = (uint8_t)(seed + i * 3u);
    }
}

static void initialize(fake_flash_t *flash, const uint8_t *old_metadata) {
    memset(flash, 0, sizeof(*flash));
    memset(flash->flash, 0xFF, sizeof(flash->flash));
    for (uint32_t i = 0; i < SLOT_SIZE; i++) {
        flash->flash[SLOT_A + i] = (uint8_t)(0x31u + i * 7u);
        flash->flash[SLOT_B + i] = (uint8_t)(0xA7u - i * 5u);
    }
    memcpy(flash->active_metadata, old_metadata, INKTAG_SWAP_METADATA_SIZE);
    flash->pending = true;
    flash->fail_after = -1;
}

static void assert_slots(const fake_flash_t *flash, const uint8_t *slot_a,
                         const uint8_t *slot_b, const uint8_t *metadata) {
    check(memcmp(&flash->flash[SLOT_A], slot_a, SLOT_SIZE) == 0,
          "slot A contents differ");
    check(memcmp(&flash->flash[SLOT_B], slot_b, SLOT_SIZE) == 0,
          "slot B contents differ");
    check(memcmp(flash->active_metadata, metadata,
                 INKTAG_SWAP_METADATA_SIZE) == 0,
          "active metadata differs");
}

static void drive_forward(fake_flash_t *flash, const uint8_t *old_metadata,
                          const uint8_t *new_metadata, bool prepare_only) {
    inktag_swap_t swap = make_swap(flash);
    for (unsigned attempt = 0; attempt < 1000u; attempt++) {
        inktag_swap_state_t state = INKTAG_SWAP_STATE_NONE;
        inktag_swap_result_t status = inktag_swap_status(&swap, &state, NULL);
        if (status == INKTAG_SWAP_NOT_PENDING || state == INKTAG_SWAP_STATE_CONFIRMED) {
            if (prepare_only) {
                (void)inktag_swap_prepare(&swap, old_metadata, new_metadata);
            } else {
                (void)inktag_swap_install(&swap, old_metadata, new_metadata);
            }
        } else if (state == INKTAG_SWAP_STATE_TRIAL_STARTED) {
            return;
        } else {
            (void)inktag_swap_boot(&swap);
        }
    }
    check(false, "forward swap did not reach trial state");
}

static void drive_rollback(fake_flash_t *flash) {
    inktag_swap_t swap = make_swap(flash);
    for (unsigned attempt = 0; attempt < 1000u; attempt++) {
        inktag_swap_state_t state = INKTAG_SWAP_STATE_NONE;
        inktag_swap_result_t status = inktag_swap_status(&swap, &state, NULL);
        check(status == INKTAG_SWAP_OK, "rollback transaction disappeared");
        if (state == INKTAG_SWAP_STATE_CONFIRMED) return;
        (void)inktag_swap_boot(&swap);
    }
    check(false, "rollback swap did not finish");
}

int main(void) {
    uint8_t old_metadata[INKTAG_SWAP_METADATA_SIZE];
    uint8_t new_metadata[INKTAG_SWAP_METADATA_SIZE];
    uint8_t old_slot[SLOT_SIZE];
    uint8_t new_slot[SLOT_SIZE];
    make_metadata(old_metadata, 0x11u);
    make_metadata(new_metadata, 0x91u);

    fake_flash_t baseline;
    initialize(&baseline, old_metadata);
    memcpy(old_slot, &baseline.flash[SLOT_A], SLOT_SIZE);
    memcpy(new_slot, &baseline.flash[SLOT_B], SLOT_SIZE);
    drive_forward(&baseline, old_metadata, new_metadata, false);
    int forward_mutations = baseline.mutations;
    assert_slots(&baseline, new_slot, old_slot, new_metadata);
    check(!baseline.pending, "pending DFU flag was not cleared");

    inktag_swap_t baseline_swap = make_swap(&baseline);
    check(inktag_swap_confirm(&baseline_swap) == INKTAG_SWAP_OK,
          "trial confirmation failed");
    check(inktag_swap_boot(&baseline_swap) == INKTAG_SWAP_OK,
          "confirmed image attempted rollback");

    for (int failure = 0; failure < forward_mutations; failure++) {
        fake_flash_t flash;
        initialize(&flash, old_metadata);
        flash.fail_after = failure;
        drive_forward(&flash, old_metadata, new_metadata, false);
        check(flash.cut_triggered, "forward power cut was not reached");
        assert_slots(&flash, new_slot, old_slot, new_metadata);
        inktag_swap_t swap = make_swap(&flash);
        check(inktag_swap_confirm(&swap) == INKTAG_SWAP_OK,
              "resumed forward swap could not be confirmed");
    }

    fake_flash_t rollback_baseline;
    initialize(&rollback_baseline, old_metadata);
    drive_forward(&rollback_baseline, old_metadata, new_metadata, false);
    rollback_baseline.mutations = 0;
    drive_rollback(&rollback_baseline);
    int rollback_mutations = rollback_baseline.mutations;
    assert_slots(&rollback_baseline, old_slot, new_slot, old_metadata);

    for (int failure = 0; failure < rollback_mutations; failure++) {
        fake_flash_t flash;
        initialize(&flash, old_metadata);
        drive_forward(&flash, old_metadata, new_metadata, false);
        flash.mutations = 0;
        flash.fail_after = failure;
        flash.cut_triggered = false;
        drive_rollback(&flash);
        check(flash.cut_triggered, "rollback power cut was not reached");
        assert_slots(&flash, old_slot, new_slot, old_metadata);
    }

    fake_flash_t prepared;
    initialize(&prepared, old_metadata);
    drive_forward(&prepared, old_metadata, new_metadata, true);
    assert_slots(&prepared, new_slot, old_slot, new_metadata);
    inktag_swap_t prepared_swap = make_swap(&prepared);
    check(inktag_swap_confirm(&prepared_swap) == INKTAG_SWAP_OK,
          "prepared swap could not be confirmed");

    printf("OTA swap tests passed: %d forward and %d rollback cut points.\n",
           forward_mutations, rollback_mutations);
    return 0;
}
