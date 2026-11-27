#!/usr/bin/env python3
"""Build and package base GR App/Recovery without keys, Git or a Pro producer.

The vendor SDK and ARM toolchain are external user-supplied dependencies.
This produces unsigned native App OTA and first-install Full packages. Building
does not operate a device or grant authority to replace installed Recovery.
"""
from contextlib import contextmanager
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

from goodix_app_integrity import append, verify as verify_integrity
from goodix_base_elf import verify as verify_code
from goodix_native_package import BOARDS, checksum, image_info, json_bytes, record, write_zip
from goodix_recovery_version import recovery_binary_identity

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {
    'gr5331_zkel': ('gr5331_zkc21v', 'gr5331', 'gr5331_zkc21v'),
    'gr551x_zkel': ('gr551x_zkc42v', 'gr551x', 'gr551x_zkc42vm'),
    'gr551x_zkel_internal': ('gr5513_zkel_internal', 'gr551x', 'gr551x_zkc42vm'),
}
NATIVE_FAMILY_MARKERS = {
    'gr5331_zkc21v': b'gr5331_zkel\0',
    'gr551x_zkc42v': b'gr551x_zkel\0',
    'gr5513_zkel_internal': b'gr5513_zkel_internal\0',
}


def native_path(path):
    value = str(path.resolve())
    if os.name == 'nt':
        if len(value) < 3 or value[1:3] != ':\\':
            raise ValueError('An absolute local-drive SDK/toolchain path is required')
        return '/mnt/'+value[0].lower()+value[2:].replace('\\', '/')
    return value


@contextmanager
def sdk_lease(sdk):
    path = sdk/'.inktag-normal-build.lock'
    token = json_bytes(dict(pid=os.getpid(), nonce=uuid.uuid4().hex, purpose='independent-base-build'))
    interrupted = False
    try:
        with path.open('xb') as stream:
            stream.write(token)
    except FileExistsError as error:
        raise ValueError('SDK is already owned; its lock must not be stolen') from error
    try:
        yield
    except (KeyboardInterrupt, SystemExit):
        interrupted = True
        raise
    finally:
        if not path.is_file() or path.read_bytes() != token:
            raise ValueError('SDK ownership changed; refusing cleanup')
        if not interrupted:
            path.unlink()


