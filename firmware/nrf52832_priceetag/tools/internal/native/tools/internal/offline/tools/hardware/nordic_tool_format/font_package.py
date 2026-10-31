"""Bounded IKFA/U8g2 validation for programmers; no JS runtime or CLI import.

Validation predicates mirror the frozen fontRoles.js decodeFont/validateNative/
parsePackage. The original input SHA and required characters are provenance-bound.
No font compilation, role editing, renderer or device operation is provided.
"""
from __future__ import annotations
import hashlib
import struct
import zlib

MAX_BYTES = ABSOLUTE_MAX_BYTES = 32768
SCHEMA = 1
CHINESE_REQUIRED = (8451,19968,19969,19971,19975,19977,19985,19993,20013,20057,20061,20108,20116,20133,20146,20154,20241,20799,20803,20820,20826,20843,20845,20891,20908,20998,21021,21160,21171,21313,21320,21359,21608,22235,22269,22307,22764,22788,22799,22805,22812,22823,22825,22836,22899,22919,23376,23433,23477,23493,23506,23567,24049,24051,24072,24179,24180,24198,24218,24314,24319,24681,24773,24778,24858,24863,25098,25100,25260,25945,26085,26086,26126,26143,26149,26257,26376,26377,26399,26410,26641,26893,27491,27597,27700,28165,28385,29238,29275,29399,29482,29492,29677,30002,30003,30328,30333,31163,31179,31181,31435,31461,31471,32650,33098,33267,33410,33426,34382,34503,34544,34915,35806,35895,36763,36784,36824,37193,37325,38384,38451,38477,38500,38632,38634,38684,38706,38738,39532,40481,40736,40857)



def _font(b, face):
    def fail():
        raise ValueError('Invalid native U8g2 font')
    def be16(p):
        return int.from_bytes(b[p:p+2], 'big')
    signed = lambda n: n - 256 if n > 127 else n
    if (len(b) < 27 or any(not 1 <= n <= 8 for n in b[2:9]) or b[4] > 7 or b[5] > 7 or
            any(b[p] > 127 for p in (9,10,13,15))):
        fail()
    unicode = 23 + be16(21)
    if unicode < 25 or unicode + 4 > len(b): fail()
    glyphs, starts, budget = {}, set(), 0
    def parse(pos, prefix, cp):
        nonlocal budget
        if pos + prefix > len(b): fail()
        length = b[pos + prefix - 1]
        end, bit = pos + length, (pos + prefix) * 8
        if length <= prefix or end > len(b) or cp in glyphs: fail()
        starts.add(pos)
        def read(n):
            nonlocal bit
            if bit + n > end * 8: fail()
            value = 0
            for i in range(n):
                value |= ((b[bit >> 3] >> (bit & 7)) & 1) << i
                bit += 1
            return value
        width, height = read(b[4]), read(b[5])
        x, y, dx = (read(b[p]) - 2 ** (b[p]-1) for p in (6,7,8))
        if width > b[9] or height > b[10] or dx < 0 or (width and not height): fail()
        budget += width * height
        if len(glyphs) >= 16384 or budget > 8*1024*1024: fail()
        cursor = 0
        while width and cursor < width * height:
            zero, one = read(b[2]), read(b[3])
            if not zero and not one: fail()
            while True:
                cursor += zero + one
                repeat = read(1)
                if cursor > width*height or (cursor >= width*height and repeat): fail()
                if not repeat: break
        glyphs[cp] = (width,height,x,y,dx)
        return end
    p, previous = 23, 0
    while p+1 < unicode and b[p+1]:
        cp = b[p]
        if not cp or cp <= previous: fail()
        previous, p = cp, parse(p,2,cp)
    if p+2 != unicode or b[p] or b[p+1]: fail()
    if any(offset and 23+offset not in starts for offset in (be16(17),be16(19))): fail()
    table = be16(unicode)
    if table < 4 or table % 4 or unicode+table+2 > len(b): fail()
    blocks, ends, block = [], [], unicode
    for p in range(unicode,unicode+table,4):
        block += be16(p)
        last = be16(p+2)
        if (block < unicode+table or block+2 > len(b) or
                (blocks and block <= blocks[-1]) or (ends and last <= ends[-1])): fail()
        blocks.append(block); ends.append(last)
    if ends[-1] != 65535: fail()
    p, previous, positions = unicode+table, 255, {}
    while p+1 < len(b) and be16(p):
        cp = be16(p)
        if cp <= previous: fail()
        previous, positions[p] = cp, cp
        p = parse(p,3,cp)
    if p+2 > len(b) or any(b[p:]) or len(b)-p > 3: fail()
    for start,last in zip(blocks,ends):
        if start != p and (start not in positions or positions[start] > last): fail()
    bucket = 0
    for pos,cp in positions.items():
        while bucket+1 < len(blocks) and pos >= blocks[bucket+1]: bucket += 1
        if cp > ends[bucket] or (bucket and cp <= ends[bucket-1]): fail()
    if len(glyphs) & 255 != b[0]: fail()
    left, bottom = signed(b[11]), signed(b[12])
    top = b[10]+bottom
    if (not b[9] or not b[10] or left+b[9] > 127 or not 0 <= top <= 127 or bottom > 0 or
            b[13] != top or signed(b[14]) != bottom or b[15] != top or signed(b[16]) != bottom): fail()
    for width,height,x,y,dx in glyphs.values():
        if x < left or x+width > left+b[9] or y < bottom or y+height > top or not 0 <= dx <= 127: fail()
    required = set(range(48,59)) if face >= 4 else set(range(32,127))|{176,177,183,215}|set(CHINESE_REQUIRED) if face < 2 else set(range(48,58))
    if not required <= glyphs.keys(): fail()
    if face >= 4:
        if len(glyphs) != 11 or left or bottom: fail()
        for cp,(width,height,x,y,dx) in glyphs.items():
            if x or y or height != b[10] or width != dx or width < 1 or (cp != 58 and width != b[9]): fail()


def parse_package(package, maximum=32768):
    b = bytes(package)
    u32 = lambda p: struct.unpack_from('<I',b,p)[0]
    if (type(maximum) is not int or not 88 <= len(b) <= maximum <= 32768 or
            b[:6] != b'IKFA\x01\x00' or b[20] not in (4,6) or any(b[21:24]) or
            struct.unpack_from('<H',b,6)[0] != 24+16*b[20] or u32(12) != len(b)):
        raise ValueError('Invalid IKFA v1 header')
    cleared = bytearray(b); cleared[16:20] = bytes(4)
    if zlib.crc32(cleared) != u32(16): raise ValueError('Role package CRC mismatch')
    end, entries = 24+16*b[20], []
    for face in range(b[20]):
        p = 24+16*face
        offset,length,crc = struct.unpack_from('<III',b,p+4)
        if b[p:p+4] != bytes((1,face,1,0)) or offset != end or not 0 < length <= len(b)-offset:
            raise ValueError('Invalid role package directory')
        font = b[offset:offset+length]
        if zlib.crc32(font) != crc: raise ValueError('Role face CRC mismatch')
        _font(font,face)
        entries.append(dict(id=face,offset=offset,bytes=length,crc32=crc,format=1))
        end = offset+length
    if end != len(b): raise ValueError('Trailing role package bytes')
    return dict(schema=1,generation=u32(8),bytes=len(b),crc32=u32(16),faces=entries)


def validate(package, maximum_bytes=32768):
    parsed = parse_package(package, maximum_bytes)
    return dict(generation=parsed['generation'],size=parsed['bytes'],crc32=parsed['crc32'],
                sha256=hashlib.sha256(package).hexdigest().upper())
