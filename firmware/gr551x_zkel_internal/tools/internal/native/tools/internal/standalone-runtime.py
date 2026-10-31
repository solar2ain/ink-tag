"""Standalone GR environment setup/check. No programmer, SDK or BLE execution."""
import argparse
import hashlib
import importlib
import importlib.abc
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import venv


def require(value, message):
    if not value:
        raise ValueError(message)


def safe(root, relative):
    require(isinstance(relative, str) and relative and '\\' not in relative,
            'Invalid bundle-relative path')
    part = Path(relative)
    require(not part.is_absolute() and ':' not in relative and '..' not in part.parts,
            'Bundle path escapes its root')
    target = root / part
    for current in (target, *target.parents):
        require(not current.is_symlink() and not getattr(current, 'is_junction', lambda: False)(),
                'Linked package/dependency paths are refused')
    require(target.resolve().is_relative_to(root.resolve()), 'Bundle path escapes root')
    return target


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(root):
    marker = safe(root, 'inktag-goodix-tools.json')
    require(marker.is_file(), 'This entry needs a generated GR standalone bundle, not an SDK/source checkout')
    data = json.loads(marker.read_text(encoding='utf-8'))
    require(data.get('schemaVersion') == 1 and data.get('kind') == 'tools-only', 'Unknown bundle kind/version')
    require(data.get('runtime') == 'tools/internal/runtime', 'Unexpected private runtime root')
    for name, record in data['files'].items():
        path = safe(root, name)
        require(path.is_file() and path.stat().st_size == record['bytes'] and sha(path) == record['sha256'],
                'Missing/changed bundle file: '+name)
    lock = json.loads(safe(root, 'tools/internal/dependencies-windows-x64.lock.json').read_text())
    return data, lock, safe(root, data['runtime'])


def validate_python(lock):
    expected = lock['python']
    require(sys.platform == 'win32' and platform.python_implementation() == 'CPython' and
            tuple(sys.version_info[:2]) == (expected['major'], expected['minor']) and
            struct.calcsize('P')*8 == expected['bits'],
            'Requires Windows x64 CPython3.12; install from https://www.python.org/downloads/windows/')