def package_native(*, family, app, app_elf, recovery, recovery_elf, version, output):
    if output.exists():
        raise ValueError('Refusing to overwrite output')
    linked = app.read_bytes()
    rec = recovery.read_bytes()
    app_proof = verify_code(app_elf.read_bytes(), 'app', linked, family)
    rec_proof = verify_code(recovery_elf.read_bytes(), 'recovery', rec, family)
    parts = tuple(int(part) for part in version['version'].split('.'))
    if (len(parts) not in (3, 4) or any(part < 0 or part > 255 for part in parts) or
        not 0 <= version['protocolVersion'] <= 255 or
        version['otaVersion'] != parts[0]*10000 + parts[1]*100 + parts[2] or
        bytes((version['protocolVersion'], *parts)) not in linked or
        NATIVE_FAMILY_MARKERS[family] not in linked):
        raise ValueError('Linked base App family/GATT version differs from package metadata')
    # An explicit linked policy is mandatory here. The generic ELF reader also
    # understands historical signed images for private historical auditing.
    from goodix_base_elf import Elf
    elf = Elf(recovery_elf.read_bytes())
    policy = elf.symbol('inktag_recovery_policy_info')
    if policy['bytes'] != 6 or elf.slice(elf.offset(policy['address'], 6), 6) != b'IKSP\x01\x02':
        raise ValueError('base package requires actual linked CRC-only Recovery')
    identity = recovery_binary_identity(rec)
    if identity != dict(version=version['recoveryVersion'], epoch=1):
        raise ValueError('Actual Recovery identity differs from build metadata')
    if not linked or len(linked) + 40 > BOARDS[family]['slot_size'] - 904:
        raise ValueError('Unchanged native payload limit exceeded')
    body = append(linked, board=family, epoch=1, release_version=version['otaVersion'])
    integrity = verify_integrity(body, board=family, epoch=1)
    firmware = body + image_info(body, family, 1)
    board = BOARDS[family]
    manifest = dict(format='inktag-ota-v1', transport='goodix-otas-v2', layout='layout-v1',
        board=family, chip=board['chip'], sourceProfile='base', release=version['version'],
        signature=dict(required=False, algorithm='none', policy='crc-only'),
        releaseVersion=version['otaVersion'], protocol=version['protocolVersion'], compatibilityEpoch=1,
        application=dict(file='application.bin', size=len(body), transfer_size=len(firmware),
            checksum=checksum(body), sha256=record(firmware)['sha256'],
            linked_size=len(linked), linked_sha256=record(linked)['sha256'], integrity=integrity,
            active_address=board['active'], slot_size=board['slot_size'],
            recovery_state_addresses=[board['recovery_a'], board['recovery_b']],
            image_store_start=board['image_store_start'], image_store_max_size=board['image_store_size'],
            image_info_size=48))
    output.mkdir(parents=True)
    write_zip(output/'app-ota.zip', {'manifest.json': json_bytes(manifest), 'application.bin': firmware})
    (output/'application.bin').write_bytes(firmware)
    (output/'recovery.bin').write_bytes(rec)
    review = dict(schemaVersion=1, sourceProfile='base', signed=False, signing=False,
        hardwareAccessed=False, completeFlashPackage=False, family=family,
        linkedApp=record(linked), appIntegrityMetadataBytes=40, nativeImageInfoBytes=48,
        recovery=dict(**record(rec), **identity, integrityPolicy='crc-only'),
        proofs=dict(app=app_proof, recovery=rec_proof), ota=record((output/'app-ota.zip').read_bytes()))
    (output/'package-review.json').write_bytes(json_bytes(review))
    return review


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-target', choices=TARGETS, required=True)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--toolchain-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=range(1, 33), default=4)
    args = parser.parse_args()
    sdk, toolchain, output = args.sdk_root.resolve(), args.toolchain_root.resolve(), args.output.resolve()
    if not sdk.is_dir() or not (toolchain/'bin').is_dir() or output.exists():
        parser.error('Existing external SDK/toolchain and a new output path are required')
    family, port, board = TARGETS[args.build_target]
    dependencies = json.loads((ROOT/'firmware/ports/goodix/base-sdk-dependencies.json').read_text(encoding='utf-8'))
    archive = sdk/dependencies['archivePath']
    if not archive.is_file() or record(archive.read_bytes())['sha256'] != dependencies['sha256'][port]:
        parser.error('External SDK BLE archive differs from the qualified dependency')
    if not (toolchain/'bin/arm-none-eabi-gcc').is_file():
        parser.error('External Linux ARM compiler is missing (Windows uses WSL)')
    command = (['wsl.exe', '--exec'] if os.name == 'nt' else []) + [
        'make', '-j'+str(args.jobs), '-f', native_path(ROOT/'tools/sdk/goodix-base-native.mk'),
        '-C', native_path(ROOT/'firmware/ports/goodix'/port), 'PORT='+port,
        'app', 'bootloader', 'SOURCE_PROFILE=base', 'REPO_ROOT='+native_path(ROOT),
        'SDK_ROOT='+native_path(sdk), 'TOOLCHAIN_PATH='+native_path(toolchain/'bin'), 'BOARD='+board,
        'PANEL_DIAGNOSTIC=0', 'CLOCK_PARTIAL=0', 'ACTIVATION_DATAFLOW=0',
        'BOOT_RESULT_DATAFLOW=0', 'CODE_FINGERPRINTS=0',
        'SDK_LITERAL_FAULT_LOG='+str(int(port=='gr5331' or family=='gr5513_zkel_internal'))]
    if port == 'gr551x':
        command.append('INTERNAL_IMAGES='+str(int(family=='gr5513_zkel_internal')))
    artifact = json.loads((ROOT/'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards'][board]['artifact']
    app_project = sdk/'projects/ble/ble_peripheral/ble_app_inktag_ota/GCC/out'
    rec_project = sdk/'projects/ble/dfu/app_bootloader_inktag/GCC/out'
    version = json.loads((ROOT/'version.json').read_text(encoding='utf-8'))
    with sdk_lease(sdk):
        subprocess.run(command, check=True, cwd=ROOT)
        review = package_native(family=family, app=app_project/(artifact+'-ota.bin'),
            app_elf=app_project/'lst'/(artifact+'-ota.elf'),
            recovery=rec_project/(artifact+'-boot.bin'), recovery_elf=rec_project/'lst'/(artifact+'-boot.elf'),
            version=version, output=output)
        # This distinct unsigned Full format carries exact native App/Recovery,
        # default GUI and the qualified host-NVDS initialization recipe. It
        # neither imports the signed Pro producer nor selects a physical target.
        from goodix_base_full import build_base
        full_path = output/'app-full.zip'
        full_review = build_base(output/'app-ota.zip',
            app_project/'lst'/(artifact+'-ota.elf'), output/'recovery.bin',
            rec_project/'lst'/(artifact+'-boot.elf'), full_path)
        review.update(full=record(full_path.read_bytes()),
            fullFormat=full_review['metadata']['schema'], fullLayout='layout-v1',
            completeFlashPackage=True,
            firstInstallPreparationRequired=True, fullHardwareQualified=False)
        (output/'package-review.json').write_bytes(json_bytes(review))
        # Preserve the actual selected source closure; no Git or ignored input
        # is necessary for this independent build/package entry.
        shutil.copy2(app_project.parents[1]/'Src/inktag-source-profile.json', output/'source-profile.json')
    print(json.dumps(review, indent=2))


if __name__ == '__main__':
    main()
