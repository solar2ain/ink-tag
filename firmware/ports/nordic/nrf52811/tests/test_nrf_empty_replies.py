"""Compare the production empty-reply adapter with the original wire writer."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')


def function(name):
    match = re.search(r'^static [^\n;{}]+\b'+name+r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


unit = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "inktag_protocol_core.h"
#define BLE_EPD_MAX_DATA_LEN 244
typedef int ble_epd_t;
static uint8_t packet[244];static unsigned length;
static unsigned ble_epd_string_send(ble_epd_t*p,uint8_t*b,uint16_t n){
 (void)p;assert(n<=sizeof(packet));memcpy(packet,b,n);length=n;return 0;
}
''' + function('epd_v2_send_response') + function('epd_v2_reply_status') + r'''
int main(void){
 for(unsigned op=0;op<256;op++)for(unsigned tx=0;tx<256;tx++)for(unsigned status=0;status<256;status++){
  epd_v2_send_response(NULL,op,tx,(inktag_status_v2_t)status,NULL,0);
  assert(length==3);uint8_t expected[3];memcpy(expected,packet,3);
  epd_v2_reply_status(NULL,op,tx,(inktag_status_v2_t)status);
  assert(length==3&&!memcmp(expected,packet,3));
 }
 puts("PASS16777216 production empty replies: every opcode/transaction/status byte identical");
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    (out/'test.c').write_text(unit, encoding='utf-8', newline='\n')
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
    subprocess.run(['cc','-std=c99','-Wall','-Werror',*flags,
        '-I'+str(ROOT/'compat/common/protocol'),'-I'+str(ROOT/'compat/common/epd'),
        str(out/'test.c'),'-o',str(out/'test')], check=True)
    subprocess.run([str(out/'test')], check=True)
