#!/usr/bin/env python3
"""Validate the exact S112/App boot checks used by the compact Recovery."""
import argparse
from pathlib import Path
import struct
import zlib


def parse_hex(path):
    image = {}
    base = 0
    ended = False
    for line in Path(path).read_text().splitlines():
        if not line:
            continue
        if ended:
            raise ValueError('data after HEX EOF')
        record = bytes.fromhex(line.removeprefix(':'))
        if len(record) < 5 or not line.startswith(':') or sum(record) & 255 or len(record) != record[0] + 5:
            raise ValueError('invalid Intel HEX record')
        size, address, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
        data = record[4:4 + size]
        if kind == 0:
            for i, value in enumerate(data):
                target = base + address + i
                if target in image and image[target] != value:
                    raise ValueError('conflicting HEX records')
                image[target] = value
        elif kind == 2:
            if size != 2 or address:
                raise ValueError('invalid HEX segment record')
            base = int.from_bytes(data, 'big') << 4
        elif kind == 4:
            if size != 2 or address:
                raise ValueError('invalid HEX linear address record')
            base = int.from_bytes(data, 'big') << 16
        elif kind == 1:
            if size or address:
                raise ValueError('invalid HEX EOF')
            ended = True
        elif kind not in (3, 5) or size != 4 or address:
            raise ValueError('unsupported or malformed HEX record')
    if not ended:
        raise ValueError('missing HEX EOF')
    return image


def validate_assets(assets):
    if len(assets) != 12288:
        raise ValueError('font region must be 12 KiB')
    magic, schema, length, checksum = struct.unpack_from('<IIII', assets)
    if magic != 0x44464b49 or (schema, length) != (1, 12228):
        raise ValueError('unexpected font manifest')
    if zlib.crc32(assets[16:16 + length]) & 0xffffffff != checksum:
        raise ValueError('font payload CRC mismatch')
    if assets[16 + length:] != b'\xff' * (len(assets) - 16 - length):
        raise ValueError('font padding mismatch')


def validate_component(image, start, end, name):
    allowed_uicr = range(0x10001014, 0x1000101c) if name == 'Recovery' else ()
    if not image or any(not start <= a < end and a not in allowed_uicr for a in image):
        raise ValueError(f'{name} is outside its fixed window')
    if any(a not in image for a in range(start, start + 8)):
        raise ValueError(f'{name} vector is incomplete')
    sp, reset = struct.unpack('<II', bytes(image[a] for a in range(start, start + 8)))
    if not (0x20000000 < sp <= 0x20006000 and sp % 8 == 0 and reset & 1 and
            start <= reset & ~1 < end and (reset & ~1) in image):
        raise ValueError(f'{name} vector is invalid')
    return max(a for a in image if start <= a < end) + 1 - start


def validate(image, assets=None):
    def span(address, size):
        return bytes(image.get(a, 255) for a in range(address, address + size))

    def word(address):
        return struct.unpack('<I', span(address, 4))[0]

    def check(condition, message):
        if not condition:
            raise ValueError(message)

    crc = lambda data: zlib.crc32(data) & 0xffffffff
    check(all(0 <= a < 0x30000 for a in image), 'Full crosses Flash/UICR boundary')
    check(word(0xff8) == 0x2a000 and word(0xffc) == 0x2e000, 'wrong Recovery/MBR addresses')
    for vector in (0x19000, 0x2a000):
        check(0x20000000 < word(vector) <= 0x20006000 and word(vector) % 8 == 0 and
              word(vector + 4) & 1, 'invalid stack/Thumb vector')
    check(0x2a000 <= word(0x2a004) & ~1 < 0x2e000, 'invalid Recovery vector')
    check(0x19000 <= word(0x19004) & ~1 < 0x2a000, 'invalid App vector')
    primary = span(0x2f000, 0x323)
    check(primary == span(0x2e000, len(primary)), 'settings backup differs')
    for origin in (0x2e000, 0x2f000):
        check(word(origin) == crc(span(origin + 4, 88)), 'settings CRC mismatch')
        check(word(origin + 4) == 2, 'settings schema must be 2')
        check(word(origin + 0x20) == 1, 'App bank is not valid')
        app_size, sd_size = word(origin + 0x18), word(origin + 0x34)
        check(0 < app_size <= 69632 and 0 < sd_size <= 0x18000, 'invalid App/SD lengths')
        # Packed SDK17 settings: the compact boot validator accepts CRC only.
        check(span(origin + 0x260, 1) == b'\x01', 'SoftDevice boot validation must be CRC')
        check(span(origin + 0x2a1, 1) == b'\x01', 'App boot validation must be CRC')
        check(word(origin + 0x25c) == crc(span(origin + 0x260, 195)), 'boot validation CRC mismatch')
        check(word(origin + 0x261) == crc(span(0x1000, sd_size)), 'SoftDevice CRC mismatch')
        app_crc = crc(span(0x19000, app_size))
        check(word(origin + 0x2a2) == app_crc and word(origin + 0x1c) == app_crc, 'App CRC mismatch')
    if assets is not None:
        validate_assets(assets)
    return app_size, sd_size


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('full', type=Path, nargs='?')
    parser.add_argument('--application', type=Path)
    parser.add_argument('--recovery', type=Path)
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    if bool(args.full) == bool(args.application) or (args.full and args.recovery):
        parser.error('select a Full HEX or --application [--recovery]')
    assets = args.assets.read_bytes() if args.assets else None
    if args.full:
        app, sd = validate(parse_hex(args.full), assets)
        print(f'PASS Full: App {app}/69632; SD {sd}; both settings/boot CRCs; vectors/boundaries; fonts checked={assets is not None}')
    else:
        app = validate_component(parse_hex(args.application), 0x19000, 0x2a000, 'App')
        if args.recovery:
            recovery = validate_component(parse_hex(args.recovery), 0x2a000, 0x2e000, 'Recovery')
            print(f'PASS Recovery: {recovery}/16384')
        if assets is not None:
            validate_assets(assets)
        print(f'PASS App: {app}/69632; HEX checksum/window/vector; fonts checked={assets is not None}')
