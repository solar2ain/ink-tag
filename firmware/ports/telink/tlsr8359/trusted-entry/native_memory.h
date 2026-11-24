#ifndef INKTAG_TLSR_NATIVE_MEMORY_H
#define INKTAG_TLSR_NATIVE_MEMORY_H
/* Temporary handoff ownership inside the existing F000..FFFF reservation.
 * A copied loader is not executable merely because its bytes are in SRAM:
 * B85's instruction-cache tag boundary must place its PC in the RAM window.
 * Match the pinned startup's 256-byte tag + 2-KiB instruction-cache layout.
 */
#define INKTAG_NATIVE_LOADER_PC 0xf000
/* TC32 code16 function pointers carry the state bit, just like the actual
 * stage0 relocation for inktag_tlsr_native_load (F001, not raw F000).
 */
#define INKTAG_NATIVE_LOADER_ENTRY (INKTAG_NATIVE_LOADER_PC | 1)
#define INKTAG_NATIVE_TAG_RAM 0x84f200
#define INKTAG_NATIVE_TAG_END 0x84f300
#define INKTAG_NATIVE_TAG_PAGE 0xf2
#define INKTAG_NATIVE_TAG_REGISTER 0x80060c
#endif
