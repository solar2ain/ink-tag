"""Verify the exported portable runtime offline. No probe, network or BLE."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import sys

def resolve(root,relative):
    if not isinstance(relative,str) or not relative or '\\' in relative:
        raise ValueError('Invalid inventory path')
    p=root/relative
    if Path(relative).is_absolute() or any(x in ('.','..') for x in relative.split('/')):
        raise ValueError('Inventory escapes bundle')
    if not p.resolve().is_relative_to(root.resolve()):raise ValueError('Linked path escapes bundle')
    for ancestor in [p,*p.parents]:
        if ancestor==root.parent:break
        if ancestor.is_symlink() or ancestor.is_junction():raise ValueError('Linked bundle path refused')
    return p

def verify_inventory(root,inventory):
    for name,entry in inventory.items():
        p=resolve(root,name)
        if not p.is_file() or p.stat().st_size!=entry['bytes'] or hashlib.sha256(p.read_bytes()).hexdigest()!=entry['sha256']:
            raise ValueError('Missing/changed bundle file: '+name)

def check(tools):
    tools=tools.resolve();root=tools.parent
    if tools!=Path(__file__).resolve().parents[1]:raise ValueError('Wrong tool root')
    value=json.loads((root/'bundle.json').read_bytes())
    if (value.get('artifactKind'),value.get('chip'),value.get('firmwareFamily'),value.get('fullWriter'),value.get('runtimeKind'),value.get('firmwareIncluded'))!=('tools-only','nrf52811','aesl22_nrf','native-v1','portable',False):
        raise ValueError('Not an nRF52811 portable tools-only bundle')
    if any(name in value for name in ('firmwareVersion','compatibilityEpoch','encodedVersion','firmwarePair')):
        raise ValueError('Tools must not select or bind a firmware version')
    if (root/'firmware').exists():raise ValueError('Tools must not contain firmware')
    verify_inventory(root,json.loads((root/'package-files.json').read_bytes())['files'])
    verify_inventory(tools/'.runtime',json.loads((tools/'internal/runtime-files.json').read_bytes())['files'])
    lock=json.loads((tools/'full-dependencies.lock.json').read_bytes())
    if sys.version.split()[0]!=lock['pythonVersion']:raise ValueError('Wrong portable Python version')
    for package in lock['packages']:
        if importlib.metadata.version(package['name'])!=package['version']:raise ValueError('Wrong package version: '+package['name'])
    import cryptography.hazmat.primitives.asymmetric.ec
    import yaml
    # Exercise the same deferred QR import/encoding used by real prepare,
    # before any caller may resolve/open a programmer. All codecs are private.
    sys.path.insert(0,str(tools/'internal'))
    from nrf11_full import read_config
    from qr_codec.device_link import build_device_link
    from qr_codec.device_qr import encode_device_qr
    read_config(tools/'configs/EL022HWRL.toml')
    encode_device_qr(build_device_link('IKT_NRF_0001'))
    if any(n=='inktag_cli' or n.startswith(('inktag_cli.','bleak')) for n in sys.modules):
        raise ValueError('Programmer runtime must not import CLI or BLE modules')
    print(json.dumps(dict(status='offline-integrity-and-imports-ok',python=sys.version.split()[0],hardwareAccess=False,networkAccess=False)))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['check']);p.add_argument('--tools',type=Path,required=True)
    try:check(p.parse_args().tools)
    except Exception as e:print(str(e),file=sys.stderr);sys.exit(1)
