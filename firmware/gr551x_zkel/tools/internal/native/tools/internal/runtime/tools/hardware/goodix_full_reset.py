"""Bounded existing SDK stale-watchdog cause cleanup before Full's reset.

No SRAM addresses, firmware ABI, new reset path or persistent suppression.
GR5331 SDK clears observed low status bits by writeback; GR551x LL uses W0C.
"""
from __future__ import annotations

REGISTERS = {
    'GR5331': (0x4000A384, 0x04, 'status-writeback'),
    'GR5513': (0xA000C544, 0x40, 'write-zero-to-clear'),
    'GR5515': (0xA000C544, 0x40, 'write-zero-to-clear'),
}


def plan(chip, value):
    if chip not in REGISTERS or type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('Unknown MCU/reset register state; App remains halted')
    address, mask, semantics = REGISTERS[chip]
    write = None
    if value & mask:
        # Preserve all control fields and unrelated W1C status on GR5331.
        # GR551x W0C: ones leave every unrelated event untouched.
        write = ((value & ~0x3f) | mask) if chip == 'GR5331' else 0xffffffff ^ mask
    return dict(chip=chip,address=address,mask=mask,semantics=semantics,
                before=value,write=write)


def verify(prepared, after):
    if type(after) is not int or not 0 <= after <= 0xffffffff:
        raise ValueError('Incomplete reset status readback; App remains halted')
    mask = prepared['mask']
    if after & mask or (after & ~mask) != (prepared['before'] & ~mask):
        raise ValueError('Reset cause cleanup/readback changed or failed; '
                         'App remains halted; no automatic reset/write replay')
    return dict(chip=prepared['chip'],address=hex(prepared['address']),
                staleWatchdogCauseCleared=True,unrelatedBitsPreserved=True,
                before=prepared['before'],after=after,writeRequested=prepared['write'] is not None,
                sramAccessed=False,futureWatchdogSuppressed=False)
