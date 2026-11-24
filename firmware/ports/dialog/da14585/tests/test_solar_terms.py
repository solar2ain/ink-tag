"""Check every independently generated solar-term date through the C API."""
from pathlib import Path
import csv
import hashlib
import json
import os
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
records = [{key: int(value) for key, value in row.items()} for row in
           csv.DictReader((ROOT / 'build-support/calendar/solar-terms-2000-2050.csv').open())]
generator = runpy.run_path(str(ROOT / 'build-support/calendar/generate-solar-terms.py'))
image = generator['encode'](records)
assert len(image) == 160
provenance = json.loads((ROOT / 'build-support/calendar/solar-terms-source.json').read_text())
assert hashlib.sha256(image).hexdigest() == provenance['resourceSha256']
for name, digest in provenance['filesSha256'].items():
    assert hashlib.sha256((ROOT / name).read_bytes()).hexdigest() == digest
for invalid in (records[:-1], [dict(records[0], day=31), *records[1:]]):
    try:
        generator['encode'](invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid resource accepted')

fixture = bytes.fromhex((ROOT / 'tests/fixtures/lunar_calendar_external.txt').read_text())
assert len(fixture) == 600 and fixture[440:] == image
mock = '''
#include <assert.h>
#include "inktag_gui_fonts.h"
static const uint8_t image[] = {''' + ','.join(map(str, fixture)) + '''};
uint8_t inktag_gui_asset_read_byte(const uint8_t* address) {
    uintptr_t offset = (uintptr_t)address - INKTAG_GUI_FONT_ASSET_VIRTUAL_BASE - INKTAG_GUI_LUNAR_SOLAR_OFFSET;
    assert(offset < sizeof(image));
    return image[offset];
}
'''
probe = r'''
#include "inktag_calendar.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for (unsigned year = 2000; year <= 2050; ++year)
        for (unsigned term = 0; term < 24; ++term) {
            uint8_t date = 0, distance = 0xff;
            unsigned month = term / 2 + 1;
            assert(GetJieQi(year, month, term % 2 ? 15 : 1, &date));
            assert(GetJieQiStr(year, month, date, &distance) == term);
            assert(distance == 0);
            printf("%u,%u,%u,%u\n", year, term, month, date);
        }
    uint8_t unchanged = 173;
    assert(!GetJieQi(1999, 1, 1, &unchanged) && unchanged == 173);
    assert(!GetJieQi(2051, 1, 1, &unchanged) && unchanged == 173);
    return 0;
}
'''
expected = ['{year},{term},{month},{day}'.format(**row) for row in records]
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for external in (False, True):
        source = out / 'probe.c'
        source.write_text((mock if external else '') + probe)
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']):
            binary = out / 'probe'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', *flags,
                            '-Icompat/common/gui',
                            *(['-DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1'] if external else []),
                            str(source), 'compat/common/gui/inktag_calendar.c',
                            '-o', str(binary)], cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            assert result.stdout.splitlines() == expected
print('PASS all1224 solar-term dates, inline/external parity, range and encoding guards; normal+ASan/UBSan')
