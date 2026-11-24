#ifndef INKTAG_GR_FLASH_LAYOUT_H
#define INKTAG_GR_FLASH_LAYOUT_H

#include <stdint.h>
#include "inktag_release_v1.h"

#define INKTAG_GR_FLASH_SECTOR_SIZE 0x00001000u
#define INKTAG_GR_USER_FONT_SIZE 0x00008000u
#define INKTAG_GR_IMAGE_INFO_LEN 48u
#define INKTAG_GR_IMAGE_INFO_STRUCT_LEN 40u
#define INKTAG_GR_SIGNATURE_EXTENSION_LEN 856u
/* Full-only immutable provisioning inside the existing boot metadata sector.
 * Ordinary App-info (0x2000) and DFU-info (0x3000) commits never erase it. */
#define INKTAG_GR_FACTORY_SEED_OFFSET 0x00000F00u

#if defined(INKTAG_GOODIX_GR5331)
#define INKTAG_GR_NATIVE_BOARD_CONTEXT 2u /* native SDK signing product_type */
#define INKTAG_GR_FLASH_BASE 0x00200000u
#define INKTAG_GR_APP_REGION_SIZE 0x00040000u
#define INKTAG_GR_LAYOUT_MARKER_MAGIC INKTAG_RELEASE_LAYOUT_GR5331
#define INKTAG_GR_USER_FONT_START 0x00260000u
#define INKTAG_GR_RECOVERY_JOURNAL_A 0x00268000u
#define INKTAG_GR_RECOVERY_JOURNAL_B 0x00269000u
#define INKTAG_GR_IMAGE_STORE_START 0x0026A000u
#define INKTAG_GR_IMAGE_STORE_SIZE 0x00014000u
#define INKTAG_GR_STORAGE_LIMIT 0x0027E000u
#elif defined(INKTAG_GOODIX_GR5515)
#define INKTAG_GR_NATIVE_BOARD_CONTEXT 0u /* native SDK signing product_type */
#define INKTAG_GR_FLASH_BASE 0x01000000u
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
/* A distinct executable/OTA layout, never a runtime fallback from v6.
 * Production stays closed until the shared signed identity is frozen. */
#ifndef INKTAG_RELEASE_LAYOUT_GR5513_INTERNAL
#error "GR5513 internal layout requires a frozen distinct signed layout identity"
#endif
#define INKTAG_GR_APP_REGION_SIZE 0x00044000u
#define INKTAG_GR_LAYOUT_MARKER_MAGIC INKTAG_RELEASE_LAYOUT_GR5513_INTERNAL
#define INKTAG_GR_USER_FONT_START 0x01064000u
#define INKTAG_GR_RECOVERY_JOURNAL_A 0x0106C000u
#define INKTAG_GR_RECOVERY_JOURNAL_B 0x0106D000u
#define INKTAG_GR_IMAGE_STORE_START 0x0106E000u
#define INKTAG_GR_IMAGE_STORE_SIZE 0x00010000u
#define INKTAG_GR_STORAGE_LIMIT 0x0107E000u
#else
#define INKTAG_GR_APP_REGION_SIZE 0x00052000u
#define INKTAG_GR_LAYOUT_MARKER_MAGIC INKTAG_RELEASE_LAYOUT_GR551X
#define INKTAG_GR_USER_FONT_START 0x01072000u
#define INKTAG_GR_RECOVERY_JOURNAL_A 0x0107A000u
#define INKTAG_GR_RECOVERY_JOURNAL_B 0x0107B000u
#define INKTAG_GR_IMAGE_STORE_START 0x0107C000u
#define INKTAG_GR_IMAGE_STORE_SIZE 0x00080000u
#define INKTAG_GR_STORAGE_LIMIT 0x010FC000u
/* This virtual data extent routes entirely to TD40 on qualified GR5513.
 * It is never read through the mirrored package-XIP window. */
