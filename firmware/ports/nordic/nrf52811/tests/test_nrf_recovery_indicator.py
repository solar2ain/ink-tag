"""Exercise shipping Recovery LED registers, edges and transport readiness."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'app/bootloader/src/main.c').read_text(encoding='utf-8')
block=source[source.index('#if INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED'):source.index('static void on_error')]
block=re.sub(r'static bool validated_install_pending\(void\)\n\{[\s\S]*?\n\}', '',block)
observer=re.search(r'static void dfu_observer\([^;{]+\)\n\{[\s\S]*?\n\}',source).group()
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "inktag_status.h"
#define INKTAG_BOARD_RECOVERY_STATUS_LED_QUALIFIED 1
#define INKTAG_BOARD_RGB_STATUS_LED_QUALIFIED 1
#define INKTAG_BOARD_STATUS_LED_PIN 18u
#define INKTAG_BOARD_LED_RED_PIN 16u
#define INKTAG_BOARD_LED_GREEN_PIN 17u
#define INKTAG_BOARD_LED_BLUE_PIN 18u
#define NRF_SUCCESS 0
typedef uint32_t ret_code_t;
typedef enum {NRF_DFU_EVT_DFU_STARTED,NRF_DFU_EVT_TRANSPORT_ACTIVATED,NRF_DFU_EVT_OBJECT_RECEIVED,
 NRF_DFU_EVT_DFU_COMPLETED,NRF_DFU_EVT_TRANSPORT_DEACTIVATED,NRF_DFU_EVT_DFU_FAILED,NRF_DFU_EVT_DFU_ABORTED} nrf_dfu_evt_type_t;
typedef void (*nrf_dfu_observer_t)(nrf_dfu_evt_type_t);
typedef struct {uint32_t TASKS_STOP,TASKS_CLEAR,TASKS_START,MODE,BITMODE,PRESCALER,SHORTS,TASKS_COUNT,CC[4],EVENTS_COMPARE[4];} timer_t;
static timer_t timer1,timer2;
#define NRF_TIMER1 (&timer1)
#define NRF_TIMER2 (&timer2)
static struct {uint32_t CONFIG[4],TASKS_OUT[4],TASKS_SET[4],TASKS_CLR[4];} gpiote;
#define NRF_GPIOTE (&gpiote)
static struct {uint32_t DIRSET,OUTSET,OUTCLR;} gpio;
#define NRF_P0 (&gpio)
static struct {uint32_t CHENCLR,CHENSET;struct {uint32_t EEP,TEP;} CH[16];struct {uint32_t TEP;} FORK[16];} ppi;
#define NRF_PPI (&ppi)
#define GPIOTE_CONFIG_OUTINIT_Low 0u
#define GPIOTE_CONFIG_OUTINIT_High 1u
#define GPIOTE_CONFIG_OUTINIT_Pos 20u
#define GPIOTE_CONFIG_MODE_Task 3u
#define GPIOTE_CONFIG_MODE_Pos 0u
#define GPIOTE_CONFIG_PSEL_Pos 8u
#define GPIOTE_CONFIG_POLARITY_Toggle 3u
#define GPIOTE_CONFIG_POLARITY_Pos 16u
#define TIMER_MODE_MODE_Timer 0u
#define TIMER_MODE_MODE_Counter 1u
#define TIMER_MODE_MODE_Pos 0u
#define TIMER_BITMODE_BITMODE_32Bit 3u
#define TIMER_BITMODE_BITMODE_Pos 0u
#define TIMER_SHORTS_COMPARE0_CLEAR_Msk 1u
#define TIMER_SHORTS_COMPARE0_STOP_Msk 2u
#define NRF_DFU_BANK_VALID_APP 1u
static struct {struct {uint32_t bank_code;} bank_1;struct {uint32_t firmware_image_offset,command_size;} progress;} s_dfu_settings;
static unsigned delay_count,transport_calls;
static uint32_t transport_result;
static void nrf_delay_ms(uint32_t ms) {
 assert(ms==INKTAG_STATUS_STARTUP_COLOR_MS&&delay_count<3);
#if INKTAG_BOARD_STATUS_LED_ACTIVE_LOW
 assert(gpio.OUTCLR==(1u<<(16u+delay_count)));
#else
 assert(gpio.OUTSET==(1u<<(16u+delay_count)));
#endif
 delay_count++;
}
ret_code_t inktag_nrf_dfu_transports_init_original(nrf_dfu_observer_t observer) {
 (void)observer;assert(delay_count==3);transport_calls++;return transport_result;
}
'''
fixture=r'''
static void object_keeps_download_phase(void) {
 /* Task registers model writes, not the timer's running value. A data object
  * must not stop, clear or restart an already selected download waveform. */
 timer1.TASKS_STOP=timer1.TASKS_CLEAR=timer1.TASKS_START=0;
 timer2.TASKS_STOP=0;gpio.DIRSET=0;
 dfu_observer(NRF_DFU_EVT_OBJECT_RECEIVED);
 assert(timer1.CC[0]==240000&&((gpiote.CONFIG[0]>>8)&31)==18);
 assert(!timer1.TASKS_STOP&&!timer1.TASKS_CLEAR&&!timer1.TASKS_START);
 assert(!timer2.TASKS_STOP&&!gpio.DIRSET);
}
static void pulse(uint8_t pin,unsigned edges,unsigned flashes,bool starts_off) {
 assert(((gpiote.CONFIG[0]>>8)&31)==pin&&timer1.CC[0]==120000&&timer2.CC[0]==edges);
 assert(ppi.CHENSET==(1u<<9)&&timer1.SHORTS==1&&timer2.SHORTS==2);
 assert(ppi.FORK[8].TEP==(uint32_t)(uintptr_t)&timer2.TASKS_COUNT);
 assert(ppi.CH[9].TEP==(uint32_t)(uintptr_t)&timer1.TASKS_STOP);
 bool on=!starts_off;unsigned count=on;
 for(unsigned i=0;i<edges;i++){on=!on;if(on)count++;}
 assert(!on&&count==flashes);
}
int main(void) {
 assert(nrf_dfu_transports_init(dfu_observer)==NRF_SUCCESS&&transport_calls==1);
 pulse(18,6,3,true);assert(gpio.DIRSET==0x70000);
 dfu_observer(NRF_DFU_EVT_TRANSPORT_ACTIVATED);pulse(17,3,2,false);
 dfu_observer(NRF_DFU_EVT_DFU_STARTED);assert(timer1.CC[0]==240000&&((gpiote.CONFIG[0]>>8)&31)==18&&ppi.FORK[8].TEP==0);
 object_keeps_download_phase();
 for(unsigned i=1;i<=17;i++) {
  s_dfu_settings.progress.firmware_image_offset=i*4096;
  object_keeps_download_phase();
 }
 /* A reconnect still starts the waveform; subsequent objects leave it alone. */
 dfu_observer(NRF_DFU_EVT_TRANSPORT_DEACTIVATED);
 dfu_observer(NRF_DFU_EVT_TRANSPORT_ACTIVATED);
 assert(timer1.TASKS_START&&timer1.CC[0]==240000);
 object_keeps_download_phase();
 /* A cached init with no completed data can resume without DFU_STARTED. */
 dfu_observer(NRF_DFU_EVT_TRANSPORT_DEACTIVATED);
 s_dfu_settings.progress.firmware_image_offset=0;
 for(unsigned init=66;init<=143;init+=77) {
  s_dfu_settings.progress.command_size=init;
  dfu_observer(NRF_DFU_EVT_TRANSPORT_ACTIVATED);
  assert(timer1.CC[0]==240000&&((gpiote.CONFIG[0]>>8)&31)==18&&ppi.FORK[8].TEP==0);
  object_keeps_download_phase();
  dfu_observer(NRF_DFU_EVT_TRANSPORT_DEACTIVATED);
 }
 dfu_observer(NRF_DFU_EVT_TRANSPORT_ACTIVATED);
 /* The last object is not authoritative completion; retain download until
  * the validated/durable completion callback selects installation. */
 s_dfu_settings.bank_1.bank_code=1;object_keeps_download_phase();
 dfu_observer(NRF_DFU_EVT_DFU_COMPLETED);assert(timer1.CC[0]==120000&&((gpiote.CONFIG[0]>>8)&31)==18);
 timer1.TASKS_CLEAR=0;dfu_observer(NRF_DFU_EVT_OBJECT_RECEIVED);
 assert(timer1.CC[0]==120000&&!timer1.TASKS_CLEAR);
 dfu_observer(NRF_DFU_EVT_TRANSPORT_DEACTIVATED);
 dfu_observer(NRF_DFU_EVT_TRANSPORT_ACTIVATED);
 assert(timer1.CC[0]==120000&&((gpiote.CONFIG[0]>>8)&31)==18&&ppi.FORK[8].TEP==0);
 dfu_observer(NRF_DFU_EVT_DFU_FAILED);pulse(16,5,3,false);
 dfu_observer(NRF_DFU_EVT_TRANSPORT_DEACTIVATED);assert(ppi.CHENCLR==0x300&&gpiote.CONFIG[0]==0&&timer1.TASKS_STOP&&timer2.TASKS_STOP);
 delay_count=0;transport_result=7;timer2.CC[0]=99;
 assert(nrf_dfu_transports_init(dfu_observer)==7&&timer2.CC[0]==99);
 puts("PASS Recovery RGB startup; ready/connected; stable download phase across 17 objects; reconnect/cached-init resume; install; error; stop; failed transport stays dark");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/'recovery.c';path.write_text(prefix+block+observer+fixture)
    for polarity in (0,1):
        binary=Path(directory)/str(polarity)
        subprocess.run(['gcc','-std=c99','-O2','-Wall','-Werror','-Ifirmware/common/status',f'-DINKTAG_BOARD_STATUS_LED_ACTIVE_LOW={polarity}',str(path),'-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],check=True)
