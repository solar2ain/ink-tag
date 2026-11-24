#!/usr/bin/env python3
"""Pin ordinary Nordic SDK inputs; retain only the non-loadable base identity."""
import argparse
import hashlib
from pathlib import Path

LOCKS = {
    'crc32.c': 'fbadf2662cd3488c74ddb9212e789a81b20020ca304757498e39121182c6ad25',
    'nrf_common.ld': '3e9ebee00a39424c3e67b83d57c6b92e0a755d7a183f892237145075dad61294',
    'nrf_bootloader_app_start.c': 'c7dfafe1eff65422a2d48cef70a87faeedd655ba5efb02dd82298de1b60721cb',
}


def materialize(source, role):
    source = Path(source)
    raw = source.read_bytes()
    if role not in ('app', 'recovery') or hashlib.sha256(raw).hexdigest() != LOCKS.get(source.name):
        raise ValueError('Unknown Nordic base SDK input')
    if source.name != 'nrf_common.ld' and role != 'recovery':
        raise ValueError('Native CRC/handoff inputs belong to Recovery')
    text = raw.decode('utf-8').replace('\r\n', '\n')
    if source.name == 'nrf_common.ld':
        if role == 'recovery':
            marker = '        KEEP(*(.isr_vector))\n'
            if text.count(marker) != 1:raise ValueError('Unknown Nordic vector table placement')
            text = text.replace(marker, marker + '        KEEP(*(.rodata.g_inktag_nrf_recovery_protocol*))\n')
        text += '\nSECTIONS { .inktag_source_profile 0 (INFO) : { KEEP(*(.inktag_source_profile)) } }\n'
    return text


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--role', choices=('app', 'recovery'), required=True)
    args = parser.parse_args()
    result = materialize(args.source, args.role)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, encoding='utf-8', newline='\n')
