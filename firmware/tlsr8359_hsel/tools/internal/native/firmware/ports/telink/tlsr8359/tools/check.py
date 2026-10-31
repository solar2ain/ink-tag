"""Offline tools-only archive integrity/dependency check. No device is opened."""
import argparse
import hashlib
import importlib
from importlib.metadata import version
import json
from pathlib import Path, PurePosixPath
import re
import sys


def check(root, *, content_only=False):
    manifest = json.loads((root/'tools-manifest.json').read_text(encoding='utf8'))
    if (manifest.get('kind') != 'tools-only' or manifest.get('firmwareIncluded') is not False
            or manifest.get('firmware') is not None):
        raise ValueError('Not a firmware-independent tools-only archive')
    if not re.fullmatch(r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)', manifest.get('toolsVersion', '')):
        raise ValueError('toolsVersion must have four numeric components')
    targets = json.loads((root/'release/build-targets.json').read_text())['targets']
    target = targets.get(manifest.get('buildTarget'))
    if not target or target['chip'] != 'tlsr8359' or manifest.get('chip') != target['chip'] or manifest.get('brand') != target['brand']:
        raise ValueError('Tools buildTarget/brand/chip mismatch')
    expected = set(manifest['files']) | {'tools-manifest.json'}
    for name, entry in manifest['files'].items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or '..' in relative.parts or '\\' in name or ':' in name:
            raise ValueError('Unsafe tools manifest path')
        path = root.joinpath(*relative.parts)
        for component in (path, *path.parents):
            if component == root.parent:
                break
            if component.is_symlink() or getattr(component, 'is_junction', lambda: False)():
                raise ValueError('Linked tools file: '+name)
        if not path.is_file() or path.stat().st_size != entry['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise ValueError('Missing/changed tools file: '+name)
    # Generated interpreter caches are harmless; additional executable/payload
    # files in the extracted distribution are not silently trusted.
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*')
              if p.is_file() and '__pycache__' not in p.parts}
    if actual != expected:
        raise ValueError('Unexpected or missing archive files')
    if not content_only:
        if sys.version_info < (3, 11):
            raise ValueError('Python 3.11 or newer is required')
        for name in ('serial', 'cryptography', 'yaml', 'qrcode'):
            importlib.import_module(name)
        requirements = root/manifest['dependencies']['requirements']
        for line in requirements.read_text(encoding='ascii').splitlines():
            if line and not line.startswith('#'):
                name, required = line.split('==')
                if version(name) != required:
                    raise ValueError('Dependency version differs; run setup.ps1: '+name)
    return dict(ok=True, files=len(manifest['files']), kind='tools-only',
                firmwareIncluded=False, deviceAccess=False,
                windowsProgrammer='existing maintained backend; no new physical qualification',
                linuxMac='offline plans only; native programmer not qualified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content-only', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[5]
    print(json.dumps(check(root, content_only=args.content_only)))
