#!/usr/bin/env python3
"""Verify read-only Nordic security inputs before compiling Recovery."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk-root', type=Path, required=True)
args = parser.parse_args()
lock = json.loads(Path(__file__).with_name('sdk-lock.json').read_text())['sdks']['nordic-nrf5-17.1.0']
for name, expected in {**lock['criticalFiles'], **lock['cryptoInputSnapshot']}.items():
    source = args.sdk_root / name
    if not source.is_file():
        raise SystemExit(f'Missing pinned Nordic input: {source}')
    if hashlib.sha256(source.read_bytes()).hexdigest().lower() != expected.lower():
        raise SystemExit(f'Changed pinned Nordic input: {source}')
print('Pinned Nordic validation / integrity inputs match (crc-only)')
