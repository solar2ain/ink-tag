"""Execute the calendar ABI, including supported dates and uint32 time limits.

The fingerprint covers the independently regenerated UTC+08 solar-term resource.
All1224 dates are checked separately against its reproducible astronomical input
in test_solar_terms.py. No former implementation is needed to execute this test.
"""
from pathlib import Path

import os, subprocess, tempfile

def _compile_run(tmp_path, name, code, includes=(), sources=(), sanitizers=False, defines=()):
    source = tmp_path / (name + '.c'); binary = tmp_path / name
    source.write_text(code, encoding='utf-8')
    for mode, flags in [('normal',['-O2']), ('san',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'])]:
        subprocess.run([os.environ.get('CC','cc'),'-std=c99',*flags,*defines,
                        *['-I'+str(ROOT/i) for i in includes],str(source),
                        *[str(ROOT/i) for i in sources],'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


ROOT = Path(__file__).resolve().parents[1]


def test_calendar_compatibility_fingerprint(tmp_path):
    _compile_run(
        tmp_path, "calendar_compatibility",
        (ROOT / "tests/fixtures/lunar_calendar_probe.c").read_text(encoding="utf-8"),
        includes=("compat/common/gui",), sources=("compat/common/gui/inktag_calendar.c",),
        sanitizers=True, defines=("-DINKTAG_LUNAR_EXPECT_COMPATIBILITY=1",),
    )


def test_calendar_external_assets_same_abi(tmp_path):
    # The 220/220-byte lunar records and regenerated160-byte solar-term data
    # exercise virtual flash reads rather than the inline arrays.
    payload = bytes.fromhex((ROOT / "tests/fixtures/lunar_calendar_external.txt").read_text())
    assert len(payload) == 600
    mock = '''
#include <assert.h>
#include "inktag_gui_fonts.h"
static const uint8_t image[] = {''' + ",".join(map(str, payload)) + '''};
uint8_t inktag_gui_asset_read_byte(const uint8_t* address) {
    uintptr_t offset = (uintptr_t)address - INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE - INKTAG_GUI_LUNAR_SOLAR_OFFSET;
    assert(offset < sizeof(image));
    return image[offset];
}
'''
    _compile_run(
        tmp_path, "calendar_external", mock +
        (ROOT / "tests/fixtures/lunar_calendar_probe.c").read_text(encoding="utf-8"),
        includes=("compat/common/gui",), sources=("compat/common/gui/inktag_calendar.c",),
        sanitizers=True, defines=("-DINKTAG_LUNAR_EXPECT_COMPATIBILITY=1",
                                  "-DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1"),
    )


def test_failed_external_calendar_read_does_not_index_past_window(tmp_path):
    _compile_run(tmp_path, "calendar_read_failure", r'''
#include <assert.h>
#include "inktag_calendar.h"
#include "inktag_gui_fonts.h"
static unsigned reads;
uint8_t inktag_gui_asset_read_byte(const uint8_t* address) {
    uintptr_t offset = (uintptr_t)address - INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE - INKTAG_GUI_LUNAR_SOLAR_OFFSET;
    assert(offset < 4u);
    ++reads;
    return 0;
}
int main(void) {
    struct Lunar_Date date = {1, 1, 1, 1};
    LUNAR_SolarToLunar(&date, 2026, 11, 4);
    assert(!date.IsLeap && !date.Year && !date.Month && !date.Date);
    assert(reads == 4);
    return 0;
}
''', includes=("compat/common/gui",), sources=("compat/common/gui/inktag_calendar.c",),
        sanitizers=True, defines=("-DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1",))

if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as directory:
        for test in (test_calendar_compatibility_fingerprint, test_calendar_external_assets_same_abi,
                     test_failed_external_calendar_read_does_not_index_past_window):
            test(Path(directory))
