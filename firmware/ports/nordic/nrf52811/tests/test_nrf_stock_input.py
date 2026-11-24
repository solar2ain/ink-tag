"""InkTag stock gesture selection; no new board/hardware qualification."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT/'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
adapter = (ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
board = (ROOT.parents[2]/'boards/nrf52811_aesl.h').read_text(encoding='utf-8')
assert '#define INKTAG_BOARD_BUTTON_PRESENT 0' in board
assert '#define INKTAG_BOARD_INPUT_QUALIFIED 0' in board
for name in ('epd_v2_send_input', 'epd_v2_set_input', 'epd_v2_button_configuration_access'):
    assert name not in adapter
for suffix in ('c', 'h'):
    assert not (ROOT/('compat/common/input/inktag_input_simulation.'+suffix)).exists()


def function(source, name):
    match = re.search(r'^(?:static )?[^\n;{}]+\b'+name+r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


action = function(service, 'epd_process_button_action')
for token in ('INKTAG_INPUT_ACTION_SHOW_SLOT', 'INKTAG_INPUT_ACTION_SHOW_CALENDAR',
              'INKTAG_INPUT_ACTION_SHOW_CLOCK', 'INKTAG_INPUT_ACTION_CONTROL_URL'):
    assert token not in action

prefix = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "inktag_input.h"
#include "inktag_protocol_v2.h"
typedef struct {int unused;} ble_epd_t;
static bool stock_hardware;
#define INKTAG_BOARD_BUTTON_PRESENT stock_hardware
static unsigned calls;
static uint8_t selected;
static struct {inktag_button_bindings_t button;} m_device_settings;
#define GFX_format snprintf
static void epd_send_text(ble_epd_t *p,const char *text) {(void)p;(void)text;}
static void epd_process_button_action(ble_epd_t *p,const inktag_input_binding_t *b) {
 (void)p;calls++;selected=b->action;assert(b->slot_id==0);
}
'''
main = r'''
int main(void) {
 ble_epd_t device;
 const uint8_t expected[]={0,INKTAG_INPUT_ACTION_NEXT_PAGE,
  INKTAG_INPUT_ACTION_TOGGLE_CLEAR_PREVIOUS,INKTAG_INPUT_ACTION_TOGGLE_SLIDESHOW};
 for(unsigned hardware=0;hardware<2;hardware++)for(unsigned legacy=0;legacy<256;legacy++) {
  stock_hardware=hardware;memset(&m_device_settings,legacy,sizeof(m_device_settings));
  uint8_t before[sizeof(m_device_settings)];memcpy(before,&m_device_settings,sizeof(before));
  for(unsigned gesture=0;gesture<256;gesture++) {
   calls=0;selected=255;
   ble_epd_handle_button_gesture(&device,(inktag_button_gesture_t)gesture);
   bool valid=hardware&&gesture>=1&&gesture<=3;
   assert(calls==(unsigned)valid);
   if(valid)assert(selected==expected[gesture]);
   assert(!memcmp(before,&m_device_settings,sizeof(before)));
  }
 }
 puts("PASS actual InkTag stock selector: all256 legacy byte values/gestures, fixed defaults, no record mutation; missing hardware rejects; EL022HWRL remains unqualified");
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    unit = out/'stock.c'
    unit.write_text(prefix+function(adapter, 'epd_v2_input_action_access')+
                    function(service, 'ble_epd_handle_button_gesture')+main, encoding='utf-8')
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie']
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', *flags,
                    '-Ifirmware/common/input', '-Ifirmware/common/epd', str(unit),
                    'compat/common/input/inktag_input.c', '-o', str(out/'test')], cwd=ROOT, check=True)
    subprocess.run([str(out/'test')], check=True)
