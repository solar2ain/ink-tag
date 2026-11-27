#!/usr/bin/env python3
"""Package a verified native build into a target-independent DAV1 Full template.

No device, SDK, signer or private key is used. Builds create an IKU1 App
and CRC-only Recovery; legacy publisher verification is not part of this tool.
This source packager does not change the separate programmer admission policy.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import tempfile
import zlib

HERE = Path(__file__).resolve().parent
OTA = runpy.run_path(str(HERE / 'package_ota.py'))
NATIVE = runpy.run_path(str(HERE / 'native_images.py'))
STATE = runpy.run_path(str(HERE.parent / 'hardware/da14585-full-state.py'))
POLICY = runpy.run_path(str(HERE / 'ota_policy.py'))
INPUTS = runpy.run_path(str(HERE / 'build_inputs.py'))
STEM = 'InkTag-da14585_aesl'
DEFAULT_PINS = bytes.fromhex('0100000020012107101123ffffffffff')
DEFAULT_SCREEN = bytes.fromhex('0120000201800028010000000080002801')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def reference(path, root):
    data = path.read_bytes()
    return dict(path=path.relative_to(root).as_posix(), bytes=len(data), sha256=digest(data))


def read_reference(root, entry):
    relative = Path(entry['path'])
    if relative.is_absolute() or '..' in relative.parts or '\\' in entry['path'] or ':' in entry['path']:
        raise ValueError('Native proof path escape')
    path = root / relative
    INPUTS['plain'](path)
    data = path.read_bytes()
    if len(data) != entry['bytes'] or digest(data) != entry['sha256']:
        raise ValueError('Native proof input size/hash mismatch: ' + entry['path'])
    return path, data


def journal_record(kind, payload):
    checksum = STATE['fnv1a'](struct.pack('<HH', kind, len(payload)) + payload)
    return (struct.pack('<IHHI', 0x31525349, kind, len(payload), checksum) + payload +
            b'\xff' * (-len(payload) % 4) + struct.pack('<I', 0x31435349))


def recovery_image(raw, version):
    if not 8 <= len(raw) <= 32704:
        raise ValueError('Recovery exceeds native 32 KiB region')
    header = bytearray(b'\xff' * 64)
    header[:4] = b'pQ\xaa\x00'
    struct.pack_into('<II', header, 4, len(raw), zlib.crc32(raw))
    encoded = version.encode('ascii')
    if len(encoded) >= 16:
        raise ValueError('Recovery version exceeds native field')
    header[12:12 + len(encoded) + 1] = encoded + b'\0'
    struct.pack_into('<I', header, 28, 0)
    header[32:34] = b'\0\0'
    return bytes(header) + raw


def verify_source_full(data, app, recovery, loader, assets, policy, epoch):
    """Check exact component pairing and native initialization, not a writer gate."""
    if policy not in POLICY['POLICY_CODES']:
        raise ValueError('Unknown Recovery policy')
    result = NATIVE['verify_full_native_executables'](data)
    expected_app = bytearray(app)
    expected_app[3] = 1
    if (data[0x4000:0x4000 + len(app)] != expected_app or
            data[0x1c000:0x1c000 + len(recovery)] != recovery or
            data[8:8 + len(loader)] != loader or
            data[0x36000:0x37000] + data[0x3e000:0x40000] != assets):
        raise ValueError('Full component pairing mismatch')
    if data[0x24000:0x35000] != b'\xff' * 0x11000:
        raise ValueError('Full image store is not erased')
    if (data[0x39000:0x3b000] != factory_placeholder() or
            data[0x38002:0x38004] != b'\xff\xff' or
            data[0x3800c:0x39000] != b'\xff' * (0x1000 - 12)):
        raise ValueError('Full factory/product template changed')
    result['recoveryInitialization'] = STATE['verify_recovery_pages'](data, epoch)
    result['defaultInitialization'] = STATE['verify_settings'](data)
    result['assetInitialization'] = STATE['verify_assets_and_first_display'](data)
    return result


def factory_placeholder():
    """Structural seed only; guarded Full must retain real target factory bytes."""
    value = bytearray(b'\xff' * 0x2000)
    value[0] = 2
    struct.pack_into('<HH', value, 0x100a, 128, 296)
    return value


def assemble(app, recovery, loader, assets, epoch):
    output = bytearray(b'\xff' * 0x40000)
    boot = b'pP\0\0\0' + len(loader).to_bytes(3, 'big') + loader
    if len(boot) > 0x4000 or len(app) > 0x17640 or len(recovery) > 0x8000 or len(assets) != 12288:
        raise ValueError('Native component bounds')
    output[:len(boot)] = boot
    application = bytearray(app)
    application[3] = 1
    output[0x4000:0x4000 + len(app)] = application
    output[0x1c000:0x1c000 + len(recovery)] = recovery
    output[0x35000:0x35008] = struct.pack('<II', 0x31444649, 0xcebbb9b6)
    output[0x36000:0x37000] = assets[:4096]
    output[0x3e000:] = assets[4096:]
    output[0x38000:0x38002] = b'pR'
    struct.pack_into('<II', output, 0x38004, 0x4000, 0x1c000)
    output[0x39000:0x3b000] = factory_placeholder()
    page_a, page_b = STATE['recovery_pages'](epoch)
    output[0x37000:0x38000] = page_a
    output[0x3b000:0x3c000] = page_b
    struct.pack_into('<IIII', output, 0x3c000, 0x31505349, 1, 0xfffffffe, 0xffffffff)
    records = b''
    if STATE['pin_defaults_capable'](app):
        STATE['require_pin_support'](app, DEFAULT_PINS)
        records += journal_record(1, DEFAULT_PINS)
    screen = b'SP\x01\x11' + DEFAULT_SCREEN + b'\0' * 3
    screen += struct.pack('<I', STATE['fnv1a'](screen))
    records += journal_record(3, screen)
    records += journal_record(7, bytes((2, 1, 1, 0, 0, 0, 0, 0, 0)))
    output[0x3c010:0x3c010 + len(records)] = records
    return bytes(output)


def package(proof_path, version_path):
    proof_path, version_path = Path(proof_path).resolve(), Path(version_path).resolve()
    root = proof_path.parent
    for path in (proof_path, version_path):
        INPUTS['plain'](path)
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    if proof['status'] != 'passed' or proof['otaSignaturePolicy'] not in POLICY['POLICY_CODES']:
        raise ValueError('Only a passed native build with explicit policy can be packaged')
    policy = proof['otaSignaturePolicy']
    captured = json.loads((root / 'inputs-before.json').read_text(encoding='utf-8'))
    expected_version = captured['sourceInputs']['version.json']
    version_data = version_path.read_bytes()
    if len(version_data) != expected_version['bytes'] or digest(version_data) != expected_version['sha256']:
        raise ValueError('Version input differs from native compiler capture')
    version = json.loads(version_data)
    semver = version['version']
    expected = '.'.join(str(version[k]) for k in ('major', 'minor', 'patch'))
    if version.get('revision'):
        expected += f".{version['revision']}"
    if semver != expected:
        raise ValueError('Version components differ')
    epoch = version['recoveryEpoch']
    raw = {}
    for name in ('application', 'recovery', 'bootloader', 'gui'):
        _, raw[name] = read_reference(root, proof['evidence'][name]['raw'])
    project, _ = read_reference(root, proof['evidence']['recovery']['project'])
    POLICY['assert_recovery_policy'](project, policy)
    advert = bytes.fromhex('0d494b0101535001')
    code = POLICY['POLICY_CODES'][policy]
    expected = advert + bytes([code])
    if raw['recovery'].count(expected) != 1 or any(
            advert + bytes([other]) in raw['recovery'] for other in (0, 1, 2) if other != code):
        raise ValueError('Recovery binary policy does not match compiler proof')
    recovery_version = version['recoveryVersion']
    if recovery_version.encode() + b'\0' not in raw['recovery']:
        raise ValueError('CRC-only Recovery actual software version is missing or mismatched')
    app = OTA['package'](raw['application'], semver + '.0' if semver.count('.') == 2 else semver, epoch)
    recovery = recovery_image(raw['recovery'], recovery_version + '.0')
    full = assemble(app, recovery, raw['bootloader'], raw['gui'], epoch)
    checks = verify_source_full(full, app, recovery, raw['bootloader'], raw['gui'], policy, epoch)
    output = root / 'packages'
    INPUTS['plain'](output)
    if output.exists():
        raise ValueError('Immutable package directory already exists')
    # Validate every component before staging; atomically expose a complete set.
    with tempfile.TemporaryDirectory(prefix='.packages-', dir=root) as staging:
        stage = Path(staging) / 'packages'
        stage.mkdir()
        for name, data in ((STEM + '-ota.img', app), (STEM + '-recovery.img', recovery),
                           (STEM + '-full.bin', full)):
            (stage / name).write_bytes(data)
        manifest = dict(schemaVersion=1, format='DAV1-source-full-template',
            otaSignaturePolicy=policy, appPublisherVerified=False,
            recoverySoftwareVersion=recovery_version, recoveryPolicyCode=code,
            recoveryPublisherVerified=False, targetIdentityIncluded=False,
            physicalWriterQualified=False,
            nativeProof=dict(bytes=proof_path.stat().st_size,
                             sha256=digest(proof_path.read_bytes())),
            compatibilityEpoch=epoch, nativeChecks=checks,
            files=[reference(p, stage) for p in sorted(stage.iterdir())])
        (stage / 'package-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        stage.rename(output)
    return dict(output=str(output), **manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--version-json', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = package(args.proof, args.version_json)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, str(error) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
