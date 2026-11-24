#!/usr/bin/env python3
"""Extract common GUI data sections and package one external-Flash image."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


MAGIC = 0x44464B49  # IKFD: independent fixed 16-byte resource, not uploaded IKFA
SCHEMA = 1
IMAGE_SIZE = 0x3000
SECTIONS = (
    ("font_object", ".rodata.u8g2_font_wqy9_t_lunar", 4315),
    ("font_object", ".rodata.u8g2_font_wqy12_t_lunar", 4830),
    ("font_object", ".rodata.u8g2_font_helvB14_tn", 287),
    ("font_object", ".rodata.u8g2_font_helvB18_tn", 389),
    ("qr_object", ".rodata.inktag_default_device_qr", 79),
    ("lunar_object", ".rodata.solar_1_1", 220),
    ("lunar_object", ".rodata.lunar_month_days", 220),
    ("lunar_object", ".rodata.YearMonthBit", 160),
)


def elf_sections(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF object")
    if data[4] != 1 or data[5] != 1:
        raise ValueError(f"{path}: expected a little-endian ELF32 object")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    section_offset = header[6]
    section_entry_size = header[11]
    section_count = header[12]
    string_index = header[13]
    if section_entry_size != 40 or string_index >= section_count:
        raise ValueError(f"{path}: invalid ELF32 section table")

    def section_header(index: int) -> tuple[int, ...]:
        offset = section_offset + index * section_entry_size
        if offset + section_entry_size > len(data):
            raise ValueError(f"{path}: truncated ELF32 section table")
        return struct.unpack_from("<IIIIIIIIII", data, offset)

    strings_header = section_header(string_index)
    strings_offset, strings_size = strings_header[4], strings_header[5]
    strings = data[strings_offset:strings_offset + strings_size]
    if len(strings) != strings_size:
        raise ValueError(f"{path}: truncated section-name string table")

    result: dict[str, bytes] = {}
    for index in range(section_count):
        entry = section_header(index)
        name_offset, section_type = entry[0], entry[1]
        content_offset, content_size = entry[4], entry[5]
        if name_offset >= len(strings) or section_type == 8:  # SHT_NOBITS
            continue
        name_end = strings.find(b"\0", name_offset)
        if name_end < 0:
            raise ValueError(f"{path}: unterminated section name")
        name = strings[name_offset:name_end].decode("ascii")
        content = data[content_offset:content_offset + content_size]
        if len(content) != content_size:
            raise ValueError(f"{path}: truncated section {name}")
        if name in result:
            raise ValueError(f"{path}: duplicate section {name}")
        result[name] = content
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-object", type=Path, required=True)
    parser.add_argument("--qr-object", type=Path, required=True)
    parser.add_argument("--lunar-object", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    objects = {
        "font_object": elf_sections(args.font_object),
        "qr_object": elf_sections(args.qr_object),
        "lunar_object": elf_sections(args.lunar_object),
    }
    parts = []
    for object_name, section_name, expected_size in SECTIONS:
        section = objects[object_name].get(section_name)
        if section is None:
            raise SystemExit(f"missing GUI asset section {section_name}")
        if len(section) != expected_size:
            raise SystemExit(
                f"{section_name} is {len(section)} bytes; expected {expected_size}"
            )
        parts.append(section)
    payload = b"".join(parts)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    image = struct.pack("<IIII", MAGIC, SCHEMA, len(payload), crc) + payload
    if len(image) > IMAGE_SIZE:
        raise SystemExit(f"GUI asset image needs {len(image)} bytes; limit is {IMAGE_SIZE}")
    image += b"\xff" * (IMAGE_SIZE - len(image))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(
        f"external GUI assets: {len(payload)}-byte payload, "
        f"{len(image)}-byte image, crc32={crc:08x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
