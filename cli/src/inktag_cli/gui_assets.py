from __future__ import annotations

import struct
import zlib
from typing import Any


MAGIC = b"ITFA"
SCHEMA = 1
HEADER_SIZE = 24
ENTRY_SIZE = 16
FONT_COUNT = 4
MAX_PACKAGE_BYTES = 16 * 1024


def validate_gui_asset_package(package: bytes) -> dict[str, Any]:
    if not 88 <= len(package) <= MAX_PACKAGE_BYTES or package[:4] != MAGIC:
        raise ValueError("not a bounded InkTag GUI asset package")
    schema, header_size = struct.unpack_from("<HH", package, 4)
    generation, total_size, expected_crc = struct.unpack_from("<III", package, 8)
    if (
        schema != SCHEMA
        or header_size != HEADER_SIZE + FONT_COUNT * ENTRY_SIZE
        or total_size != len(package)
        or package[20] != FONT_COUNT
        or package[21:24] != b"\0\0\0"
    ):
        raise ValueError("incompatible GUI asset package header")
    crc_input = bytearray(package)
    crc_input[16:20] = b"\0\0\0\0"
    if zlib.crc32(crc_input) & 0xFFFFFFFF != expected_crc:
        raise ValueError("GUI asset package CRC mismatch")

    entries: list[dict[str, int]] = []
    seen: set[int] = set()
    previous_end = header_size
    for index in range(FONT_COUNT):
        kind, face_id, data_format, flags, offset, length, entry_crc = struct.unpack_from(
            "<4BIII", package, HEADER_SIZE + index * ENTRY_SIZE
        )
        end = offset + length
        if (
            kind != 1
            or face_id >= FONT_COUNT
            or face_id in seen
            or data_format != 1
            or flags != 0
            or length == 0
            or offset < previous_end
            or end > len(package)
        ):
            raise ValueError("invalid GUI asset directory")
        if zlib.crc32(package[offset:end]) & 0xFFFFFFFF != entry_crc:
            raise ValueError(f"GUI asset face {face_id} CRC mismatch")
        entries.append({"id": face_id, "offset": offset, "bytes": length, "crc32": entry_crc})
        seen.add(face_id)
        previous_end = end
    if seen != set(range(FONT_COUNT)) or previous_end != len(package):
        raise ValueError("incomplete GUI asset package")
    return {
        "schema": schema,
        "generation": generation,
        "bytes": len(package),
        "crc32": expected_crc,
        "faces": entries,
    }


def decode_gui_asset_info(payload: bytes) -> dict[str, int | None]:
    if len(payload) < 20 or payload[0] != 1 or payload[1] != SCHEMA:
        raise ValueError("invalid GUI asset status response")
    generation, active_size, maximum_size, active_crc = struct.unpack_from("<IIII", payload, 4)
    return {
        "version": payload[0],
        "schema": payload[1],
        "activeBank": None if payload[2] == 0xFF else payload[2],
        "generation": generation,
        "activeBytes": active_size,
        "maximumBytes": maximum_size,
        "crc32": active_crc,
    }
