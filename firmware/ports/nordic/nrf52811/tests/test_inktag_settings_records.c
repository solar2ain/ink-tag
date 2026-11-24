#include <assert.h>
#include <stdio.h>

#include "inktag_settings_records.h"

int main(void) {
    uint16_t id;
    uint8_t action;
    assert(INKTAG_SETTINGS_CLEAR_IMAGES_MASK ==
        (INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DISPLAY_STATE) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_IMAGE_INDEX)));
    assert(INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK ==
        (INKTAG_SETTINGS_RESET_SETTINGS_MASK |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_NFC_BINDING)));
    assert((INKTAG_SETTINGS_CLEAR_IMAGES_MASK &
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR)) == 0u);
    assert((INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK &
        INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_IMAGE_INDEX)) == 0u);
    assert(((INKTAG_SETTINGS_CLEAR_IMAGES_MASK |
             INKTAG_SETTINGS_RESET_SETTINGS_V2_MASK) &
        (INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_BOARD_PROFILE) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_ACTIVATION) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_BOOT_DISPLAY) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_CONFIG) |
         INKTAG_SETTINGS_RECORD_BIT(INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED))) == 0u);
    assert(!inktag_settings_record_id_valid(0u));
    assert(!inktag_settings_record_id_valid(
        INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY));
    assert(inktag_settings_record_id_valid(
        INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED));
    assert(inktag_settings_record_id_valid(
        INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING));
    for (id = 1u; id <= INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED; ++id) {
        if (id == INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY) continue;
        assert(inktag_settings_record_id_valid(id));
    }

    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_ACTIVATION) == INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_FIND_KEYRING_CHUNK_12) ==
        INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_FIND_CONFIG) ==
        INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED) ==
        INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING) ==
        INKTAG_SETTINGS_LIFECYCLE_PROTECTED);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_HEALTH) == INKTAG_SETTINGS_LIFECYCLE_HEALTH);
    assert(inktag_settings_record_lifecycle(
        INKTAG_SETTINGS_RECORD_IMAGE_INDEX) ==
        INKTAG_SETTINGS_LIFECYCLE_USER);

    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_TIME,
        INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_TIME,
        INKTAG_SETTINGS_CLEAR_USER_DATA) == INKTAG_SETTINGS_RESET_KEEP);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
        INKTAG_SETTINGS_CLEAR_USER_DATA) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
        INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) == INKTAG_SETTINGS_RESET_KEEP);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
        INKTAG_SETTINGS_CLEAR_USER_DATA) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
        INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) == INKTAG_SETTINGS_RESET_KEEP);

    for (action = INKTAG_SETTINGS_CLEAR_RESET_SETTINGS;
         action <= INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET; ++action) {
        uint32_t mask = action == INKTAG_SETTINGS_CLEAR_RESET_SETTINGS ?
            INKTAG_SETTINGS_RESET_SETTINGS_MASK :
            action == INKTAG_SETTINGS_CLEAR_USER_DATA ?
                INKTAG_SETTINGS_CLEAR_USER_DATA_MASK :
            action == INKTAG_SETTINGS_CLEAR_FACTORY_RESET ?
                INKTAG_SETTINGS_FACTORY_RESET_MASK :
                INKTAG_SETTINGS_OWNER_FACTORY_RESET_MASK;
        for (id = 1u; id <= INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED; ++id) {
            bool mask_deletes =
                (mask & INKTAG_SETTINGS_RECORD_BIT(id)) != 0u;
            bool policy_deletes = inktag_settings_record_disposition(
                id, (inktag_settings_clear_action_t)action) ==
                INKTAG_SETTINGS_RESET_DELETE;
            assert(mask_deletes == policy_deletes);
        }
    }
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
        INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_KEEP);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_ACTIVATION,
        INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_FIND_CONFIG,
        INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED,
        INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY,
        INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) == INKTAG_SETTINGS_RESET_DELETE);
    assert(inktag_settings_record_disposition(
        INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY,
        INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) == INKTAG_SETTINGS_RESET_KEEP);

    for (id = INKTAG_SETTINGS_RECORD_ACTIVATION;
         id <= INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED; ++id) {
        if (inktag_settings_record_lifecycle(id) ==
            INKTAG_SETTINGS_LIFECYCLE_PROTECTED) {
            assert(inktag_settings_record_disposition(
                id, INKTAG_SETTINGS_CLEAR_RESET_SETTINGS) ==
                INKTAG_SETTINGS_RESET_KEEP);
            assert(inktag_settings_record_disposition(
                id, INKTAG_SETTINGS_CLEAR_USER_DATA) ==
                INKTAG_SETTINGS_RESET_KEEP);
            assert(inktag_settings_record_disposition(
                id, INKTAG_SETTINGS_CLEAR_FACTORY_RESET) ==
                INKTAG_SETTINGS_RESET_KEEP);
        }
    }
    puts("Settings record lifecycle tests passed.");
    return 0;
}
