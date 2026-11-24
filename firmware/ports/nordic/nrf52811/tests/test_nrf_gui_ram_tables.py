"""Compare every pixel of the original and RAM-table native renderers."""
import importlib.util
import runpy
from pathlib import Path
import subprocess
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tables', ROOT / 'build-support/gui/ram-gui-tables.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

DRIVER = r'''
#include "inktag_gui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef RAM_TABLES
#include "inktag_nrf_gui_tables.h"
uint8_t inktag_nrf_gui_tables[INKTAG_NRF_GUI_TABLES_SIZE];
#endif
static uint8_t buffer[8000];
static unsigned callbacks;
static unsigned checks, fail_check;
static bool discard_pixels;
bool inktag_gui_assets_healthy(void) { return ++checks != fail_check; }
static void capture(void *context, uint8_t *black, uint8_t *red, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    (void)context; callbacks++;
    if (discard_pixels) return;
    uint16_t geometry[] = {x, y, w, h};
    fwrite(geometry, sizeof(geometry), 1, stdout);
    size_t count = ((w + 7u) / 8u) * h;
    fwrite(black, count, 1, stdout);
    if (red) fwrite(red, count, 1, stdout);
}
int main(int argc, char **argv) {
#ifdef RAM_TABLES
    assert(argc == 2);
    FILE *pool = fopen(argv[1], "rb"); assert(pool);
    assert(fread(inktag_nrf_gui_tables, sizeof(inktag_nrf_gui_tables), 1, pool) == 1);
    fclose(pool);
#else
    (void)argc; (void)argv;
#endif
    const uint16_t geometry[][2] = {{128,250},{212,104},{250,128},{296,128},{400,300},{800,480},{122,250}};
    gui_countdown_t countdown = {.name="春节",.target_day=21000};
    gui_options_t options = {.render_buffer=buffer,.render_buffer_size=sizeof(buffer),.prefer_largest_page=true,.prefer_full_frame=true};
    for (unsigned g=0; g<7; g++) for (unsigned mode=1; mode<=4; mode++)
    for (unsigned rotation=0; rotation<4; rotation++) for (unsigned week=0; week<2; week++) for(unsigned scale=0;scale<6;scale++) {
        gui_data_t data = {.width=geometry[g][0],.height=geometry[g][1],.color=2,.mode=mode,
            .force_rotation=true,.rotation=rotation,.week_start=week,.timestamp=1791273600u,
            .native_ui_scale=scale,.temperature=-10,.voltage=2999,.countdown=&countdown,
            .board_name="AESL22 nRF",.board_name_short="NRF",.screen_info="213BWR",.mirror_x=(rotation&1)};
        strcpy(data.ssid,"IKT_NRF_ABCD");
        callbacks=0; assert(DrawGUIEx(&data,capture,NULL,&options)); assert(callbacks);
    }
    assert(GUI_CountdownNameSupported("春节", 6));
    /* A failed asset session must stop before the affected strip reaches
     * the panel. Successful earlier strips cannot turn completion into OK. */
    gui_data_t data = {.width=800,.height=480,.color=2,.mode=MODE_CALENDAR,
        .timestamp=1791273600u,.temperature=25,.voltage=3000};
    discard_pixels=true;
    for (fail_check=1;fail_check<=3;fail_check++) {
        checks=callbacks=0;
        assert(!DrawGUIEx(&data,capture,NULL,&options));
        assert(callbacks == (fail_check == 3 ? 1u : 0u));
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    out = Path(temp)
    projection = out / 'projection'
    module.generate(ROOT / 'compat/common/gui', projection)
    driver = out / 'driver.c'
    driver.write_text(DRIVER, encoding='utf-8')
    common = ROOT / 'compat/common/gui'
    dependencies = [common / x for x in ('Adafruit_GFX.c', 'u8g2_font.c', 'inktag_gui_fonts.c', 'inktag_gui_device_qr_asset.c')]
    cached_font = out / 'cached-font.c'
    runpy.run_path(str(ROOT/'build-support/gui/cache-font-glyphs.py'))['generate'](common/'u8g2_font.c',cached_font,16)
    outputs = []
    for label, selected in [('original', common), ('projected', projection), ('cached', projection)]:
        binary = out / label
        flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
        args = ['cc', *flags, '-std=gnu99', '-DINKTAG_GUI_RAW_VIEWPORT', '-DINKTAG_GUI_STATIC_BUFFER_ONLY',
                '-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1', '-DNRF52811_XXAA',
                '-I'+str(common), '-I'+str(ROOT/'compat/common/protocol'), '-I'+str(ROOT/'compat/common/epd'), '-I'+str(ROOT/'compat/common/power'),
                '-I'+str(selected)]
        if label != 'original': args += ['-DRAM_TABLES']
        sources = [cached_font if label=='cached' and x.name=='u8g2_font.c' else x for x in dependencies]
        subprocess.run([*args, str(driver), str(selected/'inktag_gui.c'), str(selected/'inktag_calendar.c'),
                        *map(str, sources), '-o', str(binary)], check=True)
        pixels = subprocess.check_output([str(binary), str(projection/'tables.bin')])
        outputs.append(pixels)
    assert outputs[0] == outputs[1] == outputs[2]
    print(f'PASS RAM GUI tables and glyph cache: 1344 native frames, all pixels identical ({len(outputs[0])} bytes); countdown glyphs preserved')
