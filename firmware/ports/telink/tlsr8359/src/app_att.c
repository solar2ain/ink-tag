#include "main.h"

#include <string.h>
#include "inktag_version.h"
#include "stack/ble/ble.h"

typedef struct {
    uint16_t intervalMin;
    uint16_t intervalMax;
    uint16_t latency;
    uint16_t timeout;
} gap_periConnectParams_t;

static const uint16_t primary_uuid = GATT_UUID_PRIMARY_SERVICE;
static const uint16_t characteristic_uuid = GATT_UUID_CHARACTER;
static const uint16_t ccc_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const uint16_t device_name_uuid = GATT_UUID_DEVICE_NAME;
static const uint16_t gap_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const uint16_t appearance_uuid = GATT_UUID_APPEARANCE;
static const uint16_t conn_param_uuid = GATT_UUID_PERI_CONN_PARAM;
static const uint16_t gatt_uuid = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const uint16_t service_change_uuid = GATT_UUID_SERVICE_CHANGE;
static const uint16_t appearance = GAP_APPEARE_UNKNOWN;
static const gap_periConnectParams_t conn_params = {8, 24, 0, 400};
/* The TLSR8359 deep-retention mode keeps only the low 32 KiB SRAM bank.
 * Attribute values and the table itself are mutable BLE state: placing them
 * in ordinary .data/.bss made the scan-response name survive while the GATT
 * Device Name reverted to the link-time factory placeholder after sleep. */
static RAM uint16_t service_change[2];
static RAM uint8_t service_change_ccc[2];
static RAM uint8_t device_name[20] = "IKT_TL_0000";
static RAM uint8_t inktag_value;
static RAM uint8_t inktag_ccc[2];
static RAM uint8_t app_version[] = INKTAG_VERSION_GATT_BYTES;

#define INKTAG_SERVICE_UUID 0xEC,0x5A,0x67,0x1C,0xC1,0xB6,0x46,0xFB,0x8D,0x91,0x28,0xD8,0x01,0x00,0x75,0x62
#define INKTAG_EPD_UUID     0xEC,0x5A,0x67,0x1C,0xC1,0xB6,0x46,0xFB,0x8D,0x91,0x28,0xD8,0x02,0x00,0x75,0x62
#define INKTAG_VERSION_UUID 0xEC,0x5A,0x67,0x1C,0xC1,0xB6,0x46,0xFB,0x8D,0x91,0x28,0xD8,0x03,0x00,0x75,0x62
static const uint8_t inktag_service_uuid[16] = {INKTAG_SERVICE_UUID};
static const uint8_t inktag_epd_uuid[16] = {INKTAG_EPD_UUID};
static const uint8_t inktag_version_uuid[16] = {INKTAG_VERSION_UUID};

static const uint8_t name_char[] = {CHAR_PROP_READ, U16_LO(GenericAccess_DeviceName_DP_H),
    U16_HI(GenericAccess_DeviceName_DP_H), U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)};
static const uint8_t appearance_char[] = {CHAR_PROP_READ, U16_LO(GenericAccess_Appearance_DP_H),
    U16_HI(GenericAccess_Appearance_DP_H), U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)};
static const uint8_t conn_char[] = {CHAR_PROP_READ, U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)};
static const uint8_t change_char[] = {CHAR_PROP_INDICATE, U16_LO(GenericAttribute_ServiceChanged_DP_H),
    U16_HI(GenericAttribute_ServiceChanged_DP_H), U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)};
static const uint8_t inktag_char[] = {CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY,
    U16_LO(INKTAG_EPD_DP_H), U16_HI(INKTAG_EPD_DP_H), INKTAG_EPD_UUID};
static const uint8_t version_char[] = {CHAR_PROP_READ, U16_LO(INKTAG_VERSION_DP_H),
    U16_HI(INKTAG_VERSION_DP_H), INKTAG_VERSION_UUID};

static RAM attribute_t attributes[] = {
    {ATT_END_H - 1, 0, 0, 0, 0, 0},
    {7, ATT_PERMISSIONS_READ, 2, 2, (uint8_t *)&primary_uuid, (uint8_t *)&gap_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(name_char), (uint8_t *)&characteristic_uuid, (uint8_t *)name_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(device_name), (uint8_t *)&device_name_uuid, device_name, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(appearance_char), (uint8_t *)&characteristic_uuid, (uint8_t *)appearance_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(appearance), (uint8_t *)&appearance_uuid, (uint8_t *)&appearance, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(conn_char), (uint8_t *)&characteristic_uuid, (uint8_t *)conn_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(conn_params), (uint8_t *)&conn_param_uuid, (uint8_t *)&conn_params, 0},
    {4, ATT_PERMISSIONS_READ, 2, 2, (uint8_t *)&primary_uuid, (uint8_t *)&gatt_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(change_char), (uint8_t *)&characteristic_uuid, (uint8_t *)change_char, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(service_change), (uint8_t *)&service_change_uuid, (uint8_t *)service_change, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(service_change_ccc), (uint8_t *)&ccc_uuid, service_change_ccc, 0},
    {6, ATT_PERMISSIONS_READ, 2, 16, (uint8_t *)&primary_uuid, (uint8_t *)inktag_service_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(inktag_char), (uint8_t *)&characteristic_uuid, (uint8_t *)inktag_char, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(inktag_value), (uint8_t *)inktag_epd_uuid, &inktag_value, inktag_ble_epd_write},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(inktag_ccc), (uint8_t *)&ccc_uuid, inktag_ccc, inktag_ble_ccc_write},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(version_char), (uint8_t *)&characteristic_uuid, (uint8_t *)version_char, 0},
    {0, ATT_PERMISSIONS_READ, 16, sizeof(app_version), (uint8_t *)inktag_version_uuid, app_version, 0},
};

void inktag_att_set_name(const uint8_t *name, uint8_t length) {
    if (!name || length == 0u || length > sizeof(device_name)) return;
    memset(device_name, 0, sizeof(device_name));
    memcpy(device_name, name, length);
    attributes[GenericAccess_DeviceName_DP_H].attrLen = length;
}

void my_att_init(void) { bls_att_setAttributeTable((uint8_t *)attributes); }
