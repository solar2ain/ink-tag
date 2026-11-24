"""Exercise real v1 parser/queue admission, cancellation and normalized mailbox."""
from pathlib import Path
import os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
adapter=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
def function(name):
    found=re.search(r'^static [^\n]+\b'+name+r'\([^;{]*\)\s*\{',adapter,re.M);assert found,name
    depth=1;i=found.end()
    while depth:
        if adapter[i]=='{':depth+=1
        if adapter[i]=='}':depth-=1
        i+=1
    return adapter[found.start():i]
PREFIX=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "inktag_protocol_core.h"
#include "inktag_release_v1.h"
#define INKTAG_NRF_V2_OPERATION_BLE 1
#define NRF_SUCCESS 0
#define EPD_V2_STORE_BUSY() store_busy
typedef struct {unsigned unused;} ble_epd_t;
static bool m_epd_operation_active,m_epd_operation_pending,m_epd_cancel_requested;
static struct {unsigned origin;} m_epd_active_operation,m_epd_pending_operation;
static struct {bool active;} m_v2_transfer;
static bool store_busy,m_v2_rx_pending,m_v2_rx_processing,scheduler_fail;
static uint8_t m_v2_rx_packet[512];static uint16_t m_v2_rx_length;
static ble_epd_t *m_v2_display_read_service;
static unsigned replies,scheduled,stopped;static uint8_t response_status;
static void epd_v2_send_response(ble_epd_t*p,uint8_t op,uint8_t tx,uint8_t status,const void*d,unsigned n){(void)p;(void)op;(void)tx;assert(!d&&!n);replies++;response_status=status;}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_slideshow_stop(ble_epd_t*p,bool notify){(void)p;assert(!notify);stopped++;}
static void epd_v2_process_queued(void*p,uint16_t n){(void)p;(void)n;}
static int app_sched_event_put(void*p,unsigned n,void(*fn)(void*,uint16_t)){assert(p&&n==sizeof(ble_epd_t*)&&fn==epd_v2_process_queued);scheduled++;return scheduler_fail?1:0;}
'''
MAIN=r'''
static void reset(void){
 m_epd_operation_active=m_epd_operation_pending=m_epd_cancel_requested=false;
 m_v2_rx_pending=m_v2_rx_processing=scheduler_fail=store_busy=m_v2_transfer.active=false;
 m_v2_rx_length=0;replies=scheduled=stopped=0;m_v2_display_read_service=NULL;
 memset(m_v2_rx_packet,0xa5,sizeof(m_v2_rx_packet));
}
int main(void){
 ble_epd_t device={0};uint8_t packet[513]={INKTAG_CMD_GET_DEVICE_INFO,1,0x10,0x42};
 for(unsigned flags=0;flags<256;flags++)for(unsigned tx=0;tx<256;tx++){
  reset();packet[1]=tx;packet[2]=flags;
  epd_v2_queue_packet(&device,packet,4);
  bool valid=tx && (flags&252u)==16u;
  assert(m_v2_rx_pending==valid && scheduled==(unsigned)valid);
  if(valid){assert(!replies&&m_v2_rx_length==4&&m_v2_rx_packet[2]==(flags&3u)&&m_v2_rx_packet[3]==0x42);}
  else assert(replies==1&&response_status==INKTAG_STATUS_INVALID_REQUEST&&m_v2_rx_packet[0]==0xa5);
 }
 reset();epd_v2_queue_packet(NULL,packet,4);epd_v2_queue_packet(&device,NULL,4);
 for(unsigned n=0;n<3;n++)epd_v2_queue_packet(&device,packet,n);
 epd_v2_queue_packet(&device,packet,513);assert(!scheduled&&!replies&&!m_v2_rx_pending);
 packet[0]=INKTAG_CMD_CANCEL;packet[1]=9;
 for(unsigned flags=0;flags<256;flags++){
  reset();m_epd_operation_active=m_epd_operation_pending=true;
  m_epd_active_operation.origin=m_epd_pending_operation.origin=INKTAG_NRF_V2_OPERATION_BLE;
  packet[2]=flags;epd_v2_queue_packet(&device,packet,4);
  bool valid=(flags&252u)==16u;
  assert(stopped==(unsigned)valid&&m_epd_cancel_requested==valid&&m_epd_operation_pending==!valid);
  assert(replies==1&&response_status==(valid?INKTAG_STATUS_OK:INKTAG_STATUS_INVALID_REQUEST));
 }
 packet[0]=INKTAG_CMD_SET_SCREEN_PROFILE;packet[2]=16;
 for(unsigned busy=1;busy<16;busy++){
  reset();m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;m_v2_transfer.active=busy&4;store_busy=busy&8;
  epd_v2_queue_packet(&device,packet,4);assert(replies==1&&response_status==INKTAG_STATUS_BUSY&&!scheduled);
 }
 packet[0]=INKTAG_CMD_GET_DEVICE_INFO;
 for(unsigned busy=1;busy<4;busy++){
  reset();m_v2_rx_pending=busy&1;m_v2_rx_processing=busy&2;
  epd_v2_queue_packet(&device,packet,4);assert(replies==1&&response_status==INKTAG_STATUS_BUSY&&!scheduled&&m_v2_rx_packet[0]==0xa5);
 }
 reset();scheduler_fail=true;epd_v2_queue_packet(&device,packet,4);
 assert(!m_v2_rx_pending&&!m_v2_rx_length&&response_status==INKTAG_STATUS_INTERNAL_ERROR);
 reset();m_epd_operation_active=true;packet[0]=INKTAG_CMD_GET_TIME;
 epd_v2_queue_packet(&device,packet,4);assert(m_v2_rx_pending&&!scheduled&&m_v2_display_read_service==&device);
 const unsigned retained[]={INKTAG_CMD_SET_TIME,INKTAG_CMD_SET_SCREEN_PROFILE,INKTAG_CMD_SET_SETTINGS,INKTAG_CMD_SET_INDICATOR_CONFIG};
 for(unsigned i=0;i<sizeof(retained)/sizeof(*retained);i++)assert(inktag_protocol_command_mutates_persistent_state(retained[i],NULL,0));
 const unsigned excluded[]={INKTAG_CMD_SET_INPUT_CONFIG,INKTAG_CMD_SET_IO_CONFIG,INKTAG_CMD_SET_NFC_NDEF,INKTAG_CMD_SET_ACTIVATION_CODE,INKTAG_CMD_ACTIVATE_LAYOUT,INKTAG_CMD_SET_FIND_CONFIG,INKTAG_CMD_FIND_ACTION};
 for(unsigned i=0;i<sizeof(excluded)/sizeof(*excluded);i++)assert(!inktag_protocol_command_mutates_persistent_state(excluded[i],NULL,0));
 puts("PASS actual v1 queue: 65536 flags/tx combinations, cancellation, BUSY, mailbox leases, scheduler failure and InkTag mutation gates");
}
'''
with tempfile.TemporaryDirectory() as d:
    out=Path(d);(out/'driver.c').write_text(PREFIX+'\n'.join(function(n) for n in ('epd_v2_command_policy','epd_v2_command_requires_idle_display','epd_v2_command_admission_status','epd_v2_defer_display_read','epd_v2_queue_packet'))+MAIN)
    flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    subprocess.run(['cc','-std=gnu99',*flags,'-DNRF52811_XXAA','-I'+str(ROOT/'compat/common/protocol'),'-I'+str(ROOT/'compat/common/epd'),str(out/'driver.c'),str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test')],check=True)
