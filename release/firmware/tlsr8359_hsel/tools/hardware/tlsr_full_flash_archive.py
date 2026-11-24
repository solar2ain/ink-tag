"""Validate curated factory rescue archives for full-flash. Offline only."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from tlsr_full_flash_base import sha
from tlsr_full_flash_config import PCB_BACKENDS, ROOT

# Frozen prior complete double-read baselines, moved without inventing new
# reads/capacity evidence. Other historical/single/partial captures cannot pass.
HISTORICAL = {
    'hs_nowa154': 'E70D15BF930087FCA297B9EB6E5031BF84908D7672012A7B01B81C3932A5CA04',
    'hs_nowa213': 'E11F55F32B4ECDF1798C80B78A22B1A52AF0DC8000B711C94B640AE66A703DB2',
    'hs_nebular213': '0B7C7491E5078851A4657F6E2EB95CFC3639A433D29D3AF2AB9F7D6EA347B3E3',
    'hs_nebular420': '86271B4DA80A1C30E9D3B6E82ADEEF081BF0CD18580D74CAE9C8BABAA2DC3DB3',
}
# Independently matched1MiB SSD1683 capture, retained in the factory archive.
# This expands only this exact PCB's reviewed content, not arbitrary hashes.
HISTORICAL_REVISIONS = {
    'hs_nebular420': {
        HISTORICAL['hs_nebular420'],
        'D5E6BDB5DF688AB02F28D4B4594C94C898A60B9110C8BADC78F8FD38BD31BA88',
    },
}


def validate(directory: Path, board: str, *, expected_mac: str = '') -> dict:
    directory = directory.resolve(strict=True)
    entry = next(item for item in PCB_BACKENDS.values() if item[0] == board)
    pcb = entry[2]
    metadata_path = directory / 'backup.json'
    if metadata_path.is_symlink() or metadata_path.stat().st_size > 131072:
        raise ValueError('Factory metadata must be a bounded regular file.')
    metadata = json.loads(metadata_path.read_text(encoding='utf-8-sig'))
    # Owning-tool archive namespace; this does not establish the die identity.
    chip = 'tlsr8359'
    if metadata.get('board') != pcb or metadata.get('chip') != chip:
        raise ValueError('Factory archive PCB/owning-tool namespace mismatch.')
    if 'originalMetadataFile' in metadata or 'originalMetadataSha256' in metadata:
        if metadata.get('originalMetadataFile') != 'backup.original.json':
            raise ValueError('Migration must retain its fixed original metadata member.')
        original_path = directory / 'backup.original.json'
        if original_path.is_symlink() or original_path.stat().st_size > 131072:
            raise ValueError('Unsafe original migration metadata.')
        original_bytes = original_path.read_bytes()
        if sha(original_bytes) != str(metadata.get('originalMetadataSha256', '')).upper():
            raise ValueError('Original migration metadata hash changed.')
        original = json.loads(original_bytes.decode('utf-8-sig'))
        if original.get('chip') != 'hs9118' or any(
                metadata.get(key) != value for key, value in original.items() if key not in ('chip', 'directory')):
            raise ValueError('Migration changed original evidence or qualification.')
    registered = json.loads((ROOT / 'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards'][board]
    contract = registered.get('firstFlash', {})
    if contract.get('imageSizeBytes') != 524288:
        raise ValueError('Exact board has no admitted Config Full write window.')

    def member(name, size, digest):
        if not isinstance(name, str) or Path(name).name != name or '/' in name or '\\' in name:
            raise ValueError('Factory member must be a fixed local filename.')
        path = directory / name
        if path.is_symlink() or path.resolve(strict=True).parent != directory:
            raise ValueError('Factory member escapes archive.')
        data = path.read_bytes()
        if len(data) != size or sha(data) != digest.upper():
            raise ValueError('Factory member size/SHA256 mismatch: ' + name)
        return path, data

    schema = metadata.get('schema')
    if schema == 'inktag-stock-backup-v1':
        if (metadata.get('classification') != 'stock' or metadata.get('originalQualified') is not True
                or metadata.get('verification') != 'matched-double-read'):
            raise ValueError('Factory archive lacks complete independent matching reads.')
        pairs = [item for item in metadata.get('readPairs', []) if item.get('role') == 'flash']
        if len(pairs) != 1:
            raise ValueError('One exact Flash read pair required.')
        pair = pairs[0]
        size, digest = pair['bytes'], pair['sha256']
        if type(size) is not int or size not in (524288, 1048576) or not re.fullmatch('[0-9a-fA-F]{64}', digest):
            raise ValueError('Unqualified archive range/hash.')
        if {'address': 0, 'bytes': size} not in [
                {'address': x.get('address'), 'bytes': x.get('bytes')} for x in metadata.get('ranges', [])]:
            raise ValueError('Factory archive does not start at zero.')
        if len({pair['first'], pair['second'], pair['canonical']}) != 3:
            raise ValueError('Independent read members must be distinct.')
        _, first = member(pair['first'], size, digest)
        verify, second = member(pair['second'], size, digest)
        path, data = member(pair['canonical'], size, digest)
        if first != second or data != first:
            raise ValueError('Independent factory read mismatch.')
    elif schema == 'inktag-historical-backup-v1':
        allowed = HISTORICAL_REVISIONS.get(board, {HISTORICAL.get(board)}) - {None}
        if (not allowed or metadata.get('classification') != 'historical-factory-content-baseline'
                or metadata.get('verification') != 'prior-complete-matched-read-record; no new hardware double-read'):
            raise ValueError('Historical archive is not a frozen matched factory baseline.')
        first_entry = next(x for x in metadata.get('files', []) if x['relative'] == 'tlsr8359-flash.bin')
        digest = first_entry.get('sha256', '').upper()
        if digest not in allowed:
            raise ValueError('Historical archive hash is not reviewed for this PCB.')
        second_entry = next(x for x in metadata.get('files', [])
                            if x['relative'] == 'tlsr8359-flash-verify.bin' and x.get('sha256', '').upper() == digest)
        size = first_entry['bytes']
        if size not in (524288, 1048576) or second_entry['bytes'] != size:
            raise ValueError('Historical factory range mismatch.')
        path, data = member(first_entry['relative'], size, digest)
        verify, second = member(second_entry['relative'], size, digest)
        if data != second:
            raise ValueError('Historical factory reads mismatch.')
    else:
        raise ValueError('Unknown/non-factory archive schema; no live-state capture admitted.')
    capacity = registered.get('physicalFlashSizeBytes') or registered.get('verifiedFlashWindowSizeBytes') or 0
    if size > capacity:
        raise ValueError('Factory capture exceeds the board-qualified physical/window extent.')
    archive_jedec = metadata.get('identity', {}).get('flashJedec')
    if archive_jedec and archive_jedec.upper() != contract.get('flashJedecId', '').upper():
        raise ValueError('Factory JEDEC differs from exact registered board.')
    if expected_mac:
        if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', expected_mac):
            raise ValueError('Exact MAC syntax required.')
        stored = data[0x76000:0x76006]
        if stored != b'\xff' * 6 and stored != bytes.fromhex(expected_mac.replace(':', ''))[::-1]:
            raise ValueError('Factory archive MAC differs from exact target.')
    return dict(Directory=str(directory), InfoPath=str(metadata_path), FlashPath=str(path),
                VerifyPath=str(verify), FlashSha256=sha(data), Format=schema,
                Info={'verification': metadata['verification'], 'board_id': board},
                current_device_qualified=False)


def find(roots: list[Path], board: str, expected_mac: str, board_note: str = '') -> dict:
    entry = next(item for item in PCB_BACKENDS.values() if item[0] == board)
    chip = 'tlsr8359'
    candidates = []
    for root in roots:
        for metadata in sorted((root / 'factory_backups' / chip / entry[2]).glob('*/backup.json')):
            try:
                result = validate(metadata.parent, board, expected_mac=expected_mac)
                if board_note and json.loads(metadata.read_text(encoding='utf-8-sig')).get('boardNote') != board_note:
                    continue
                candidates.append(result)
            except (ValueError, OSError, KeyError, StopIteration):
                continue
    # Copies of the same frozen content do not create a new identity choice.
    unique = {item['FlashSha256']: item for item in candidates}
    if len(unique) != 1:
        raise ValueError('No unique verified factory rescue archive for this unit. Specify -StockBackup <factory_backups/.../timestamp> explicitly.')
    return next(iter(unique.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', required=True)
    parser.add_argument('--expected-mac', default='')
    parser.add_argument('--board-note', default='')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--root', type=Path, action='append', default=[])
    args = parser.parse_args()
    try:
        result = validate(args.archive, args.board, expected_mac=args.expected_mac) if args.archive else find(args.root, args.board, args.expected_mac, args.board_note)
        if args.board_note and json.loads(Path(result['InfoPath']).read_text(encoding='utf-8-sig')).get('boardNote')!=args.board_note:
            raise ValueError('Explicit archive manufacturing note differs from Config filename.')
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, StopIteration) as exc:
        parser.error(str(exc) or 'Missing complete factory pair.')


if __name__ == '__main__':
    main()
