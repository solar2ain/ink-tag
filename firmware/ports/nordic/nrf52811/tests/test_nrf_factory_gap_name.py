"""Run the shipping GAP initializer; stale user names never change InkTag GAP."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--main-source', type=Path,
                    default=ROOT/'compat/nordic/src/main.c')
args = parser.parse_args()
source = args.main_source.read_text(encoding='utf8')
match = re.search(r'^static void gap_params_init\(void\)\s*\{', source, re.M)
assert match
end = match.end()
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
function = source[match.start():end]
driver = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define INKTAG_NRF_SDK17 1
#define INKTAG_DEVICE_SETTINGS_ENABLED 1
#define DEVICE_NAME "IKT_NRF"
#define MIN_CONN_INTERVAL 6
#define MAX_CONN_INTERVAL 12
#define SLAVE_LATENCY 0
#define CONN_SUP_TIMEOUT 400
#define NRF_LOG_INFO(...) ((void)0)
#define APP_ERROR_CHECK(value) assert((value)==0)
#define BLE_GAP_CONN_SEC_MODE_SET_OPEN(p) ((p)->open=1)
typedef struct { uint8_t addr[6]; } ble_gap_addr_t;
typedef struct { unsigned open; } ble_gap_conn_sec_mode_t;
typedef struct { unsigned min_conn_interval,max_conn_interval,slave_latency,conn_sup_timeout; } ble_gap_conn_params_t;
typedef struct { uint8_t name_length,name[20],label_length,label[24],buttons[6]; } epd_device_settings_t;
static epd_device_settings_t old;
static uint8_t a[6];
static unsigned reads,sets,params;
static char advertised[32];
static int sd_ble_gap_addr_get(ble_gap_addr_t *out){memcpy(out->addr,a,6);return 0;}
static int sd_ble_gap_device_name_set(const ble_gap_conn_sec_mode_t *sec,const uint8_t *p,unsigned n){
 assert(sec->open==1&&n<sizeof(advertised));memcpy(advertised,p,n);advertised[n]=0;++sets;return 0;
}
static int sd_ble_gap_ppcp_set(const ble_gap_conn_params_t *p){
 assert(p->min_conn_interval==6&&p->max_conn_interval==12&&!p->slave_latency&&p->conn_sup_timeout==400);++params;return 0;
}
static int epd_device_settings_read(epd_device_settings_t *out){++reads;*out=old;return 1;}
static int GFX_format(char *out,unsigned n,const char *fmt,...){va_list v;va_start(v,fmt);int r=vsnprintf(out,n,fmt,v);va_end(v);return r;}
FUNCTION
int main(void){
 memset(&old,0xA5,sizeof(old));old.name_length=12;memcpy(old.name,"IKT_NRF_Old1",12);
 old.label_length=4;memcpy(old.label,"Desk",4);
 epd_device_settings_t saved=old;
 for(unsigned identity=0;identity<65536;identity++){
  a[0]=(uint8_t)identity;a[1]=(uint8_t)(identity>>8);a[2]=0x12;a[3]=0x34;a[4]=0x56;a[5]=0x78;
  gap_params_init();char expected[20];snprintf(expected,sizeof(expected),"IKT_NRF_%02X%02X",a[1],a[0]);
  assert(!strcmp(advertised,expected));assert(!reads);assert(!memcmp(&old,&saved,sizeof(old)));
 }
 assert(sets==65536&&params==65536);
 puts("PASS actual GAP initializer:65536 factory identities; stale name/label/button records not read or mutated; connection settings retained");
}
'''.replace('FUNCTION', function)
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    c = out/'gap.c'
    c.write_text(driver, encoding='utf8', newline='\n')
    flags = ['-O2']
    if os.environ.get('INKTAG_TEST_SANITIZERS'):
        flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie']
    subprocess.run([os.environ.get('CC','cc'), *flags, '-std=gnu99', '-Wall',
                    '-Werror=return-type', str(c), '-o', str(out/'gap')], check=True)
    subprocess.run([str(out/'gap')], check=True)
