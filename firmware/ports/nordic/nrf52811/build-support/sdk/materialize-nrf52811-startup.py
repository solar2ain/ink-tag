#!/usr/bin/env python3
"""Keep every implemented interrupt, remove only unused trailing zero vectors.

The SDK startup supplies 128 words; nRF52811 defines IRQ0..29. Keep all 16
core entries and every IRQ0..29 entry, including reserved holes. Reset/ISR
bodies, table base and Flash partition remain unchanged.
"""
import argparse
import hashlib
from pathlib import Path
import re

STARTUP_SHA256 = 'a329b17cf3c5d8cd74e09c893ea853268543f39b4edb041ca2f1a6575bbecd47'


def materialize(source):
    payload = source.read_bytes()
    if hashlib.sha256(payload).hexdigest() != STARTUP_SHA256:
        raise ValueError('Startup source differs from hash-locked nRF52811 SDK17.1')
    text = payload.decode('utf-8').replace('\r\n', '\n')
    start = text.index('__isr_vector:\n')
    end = text.index('    .size __isr_vector', start)
    matches = list(re.finditer(r'^    \.long\s+(\S+)[^\n]*\n', text[start:end], re.M))
    if len(matches) != 128 or any(m[1] != '0' for m in matches[46:]):
        raise ValueError('Unexpected SDK vector entries or nonzero trailing vector')
    first_removed = start + matches[46].start()
    return ('/* Generated nRF52811 startup: all core/IRQ0..29 entries retained. */\n'
            + text[:first_removed] + text[end:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    text = materialize(args.source)
    if not args.check_only:
        if args.output is None:
            parser.error('--output is required')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
