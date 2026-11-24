"""Execute the actual InkTag HVX sender; preserve parameters and vendor status."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
match = re.search(r'^uint32_t ble_epd_string_send\([^;{]*\)\s*\{', source, re.M)
assert match
depth, end = 1, match.end()
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
shipping = source[match.start():end]
driver = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NRF_SUCCESS 0u
#define NRF_ERROR_INVALID_STATE 8u
#define NRF_ERROR_INVALID_PARAM 7u
#define BLE_CONN_HANDLE_INVALID 65535u
#define BLE_GATT_HVX_NOTIFICATION 1u
typedef struct {bool is_notification_enabled;uint16_t conn_handle,max_data_len;
 struct {uint16_t value_handle;} char_handles;} ble_epd_t;
typedef struct {uint16_t handle;uint8_t type;uint16_t *p_len;uint8_t *p_data;} ble_gatts_hvx_params_t;
static unsigned calls;
static uint32_t next_status;
static uint16_t expected_length;
static uint8_t *expected_data;
static uint32_t sd_ble_gatts_hvx(uint16_t handle,ble_gatts_hvx_params_t *p) {
 calls++;assert(handle==42&&p->handle==91&&p->type==BLE_GATT_HVX_NOTIFICATION);
 assert(p->p_len&&*p->p_len==expected_length&&p->p_data==expected_data);return next_status;
}
'''+shipping+r'''
int main(void) {
 uint8_t data[245];memset(data,0x5a,sizeof(data));expected_data=data;
 ble_epd_t epd={true,42,244,{91}};
 const uint32_t statuses[]={0,8,19,0xffffffff};
 for(unsigned n=0;n<=245;n++)for(unsigned s=0;s<4;s++) {
  calls=0;next_status=statuses[s];expected_length=n;
  uint32_t result=ble_epd_string_send(&epd,data,n);
  assert(calls==(n<=244));assert(result==(n<=244?next_status:NRF_ERROR_INVALID_PARAM));
  for(unsigned i=0;i<sizeof(data);i++)assert(data[i]==0x5a);
 }
 for(unsigned connected=0;connected<2;connected++)for(unsigned enabled=0;enabled<2;enabled++) {
  if(connected&&enabled)continue;
  epd.conn_handle=connected?42:BLE_CONN_HANDLE_INVALID;epd.is_notification_enabled=enabled;calls=0;
  assert(ble_epd_string_send(&epd,data,245)==NRF_ERROR_INVALID_STATE&&!calls);
 }
 puts("PASS actual InkTag notification sender:0..245B, exact HVX/handle/data, vendor status, disconnect/unsubscribe precedence, no mutation");
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    (out/'notify.c').write_text(driver, encoding='utf-8')
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie']
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror=return-type',
                    *flags, str(out/'notify.c'), '-o', str(out/'test')], check=True)
    subprocess.run([str(out/'test')], check=True)
