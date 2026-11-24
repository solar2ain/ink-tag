#include "inktag_settings_records.h"

static bool is_find_keyring_chunk(uint16_t record_id) {
    return record_id >= INKTAG_SETTINGS_RECORD_FIND_KEYRING_CHUNK_0 &&
           record_id <= INKTAG_SETTINGS_RECORD_FIND_KEYRING_CHUNK_12;
}

bool inktag_settings_record_id_valid(uint16_t record_id) {
    return (record_id >= INKTAG_SETTINGS_RECORD_BOARD_PROFILE &&
            record_id <= INKTAG_SETTINGS_RECORD_ACTIVATION) ||
           (record_id >= INKTAG_SETTINGS_RECORD_HEALTH &&
            record_id <= INKTAG_SETTINGS_RECORD_FIND_KEYRING_MANIFEST) ||
           is_find_keyring_chunk(record_id) ||
           record_id == INKTAG_SETTINGS_RECORD_FIND_CONFIG ||
           record_id == INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED ||
           record_id == INKTAG_SETTINGS_RECORD_IMAGE_INDEX ||
           record_id == INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING;
}

inktag_settings_lifecycle_t inktag_settings_record_lifecycle(
    uint16_t record_id) {
    switch (record_id) {
        case INKTAG_SETTINGS_RECORD_ACTIVATION:
        case INKTAG_SETTINGS_RECORD_FIND_CONFIG:
        case INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED:
        case INKTAG_SETTINGS_RECORD_BOARD_PROFILE:
        case INKTAG_SETTINGS_RECORD_BOOT_DISPLAY:
        case INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY:
        case INKTAG_SETTINGS_RECORD_FIND_KEYRING_MANIFEST:
        case INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING:
            return INKTAG_SETTINGS_LIFECYCLE_PROTECTED;
        case INKTAG_SETTINGS_RECORD_HEALTH:
            return INKTAG_SETTINGS_LIFECYCLE_HEALTH;
        default:
            return is_find_keyring_chunk(record_id) ?
                INKTAG_SETTINGS_LIFECYCLE_PROTECTED :
                INKTAG_SETTINGS_LIFECYCLE_USER;
    }
}

inktag_settings_reset_disposition_t inktag_settings_record_disposition(
    uint16_t record_id, inktag_settings_clear_action_t action) {
    /* ID 9 is retired for all new writes, but an owner reset must erase any
     * pre-v2 value still present in a raw journal generation. */
    if (record_id == INKTAG_SETTINGS_RECORD_RETIRED_FIND_SINGLE_KEY &&
        action == INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET) {
        return INKTAG_SETTINGS_RESET_DELETE;
    }
    if (!inktag_settings_record_id_valid(record_id) ||
        (action != INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET &&
         inktag_settings_record_lifecycle(record_id) !=
             INKTAG_SETTINGS_LIFECYCLE_USER)) {
        return INKTAG_SETTINGS_RESET_KEEP;
    }

    switch (action) {
        case INKTAG_SETTINGS_CLEAR_RESET_SETTINGS:
            switch (record_id) {
                case INKTAG_SETTINGS_RECORD_TIME:
                case INKTAG_SETTINGS_RECORD_SCREEN_PROFILE:
                case INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY:
                case INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS:
                case INKTAG_SETTINGS_RECORD_INDICATOR:
                    return INKTAG_SETTINGS_RESET_DELETE;
                default:
                    return INKTAG_SETTINGS_RESET_KEEP;
            }
        case INKTAG_SETTINGS_CLEAR_USER_DATA:
            switch (record_id) {
                case INKTAG_SETTINGS_RECORD_DISPLAY_STATE:
                case INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR:
                case INKTAG_SETTINGS_RECORD_NFC_BINDING:
                case INKTAG_SETTINGS_RECORD_IMAGE_INDEX:
                    return INKTAG_SETTINGS_RESET_DELETE;
                default:
                    return INKTAG_SETTINGS_RESET_KEEP;
            }
        case INKTAG_SETTINGS_CLEAR_FACTORY_RESET:
            return INKTAG_SETTINGS_RESET_DELETE;
        case INKTAG_SETTINGS_CLEAR_OWNER_FACTORY_RESET:
            return (INKTAG_SETTINGS_OWNER_FACTORY_RESET_MASK &
                    INKTAG_SETTINGS_RECORD_BIT(record_id)) != 0u ?
                INKTAG_SETTINGS_RESET_DELETE : INKTAG_SETTINGS_RESET_KEEP;
        default:
            return INKTAG_SETTINGS_RESET_KEEP;
    }
}