#define INKTAG_GR5513_EXTERNAL_FLASH_BOUNDARY INKTAG_GR_IMAGE_STORE_START
#endif
#else
#define INKTAG_GR_APP_REGION_SIZE 0u
#define INKTAG_GR_LAYOUT_MARKER_MAGIC 0u
#define INKTAG_GR_IMAGE_STORE_START 0u
#define INKTAG_GR_IMAGE_STORE_SIZE 0u
#define INKTAG_GR_USER_FONT_START 0u
#define INKTAG_GR_RECOVERY_JOURNAL_A 0u
#define INKTAG_GR_RECOVERY_JOURNAL_B 0u
#define INKTAG_GR_STORAGE_LIMIT 0u
#endif

#define INKTAG_GR_LAYOUT_MARKER_INVERSE (~INKTAG_GR_LAYOUT_MARKER_MAGIC)
#define INKTAG_GR_APP_PAYLOAD_LIMIT \
    (INKTAG_GR_APP_REGION_SIZE - INKTAG_GR_IMAGE_INFO_LEN - \
     INKTAG_GR_SIGNATURE_EXTENSION_LEN)

#if defined(INKTAG_GOODIX_OTA) || defined(INKTAG_SWAP_BOOTLOADER)
_Static_assert(INKTAG_GR_USER_FONT_SIZE == 0x00008000u,
               "Goodix single user-font region must remain 32 KiB");
#if defined(INKTAG_GOODIX_GR5331)
_Static_assert(INKTAG_GR_APP_REGION_SIZE == 0x00040000u,
               "GR5331 layout v4 application region must remain 256 KiB");
#elif defined(INKTAG_GOODIX_GR5515)
#if defined(INKTAG_GR5513_INTERNAL_IMAGES)
_Static_assert(INKTAG_GR_APP_REGION_SIZE == 0x00044000u,
               "GR5513 internal application region must remain 272 KiB");
_Static_assert(INKTAG_GR_STORAGE_LIMIT == 0x0107E000u,
               "GR5513 internal images must leave 8 KiB native NVDS");
#else
_Static_assert(INKTAG_GR_APP_REGION_SIZE == 0x00052000u,
               "GR551x layout v6 application region must remain 328 KiB");
#endif
#endif
_Static_assert(INKTAG_GR_IMAGE_INFO_LEN - INKTAG_GR_IMAGE_INFO_STRUCT_LEN == 8u,
               "Goodix layout marker must fill the signed image-info tail");
_Static_assert((INKTAG_GR_USER_FONT_START &
                (INKTAG_GR_FLASH_SECTOR_SIZE - 1u)) == 0u,
               "Goodix user fonts must be sector aligned");
_Static_assert(INKTAG_GR_FLASH_BASE + 0x00020000u +
                   INKTAG_GR_APP_REGION_SIZE == INKTAG_GR_USER_FONT_START,
               "Application must end exactly at user fonts");
_Static_assert(INKTAG_GR_USER_FONT_START + INKTAG_GR_USER_FONT_SIZE ==
                   INKTAG_GR_RECOVERY_JOURNAL_A,
               "User fonts must end exactly at Recovery journal");
_Static_assert(INKTAG_GR_RECOVERY_JOURNAL_B ==
                   INKTAG_GR_RECOVERY_JOURNAL_A + INKTAG_GR_FLASH_SECTOR_SIZE,
               "Recovery journal pages must remain adjacent");
_Static_assert(INKTAG_GR_IMAGE_STORE_START ==
                   INKTAG_GR_RECOVERY_JOURNAL_B + INKTAG_GR_FLASH_SECTOR_SIZE,
               "Image extent must start after Recovery journal");
_Static_assert(INKTAG_GR_IMAGE_STORE_START + INKTAG_GR_IMAGE_STORE_SIZE ==
                   INKTAG_GR_STORAGE_LIMIT,
               "Image extent must end before NVDS");
#if defined(INKTAG_GOODIX_GR5515) && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
_Static_assert(INKTAG_GR_IMAGE_STORE_START ==
                   INKTAG_GR5513_EXTERNAL_FLASH_BOUNDARY,
               "GR5513 image extent must be auxiliary-only");
#endif
#endif

#endif
