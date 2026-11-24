#ifndef INKTAG_GR_BLE_ADDRESS_H
#define INKTAG_GR_BLE_ADDRESS_H

#include "inktag_ble_address.h"

/* Vendor factory domain, deliberately outside ordinary Settings clear maps. */
#define INKTAG_GR_BLE_ADDRESS_NVDS_TAG 0x5212u

/* Initialize an absent/invalid optional record: keep confirmed public/legacy
 * SDK identity, otherwise complete hardware UID compact, hardware RNG last.
 * Persist and read back once before use; temporary SDK random is not factory.
 * Valid typed identity wins; true NVDS errors never trigger erase or retries.
 * The same code and typed record are used by App and Common Recovery. */
bool inktag_gr_ble_address_init(void);

/* Ordinary identity only: call before names, Find snapshots or advertising.
 * SDK bytes are LSB first; common bytes are canonical MSB first. A failed
 * read leaves output unchanged. This does not establish persistence authority. */
bool inktag_gr_ble_address_read_current(inktag_ble_address_t *output);

/* Apply a separately authorized, typed identity. Equal public addresses do
 * not invoke the SDK setter (it writes NVDS). Changing a public address here
 * is refused: Full must provision its vendor record before stack startup.
 * Static-random uses the SDK runtime path and verifies exact bytes AND type.
 * Failure is unconfirmed, not permission to retry/rekey. No persistence IO. */
bool inktag_gr_ble_address_apply(const inktag_ble_address_t *address);

#endif
