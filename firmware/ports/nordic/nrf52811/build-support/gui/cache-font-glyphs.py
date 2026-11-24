#!/usr/bin/env python3
"""Add a bounded glyph-address cache to the unchanged common font lookup."""
from pathlib import Path
import re
import sys

def generate(source: Path, target: Path, entries: int = 16):
    assert entries in (8, 16, 32)
    original = source.read_text(encoding='utf-8')
    pattern = r'const uint8_t\* u8g2_font_get_glyph_data\([^;{]*\) \{[\s\S]*?\n}'
    match = re.search(pattern, original)
    assert match, 'common glyph lookup signature changed'
    uncached = match.group().replace('u8g2_font_get_glyph_data(', 'inktag_font_find_glyph(', 1)
    uncached = 'static ' + uncached
    cache = '''
/* Glyph addresses are immutable within this fixed, CRC-validated asset image.
 * Cache lookup results, including absent glyphs; font decoding is unchanged. */
static struct {
    const uint8_t *font;
    const uint8_t *glyph;
    uint16_t encoding;
} inktag_glyph_cache[ENTRY_COUNT];
static uint8_t inktag_glyph_cursor;
const uint8_t* u8g2_font_get_glyph_data(u8g2_font_t* u8g2, uint16_t encoding) {
    if (U8G2_READ_FAILED()) return NULL;
    for (uint8_t i = 0; i < ENTRY_COUNT; i++) {
        if (inktag_glyph_cache[i].font == u8g2->font &&
            inktag_glyph_cache[i].encoding == encoding)
            return inktag_glyph_cache[i].glyph;
    }
    const uint8_t *glyph = inktag_font_find_glyph(u8g2, encoding);
    if (U8G2_READ_FAILED()) return NULL; /* Never cache a failed IO as absence. */
    uint8_t index = inktag_glyph_cursor++ & (ENTRY_COUNT - 1u);
    inktag_glyph_cache[index].font = u8g2->font;
    inktag_glyph_cache[index].glyph = glyph;
    inktag_glyph_cache[index].encoding = encoding;
    return glyph;
}
'''.replace('ENTRY_COUNT', str(entries))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(original[:match.start()] + uncached + cache + original[match.end():], encoding='utf-8')

if __name__ == '__main__':
    generate(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]) if len(sys.argv)>3 else 16)
