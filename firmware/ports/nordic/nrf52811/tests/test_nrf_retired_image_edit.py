"""Retired 0x1e stays unsupported in the real parser, and opt-ins fail at build time."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
def function(name):
 match=re.search(r'^static [^\n]+\b'+name+r'\([^;{]*\)\s*\{[\s\S]*?\n}',source,re.M)
 assert match,name
 return match.group()
queue=function('epd_v2_queue_packet');a=0;b=queue.index('    if (packet[0] == INKTAG_CMD_CANCEL')
shipping=queue[a:b]+'''    assert(!"retired command must return before mailbox admission");
'''+queue[queue.index('reply:'):]
DRIVER=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_protocol_core.h"
#include "inktag_release_v1.h"
#include "inktag_power.h"
#include "inktag_nrf_unsupported_status.h"
#define INKTAG_NRF_CMD_SET_IMAGE_METADATA 0x1e
#define INKTAG_NRF_HEALTH_OPERATION_BLE 1
#define INKTAG_BOARD_INPUT_QUALIFIED 0
#define EPD_V2_STORE_BUSY() store_busy
typedef struct { int unused; } ble_epd_t;
static bool m_epd_operation_active,m_epd_operation_pending,store_busy;
static struct { bool active; } m_v2_transfer;
static unsigned replies,power_checks,settings_checks,stages;
static uint8_t m_v2_rx_packet[512];
static uint8_t status,opcode,transaction;
static __attribute__((unused)) void inktag_nrf_health_stage(unsigned stage,unsigned operation) { (void)stage;assert(operation==INKTAG_NRF_HEALTH_OPERATION_BLE);stages++; }
static __attribute__((unused)) bool inktag_nrf_health_operation_allowed(unsigned operation,bool fresh) { (void)operation;(void)fresh;power_checks++;return false; }
static __attribute__((unused)) uint8_t epd_v2_display_settings_write_access(const uint8_t *p,unsigned n) { (void)p;(void)n;settings_checks++;return 0; }
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t result,const uint8_t *data,unsigned length) {
 (void)p;replies++;opcode=op;transaction=tx;status=result;assert(!data && !length);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

''' + function('epd_v2_command_policy')+'\n'+function('epd_v2_command_requires_idle_display')+'\n'+function('epd_v2_command_admission_status')+'\n'+function('epd_v2_command_supported_on_target')+'\n'+shipping+r'''
static void process_wire(ble_epd_t *p,const uint8_t *wire,uint16_t length){
    ble_epd_t actual; (void)p;
    epd_v2_queue_packet(&actual,wire,length);
}

int main(void) {
 uint8_t packet[260];memset(packet,0xff,sizeof(packet));packet[0]=0x1e;packet[1]=39;
 assert(!epd_v2_command_supported_on_target(0x1e));
 assert(!epd_v2_command_requires_idle_display(0x1e));
 for(unsigned busy=0;busy<16;busy++)
  for(unsigned flag=0;flag<256;flag++)
   for(unsigned length=0;length<256;length++) {
    m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;m_v2_transfer.active=busy&4;store_busy=busy&8;
    packet[2]=flag;replies=power_checks=settings_checks=stages=0;
    process_wire(NULL,packet,3+length);
    assert(replies==1 && opcode==0x1e && transaction==39);
    bool valid=(flag&~(INKTAG_REQUEST_FLAG_CONFIRM|INKTAG_REQUEST_FLAG_FORCE))==0x10;
    assert(stages==0);assert(status==(valid?INKTAG_STATUS_UNSUPPORTED:INKTAG_STATUS_INVALID_REQUEST));
    assert(!power_checks && !settings_checks);
    assert(m_epd_operation_active==!!(busy&1) && m_epd_operation_pending==!!(busy&2));
    assert(m_v2_transfer.active==!!(busy&4) && store_busy==!!(busy&8));
   }
 for(unsigned tx=0;tx<256;tx++) {
  packet[1]=tx;packet[2]=0x10;process_wire(NULL,packet,3);
  assert(status==(tx?INKTAG_STATUS_UNSUPPORTED:INKTAG_STATUS_INVALID_REQUEST));
 }
 replies=0;process_wire(NULL,packet,2);assert(!replies);
 process_wire(NULL,NULL,0);assert(!replies);
 puts("PASS 1048576 retired-command parser cases plus all transaction IDs/short/null input; no power gate, store write or busy-state mutation");
}
'''
with tempfile.TemporaryDirectory() as temp:
 out=Path(temp);(out/'driver.c').write_text(DRIVER,encoding='utf-8')
 includes=['-I'+str(ROOT/p) for p in ('compat/common/epd','compat/common/protocol','compat/common/power','compat/nordic/src')]
 for features in range(8):
  defines=[f'-DINKTAG_NRF_IMAGE_STORE_ENABLED={features&1}',f'-DINKTAG_NRF_FIND_ENABLED={(features>>1)&1}',f'-DINKTAG_NRF_ACTIVATION_ENABLED={(features>>2)&1}','-DINKTAG_NRF_INDICATOR_ENABLED=1','-DINKTAG_NRF_TEST_ENABLED=1']
  subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O2','-Wall','-Werror',*includes,*defines,str(out/'driver.c'),str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),'-o',str(out/'test')],check=True)
  subprocess.run([str(out/'test')],check=True)
 (out/'header.c').write_text('#include "inktag_nrf_image_store.h"\n',encoding='utf-8')
 for option in ('INKTAG_NRF_NATIVE_IMAGE_METADATA','INKTAG_NRF_IMAGE_METADATA_EDIT'):
  for value in ('1','2','-1'):
   result=subprocess.run([os.environ.get('CC','cc'),'-std=c99','-fsyntax-only',*includes,f'-D{option}={value}',str(out/'header.c')],text=True,capture_output=True)
   assert result.returncode and 'editing and native COW are retired' in result.stderr,(option,value,result.stderr)
  for value in ('1','2','-1','yes'):
   result=subprocess.run(['make','-f','app/Makefile','-n',f'{option}={value}'],cwd=ROOT,text=True,capture_output=True)
   assert result.returncode and 'retired' in result.stderr,(option,value,result.stderr)
print('PASS retired metadata/COW opt-ins rejected by both C header and maintained Makefile; 8 feature configurations')
