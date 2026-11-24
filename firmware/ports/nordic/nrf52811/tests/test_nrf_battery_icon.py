"""Exercise the real battery icon at policy boundaries in BW, BWR and BWRY."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
gui = ROOT / 'compat/common/gui'
source = (gui / 'inktag_gui_default.inc').read_text(encoding='utf-8')

def function(name):
    start = source.rfind('\nstatic ', 0, source.index(name + '(')) + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include <assert.h>
#include "Adafruit_GFX.h"
#include "inktag_gui.h"
#define V1_WHITE GFX_WHITE
#define V1_BLACK GFX_BLACK
#define V1_RED GFX_RED
''' + function('v1_battery_percent') + '\n' + function('v1_battery') + r'''
int main(void) {
    const uint16_t volts[] = {0, GUI_BATTERY_RED_MAX_MV-1,
        GUI_BATTERY_RED_MAX_MV, GUI_BATTERY_RED_MAX_MV+1, 3300};
    for (unsigned mode=1; mode<=3; mode++) for (unsigned v=0; v<5; v++) {
        Adafruit_GFX gfx;
        uint8_t buffer[256];
        if (mode==1) GFX_begin_with_buffer(&gfx,32,12,12,buffer,sizeof buffer);
        if (mode==2) GFX_begin_3c_with_buffer(&gfx,32,12,24,buffer,sizeof buffer);
        if (mode==3) GFX_begin_4c_with_buffer(&gfx,32,12,24,buffer,sizeof buffer);
        GFX_firstPage(&gfx); GFX_fillScreen(&gfx,GFX_WHITE);
        assert(v1_battery(&gfx,28,2,16,volts[v],8)==10);
        unsigned red=0,black=0;
        for (unsigned y=0; y<12; y++) for (unsigned x=0; x<32; x++) {
            bool r=false,b=false;
            if (mode==3) {
                unsigned code=(gfx.buffer[y*8+x/4] >> (6-2*(x%4))) & 3;
                r=code==3; b=code==0;
            } else {
                unsigned mask=0x80u >> (x%8),at=y*4+x/8;
                b=!(gfx.buffer[at]&mask);
                r=mode==2 && !(gfx.color[at]&mask);
            }
            if (r||b) assert(x>=10 && x<=27 && y>=2 && y<=9);
            red+=r; black+=b;
        }
        bool warning=volts[v] && volts[v]<=GUI_BATTERY_RED_MAX_MV;
        if (mode!=1 && warning) assert(red>0 && black==0);
        else assert(black>0 && red==0);
        GFX_end(&gfx);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temporary:
    out = Path(temporary)
    driver = out / 'battery.c'
    driver.write_text(harness)
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else [
        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        '-fno-sanitize-recover=all', '-no-pie']
    subprocess.run(['cc', '-std=gnu99', *flags, '-ffunction-sections', '-fdata-sections',
        '-DINKTAG_GUI_STATIC_BUFFER_ONLY=1', '-I'+str(gui),
        '-I'+str(ROOT/'compat/common/power'), str(driver),
        str(gui/'Adafruit_GFX.c'), '-Wl,--gc-sections', '-o', str(out/'battery')], check=True)
    subprocess.run([str(out/'battery')], check=True)
print('PASS battery warning boundary, missing sample and BW/BWR/BWRY mapping (15 cases)')
