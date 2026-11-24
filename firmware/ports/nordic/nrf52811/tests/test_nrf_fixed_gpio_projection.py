"""Compare actual configured GPIO/SPI traces with the unchanged common driver."""
from pathlib import Path
import re,runpy,subprocess,tempfile,os
ROOT=Path(__file__).resolve().parents[1]
original=(ROOT/'compat/common/epd/inktag_epd_driver.c').read_text(encoding='utf-8')
project=runpy.run_path(str(ROOT/'build-support/gui/compact-epd-commands.py'))['project_fixed_pins']
projected=project(original)
def function(source,name):
    found=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{]*\)\s*\{',source,re.M)
    assert found,name
    start=found.start();depth=1;i=found.end()
    while depth:
        if source[i]=='{':depth+=1
        elif source[i]=='}':depth-=1
        i+=1
    return source[start:i]

names=['EPD_GPIO_Load','EPD_GPIO_IsConfigured','EPD_GPIO_Init','EPD_GPIO_Uninit','epd_select_direction','epd_transfer','EPD_SPI_Write','EPD_SPI_Read','EPD_ReadCommand','EPD_ReadBusy','epd_led_level','EPD_LED_ON','EPD_LED_OFF']
for name in names[2:]:assert function(original,name).replace('epd_pads[pad]','inktag_epd_pad_value(pad)')==function(projected,name)
PREFIX=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "inktag_pin_config.h"
#define NRF52811_XXAA 1
#define INKTAG_NRF_BOARD_NRF52811_AESL 1
#define INKTAG_NRF_SDK17 1
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define INKTAG_EPD_REQUIRE_CONFIGURED_IO 1
#define INKTAG_EPD_SPI_SOFTWARE_CS 1
#define INKTAG_EPD_SPI_FREQUENCY 4000000
#define INKTAG_EPD_BUSY_INPUT_MODE 0
#define INKTAG_BOARD_CONFIG_GPIO_MASK UINT32_C(0xffdf0ff8)
#define INKTAG_BOARD_DEFAULT_EPD_CONFIG {20,19,6,5,4,3,2,255,255,18,7}
#define NRF_DRV_SPI_DEFAULT_CONFIG {0}
#define NRF_DRV_SPI_PIN_NOT_USED 255
#define NRF_SPI_PIN_NOT_CONNECTED 0xffffffffu
#define NRF_GPIO_PIN_DIR_INPUT 0
#define NRF_GPIO_PIN_DIR_OUTPUT 1
#define NRF_SUCCESS 0
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define DEFAULT 3
#define LOW 0
#define HIGH 1
#define HAL_SPI_INSTANCE 0
#define APP_ERROR_CHECK(x) assert((x)==0)
void EPD_LED_OFF(void);
void EPD_LED_ON(void);
typedef uint8_t epd_pin_id_t;
typedef int ret_code_t;
typedef unsigned nrf_gpio_pin_dir_t;
typedef struct{uint8_t mosi_pin,sclk_pin,cs_pin,dc_pin,rst_pin,busy_pin,bs_pin,reserved,wakeup_pin,led_pin,en_pin,display_mode,week_start;} epd_config_t;
typedef struct{unsigned sck_pin,mosi_pin,ss_pin,frequency;} nrf_drv_spi_config_t;
enum epd_pad { PAD_MOSI,PAD_SCLK,PAD_CS,PAD_DC,PAD_RESET,PAD_BUSY,PAD_BS,PAD_ENABLE,PAD_LED,PAD_COUNT };
static bool m_gpio_configured;
static unsigned spi,m_driver_refs,mode[32],level[32],transfers,fail,gpio_calls;
static uint8_t current_wire[16];
const uint8_t *const inktag_nrf_pins_wire=current_wire;
static void pinMode(unsigned pin,unsigned m){assert(pin<32);++gpio_calls;mode[pin]=m==OUTPUT?1:0;printf("mode %u %u\n",pin,m);}
static void digitalWrite(unsigned pin,unsigned v){assert(pin<32);++gpio_calls;level[pin]=v;printf("write %u %u\n",pin,v);}
const uint8_t *inktag_nrf_pins_current(void){return current_wire[0]?current_wire:NULL;}
static bool allowed(void *context,inktag_pin_role_t role,uint8_t pin){
 (void)context;return role<INKTAG_PIN_TXD && pin<32u && ((INKTAG_BOARD_CONFIG_GPIO_MASK>>pin)&1u);
}
static void publish_config(const epd_config_t *cfg,uint16_t enable_low){
 uint8_t wire[16]={1,0,0,0};uint16_t low=enable_low;
 memcpy(wire+4,cfg,6);wire[10]=cfg->en_pin;
 wire[11]=cfg->reserved;wire[12]=cfg->wakeup_pin;wire[13]=cfg->led_pin;wire[14]=wire[15]=255;
 for(unsigned role=7;role<10;++role)if(wire[4+role]!=255u)low|=1u<<role;
 wire[2]=(uint8_t)low;wire[3]=(uint8_t)(low>>8);
 inktag_pin_config_t decoded;assert(inktag_pin_config_decode(wire,16,&decoded));
 assert(inktag_pin_config_validate(&decoded,allowed,NULL));
 /* Model the validated publish boundary into static storage, before Load. */
 memcpy(current_wire,wire,16);
}
static void nrf_gpio_pin_write(unsigned pin,unsigned v){digitalWrite(pin,v);}
static bool digitalRead(unsigned pin){assert(pin<32);return level[pin]!=0;}
static unsigned nrf_gpio_pin_dir_get(unsigned pin){assert(pin<32);return mode[pin];}
static void nrf_spi_pins_set(unsigned s,unsigned clock,unsigned mosi,unsigned miso){printf("route %u %u %u %u\n",s,clock,mosi,miso);}
static int nrf_drv_spi_init(unsigned*s,nrf_drv_spi_config_t*c,void*h,void*x){(void)s;(void)h;(void)x;printf("init %u %u %u %u\n",c->sck_pin,c->mosi_pin,c->ss_pin,c->frequency);return 0;}
static void nrf_drv_spi_uninit(unsigned*s){(void)s;puts("uninit");}
static int nrf_drv_spi_transfer(unsigned*s,const uint8_t*t,unsigned tn,uint8_t*r,unsigned rn){(void)s;printf("transfer %u %u",tn,rn);for(unsigned i=0;i<tn;++i)printf(" %u",t[i]);puts("");if(++transfers==fail)return 1;for(unsigned i=0;i<rn;++i)r[i]=(uint8_t)(0xa0u+i);return 0;}
static void EPD_BoardRelease(void){puts("release");}
'''
MAIN=r'''
int main(int argc,char **argv){
 (void)argv;
 const uint8_t wiring[]=INKTAG_BOARD_DEFAULT_EPD_CONFIG;
 epd_config_t config={0};for(unsigned i=0;i<sizeof(wiring);++i)((uint8_t*)&config)[i]=wiring[i];
#ifdef EPD_MOSI_PIN
 if(argc>1 && !strcmp(argv[1],"live-polarity")){
  /* Supply a real, complete pin16 snapshot, rather than the NULL legacy path.
   * Exercise both EN levels through the actual generated write wrapper. */
  assert(EPD_LED_PIN==255u);
  for(unsigned active_low=0;active_low<2;++active_low){
   publish_config(&config,(uint16_t)(active_low<<6));EPD_GPIO_Load(&config);
   assert(!memcmp(current_wire+4,&config,6) && current_wire[10]==config.en_pin);
   EPD_GPIO_Init();
   assert(level[config.en_pin]==(1u^active_low) && mode[config.en_pin]==1);
   assert(level[config.cs_pin]==HIGH && level[config.dc_pin]==LOW && level[config.rst_pin]==HIGH);
   digitalWrite(config.en_pin,HIGH);assert(level[config.en_pin]==(1u^active_low));
   digitalWrite(config.en_pin,LOW);assert(level[config.en_pin]==active_low);
   /* The physically adjacent non-EN pad must not inherit EN polarity. */
   unsigned adjacent=config.en_pin+1u;
   digitalWrite(adjacent,HIGH);assert(level[adjacent]==HIGH);
   digitalWrite(adjacent,LOW);assert(level[adjacent]==LOW);
   unsigned before=gpio_calls;
   EPD_LED_ON();EPD_LED_OFF();
   assert(gpio_calls==before && EPD_LED_PIN==255u);
   EPD_GPIO_Uninit();
   assert(!m_driver_refs && level[config.en_pin]==active_low && mode[config.en_pin]==0);
  }
  puts("PASS live pin16 current: EN active-high/active-low init/write/teardown; adjacent non-EN unchanged; legacy LED FF/no GPIO");
  return 0;
 }
#endif
 if(argc>1){config.mosi_pin=8;config.sclk_pin=10;config.cs_pin=11;config.dc_pin=4;config.rst_pin=5;config.busy_pin=6;config.en_pin=9;}
 if(argc>2){config.mosi_pin=19;config.sclk_pin=6;config.cs_pin=5;config.dc_pin=4;config.rst_pin=3;config.busy_pin=7;config.en_pin=16;config.led_pin=20;}
 publish_config(&config,0u);
 EPD_GPIO_Load(&config);
 for(unsigned depth=1;depth<=3;++depth){
  for(unsigned i=0;i<depth;++i)EPD_GPIO_Init();
  const uint8_t data[]={0x12,0x34,0xff,0};uint8_t read[32];
  for(unsigned length=1;length<=4;++length){EPD_SPI_Write((uint8_t*)data,length);EPD_SPI_Read(read,length);}
  for(unsigned which=0;which<3;++which){fail=which?transfers+which:0;bool ok=EPD_ReadCommand(0x1b,read,sizeof(read));assert(ok==(which==0));assert(level[config.cs_pin]==1&&mode[config.mosi_pin]==1);}
  fail=0;assert(!EPD_ReadCommand(0x1b,NULL,2));assert(!EPD_ReadCommand(0x1b,read,0));
  level[config.busy_pin]=0;assert(!EPD_ReadBusy());level[config.busy_pin]=1;assert(EPD_ReadBusy());
  for(unsigned i=0;i<depth;++i)EPD_GPIO_Uninit();
  assert(!m_driver_refs&&level[config.en_pin]==0);EPD_GPIO_Uninit();
 }
 epd_config_t next=config;
#ifdef EPD_MOSI_PIN
 /* A failed native read must not publish FF pins into the GPIO backend. */
 for(unsigned i=0;i<sizeof(next);++i)((uint8_t*)&next)[i]=255;
 publish_config(&next,0u);
 EPD_GPIO_Load(&next);assert(EPD_MOSI_PIN==255 && EPD_EN_PIN==255);
 assert(!EPD_GPIO_IsConfigured());
 next=config; /* Public runtime keeps epd=NULL until validated routes return. */
#endif
 publish_config(&next,0u);
 EPD_GPIO_Load(&next);EPD_GPIO_Init();
 const uint8_t one=0x55;EPD_SPI_Write((uint8_t*)&one,1);
 assert(level[config.en_pin]==1 && mode[config.mosi_pin]==1);
 EPD_GPIO_Uninit();assert(level[config.en_pin]==0);
}
'''
with tempfile.TemporaryDirectory() as temporary:
 out=Path(temporary);results=[]
 for name,source in [('reference',original),('fixed',projected)]:
  a=source.index('static uint8_t epd_pads[PAD_COUNT]') if name=='reference' else source.index('#if !defined(INKTAG_NRF_BOARD_NRF52811_AESL)')
  b=source.index('static bool EPD_LED_ENABLED',a);b=source.index(';',b)+1
  aliases='\n'.join(line for line in source.splitlines() if line.startswith('#define EPD_') and 'epd_pads[' in line)+'\n'
  body=PREFIX+source[a:b]+'\n'+aliases+'\n'+'\n'.join(function(source,n) for n in names)+MAIN
  c=out/(name+'.c');c.write_text(body,encoding='utf-8');binary=out/name
  flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
  subprocess.run(['cc','-std=gnu99',*flags,'-Ifirmware/common/protocol',str(c),
                  'compat/common/protocol/inktag_pin_config.c','-o',str(binary)],check=True)
  results.append([subprocess.check_output([str(binary),*args]) for args in [[],['custom'],['rotated','full']]])
 # RGB is exercised through its authoritative production adapter. The legacy
 # screen-only blue hook is retired; no other pin/transfer/wait may change.
 def without_legacy_blue(trace,pin):
  return b'\n'.join(line for line in trace.splitlines()
   if not line.startswith((f'mode {pin} '.encode(),f'write {pin} '.encode())))+b'\n'
 assert [without_legacy_blue(r,pin) for r,pin in zip(results[0],[18,18,20])]==results[1]
 assert results[0][0]!=results[0][1]
 print(subprocess.check_output([str(out/'fixed'),'live-polarity'],text=True),end='')
 print('PASS display GPIO/SPI traces match common except retired legacy blue hook; nested leases, 4MHz, error cleanup and rail-off unchanged; RGB remains owned by its tested adapter')
