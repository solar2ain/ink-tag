"""Derive native fixed-mapping startup from the pinned SDK; no signer/device."""
from pathlib import Path
import argparse


def generate(source: str, role: str) -> str:
    opening = '#if USE_SET_BOOT\nSET_BOOT:'
    if source.count(opening) != 1:
        raise ValueError('Pinned SDK boot-selection block changed; refusing startup generation')
    start = source.index(opening)
    end = source.index('#endif', start) + len('#endif')
    source = source[:start] + '''/* Fixed physical XIP. RAM4/5 never select an App. */
    tloadr r0, BOOT_SEL_D
    tmov r1,#0
    tstorerb r1,[r0,#0]
''' + source[end:]
    if role == 'recovery':
        stack = '(__RAM_START_ADDR + __RAM_SIZE_MAX)'
        if source.count(stack) != 2:
            raise ValueError('Pinned SDK stack definition changed')
        source = source.replace(stack, '(__RAM_START_ADDR + __RAM_SIZE_MAX - 0x1000)')
        other = '(__RAM_START_ADDR + __RAM_SIZE_MAX - IRQ_STK_SIZE)'
        if source.count(other) != 1:
            raise ValueError('Pinned SDK non-IRQ stack definition changed')
        source = source.replace(other, '(__RAM_START_ADDR + __RAM_SIZE_MAX - 0x1000 - IRQ_STK_SIZE)')
    elif role != 'app':
        raise ValueError('Unknown native startup role')
    return source


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--role', choices=('app', 'recovery'), required=True)
    a = p.parse_args()
    text = generate(a.input.read_text(), a.role)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    if not a.output.exists() or a.output.read_text() != text:
        a.output.write_text(text)
