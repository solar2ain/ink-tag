"""Nordic SWD final-reset diagnostic cleanup. Pure offline; no target code.

Only nRF52832 xxAA's physical 64 KiB SRAM is admitted, from actual FICR.
This is NOT a general resident-loader clearing recipe. The writer must finish
all target helpers, hold the CPU halted across direct debug writes/readback,
and run no target instruction until the sole guarded native reset.
"""
from __future__ import annotations
import struct


def cleanup_plan(ficr: bytes) -> dict:
    if len(ficr) != 4096:
        raise ValueError('Incomplete FICR for physical SRAM ownership')
    word = lambda at: struct.unpack_from('<I', ficr, at)[0]
    if word(0x100) != 0x52832 or word(0x10c) != 64 or word(0x110) != 512:
        raise ValueError('Unknown physical SRAM part/capacity; no erase permitted')
    return dict(ramAddress=0x20000000, ramBytes=65536,
                resetReasonAddress=0x40000400, resetReasonKnownMask=0x000F000F,
                algorithm='nrf-swd-final-reset-direct-debug-v1',
                noTargetWorkerAfterCleanup=True, resetOnlyAfterReadback=True)


def observation(plan: dict, reset_reason: bytes) -> int:
    if len(reset_reason) != 4:
        raise ValueError('Incomplete sticky reset-cause read')
    bits = struct.unpack('<I', reset_reason)[0]
    if bits & ~plan['resetReasonKnownMask']:
        raise ValueError('Unknown sticky reset-cause bits; no cleanup/reset')
    return bits


def verify_cleanup(plan: dict, ram: bytes, reset_before: bytes, reset_after: bytes) -> None:
    if len(ram) != plan['ramBytes'] or ram != bytes(plan['ramBytes']):
        raise ValueError('Physical SRAM diagnostic cleanup readback mismatch; no reset')
    if len(reset_before) != 4 or len(reset_after) != 4:
        raise ValueError('Incomplete sticky reset-cause readback; no reset')
    before, after = (struct.unpack('<I', x)[0] for x in (reset_before, reset_after))
    if before & ~plan['resetReasonKnownMask'] or after != 0:
        raise ValueError('Sticky reset-cause cleanup readback mismatch; no reset')
