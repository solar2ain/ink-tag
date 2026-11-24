#include "recovery.h"

#include <string.h>

#include "stack/ble/ble.h"
#include "inktag_tlsr_ota_policy.h"

typedef struct {
    uint16_t intervalMin;
    uint16_t intervalMax;
    uint16_t latency;
    uint16_t timeout;
} gap_periConnectParams_t;

enum {
    RECOVERY_ATT_START = 0,
    RECOVERY_GAP_PS_H,
    RECOVERY_NAME_CD_H,
    RECOVERY_NAME_DP_H,
    RECOVERY_APPEARANCE_CD_H,
    RECOVERY_APPEARANCE_DP_H,
    RECOVERY_CONN_CD_H,
    RECOVERY_CONN_DP_H,
    RECOVERY_OTA_PS_H,
    RECOVERY_OTA_CD_H,
    RECOVERY_OTA_DP_H,
    RECOVERY_OTA_CCC_H,
    RECOVERY_COMPAT_CD_H,
    RECOVERY_COMPAT_DP_H,
#if defined(INKTAG_TLSR_BOOT_BINDING)
    RECOVERY_ENTRY_CD_H,
    RECOVERY_ENTRY_DP_H,
#endif
    RECOVERY_VERSION_CD_H,
    RECOVERY_VERSION_DP_H,
    RECOVERY_ATT_END_H,
};

static const uint16_t primary_uuid = GATT_UUID_PRIMARY_SERVICE;
static const uint16_t characteristic_uuid = GATT_UUID_CHARACTER;
static const uint16_t device_name_uuid = GATT_UUID_DEVICE_NAME;
static const uint16_t gap_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const uint16_t appearance_uuid = GATT_UUID_APPEARANCE;
static const uint16_t conn_param_uuid = GATT_UUID_PERI_CONN_PARAM;
static const uint16_t ccc_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const uint16_t appearance = GAP_APPEARE_UNKNOWN;
static const gap_periConnectParams_t conn_params = {8, 12, 0, 400};
/* TLSR8359 retains only the low 32 KiB SRAM bank. The ATT table is mutable
 * stack state and must share that retained placement with the application
 * table; ordinary .data can be overwritten by the BLE controller workspace. */
static RAM uint8_t device_name[20];
static RAM uint16_t ota_ccc;
static RAM uint8_t ota_notify_enabled;
static RAM uint8_t ota_notify_pending;

#define TELINK_OTA_SERVICE_UUID 0x12,0x19,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
#define TELINK_OTA_CHAR_UUID    0x12,0x2B,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
static const uint8_t ota_service_uuid[16] = {TELINK_OTA_SERVICE_UUID};
/* Read-only pre-START compatibility proof, in the existing OTA service.
 * It is compiled policy, never a mutable Flash/Settings authority. */
#define TELINK_OTA_COMPAT_UUID 0x13,0x2B,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
#define COMPAT_LE32(value) (uint8_t)(value), (uint8_t)((value)>>8), (uint8_t)((value)>>16), (uint8_t)((value)>>24)
static const uint8_t compatibility_value[20] = {
    'I','T','C','1', COMPAT_LE32(INKTAG_APPLICATION_SIZE),
    COMPAT_LE32(INKTAG_TLSR_OTA_COMPATIBILITY_EPOCH),
    COMPAT_LE32(INKTAG_TLSR_OTA_LAYOUT_SIGNATURE),
    COMPAT_LE32(INKTAG_TLSR_OTA_BOARD_SIGNATURE),
};
#if defined(INKTAG_TLSR_BOOT_BINDING)
static const uint8_t entry_value[20] = {
    'I','A','B','1', COMPAT_LE32(INKTAG_APPLICATION_START),
    COMPAT_LE32(INKTAG_APPLICATION_SIZE), COMPAT_LE32(INKTAG_RECOVERY_START),
    COMPAT_LE32(INKTAG_RECOVERY_SIZE),
};
#endif
#undef COMPAT_LE32

