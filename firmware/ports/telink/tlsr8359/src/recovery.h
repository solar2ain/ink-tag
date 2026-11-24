#ifndef INKTAG_TLSR_RECOVERY_APP_H
#define INKTAG_TLSR_RECOVERY_APP_H

#include <stdint.h>
#include <stdbool.h>

#include "inktag_tlsr.h"

enum {
    INKTAG_RECOVERY_OTA_IDLE = 0,
    INKTAG_RECOVERY_OTA_RECEIVING = 1,
    INKTAG_RECOVERY_OTA_COMPLETE = 2,
    INKTAG_RECOVERY_OTA_BAD_PACKET = 3,
    INKTAG_RECOVERY_OTA_BAD_SEQUENCE = 4,
    INKTAG_RECOVERY_OTA_BAD_IMAGE = 5,
    INKTAG_RECOVERY_OTA_FLASH_ERROR = 6,
    INKTAG_RECOVERY_OTA_QUEUE_FULL = 7,
    INKTAG_RECOVERY_OTA_SAFE_RESUME_COMPLETE = 8,
    INKTAG_RECOVERY_OTA_LOW_POWER = 9,
    INKTAG_RECOVERY_OTA_UNSAFE_RESUME = 10,
};

#define INKTAG_RECOVERY_OTA_STATUS_SIZE 14u
#define INKTAG_RECOVERY_OTA_FAST_VERSION 1u
#define INKTAG_RECOVERY_OTA_FAST_MAX_BLOCKS 14u

/* One compiled software identity, shared by physical readback and read-only ATT. */
extern const uint8_t inktag_tlsr_recovery_version_info[14];

void inktag_recovery_ble_init(void);
void inktag_recovery_ble_process(void);
void inktag_recovery_ble_request_fast_connection(void);
void inktag_recovery_att_init(void);
void inktag_recovery_att_set_name(const uint8_t *name, uint8_t length);
void inktag_recovery_att_notify_status(void);
void inktag_recovery_att_process(void);
void inktag_recovery_att_link_boundary(bool connected);
bool inktag_recovery_ota_return_waiting(void);
void inktag_recovery_ota_return_transmitted(void);
void inktag_recovery_ota_return_cancel(void);
void inktag_recovery_ota_init(void);
void inktag_recovery_ota_role_ready(void);
int inktag_recovery_ota_write(void *packet);
void inktag_recovery_ota_process(void);
uint8_t *inktag_recovery_ota_status_value(void);

#endif
