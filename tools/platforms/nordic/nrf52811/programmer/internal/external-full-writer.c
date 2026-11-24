/* Bounded EL022H2 RAM worker. No BLE, bootloader, GPIO routing or retry policy. */
#include <stdint.h>
#include <stdbool.h>
struct full_mailbox {
 uint32_t magic, command, address, length, input_crc, nonce;
 uint32_t completed_nonce, error, done, jedec, output_crc, reserved;
};
#define WORKER_MAGIC 0x3157524eu /* NRW1 */
static uint32_t worker_crc(const uint8_t *p,uint32_t n) {
 uint32_t crc=~0u;
 while(n--) { crc^=*p++; for(unsigned i=0;i<8;++i)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u); }
 return ~crc;
}
#ifndef INKTAG_WRITER_HOST_TEST
#define GPIO_BASE       0x50000000u
#define GPIO_OUTSET     (*(volatile uint32_t *)(GPIO_BASE + 0x508u))
#define GPIO_OUTCLR     (*(volatile uint32_t *)(GPIO_BASE + 0x50Cu))
#define GPIO_IN         (*(volatile uint32_t *)(GPIO_BASE + 0x510u))
#define GPIO_DIRCLR     (*(volatile uint32_t *)(GPIO_BASE + 0x51Cu))
#define GPIO_PIN_CNF(n) (*(volatile uint32_t *)(GPIO_BASE + 0x700u + ((n) * 4u)))
#define SERIAL0_ENABLE  (*(volatile uint32_t *)(0x40003000u + 0x500u))
#define SERIAL1_ENABLE  (*(volatile uint32_t *)(0x40004000u + 0x500u))

#define PIN_CS          12u
#define PIN_MISO        13u
#define PIN_SCK         14u
#define PIN_MOSI        15u
#define PIN_MASK(n)     (1u << (n))
#define SOURCE_ADDRESS 0x20002000u
#define MAILBOX_ADDR 0x20001000u
static void delay_half_period(void)
{
    for (volatile uint32_t i = 0u; i < 24u; ++i) __asm volatile ("nop");
}

static void chip_select(uint32_t active)
{
    if (active) GPIO_OUTCLR = PIN_MASK(PIN_CS);
    else GPIO_OUTSET = PIN_MASK(PIN_CS);
    delay_half_period();
}

static uint8_t spi_byte(uint8_t tx)
{
    uint8_t rx = 0u;
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        if ((tx & 0x80u) != 0u) GPIO_OUTSET = PIN_MASK(PIN_MOSI);
        else GPIO_OUTCLR = PIN_MASK(PIN_MOSI);
        tx <<= 1;
        delay_half_period();
        GPIO_OUTSET = PIN_MASK(PIN_SCK);
        delay_half_period();
        rx = (uint8_t)(rx << 1);
        if ((GPIO_IN & PIN_MASK(PIN_MISO)) != 0u) rx |= 1u;
        GPIO_OUTCLR = PIN_MASK(PIN_SCK);
    }
    return rx;
}

static void simple_command(uint8_t command)
{
    chip_select(1u);
    (void)spi_byte(command);
    chip_select(0u);
}

static void send_address(uint32_t address)
{
    (void)spi_byte((uint8_t)(address >> 16));
    (void)spi_byte((uint8_t)(address >> 8));
    (void)spi_byte((uint8_t)address);
}

static uint8_t read_status(void)
{
    uint8_t status;
    chip_select(1u);
    (void)spi_byte(0x05u);
    status = spi_byte(0u);
    chip_select(0u);
    return status;
}

static uint32_t wait_ready(uint32_t attempts)
{
    while (attempts-- != 0u) {
        if ((read_status() & 1u) == 0u) return 1u;
        delay_half_period();
    }
    return 0u;
}

static uint32_t write_enable(void)
{
    chip_select(1u);
    (void)spi_byte(0x06u);
    chip_select(0u);
    return (read_status() & 2u) != 0u;
}

static uint32_t erase_sector(uint32_t address)
{
    if (!write_enable()) return 0u;
    chip_select(1u);
    (void)spi_byte(0x20u);
    send_address(address);
    chip_select(0u);
    return wait_ready(100000u);
}

static void release_pins(void)
{
    GPIO_OUTSET = PIN_MASK(PIN_CS);
    GPIO_DIRCLR = PIN_MASK(PIN_CS) | PIN_MASK(PIN_MISO) |
                  PIN_MASK(PIN_SCK) | PIN_MASK(PIN_MOSI);
    GPIO_PIN_CNF(PIN_CS) = 0u;
    GPIO_PIN_CNF(PIN_MISO) = 0u;
    GPIO_PIN_CNF(PIN_SCK) = 0u;
    GPIO_PIN_CNF(PIN_MOSI) = 0u;
}


