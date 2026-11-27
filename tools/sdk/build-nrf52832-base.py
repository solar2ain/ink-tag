#!/usr/bin/env python3
"""Build/package Nordic base offline. No key, signer, transport or private source."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import shlex
import struct
import subprocess
import sys
import zipfile
import zlib

from goodix_code_elf import Elf
from nordic_base_identity import allocated_memory, verify
from nordic_recovery_identity import recovery_identity

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'nrf52832_priceetag'
PROFILE = TARGET+'-base'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def varint(value):
    if type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('Native unsigned 32-bit field required')
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    return bytes(result)+bytes([value])


def field(number, value):
    return varint(number*8+(2 if isinstance(value, bytes) else 0)) + (
        varint(len(value))+value if isinstance(value, bytes) else varint(value))


def app_context(app, version):
    if not 8 <= len(app) <= 163840 or not 0 < version & 65535 or version >> 16 != 1:
        raise ValueError('HW56 App length/version/epoch context rejected')
    stack, reset = struct.unpack_from('<II', app)
    if (stack & 7 or not 0x20004000 < stack <= 0x20010000 or not reset & 1 or
            not 0x26000 <= reset & ~1 < 0x26000+len(app)):
        raise ValueError('HW56 App vectors rejected')


def init_packet(app, version):
    app_context(app, version)
    native_hash = field(1, 3)+field(2, hashlib.sha256(app).digest()[::-1])
    validation = field(1, 1)+field(2, b'')
    init = b''.join(field(n, v) for n, v in (
        (1, version), (2, 56), (3, b'\x81\x02'), (4, 0), (7, len(app)),
        (8, native_hash), (9, 0), (10, validation)))
    # dfu_packet.command -> command{INIT, init}; no signed_command field.
    return field(1, field(1, 1)+field(2, init))


def archive(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(entry, data)
    return output.getvalue()


def ota_package(app, version):
    return archive({'manifest.json': (json.dumps({'manifest': {'application': {
        'bin_file': 'app.bin', 'dat_file': 'app.dat'}}}, sort_keys=True)+'\n').encode(),
        'app.bin': app, 'app.dat': init_packet(app, version)})


def native_settings(app, version, softdevice):
    """SDK17 -fshort-enums Settings ABI, identical primary and backup pages.

    Prefix CRC [4,92), boot-validation CRC [608,803), App bank0 at24,
    SD size at52; packed CRC boot-validation records start at608 and673.
    No retained signature, proof marker, pending command or new integrity ABI.
    """
    app_context(app, version)
    if not 8 <= len(softdevice) <= 0x25000:
        raise ValueError('S132 length rejected')
    page = bytearray(812)
    crc = zlib.crc32(app) & 0xffffffff
    struct.pack_into('<IIII', page, 4, 2, version, 0, 0)
    struct.pack_into('<III', page, 24, len(app), crc, 1)
    struct.pack_into('<I', page, 52, len(softdevice))
    page[608] = page[673] = 1
    struct.pack_into('<I', page, 609, zlib.crc32(softdevice) & 0xffffffff)
    struct.pack_into('<I', page, 674, crc)
    struct.pack_into('<I', page, 604, zlib.crc32(page[608:803]) & 0xffffffff)
    struct.pack_into('<I', page, 0, zlib.crc32(page[4:92]) & 0xffffffff)
    return bytes(page)


def build(repo):
    if any(os.environ.get(n) for n in ('MAKEFLAGS','MFLAGS','MAKEFILES','CFLAGS',
            'CPPFLAGS','LDFLAGS','SDK_ROOT','TOOLCHAIN_PATH','INKTAG_BUILD_PRO',
            'INKTAG_AUX_RESERVED_ENABLED')):
        raise ValueError('Use the explicit base entry without inherited compiler/profile overrides')
    for role in ('', 'bootloader/'):
        command = ['make', '-j2', '--output-sync=target', '-f',
                   f'firmware/ports/nordic/nrf52832/{role}Makefile.base',
                   'INKTAG_AUX_RESERVED_ENABLED=0']
        if os.name == 'nt':
            cwd = '/mnt/'+str(repo)[0].lower()+str(repo)[2:].replace('\\','/')
            command = ['wsl.exe','--exec','bash','--noprofile','--norc','-c',
                       'cd '+shlex.quote(cwd)+' && '+shlex.join(command)]
        subprocess.run(command, cwd=repo, check=True)


def linked(repo, role):
    suffix = '-bootloader' if role == 'recovery' else ''
    folder = 'bootloader/' if suffix else ''
    prefix = 'InkTag-'+TARGET+suffix
    elf_path = repo/f'.cache/build/nordic/{PROFILE}/{folder}{prefix}.elf'
    output = repo/f'_build/nordic/{PROFILE}/{folder}'
    app = (output/(prefix+'.bin')).read_bytes()
    raw = elf_path.read_bytes()
    verify(raw, role, app, source_profile='base')
    helpers = runpy.run_path(str(repo/'tools/sdk/merge-ihex.py'))
    actual = helpers['read_hex'](output/(prefix+'.hex'))
    expected = allocated_memory(Elf(raw))
    if actual != expected:
        raise ValueError('base ELF/BIN/HEX mismatch: '+role)
    return app, {a:v for a,v in actual.items() if a < 0x60000}, {
        ext: {'bytes': len(data), 'sha256': digest(data)} for ext, data in (
            ('elf',raw), ('bin',app), ('hex',(output/(prefix+'.hex')).read_bytes()))}


def fill_recovery_padding(recovery, rec_hex):
    # Intel HEX omits linker alignment bytes that the linked BIN contains.
    # Full must preserve these exact bytes, as the PRO packager does.
    for address, value in enumerate(recovery, start=0x58000):
        if rec_hex.setdefault(address, value) != value:
            raise ValueError(f'Recovery HEX and linked BIN differ at 0x{address:05X}')


def package(repo, output, config=None):
    repo, output = Path(repo).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError('Preserve existing output; select a new directory')
    version_info = json.loads((repo/'version.json').read_text())
    version = (1 << 16) | version_info['otaVersion']
    app, app_hex, app_refs = linked(repo, 'app')
    recovery, rec_hex, rec_refs = linked(repo, 'recovery')
    fill_recovery_padding(recovery, rec_hex)
    recovery_version, epoch, _ = recovery_identity(recovery, repo)
    app_context(app, version)
    helpers = runpy.run_path(str(repo/'tools/sdk/merge-ihex.py'))
    sd_path = repo/'.sdk/nordic/nRF5_SDK_17.1.0_ddde560/components/softdevice/s132/hex/s132_nrf52_7.2.0_softdevice.hex'
    # Firmware dependency remains pinned independently from the App producer.
    expected = 'b5b345052d160ddbc5dcf7aacfe0da6ecba52902cceaf91bae0b7a23955356fa'
    if digest(sd_path.read_bytes()) != expected.lower():
        raise ValueError('Unpinned S132 input')
    sd_hex = helpers['read_hex'](sd_path)
    if not sd_hex or min(sd_hex) != 0 or max(sd_hex) >= 0x26000:
        raise ValueError('S132 address span rejected')
    sd = bytes(sd_hex.get(a,255) for a in range(0x1000,max(sd_hex)+1))
    settings = native_settings(app, version, sd)
    output.mkdir(parents=True)
    factory_path = output/'factory-template.hex'
    factory_command = [sys.executable,str(repo/'tools/sdk/generate-nordic-factory-config.py'),
        '--repo',str(repo),'--board',TARGET,'--application',
        str(repo/f'_build/nordic/{PROFILE}/InkTag-{TARGET}.hex'),'--output',str(factory_path)]
    if config is not None:
        factory_command += ['--config',str(Path(config).resolve())]
    subprocess.run(factory_command, cwd=repo, check=True)
    full = {}
    for component in (sd_hex, app_hex, rec_hex, helpers['read_hex'](factory_path),
            {base+i:v for base in (0x5e000,0x5f000) for i,v in enumerate(settings)}):
        if set(full).intersection(component):
            raise ValueError('Overlapping native Full components')
        full.update(component)
    full = runpy.run_path(str(repo/'tools/sdk/nrf52832-tail-layout.py'))['bind_selectors'](full)
    full_path = output/'full.hex'
    helpers['write_hex'](full_path,full)
    ota = ota_package(app,version)
    (output/'app-ota.zip').write_bytes(ota)
    sys.path.insert(0,str(repo/'tools/hardware'))
    from nordic_base_bundle import verify_bundle
    files = {'app-ota.zip':ota, 'full.hex':full_path.read_bytes()}
    base = dict(format_version=1,mcu='nrf52832',firmware_family=TARGET,
        backend_ids=[TARGET],hw_version=56,compatibility_epoch=epoch,config_schema=1,
        baseline='inktag-release-v1',layout_version=1,layout_marker='N3V1',
        images={n:{'bytes':len(d),'sha256':digest(d)} for n,d in files.items()})
    (output/'app-full.zip').write_bytes(archive(files | {
        'base.json':(json.dumps(base,sort_keys=True)+'\n').encode()}))
    verify_bundle(output/'app-full.zip')
    receipt = dict(schemaVersion=1,sourceProfile='base',appVerificationPolicy='crc-only',
        requiredRecoveryProtocol=0xe1,recoveryVersion=recovery_version,
        compatibilityEpoch=1,layout='N3V1',signaturePresent=False,signing=False,
        privateKeyRequired=False,deviceAccess=False,fullIsUnboundTemplate=True,
        linked={'app':app_refs,'recovery':rec_refs},
        artifacts={p.name:{'bytes':p.stat().st_size,'sha256':digest(p.read_bytes())}
                   for p in output.iterdir() if p.is_file()})
    (output/'build-package.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return receipt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--config',type=Path)
    p.add_argument('--package-only',action='store_true',help='Verify already built base ELF/BIN/HEX before packaging')
    args = p.parse_args()
    if not args.package_only:
        build(ROOT)
    print(json.dumps(package(ROOT,args.output,args.config),indent=2))


if __name__ == '__main__':
    main()
