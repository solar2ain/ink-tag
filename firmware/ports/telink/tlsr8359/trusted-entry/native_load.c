/* Native B85 load primitive, executing entirely at SRAM F000 after the
 * caller establishes the temporary instruction-cache/RAM mapping.
 * Uses the pinned SDK MSPI register meanings and Telink's B85 0x602=0x88
 * RAM-vector restart. Not a general loader or callable BLE command. */
#include <stdint.h>
#include <stdbool.h>
#define IO8(a) (*(volatile uint8_t *)(0x800000u+(a)))

static void stop(void) { for (;;) {} }

static uint8_t transfer(uint8_t value) {
    uint32_t budget=100000u;
    IO8(0x0c)=value;
    while(IO8(0x0d)&16u) if(--budget==0u) stop();
    return IO8(0x0c);
}

static void read_bytes(uint32_t address,volatile uint8_t *out,uint32_t count) {
    IO8(0x0d)=1u;IO8(0x0d)=0u;
    transfer(3u);transfer((uint8_t)(address>>16));
    transfer((uint8_t)(address>>8));transfer((uint8_t)address);
    while(count--) *out++=transfer(0u);
    IO8(0x0d)=1u;
}

__attribute__((section(".native_entry"),noreturn))
void inktag_tlsr_native_load(uint32_t base,uint32_t capacity) {
    uint8_t header[32];
    uint32_t prefix,declared;
    /* Only two fixed destinations; neither a RAM token nor an App field
     * chooses the trusted entry. Interrupts cannot run after SRAM overwrite. */
    IO8(0x643)=0u;
    if(!((base==0x40000u && capacity==0x11000u) ||
         (base==0x1000u && capacity==0x3f000u))) stop();
    IO8(0x0d)=1u;IO8(0x0d)=0u;transfer(0xabu);IO8(0x0d)=1u;
    { volatile uint32_t settle;for(settle=0;settle<1000u;++settle) {} }
    read_bytes(base,header,sizeof(header));
    prefix=((uint32_t)header[12]|(uint32_t)header[13]<<8)*16u;
    declared=(uint32_t)header[24]|(uint32_t)header[25]<<8|
             (uint32_t)header[26]<<16|(uint32_t)header[27]<<24;
    if(header[8]!='K'||header[9]!='N'||header[10]!='L'||header[11]!='T'||
       header[14]!=0x88u||header[15]!=0u||prefix<32u||prefix>0x8000u||
       prefix>declared||declared>capacity-4u) stop();
    read_bytes(base,(volatile uint8_t *)0x840000u,prefix);
    IO8(0x63e)=0u; /* both fixed roles use physical .text/.data addresses */
    IO8(0x602)=0x88u;
    stop(); /* failed native restart never becomes a direct mutable jump */
}
