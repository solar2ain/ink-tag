#!/usr/bin/env python3
"""Offline nRF52832 capture provenance and verified historical-path lookup.

No transport, debugger, restore or firmware-writing implementation lives here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import runpy
import stat
import struct
import zlib

ROOT = Path(__file__).resolve().parents[2]
STOCK_HASHES = {
    'flash': '439c1fefa1684c69df25caf67c0a568fe2844d5ecd17389bd7a10676497e45da',
    'ficr': '2364f631fe23e531eded6bd152106132413cf343c21f4119c9006ebfb1a52925',
    'uicr': '07b43b807e558f63b020e68b0a6b78b2d03cb21aae52e23a2f47235467aa3b01',
}


def no_links(path: Path) -> Path:
    # absolute() retains lexical '..'. Never resolve() it away: that could
    # follow a symlink/junction before the NoFollow checks below.
    if '..' in path.parts:
        raise ValueError(f'Parent traversal refused: {path}')
    path = path.absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink() or (parent.exists() and
                getattr(parent.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError(f'Linked path refused: {parent}')
    return path


def evidence(path: Path) -> dict:
    no_links(path)
    if not path.is_file():
        raise ValueError(f'Missing evidence: {path}')
    return {'relativePath': path.name, 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def tree_evidence(directory: Path) -> dict:
    pending, files = [no_links(directory)], {}
    while pending:
        for entry in sorted(pending.pop().iterdir()):
            no_links(entry)  # Check before descending; never follow a junction.
            if entry.is_dir():
                pending.append(entry)
            elif entry.is_file():
                name = str(entry.relative_to(directory))
                row = evidence(entry)
                row['relativePath'] = name
                files[name] = row
            else:
                raise ValueError('Non-regular backup evidence')
    return files


def resolve_history(path: Path, canonical: Path) -> Path:
    path, canonical = no_links(path), no_links(canonical)
    if path.exists():
        return path
    maps = no_links(canonical / '.local/backup-archive/nrf52832/path-maps')
    matches = []
    if maps.exists():
        for mapping in sorted(maps.glob('*.json')):
            no_links(mapping)
            data = json.loads(mapping.read_text(encoding='utf-8-sig'))
            if data.get('schema') != 1:
                raise ValueError('Unknown historical path-map schema')
            for session in data['sessions']:
                old = no_links(Path(session['source']))
                if path != old and old not in path.parents:
                    continue
                destination = no_links(Path(session['destination']))
                archive = canonical / '.local/backup-archive/nrf52832'
                if archive not in destination.parents:
                    raise ValueError('Mapped destination escapes nRF archive')
                expected = {f['relativePath']: f for f in session['files']}
                actual = tree_evidence(destination)
                if actual.keys() != expected.keys():
                    raise ValueError('Mapped capture file set changed')
                for name, row in actual.items():
                    if any(row[k] != expected[name][k] for k in ('bytes', 'sha256')):
                        raise ValueError(f'Mapped capture byte mismatch: {name}')
                candidate = no_links(destination / path.relative_to(old))
                if candidate != destination and destination not in candidate.parents:
                    raise ValueError('Resolved path escapes the verified capture session')
                matches.append(candidate)
    if len(matches) != 1 or not matches[0].exists():
        raise ValueError('Historical capture missing or mapping ambiguous')
    return matches[0]


def inspect(directory: Path) -> dict:
    directory = no_links(directory)
    paths = {}
    for part, legacy in (('flash', 'stock-flash.bin'), ('ficr', 'ficr.bin'), ('uicr', 'uicr.bin')):
        path = directory / f'nrf52832-priceetag-{part}.bin'
        paths[part] = path if path.exists() else directory / legacy
    files = {part: evidence(path) for part, path in paths.items()}
    if [files[p]['bytes'] for p in ('flash', 'ficr', 'uicr')] != [524288, 4096, 4096]:
        raise ValueError('Incomplete Flash/FICR/UICR read sizes')
    flash, ficr, uicr = (paths[p].read_bytes() for p in ('flash', 'ficr', 'uicr'))
    u32 = lambda data, offset: struct.unpack_from('<I', data, offset)[0]
    if (u32(ficr, 0x100) != 0x52832 or u32(ficr, 0x10) != 4096 or
            u32(ficr, 0x14) != 128 or u32(ficr, 0x110) != 512 or u32(ficr, 0x10c) != 64):
        raise ValueError('Not the qualified nRF52832 xxAA identity/capacity')
    uid = f'{u32(ficr, 0x64):08X}{u32(ficr, 0x60):08X}'
    info_path = directory / 'backup-info.txt'
    metadata_verified = False
    info = {}
    if info_path.exists():
        no_links(info_path)
        info = dict(line.split('=', 1) for line in info_path.read_text(encoding='utf-8-sig').splitlines() if '=' in line)
        if (info.get('device_id', '').upper() != uid or
                int(info.get('flash_size_bytes', '0')) != 524288 or
                any(info.get(p + '_sha256', '').lower() != files[p]['sha256'] for p in files)):
            raise ValueError('Capture identity/hashes do not match original metadata')
        metadata_verified = True
    factory = all(files[p]['sha256'] == STOCK_HASHES[p] for p in files)
    if not factory and not metadata_verified:
        raise ValueError('Non-factory capture lacks original identity/hash metadata')
    # Classification remains descriptive, never a physical/write qualification.
    tail_layout = struct.unpack_from('<II', flash, 0xFF8) == (0x58000, 0x5E000)
    dfu, settings, limit = (0x5F000, 0x56000, 163840) if tail_layout else (0x7F000, 0x76000, 131072)
    size = u32(flash, dfu + 24)
    app_ok = (8 <= size <= limit and u32(flash, dfu + 32) == 1 and
              zlib.crc32(flash[0x26000:0x26000 + size]) & 0xffffffff == u32(flash, dfu + 28))
    journal_ok = False
    if not factory and app_ok:
        # Reuse the reviewed decoder rather than adding another Settings ABI.
        full = runpy.run_path(str(ROOT / 'tools/sdk/nordic-full-settings.py'))
        try:
            _, records = full['journal_decode'](flash[settings:settings+8192])
            journal_ok = records.get(1) == full['FACTORY']['BOARDS']['nrf52832_priceetag']['epd']
        except ValueError:
            pass
    current = not factory and app_ok and journal_ok and b'InkTag' in flash
    contents = tree_evidence(directory)
    contents.pop('backup-provenance.json', None)  # Manifest never hashes itself.
    return {'schema': 1, 'actualChip': 'nrf52832', 'board': info.get('board', ''),
            'boardNote': info.get('board_note', ''),
            'boardEvidence': 'operator label only; no PCB qualification',
            'registeredFactoryBoard': 'nrf52832_priceetag' if factory else None,
            'deviceId': uid, 'verifiedCapacity': 524288, 'readRange': [0, 524288],
            'metadataVerified': metadata_verified, 'factory': factory,
            'provenance': 'reviewed exact factory Flash/FICR/UICR baseline' if factory else 'original capture metadata',
            'category': 'factory' if factory else 'inktag-current' if current else 'quarantine-source-unconfirmed',
            'files': files, 'captureFiles': contents}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inspect', 'require-stock', 'resolve'))
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--canonical', type=Path)
    args = parser.parse_args()
    directory = resolve_history(args.directory, args.canonical) if args.canonical else no_links(args.directory)
    if args.action == 'resolve':
        print(directory)
        return
    result = inspect(directory if directory.is_dir() else directory.parent)
    if args.action == 'require-stock' and not result['factory']:
        raise ValueError('Not a reviewed original factory backup; current/unknown captures cannot satisfy StockBackup')
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit(str(error))
