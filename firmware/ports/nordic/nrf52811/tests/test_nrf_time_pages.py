"""Exercise current factory fonts and actual native date/time rendering."""
from datetime import datetime, timezone
import os
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DRIVER = r'''
#include "inktag_gui.h"
#include "inktag_epd_screen.h"
#include "inktag_board.h"
#include "inktag_nrf_gui_tables.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint8_t inktag_nrf_gui_tables[INKTAG_NRF_GUI_TABLES_SIZE];
static uint8_t buffer[8000],image[16000],previous[16000];
static unsigned callbacks;
bool inktag_gui_assets_healthy(void){return true;}
static void capture(void *unused,uint8_t *black,uint8_t *red,uint16_t x,uint16_t y,uint16_t w,uint16_t h){
 (void)unused;assert(x==0&&w==128&&y+h<=250);callbacks++;
 memcpy(image+y*16,black,h*16);if(red)memcpy(image+8000+y*16,red,h*16);
}
int main(int argc,char **argv){
 assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);
 assert(fread(inktag_nrf_gui_tables,sizeof(inktag_nrf_gui_tables),1,f)==1);fclose(f);
 epd_screen_config_t factory=INKTAG_BOARD_DEFAULT_SCREEN_CONFIG;
 gui_data_t data={.width=factory.width,.height=factory.height,.visible_width=factory.visible_width,
  .visible_height=factory.visible_height,.visible_x=factory.visible_x,.visible_y=factory.visible_y,
  .force_rotation=true,.rotation=factory.transform&3,.color=factory.color_id,.native_ui_scale=factory.native_ui_scale,
  .week_start=1,.temperature=25,.voltage=3000,.bluetooth_name="IKT_NRF_ABCD"};
 gui_options_t options={.render_buffer=buffer,.render_buffer_size=sizeof(buffer),.prefer_full_frame=true};
 for(unsigned t=0;t<sizeof(times)/sizeof(*times);t++)for(unsigned mode=MODE_CALENDAR;mode<=MODE_CLOCK;mode++){
  data.mode=mode;data.timestamp=times[t];callbacks=0;memset(image,0,sizeof(image));
  assert(DrawGUIEx(&data,capture,NULL,&options)&&callbacks);fwrite(image,1,sizeof(image),stdout);memcpy(previous,image,sizeof(image));
  data.timestamp+=60;callbacks=0;memset(image,0,sizeof(image));
  assert(DrawGUIEx(&data,capture,NULL,&options)&&callbacks);
  assert(memcmp(previous,image,sizeof(image)));fwrite(image,1,sizeof(image),stdout);
 }
 for(unsigned week=0;week<7;week++){
  data.week_start=week;data.mode=MODE_CALENDAR;data.timestamp=1791273600u;
  memset(image,0,sizeof(image));assert(DrawGUIEx(&data,capture,NULL,&options));assert(data.week_start==week);
  if(week)assert(memcmp(previous,image,sizeof(image)));memcpy(previous,image,sizeof(image));
 }
 data.week_start=1;
 for(unsigned mode=MODE_CALENDAR;mode<=MODE_CLOCK;mode++){
  uint32_t step=mode==MODE_CLOCK?60u:86400u;data.mode=mode;data.timestamp=UINT32_MAX-step;
  memset(image,0,sizeof(image));assert(DrawGUIEx(&data,capture,NULL,&options));memcpy(previous,image,sizeof(image));
  data.timestamp=UINT32_MAX;memset(image,0,sizeof(image));assert(DrawGUIEx(&data,capture,NULL,&options));assert(memcmp(previous,image,sizeof(image)));
 }
 for(unsigned mode=MODE_CALENDAR;mode<=MODE_CLOCK;mode++){
  data.mode=mode;data.timestamp=1;memset(image,0,sizeof(image));assert(DrawGUIEx(&data,capture,NULL,&options));
  fwrite(image,1,sizeof(image),stdout);memcpy(previous,image,sizeof(image));
  data.timestamp=0;memset(image,0,sizeof(image));assert(DrawGUIEx(&data,capture,NULL,&options));
  assert(!memcmp(previous,image,sizeof(image))); /* epoch0 is not an unset sentinel */
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    common = ROOT/'compat/common/gui'
    projection = out/'projection'
    runpy.run_path(str(ROOT/'build-support/gui/ram-gui-tables.py'))['generate'](common, projection)
    font = out/'font.c'
    runpy.run_path(str(ROOT/'build-support/gui/cache-font-glyphs.py'))['generate'](common/'u8g2_font.c', font, 16)
    dates = [int(datetime(year, 1, 1, 23, 59, 30, tzinfo=timezone.utc).timestamp()) for year in (1970, 1999, 2000, 2023, 2024, 2099, 2100, 2106)]
    declaration = 'static const unsigned times[]={'+','.join(str(t)+'u' for t in dates)+'};\n'
    (out/'test.c').write_text(DRIVER.replace('int main(', declaration+'int main('), encoding='utf-8', newline='\n')
    flags = ['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie']
    includes = [common, projection, ROOT/'compat/common/protocol',ROOT/'compat/common/epd',ROOT/'compat/common/power',ROOT.parents[2]/'boards',ROOT/'compat/nordic/config']
    subprocess.run(['cc',*flags,'-std=gnu99','-DNRF52811_XXAA','-DINKTAG_GUI_RAW_VIEWPORT',
        '-DINKTAG_GUI_STATIC_BUFFER_ONLY','-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1',
        *['-I'+str(p) for p in includes],str(out/'test.c'),str(projection/'inktag_gui.c'),
        str(projection/'inktag_calendar.c'),str(font),*[str(common/p) for p in ('Adafruit_GFX.c','inktag_gui_fonts.c','inktag_gui_device_qr_asset.c')],
        '-o',str(out/'test')], check=True)
    pixels = subprocess.check_output([str(out/'test'),str(projection/'tables.bin')])
    assert len(pixels) == 34 * 16000
    frames = [hashlib.sha256(pixels[i:i+16000]).hexdigest() for i in range(0,len(pixels),16000)]
    oracle = json.loads((ROOT/'tests/fixtures/nrf_time_pages_oracle.json').read_text(encoding='utf-8'))
    assert frames == oracle['frames'], 'Existing nonzero date/time pixels changed'
    print('PASS34 frozen factory frames and epoch0; real calendar/clock cold+next-minute pixels across1970..2106')
