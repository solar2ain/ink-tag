"""Exercise identity new-write ASCII gates and legacy UTF-8 read validators."""
from pathlib import Path
import os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
config=(ROOT/'compat/common/epd/inktag_epd_config.c').read_text(encoding='utf-8')
def extract(text,name):
 m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{]*\)\s*\{[\s\S]*?\n}',text,re.M);assert m,name
 return m.group()
start=s.index('static void epd_v2_set_settings(');end=s.index('    } else if (length >= INKTAG_DISPLAY_SETTINGS_BASE_SIZE',start)
identity=s[start:end]+'    } else {epd_v2_send_response(p_epd,opcode,transaction_id,INKTAG_STATUS_INVALID_REQUEST,NULL,0);}\n}\n'
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_epd_config.h"
#include "inktag_protocol_core.h"
#include "inktag_text.h"
#include "inktag_device_display_name.h"
#define INKTAG_NRF_ACTIVATION_ENABLED 0
#define NRF_SUCCESS 0
#define INKTAG_EVENT_DEVICE_CHANGED 1
typedef struct {int unused;} ble_epd_t;
typedef int ble_gap_conn_sec_mode_t;
#define BLE_GAP_CONN_SEC_MODE_SET_OPEN(x) ((void)(x))
static epd_device_settings_t m_device_settings,saved;
static unsigned writes,gap_writes;
static bool fail_write;
static uint8_t status,reboot;
static const char current_name[]="IKT_NRF_ABCD";
static unsigned sd_ble_gap_device_name_get(uint8_t *out,uint16_t *length) {assert(*length>=strlen(current_name));*length=strlen(current_name);memcpy(out,current_name,*length);return 0;}
static unsigned sd_ble_gap_device_name_set(const void *security,const uint8_t *name,uint8_t length) {(void)security;(void)name;(void)length;gap_writes++;return 0;}
bool epd_device_settings_write(const epd_device_settings_t *next) {writes++;if(fail_write)return false;saved=*next;return true;}
static void epd_v2_send_response(ble_epd_t *p,uint8_t op,uint8_t tx,uint8_t st,const uint8_t *data,uint16_t n) {(void)p;(void)op;(void)tx;status=st;reboot=n?data[0]:255;}
#define epd_v2_reply_status(p,o,t,s) epd_v2_send_response(p,o,t,s,NULL,0u)

static void epd_v2_send_event(ble_epd_t *p,uint8_t ev,uint8_t st,uint8_t op,uint8_t tx,const uint8_t *data,uint16_t n) {(void)p;(void)ev;(void)st;(void)op;(void)tx;(void)data;(void)n;}
'''
main=r'''
static void label(const char *name,const uint8_t *value,uint8_t length) {
 uint8_t wire[80]={INKTAG_SETTINGS_SECTION_IDENTITY,EPD_DEVICE_SETTINGS_VERSION,0,length};wire[2]=strlen(name);memcpy(wire+4,name,wire[2]);memcpy(wire+4+wire[2],value,length);epd_v2_set_settings(NULL,0,1,wire,4+wire[2]+length);
}
int main(void) {
 memset(&m_device_settings,0,sizeof(m_device_settings));m_device_settings.version=1;
 uint8_t value[25];memset(value,'a',sizeof(value));
 const char *write_name=INKTAG_NRF_DEVICE_NAME_WRITABLE?current_name:"";
 label(write_name,value,24);assert(status==INKTAG_STATUS_OK&&writes==1&&m_device_settings.label_length==24);
#if INKTAG_NRF_DEVICE_NAME_WRITABLE
 assert(gap_writes==1&&reboot==1&&saved.name_length==strlen(current_name));
#else
 assert(!gap_writes&&!reboot&&!saved.name_length);
#endif
 epd_device_settings_t before=m_device_settings;
 label(write_name,value,25);assert(status==INKTAG_STATUS_INVALID_REQUEST&&writes==1);
 label("IKT_NRF_A1B2",value,2);assert(status==(INKTAG_NRF_DEVICE_NAME_WRITABLE?INKTAG_STATUS_DENIED:INKTAG_STATUS_INVALID_REQUEST)&&writes==1&&!memcmp(&before,&m_device_settings,sizeof(before)));
 value[0]=0;label(write_name,value,1);assert(status==INKTAG_STATUS_INVALID_REQUEST&&writes==1);value[0]=0xc0;value[1]=0x80;label(write_name,value,2);assert(status==INKTAG_STATUS_INVALID_REQUEST&&writes==1);
 memset(value,'a',sizeof(value));fail_write=true;label(write_name,value,3);assert(status==INKTAG_STATUS_PERSISTENCE_ERROR&&!memcmp(&before,&m_device_settings,sizeof(before)));fail_write=false;
 for(unsigned n=0;n<24;n+=3){value[n]=0xe6;value[n+1]=0xb5;value[n+2]=0x8b;}
 assert(epd_device_label_valid(value,24));
 unsigned prior_writes=writes;label(write_name,value,24);assert(status==INKTAG_STATUS_INVALID_REQUEST&&writes==prior_writes&&!memcmp(&before,&m_device_settings,sizeof(before)));
 memset(value,'a',sizeof(value));
 label("",value,24);
#if INKTAG_NRF_DEVICE_NAME_WRITABLE
 assert(status==INKTAG_STATUS_INVALID_REQUEST);
#else
 assert(status==INKTAG_STATUS_OK&&!saved.name_length&&!gap_writes);
#endif
 label(write_name,value,0);assert(status==INKTAG_STATUS_OK&&!m_device_settings.label_length);
#if !INKTAG_NRF_DEVICE_NAME_WRITABLE
 prior_writes=writes;label(current_name,value,0);assert(status==INKTAG_STATUS_INVALID_REQUEST&&writes==prior_writes);
#endif
 puts("PASS production identity: main-compatible read-only Name field, label-only, 24B ASCII, UTF-8 retained reads/new-write rejection, denial, failure, no InkTag GAP rename");
}
'''
with tempfile.TemporaryDirectory() as temp:
 c=Path(temp)/'test.c';c.write_text(prefix+extract(config,'device_settings_utf8_valid')+'\n'+extract(config,'epd_device_name_valid')+'\n'+extract(config,'epd_device_label_valid')+'\n'+extract(s,'epd_v2_runtime_name')+'\n'+identity+main)
 for writable in (0,):
  flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
  b=Path(temp)/str(writable);subprocess.run(['gcc','-std=c99',*flags,'-Ifirmware/common/epd','-Ifirmware/common/ble','-Ifirmware/common/input','-Ifirmware/common/protocol',f'-DINKTAG_NRF_DEVICE_NAME_WRITABLE={writable}',str(c),'compat/common/protocol/inktag_text.c','-o',str(b)],cwd=ROOT,check=True);subprocess.run([str(b)],check=True)
