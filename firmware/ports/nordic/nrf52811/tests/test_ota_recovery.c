#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inktag_recovery.h"

#define SECTOR_SIZE 4096u
#define CONTROL_A   0x1000u
#define CONTROL_B   0x2000u
#define FLASH_SIZE  0x3000u

typedef struct {
    uint8_t bytes[FLASH_SIZE];
    int fail_after;
    int mutations;
} fake_flash_t;

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "recovery test failed: %s\n", message);
        exit(1);
    }
}

static uint32_t test_crc32(const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (length-- > 0u) {
        crc ^= *bytes++;
        for (uint8_t bit = 0; bit < 8u; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void stamp_retired_version_floor(fake_flash_t *flash,
                                        uint32_t version) {
    /* The retired pilot used metadata format 1 at byte 17 and a uint32
     * confirmed release version at byte 20. Recompute the record CRC so this
     * is a valid old-format record, not corruption. */
    uint8_t *record = &flash->bytes[CONTROL_A + 64u];
    uint32_t crc;
    record[17] = 1u;
    memcpy(&record[20], &version, sizeof(version));
    crc = test_crc32(record, 28u);
    memcpy(&record[28], &crc, sizeof(crc));
}

static bool fake_read(void *context, uint32_t address, void *data,
                      uint32_t length) {
    fake_flash_t *flash = context;
    if (address > FLASH_SIZE || length > FLASH_SIZE - address) return false;
    memcpy(data, flash->bytes + address, length);
    return true;
}

static bool fake_write(void *context, uint32_t address, const void *data,
                       uint32_t length) {
    fake_flash_t *flash = context;
    bool cut = flash->mutations++ == flash->fail_after;
    uint32_t written = cut ? length / 2u : length;
    const uint8_t *source = data;
    for (uint32_t i = 0; i < written; i++) {
        flash->bytes[address + i] &= source[i];
    }
    return !cut;
}

static bool fake_erase(void *context, uint32_t address, uint32_t length) {
    fake_flash_t *flash = context;
    bool cut = flash->mutations++ == flash->fail_after;
    memset(flash->bytes + address, 0xFF, cut ? length / 2u : length);
    return !cut;
}

static inktag_recovery_t make_recovery(fake_flash_t *flash) {
    inktag_recovery_t recovery = {
        .layout = {
            .control_a = CONTROL_A,
            .control_b = CONTROL_B,
            .sector_size = SECTOR_SIZE,
            .layout_signature = 0x52545354u,
        },
        .io = {
            .context = flash,
            .read = fake_read,
            .write = fake_write,
            .erase = fake_erase,
        },
    };
    return recovery;
}

static void expect_state(inktag_recovery_t *recovery,
                         inktag_recovery_state_t expected_state,
                         uint32_t expected_generation) {
    inktag_recovery_state_t state = INKTAG_RECOVERY_STATE_NONE;
    uint32_t generation = 0;
    inktag_recovery_result_t result =
        inktag_recovery_status(recovery, &state, &generation);
    check(result == (expected_state == INKTAG_RECOVERY_STATE_REQUIRED
                         ? INKTAG_RECOVERY_ENTER
                         : INKTAG_RECOVERY_OK),
          "status result");
    check(state == expected_state, "status state");
    check(generation == expected_generation, "status generation");
}

static void complete_trial(inktag_recovery_t *recovery, const char *message) {
    check(inktag_recovery_arm(recovery) == INKTAG_RECOVERY_OK, message);
    check(inktag_recovery_boot(recovery) == INKTAG_RECOVERY_OK, message);
    check(inktag_recovery_confirm(recovery) == INKTAG_RECOVERY_OK, message);
}

static void complete_epoch_trial(inktag_recovery_t *recovery,
                                 uint32_t compatibility_epoch,
                                 const char *message) {
    check(inktag_recovery_arm_compatibility_epoch(
              recovery, compatibility_epoch) ==
              INKTAG_RECOVERY_OK,
          message);
    check(inktag_recovery_boot(recovery) == INKTAG_RECOVERY_OK, message);
    check(inktag_recovery_confirm_compatibility_epoch(
              recovery, compatibility_epoch) ==
              INKTAG_RECOVERY_OK,
          message);
}

int main(void) {
    fake_flash_t flash;
    memset(&flash, 0xFF, sizeof(flash));
    flash.fail_after = -1;
    flash.mutations = 0;
    inktag_recovery_t recovery = make_recovery(&flash);

    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_NOT_PENDING,
          "empty log must boot normally");
    check(inktag_recovery_arm(&recovery) == INKTAG_RECOVERY_OK, "arm trial");
    expect_state(&recovery, INKTAG_RECOVERY_STATE_ARMED, 1);
    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_OK,
          "first trial boot must run application");
    expect_state(&recovery, INKTAG_RECOVERY_STATE_TRIAL_STARTED, 1);
    check(inktag_recovery_confirm(&recovery) == INKTAG_RECOVERY_OK,
          "healthy application confirms trial");
    expect_state(&recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 1);

    check(inktag_recovery_arm(&recovery) == INKTAG_RECOVERY_OK, "arm second trial");
    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_OK,
          "second trial starts");
    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_ENTER,
          "unconfirmed reset must enter Recovery");
    expect_state(&recovery, INKTAG_RECOVERY_STATE_REQUIRED, 2);
    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_ENTER,
          "Recovery must remain available after another reset");

    check(inktag_recovery_arm(&recovery) == INKTAG_RECOVERY_OK,
          "successful recovery upload arms a new trial");
    check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_OK,
          "recovered application starts");
    check(inktag_recovery_confirm(&recovery) == INKTAG_RECOVERY_OK,
          "recovered application confirms");
    expect_state(&recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 3);

    fake_flash_t baseline = flash;
    for (int cut = 0; cut < 1; cut++) {
        fake_flash_t interrupted = baseline;
        interrupted.fail_after = cut;
        interrupted.mutations = 0;
        inktag_recovery_t interrupted_recovery = make_recovery(&interrupted);
        check(inktag_recovery_arm(&interrupted_recovery) != INKTAG_RECOVERY_OK,
              "partial record write must fail");
        expect_state(&interrupted_recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 3);
        interrupted.fail_after = -1;
        check(inktag_recovery_arm(&interrupted_recovery) == INKTAG_RECOVERY_OK,
              "arm must recover after interrupted record write");
    }

    /* Legacy/all-0xFF metadata has no epoch floor. The first epoch-aware
     * confirmation establishes epoch 0. Ordinary higher/lower release
     * versions are intentionally invisible here and both use epoch 0. */
    fake_flash_t epoch_flash;
    memset(&epoch_flash, 0xFF, sizeof(epoch_flash));
    epoch_flash.fail_after = -1;
    epoch_flash.mutations = 0;
    inktag_recovery_t epoch_recovery = make_recovery(&epoch_flash);
    complete_trial(&epoch_recovery, "prepare legacy epoch migration record");
    complete_epoch_trial(&epoch_recovery, 0u,
                         "establish confirmed epoch zero");
    expect_state(&epoch_recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 2);
    complete_epoch_trial(&epoch_recovery, 0u,
                         "same epoch lower release must remain allowed");
    complete_epoch_trial(&epoch_recovery, 0u,
                         "same epoch higher release must remain allowed");

    fake_flash_t retired_floor;
    memset(&retired_floor, 0xFF, sizeof(retired_floor));
    retired_floor.fail_after = -1;
    retired_floor.mutations = 0;
    inktag_recovery_t retired_recovery = make_recovery(&retired_floor);
    complete_trial(&retired_recovery, "prepare retired version-floor record");
    stamp_retired_version_floor(&retired_floor, 500u);
    complete_epoch_trial(&retired_recovery, 0u,
                         "retired version floor must not become an epoch");

    complete_epoch_trial(&epoch_recovery, 1u,
                         "confirmed higher epoch advances floor");
    check(inktag_recovery_arm_compatibility_epoch(&epoch_recovery, 0u) ==
              INKTAG_RECOVERY_ROLLBACK_ERROR,
          "lower compatibility epoch must be rejected");
    expect_state(&epoch_recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 5);
    check(inktag_recovery_arm_compatibility_epoch(&epoch_recovery, 2u) ==
              INKTAG_RECOVERY_OK,
          "higher epoch should arm");
    check(inktag_recovery_boot(&epoch_recovery) == INKTAG_RECOVERY_OK,
          "higher epoch trial should start");
    fake_flash_t unconfirmed_higher = epoch_flash;
    unconfirmed_higher.fail_after = 0;
    unconfirmed_higher.mutations = 0;
    inktag_recovery_t interrupted_epoch =
        make_recovery(&unconfirmed_higher);
    check(inktag_recovery_confirm_compatibility_epoch(
              &interrupted_epoch, 2u) !=
              INKTAG_RECOVERY_OK,
          "partial confirmation must not advance epoch floor");
    expect_state(&interrupted_epoch,
                 INKTAG_RECOVERY_STATE_TRIAL_STARTED, 6);
    unconfirmed_higher.fail_after = -1;
    check(inktag_recovery_boot(&interrupted_epoch) ==
              INKTAG_RECOVERY_ENTER,
          "unconfirmed higher epoch must return to Recovery");
    check(inktag_recovery_arm_compatibility_epoch(&interrupted_epoch, 0u) ==
              INKTAG_RECOVERY_ROLLBACK_ERROR,
          "old confirmed epoch must survive failed higher confirmation");
    check(inktag_recovery_arm_compatibility_epoch(&interrupted_epoch, 1u) ==
              INKTAG_RECOVERY_OK,
          "confirmed epoch must remain recoverable");
    check(inktag_recovery_arm_compatibility_epoch(
              &interrupted_epoch,
              INKTAG_RECOVERY_COMPATIBILITY_EPOCH_MAX + 1u) ==
              INKTAG_RECOVERY_BAD_LAYOUT,
          "out-of-contract epoch must be rejected");

    /* Fill page A exactly through TRIAL_STARTED. Confirming generation 43
     * must erase page B and append there. Cutting either mutation must leave
     * the full page-A record authoritative, so the next boot enters Recovery. */
    fake_flash_t rollover;
    memset(&rollover, 0xFF, sizeof(rollover));
    rollover.fail_after = -1;
    rollover.mutations = 0;
    inktag_recovery_t rollover_recovery = make_recovery(&rollover);
    for (uint32_t generation = 1; generation < 43; generation++) {
        complete_epoch_trial(&rollover_recovery, 1u,
                             "prepare epoch rollover page");
    }
    check(inktag_recovery_arm_compatibility_epoch(&rollover_recovery, 2u) ==
              INKTAG_RECOVERY_OK,
          "fill page with rollover trial arm");
    check(inktag_recovery_boot(&rollover_recovery) == INKTAG_RECOVERY_OK,
          "fill page with rollover trial start");
    expect_state(&rollover_recovery, INKTAG_RECOVERY_STATE_TRIAL_STARTED, 43);
    fake_flash_t rollover_baseline = rollover;
    for (int cut = 0; cut < 2; cut++) {
        fake_flash_t interrupted = rollover_baseline;
        interrupted.fail_after = cut;
        interrupted.mutations = 0;
        inktag_recovery_t interrupted_recovery = make_recovery(&interrupted);
        check(inktag_recovery_confirm_compatibility_epoch(
                  &interrupted_recovery, 2u) !=
                  INKTAG_RECOVERY_OK,
              "rollover erase/write interruption must fail");
        expect_state(&interrupted_recovery,
                     INKTAG_RECOVERY_STATE_TRIAL_STARTED, 43);
        interrupted.fail_after = -1;
        check(inktag_recovery_boot(&interrupted_recovery) == INKTAG_RECOVERY_ENTER,
              "interrupted rollover must enter Recovery on reboot");
        expect_state(&interrupted_recovery, INKTAG_RECOVERY_STATE_REQUIRED, 43);
        check(inktag_recovery_arm_compatibility_epoch(
                  &interrupted_recovery, 0u) ==
                  INKTAG_RECOVERY_ROLLBACK_ERROR,
          "rollover interruption must retain confirmed floor");
        check(inktag_recovery_arm_compatibility_epoch(
                  &interrupted_recovery, 1u) ==
                  INKTAG_RECOVERY_OK,
              "rollover interruption must allow confirmed epoch recovery");
    }

    for (uint32_t generation = 4; generation < 180; generation++) {
        check(inktag_recovery_arm(&recovery) == INKTAG_RECOVERY_OK,
              "journal rollover arm");
        check(inktag_recovery_boot(&recovery) == INKTAG_RECOVERY_OK,
              "journal rollover boot");
        check(inktag_recovery_confirm(&recovery) == INKTAG_RECOVERY_OK,
              "journal rollover confirm");
    }
    expect_state(&recovery, INKTAG_RECOVERY_STATE_CONFIRMED, 179);
    puts("single-slot recovery tests passed");
    return 0;
}
