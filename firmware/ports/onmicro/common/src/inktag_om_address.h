#ifndef INKTAG_OM_ADDRESS_H
#define INKTAG_OM_ADDRESS_H
#include "inktag_ble_address.h"

/* Runtime results, not a persisted record or protocol field. */
#define INKTAG_OM_ADDRESS_NVDS 0u
#define INKTAG_OM_ADDRESS_HS_LEGACY 1u
#define INKTAG_OM_ADDRESS_GENERATED 1u
#define INKTAG_OM_ADDRESS_INIT_ERROR 2u
#define INKTAG_OM_ADDRESS_READ_ERROR 3u
#define INKTAG_OM_ADDRESS_RECORD_ERROR 4u
#define INKTAG_OM_ADDRESS_ROOT_ERROR 5u
#define INKTAG_OM_ADDRESS_WRITE_ERROR 6u

/* SDK LSB-first and explicit SDK enum <-> canonical MSB-first/type.
 * These conversions never infer type from bits or modify address bits. */
bool inktag_om_address_from_sdk(const uint8_t lsb[6], uint8_t sdk_type,
                                inktag_ble_address_t *output);
bool inktag_om_address_to_sdk(const inktag_ble_address_t *address,
                              uint8_t lsb[6], uint8_t *sdk_type);

/* Preserve usable native tag1. For a confirmed missing tag1, prefer a
 * supported complete Flash UID.
 * Otherwise retain the SDK RNG fallback, and persist a legal Static Random
 * address in the same tag with write/readback and voltage checks. Both roles
 * use this path; generated address is never embedded in a generic BIN.
 * On error both outputs remain unchanged. */
uint8_t inktag_om_address_load(uint8_t nvds_init_status,
                              uint8_t lsb[6], uint8_t *sdk_type);
#if !defined(INKTAG_OM_FIXED_RECOVERY)
/* Return the byte count of the JEDEC prefix and full UID, or zero. */
uint8_t inktag_om_flash_uid(uint8_t uid[19]);
#endif

#endif
