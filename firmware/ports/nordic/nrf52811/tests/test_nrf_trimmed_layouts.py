"""Keep rectangular-template pixels including square fallbacks; omit the low page."""
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=ast.parse((ROOT/'tests/test_nrf_gui_ram_tables.py').read_text())
driver=next(ast.literal_eval(n.value) for n in source.body if isinstance(n,ast.Assign)
    and any(isinstance(t,ast.Name) and t.id=='DRIVER' for t in n.targets))
old='{{128,250},{212,104},{250,128},{296,128},{400,300},{800,480},{122,250}}'
new='{{128,250},{122,250},{296,128},{400,300},{768,552},{800,480},{960,680},{152,152},{200,200},{240,240},{299,300},{300,299}}'
assert driver.count(old)==1
driver=driver.replace(old,new).replace('mode<=4','mode<=3')
assert driver.count('g<7')==1
driver=driver.replace('g<7','g<sizeof(geometry)/sizeof(geometry[0])')
needle='    assert(GUI_CountdownNameSupported("春节", 6));'
assert driver.count(needle)==1
driver=driver.replace(needle,needle+r'''
#ifdef TRIMMED_PAGE
    gui_data_t low={.width=128,.height=250,.color=2,.mode=MODE_LOW_BATTERY,.voltage=2400};
    callbacks=checks=0;
    assert(!DrawGUIEx(&low,capture,NULL,&options));
    assert(callbacks==0 && checks==0); /* no allocation/font/asset/pixel work */
#endif
''')
with tempfile.TemporaryDirectory() as temporary:
    out=Path(temporary);common=ROOT/'compat/common/gui';projection=out/'projection'
    runpy.run_path(str(ROOT/'build-support/gui/ram-gui-tables.py'))['generate'](common,projection)
    cached=out/'font.c'
    runpy.run_path(str(ROOT/'build-support/gui/cache-font-glyphs.py'))['generate'](common/'u8g2_font.c',cached,16)
    harness=out/'driver.c';harness.write_text(driver)
    results=[]
    for name,selected in [('baseline',common),('trimmed',projection)]:
        binary=out/name
        flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
        args=['cc','-std=gnu99',*flags,'-DINKTAG_GUI_RAW_VIEWPORT','-DINKTAG_GUI_STATIC_BUFFER_ONLY',
              '-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1','-DNRF52811_XXAA',
              *['-I'+str(ROOT/p) for p in ['compat/common/gui','compat/common/protocol','compat/common/epd','compat/common/power']],'-I'+str(selected)]
        if name=='trimmed':args+=['-DRAM_TABLES','-DTRIMMED_PAGE','-DINKTAG_GUI_DISABLE_LOW_BATTERY_PAGE=1']
        dependencies=[common/'Adafruit_GFX.c',cached if name=='trimmed' else common/'u8g2_font.c',common/'inktag_gui_fonts.c',common/'inktag_gui_device_qr_asset.c']
        subprocess.run([*args,str(harness),str(selected/'inktag_gui.c'),str(selected/'inktag_calendar.c'),*map(str,dependencies),'-o',str(binary)],check=True)
        pixels=subprocess.check_output([str(binary),str(projection/'tables.bin')])
        results.append(pixels)
    assert results[0]==results[1], 'retained small/large page raster changed'
    result=dict(frames=12*3*4*2*6,pixelBytes=len(results[0]),sha256=hashlib.sha256(results[0]).hexdigest(),
        retained=['original 2.13','122 visible columns','296x128','400x300','A0/A1 768x552','800x480','960x680','152/200/240 square fallback','299x300/300x299 near-square fallback'],
        lowPageRejectedBeforeAssetsAndCallbacks=True)
    print('PASS retained layouts: '+json.dumps(result))
