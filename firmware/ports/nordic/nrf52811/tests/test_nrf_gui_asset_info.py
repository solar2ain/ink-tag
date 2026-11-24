"""Exercise the shipping InkTag query and parser; no owner package exists here."""
import argparse
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
def function(name):
    match=re.search(r'^static [^\n]+\b'+name+r'\([^;{]*\)\s*\{[\s\S]*?\n}',source,re.M)
    assert match,name
    return match.group()
start=source.index('static void epd_v2_process_packet(')
switch=source.index('    switch (request.opcode) {',start)
case_start=source.index('        case INKTAG_CMD_GET_GUI_ASSET_INFO:',switch)
case_end=source.index('        case INKTAG_CMD_GET_TIME:',case_start)
shipping=source[start:switch]+'    switch (request.opcode) {\n'+source[case_start:case_end]+'''
        default: assert(!"fixture only dispatches the GUI query");
    }
}
'''
driver=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_protocol_core.h"
#include "inktag_power.h"
#define NRF52811_XXAA 1
#include "inktag_board.h"
#define INKTAG_NRF_CMD_SET_IMAGE_METADATA 0x1e
#define INKTAG_NRF_IMAGE_STORE_ENABLED 1
#define INKTAG_NRF_FIND_ENABLED 0
#define INKTAG_NRF_ACTIVATION_ENABLED 0
#define INKTAG_NRF_INDICATOR_ENABLED 1
#define INKTAG_NRF_TEST_ENABLED 1
#define INKTAG_NRF_NATIVE_IMAGE_METADATA 0
#define INKTAG_NRF_SIMPLE_IMAGE_METADATA 1
#define INKTAG_NRF_CAPABILITY_IMAGE_METADATA UINT32_C(0x10000000)
#define INKTAG_NRF_HEALTH_OPERATION_BLE 1
#define EPD_V2_STORE_BUSY() store_busy
typedef struct { int unused; } ble_epd_t;
static bool m_epd_operation_active,m_epd_operation_pending,store_busy;
static struct { bool active; } m_v2_transfer;
static unsigned replies,power_checks,stages;
static uint8_t status,payload[20],opcode,transaction;
static unsigned response_length;
static bool inktag_image_store_supported(void) { return true; }
static void inktag_nrf_health_stage(unsigned stage,unsigned operation) {
    (void)stage;assert(operation==INKTAG_NRF_HEALTH_OPERATION_BLE);stages++;
}
static bool inktag_nrf_health_operation_allowed(unsigned operation,bool fresh) {
    (void)operation;(void)fresh;power_checks++;return false;
}
static uint8_t epd_v2_display_settings_write_access(const uint8_t *p,unsigned n) {
    (void)p;(void)n;assert(!"read query must not check settings write admission");return 0;
}
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t result,
        const uint8_t *data,unsigned length) {
    (void)p;replies++;opcode=op;transaction=tx;status=result;response_length=length;
    assert(length==0 || length==20);assert((data!=NULL)==(length!=0));
    if(length) memcpy(payload,data,length);
}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

''' + function('epd_v2_command_policy')+'\n'+function('epd_v2_command_requires_idle_display')+'\n'+function('epd_v2_command_admission_status')+'\n'+function('epd_v2_command_supported_on_target')+'\n'+function('epd_v2_capabilities')+'\n'+shipping+r'''
static void process_wire(ble_epd_t *p,const uint8_t *wire,uint16_t length){
    if(!wire || length<3)return;
    inktag_protocol_request_t request;
    uint8_t result=inktag_protocol_parse_request(wire,length,&request);
    if(result!=INKTAG_STATUS_OK){epd_v2_send_response(p,wire[0],wire[1],result,NULL,0);return;}
    uint8_t queued[260];assert(length<=sizeof(queued));memcpy(queued,wire,length);queued[2]=request.flags;
    epd_v2_process_packet(p,queued,length);
}

