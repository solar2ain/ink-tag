#ifndef INKTAG_OM_FLASH_LAYOUT_H
#define INKTAG_OM_FLASH_LAYOUT_H

#define INKTAG_OM_FLASH_SECTOR_SIZE 0x00001000u

#if defined(INKTAG_OM_NEXT_LAYOUT)
/* Source-only successor to the preserved-tail pilot. Settings relocation is
 * Full-only: never discover/migrate/format an old pair during normal boot. */
#define INKTAG_OM_IMAGE_STORE_PRIMARY_START 0x00084000u
#define INKTAG_OM_IMAGE_STORE_PRIMARY_SIZE 0x00079000u
#define INKTAG_OM_NVDS_ADDRESS 0x000FD000u
#define INKTAG_OM_FACTORY_CONF_SIZE 0x00001000u
/* The vendor SDK derives its optional static CONF from NVDS base + 8 KiB. */
#define INKTAG_OM_FACTORY_CONF_ADDRESS (INKTAG_OM_NVDS_ADDRESS + 0x2000u)
#define INKTAG_OM_GUI_ASSET_BANK_A_START 0x00078000u
#define INKTAG_OM_GUI_ASSET_BANK_SIZE 0x00008000u
#else
#define INKTAG_OM_IMAGE_STORE_PRIMARY_START 0x00082000u
#define INKTAG_OM_IMAGE_STORE_PRIMARY_SIZE 0x0007A000u
#define INKTAG_OM_NVDS_ADDRESS 0u /* Frozen ZKC MBR-selected pair. */

/* The already-owned contiguous 32 KiB font reservation becomes one native
 * package. No boundary moves and no boot conversion/erase of retired media. */
#define INKTAG_OM_GUI_ASSET_BANK_A_START 0x00075000u
#define INKTAG_OM_GUI_ASSET_BANK_SIZE 0x00008000u
#endif

/* Layout v2: fonts and Recovery state are before the fixed MBR pair;
 * one contiguous internal image extent follows it. No secondary pools. */

#endif
