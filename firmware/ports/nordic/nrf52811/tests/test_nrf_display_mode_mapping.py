"""Compare the shipping persisted-to-wire mapping against the prior switch."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
function = re.search(r'^static uint8_t epd_v2_current_display_mode\(void\) \{[\s\S]*?\n}', source, re.M)
assert function
driver = r'''
#include <assert.h>
#include <stdio.h>
#include "inktag_epd_config.h"
#include "inktag_protocol_v2.h"
static bool m_display_state_loaded;
static struct { uint8_t mode, argument; } m_display_state;
'''+function.group()+r'''
static uint8_t prior_mapping(void) {
    if (!m_display_state_loaded) return INKTAG_DISPLAY_MODE_DEVICE_INFO;
    switch (m_display_state.mode) {
        case EPD_DISPLAY_STATE_CALENDAR: return INKTAG_DISPLAY_MODE_CALENDAR;
        case EPD_DISPLAY_STATE_CLOCK: return INKTAG_DISPLAY_MODE_CLOCK;
        case EPD_DISPLAY_STATE_CLEAR: return INKTAG_DISPLAY_MODE_CLEAR;
        case EPD_DISPLAY_STATE_SLIDESHOW: return INKTAG_DISPLAY_MODE_SLIDESHOW;
        case EPD_DISPLAY_STATE_DEVICE_INFO: return INKTAG_DISPLAY_MODE_DEVICE_INFO;
        default: return m_display_state.argument == EPD_DISPLAY_STATE_NO_SOURCE ?
            INKTAG_DISPLAY_MODE_DIRECT_IMAGE : INKTAG_DISPLAY_MODE_RESOURCE;
    }
}
int main(void) {
    for (unsigned loaded=0; loaded<2; loaded++)
        for (unsigned mode=0; mode<256; mode++)
            for (unsigned argument=0; argument<256; argument++) {
                m_display_state_loaded=loaded; m_display_state.mode=mode;
                m_display_state.argument=argument;
                assert(epd_v2_current_display_mode()==prior_mapping());
            }
    puts("PASS all 131072 display-state mappings including unknown modes");
}
'''
with tempfile.TemporaryDirectory() as temp:
    out = Path(temp)
    (out/'driver.c').write_text(driver, encoding='utf-8')
    includes = ['-I'+str(ROOT/p) for p in ('compat/common/epd', 'compat/common/ble', 'compat/common/input')]
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O2', '-Wall', '-Werror', *includes,
                    str(out/'driver.c'), '-o', str(out/'test')], check=True)
    subprocess.run([str(out/'test')], check=True)
