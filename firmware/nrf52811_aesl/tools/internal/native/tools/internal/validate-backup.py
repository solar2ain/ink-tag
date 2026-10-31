#!/usr/bin/env python3
"""Read-only nRF52811 backup checks; optionally create a new bound manifest.

This validates saved bytes, not original-firmware authenticity or restore safety.
It never connects to hardware or assumes an external Flash was backed up.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(directory, serial, include_flash=True, require_double_read=False):
    if not serial or not serial.isdecimal():
        raise ValueError('J-Link serial must be explicit numeric text')
    names = {'flash': ('aesl22-nrf-flash.bin', 196608),
             'ficr': ('aesl22-nrf-ficr.bin', 4096),
             'uicr': ('aesl22-nrf-uicr.bin', 4096)}
    if not include_flash:
        del names['flash']
    saved = {}
    data = {}
    for kind, (name, length) in names.items():
        data[kind] = (directory / name).read_bytes()
        if len(data[kind]) != length:
            raise ValueError(f'{kind} size must be {length}')
        saved[kind] = {'file': name, 'bytes': length,
                       'sha256': hashlib.sha256(data[kind]).hexdigest()}
    ficr = data['ficr']
    part, _, _, ram, flash = struct.unpack_from('<5I', ficr, 0x100)
    if (part, ram, flash) != (0x52811, 24, 192):
        raise ValueError('FICR must identify nRF52811 / 24 KiB RAM / 192 KiB Flash')
    words = lambda offset, count: ' '.join(f'{v:08X}' for v in struct.unpack_from('<' + 'I' * count, ficr, offset))
    identity = {'jlinkSerial': serial, 'ficr': {
        '10000060': words(0x60, 2), '100000A4': words(0xa4, 2),
        '10000100': words(0x100, 1)}}
    info = directory / 'backup-info.txt'
    values = {}
    if info.exists():
        values = dict(line.split('=', 1) for line in info.read_text(encoding='utf-8-sig').splitlines() if '=' in line)
        for kind, entry in saved.items():
            if values.get(kind + '_sha256', '').lower() != entry['sha256']:
                raise ValueError(f'{kind} hash differs from backup-info.txt')
        if values.get('jlink_serial') and values['jlink_serial'] != serial:
            raise ValueError('J-Link serial differs from backup-info.txt')
    board = values.get('board', 'UNIDENTIFIED-PCB')
    if board not in ('aesl22_nrf', 'nrf52811_asel22', 'UNIDENTIFIED-PCB'):
        raise ValueError('Unknown board metadata; do not infer a PCB from the MCU')
    manifest = {'schema': 1, 'board': board, 'identity': identity,
                'files': saved, 'externalFlashIncluded': False}
    second = directory / 'read-2'
    if require_double_read or second.exists():
        if not include_flash or not second.is_dir() or second.is_symlink():
            raise ValueError('Second complete read is missing or unsafe')
        verification = {}
        for kind, (name, length) in names.items():
            path = second / name
            if path.is_symlink() or not path.resolve().is_relative_to(directory.resolve()):
                raise ValueError('Second complete read escapes the capture directory')
            if not path.is_file():
                raise ValueError(f'Second complete read is missing {kind}')
            raw = path.read_bytes()
            if len(raw) != length:
                raise ValueError(f'Second {kind} size must be {length}')
            value = hashlib.sha256(raw).hexdigest()
            if value != saved[kind]['sha256']:
                raise ValueError(f'Second {kind} SHA256 differs; both captures are retained')
            verification[kind] = {'first': dict(saved[kind]),
                'second': {'file': 'read-2/' + name, 'bytes': length, 'sha256': value}}
        manifest['readVerification'] = {'method': 'two-complete-reads-sha256',
            'captureCount': 2, 'ranges': verification, 'factoryProvenance': 'not-qualified'}
    if not include_flash:
        manifest.update(schema=2, internalFlashIncluded=False)
    return manifest


def save_new(path, value):
    if path.exists():
        if json.loads(path.read_text(encoding='utf-8-sig')) != value:
            raise ValueError(f'refusing to overwrite different evidence: {path}')
        return
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--jlink-serial', required=True)
    parser.add_argument('--write-manifest', action='store_true')
    parser.add_argument('--config-only', action='store_true',
                        help='Validate FICR/UICR only; no firmware backup is required.')
    parser.add_argument('--require-double-read', action='store_true',
                        help='Require two complete internal Flash/FICR/UICR captures with matching SHA256.')
    args = parser.parse_args()
    manifest = inspect(args.directory, args.jlink_serial, not args.config_only, args.require_double_read)
    previous = args.directory / ('config-manifest.json' if args.config_only else 'backup-manifest.json')
    if previous.exists() and json.loads(previous.read_text(encoding='utf-8-sig')) != manifest:
        raise ValueError(f'{previous.name} differs from saved bytes/identity')
    if args.write_manifest:
        save_new(previous, manifest)
        save_new(args.directory / 'expected-identity.json', manifest['identity'])
    print(json.dumps(manifest, indent=2))
