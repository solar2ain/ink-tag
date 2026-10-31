#!/usr/bin/env python3
"""Offline backup placement and SHA-checked historical-path resolution.

No saved dump is admitted as factory firmware by this helper. Positive InkTag
markers select the current-state archive; all other captures remain unknown.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import struct
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_root(repo):
    repo = repo.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
    command = ['git', '-C', str(repo)]
    pointer = repo / '.git'
    if not pointer.exists():
        # A standalone export must not adopt an unrelated enclosing checkout's
        # backup index simply because Git can discover an ancestor repository.
        return repo
    if os.name != 'nt' and pointer.is_file():
        gitdir = pointer.read_text().strip().removeprefix('gitdir: ')
        if re.match(r'^[A-Za-z]:[\\/]', gitdir):
            gitdir = subprocess.check_output(['wslpath', '-u', gitdir], text=True, timeout=10).strip()
            command = ['git', '--git-dir=' + gitdir, '--work-tree=' + str(repo)]
    try:
        result = subprocess.run([*command, 'rev-parse', '--path-format=absolute',
                                 '--git-common-dir'], env=env, capture_output=True,
                                text=True, timeout=10, check=True)
        common = Path(result.stdout.strip()).resolve()
        if common.name == '.git':
            return common.parent
    except (OSError, subprocess.SubprocessError):
        if pointer.exists():
            raise ValueError('Cannot resolve the primary Git checkout for backup placement')
    # A downloaded tool bundle without Git is rooted at its own install folder.
    return repo


def label(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', value) or value.endswith('.'):
        raise ValueError('Invalid board/note directory label')
    if value.split('.')[0].upper() in {'CON', 'PRN', 'AUX', 'NUL',
                                     *(f'COM{i}' for i in range(1, 10)),
                                     *(f'LPT{i}' for i in range(1, 10))}:
        raise ValueError('Reserved directory label')
    return value


def assert_no_links(path):
    for item in (path, *path.parents):
        if item.is_symlink() or (hasattr(os.path, 'isjunction') and os.path.isjunction(item)):
            raise ValueError(f'Backup path contains a link: {item}')


def classify(directory):
    ficr = (directory / 'aesl22-nrf-ficr.bin').read_bytes()
    flash = (directory / 'aesl22-nrf-flash.bin').read_bytes()
    if len(ficr) != 4096 or len(flash) != 196608:
        raise ValueError('Incomplete internal Flash/FICR capture')
    if struct.unpack_from('<I', ficr, 0x100)[0] != 0x52811 or struct.unpack_from('<II', ficr, 0x10c) != (24, 192):
        raise ValueError('FICR does not identify nRF52811 / 24KiB RAM / 192KiB Flash')
    return 'inktag-current' if any(marker in flash for marker in (b'InkTag', b'INKTAG', b'IKT_')) else 'unknown'


def route(directory, archive_root, pcb, note=''):
    directory, archive_root = directory.absolute(), archive_root.absolute()
    assert_no_links(directory)
    assert_no_links(archive_root)
    directory, archive_root = directory.resolve(), archive_root.resolve()
    if not directory.is_relative_to(archive_root / 'nrf52811' / 'failed'):
        raise ValueError('Source must be this archive root\'s pending capture session')
    pcb = label(pcb)
    if note:
        label(note)
    if 'backups' in (part.lower() for part in archive_root.parts):
        raise ValueError('The backups directory is stock-only; use a diagnostic archive root')
    kind = classify(directory)
    validator = runpy.run_path(str(Path(__file__).with_name('validate-backup.py')))
    saved_manifest = json.loads((directory / 'backup-manifest.json').read_text(encoding='utf-8-sig'))
    if validator['inspect'](directory, saved_manifest['identity']['jlinkSerial'], require_double_read=True) != saved_manifest:
        raise ValueError('Backup manifest does not match the captured files')
    target = archive_root / 'nrf52811' / kind / directory.name
    if target.exists():
        raise FileExistsError(f'Refusing to overwrite backup session: {target}')
    files = {}
    for path in directory.rglob('*'):
        assert_no_links(path)
        if path.is_file():
            files[path.relative_to(directory).as_posix()] = {'bytes': path.stat().st_size, 'sha256': digest(path)}
    record = {'schema': 1, 'chip': 'nrf52811', 'pcb': pcb, 'boardNote': note,
              'classification': kind, 'factoryProvenance': 'not-qualified',
              'readRanges': {'internalFlash': [0, 196608], 'FICR': [0x10000000, 4096],
                             'UICR': [0x10001000, 4096]},
              'externalFlashIncluded': False, 'source': str(directory), 'target': str(target), 'files': files}
    manifest = directory / 'archive-location.json'
    with manifest.open('x', encoding='utf-8') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    files['archive-location.json'] = {'bytes': manifest.stat().st_size, 'sha256': digest(manifest)}
    target.parent.mkdir(parents=True, exist_ok=True)
    assert_no_links(target.parent)
    # Move once. Failure preserves the source/partial destination; never replay.
    shutil.move(str(directory), str(target))
    for name, saved in files.items():
        path = target / name
        if path.stat().st_size != saved['bytes'] or digest(path) != saved['sha256']:
            raise ValueError(f'Moved backup hash mismatch: {path}')
    return {'directory': str(target), 'classification': kind,
            'identityPath': str(target / 'expected-identity.json')}


def resolve_path(path, repo):
    path = path.absolute()
    if path.exists():
        return path
    root = canonical_root(repo)
    index = root / '.local/backup-archive/path-map.json'
    if not index.is_file():
        raise FileNotFoundError(path)
    entries = json.loads(index.read_text(encoding='utf-8-sig'))['entries']
    for entry in entries:
        for source in (entry['source'], *entry.get('aliases', [])):
            try:
                relative = path.relative_to(Path(source))
            except ValueError:
                continue
            target = Path(entry['target']).absolute()
            assert_no_links(target)
            target = target.resolve()
            if not (target.is_relative_to(root / '.local/backup-archive') or target.is_relative_to(root / '.local/backups')):
                raise ValueError('Historical mapping target is outside the canonical archive')
            for saved in entry['files']:
                file = target / saved['relative']
                if not file.resolve().is_relative_to(target):
                    raise ValueError('Historical file is outside its backup session')
                assert_no_links(file)
                if file.stat().st_size != saved['bytes'] or digest(file) != saved['sha256']:
                    raise ValueError(f'Historical mapping hash mismatch: {file}')
            result = target / relative
            if not result.resolve().is_relative_to(target):
                raise ValueError('Historical path escapes its backup session')
            if not result.exists():
                raise FileNotFoundError(result)
            return result
    raise FileNotFoundError(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--canonical-root', action='store_true')
    parser.add_argument('--resolve-path', type=Path)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--archive-root', type=Path)
    parser.add_argument('--pcb', default='UNIDENTIFIED-PCB')
    parser.add_argument('--board-note', default='')
    args = parser.parse_args()
    if args.canonical_root:
        print(canonical_root(args.repo))
    elif args.resolve_path:
        print(resolve_path(args.resolve_path, args.repo))
    elif args.directory and args.archive_root:
        print(json.dumps(route(args.directory, args.archive_root, args.pcb, args.board_note)))
    else:
        parser.error('Choose canonical-root, resolve-path, or directory and archive-root')
