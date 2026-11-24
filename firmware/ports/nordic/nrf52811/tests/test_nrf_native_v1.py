"""Compile the actual native configuration implementation against strict NOR."""
from pathlib import Path
import os,subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1]
INCLUDES=['tests/mocks/nrf_external','compat/nordic/src','compat/common/epd','compat/common/storage','compat/common/input','compat/common/protocol','compat/common/ble','compat/common/status','compat/common/power','compat/common/lifecycle']
SOURCES=['compat/common/epd/inktag_epd_screen.c','compat/common/protocol/inktag_display_settings.c','compat/common/protocol/inktag_protocol_core.c','compat/common/protocol/inktag_text.c','compat/common/input/inktag_input.c','compat/common/ble/inktag_ble_identity.c','compat/common/protocol/inktag_pin_config.c','compat/nordic/src/inktag_nrf_pins.c','compat/nordic/src/inktag_nrf_pin_epoch.c']
def prepare(out):
    (out/'inktag_board.h').write_text('#include <stdint.h>\n#include "'+str(ROOT.parents[2]/'boards/nrf52811_aesl.h')+'"\n#undef INKTAG_BOARD_DFU_SETTINGS_START\nextern uint32_t test_native_dfu[23];\n#define INKTAG_BOARD_DFU_SETTINGS_START ((uintptr_t)test_native_dfu)\n')
    (out/'nrf.h').write_text('#ifndef TEST_NRF_H\n#define TEST_NRF_H\n#include <stdint.h>\ntypedef struct {uint32_t OUTSET,OUTCLR,DIRSET,DIRCLR,PIN_CNF[32];} test_gpio_t;\nextern test_gpio_t test_gpio;\n#define NRF_P0 (&test_gpio)\n#endif\n')
    (out/'app_util_platform.h').write_text('#include <assert.h>\nextern unsigned test_critical;\n#define CRITICAL_REGION_ENTER() (++test_critical)\n#define CRITICAL_REGION_EXIT() do {assert(test_critical);--test_critical;} while(0)\n')
    (out/'sdk_config.h').write_text('#define FDS_ENABLED 0\n')
    config=(ROOT/'compat/common/epd/inktag_epd_config.c').read_text()
    candidate=config.split('#if defined(INKTAG_NRF_COMPACT_STORE)',1)[0]+(ROOT/'compat/nordic/src/inktag_nrf_native_v1.inc').read_text()
    (out/'candidate-config.c').write_text(candidate,encoding='utf-8')
def compile_run(out,source,args=(),extra_sources=()):
    prepare(out)
    source=re.sub(r'^#define INKTAG_BOARD_(?:DFU_SETTINGS_START|DEFAULT_EPD_CONFIG)[^\n]*\n','',source,flags=re.M)
    mains=list(re.finditer(r'int\s+main\s*\(([^)]*)\)',source));assert mains
    main=mains[-1]
    arguments='argc,argv' if 'argc' in main.group(1) else ''
    opening=source.index('{',main.end());end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    # A C main falling off its end returns zero; preserve that rule after
    # renaming it into a helper for the native Settings initialization wrapper.
    source=source[:end-1]+'\nreturn 0;\n'+source[end-1:]
    source=source[:main.start()]+source[main.start():].replace('main','native_fixture_main',1)
    source='#include "inktag_board.h"\n#include "nrf.h"\nuint32_t test_native_dfu[23];test_gpio_t test_gpio;unsigned test_critical;\n'+source
    source+='\nint main(int argc,char **argv){test_native_dfu[1]=2;test_native_dfu[2]=0x100c8u;test_native_dfu[6]=8;test_native_dfu[8]=1;test_native_dfu[0]=~inktag_crc32_update(UINT32_MAX,(const uint8_t*)test_native_dfu+4,88);return native_fixture_main('+arguments+');}\n'
    p=out/'driver.c';p.write_text(source,encoding='utf-8')
    flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
    subprocess.run(['cc','-std=gnu99',*flags,'-DNRF52811_XXAA','-I'+str(out),*['-I'+str(ROOT/i) for i in INCLUDES],str(p),*[str(ROOT/s) for s in [*SOURCES,*extra_sources]],'-o',str(out/'test')],check=True)
    subprocess.run([str(out/'test'),*map(str,args)],check=True)
if __name__=='__main__':
    with tempfile.TemporaryDirectory() as d:compile_run(Path(d),(ROOT/'tests/fixtures/nrf_native_v1.inc').read_text())
