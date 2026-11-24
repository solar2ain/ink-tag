#!/usr/bin/env python3
"""Offline HW56 Full boot-selector binding; never opens a device."""
from __future__ import annotations

import argparse
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parents[2]
HEX = runpy.run_path(str(ROOT / 'tools/sdk/merge-ihex.py'))
RECOVERY, MBR_PARAMS, DFU_SETTINGS = 0x58000, 0x5E000, 0x5F000
IMAGE_START, FLASH_END = 0x60000, 0x80000
SELECTORS = {0xFF8: RECOVERY, 0xFFC: MBR_PARAMS}


def bind_selectors(memory: dict[int, int]) -> dict[int, int]:
    """MBR prioritizes these selectors over unchanged stock UICR NRFFW0/1.

    Only an erased slot or this exact HW56 value is accepted; don't overwrite
    an installed/unknown selector by silently interpreting an App as Full.
    """
    result = dict(memory)
    if not all(a in memory for a in (0, 0x26000, RECOVERY, MBR_PARAMS, DFU_SETTINGS)):
        raise ValueError('Complete HW56 Full components required')
    if any(a < 0 or a >= IMAGE_START for a in memory):
        raise ValueError('Full input overlaps images, UICR or unknown address space')
    for address, value in SELECTORS.items():
        old = bytes(memory.get(address + i, 255) for i in range(4))
        encoded = struct.pack('<I', value)
        if old not in (b'\xff' * 4, encoded):
            raise ValueError('Unknown/legacy MBR selector in candidate Full')
        result.update({address + i: b for i, b in enumerate(encoded)})
    return result


def validate_selectors(flash: bytes) -> None:
    if len(flash) != FLASH_END or any(struct.unpack_from('<I', flash, a)[0] != v
                                    for a, v in SELECTORS.items()):
        raise ValueError('HW56 requires both explicit MBR selectors')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a new file')
    HEX['write_hex'](args.output, bind_selectors(HEX['read_hex'](args.input)))


if __name__ == '__main__':
    main()
