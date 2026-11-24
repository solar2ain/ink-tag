#ifndef INKTAG_NRF_CONFIG_STORE_H
#define INKTAG_NRF_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_settings_records.h"
#include "inktag_settings_journal.h"

typedef enum {
    INKTAG_NRF_CONFIG_ADVERTISING = INKTAG_SETTINGS_RECORD_ADVERTISING,
    INKTAG_NRF_CONFIG_BOARD = INKTAG_SETTINGS_RECORD_BOARD_PROFILE,
    INKTAG_NRF_CONFIG_TIME = INKTAG_SETTINGS_RECORD_TIME,
    INKTAG_NRF_CONFIG_SCREEN = INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
    INKTAG_NRF_CONFIG_DISPLAY_STATE = INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
    INKTAG_NRF_CONFIG_DEVICE_INFO_QR = INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
    INKTAG_NRF_CONFIG_DEVICE_IDENTITY = INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
    INKTAG_NRF_CONFIG_DEVICE_SETTINGS = INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,
    INKTAG_NRF_CONFIG_ACTIVATION = INKTAG_SETTINGS_RECORD_ACTIVATION,
    INKTAG_NRF_CONFIG_FIND = INKTAG_SETTINGS_RECORD_FIND_CONFIG,
    INKTAG_NRF_CONFIG_HEALTH = INKTAG_SETTINGS_RECORD_HEALTH,
    INKTAG_NRF_CONFIG_INDICATOR = INKTAG_SETTINGS_RECORD_INDICATOR,
    INKTAG_NRF_CONFIG_BOOT_DISPLAY = INKTAG_SETTINGS_RECORD_BOOT_DISPLAY,
    INKTAG_NRF_CONFIG_NFC_BINDING = INKTAG_SETTINGS_RECORD_NFC_BINDING,
    INKTAG_NRF_CONFIG_NFC_ANTI_REPLAY = INKTAG_SETTINGS_RECORD_NFC_ANTI_REPLAY,
    INKTAG_NRF_CONFIG_IMAGE_INDEX = INKTAG_SETTINGS_RECORD_IMAGE_INDEX,
    INKTAG_NRF_CONFIG_FIND_IDENTITY = INKTAG_SETTINGS_RECORD_FIND_DERIVATION_SEED,
    INKTAG_NRF_CONFIG_RESET_PENDING = INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
    /* Retired keyring IDs15-28 never gain write permission. Read-only history
     * inspection can reject old material; nRF52811 implements no Find. */
#if INKTAG_NRF_FIND_ENABLED
    INKTAG_NRF_CONFIG_RECORD_MAX = INKTAG_SETTINGS_RECORD_MAX,
#else
    INKTAG_NRF_CONFIG_RECORD_MAX = INKTAG_NRF_CONFIG_IMAGE_INDEX,
#endif
} inktag_nrf_config_record_type_t;

bool inktag_nrf_config_store_init(void);
bool inktag_nrf_config_store_read(inktag_nrf_config_record_type_t type,
                                  void *data, uint16_t capacity,
                                  uint16_t *length);
/* Security/index callers must distinguish proven absence from unreadable or
 * damaged authority. All IDs use the existing common CPG1/CCR1 journal. */
inktag_settings_journal_read_result_t inktag_nrf_config_store_read_result(
    inktag_nrf_config_record_type_t type, void *data, uint16_t capacity,
    uint16_t *length);
bool inktag_nrf_config_store_write(inktag_nrf_config_record_type_t type,
                                   const void *data, uint16_t length);
bool inktag_nrf_config_store_delete(inktag_nrf_config_record_type_t type);
/* Frozen common scopes, one existing journal generation; no private masks. */
bool inktag_nrf_config_store_clear_images(void);
bool inktag_nrf_config_store_reset_settings(void);
bool inktag_nrf_config_store_put_atomic(inktag_nrf_config_record_type_t type,
                                       const void *data, uint16_t length);
bool inktag_nrf_config_store_time_settings(const void *time, uint16_t time_length,
                                          const void *settings, uint16_t settings_length);
bool inktag_nrf_config_store_inactive_erased(void);
bool inktag_nrf_config_store_scrub_inactive(void);
bool inktag_nrf_config_store_find_public_safe(bool first_provision);
#if INKTAG_NRF_FIND_ENABLED
bool inktag_nrf_config_store_unregister_find(void);
#endif
uint32_t inktag_nrf_config_store_last_error(void);

/* Validated SDK Bank 0 metadata, not proof of a requested OTA artifact. */
bool inktag_nrf_application_info(uint32_t *size, uint32_t *checksum);

/* A Full image seeds the current application fingerprint plus a one-shot
 * first-display request. On boot this adapter compares the seed with Nordic's
 * validated Bank 0 settings, commits any marker/fingerprint change, then
 * returns product-policy inputs. False means no automatic display is safe. */
bool inktag_nrf_boot_display_context(bool *first_display_requested,
                                     bool *ota_install_boot,
                                     bool consume_first_display);

#endif
