/* Project-owned SDK17 relocation. Force-included in every Recovery C unit:
 * first load the unmodified vendor ABI, then override only address macros.
 * nrf_dfu_types.h's include guard prevents a later SDK include undoing this.
 * The SDK tree is never patched and all dependent objects track this header. */
#ifndef INKTAG_NRF52832_DFU_LAYOUT_H
#define INKTAG_NRF52832_DFU_LAYOUT_H

#include "nrf_dfu_types.h"
#include "inktag_board.h"

#if !defined(NRF52832_XXAA)
#error "HW56 relocation belongs only to the verified 512 KiB nRF52832 target"
#endif
#if BOOTLOADER_SETTINGS_ADDRESS != 0x7F000UL || NRF_MBR_PARAMS_PAGE_ADDRESS != 0x7E000UL
#error "Unexpected SDK DFU address defaults; re-audit the relocation"
#endif
#undef BOOTLOADER_SETTINGS_ADDRESS
#undef NRF_MBR_PARAMS_PAGE_ADDRESS
#define BOOTLOADER_SETTINGS_ADDRESS INKTAG_BOARD_DFU_SETTINGS_START
#define NRF_MBR_PARAMS_PAGE_ADDRESS INKTAG_BOARD_MBR_PARAMS_START

_Static_assert(INKTAG_BOARD_BOOTLOADER_START + 0x6000UL == NRF_MBR_PARAMS_PAGE_ADDRESS,
               "Recovery protection must end at its adjacent MBR page");
_Static_assert(NRF_MBR_PARAMS_PAGE_ADDRESS + 0x1000UL == BOOTLOADER_SETTINGS_ADDRESS,
               "SDK backup must remain immediately below primary Settings");
_Static_assert(BOOTLOADER_SETTINGS_ADDRESS + 0x1000UL == INKTAG_BOARD_IMAGE_STORE_START,
               "SDK writes/protection must not include the tail image extent");
_Static_assert(INKTAG_BOARD_BOOTLOADER_START - NRF_DFU_APP_DATA_AREA_SIZE ==
                   INKTAG_BOARD_DFU_REGION_END,
               "Relocating Recovery must not expand the signed App payload");
#endif
