#include "inktag_tlsr_identity.h"

#include <stdbool.h>
#include <string.h>

#include "drivers.h"
#include "drivers/8258/spi_i.h"
#include "app_config.h"
#include "inktag_tlsr.h"
#include "inktag_ble_address.h"
#include "inktag_tlsr_power.h"

static _attribute_data_retention_ uint8_t s_address_type;
/* Actual binary capability required in BOTH members of a typed Full base.
 * The reference below keeps the marker through section garbage collection. */
static const char s_address_contract[] = "INKTAG-TL-ADDR1R\0INKTAG-TL-AUTO1";

static _attribute_ram_code_ _attribute_no_inline_ void
read_flash_unique_id(uint8_t uid[16]) {
    uint8_t irq_state = irq_disable();
    uint8_t index;

    /* 0x4b plus four dummy address/mode bytes is supported by the qualified
     * Puya P25Q40SH.  Keep this read-only helper local: the SDK's equivalent
     * implementation is compiled out in this minimal driver build. */
    mspi_high();
    sleep_us(1);
    mspi_low();
    mspi_write(FLASH_GD_PUYA_READ_UID_CMD);
    mspi_wait();
    for (index = 0u; index < 4u; ++index) {
        mspi_write(0x00u);
        mspi_wait();
    }
    mspi_write(0x00u);
    mspi_wait();
    mspi_ctrl_write(0x0au);
    mspi_wait();
    for (index = 0u; index < 16u; ++index) {
        uid[index] = mspi_get();
        mspi_wait();
    }
    mspi_high();
    irq_restore(irq_state);
}

static bool all_value(const uint8_t *data, uint8_t length, uint8_t value) {
    while (length-- != 0u) {
        if (*data++ != value) return false;
    }
    return true;
}

/* Explicit clean-BIN intent, separate from the first-display byte at 10C.
 * A5 -> A4 commits the six-byte address seed; A4 -> 00 commits both records.
 * Only 1->0 programming is used; never erase Factory during initialization. */
#define ADDRESS_INIT_MARKER 0x7F10Du

static bool identity_program(uint32_t at, const uint8_t *data, uint8_t length) {
    uint8_t current[INKTAG_TELINK_TYPED_ADDRESS_BYTES], i;
    flash_read_page(at, length, current);
    if (!memcmp(current, data, length)) return true;
    for (i=0u; i<length; ++i)
        if ((current[i]&data[i])!=data[i]) return false;
    if (!inktag_tlsr_flash_write_page(at,length,data,INKTAG_POWER_OPERATION_FLASH))
        return false;
    flash_read_page(at,length,current);
    return !memcmp(current,data,length);
}

static bool identity_initialize(void) {
    uint8_t marker, legacy[6], record[INKTAG_BLE_ADDRESS_RECORD_BYTES], i;
    inktag_ble_address_t address;
    flash_read_page(ADDRESS_INIT_MARKER,1u,&marker);
    if (marker!=0xA5u && marker!=0xA4u) return true;
    flash_read_page(INKTAG_TELINK_MAC_SECTOR,sizeof(legacy),legacy);
    if (marker==0xA5u && all_value(legacy,sizeof(legacy),0xFFu)) {
        /* Retained addresses and partly programmed seeds keep precedence.
         * Only a new clean image with erased address bytes derives a value.
         * Both native entry points initialize the SDK RNG before BLE. */
        uint8_t uid[16];
        if (inktag_tlsr_identity_read_uid(uid) &&
            inktag_ble_address_derive_uid_compact(INKTAG_BLE_ADDRESS_NS_TLSR8359,
                                         uid,sizeof(uid),&address)) {
            for(i=0u;i<6u;++i) legacy[i]=address.bytes[5u-i];
        } else {
            generateRandomNum(sizeof(legacy),legacy);
            legacy[0]=(legacy[0]&0xFCu)|1u; /* Nonreserved random part. */
            legacy[5]=(legacy[5]&0x3Fu)|0xC0u;
        }
    }
    for(i=0u;i<6u;++i) address.bytes[i]=legacy[5u-i];
    address.type=INKTAG_BLE_ADDRESS_STATIC_RANDOM;
    if (!inktag_ble_address_record_encode(&address,record)) return false;
    if (marker==0xA5u) {
        if (!identity_program(INKTAG_TELINK_MAC_SECTOR,legacy,sizeof(legacy)))
            return false;
        marker=0xA4u;
        if (!identity_program(ADDRESS_INIT_MARKER,&marker,1u)) return false;
    }
    if (!identity_program(INKTAG_TELINK_TYPED_ADDRESS,record,sizeof(record)))
        return false;
    marker=0u;
    return identity_program(ADDRESS_INIT_MARKER,&marker,1u);
}

static bool flash_uid_unavailable(const uint8_t uid[16]) {
    uint8_t index;
    bool sdk_no_uid = true;

    if (all_value(uid, 16u, 0x00u) || all_value(uid, 16u, 0xFFu))
        return true;
    /* Telink's B85 flash_read_mid_uid_with_check() treats this repeating
     * response as a device without a usable Flash UID. */
    for (index = 0u; index < 16u; ++index) {
        if (uid[index] != ((index & 1u) == 0u ? 0x51u : 0x01u)) {
            sdk_no_uid = false;
            break;
        }
    }
    return sdk_no_uid;
}