class NoDeviceImports(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('bleak', 'serial', 'usb', 'hid', 'inktag_cli'):
            raise ImportError('Device/full-CLI module forbidden in offline tool runtime: '+fullname)
        return None


def check(root, *, python_only=False, groups=None):
    data, lock, runtime = load(root)
    validate_python(lock)
    env_python = safe(root, '.env/Scripts/python.exe')
    require(env_python.is_file() and Path(sys.executable).resolve() == env_python.resolve(),
            'Run tools/setup.ps1 first; no installed/global CLI environment fallback')
    for wheel in lock['wheels']:
        require(importlib.metadata.version(wheel['name']) == wheel['version'],
                'Wrong Python dependency: '+wheel['name'])
    sys.meta_path.insert(0, NoDeviceImports())
    sys.path.insert(0,str(runtime/'tools/hardware'))
    for name in ('cryptography', 'cffi', 'yaml', 'qrcode', 'goodix_configured_full',
                 'goodix_portable_executor', 'goodix_release_full'):
        module = importlib.import_module(name)
        source = Path(module.__file__).resolve()
        require(source.is_relative_to(runtime) or source.is_relative_to(root/'.env'),
                'Imported outside isolated bundle: '+name)
    pins = importlib.import_module('goodix_pin_config')
    configs = sorted(safe(root, 'tools/configs').glob('*.toml'))
    require(configs, 'No PCB configurations supplied')
    for config in configs:
        pins.read_board_config(config)
    dependencies, missing = {}, []
    selected = [] if python_only else (groups or list(lock['native']))
    for group in selected:
        require(group in lock['native'], 'Unknown native dependency group')
        spec = lock['native'][group]
        folder = safe(runtime, spec['destination'])
        bad = []
        for name, record in spec['files'].items():
            path = safe(folder, name)
            if not path.is_file() or path.stat().st_size != record['bytes'] or sha(path) != record['sha256']:
                bad.append(name)
        dependencies[group] = dict(version=spec['version'], ready=not bad, missingOrChanged=bad,
                                   officialSource=spec['source'])
        if bad:
            missing.append(group)
    return dict(ok=not missing, offlineOnly=True, nativeExecuted=False, hardwareAccessed=False,
                sdkRequired=False, installedCliRequired=False, firmwareIncluded=False,
                firmwarePair='not-applicable-tools-only', buildId=data['buildId'],
                python=sys.version.split()[0], pythonOnly=python_only, configsChecked=len(configs),
                dependencies=dependencies, missing=missing,
                driverStatus='manual installation/target operation not checked; no enumeration',
                next='Run setup.ps1 with -JLinkHome and/or -GProgrammerHome from licensed official installations' if missing else
                     'Offline files/imports ready only; device/Full qualification is separate')


def setup(root, base_python, *, jlink=None, gprogrammer=None, offline=False):
    data, lock, runtime = load(root)
    validate_python(lock)
    environment = safe(root, '.env')
    env_python = safe(root, '.env/Scripts/python.exe')
    if not environment.exists():
        venv.EnvBuilder(with_pip=True).create(environment)
    require(env_python.is_file(), 'Incomplete isolated environment retained; use a fresh extraction, no overwrite')
    wheels = safe(root, 'tools/internal/wheels')
    for record in lock['wheels']:
        path = safe(wheels, record['file'])
        require(path.is_file() and sha(path) == record['sha256'], 'Missing/changed locked wheel: '+record['file'])
    # All transitive packages are listed/hash-bound; no resolver or index use.
    command = [str(env_python), '-I', '-m', 'pip', '--isolated', '--disable-pip-version-check', '--no-cache-dir', 'install', '--no-index',
               '--find-links', str(wheels), '--no-deps', '--only-binary=:all:', '--require-hashes',
               '-r', str(safe(root, 'tools/internal/requirements-windows-x64.lock'))]
    subprocess.run(command, check=True, stdout=sys.stderr, stderr=sys.stderr)
    subprocess.run([str(env_python), '-I', '-m', 'pip', '--isolated', '--disable-pip-version-check', 'check'], check=True, stdout=sys.stderr, stderr=sys.stderr)
    for group, supplied in (('jlink', jlink), ('gprogrammer', gprogrammer)):
        if supplied is None:
            continue
        source = supplied.resolve(strict=True)
        spec = lock['native'][group]
        destination = safe(runtime, spec['destination'])
        # Check complete explicit closure before copying any executable. Never
        # execute an installer/tool, recursively guess an installation or alter it.
        copies = []
        for name, record in spec['files'].items():
            incoming, outgoing = safe(source, name), safe(destination, name)
            require(incoming.is_file() and incoming.stat().st_size == record['bytes'] and
                    sha(incoming) == record['sha256'], 'Wrong/missing locked '+group+' file: '+name)
            require(not outgoing.exists() or sha(outgoing) == record['sha256'],
                    'Installed dependency differs; use a fresh extraction: '+name)
            copies.append((incoming, outgoing))
        for incoming, outgoing in copies:
            outgoing.parent.mkdir(parents=True, exist_ok=True)
            if not outgoing.exists():
                shutil.copyfile(incoming, outgoing)
    return dict(ok=True, setupComplete=True, nativeExecuted=False, hardwareAccessed=False,
                sdkChanged=False, onlineAcquisition=False, firmwareIncluded=False,
                python=str(env_python), next='Run tools/check.ps1; native dependencies not supplied remain missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('setup', 'check'))
    parser.add_argument('--bundle-root', required=True, type=Path)
    parser.add_argument('--python-only', action='store_true')
    parser.add_argument('--native-group', action='append')
    parser.add_argument('--jlink-home', type=Path)
    parser.add_argument('--gprogrammer-home', type=Path)
    parser.add_argument('--offline', action='store_true', help='Setup is always offline with included locked wheels')
    args = parser.parse_args()
    try:
        result = (setup(args.bundle_root, sys.executable, jlink=args.jlink_home,
                        gprogrammer=args.gprogrammer_home, offline=args.offline) if args.action == 'setup' else
                  check(args.bundle_root, python_only=args.python_only, groups=args.native_group))
    except (ValueError, OSError, ImportError, subprocess.SubprocessError) as error:
        result = dict(ok=False, offlineOnly=True, hardwareAccessed=False, nativeExecuted=False,
                      error=str(error), next='See tools/setup.ps1 -h and tools/check.ps1 -h')
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
