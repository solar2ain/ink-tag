#!/usr/bin/env python3
"""Build the native IKFA v1 package from the application font source."""

from __future__ import annotations

import argparse
import ast
import hashlib
import base64
import json
import re
import struct
import sys
import zlib
from pathlib import Path


SCHEMA = 1
HEADER_SIZE = 24
ENTRY_SIZE = 16
MAX_BYTES = 32 * 1024
ABSOLUTE_MAX_BYTES = 32 * 1024
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "cli/src"))
TEMPLATE = Path(__file__).resolve().parents[2] / 'firmware/factory/native-font-template.json'
FONTS = (
    (0, "u8g2_font_wqy9_t_lunar"),
    (1, "u8g2_font_wqy12_t_lunar"),
    (2, "u8g2_font_helvB14_tn"),
    (3, "u8g2_font_helvB18_tn"),
)


def extract_font(source: str, name: str) -> bytes:
    match = re.search(
        rf"const\s+uint8_t\s+{re.escape(name)}\[(\d+)\][\s\S]*?"
        rf"=\s*((?:\"(?:\\.|[^\"\\])*\"\s*)+);",
        source,
    )
    if match is None:
        raise ValueError(f"font array {name} was not found")
    declared = int(match.group(1))
    decoded = bytearray()
    for token in re.findall(r'"(?:\\.|[^"\\])*"', match.group(2)):
        value = ast.literal_eval(token)
        decoded.extend(value.encode("latin-1"))
    if len(decoded) + 1 != declared:
        raise ValueError(
            f"{name}: decoded {len(decoded)} bytes plus NUL; expected {declared}"
        )
    decoded.append(0)
    return bytes(decoded)


def native_template() -> tuple[bytes, str]:
    data=json.loads(TEMPLATE.read_text(encoding='utf-8'))
    package=base64.b64decode(data['package_base64'],validate=True)
    if (data['format_version']!=1 or data['package_schema']!=SCHEMA or
            hashlib.sha256(package).hexdigest()!=data['package_sha256'] or
            len(package)<88 or package[:8]!=b'IKFA\x01\x00\x58\x00'):
        raise ValueError('invalid native factory-font template')
    return package,data['source_fonts_sha256']


def generation_package(template: bytes, generation: int) -> bytes:
    if type(generation) is not int or not 0<=generation<=0xffffffff:
        raise ValueError('generation must fit uint32')
    package=bytearray(template)
    struct.pack_into('<I',package,8,generation)
    package[16:20]=b'\0'*4
    struct.pack_into('<I',package,16,zlib.crc32(package))
    return bytes(package)


def build(fonts: list[tuple[int, bytes]], generation: int, maximum_bytes: int = MAX_BYTES) -> bytes:
    if [face for face, _ in fonts] != list(range(4)):
        raise ValueError('default package requires the four ordered built-in faces')
    template,source_hash=native_template()
    identity=b''.join(struct.pack('<I',len(data))+data for _,data in fonts)
    if hashlib.sha256(identity).hexdigest()==source_hash:
        package=generation_package(template,generation)
        validate(package,maximum_bytes)
        return package
    from inktag_cli.gui_font_roles import compose_font_roles
    return compose_font_roles([data for _, data in fonts], {}, generation, maximum_size=maximum_bytes)


def validate(package: bytes, maximum_bytes: int = MAX_BYTES) -> dict[str, int | str]:
    if type(maximum_bytes) is not int or not 88<=maximum_bytes<=ABSOLUTE_MAX_BYTES or not 88<=len(package)<=maximum_bytes:
        raise ValueError('invalid explicit native package capacity')
    template,_=native_template()
    generation=struct.unpack_from('<I',package,8)[0]
    if len(package)==len(template) and package==generation_package(template,generation):
        return {'generation':generation,'size':len(package),'crc32':struct.unpack_from('<I',package,16)[0],
                'sha256':hashlib.sha256(package).hexdigest().upper()}
    from inktag_cli.gui_font_roles import validate_font_package
    parsed = validate_font_package(package, maximum_size=maximum_bytes)
    return {'generation': parsed['generation'], 'size': parsed['bytes'],
            'crc32': parsed['crc32'], 'sha256': hashlib.sha256(package).hexdigest().upper()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fonts", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--validate", type=Path)
    parser.add_argument("--generation", type=lambda value: int(value, 0), default=1)
    parser.add_argument('--max-bytes', type=int, choices=(16384, 32768), default=MAX_BYTES,
                        help='Explicit complete-package limit; default is32 KiB.')
    args = parser.parse_args()
    if args.validate is not None:
        if args.fonts is not None or args.output is not None:
            parser.error("--validate cannot be combined with --fonts/--output")
        try:
            info = validate(args.validate.read_bytes(), args.max_bytes)
        except (OSError, ValueError) as error:
            parser.error(str(error))
        print(
            f"IKFA v1 valid: generation={info['generation']}, "
            f"bytes={info['size']}, crc32={info['crc32']:08X}, "
            f"sha256={info['sha256']}"
        )
        return
    if args.fonts is None or args.output is None:
        parser.error("--fonts and --output are required when not validating")
    if not 0 <= args.generation <= 0xFFFFFFFF:
        parser.error("--generation must fit uint32")
    source = args.fonts.read_text(encoding="utf-8")
    fonts = [(face_id, extract_font(source, name)) for face_id, name in FONTS]
    package = build(fonts, args.generation, args.max_bytes)
    validate(package, args.max_bytes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(
        f"IKFA v1: generation={args.generation}, bytes={len(package)}, "
        f"crc32={struct.unpack_from('<I', package, 16)[0]:08X}"
    )


if __name__ == "__main__":
    main()
