#!/usr/bin/env python3
"""Create the versioned nRF52811 external-Flash font asset image."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


MAGIC = 0x44464b49  # IKFD: fixed independent GUI resources, not a user font upload
SCHEMA = 1
PAYLOAD_SIZE = 4_315 + 4_830 + 287 + 389 + 79 + 220 + 220 + 160
IMAGE_SIZE = 0x3000


def package(payload: bytes, tables: bytes = b'') -> bytes:
    if len(payload) != PAYLOAD_SIZE:
        raise ValueError(
            f"font payload is {len(payload)} bytes; expected {PAYLOAD_SIZE}"
        )
    payload += tables
    if len(payload) + 16 > IMAGE_SIZE:
        raise ValueError('GUI assets exceed the existing font region')
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack("<IIII", MAGIC, SCHEMA, len(payload), crc)
    image = header + payload
    return image + (b"\xff" * (IMAGE_SIZE - len(image)))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("payload", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--tables", type=Path)
    parser.add_argument("--binding-only", action="store_true",
                        help="Bind the application's GUI ABI to a packaged image")
    args = parser.parse_args()

    if args.binding_only:
        image = args.payload.read_bytes()
        magic, schema, length, crc = struct.unpack_from('<IIII', image)
        if (len(image) != IMAGE_SIZE or magic != MAGIC or schema != SCHEMA or
                length > IMAGE_SIZE - 16 or zlib.crc32(image[16:16+length]) != crc):
            raise ValueError('Invalid GUI image for compile-time binding')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text('#define INKTAG_NRF_GUI_EXPECTED_CRC 0x%08xu\n' % crc,
                               encoding='ascii')
        return 0

    payload = args.payload.read_bytes()
    image = package(payload, args.tables.read_bytes() if args.tables else b'')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    crc = struct.unpack_from("<I", image, 12)[0]
    print(
        f"external font assets: {struct.unpack_from('<I', image, 8)[0]}-byte payload, "
        f"{len(image)}-byte image, crc32={crc:08x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