static bool flash_read(uint32_t address,uint8_t *data,uint32_t length) {
 chip_select(1u);(void)spi_byte(3u);send_address(address);
 while(length--)*data++=spi_byte(0u);chip_select(0u);return true;
}
static bool flash_program(uint32_t address,const uint8_t *data,uint32_t length) {
 if(!write_enable())return false;
 chip_select(1u);(void)spi_byte(2u);send_address(address);
 while(length--)(void)spi_byte(*data++);chip_select(0u);return wait_ready(100000u)!=0u;
}
#else
static bool flash_read(uint32_t address,uint8_t *data,uint32_t length);
static bool flash_program(uint32_t address,const uint8_t *data,uint32_t length);
static uint32_t erase_sector(uint32_t address);
#endif
static bool worker_valid(const volatile struct full_mailbox *m) {
 if(m->magic!=WORKER_MAGIC || !m->nonce || m->reserved)return false;
 if(m->command==1u)return m->length==4096u && !(m->address&4095u) && m->address<=0x7f000u;
 if(m->command==2u)return m->length==4096u && !(m->address&4095u) && m->address<=0x4000u;
 return (m->command==3u || m->command==4u) && m->address==0xa000u && m->length==0x76000u;
}
static uint32_t worker_execute(volatile struct full_mailbox *m,uint8_t *buffer) {
 if(!worker_valid(m))return 1u;
 if(m->command==1u) {
  if(!flash_read(m->address,buffer,4096u))return 2u;
  m->output_crc=worker_crc(buffer,4096u);return 0u;
 }
 if(m->command==2u) {
  if(worker_crc(buffer,4096u)!=m->input_crc)return 3u;
  if(!erase_sector(m->address))return 4u;
  for(uint32_t i=0;i<4096u;i+=256u)
   if(!flash_program(m->address+i,buffer+i,256u))return 5u;
  if(!flash_read(m->address,buffer,4096u))return 6u;
  m->output_crc=worker_crc(buffer,4096u);
  return m->output_crc==m->input_crc?0u:7u;
 }
 uint32_t count=0u;const uint8_t deleted[4]={0,0,0,0};
 for(uint32_t address=0xa000u;address<0x80000u;address+=4096u) {
  if(m->command==3u && !flash_program(address,deleted,4u))return 8u;
  if(!flash_read(address,buffer+count,4u))return 9u;
  if(m->command==3u && (buffer[count] || buffer[count+1] || buffer[count+2] || buffer[count+3]))return 10u;
  count+=4u;
 }
 m->output_crc=worker_crc(buffer,count);return 0u;
}
#ifndef INKTAG_WRITER_HOST_TEST
__attribute__((noreturn,noinline,used)) static void worker_main(void) {
 volatile struct full_mailbox *m=(volatile struct full_mailbox*)MAILBOX_ADDR;
 m->done=0u;m->completed_nonce=0u;m->error=1u;
 if(worker_valid(m)) {
  SERIAL0_ENABLE=0u;SERIAL1_ENABLE=0u;
  GPIO_OUTSET=PIN_MASK(PIN_CS);GPIO_OUTCLR=PIN_MASK(PIN_SCK)|PIN_MASK(PIN_MOSI);
  GPIO_PIN_CNF(PIN_CS)=1u;GPIO_PIN_CNF(PIN_SCK)=1u;GPIO_PIN_CNF(PIN_MOSI)=1u;GPIO_PIN_CNF(PIN_MISO)=0u;
  simple_command(0xabu);for(volatile unsigned i=0;i<4000u;++i)__asm volatile("nop");
  chip_select(1u);(void)spi_byte(0x9fu);
  uint32_t id=(uint32_t)spi_byte(0u)<<16;id|=(uint32_t)spi_byte(0u)<<8;id|=spi_byte(0u);
  chip_select(0u);m->jedec=id;
  m->error=id!=0xa14013u || (read_status()&3u)?11u:
   worker_execute(m,(uint8_t*)SOURCE_ADDRESS);
  simple_command(0xb9u);release_pins();
 }
 m->completed_nonce=m->nonce;m->done=0x454e4f44u;
 __asm volatile("dsb\n\tisb\n\tbkpt #0");for(;;)__asm volatile("wfe");
}
/* A C prologue must never precede the mask. The debugger also establishes
 * PRIMASK before Go, so pending App/SoftDevice IRQs cannot enter this RAM. */
__attribute__((naked,section(".probe_entry"),noreturn)) void probe_entry(void) {
 __asm volatile("cpsid i\n\tb worker_main");
}
#endif
