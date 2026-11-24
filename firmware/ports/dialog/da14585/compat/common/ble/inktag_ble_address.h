#ifndef INKTAG_BLE_ADDRESS_H
#define INKTAG_BLE_ADDRESS_H

#include <stdbool.h>
#include <stdint.h>

#define INKTAG_BLE_ADDRESS_BYTES 6u
#define INKTAG_BLE_ADDRESS_UID_MAX_BYTES 32u
#define INKTAG_BLE_ADDRESS_PUBLIC 0u
#define INKTAG_BLE_ADDRESS_STATIC_RANDOM 1u
#define INKTAG_BLE_ADDRESS_RECORD_BYTES 11u

/* Algorithm namespace, not board_id, activation platform_id or packaging name.
 * GR5513/5515 share one; an HS9118 marking alone selects neither TL nor OM. */
#define INKTAG_BLE_ADDRESS_NS_GR551X 0x0101u
#define INKTAG_BLE_ADDRESS_NS_GR5331 0x0102u
#define INKTAG_BLE_ADDRESS_NS_OM6626 0x0201u
#define INKTAG_BLE_ADDRESS_NS_TLSR8359 0x0301u
#define INKTAG_BLE_ADDRESS_NS_NRF52832 0x0401u
#define INKTAG_BLE_ADDRESS_NS_NRF52811 0x0402u
#define INKTAG_BLE_ADDRESS_NS_DA14585 0x0501u

/* Canonical display order, most significant octet first. Ports convert SDK
 * byte order AND address type explicitly. Never reinterpret a public address
 * as static-random by changing its high bits or matching a UID hash. Neither
 * the bits nor the hash proves a stored address's type. Find addresses are not
 * ordinary device identity and must never enter this API. */
typedef struct {
    uint8_t bytes[INKTAG_BLE_ADDRESS_BYTES];
    uint8_t type;
} inktag_ble_address_t;

/* ABSENT requires a confirmed missing/erased record. Read errors, partial
 * records and failed integrity checks are ERROR, not permission to rekey. */
typedef enum {
    INKTAG_BLE_ADDRESS_ABSENT = 0,
    INKTAG_BLE_ADDRESS_PRESENT = 1,
    INKTAG_BLE_ADDRESS_ERROR = 2
} inktag_ble_address_state_t;

typedef enum {
    INKTAG_BLE_ADDRESS_REFUSED = 0,
    INKTAG_BLE_ADDRESS_INSTALLED = 1,
    INKTAG_BLE_ADDRESS_FACTORY = 2,
    INKTAG_BLE_ADDRESS_MISSING = 3
} inktag_ble_address_result_t;

bool inktag_ble_address_valid(const inktag_ble_address_t *address);

/* Pure selection, no IO, write, migration or hash fallback. Existing identity
 * wins over factory identity. A present invalid record refuses instead of
 * silently replacing it. MISSING leaves output untouched; only an explicit
 * new-Full provisioning path may then call derive_uid. Ordinary OTA/startup
 * must retain the port's installed legacy identity, including its derivation
 * if no persistent record exists. Output is unchanged on refusal or missing. */
inktag_ble_address_result_t inktag_ble_address_select(
    inktag_ble_address_state_t installed_state,
    const inktag_ble_address_t *installed,
    inktag_ble_address_state_t factory_state,
    const inktag_ble_address_t *factory,
    inktag_ble_address_t *output);

/* New-Full fallback only, when both sources were confirmed absent. The port
 * must supply a verified, stable, complete UID, not a board selector, common
 * fallback, transient RNG or advertisement name. Domain includes its NUL:
 * SHA256("INKTAG-BLE-ADDR-V1\0" || namespace_BE16 || uid_length_U8 || uid).
 * Use the first six digest bytes, force the first octet's high bits to 11,
 * and reject reserved all-zero/all-one random portions. Not a uniqueness
 * guarantee. Never change the independent activation/Find stable-ID domain.
 * Kept in a separate translation unit so factory-only ports need no SHA code.
 * Output is unchanged on error. UID/output buffers must not overlap. */
bool inktag_ble_address_derive_uid(uint16_t address_namespace,
                                 const uint8_t *uid, uint8_t uid_length,
                                 inktag_ble_address_t *output);

/* Full-only factory record, never a BLE-writable setting. Header 0x10/0x11
 * carries version 1 and public/static type, followed by canonical six bytes
 * and IEEE CRC32 (initial/final XOR FFFFFFFF) of the first seven bytes, LE.
 * No packed structs or native enum layout. Errors leave output alone.
 * Encode input and output must not overlap.
 * Backend atomicity, voltage gates and protected retention remain mandatory.
 * Kept separate from selection so factory-only SDK users need no record code.
 */
bool inktag_ble_address_record_encode(const inktag_ble_address_t *address,
                                     uint8_t output[INKTAG_BLE_ADDRESS_RECORD_BYTES]);
bool inktag_ble_address_record_decode(const uint8_t *record, uint16_t length,
                                     inktag_ble_address_t *output);

#endif
