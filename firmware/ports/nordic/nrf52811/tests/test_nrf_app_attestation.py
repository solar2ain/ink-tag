"""Exercise the production Nordic committed-Bank-0 identity encoder."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text()
start = source.index('static void epd_v2_application_info(')
end = source.index('\n}\n', start) + 3
body = source[start:end]
assert 'epd_v2_application_info(&payload[15]);' in source
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static uint32_t dfu_bank[9];
#define INKTAG_BOARD_DFU_SETTINGS_START ((uintptr_t)dfu_bank)
''' + body + r'''
#define settings dfu_bank
static void valid(void) {
    memset(settings, 0, sizeof(settings));
    settings[1]=2; settings[6]=69584; settings[7]=0x2662D7DD; settings[8]=1;
}
static void rejected(void) {
    uint8_t out[8]={0};
    const uint8_t empty[8]={0};
    epd_v2_application_info(out);
    assert(memcmp(out, empty, sizeof(out))==0);
}
int main(void) {
    uint8_t out[8]={0};
    const uint8_t expected[8]={0xD0,0x0F,0x01,0x00,0xDD,0xD7,0x62,0x26};
    valid(); epd_v2_application_info(out);
    assert(memcmp(out, expected, sizeof(out))==0);
    valid(); settings[6]=0x11000;
    memset(out,0,sizeof(out)); epd_v2_application_info(out);
    assert(memcmp(out, &settings[6],8)==0);
    valid(); settings[6]=0; rejected();
    valid(); settings[6]=0x11001; rejected();
    valid(); settings[6]=UINT32_MAX; rejected();
    valid(); settings[1]=0; rejected();
    valid(); settings[1]=1; rejected();
    valid(); settings[1]=UINT32_MAX; rejected();
    valid(); settings[8]=0; rejected();
    valid(); settings[8]=UINT32_MAX; rejected();
    valid(); settings[3]=65736; rejected();
    valid(); settings[3]=UINT32_MAX; rejected();
    puts("PASS committed Bank 0 size/CRC, full bound, invalid metadata and pending confirmation");
}
'''
with tempfile.TemporaryDirectory() as directory:
    file = Path(directory) / 'attestation.c'
    binary = Path(directory) / 'attestation'
    file.write_text(fixture)
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-O2', '-Wall',
                    '-Wextra', '-Werror', str(file), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