static const uint8_t name_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_NAME_DP_H), U16_HI(RECOVERY_NAME_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME),
};
static const uint8_t appearance_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_APPEARANCE_DP_H),
    U16_HI(RECOVERY_APPEARANCE_DP_H), U16_LO(GATT_UUID_APPEARANCE),
    U16_HI(GATT_UUID_APPEARANCE),
};
static const uint8_t conn_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_CONN_DP_H), U16_HI(RECOVERY_CONN_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM),
};
/* WRITE is the delivery barrier for start/finish and a 20-byte sector opener;
 * ordinary 228-byte v2 data retains WRITE_WITHOUT_RSP throughput. */
static const uint8_t ota_char[] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP |
        CHAR_PROP_NOTIFY,
    U16_LO(RECOVERY_OTA_DP_H), U16_HI(RECOVERY_OTA_DP_H), TELINK_OTA_CHAR_UUID,
};
static const uint8_t compatibility_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_COMPAT_DP_H), U16_HI(RECOVERY_COMPAT_DP_H),
    TELINK_OTA_COMPAT_UUID,
};
#if defined(INKTAG_TLSR_BOOT_BINDING)
static const uint8_t entry_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_ENTRY_DP_H), U16_HI(RECOVERY_ENTRY_DP_H),
    0x14,0x2B,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
};
#endif

/* Appended read-only software identity; existing OTA/proof handles stay fixed. */
static const uint8_t version_char[] = {
    CHAR_PROP_READ, U16_LO(RECOVERY_VERSION_DP_H), U16_HI(RECOVERY_VERSION_DP_H),
    0x15,0x2B,0x0D,0x0C,0x0B,0x0A,0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x02,0x01,0x00
};

static RAM attribute_t attributes[RECOVERY_ATT_END_H];
static RAM volatile uint8_t return_tx_armed;
static RAM volatile uint8_t return_tx_confirmed;
static RAM volatile uint8_t link_connected;

static void return_tx_poll(void) {
    uint8_t irq_state = irq_disable();
    /* The pinned SDK counts BOTH software LL FIFO and hardware DMA FIFO.
     * Enqueue success, disconnect/queue clearing and elapsed time are not TX
     * completion. Only the same live connection may confirm this barrier. */
    if (return_tx_armed && link_connected && blc_ll_getTxFifoNumber() == 0u) {
        return_tx_armed = 0u;
        /* CCC callbacks may run inside SDK dispatch. Only latch transport
         * evidence here; Flash/journal checks begin in our main-loop process. */
        return_tx_confirmed = 1u;
    }
    irq_restore(irq_state);
}

static int ota_ccc_write(void *packet) {
    rf_packet_att_write_t *request = (rf_packet_att_write_t *)packet;
    return_tx_poll();
    ota_notify_enabled = request->l2capLen >= 5u && request->value == 1u;
    inktag_recovery_ota_status_value()[12] = ota_notify_enabled;
    return 0;
}

static const attribute_t attribute_template[] = {
    {RECOVERY_ATT_END_H - 1, 0, 0, 0, 0, 0},
    {7, ATT_PERMISSIONS_READ, 2, 2, (uint8_t *)&primary_uuid, (uint8_t *)&gap_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(name_char), (uint8_t *)&characteristic_uuid, (uint8_t *)name_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(device_name), (uint8_t *)&device_name_uuid, device_name, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(appearance_char), (uint8_t *)&characteristic_uuid, (uint8_t *)appearance_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(appearance), (uint8_t *)&appearance_uuid, (uint8_t *)&appearance, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(conn_char), (uint8_t *)&characteristic_uuid, (uint8_t *)conn_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(conn_params), (uint8_t *)&conn_param_uuid, (uint8_t *)&conn_params, 0},
    {RECOVERY_ATT_END_H - RECOVERY_OTA_PS_H, ATT_PERMISSIONS_READ, 2, 16, (uint8_t *)&primary_uuid, (uint8_t *)ota_service_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(ota_char), (uint8_t *)&characteristic_uuid, (uint8_t *)ota_char, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, INKTAG_RECOVERY_OTA_STATUS_SIZE,
     (uint8_t *)(ota_char + 3), NULL, inktag_recovery_ota_write},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(ota_ccc), (uint8_t *)&ccc_uuid,
     (uint8_t *)&ota_ccc, ota_ccc_write},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(compatibility_char),
     (uint8_t *)&characteristic_uuid, (uint8_t *)compatibility_char, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(compatibility_value),
     (uint8_t *)(compatibility_char + 3), (uint8_t *)compatibility_value, 0},
