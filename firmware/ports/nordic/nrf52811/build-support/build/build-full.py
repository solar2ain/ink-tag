#!/usr/bin/env python3
"""Build a complete source Full package; no keys, signing or device access."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import uuid
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT.parents[3]
sys.dont_write_bytecode = True
SD_FILE = 'components/softdevice/s112/hex/s112_nrf52_7.3.0_softdevice.hex'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha(data):
    return hashlib.sha256(data).hexdigest()


def windows_to_wsl(path):
    text = str(Path(path).absolute())
    if not re.match(r'^[A-Za-z]:[\\/]', text):
        raise ValueError('Use a local drive path for Windows/WSL builds')
    return '/mnt/' + text[0].lower() + '/' + text[3:].replace('\\', '/')


def settings(app, softdevice, version):
    """SDK17 settings-v2 ABI: confirmed Bank0, no pending update, CRC boot checks.

    The first 0x323 bytes match Nordic's initialized settings structure; unused
    page bytes remain erased. Both native pages receive the identical seed.
    """
    data = bytearray(0x323)
    crc = lambda b: zlib.crc32(b) & 0xffffffff
    for offset, value in [(4, 2), (8, version), (12, 0), (0x18, len(app)),
                          (0x1c, crc(app)), (0x20, 1), (0x34, len(softdevice))]:
        struct.pack_into('<I', data, offset, value)
    data[0x260] = data[0x2a1] = 1
    struct.pack_into('<I', data, 0x261, crc(softdevice))
    struct.pack_into('<I', data, 0x2a2, crc(app))
    struct.pack_into('<I', data, 0x25c, crc(data[0x260:0x323]))
    struct.pack_into('<I', data, 0, crc(data[4:92]))
    return bytes(data)


def compose(full, packer, sd_memory, app, recovery, assets, ota_version,
            profile, expected_gui_crc):
    """Pure package composition, also exercised by clean-checkout fixtures."""
    if profile != 'crc-only':
        raise ValueError('Unknown Recovery profile')
    if type(ota_version) is not int or not 0 < ota_version <= 65535:
        raise ValueError('otaVersion must fit epoch1')
    if not sd_memory or any(not 0 <= a < 0x19000 for a in sd_memory):
        raise ValueError('SoftDevice/MBR crosses the fixed App boundary')
    app_map = {0x19000+i: v for i, v in enumerate(app)}
    rec_map = {0x2a000+i: v for i, v in enumerate(recovery)}
    full.HEX['validate_component'](app_map, 0x19000, 0x2a000, 'App')
    full.HEX['validate_component'](rec_map, 0x2a000, 0x2e000, 'Recovery')
    full.HEX['validate_assets'](assets)
    if type(expected_gui_crc) is not int or struct.unpack_from('<I', assets, 12)[0] != expected_gui_crc:
        raise ValueError('GUI differs from the App compiled resource binding')
    encoded = (1 << 16) | ota_version
    ota, _ = packer.package(app, ota_version)
    sd_end = max((a for a in sd_memory if a >= 0x1000), default=0) + 1
    if not 0x1000 < sd_end <= 0x19000:
        raise ValueError('SoftDevice is missing or too large')
    sd = bytes(sd_memory.get(a, 255) for a in range(0x1000, sd_end))
    seed = settings(app, sd, encoded)
    memory = dict(sd_memory)
    memory.update({0xff8+i: v for i, v in enumerate(struct.pack('<II', 0x2a000, 0x2e000))})
    memory.update(app_map)
    memory.update(rec_map)
    for address in (0x2e000, 0x2f000):
        memory.update({address+i: v for i, v in enumerate(seed)})
    full.HEX['validate'](memory, assets)
    manifest = dict(format_version=1, mcu='nrf52811', firmware_family='aesl22_nrf',
        backend_ids=['nrf52811_asel22'], hw_version=52, config_schema=1,
        baseline='inktag-release-v1', layout_version=1, layout_marker='N1V1',
        factory_format='N1P1', user_format='N1R1', compatibility_epoch=1,
        container_kind=full.SELF_BUILT_CONTAINER, recovery_signature_policy='crc-only',
        minimum_tools_version=full.SELF_BUILT_MIN_TOOLS, app_gui_crc32=expected_gui_crc)
    return memory, ota, manifest, seed


def write_package(full, destination, memory, ota, assets, manifest):
    destination = Path(destination)
    full_hex = destination.with_suffix('.hex')
    full.write_hex(full_hex, memory)
    files = {'full.hex': full_hex.read_bytes(), 'app-ota.zip': ota, 'gui-assets.bin': assets}
    manifest = {**manifest, 'images': {n: dict(bytes=len(b), sha256=sha(b)) for n, b in files.items()}}
    files['base.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    _, _, review = full.verify_bundle(destination)
    return review


def aux_reserved_policy(text):
    config = dict(line.split('=', 1) for line in text.splitlines() if '=' in line)
    switches = [p for p in shlex.split(config.get('CFLAGS', ''))
                if p.startswith('-DINKTAG_AUX_RESERVED_ENABLED')]
    if switches != ['-DINKTAG_AUX_RESERVED_ENABLED=0']:
        raise ValueError('Normal builds require actual INKTAG_AUX_RESERVED_ENABLED=0')
    sources = shlex.split(config.get('SOURCES', ''))
    if any(Path(p).name.lower().startswith(('nrfx_pwm', 'nrf_drv_pwm', 'app_pwm'))
           for p in sources):
        raise ValueError('Unexpected PWM source in this board profile')


def compiler_policy(text):
    aux_reserved_policy(text)
    config = dict(line.split('=', 1) for line in text.splitlines() if '=' in line)
    sources = shlex.split(config.get('SOURCES', ''))
    if not any(p.endswith('/inktag_nrf_dfu_hash.c') for p in sources):
        raise ValueError('Missing CRC-only Recovery integrity source')
    if any(token in text for token in ('inktag_nrf_dfu_crypto.c', 'dfu_public_key.c',
            'uECC', 'micro-ecc', 'INKTAG_NRF_REQUIRE_SIGNED_DFU')):
        raise ValueError('Retired Recovery signature dependency or option')


def source_snapshot():
    paths = [ROOT/'build.ps1', ROOT/'build.py', ROOT/'Makefile', ROOT/'version.json']
    for folder in ('app', 'compat', 'build-support', 'protocol'):
        paths.extend(p for p in (ROOT/folder).rglob('*') if p.is_file()
                     and not {'__pycache__', '.runtime'}.intersection(p.parts)
                     and (p.name.startswith('Makefile') or p.suffix in ('.c', '.h', '.inc', '.ld', '.S', '.mk', '.py', '.json', '.toml', '.ps1')))
    records = {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sorted(set(paths))}
    for row in json.loads((ROOT/'shared-common.json').read_text())['files']:
        path = (BASE/row['canonical']).resolve()
        if not path.is_relative_to(BASE):
            raise ValueError('Shared source outside the Base checkout')
        records['base/'+row['canonical']] = sha(path.read_bytes())
    for name in ('Makefile', 'makefile.basic', 'firmware/boards/nrf52811_aesl.h',
                 'tools/platforms/nordic/nrf52811/programmer/internal/nrf11_full_format.py',
                 'tools/platforms/nordic/nrf52811/programmer/internal/validate-full.py'):
        records['base/'+name] = sha((BASE/name).read_bytes())
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=('crc-only',), default='crc-only')
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--toolchain-path', type=Path)
    parser.add_argument('--build-root', type=Path)
    parser.add_argument('--output-root', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 64:
        parser.error('--jobs must be in 1..64')
    if 'INKTAG_NRF_REQUIRE_SIGNED_DFU' in os.environ:
        raise ValueError('Retired Recovery signature policy environment option')
    if os.name == 'nt':
        # The documented Arm toolchain is Linux x86_64. Preserve argument
        # boundaries; never construct a shell command or download dependencies.
        command = ['wsl', '--cd', windows_to_wsl(ROOT), 'python3', '-B', 'build-support/build/build-full.py',
                   '--profile', args.profile, '--jobs', str(args.jobs)]
        for name in ('sdk_root', 'toolchain_path', 'build_root', 'output_root'):
            value = getattr(args, name)
            if value is None and name in ('sdk_root', 'toolchain_path'):
                value = os.environ.get('SDK_ROOT' if name == 'sdk_root' else 'TOOLCHAIN_PATH')
            if value is not None:
                command += ['--'+name.replace('_', '-'), windows_to_wsl(ROOT/value)]
        raise SystemExit(subprocess.call(command))
    primary_module = load('primary_checkout', 'build-support/sdk/primary-checkout.py')
    primary = primary_module.primary_checkout(ROOT)
    config_path = ROOT/'.local/build-full-config.json'
    config = json.loads(config_path.read_text(encoding='utf-8')) if config_path.exists() else {}
    if type(config) is not dict or set(config)-{'sdkRoot', 'toolchainPath'} or any(type(v) is not str for v in config.values()):
        raise ValueError('Local build config accepts only sdkRoot/toolchainPath strings')
    sdk = primary_module.native_path(str(args.sdk_root or os.environ.get('SDK_ROOT') or config.get('sdkRoot') or primary/'.sdk/nordic/nRF5_SDK_17.1.0_ddde560')).resolve()
    toolchain = primary_module.native_path(str(args.toolchain_path or os.environ.get('TOOLCHAIN_PATH') or config.get('toolchainPath') or primary/'.tools/arm-gnu-toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin')).resolve()
    build_root = (args.build_root or ROOT/'.cache/build/nordic/nrf52811_aesl').resolve()
    output_root = (args.output_root or ROOT/'_build/nordic/nrf52811_aesl').resolve()
    for value in (build_root, output_root):
        if not value.is_relative_to(BASE):
            raise ValueError('Build/output root must belong to this source checkout')
    lock = json.loads((ROOT/'build-support/sdk/sdk-lock.json').read_text(encoding='utf-8'))['sdks']['nordic-nrf5-17.1.0']
    if sha((sdk/SD_FILE).read_bytes()) != lock['criticalFiles'][SD_FILE]:
        raise ValueError('S1127.3.0 HEX differs from the pinned SDK input')
    build_id = 'full-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    evidence = ROOT/'.local/full-builds'/build_id
    evidence.mkdir(parents=True, exist_ok=False)
    def run(name, command):
        with (evidence/(name+'.log')).open('xb') as stream:
            result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(name+' failed; see '+str(evidence/(name+'.log')))
    run('version', [sys.executable, 'build-support/version/sync-version.py', '--repo', '.'])
    run('compiler', [toolchain/'arm-none-eabi-gcc', '--version'])
    if '15.2.1' not in (evidence/'compiler.log').read_text(encoding='utf-8'):
        raise ValueError('Use the qualified Arm GNU15.2.1 toolchain')
    before = source_snapshot()
    common = ['SDK_ROOT='+str(sdk), 'TOOLCHAIN_PATH='+str(toolchain)]
    entry = 'Makefile'
    run('build', ['make', '-j'+str(args.jobs), '-f', entry,
                 'NRF_BUILD_ROOT='+str(build_root), 'NRF_OUTPUT_ROOT='+str(output_root),
                 *common])
    profile_build = build_root/args.profile
    output = output_root/args.profile
    run('app-policy', ['make', '-f', 'app/Makefile',
        'print-delivery-config', 'BUILD_DIR='+str(profile_build),
        'OUTPUT_DIR='+str(output), *common])
    aux_reserved_policy((evidence/'app-policy.log').read_text(encoding='utf-8'))
    run('recovery-policy', ['make', '-f', 'app/bootloader/Makefile',
        'print-delivery-config',
        'BUILD_DIR='+str(profile_build/'bootloader'), 'OUTPUT_DIR='+str(output/'bootloader'), *common])
    compiler_policy((evidence/'recovery-policy.log').read_text(encoding='utf-8'))
    if before != source_snapshot():
        raise ValueError('Source changed while building; previous Full output retained')
    full = load('source_full_validator', '../../../../tools/platforms/nordic/nrf52811/programmer/internal/nrf11_full_format.py')
    packer = load('source_ota_packager', 'build-support/ota/package-nrf52811.py')
    app = (output/'InkTag-nrf52811_aesl.bin').read_bytes()
    recovery = (output/'bootloader/InkTag-nrf52811_aesl-bootloader.bin').read_bytes()
    assets = (output/'InkTag-nrf52811_aesl-font-assets.bin').read_bytes()
    binding = (profile_build/'gui-projection/inktag_nrf_gui_asset_binding.h').read_text(encoding='utf-8')
    match = re.fullmatch(r'#define INKTAG_NRF_GUI_EXPECTED_CRC (0x[0-9a-fA-F]+)u\s*', binding)
    if not match:
        raise ValueError('Missing exact compiled GUI CRC binding')
    version = json.loads((ROOT/'version.json').read_text(encoding='utf-8'))
    memory, ota, manifest, seed = compose(full, packer, full.HEX['parse_hex'](sdk/SD_FILE),
        app, recovery, assets, version['otaVersion'], args.profile, int(match[1], 16))
    display = version['version']+'.0'
    name = 'InkTag-nrf52811_aesl-v'+display+'-self-built-full.zip'
    staged = evidence/name
    review = write_package(full, staged, memory, ota, assets, manifest)
    if before != source_snapshot():
        raise ValueError('Source changed while packaging; previous Full output retained')
    # Publish only verified complete files; a failed build keeps prior outputs.
    output.mkdir(parents=True, exist_ok=True)
    for target, data in [(output/name, staged.read_bytes()),
                         (output/name.replace('.zip', '.hex'), staged.with_suffix('.hex').read_bytes()),
                         (output/name.replace('-full.zip', '-ota.zip'), ota)]:
        temporary = target.with_name(target.name+'.tmp-'+uuid.uuid4().hex)
        temporary.write_bytes(data)
        os.replace(temporary, target)
    report = dict(buildId=build_id, profile=args.profile, full=str(output/name),
        recoveryVersion=version['recoveryVersion'],
        recoveryVerificationPolicy='crc-sha256',
        recoveryProtocol=0xe1, auxReservedEnabled=False,
        bytes=staged.stat().st_size, sha256=sha(staged.read_bytes()),
        minimumToolsVersion=full.SELF_BUILT_MIN_TOOLS, nativeReview=review,
        sourceInputs=before, settingsSeedSha256=sha(seed),
        signingPerformed=False, deviceAccess=False, officialCandidateStaged=False)
    (evidence/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'sourceInputs'}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError) as error:
        raise SystemExit(str(error)) from error