int main(int argc,char **argv) {
    assert(argc==2);
    uint8_t packet[260]={INKTAG_CMD_GET_GUI_ASSET_INFO,39,0};
    const uint8_t expected[20]={1,1,255,0,0,0,0,0,0,0,0,0,0,64,0,0,0,0,0,0};
    assert(expected[1]==1u);
    assert(inktag_read_u32_le(expected+12)==16384u);
    assert(!(epd_v2_capabilities() & (INKTAG_CAPABILITY_GUI_ASSET_UPDATE|INKTAG_CAPABILITY_ACTIVATION|INKTAG_CAPABILITY_FIND)));
    assert(!inktag_protocol_command_mutates_persistent_state(packet[0],NULL,0));
    assert(!epd_v2_command_requires_idle_display(packet[0]));
    assert(epd_v2_command_supported_on_target(packet[0]));
    assert(!epd_v2_command_supported_on_target(INKTAG_CMD_SET_ACTIVATION_CODE));
    for(unsigned busy=0;busy<16;busy++)
        for(unsigned flag=0;flag<256;flag++)
            for(unsigned length=0;length<256;length++) {
                m_epd_operation_active=busy&1;m_epd_operation_pending=busy&2;
                m_v2_transfer.active=busy&4;store_busy=busy&8;
                packet[2]=flag;replies=power_checks=stages=0;
                process_wire(NULL,packet,3+length);
                assert(replies==1 && opcode==packet[0] && transaction==39);
                assert(!power_checks);
                bool valid=length==0 && (flag&~(INKTAG_REQUEST_FLAG_CONFIRM|INKTAG_REQUEST_FLAG_FORCE))==0x10;
                assert(status==(valid?INKTAG_STATUS_OK:INKTAG_STATUS_INVALID_REQUEST));
                assert(response_length==(valid?20:0));
                if(valid) assert(!memcmp(payload,expected,20));
                assert(m_epd_operation_active==!!(busy&1) && m_epd_operation_pending==!!(busy&2));
                assert(m_v2_transfer.active==!!(busy&4) && store_busy==!!(busy&8));
            }
    packet[1]=0;packet[2]=0x10;process_wire(NULL,packet,3);
    assert(status==INKTAG_STATUS_INVALID_REQUEST && !response_length);
    replies=0;process_wire(NULL,packet,2);assert(!replies);
    packet[1]=39;packet[2]=0x10;process_wire(NULL,packet,3);
    assert(status==INKTAG_STATUS_OK && response_length==20);
    FILE *file=fopen(argv[1],"wb");assert(file);
    assert(fwrite(payload,response_length,1,file)==1);assert(!fclose(file));
    puts("PASS 1048576 real parser/query cases: standard no-owner fields, busy/low-power reads, invalid payload/flags/tx and no mutation");
}
'''
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cli-decoder',type=Path,help='Read-only frozen maintained CLI gui_assets.py; never copied here')
args=parser.parse_args()
with tempfile.TemporaryDirectory() as temp:
    out=Path(temp)
    (out/'driver.c').write_text(driver,encoding='utf-8')
    includes=['-I'+str(ROOT/p) for p in ('compat/common/epd','compat/common/protocol','compat/common/power',
                                      'compat/common/gui','../../../boards', 'compat/nordic/config')]
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu99','-O2','-Wall','-Werror',*includes,
                    str(out/'driver.c'),str(ROOT/'compat/common/protocol/inktag_protocol_core.c'),'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test'),str(out/'response.bin')],check=True)
    if args.cli_decoder:
        decode=runpy.run_path(str(args.cli_decoder))['decode_gui_asset_info']
        assert decode((out/'response.bin').read_bytes())=={
            'version':1,'schema':1,'activeBank':None,'generation':0,
            'activeBytes':0,'maximumBytes':16384,'crc32':0}
        print('PASS frozen maintained CLI decodes real shipping-C query bytes')
