#ifndef INKTAG_TLSR_TRUSTED_ENTRY_PROFILE_H
#define INKTAG_TLSR_TRUSTED_ENTRY_PROFILE_H
/* Explicit paired native build. Never relocate old unbound signed bytes.
 * Physical partition boundaries and complete signed capacity are unchanged. */
#include "inktag_tlsr_flash_layout.h"
#include "inktag_tlsr_ota_policy.h"
#undef INKTAG_APPLICATION_START
#undef INKTAG_APPLICATION_MARKER
#undef INKTAG_TLSR_OTA_LAYOUT_ID
#define INKTAG_APPLICATION_START 0x01000u
#define INKTAG_APPLICATION_MARKER 0x01008u
#define INKTAG_TLSR_OTA_LAYOUT_ID "layout-v1"
/* Same TLV1/v1 name. Signed body + separate read-only address proof bind
 * the physical entry; an old zero-linked signature cannot be relocated. */
#define INKTAG_TLSR_BOOT_BINDING 1
#define INKTAG_TLSR_BOOT_BINDING_OFFSET 0x140u
/* Only address-bound images of this entry first ship at release epoch1,
 * including upper-stock exclusion. Historical unbound TLV1 retains gate2. */
#undef INKTAG_TLSR_OTA_UPPER_STOCK_MIN_EPOCH
#define INKTAG_TLSR_OTA_UPPER_STOCK_MIN_EPOCH 1u
#define INKTAG_TLSR_TRUSTED_ENTRY 1
#if INKTAG_APPLICATION_START + INKTAG_APPLICATION_SIZE != INKTAG_APPLICATION_REGION_SIZE || \
    INKTAG_APPLICATION_SIZE != 0x3F000u || INKTAG_RECOVERY_START != 0x40000u
#error Trusted entry must not reduce signed capacity or move physical partitions
#endif
#ifndef __ASSEMBLER__
#include <stdbool.h>
bool inktag_tlsr_trusted_boot(void);
void inktag_tlsr_trusted_handoff(void);
bool inktag_recovery_ota_boot_image_valid(void);
bool inktag_tlsr_boot_binding_matches(const unsigned char *value);
#endif
#endif
