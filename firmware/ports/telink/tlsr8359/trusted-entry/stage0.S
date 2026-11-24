/* Fixed native entry. ROM loads this bounded image into SRAM. Configure the
 * temporary RAM instruction window before moving above 32 KiB: the ROM's
 * default cache boundary does not make a copied F000 loader executable. */
#include "native_memory.h"
    .code 16
    .section .vectors,"ax"
    .global __start
__start:
    tj stage0_reset
    .org 0x8
    .word 0x544c4e4b
    .word (0x00880000 + __stage0_load_div16__)
    .org 0x10
    tj stage0_stop
    .org 0x18
    .word __stage0_bin_size__
    .org 0x20
stage0_reset:
    tmov r0,#0x12
    tmcsr r0
    tloadr r0,stage0_data+16
    tmov r13,r0
    tmov r0,#0x13
    tmcsr r0
    tloadr r0,stage0_data+16
    tmov r13,r0
    tloadr r1,stage0_data
    tloadr r2,stage0_data+4
    tloadr r3,stage0_data+8
stage0_copy:
    tcmp r2,r3
    tjge stage0_loaded
    tloadr r0,[r1,#0]
    tstorer r0,[r2,#0]
    tadd r1,#4
    tadd r2,#4
    tj stage0_copy
stage0_loaded:
    tloadr r1,stage0_cache
    tloadr r2,stage0_cache+4
    tmov r0,#0
stage0_zero_tag:
    tcmp r1,r2
    tjge stage0_tag_ready
    tstorer r0,[r1,#0]
    tadd r1,#4
    tj stage0_zero_tag
stage0_tag_ready:
    tloadr r1,stage0_cache+8
    tmov r0,#INKTAG_NATIVE_TAG_PAGE
    tstorerb r0,[r1,#0]
    tadd r0,#1
    tstorerb r0,[r1,#1]
    tloadr r0,stage0_data+20
    tloadr r1,stage0_data+24
    tloadr r2,stage0_data+12
    tjex r2
stage0_stop:
    tj stage0_stop
    .balign 4
stage0_data:
    .word (0x840000 + __native_load_lma__)
    .word (0x840000 + __native_load_start__)
    .word (0x840000 + __native_load_end__)
    .word inktag_tlsr_native_load
    .word 0x84f000
    .word 0x40000
    .word 0x11000
stage0_cache:
    .word INKTAG_NATIVE_TAG_RAM
    .word INKTAG_NATIVE_TAG_END
    .word INKTAG_NATIVE_TAG_REGISTER
