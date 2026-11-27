"""Verify the native BASIC distribution without an Apple account or a device."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import stat
import sys
import zipfile

from publication_scope import EXECUTABLE_IDENTITIES, executable_target


INVENTORY = 'release/firmware/SHA256SUMS.txt'


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def native_inventory(repo: Path) -> dict[str, str]:
    """Bind every board file, including offline tools and per-board BIN/HEX."""
    inventory = repo / INVENTORY
    folder = inventory.parent
    files = {}
    for line in inventory.read_text('utf-8').splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match:
            raise ValueError('Invalid BASIC firmware inventory entry')
        expected, name = match.groups()
        path = folder / name
        if (Path(name).is_absolute() or '\\' in name or ':' in name or
                '..' in Path(name).parts or name in files or path.is_symlink() or
                not path.resolve().is_relative_to(folder.resolve()) or not path.is_file()):
            raise ValueError(f'Invalid BASIC firmware inventory path: {name}')
        if digest(path.read_bytes()) != expected:
            raise ValueError(f'BASIC firmware inventory bytes changed: {name}')
        files[name] = expected
    actual = {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p != inventory}
    if actual != files.keys():
        raise ValueError('BASIC firmware inventory must cover all distribution files')
    return {'release/firmware/' + name: value for name, value in files.items()}


def verify_firmware_bundle(repo: Path, path: Path) -> None:
    members = native_inventory(repo)
    members[INVENTORY] = digest((repo / INVENTORY).read_bytes())
    expected = {f"{path.stem}/{name.removeprefix('release/firmware/')}": name for name in members}
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(expected) or set(names) != expected.keys():
            raise ValueError('Firmware bundle does not match the release tree')
        for member in archive.infolist():
            source = expected[member.filename]
            if (stat.S_ISLNK(member.external_attr >> 16) or member.file_size != (repo / source).stat().st_size or
                    digest(archive.read(member)) != members[source]):
                raise ValueError(f'Firmware bundle member changed: {member.filename}')


def basic_policy(target: str) -> dict:
    return {'algorithm': 'none', 'packageBoard': EXECUTABLE_IDENTITIES[target][1], 'keyPolicy': 'crc-only'}


def four_part_version(value: str) -> str:
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:\.\d+)?', value):
        raise ValueError('Invalid native App version')
    return value + '.0' if value.count('.') == 2 else value


def nordic_version_from_inventory(repo: Path, target: str, ota: bytes, full: bytes) -> str | None:
    # Nordic DAT encodes epoch and major/minor/patch. The separately audited
    # native build inventory binds the revision to the exact OTA/Full pair.
    manifest = repo / 'release/firmware/manifest.json'
    if not manifest.is_file():
        return None
    data = json.loads(manifest.read_bytes())
    artifacts = data['targets'].get(target, {}).get('artifacts', {})
    for kind, package in (('ota', ota), ('full', full)):
        if not any(entry['bytes'] == len(package) and entry['sha256'] == digest(package)
                   for entry in artifacts.get(kind, [])):
            return None
    return data['appVersion']


def inspect_basic_pair(repo: Path, build_target: str, ota: Path, full: Path) -> dict:
    sys.path.insert(0, str(repo / 'cli/src'))
    try:
        from inktag_cli.ota import inspect_package
        info = inspect_package(ota)
    finally:
        sys.path.pop(0)
    target = executable_target(build_target)
    policy = basic_policy(build_target)
    fields = info.details
    native_layout = fields.get('signedLayout', fields.get('layout'))
    package_version = fields.get('version')
    if info.transport in ('goodix', 'onmicro', 'telink'):
        with zipfile.ZipFile(ota) as package:
            manifest = json.loads(package.read('manifest.json'))
        package_version = manifest.get('release')
        if info.transport == 'telink':
            native_layout = manifest['layout']
    if (info.board != policy['packageBoard'] or fields.get('signatureStatus') != 'unsigned' or
            fields.get('compatibilityEpoch') != target['compatibilityEpoch'] or native_layout != target['layout']):
        raise ValueError(f'BASIC OTA native target, layout or integrity policy differs: {build_target}')

    release = int(fields.get('releaseVersion', info.version))
    if info.transport == 'nordic' and release >> 16 != target['compatibilityEpoch']:
        raise ValueError(f'Nordic release epoch differs: {build_target}')
    version = release & 0xffff if info.transport == 'nordic' else release
    ota_bytes, full_bytes = ota.read_bytes(), full.read_bytes()
    if full.suffix == '.zip':
        with zipfile.ZipFile(full) as package:
            if package.namelist().count('app-ota.zip') != 1 or package.read('app-ota.zip') != ota_bytes:
                raise ValueError(f'Full package embeds a different BASIC OTA: {build_target}')
            if build_target == 'nrf52811_aesl':
                images = json.loads(package.read('base.json'))['images']
                for name in ('full.hex', 'gui-assets.bin'):
                    if package.namelist().count(name) != 1:
                        raise ValueError(f'nRF52811 Full requires a unique {name}')
                    data = package.read(name)
                    if images[name] != {'bytes': len(data), 'sha256': digest(data)}:
                        raise ValueError(f'nRF52811 Full {name} differs from its manifest')
        method = 'embedded-ota'
    elif build_target == 'da14585_aesl' and full.suffix == '.bin':
        app = full_bytes[0x4000:0x4000 + len(ota_bytes)]
        if (len(full_bytes) != 0x40000 or len(app) < 64 or app[3] != 1 or ota_bytes[3] != 255 or
                app[:3] + app[4:] != ota_bytes[:3] + ota_bytes[4:]):
            raise ValueError('DA14585 Full contains a different application')
        method = 'native-image-role'
    else:
        raise ValueError(f'Unsupported BASIC Full container: {build_target}')
    encoded_version = f'{version // 10000}.{version // 100 % 100}.{version % 100}'
    if info.transport == 'nordic':
        package_version = nordic_version_from_inventory(repo, build_target, ota_bytes, full_bytes)
    app_version = four_part_version(package_version or encoded_version)
    if app_version.rsplit('.', 1)[0] != encoded_version:
        raise ValueError(f'BASIC package release differs from its native version code: {build_target}')
    return {
        'target': target,
        'version': app_version,
        'releaseVersion': release,
        'signaturePolicy': policy,
        'pairing': {'otaSha256': digest(ota_bytes), 'fullSha256': digest(full_bytes), 'method': method},
    }
