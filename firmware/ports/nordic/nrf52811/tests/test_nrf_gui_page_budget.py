"""Exercise the production Nordic page options with real GUI/glyph rendering."""
import os
from pathlib import Path
import re
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GUI = ROOT / 'compat/common/gui'
service = (ROOT / 'compat/common/epd/inktag_epd_service.c').read_text(encoding='utf-8')
options = re.search(r'gui_options_t page_options\s*=\s*(\{[\s\S]*?\n\s*\});', service)
assert options, 'Native-page production options missing'
make = (ROOT / 'app/Makefile').read_text(encoding='utf-8')
budget = int(re.search(r'^INKTAG_GUI_RENDER_BUFFER_SIZE\s*:=\s*(\d+)', make, re.M)[1])
entries = int(re.search(r'^INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES\s*\?=\s*(\d+)', make, re.M)[1])
assert budget == 8000 and entries == 16

DRIVER = r'''
#include "inktag_gui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t page_buffer[RENDER_BUDGET], full_buffer[106000];
static uint8_t reference[106000], actual[106000];
typedef struct { uint8_t *pixels; size_t plane; unsigned pages, y; } capture_t;
static void capture(void *ctx,uint8_t *b,uint8_t *r,uint16_t x,uint16_t y,uint16_t w,uint16_t h) {
    capture_t *c=ctx;size_t row=(w+7u)/8u,n=row*h;
    assert(!x && y==c->y);c->y+=h;++c->pages;
    if(b==r)memcpy(c->pixels+row*y*2u,b,n*2u);
    else {memcpy(c->pixels+row*y,b,n);if(r)memcpy(c->pixels+c->plane+row*y,r,n);}
}
#define EPD_GUI_RENDER_BUFFER_FIELDS \
    .render_buffer=page_buffer,.render_buffer_size=sizeof(page_buffer),
static gui_options_t production_options(void) {
    gui_options_t options = PRODUCTION_OPTIONS;
    return options;
}
int main(void) {
    const unsigned widths[]={128,400,768},heights[]={250,300,552},colors[]={2,2,3};
#if defined(INKTAG_NRF_BOARD_NRF52811_AESL)
    const unsigned expected_pages[]={1,10,18};
#else
    /* Keep the other Nordic target's conservative page policy unchanged. */
    const unsigned expected_pages[]={8,10,18};
#endif
    for(unsigned geometry=0;geometry<3;++geometry)for(unsigned rotation=0;rotation<4;++rotation)
    for(unsigned week=0;week<2;++week){
        gui_data_t data={.width=widths[geometry],.height=heights[geometry],
            .visible_width=geometry?widths[geometry]:122,.visible_height=heights[geometry],
            .force_rotation=true,.rotation=rotation,.color=colors[geometry],.mode=MODE_CALENDAR,
            .timestamp=1792886400u,.temperature=20,.voltage=3100,.week_start=week};
        strcpy(data.ssid,"IKT_NRF_F988");
        size_t plane=((data.width+7u)/8u)*data.height;
        gui_options_t options=production_options();
        capture_t paged={actual,plane,0,0},full={reference,plane,0,0};
        memset(actual,0,sizeof(actual));memset(reference,0,sizeof(reference));
        assert(DrawGUIEx(&data,capture,&paged,&options));
        assert(paged.pages==expected_pages[geometry] && paged.y==data.height);
        options.render_buffer=full_buffer;options.render_buffer_size=sizeof(full_buffer);
        assert(DrawGUIEx(&data,capture,&full,&options));
        assert(full.pages==1 && full.y==data.height);
        assert(!memcmp(actual,reference,2u*plane));
    }
    puts("PASS production Nordic calendar options: 24 paged/full frames match, exact page counts and fixed buffers");
}
'''.replace('PRODUCTION_OPTIONS', options[1])

with tempfile.TemporaryDirectory() as temp:
    out = Path(temp)
    driver = out / 'driver.c'
    driver.write_text(DRIVER, encoding='utf-8')
    cached = out / 'font.c'
    runpy.run_path(str(ROOT/'build-support/gui/cache-font-glyphs.py'))['generate'](GUI/'u8g2_font.c', cached, entries)
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else [
        '-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-fno-sanitize-recover=all','-no-pie']
    for board, budget_arg in [('nrf11', budget), ('nrf32', 7168)]:
        args = ['cc', '-std=gnu99', *flags, '-DINKTAG_GUI_RAW_VIEWPORT',
                '-DINKTAG_GUI_STATIC_BUFFER_ONLY', '-DRENDER_BUDGET='+str(budget_arg),
                '-I'+str(GUI), '-I'+str(ROOT/'compat/common/epd'),
                '-I'+str(ROOT/'compat/common/protocol')]
        if board == 'nrf11': args += ['-DNRF52811_XXAA', '-DINKTAG_NRF_BOARD_NRF52811_AESL']
        else: args += ['-DNRF52832_XXAA']
        binary = out / board
        sources = [driver, GUI/'inktag_gui.c', GUI/'inktag_calendar.c', GUI/'Adafruit_GFX.c',
                   cached, GUI/'inktag_gui_fonts.c', GUI/'inktag_gui_device_qr_asset.c']
        subprocess.run([*args, *map(str,sources), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
