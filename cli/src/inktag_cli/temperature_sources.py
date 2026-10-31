"""Decode the optional final GET_STATUS temperature-source record."""

from __future__ import annotations

import struct


def has_temperature_source_record(payload: bytes) -> bool:
    if len(payload) < 25 or payload[0] != 1:
        return False
    trailer = payload[-9:]
    if trailer[:4] != b"ITT\x01" or trailer[4] & ~0x03:
        return False
    flags = trailer[4]
    mcu, panel = struct.unpack_from("<hh", trailer, 5)
    if ((flags & 0x01) == 0 and mcu != 0) or ((flags & 0x02) == 0 and panel != 0):
        return False
    return True


def parse_temperature_sources(payload: bytes) -> tuple[float | None, float | None]:
    if not has_temperature_source_record(payload):
        return None, None
    trailer = payload[-9:]
    flags = trailer[4]
    mcu, panel = struct.unpack_from("<hh", trailer, 5)
    return (mcu / 16 if flags & 0x01 else None,
            panel / 16 if flags & 0x02 else None)
