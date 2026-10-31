#!/usr/bin/env python3
"""Frozen native journal/default record helpers; no CLI entry or device I/O."""

import argparse
import binascii
import json
import runpy
import struct
from pathlib import Path


PAGE_SIZE = 4096
COMPACT_PAGE_MAGIC = int.from_bytes(b'ISP1', 'little')
COMPACT_RECORD_MAGIC = int.from_bytes(b'ISR1', 'little')
COMPACT_RECORD_COMMIT = int.from_bytes(b'ISC1', 'little')
DEVICE_INFO_MAX_BYTES = 407
BOOT_DISPLAY_MAGIC = 0x3153424E
BOOT_DISPLAY_VERSION = 1
BOOT_DISPLAY_FIRST_REQUEST = 0x01

BOARDS = {
    "nrf52832_priceetag": {
        "user_font_start": 0x4E000,
        "user_font_size": 0x8000,
        "settings_start": 0x56000,
        "settings_pages": 2,
        "app_start": 0x26000,
        "app_end": 0x4E000,
        "epd": bytes((0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0xFF,
                      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00)),
        "screen": (1, 0x21, 1, 0, 152, 152, 0, 0, 152, 152),
    },
}


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def compact_record(record_type: int, data: bytes) -> bytes:
    checksum = fnv1a(struct.pack("<HH", record_type, len(data)) + data)
    padded = data + bytes((-len(data)) & 3)
    return (struct.pack("<IHHI", COMPACT_RECORD_MAGIC, record_type,
                        len(data), checksum) + padded +
            struct.pack("<I", COMPACT_RECORD_COMMIT))


def screen_record(values: tuple[int, ...]) -> bytes:
    # The installed HW55 template used schema1. Current common reads only the
    # 17-byte native screen ABI and its28-byte record, even at native AUTO.
    _, driver, color, transform, *geometry = values
    wire = struct.pack("<BBBBBHHHHHH", 1, driver, 0, color, transform, *geometry)
    body = b"SP" + bytes((1, len(wire))) + wire + bytes(3)
    return body + struct.pack("<I", fnv1a(body))


def factory_names(ficr: bytes) -> tuple[bytes, bytes]:
    """Exact HW55 factory identity, shared with the per-unit Full preparer."""
    if (len(ficr) != PAGE_SIZE or struct.unpack_from('<I', ficr, 0x100)[0] != 0x52832 or
            struct.unpack_from('<I', ficr, 0x10)[0] != PAGE_SIZE or
            struct.unpack_from('<I', ficr, 0x14)[0] != 128 or
            struct.unpack_from('<I', ficr, 0x10c)[0] != 64 or
            struct.unpack_from('<I', ficr, 0x110)[0] != 512 or
            ficr[0x60:0x68] in (bytes(8), b'\xff' * 8) or
            ficr[0xa4:0xaa] in (bytes(6), b'\xff' * 6)):
        raise ValueError('Exact factory FICR identity rejected')
    id4 = struct.unpack_from('<I', ficr, 0xa4)[0] & 0xffff
    return f'IKT_NRF_{id4:04X}'.encode('ascii'), f'InkTag {id4:04X}'.encode('ascii')


def display_wire_flags(wire: bytes) -> int:
    if (not 9 <= len(wire) <= 21 or wire[:2] != b'\x02\x01' or wire[2] > 1 or wire[3] & ~0x11 or
            len(wire) != 9 + wire[8] or wire[8] > 12 or
            (struct.unpack_from('<I', wire, 4)[0] == 0) != (wire[8] == 0)):
        raise ValueError('Saved display settings rejected')
    try:
        countdown = wire[9:].decode('utf-8')
    except UnicodeDecodeError as error:
        raise ValueError('Saved countdown rejected') from error
    if len(countdown) > 4 or any(ord(c) < 0x20 or ord(c) == 0x7f for c in countdown):
        raise ValueError('Saved countdown rejected')
    return wire[3]


def device_settings_names(data: bytes) -> tuple[bytes, bytes]:
    """Exact NUS1 admission; never imports historical preference envelopes."""
    from nordic_full_config import validate_io, DEFAULT_PINS
    if (len(data) != 112 or data[:5] != b'NUS1\x01' or data[79] or
            data[78] not in (0, 1) or fnv1a(data[:108]) != struct.unpack_from('<I', data, 108)[0]):
        raise ValueError('Unknown Nordic release-v1 user authority')
    n, length, size = data[5], data[25], data[56]
    if (n > 19 or length > 24 or size > 21 or any(data[6+n:25]) or
            any(data[26+length:50]) or any(data[57+size:78]) or
            any(data[i] > 10 or data[i] == 8 for i in (50, 52, 54))):
        raise ValueError('Device-settings fields/padding rejected')
    name, label = data[6:6+n], data[26:26+length]
    if name and (not any(name.startswith(p) and len(name) > len(p) for p in (b'IKT_', b'InkTag_')) or
                 any(b < 0x20 or b == 0x7f for b in name)):
        raise ValueError('Saved BLE name rejected')
    try:
        text = label.decode('utf-8')
    except UnicodeDecodeError as error:
        raise ValueError('Saved device label rejected') from error
    if any(ord(c) < 0x20 or ord(c) == 0x7f for c in text):
        raise ValueError('Saved device label rejected')
    display_wire_flags(data[57:57+size])
    if not data[78] and any(data[80:96]):
        raise ValueError('Absent pin override must be zero')
    validate_io(data[80:96] if data[78] else DEFAULT_PINS, data[96:108])
    return name, label


def identity_display_source_valid(identity: bytes | None, page: bytes | None) -> None:
    # ID4 is replay-only. It can neither supply nor shadow preferences.
    if identity is not None:
        device_settings_names(identity)
    if page is not None and (len(page) != 16 or page[:5] != b'NDP1\x01' or
            page[5:8] != bytes(3) or page[8] > 5 or
            (page[8] == 4 and not 30 <= struct.unpack_from('<I', page, 12)[0] <= 86400)):
        raise ValueError('Page replay authority rejected')


def device_settings_record(name: bytes, label: bytes, *, aux: bytes | None = None) -> bytes:
    from nordic_full_config import DEFAULT_AUX
    if len(name)>19 or len(label)>24:
        raise ValueError('Nordic identity lengths exceeded')
    data = bytearray(112)
    data[:5] = b'NUS1\x01'
    data[5] = len(name)
    data[6:25] = name.ljust(19, b'\0')
    data[25] = len(label)
    data[26:50] = label.ljust(24, b'\0')
    data[50:56] = bytes((1, 0, 10, 0, 9, 0))
    data[56:66] = bytes((9, 2, 1, 1, 0, 0, 0, 0, 0, 0))
    data[96:108] = DEFAULT_AUX if aux is None else aux
    struct.pack_into('<I', data, 108, fnv1a(data[:108]))
    device_settings_names(data)
    return bytes(data)


def complete_device_names(data: bytes | None, ficr: bytes) -> bytes:
    # Valid user records, including deliberately empty fields, are preserved.
    if data is not None:
        device_settings_names(data)
        return data
    return factory_device_settings_record(ficr)


def factory_device_settings_record(ficr: bytes, *, aux: bytes | None = None) -> bytes:
    return device_settings_record(*factory_names(ficr), aux=aux)

