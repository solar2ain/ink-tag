"""Offline OM release identity and native payload checks; no signing or I/O to devices."""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


def recovery_identity(root: Path, native: bytes, elf_path: Path) -> dict:
    """Require the generated identity in an allocated, retained ELF object and BIN."""
    version = json.loads((root / 'version.json').read_text(encoding='utf-8'))['recoveryVersion']
    if not isinstance(version, str) or not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Numeric Recovery major.minor.patch is required')
    major, minor, patch = map(int, version.split('.'))
    if major != 1 or any(value > 255 for value in (major, minor, patch)):
        raise ValueError('OM layout-v1 Recovery requires major/epoch 1 and byte-sized components')
    header = (root / 'firmware/common/ota/inktag_recovery_version.h').read_text(encoding='utf-8')
    expected = {'MAJOR': major, 'MINOR': minor, 'PATCH': patch}
    if (any(not re.search(r'^#define INKTAG_RECOVERY_VERSION_' + key + r'\s+' + str(value) + r'u\s*$', header, re.M)
            for key, value in expected.items())
            or f'#define INKTAG_RECOVERY_VERSION_STRING "{version}"' not in header
            or '#define INKTAG_RECOVERY_EPOCH INKTAG_RECOVERY_VERSION_MAJOR' not in header):
        raise ValueError('Generated Recovery version header is stale')
    marker = b'IKRV' + bytes((major, minor, patch, major))
    if native.count(marker) != 1:
        raise ValueError('Native Recovery must contain exactly one current IKRV identity')

    from elftools.elf.elffile import ELFFile
    with elf_path.open('rb') as stream:
        elf = ELFFile(stream)
        table = elf.get_section_by_name('.symtab')
        symbols = table.get_symbol_by_name('inktag_om_recovery_info') if table else None
        if not symbols or len(symbols) != 1:
            raise ValueError('Retained Recovery identity ELF symbol is missing or ambiguous')
        symbol = symbols[0]
        if (symbol['st_size'] != len(marker) or symbol['st_info']['type'] != 'STT_OBJECT'
                or not isinstance(symbol['st_shndx'], int)):
            raise ValueError('Invalid Recovery identity ELF object')
        section = elf.get_section(symbol['st_shndx'])
        if not section['sh_flags'] & 2 or section['sh_type'] == 'SHT_NOBITS':
            raise ValueError('Recovery identity is not in loadable bytes')
        address = int(symbol['st_value'])
        offset = address - 0x404000
        section_offset = address - int(section['sh_addr'])
        if (not 0 <= offset <= len(native) - len(marker)
                or not 0 <= section_offset <= int(section['sh_size']) - len(marker)
                or native[offset:offset + len(marker)] != marker
                or section.data()[section_offset:section_offset + len(marker)] != marker
                or not any(segment['p_type'] == 'PT_LOAD' and segment.section_in_segment(section)
                           for segment in elf.iter_segments())):
            raise ValueError('Recovery identity ELF/BIN load address differs')
    return dict(recoveryVersion=version, compatibilityEpoch=major,
                nativeRecoveryBytes=len(native), nativeRecoverySha256=hashlib.sha256(native).hexdigest(),
                identityBytesHex=marker.hex(), identityXipAddress=address)


def release_path_evidence(native: bytes) -> dict:
    """Reject InkTag source paths, retaining SDK/toolchain diagnostics and protocol domains."""
    paths = []
    for match in re.finditer(rb'[ -~]{6,}', native):
        text = match.group().decode('ascii')
        normalized = text.replace('\\', '/').lower()
        if '/' not in normalized or not re.search(r'\.(?:c|cpp|h)(?:$|[: ])', normalized):
            continue
        paths.append(dict(offset=match.start(), path=text))
        owned = (any(part in normalized for part in ('firmware/common/', 'firmware/ports/',
                    'projects/ble_app_inktag/', 'projects/inktag_om_swap_boot/', 'cli/src/inktag_cli/'))
                 or bool(re.search(r'(?:^|/)inktag_[^/ ]+\.(?:c|cpp|h)(?:$|[: ])', normalized)))
        if owned:
            raise ValueError('InkTag source path in native release payload: ' + text)
        sdk = any(part in normalized for part in ('/src/newlib/', '/newlib/libc/', '/libgcc/',
                  '/om6626.sdk/', '/components/', '/cmsis/', '/hal/'))
        if not sdk:
            raise ValueError('Unclassified source path in native release payload: ' + text)
    return dict(inktagSourcePaths=[], sdkOrToolchainPaths=paths)
