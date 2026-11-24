"""Permanent InkTag build policy rejects attempts to restore positive engines."""
import os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT.parents[3]
with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/'policy.c'; source.write_text('#include "inktag_base_policy.h"\nint main(void){return !INKTAG_PROTOCOL_BASE_ONLY_TARGET;}\n')
    for define in (None,'INKTAG_NRF_ACTIVATION_ENABLED=1','INKTAG_NRF_FIND_ENABLED=1',
                   'INKTAG_NRF_PASSWORD_ENABLED=1','INKTAG_PROTOCOL_BASE_ONLY_TARGET=0','INKTAG_NRF_DEVICE_NAME_WRITABLE=1'):
        binary=Path(directory)/'policy'
        result=subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Icompat/common/protocol',
                  *(['-D'+define] if define else []),str(source),'-o',str(binary)],cwd=ROOT,capture_output=True)
        assert (result.returncode==0)==(define is None),(define,result.stderr)
        if define is None:subprocess.run([str(binary)],check=True)
        else:assert b'This_nRF52811_source_port' in result.stderr
print('PASS permanent InkTag policy and five rejected positive configurations')

# Export closure physically excludes unused engines, independently of LTO.
import json,re
policy=json.loads((ROOT/'build-support/source/source-allowlist.json').read_text())
audit_name='protocol/private-projection.json'
assert audit_name not in policy['files']
if (BASE/'source-export.json').is_file():
    assert not (ROOT/audit_name).exists()
# An accidental allowlist addition must fail before internal audit data is read.
import runpy
select=runpy.run_path(str(ROOT/'build-support/source/export-source.py'))['selected']
assert select(ROOT) == policy['files']
with tempfile.TemporaryDirectory() as directory:
    isolated=Path(directory);(isolated/'build-support/source').mkdir(parents=True)
    (isolated/'build-support/source/source-allowlist.json').write_text(json.dumps({'files':[audit_name]}))
    try:
        select(isolated)
    except ValueError as error:
        assert 'Unsafe export path' in str(error)
    else:
        raise AssertionError('Internal audit file was allowed into public source')
for stem in ('nfc_binding','nfc_link','nfc_command','ndef','type2_uri_store','type4_ndef','gt_mirror'):
    for suffix in ('c','h'):
        name='compat/common/input/inktag_'+stem+'.'+suffix
        assert not (ROOT/name).exists(),name
        assert name not in policy['files'],name
source=(ROOT/'compat/common/epd/inktag_epd_ssd.c').read_text()
for symbol in ('SSD1675_154_FULL_LUT','SSD1677_426_GRAY4_LUT'):
    assert symbol not in source
print('PASS physical InkTag closure and neutral public provenance')

# Public InkTag sources contain no uploadable-font codec or excluded screen engines.
for name in ('inktag_gui_asset_package.c','inktag_gui_asset_package.h'):
    assert not (ROOT/'compat/common/gui'/name).exists()
for name in ('inktag_epd_e6.inc',):
    assert not (ROOT/'compat/common/epd'/name).exists()
with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/'screens.c'
    source.write_text('#include "inktag_epd_screen.h"\nint main(void){return 0;}\n')
    for name in ('UC8151','UC8159','SSD1675','SSD1677_GRAY4','JD79665CA','OSPREY_E6'):
        result=subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Icompat/common/epd',
                '-DINKTAG_EPD_ENABLE_'+name+'=1',str(source),'-o',str(Path(directory)/'screens')],cwd=ROOT,capture_output=True)
        assert result.returncode and b'This_nRF52811_source_port_excludes_' in result.stderr,name
print('PASS absent unsupported implementations and six rejected screen build configurations')

# Custom input engines are not part of this independent InkTag source.
for suffix in ('c','h'):
    assert not (ROOT/('compat/common/input/inktag_input_simulation.'+suffix)).exists()
source=(ROOT/'compat/nordic/src/inktag_nrf_protocol_v2.inc').read_text(encoding='utf-8')
for symbol in ('epd_v2_set_input','epd_v2_send_input','epd_v2_button_configuration_access'):
    assert symbol not in source
print('PASS absent custom input engines; wire IDs and stock runtime remain')

# Link the real input source without section GC. A retired injection function
# must not merely disappear from the shipping image through optimization.
with tempfile.TemporaryDirectory() as directory:
    probe=Path(directory)/'simulation.c'
    probe.write_text('#include "inktag_input.h"\n'
        'extern bool inktag_button_simulate(inktag_button_state_t*,inktag_button_gesture_t);\n'
        'int main(void){inktag_button_state_t s={0};return inktag_button_simulate(&s,INKTAG_BUTTON_GESTURE_DOUBLE);}\n')
    result=subprocess.run([os.environ.get('CC','cc'),'-std=c99','-O0',
        '-Icompat/common/input',str(probe),'compat/common/input/inktag_input.c',
        '-o',str(Path(directory)/'simulation')],cwd=ROOT,capture_output=True)
    assert result.returncode and b'inktag_button_simulate' in result.stderr
for name in ('inktag_gui_assets.h','inktag_gui_fonts.h','inktag_gui_fonts.c','u8g2_font.h'):
    text=(ROOT/'compat/common/gui'/name).read_text(encoding='utf8')
    assert 'INKTAG_GUI_ATOMIC_FONT_ASSETS' not in text
    assert 'INKTAG_GUI_EMBEDDED_FONT_FALLBACK' not in text
print('PASS unlinked input injection rejected; only fixed-font rendering adapters remain')

# The fixed InkTag title needs only A/G/I/K/N/T. Historical resource ABI padding
# is not an additional drawable alphabet or a Pro title selector.
native_page=(ROOT/'compat/common/gui/inktag_gui_default.inc').read_text(encoding='utf8')
assert 'v1_word[6][7]' in native_page
for field in ('activated','brand_hidden'):
    assert field not in native_page
assert not re.search(r'\b(?:bool\s+pro|"INKTAG PRO"|"PRO")', native_page)
print('PASS fixed six-glyph INKTAG title; no Pro renderer selection')

# Source and build names describe the default InkTag implementation. Reserved
# protocol values survive their renamed host identifiers.
header=(ROOT/'compat/common/epd/inktag_protocol_v2.h').read_text()
assert 'INKTAG_ACTIVATION_STATE_DEFAULT = 0x01' in header
header=(ROOT/'compat/common/security/inktag_brand_overlay.h').read_text()
assert 'INKTAG_BRAND_EDITION_DEFAULT = 0' in header
layout=(ROOT/'compat/common/gui/inktag_gui_default.inc').read_text()
assert 'DrawLayoutV1Page(' in layout and '"BASIC",' in layout
assert 'name = "InkTag"' in layout
assert '"FREE"' not in layout
print('PASS renamed default source symbols, unchanged wire numbers, BASIC tier and InkTag product name')
