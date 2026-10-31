"""Tool-local offline preparation/check only. No network, pip, J-Link or device API."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys
import zipfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def plain(path):
    for item in (path, *path.parents):
        if item.exists() and (item.is_symlink() or getattr(item.stat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError('Linked runtime/package path refused')


def lock_read(path):
    plain(path)
    data = json.loads(path.read_bytes())
    if data.get('schemaVersion') != 1 or data.get('status') != 'locked' or data.get('closureReviewed') is not True:
        raise ValueError('Dependency closure is not locked/reviewed')
    if data.get('target') != {'os': 'windows', 'arch': 'amd64', 'pythonAbi': 'cp313'}:
        raise ValueError('Unsupported runtime target')
    if os.name != 'nt' or sys.version_info[:3] != (3, 13, 16) or struct.calcsize('P') != 8 or not sys.flags.isolated:
        raise ValueError('Requires isolated CPython3.13.16 Windows AMD64')
    return data


def verify_artifact(root, entry):
    name = entry['file']
    if Path(name).name != name or '/' in name or '\\' in name or ':' in name:
        raise ValueError('Unsafe artifact name')
    path = root / name
    plain(path)
    if path.stat().st_size != entry['bytes'] or digest(path).lower() != entry['sha256'].lower():
        raise ValueError('Dependency artifact size/hash mismatch: ' + name)
    return path


def extract(wheel, destination):
    seen = set()
    with zipfile.ZipFile(wheel) as archive:
        for info in archive.infolist():
            path = PurePosixPath(info.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in info.filename or ':' in info.filename:
                raise ValueError('Unsafe archive member')
            if (info.external_attr >> 16) & 0xF000 == 0xA000 or info.filename.casefold() in seen:
                raise ValueError('Linked/duplicate archive member')
            seen.add(info.filename.casefold())
            target = destination.joinpath(*path.parts)
            plain(target)
            if target.exists() and not info.is_dir():
                raise ValueError('Wheel collision; no overwrite')
        archive.extractall(destination)


def versions(lock):
    # Metadata only for pylink: do not instantiate JLink/Library or load its DLL.
    found = {entry['name']: importlib.metadata.version(entry['name']) for entry in lock['wheels']}
    for entry in lock['wheels']:
        if found[entry['name']] != entry['version']:
            raise ValueError('Wrong dependency version: ' + entry['name'])
    import _cffi_backend
    from cryptography.hazmat.primitives.asymmetric import ec
    # Exercise the pinned binary ABI without a production key or any target IO.
    point = bytes.fromhex('04' + '6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296' +
                         '4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5')
    ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), point)
    assert _cffi_backend.__version__ == '2.1.1'
    return found


def check(lock_path, runtime):
    lock = lock_read(lock_path)
    receipt = json.loads((runtime / 'environment.json').read_bytes())
    if receipt['lockSha256'] != digest(lock_path):
        raise ValueError('Runtime belongs to a different dependency lock')
    actual = {p.relative_to(runtime).as_posix() for p in runtime.rglob('*') if p.is_file()
              and p != runtime / 'environment.json'}
    if actual != set(receipt['files']):
        raise ValueError('Runtime file set changed')
    for relative, expected in receipt['files'].items():
        path = runtime / relative
        plain(path)
        if digest(path) != expected:
            raise ValueError('Runtime file modified: ' + relative)
    return versions(lock)


def setup(lock_path, artifacts, runtime):
    lock = lock_read(lock_path)
    plain(runtime)
    if (runtime / 'environment.json').exists():
        raise ValueError('Runtime already prepared; no overwrite')
    verify_artifact(artifacts, lock['runtime'])
    site = runtime / 'site'
    if site.exists():
        raise ValueError('Partial runtime site exists; preserve it and start a fresh preparation')
    site.mkdir()
    for entry in lock['wheels']:
        archive = verify_artifact(artifacts, entry)
        with zipfile.ZipFile(archive) as wheel:
            for license_entry in entry['licenseEntries']:
                data = wheel.read(license_entry['path'])
                if hashlib.sha256(data).hexdigest() != license_entry['sha256']:
                    raise ValueError('License binding changed')
        extract(archive, site)
    sys.path.insert(0, str(site))
    result = versions(lock)
    files = {p.relative_to(runtime).as_posix(): digest(p) for p in runtime.rglob('*') if p.is_file()}
    with (runtime / 'environment.json').open('x', encoding='utf8') as stream:
        json.dump({'lockSha256': digest(lock_path), 'files': files, 'versions': result,
                   'scope': 'Host ABI only; no vendor DLL/probe/target access'}, stream, indent=2)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['setup', 'check'])
    parser.add_argument('--lock', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    if os.name == 'nt':
        # Do not change Windows global LongPathsEnabled. Prefix only these
        # absolute, tool-local inputs for deep wheel members/file checks.
        for name in ('lock', 'runtime', 'artifacts'):
            value = getattr(args, name)
            if value is not None:
                full = str(value.absolute())
                if not full.startswith('\\\\?\\'):
                    full = ('\\\\?\\UNC\\' + full[2:]) if full.startswith('\\\\') else ('\\\\?\\' + full)
                setattr(args, name, Path(full))
    try:
        if args.action == 'setup':
            if args.artifacts is None:
                parser.error('--artifacts required for setup')
            found = setup(args.lock, args.artifacts, args.runtime)
        else:
            found = check(args.lock, args.runtime)
        print(json.dumps({'ok': True, 'versions': found, 'vendorDllLoaded': False, 'deviceAccess': False}))
    except Exception as error:
        print(json.dumps({'ok': False, 'error': str(error), 'deviceAccess': False}), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
