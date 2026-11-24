#!/usr/bin/env python3
"""Create an explicitly unsigned, SHA-256-checked DA14585 App OTA image.

Only a Recovery reporting CRC-only or the legacy optional policy admits this format.
No signing key, SDK, device access, or client dependency is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

PAYLOAD_LIMIT = 95744
TRAILER_BYTES = 96
BOARD = 0x42584144
LAYOUT = 0x31564144
UNSIGNED_VERIFIER = 0x31554144
UNSIGNED_MAGIC = 0x31554B49
PREFLIGHT_MAGIC = 0x31504B49


def package(raw: bytes, version: str, epoch: int = 1) -> bytes:
    if not raw or len(raw) > PAYLOAD_LIMIT - TRAILER_BYTES:
        raise ValueError("App raw must be 1..95648 bytes")
    if not isinstance(epoch, int) or not 0 <= epoch <= 0xffff:
        raise ValueError("Compatibility epoch must fit the existing Recovery policy")
    if re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version) is None:
        raise ValueError("Version must have four numeric components")
    parts = [int(p) for p in version.split('.')]
    if any(p > 255 for p in parts[:3]) or parts[3] > 65535 or len(version) >= 16:
        raise ValueError("Version exceeds native fields")
    release = parts[0] * 10000 + parts[1] * 100 + parts[2]
    prefix = struct.pack('<IBBHIIIIII', UNSIGNED_MAGIC, 1, 0, TRAILER_BYTES,
                         len(raw), epoch, BOARD, LAYOUT, UNSIGNED_VERIFIER, release)
    payload = raw + prefix + hashlib.sha256(raw + prefix).digest() + bytes(32)
    header = bytearray(b'\xff' * 64)
    header[:4] = b'pQ\xaa\xff'
    struct.pack_into('<II', header, 4, len(payload), zlib.crc32(payload))
    header[12:12 + len(version)] = version.encode('ascii')
    header[12 + len(version)] = 0
    # Reproducible native timestamp and unencrypted/unencrypted-key flags.
    struct.pack_into('<I', header, 28, 0)
    header[32:34] = b'\x00\x00'
    struct.pack_into('<IIIIII', header, 40, PREFLIGHT_MAGIC, len(raw), epoch,
                     BOARD, LAYOUT, UNSIGNED_VERIFIER)
    return bytes(header) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', required=True, help='e.g. 0.2.2.0')
    parser.add_argument('--epoch', type=int, default=1)
    args = parser.parse_args()
    image = package(args.input.read_bytes(), args.version, args.epoch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Preserve an existing artifact instead of silently replacing it.
    with args.output.open('xb') as stream:
        stream.write(image)
    print(json.dumps(dict(format='da-unsigned-iku1', publisherVerified=False,
                          path=str(args.output), bytes=len(image),
                          sha256=hashlib.sha256(image).hexdigest())))


if __name__ == '__main__':
    main()
