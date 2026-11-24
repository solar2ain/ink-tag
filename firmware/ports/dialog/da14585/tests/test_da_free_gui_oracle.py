"""Full-frame DA Free oracle, independent of SDKs, Git and ignored build files."""
import hashlib
import json
from pathlib import Path
import struct
import zlib

import pytest

from da_host_helpers import ROOT, compile_run

FIXTURES = ROOT / 'tests/fixtures'
GOLDEN = json.loads((FIXTURES / 'da_free_gui_golden.json').read_text())
DEVICE_INFO_GOLDEN = json.loads((FIXTURES / 'da_basic_device_info_golden.json').read_text())


def frame_hashes(output):
    lines = iter(output.splitlines())
    cases = {}
    variants = 0
    for line in lines:
        if line == 'INVALID rejected-without-callback':
            assert next(lines, None) is None
            break
        fields = line.split()
        assert fields[0] == 'O'
        width, height, mode, rotation, minute, reserved = map(int, fields[1:])
        black, red = bytearray(), bytearray()
        rows = pages = 0
        for row in lines:
            fields = row.split()
            if fields[0] == 'E':
                assert list(map(int, fields[1:])) == [height, pages]
                assert rows == height
                break
            assert fields[0] == 'R' and int(fields[1]) == rows
            count = ((width + 7) // 8) * int(fields[2])
            b, c = next(lines), next(lines)
            assert b.startswith('B ') and c.startswith('C ')
            b, c = bytes.fromhex(b[2:]), bytes.fromhex(c[2:])
            assert len(b) == len(c) == count
            black.extend(b)
            red.extend(c)
            rows += int(fields[2])
            pages += 1
        else:
            raise AssertionError('Truncated frame')
        key = f'{width}x{height}/mode-{mode}/rotation-{rotation}/minute-{minute}'
        actual = dict(pixelsSha256=hashlib.sha256(black + red).hexdigest(),
                      blackSha256=hashlib.sha256(black).hexdigest(),
                      redSha256=hashlib.sha256(red).hexdigest(),
                      planeBytes=len(black), rows=rows, pages=pages)
        if reserved:
            assert cases[key] == actual, 'Reserved ABI fields changed Free pixels'
        else:
            assert key not in cases
            cases[key] = actual
        variants += 1
    else:
        raise AssertionError('Missing unsupported-mode result')
    assert len(cases) == 192 and variants == 384
    return cases


def external_adapter(tmp_path):
    # This is the actual frozen 12 KiB DA resource, not an optional local font.
    # Original font/resource terms remain in SOURCE_NOTICES.md and their sources.
    image = bytes.fromhex((FIXTURES / 'da_free_gui_ikfd.txt').read_text())
    assert hashlib.sha256(image).hexdigest() == GOLDEN['nativeAssetSha256']
    magic, schema, size, crc = struct.unpack_from('<IIII', image)
    assert (magic, schema, len(image)) == (0x44464B49, 1, 12288)
    assert zlib.crc32(image[16:16 + size]) & 0xffffffff == crc
    source = tmp_path / 'fixed-assets.c'
    source.write_text('''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
static const uint8_t image[] = {''' + ','.join(map(str, image)) + '''};
bool inktag_gui_assets_begin(void) { return true; }
void inktag_gui_assets_end(void) {}
bool inktag_gui_assets_healthy(void) { return true; }
uint8_t inktag_gui_asset_read_byte(const uint8_t *address) {
    uintptr_t offset = (uintptr_t)address - UINT32_C(0x60000000);
    assert(offset >= 16 && offset < sizeof image);
    return image[offset];
}
''')
    return source


@pytest.mark.parametrize('external', (False, True), ids=('inline', 'native-ikfd'))
@pytest.mark.parametrize('sanitize', (False, True), ids=('normal', 'asan-ubsan'))
def test_complete_free_pages_match_frozen_pixels(tmp_path, external, sanitize):
    sources = [FIXTURES / 'da_free_gui_oracle.c']
    sources += [ROOT / 'compat/common/gui' / name for name in (
        'Adafruit_GFX.c', 'u8g2_font.c', 'inktag_gui_fonts.c',
        'inktag_gui.c', 'inktag_calendar.c')]
    flags = ['-O2', '-DINKTAG_DIALOG', '-DINKTAG_GUI_RAW_VIEWPORT=1',
             '-DINKTAG_EPD_ENABLE_JD79665CA=0', '-Icompat/common/gui', '-Icompat/common/epd']
    if external:
        sources.append(external_adapter(tmp_path))
        flags += ['-DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1',
                  '-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1']
    output = compile_run(tmp_path, flags, sources, sanitize=sanitize, capture=True)
    assert set(DEVICE_INFO_GOLDEN['cases']) == {key for key in GOLDEN['cases'] if '/mode-3/' in key}
    expected = {**GOLDEN['cases'], **DEVICE_INFO_GOLDEN['cases']}
    assert frame_hashes(output) == expected
