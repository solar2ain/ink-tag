#!/usr/bin/env python3
"""Generate a bounded RAM text pool and unchanged GUI algorithms from common."""
import ast
import json
from pathlib import Path
import re
import struct
import sys

TOKEN = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"')
TABLE = re.compile(r'const char (Lunar_\w+|JieQiStr)\[(\d+)\]\[(\d+)\]\s*=\s*\{([\s\S]*?)\};')

def generate(source, target):
    target.mkdir(parents=True, exist_ok=True)
    # Installed resource ABI: never repack offsets when a dead renderer branch
    # is removed. Reserved bytes grant no runtime capability.
    abi = json.loads(Path(__file__).with_name('gui-pool-v1.json').read_text())
    pool = bytearray.fromhex(abi['poolHex'])
    pointers = {bytes.fromhex(e['hex']): e['offset'] for e in abi['entries']}
    def add(data, alignment=1):
        if data not in pointers:
            raise ValueError('GUI pool ABI requires an explicit new resource layout')
        offset = pointers[data]
        assert offset % alignment == 0
        assert pool[offset:offset+len(data)] == data
        return offset
    header = ['#ifndef INKTAG_NRF_GUI_TABLES_H', '#define INKTAG_NRF_GUI_TABLES_H',
              '#include <stdint.h>', '#include <stdbool.h>', '#include "inktag_calendar.h"',
              '#if defined(INKTAG_GUI_EXTERNAL_FONT_ASSETS)',
              'bool inktag_nrf_gui_assets_ok(void);', '#else',
              'static inline bool inktag_nrf_gui_assets_ok(void) { return true; }', '#endif',
              'extern uint8_t inktag_nrf_gui_tables[] __attribute__((aligned(4)));']
    lunar = (source / 'inktag_calendar.c').read_text(encoding='utf-8')
    tables = []
    def table(match):
        name, rows, width, initializer = match.groups()
        rows, width = int(rows), int(width)
        values = [ast.literal_eval(m.group()).encode('utf-8') for m in TOKEN.finditer(initializer)
                  if m.group().startswith('"')]
        assert len(values) == rows and all(len(v) < width for v in values), name
        data = b''.join(v + bytes(width - len(v)) for v in values)
        offset = add(data)
        header.append(f'#define {name} ((const char (*)[{width}])(inktag_nrf_gui_tables + {offset}u))')
        tables.append({'name': name, 'offset': offset, 'rows': rows, 'width': width})
        return ''
    lunar = TABLE.sub(table, lunar)
    assert len(tables) == 8
    lunar = lunar.replace('#include "inktag_gui_fonts.h"', '#include "inktag_gui_fonts.h"\n#include "inktag_nrf_gui_tables.h"')
    original = (source / 'inktag_gui_default.inc').read_text(encoding='utf-8')
    original=original.replace('#include "../power/inktag_power.h"','#include "inktag_power.h"')
    scalars = []
    for name, bits, rows, width in [('v1_word', 8, 6, 7), ('holidays', 16, 39, 1),
                                   ('digits', 8, 10, 1), ('segments', 8, 7, 5),
                                   ('compact_order', 8, 4, 1)]:
        pattern = re.compile(r'static const uint' + str(bits) + r'_t ' + name + r'\[[^;=]*\]\s*=\s*\{([\s\S]*?)\};')
        match = pattern.search(original)
        assert match, name
        body = re.sub(r'/\*[\s\S]*?\*/', '', match[1])
        values = [int(v.rstrip('u'), 16 if v.startswith('0x') else 10)
                  for v in re.findall(r'0x[0-9A-Fa-f]+|\b\d+u?\b', body)]
        assert len(values) == rows * width, (name, values)
        data = struct.pack('<' + ('B' if bits == 8 else 'H') * len(values), *values)
        offset = add(data, bits // 8)
        cast = f'const uint{bits}_t (*)[{width}]' if width > 1 else f'const uint{bits}_t *'
        if name == 'v1_word':
            replacement = f'#define {name} (({cast})(inktag_nrf_gui_tables + {offset}u))'
        else:
            declaration = f'const uint{bits}_t (* const {name})[{width}]' if width > 1 else f'const uint{bits}_t * const {name}'
            replacement = f'{declaration} = ({cast})(inktag_nrf_gui_tables + {offset}u);'
        original = original[:match.start()] + replacement + original[match.end():]
        original = original.replace(f'sizeof({name}) / sizeof({name}[0])', f'{rows}u')
        scalars.append({'name': name, 'offset': offset, 'bytes': len(data)})
    festival_rows = []
    def festival(match):
        name, initializer = match.groups()
        entries = re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*("(?:\\.|[^"\\])*")\s*\}', initializer)
        assert len(entries) == (15 if name == 'solar' else 11)
        data = bytearray()
        for month, day, literal in entries:
            text = ast.literal_eval(literal).encode('utf-8') + b'\0'
            data.extend(struct.pack('<BBH', int(month), int(day), add(text)))
        offset = add(bytes(data), 2)
        festival_rows.append({'name': name, 'offset': offset, 'rows': len(entries)})
        return f'const v1_festival_t * const {name} = (const v1_festival_t *)(inktag_nrf_gui_tables + {offset}u);'
    original = re.sub(r'static const v1_festival_t (solar|lunar_days)\[\]\s*=\s*\{([\s\S]*?)\};', festival, original)
    assert len(festival_rows) == 2
    original = original.replace('const char* name;\n} v1_festival_t;', 'uint16_t name_offset;\n} v1_festival_t;')
    original = original.replace('return days[i].name;', 'return (const char *)(inktag_nrf_gui_tables + days[i].name_offset);')
    for entry in festival_rows:
        name = entry['name']
        original = original.replace(f'sizeof({name}) / sizeof({name}[0])', f"{entry['rows']}u")
    styles = re.search(r'static const v1_low_battery_style_t styles\[\]\s*=\s*\{([\s\S]*?)\};', original)
    assert styles
    rows = re.findall(r'\{([^{}]+)\}', styles[1])
    assert len(rows) == 3
    data = b''.join(struct.pack('<HHHBBBBBx', *[int(n) for n in re.findall(r'\d+', row)]) for row in rows)
    offset = add(data, 2)
    original = original[:styles.start()] + f'const v1_low_battery_style_t * const styles = (const v1_low_battery_style_t *)(inktag_nrf_gui_tables + {offset}u);' + original[styles.end():]
    scalars.append({'name': 'low_battery_styles', 'offset': offset, 'bytes': len(data)})
    # Fixed GNU Arm little-endian layout, checked against the original C
    # initializer by the native ABI and complete-frame regression fixtures.
    match = re.search(r'static const struct \{\s*uint16_t width, height;\s*v1_layout_preset_t layout;\s*\} screens\[\]\s*=\s*\{([\s\S]*?)\};', original)
    assert match
    expected_families = 'V1_LAYOUT_COMPACT_RECT = 1,\n    V1_LAYOUT_NORMAL,\n    V1_LAYOUT_LARGE,'
    assert expected_families in original
    entries = re.findall(r'\{(\d+),\s*(\d+),\s*\{(\d+)u,\s*(V1_LAYOUT_\w+),\s*(true|false),\s*(true|false),\s*(true|false)\}\}',match[1])
    assert [(int(e[0]),int(e[1])) for e in entries]==[(212,104),(250,128),(296,152)]
    # Reserve the installed ABI's former two rows. They are not advertised or
    # read as presets; keeping their bytes preserves the installed resource.
    packed=bytearray(struct.pack('<HHHBx',152,152,256,8)+
                     struct.pack('<HHHBx',200,200,337,24))
    for width,height,scale,family,lunar_flag,qr,voltage in entries:
        family_id={'V1_LAYOUT_COMPACT_RECT':1}[family]
        flags=family_id|(int(lunar_flag=='true')<<2)|(int(qr=='true')<<3)|(int(voltage=='true')<<4)
        packed.extend(struct.pack('<HHHBx',int(width),int(height),int(scale),flags))
    offset=add(bytes(packed),4)
    replacement = f"""#define INKTAG_NRF_GUI_SCREEN_PRESET_COUNT 3u
#define INKTAG_NRF_GUI_SCREEN_PRESET_SKIP 16u
    const struct {{ uint16_t width,height; v1_layout_preset_t layout; }} *const screens =
        (const void*)(inktag_nrf_gui_tables + {offset}u + INKTAG_NRF_GUI_SCREEN_PRESET_SKIP);
    _Static_assert(sizeof(*screens)==8u && sizeof(v1_layout_preset_t)==4u,"preset pool ABI");
"""
    original=original[:match.start()]+replacement+original[match.end():]
    original=original.replace('sizeof(screens) / sizeof(screens[0])','INKTAG_NRF_GUI_SCREEN_PRESET_COUNT')
    # No square family or fallback exists in the independent InkTag renderer.
    scalars.append({'name':'layout_presets','offset':offset,'bytes':len(packed)})
    offset=add(struct.pack('<6H',256,256,384,512,320,448),2)
    match=re.search(r'static uint16_t v1_scale_q8\(const gui_data_t\* data\) \{[\s\S]*?\n\}',original)
    assert match
    replacement=f"""static uint16_t v1_scale_q8(const gui_data_t* data) {{
    const uint16_t *scales=(const uint16_t*)(inktag_nrf_gui_tables + {offset}u);
    return data->native_ui_scale<6u ? scales[data->native_ui_scale] : 256u;
}}"""
    original=original[:match.start()]+replacement+original[match.end():]
    scalars.append({'name':'ui_scales','offset':offset,'bytes':12})
    # All references below execute only inside an admitted GUI asset lease.
    # Reuse the existing 1728-byte RAM pool, including the calendar's duplicate
    # holiday array; neither the asset region nor static RAM grows.
    for name in ('v1_date_units', 'keys'):
        pattern = re.compile(r'static const char\* const ' + name + r'\[\]\s*=\s*\{([^}]+)\};')
        match = pattern.search(original)
        assert match, name
        values = [ast.literal_eval(m.group()).encode('utf-8') + b'\0'
                  for m in TOKEN.finditer(match[1]) if m.group().startswith('"')]
        positions = [add(value) for value in values]
        offset = add(struct.pack('<' + 'H'*len(positions), *positions), 2)
        replacement = f'const uint16_t * const {name} = (const uint16_t *)(inktag_nrf_gui_tables + {offset}u);'
        original = original[:match.start()] + replacement + original[match.end():]
        if name == 'v1_date_units':
            original = original.replace('v1_date_units[i]', '((const char *)(inktag_nrf_gui_tables + v1_date_units[i]))')
        else:
            original = original.replace('const char* const keys[],', 'const uint16_t keys[],')
            original = original.replace('keys[field]', '((const char *)(inktag_nrf_gui_tables + keys[field]))')
        scalars.append({'name': name + '_offsets', 'offset': offset, 'bytes': len(positions)*2})
    calendar = (source/'inktag_gui_calendar.inc').read_text(encoding='utf-8')
    match = re.search(r'static const uint16_t holidays\[\]\s*=\s*\{([^}]+)\};', calendar)
    assert match
    values = [int(v,16) for v in re.findall(r'0x[0-9A-Fa-f]+',match[1])]
    assert len(values)==39
    offset = add(struct.pack('<39H',*values),2)
    calendar = calendar[:match.start()] + f'static const uint16_t * const holidays = (const uint16_t *)(inktag_nrf_gui_tables + {offset}u);' + calendar[match.end():]
    calendar = calendar.replace('sizeof(holidays) / sizeof(holidays[0])', '39u')
    calendar = '#include "inktag_nrf_gui_tables.h"\n' + calendar
    # Only the colour-name function reads these strings, during page layout.
    gui_source = (source / 'inktag_gui.c').read_text(encoding='utf-8')
    a = gui_source.index('const char* GUI_EpdColorName(')
    b = gui_source.index('\n}',a)+2
    color_function = gui_source[a:b]
    def color_literal(match):
        text=match.group()
        if not text.startswith('"'): return text
        position=add(ast.literal_eval(text).encode('utf-8')+b'\0')
        return f'((const char *)(inktag_nrf_gui_tables + {position}u))'
    gui_source = gui_source[:a] + TOKEN.sub(color_literal,color_function) + gui_source[b:]
    gui_source = gui_source.replace('#include "inktag_gui.h"', '#include "inktag_gui.h"\n#include "inktag_nrf_gui_tables.h"')
    # Omitting a dedicated warning page must not produce a successful blank
    # refresh. Reject it before allocation, asset access or panel callbacks.
    entry='bool DrawGUIEx(gui_data_t* data, buffer_callback callback, void* callback_data, const gui_options_t* options) {'
    assert gui_source.count(entry)==1
    gui_source=gui_source.replace(entry,entry+'''
#if defined(INKTAG_GUI_DISABLE_LOW_BATTERY_PAGE)
    if(data->mode==MODE_LOW_BATTERY)return false;
#endif''')
    start=gui_source.index('            case MODE_LOW_BATTERY:')
    end=gui_source.index('            default:',start)
    gui_source=gui_source[:start]+'#if !defined(INKTAG_GUI_DISABLE_LOW_BATTERY_PAGE)\n'+gui_source[start:end]+'#endif\n'+gui_source[end:]
    strings = []
    def literal(match):
        value = match.group()
        line_start = original.rfind('\n', 0, match.start()) + 1
        prefix = original[line_start:match.start()]
        if not value.startswith('"') or prefix.lstrip().startswith('#') or '_Static_assert(' in prefix:
            return value
        data = ast.literal_eval(value).encode('utf-8') + b'\0'
        offset = add(data)
        strings.append({'literal': value, 'offset': offset})
        return f'((const char *)(inktag_nrf_gui_tables + {offset}u))'
    gui = TOKEN.sub(literal, original)
    gui = gui.replace('#include "inktag_gui_fonts.h"', '#include "inktag_gui_fonts.h"\n#include "inktag_nrf_gui_tables.h"')
    used = len(pool)
    assert used <= 1728, used
    pool.extend(bytes(1728 - used))
    assert 10500 + len(pool) + 16 <= 12288, len(pool)
    header += [f'#define INKTAG_NRF_GUI_TABLES_SIZE {len(pool)}u', '#endif', '']
    (target / 'inktag_nrf_gui_tables.h').write_text('\n'.join(header), encoding='utf-8')
    # The accepted common renderer now owns failure-aware page admission and
    # completion. Preserve that source instead of injecting a second guard.
    assert gui_source.count('    return assets_ok;') == 1
    (target / 'inktag_gui.c').write_text(gui_source, encoding='utf-8')
    (target / 'inktag_gui_default.inc').write_text(gui, encoding='utf-8')
    (target / 'inktag_gui_calendar.inc').write_text(calendar, encoding='utf-8')
    (target / 'inktag_calendar.c').write_text(lunar, encoding='utf-8')
    (target / 'tables.bin').write_bytes(pool)
    (target / 'tables.json').write_text(json.dumps({'bytes': len(pool), 'used': used, 'tables': tables, 'strings': strings, 'scalars': scalars, 'festivals': festival_rows}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'GUI text pool: {len(pool)} bytes; {len(strings)} literal references; {len(tables)} arrays')

if __name__ == '__main__':
    generate(*map(Path, sys.argv[1:]))
