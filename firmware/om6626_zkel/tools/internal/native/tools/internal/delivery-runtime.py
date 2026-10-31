"""Offline bundle validation only; imports no programmer or BLE client."""
import argparse
import hashlib
import importlib.metadata
import json
import platform
import sys
from pathlib import Path

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def child(root, relative):
    path=root/relative
    if path.is_symlink() or '..' in Path(relative).parts or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('path escapes bundle')
    return path

def check(root, firmware=None, config=None):
    bundle=json.loads((root/'bundle.json').read_text(encoding='utf-8-sig'))
    if (bundle.get('kind')!='tools-only' or bundle.get('firmwareIncluded') is not False or
            any(k in bundle for k in ('defaultFirmware','firmwareVersion'))):
        raise ValueError('tools-only manifest required; firmware binding is forbidden')
    for relative, expected in bundle['files'].items():
        path=child(root,relative)
        if not path.is_file() or digest(path)!=expected:
            raise ValueError('bundle file changed/missing: '+relative)
    dependencies=json.loads((root/'tools/internal/delivery-dependencies.json').read_text(encoding='utf-8-sig'))
    if platform.system()!='Windows' or platform.machine().lower() not in ('amd64','x86_64') or platform.python_version()!=dependencies['pythonVersion']:
        raise ValueError('Requires the tested Windows x64 Python '+dependencies['pythonVersion'])
    for name, version in dependencies['pythonPackages'].items():
        if importlib.metadata.version(name)!=version:
            raise ValueError('pinned Python dependency differs: '+name)
    for name in ('cryptography','cffi','unicorn','intelhex','qrcode','colorama','yaml'):
        __import__(name)
    runtime=root/'tools/internal/runtime'
    sys.path[:0]=[str(runtime/'tools/hardware')]
    from onmicro_full_config import read_config,require_family
    context=dict(baseline='inktag-release-v1',family=bundle['firmwareFamily'],layout='layout-v1')
    signature_verified=False
    if firmware is not None:
        import onmicro_full_base as full
        bound=full.inspect(Path(firmware))
        if (bound[0]['family']!=bundle['firmwareFamily'] or
                bound[0]['layout'] not in bundle['supportedLayouts']):
            raise ValueError('firmware does not match this tools family/layout')
        full.authenticated_program(bound)
        context=bound[0]
        signature_verified=True
    configs=[Path(config)] if config else [child(root,p) for p in bundle['configs']]
    for item in configs:
        require_family(read_config(item),context)
    if any(name in ('bleak','serial','inktag_cli') or name.startswith(('inktag_cli.','bleak.')) for name in sys.modules):
        raise ValueError('unexpected BLE/runtime client import')
    return dict(kind='tools-only',family=context['family'],layout=context['layout'],configs=len(configs),firmwareSupplied=firmware is not None,publisherSignatureVerified=signature_verified,hardwareAccessed=False,networkAccessed=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',required=True,type=Path)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--config',type=Path)
    args=parser.parse_args()
    try:
        print(json.dumps(check(args.root,args.firmware,args.config)))
    except Exception as error:
        print(str(error),file=sys.stderr)
        return 1
    return 0

if __name__=='__main__': raise SystemExit(main())
