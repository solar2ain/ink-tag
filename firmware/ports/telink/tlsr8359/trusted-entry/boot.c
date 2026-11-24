#if INKTAG_BUILD_PRO
#include "boot_pro.inc"
#else
#include "recovery.h"
#include "profile.h"
#include "image_verify.h"
#include "inktag_tlsr_recovery.h"
#include "native_memory.h"
#include <string.h>

extern bool inktag_tlsr_trusted_journal_boot(void);
extern uint32_t inktag_tlsr_take_verified_boot_size(void);
extern uint8_t __native_load_image_start__[],__native_load_image_end__[];

/* Remain in the already loaded low RAM prefix while changing cache pointers.
 * XIP code may not execute through the intermediate tag-register state.
 * This is called only after fresh image/epoch proof and RF/DMA quiescence.
 */
static __attribute__((section(".ram_code"),noinline,noreturn))
void enter_native_ram(uint32_t base,uint32_t capacity,uint32_t verified_size) {
    /* The real RAM handoff consumes admission as data, in addition to the
     * outer branch. Zero/short/stale success cannot select a native role. */
    if (verified_size<32u || verified_size>capacity) for (;;) {}
    volatile uint32_t *tag=(volatile uint32_t *)INKTAG_NATIVE_TAG_RAM;
    while(tag<(volatile uint32_t *)INKTAG_NATIVE_TAG_END) *tag++=0;
    *(volatile uint8_t *)INKTAG_NATIVE_TAG_REGISTER=INKTAG_NATIVE_TAG_PAGE;
    *(volatile uint8_t *)(INKTAG_NATIVE_TAG_REGISTER+1)=INKTAG_NATIVE_TAG_PAGE+1;
    ((void (*)(uint32_t,uint32_t))INKTAG_NATIVE_LOADER_ENTRY)(base,capacity);
    for(;;) {} /* failed native restart never skips the verifier */
}

bool inktag_tlsr_trusted_boot(void) {
    uint8_t irq_state=irq_disable();
    uint8_t header[32];
    uint32_t prefix;
    (void)inktag_tlsr_take_verified_boot_size();
    flash_read_page(INKTAG_APPLICATION_START,sizeof(header),header);
    /* The profile binds the physical base and signed context as one pair.
     * An old zero-linked image is never relocated through a mutable token. */
    if(!inktag_tlsr_boot_ram_prefix(header,INKTAG_APPLICATION_SIZE,&prefix) ||
       !inktag_recovery_ota_boot_image_valid() || !inktag_tlsr_trusted_journal_boot()) {
        irq_restore(irq_state);
        return false;
    }
    uint32_t admitted=inktag_tlsr_take_verified_boot_size();
    if (admitted<32u || admitted!=((uint32_t)header[24]|(uint32_t)header[25]<<8|
        (uint32_t)header[26]<<16|(uint32_t)header[27]<<24)+4u) {
        irq_restore(irq_state);return false;
    }
    /* IRQ masking alone does not stop RF DMA from the live Recovery link.
     * After proof succeeds, quiesce the pinned SDK radio and both RF DMA
     * channels before replacing SRAM. App initializes its own radio afresh. */
    rf_set_tx_rx_off_auto_mode();
    rf_set_tx_rx_off();
    RFDMA_RX_DISABLE;
    RFDMA_TX_DISABLE;
    memcpy((void *)0x84f000u,__native_load_image_start__,
           (uint32_t)(__native_load_image_end__-__native_load_image_start__));
    enter_native_ram(INKTAG_APPLICATION_START,INKTAG_APPLICATION_SIZE,admitted);
    for(;;) {} /* native handoff failure must not fall into App */
}

void inktag_tlsr_trusted_handoff(void) {
    /* A return enters the same fresh verifier; a TX ACK is never an App proof.
     * On any failed admission the caller remains in Recovery without a reset. */
    (void)inktag_tlsr_trusted_boot();
}
#endif
