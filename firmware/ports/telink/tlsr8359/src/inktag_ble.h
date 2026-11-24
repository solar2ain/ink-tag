#ifndef INKTAG_TLSR_BLE_H
#define INKTAG_TLSR_BLE_H

#include <stdbool.h>
#include <stdint.h>

#include "inktag_findmy.h"

typedef enum {
    INKTAG_BLE_NOTIFY_BEST_EFFORT = 0,
    INKTAG_BLE_NOTIFY_RELIABLE = 1,
} inktag_ble_notify_kind_t;

void inktag_ble_init(void);
void inktag_ble_process(void);
#if !INKTAG_BUILD_PRO
const uint8_t *inktag_ble_basic_name(uint8_t *length);
#endif
void inktag_ble_set_name(const uint8_t *name, uint8_t length);
void inktag_ble_apply_settings_name(void);
bool inktag_ble_apply_find_config(
    const inktag_findmy_config_t *config,
    const uint8_t selected_public_key[INKTAG_FINDMY_PUBLIC_KEY_BYTES]);
bool inktag_ble_notify(const uint8_t *data, uint16_t length,
                       inktag_ble_notify_kind_t kind);
bool inktag_ble_notifications_drained(void);
void inktag_ble_notify_diagnostics(uint8_t *last_status,
                                   uint16_t *retry_count,
                                   uint16_t *drop_count,
                                   uint8_t *pending_count);
bool inktag_ble_connected(void);
bool inktag_ble_requires_awake(void);
bool inktag_ble_advertising_started(void);
bool inktag_ble_find_window_active(void);
uint8_t *inktag_ble_mac(void);
int inktag_ble_epd_write(void *packet);
int inktag_ble_ccc_write(void *packet);

#endif