bool inktag_tlsr_identity_read_uid(uint8_t uid[16]) {
    uint8_t confirmation[16];
    if (!uid) return false;
    memset(uid, 0, 16u);
    read_flash_unique_id(uid);
    if (flash_uid_unavailable(uid)) return false;
    memset(confirmation, 0, sizeof(confirmation));
    read_flash_unique_id(confirmation);
    if (flash_uid_unavailable(confirmation) ||
        memcmp(uid, confirmation, sizeof(confirmation)) != 0) {
        memset(uid, 0, 16u);
        memset(confirmation, 0, sizeof(confirmation));
        return false;
    }
    memset(confirmation, 0, sizeof(confirmation));
    return true;
}

static inline __attribute__((always_inline)) void legacy_identity_init(uint8_t public_mac[6],
                               uint8_t random_static_mac[6]) {
    uint8_t stored[8];
    uint8_t uid[16];
    uint32_t hash;

    flash_read_page(INKTAG_TELINK_MAC_SECTOR, sizeof(stored), stored);
    if (!all_value(stored, 6u, 0xFFu)) {
        memcpy(public_mac, stored, 6u);
        hash = inktag_fnv1a(public_mac, 6u);
    } else {
        memset(uid, 0, sizeof(uid));
        read_flash_unique_id(uid);
        if (all_value(uid, sizeof(uid), 0x00u) ||
            all_value(uid, sizeof(uid), 0xFFu)) {
            /* Both qualified packages have a selector at this fixed address.
             * This fallback remains deterministic, but the P25Q40SH path used
             * by Nowa is expected to take the unique-ID branch above. */
            flash_read_page(INKTAG_BOARD_SELECTOR_ADDRESS, sizeof(uid), uid);
        }
        hash = inktag_fnv1a(uid, sizeof(uid));
        public_mac[0] = (uint8_t)hash;
        public_mac[1] = (uint8_t)(hash >> 8);
        public_mac[2] = (uint8_t)(hash >> 16);
        public_mac[3] = 0x38u;
        public_mac[4] = 0xC1u;
        public_mac[5] = 0xA4u;
    }

    random_static_mac[0] = public_mac[0];
    random_static_mac[1] = public_mac[1];
    random_static_mac[2] = public_mac[2];
    random_static_mac[3] = (uint8_t)(hash >> 8);
    random_static_mac[4] = (uint8_t)(hash >> 16);
    random_static_mac[5] = 0xC0u;
    if (!all_value(&stored[6], 2u, 0xFFu)) {
        random_static_mac[3] = stored[6];
        random_static_mac[4] = stored[7];
    }
}

uint8_t inktag_tlsr_identity_address_type(void) { return s_address_type; }

bool inktag_tlsr_identity_init(uint8_t ordinary[6]) {
    uint8_t record[INKTAG_TELINK_TYPED_ADDRESS_BYTES], stored[8], sdk[6], random[6];
    inktag_ble_address_t candidate;
    uint8_t i, flags;
    s_address_type = 0xFFu;
    if (!ordinary) return false;
    flags = inktag_tlsr_board_selector_flags();
    if (flags == 0xFFu) return false;
    __asm__ volatile ("" : : "r"(s_address_contract) : "memory");
    if (!identity_initialize()) return false;
    flash_read_page(INKTAG_TELINK_TYPED_ADDRESS, sizeof(record), record);
    if (all_value(record, sizeof(record), 0xFFu)) {
        if (flags & INKTAG_TLSR_SELECTOR_TYPED_ADDRESS_REQUIRED) return false;
        /* No new record means installed legacy behavior, never a new hash or
         * write-on-boot migration. Retain even the historical derivation. */
        legacy_identity_init(sdk, random);
        for (i = 0u; i < 6u; ++i) candidate.bytes[i] = sdk[5u-i];
        candidate.type = INKTAG_BLE_ADDRESS_PUBLIC;
    } else {
        if (!all_value(record + INKTAG_BLE_ADDRESS_RECORD_BYTES,
                       sizeof(record) - INKTAG_BLE_ADDRESS_RECORD_BYTES, 0xFFu) ||
            !inktag_ble_address_record_decode(record,
                            INKTAG_BLE_ADDRESS_RECORD_BYTES, &candidate)) return false;
        flash_read_page(INKTAG_TELINK_MAC_SECTOR, sizeof(stored), stored);
        for (i = 0u; i < 6u; ++i) sdk[i] = candidate.bytes[5u-i];
        /* Full binds the new typed record to the retained compatibility bytes.
         * A partial/mismatched write never falls back or manufactures a MAC. */
        if (memcmp(stored, sdk, 6u) != 0) return false;
    }
    if (!inktag_ble_address_valid(&candidate)) return false;
    memcpy(ordinary, sdk, 6u);
    s_address_type = candidate.type;
    return true;
}
