#ifndef INKTAG_NRF_CONFIG_STORE_H
#define INKTAG_NRF_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_settings_records.h"
#include "inktag_settings_journal.h"

typedef enum {
    INKTAG_NRF_CONFIG_BOARD = INKTAG_SETTINGS_RECORD_BOARD_PROFILE,
    INKTAG_NRF_CONFIG_TIME = INKTAG_SETTINGS_RECORD_TIME,
    INKTAG_NRF_CONFIG_SCREEN = INKTAG_SETTINGS_RECORD_SCREEN_PROFILE,
    INKTAG_NRF_CONFIG_DISPLAY_STATE = INKTAG_SETTINGS_RECORD_DISPLAY_STATE,
    INKTAG_NRF_CONFIG_DEVICE_INFO_QR = INKTAG_SETTINGS_RECORD_DEVICE_INFO_QR,
    INKTAG_NRF_CONFIG_DEVICE_IDENTITY = INKTAG_SETTINGS_RECORD_DEVICE_IDENTITY,
    INKTAG_NRF_CONFIG_DEVICE_SETTINGS = INKTAG_SETTINGS_RECORD_DEVICE_SETTINGS,
    INKTAG_NRF_CONFIG_HEALTH = INKTAG_SETTINGS_RECORD_HEALTH,
    INKTAG_NRF_CONFIG_INDICATOR = INKTAG_SETTINGS_RECORD_INDICATOR,
    INKTAG_NRF_CONFIG_BOOT_DISPLAY = INKTAG_SETTINGS_RECORD_BOOT_DISPLAY,
    INKTAG_NRF_CONFIG_CONTENT_CLEAR_PENDING = INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
    /* Keyring records 15-28 are intentionally excluded. nRF52832 owns them in
     * its physically separate keyring journal; nRF52811 implements no Find. */
    INKTAG_NRF_CONFIG_RECORD_MAX = INKTAG_SETTINGS_RECORD_OWNER_RESET_PENDING,
} inktag_nrf_config_record_type_t;

bool inktag_nrf_config_store_init(void);
bool inktag_nrf_config_store_read(inktag_nrf_config_record_type_t type,
                                  void *data, uint16_t capacity,
                                  uint16_t *length);
inktag_settings_journal_read_result_t inktag_nrf_config_store_read_result(
    inktag_nrf_config_record_type_t type, void *data, uint16_t capacity,
    uint16_t *length);
/* Bounded serializer reads avoid a second full QR buffer on the 2 KiB stack. */
bool inktag_nrf_config_store_read_part(inktag_nrf_config_record_type_t type,
    uint16_t offset, void *data, uint16_t length, uint16_t *record_length);
bool inktag_nrf_config_store_write(inktag_nrf_config_record_type_t type,
                                   const void *data, uint16_t length);
bool inktag_nrf_config_store_delete(inktag_nrf_config_record_type_t type);
/* One generation switch; never deletes factory, activation or health records.
 * ID32 removal is reserved for completion of a durable content-clear intent. */
bool inktag_nrf_config_store_clear_records(uint32_t mask);
/* Existing journal generation switch with a replacement for mixed ID4.
 * The caller retains either display preferences or the image replay state. */
bool inktag_nrf_config_store_cleanup_display(uint32_t mask,
                                             const void *data, uint16_t length);
/* User preferences and replay publish together during either lifecycle scope. */
bool inktag_nrf_config_store_cleanup_user(uint32_t mask,
    const void *user, uint16_t user_length, const void *replay, uint16_t replay_length);
uint32_t inktag_nrf_config_store_last_error(void);
/* Real native Secure DFU metadata, never the App's advertised version. */
bool inktag_nrf_pin_epoch_ready(void);
/* Fresh byte readback through the existing journal scratch buffer. This is
 * synchronous main-context only, after a mutation has completely returned. */
bool inktag_nrf_config_store_read_matches(inktag_nrf_config_record_type_t type,
                                         const void *data,uint16_t length);

/* A Full image seeds the current application fingerprint plus a one-shot
 * first-display request. On boot this adapter compares the seed with Nordic's
 * validated Bank 0 settings, commits any marker/fingerprint change, then
 * returns product-policy inputs. False means no automatic display is safe. */
bool inktag_nrf_boot_display_context(bool *first_display_requested,
                                     bool *ota_install_boot);

#endif
