"""Shared OM6626 application-image routing helpers."""

import struct


APP_XIP_BASE = 0x0042B000
APP_XIP_END = 0x00475000
LAUNCH_RECORD_OFFSET = 0x100
LAUNCH_RECORD_MAGIC = 0x314C4D4F  # "OML1"
LAUNCH_RECORD_GUARD = 0xB007A55A
FIXED_BOOT_ENTRY_XIP = 0x00404101


def route_application_through_fixed_boot(firmware: bytes, *,
                                        app_xip_base: int = APP_XIP_BASE,
                                        app_xip_end: int = APP_XIP_END) -> bytes:
    """Patch the reset vector while preserving its real target in the image."""
    if len(firmware) < LAUNCH_RECORD_OFFSET + 12:
        raise ValueError("OM6626 application is too small for its launch record")
    stack, reset = struct.unpack_from("<II", firmware, 0)
    magic, recorded_reset, guard = struct.unpack_from(
        "<III", firmware, LAUNCH_RECORD_OFFSET
    )
    if not (0x20000000 <= stack <= 0x20013EA0):
        raise ValueError(f"invalid OM6626 application stack 0x{stack:08X}")
    if not (app_xip_base <= (reset & ~1) < app_xip_end and reset & 1):
        raise ValueError(f"invalid OM6626 application reset vector 0x{reset:08X}")
    if (magic, recorded_reset, guard) != (
        LAUNCH_RECORD_MAGIC,
        reset,
        LAUNCH_RECORD_GUARD,
    ):
        raise ValueError("OM6626 launch record does not match the application vector")
    result = bytearray(firmware)
    struct.pack_into("<I", result, 4, FIXED_BOOT_ENTRY_XIP)
    return bytes(result)