#if defined(INKTAG_TLSR_BOOT_BINDING)
    {0, ATT_PERMISSIONS_READ, 2, sizeof(entry_char),
     (uint8_t *)&characteristic_uuid, (uint8_t *)entry_char, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(entry_value),
     (uint8_t *)(entry_char + 3), (uint8_t *)entry_value, 0},
#endif
    {0, ATT_PERMISSIONS_READ, 2, sizeof(version_char),
     (uint8_t *)&characteristic_uuid, (uint8_t *)version_char, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(inktag_tlsr_recovery_version_info),
     (uint8_t *)(version_char + 3), (uint8_t *)inktag_tlsr_recovery_version_info, 0},
};

void inktag_recovery_att_notify_status(void) {
    ota_notify_pending = 1u;
}

void inktag_recovery_att_process(void) {
    uint8_t *status = inktag_recovery_ota_status_value();
    uint8_t irq_state;
    bool return_failed = false;
    return_tx_poll();
    if (return_tx_confirmed) {
        return_tx_confirmed = 0u;
        inktag_recovery_ota_return_transmitted();
    }
    if (!ota_notify_enabled || !ota_notify_pending) return;
    /* Witness fresh LL data before an IRQ can drain it. The 14-byte status
     * fits one default ATT notification / LL packet. */
    irq_state = irq_disable();
    status[13] = bls_att_pushNotifyData(RECOVERY_OTA_DP_H, status,
                                       INKTAG_RECOVERY_OTA_STATUS_SIZE);
    if (status[13] == BLE_SUCCESS) {
        ota_notify_pending = 0u;
        if (inktag_recovery_ota_return_waiting()) {
            if (link_connected && blc_ll_getTxFifoNumber() != 0u) return_tx_armed = 1u;
            else return_failed = true;
        }
    } else if (inktag_recovery_ota_return_waiting()) {
        return_failed = true;
    }
    irq_restore(irq_state);
    if (return_failed) inktag_recovery_ota_return_cancel();
}

void inktag_recovery_att_link_boundary(bool connected) {
    /* A terminate may have cleared the DMA FIFO already. Never infer a TX
     * completion from its zero count here. Confirmed work stays scheduled. */
    return_tx_armed = 0u;
    link_connected = connected;
    ota_notify_enabled = ota_notify_pending = ota_ccc = 0u;
    if (!return_tx_confirmed) inktag_recovery_ota_return_cancel();
}

void inktag_recovery_att_set_name(const uint8_t *name, uint8_t length) {
    if (!name || length == 0u || length > sizeof(device_name)) return;
    memset(device_name, 0, sizeof(device_name));
    memcpy(device_name, name, length);
    attributes[RECOVERY_NAME_DP_H].attrLen = length;
}

void inktag_recovery_att_init(void) {
    /* The Recovery image executes from the alternate 0x40000 boot address but
     * is linked at zero. Do not depend on cstartup's absolute .data load for
     * mutable state: construct the table from mapped read-only code/data. */
    memcpy(attributes, attribute_template, sizeof(attribute_template));
    ota_ccc = 0u;
    ota_notify_enabled = 0u;
    ota_notify_pending = 0u;
    return_tx_armed = return_tx_confirmed = link_connected = 0u;
    attributes[RECOVERY_OTA_DP_H].pAttrValue = inktag_recovery_ota_status_value();
    bls_att_setAttributeTable((uint8_t *)attributes);
}
