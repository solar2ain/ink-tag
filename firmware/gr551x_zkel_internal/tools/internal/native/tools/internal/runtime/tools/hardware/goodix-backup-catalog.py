#!/usr/bin/env python3
"""Host-only Goodix backup provenance and archive routing (no device access).

Hash equality/size are not factory provenance. Only reviewed exact-board stock
content may satisfy the historical rescue gate; unknown/new captures quarantine.
This does not qualify the pending Chip/Config public-interface pilot.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

# Source-owned reviewed factory content; no user records/private keys here.
# GR5331's historical 1MiB debugger window mirrors its physical 512KiB.
STOCK = {
    '51089bb71bec5a339fe68613741745f780e65237c1baf721874622dd5175db36':
        ('gr5331', 'gr5331_zkc21v', 1048576, 524288, 0x00200000),
    '9e37f20d191208d0bb34779577a5925633c54a0c7db30fb4a02124e011104831':
        ('gr5331', 'gr5331_zkc21v', 524288, 524288, 0x00200000),
    'da822acb7a04e8bcfa9078d75677409483a059cb3688773eb202b1917c118819':
        ('gr5515', 'gr5515_zkc42vm', 1048576, 1048576, 0x01000000),
    'bd51f9f0306279a7e7cd2703d01a460fba9002ce187008871e2e329cf4fe7cc1':
        ('gr5513', 'gr5513_zkc42v', 524288, 524288, 0x01000000),
}


def board_records() -> dict:
    return json.loads((Path(__file__).with_name('goodix-backup-boards.json')).read_text(encoding='utf-8'))['boards']


def board_matches(requested: str, logical: str) -> bool:
    # A physical PCB label is archival metadata, never a firmware selector.
    # Explicit logical 42vm must NOT match the 42v stock image through shared PCB.
    return not requested or requested.casefold() in {
        logical.casefold(), board_records()[logical]['pcb'].casefold()}


def reject_links(path: Path) -> Path:
    path = path.absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or (os.path.lexists(part) and getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError(f'Refusing symlink/junction: {part}')
    return path


def _git_metadata_line(path: Path) -> str:
    """Read one bounded administration pointer, never execute a Git command."""
    path = reject_links(path)
    with path.open('rb') as stream:
        raw = stream.read(8193)
    if len(raw) > 8192 or b'\0' in raw:
        raise ValueError('Invalid Git metadata; refusing installation fallback')
    line = raw.decode('utf-8-sig').strip()
    if not line or '\n' in line or '\r' in line:
        raise ValueError('Ambiguous Git metadata; refusing installation fallback')
    return line


def _git_metadata_path(base: Path, value: str) -> Path:
    target = Path(value)
    return reject_links(target if target.is_absolute() else base / target).resolve(strict=True)


def _primary_git_directory(common: Path) -> Path:
    if common.name != '.git' or not common.is_dir():
        raise ValueError('Invalid Git common directory; refusing installation fallback')
    for name in ('HEAD', 'config'):
        if not reject_links(common / name).is_file():
            raise ValueError('Incomplete Git common directory; refusing installation fallback')
    return reject_links(common.parent).resolve(strict=True)


def primary_root(repo: Path) -> Path:
    repo = reject_links(repo).resolve(strict=True)
    if not repo.is_dir():
        raise ValueError('Explicit tool installation root must be a directory')
    pointer = reject_links(repo / '.git')
    # A downloaded package is rooted at the explicitly supplied installation,
    # even if an ancestor happens to be a Git checkout. It needs no Git binary.
    if not os.path.lexists(pointer):
        return repo
    if not (pointer.is_file() or pointer.is_dir()):
        raise ValueError('Invalid Git checkout marker; refusing installation fallback')
    # Match the shared PowerShell archive resolver: installed packages and
    # source/worktree tools must not require Git to be installed on the host.
    # Read this explicit tree's metadata, not cwd or any GIT_* environment.
    # A malformed marker still fails closed instead of becoming an install.
    try:
        if pointer.is_dir():
            return _primary_git_directory(pointer)
        line = _git_metadata_line(pointer)
        if not line.startswith('gitdir: '):
            raise ValueError('Invalid Git worktree pointer; refusing installation fallback')
        admin = _git_metadata_path(repo, line[len('gitdir: '):])
        if not admin.is_dir() or not reject_links(admin / 'HEAD').is_file():
            raise ValueError('Invalid Git worktree administration; refusing installation fallback')
        common = _git_metadata_path(admin, _git_metadata_line(admin / 'commondir'))
        primary = _primary_git_directory(common)
        if admin.parent != common / 'worktrees':
            raise ValueError('Git administration is outside worktrees; refusing installation fallback')
        backlink = _git_metadata_path(admin, _git_metadata_line(admin / 'gitdir'))
        if backlink != pointer:
            raise ValueError('Git worktree backlink mismatch; refusing installation fallback')
        return primary
    except (OSError, UnicodeError) as exc:
        raise ValueError('Cannot resolve primary Git metadata; refusing installation fallback') from exc


def inspect(path: Path, chip: str, board: str = '') -> dict:
    path = reject_links(path)
    data = path.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    accepted = STOCK.get(sha)
    # Classification may say stock content, but requested actual chip/PCB must
    # match. A family alias/MAC/filename never substitutes for exact identity.
    if accepted and len(data) == accepted[2]:
        actual, pcb, size, physical, start = accepted
        matched = chip.lower() == actual and board_matches(board, pcb)
        return dict(path=str(path), sha256=sha, bytes=size,
                    classification='stock' if matched else 'identity-mismatch',
                    stockQualified=matched, chip=actual, pcb=pcb,
                    physicalBytes=physical, readBase=start,
                    reason='reviewed exact-board factory SHA + recorded identity/range' if matched else 'factory content does not match requested exact chip/PCB')
    if b'InkTag' in data or b'IKT_' in data:
        classification = 'inktag-current'
    elif not data or (len(data) >= 32768 and data[:32768] == data[:4] * 8192):
        classification = 'invalid-capture'
    else:
        classification = 'unconfirmed'
    return dict(path=str(path), sha256=sha, bytes=len(data), classification=classification,
                stockQualified=False, chip=chip or 'UNIDENTIFIED-MCU', pcb=board or 'UNIDENTIFIED-PCB',
                reason='No reviewed factory provenance; size/double-read agreement is insufficient')


def mapped_path(path: Path, primary: Path) -> Path:
    """Exact legacy-path lookup only. Never rewrite/recreate old reports."""
    path = path.absolute()
    if path.exists():
        return reject_links(path)
    index = primary / '.local/backup-archive/_indices'
    if not index.exists():
        return path
    key = os.path.normcase(str(path)).replace('/', '\\').casefold()
    matches = set()
    for folder in reject_links(index).iterdir():
        if not folder.name.startswith('gr-'):
            continue  # other platforms own their maps/writers
        reject_links(folder)
        manifest = folder / 'mapping-v1.json'
        if not manifest.is_file():
            continue
        for row in json.loads(reject_links(manifest).read_text(encoding='utf-8-sig')).get('entries', []):
            aliases = [row['source'], *row.get('legacyAliases', [])]
            if any(os.path.normcase(x).replace('/', '\\').casefold() == key for x in aliases):
                dest = reject_links(Path(row['destination']))
                if not (any(dest.is_relative_to(primary / '.local' / kind) for kind in ('backups', 'backup-archive'))
                        or any(dest.is_relative_to(primary / 'factory_backups' / chip) for chip in ('gr5331','gr5513','gr5515'))):
                    raise ValueError('Mapped destination outside canonical archive roots')
                # Map classification is NOT authority. Content is checked by
                # inspect() against STOCK after resolving the requested path.
                if dest.stat().st_size != row['bytes'] or hashlib.sha256(dest.read_bytes()).hexdigest() != row['sha256']:
                    raise ValueError('Mapped capture content changed')
                matches.add(dest)
    if len(matches) > 1:
        raise ValueError('Ambiguous historical mapping; specify exact new path')
    return next(iter(matches)) if matches else path


def walk_no_links(root: Path):
    reject_links(root)
    for child in sorted(root.iterdir()):
        reject_links(child)
        if child.is_dir():
            yield from walk_no_links(child)
        elif child.is_file():
            yield child


def find_stock(repo: Path, chip: str, board: str, explicit: str = '') -> dict:
    primary = primary_root(repo)
    if explicit:
        result = inspect(mapped_path(Path(explicit), primary), chip, board)
        if not result['stockQualified']:
            raise ValueError(f"Backup rejected: {result['classification']}; not exact-board reviewed factory content")
        return result
    candidates = []
    record = board_records().get(board)
    pcb = record['pcb'] if record else board
    # Canonical factory_backups first, but receipt/path alone is not authority:
    # every payload still passes exact requested logical-board and frozen SHA.
    for root in dict.fromkeys([primary, repo]):
        archive = root / 'factory_backups' / chip / pcb
        if archive.exists():
            for receipt in (p for p in walk_no_links(archive) if p.name == 'backup.json'):
                metadata = json.loads(receipt.read_text(encoding='utf-8-sig'))
                if metadata.get('chip') != chip or metadata.get('board') not in (pcb, board):
                    continue
                if metadata.get('logicalBoard', board) != board:
                    continue
                rows = metadata.get('files', []) or [dict(relative=pair.get('canonical', pair['first']),
                    bytes=pair['bytes'], sha256=pair['sha256']) for pair in metadata.get('readPairs', [])]
                for row in rows:
                    relative = Path(row['relative'])
                    if relative.is_absolute() or '..' in relative.parts:
                        raise ValueError('Invalid factory receipt file path')
                    payload = reject_links(receipt.parent / relative)
                    if payload.stat().st_size != row['bytes'] or hashlib.sha256(payload.read_bytes()).hexdigest() != row['sha256']:
                        raise ValueError('Factory receipt content changed')
                    candidates.append(payload)
    stock_root = primary / '.local/backups' / chip.upper() / board
    if stock_root.exists():
        candidates.extend(p for p in walk_no_links(stock_root) if p.suffix.lower() == '.bin')
    for path in dict.fromkeys(candidates):
        if path.is_file():
            result = inspect(path, chip, board)
            if result['stockQualified']:
                return result
    raise ValueError('No reviewed exact-chip/PCB factory backup found; unknown/current/invalid data never satisfy stock gate')


def route(path: Path, chip: str, board: str, verified_chip: bool) -> dict:
    result = inspect(path, chip, board)
    if not verified_chip:
        result.update(classification='unconfirmed', stockQualified=False,
                      chip='UNIDENTIFIED-MCU', pcb='UNIDENTIFIED-PCB',
                      reason='Capture transport did not establish actual MCU; target label/CPUID is insufficient')
    actual = chip.upper() if verified_chip else 'UNIDENTIFIED-MCU'
    if result['stockQualified']:
        result['relativeDirectory'] = str(Path('backups') / actual / result['pcb'])
    else:
        result['relativeDirectory'] = str(Path('backup-archive') / actual / result['classification'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('primary-root', 'map-path', 'inspect', 'find-stock', 'route'))
    parser.add_argument('--repo-root', type=Path, required=True)
    parser.add_argument('--chip', choices=('gr5331', 'gr5513', 'gr5515'))
    parser.add_argument('--board', default='')
    parser.add_argument('--file', default='')
    parser.add_argument('--verified-chip', action='store_true')
    args = parser.parse_args()
    try:
        if args.action == 'primary-root':
            result = {'primaryRoot': str(primary_root(args.repo_root))}
        elif args.action == 'map-path':
            if not args.file:
                raise ValueError('Historical capture path required')
            path = mapped_path(Path(args.file), primary_root(args.repo_root))
            if not path.is_file():
                raise ValueError('Capture path not found in current filesystem or fixed mapping')
            result = {'path': str(path), 'factoryQualification': 'not-established-by-path-resolution'}
        elif not args.chip:
            raise ValueError('Exact chip required')
        elif args.action == 'find-stock':
            result = find_stock(args.repo_root, args.chip, args.board, args.file)
        elif not args.file:
            raise ValueError('Capture file required')
        elif args.action == 'route':
            result = route(Path(args.file), args.chip, args.board, args.verified_chip)
        else:
            result = inspect(Path(args.file), args.chip, args.board)
        print(json.dumps(result))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError, json.JSONDecodeError, KeyError, TypeError) as exc:
        print(json.dumps({'error': str(exc)}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
