"""Explicit key-free HW56 Full integrity admission; no signed-input fallback.

base Full accepts the public native format without compiler certificates or
publisher authentication. Actual delivered Recovery behavior is qualified by
separate source/build/test receipts; unsigned OTA still needs live E1.
"""
from __future__ import annotations
import hashlib
import io
import json
from pathlib import Path
import runpy
import stat
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]

def json_document(data):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result: raise ValueError('Duplicate base JSON field')
            result[key]=value
        return result
    try: return json.loads(data,object_pairs_hook=unique)
    except (TypeError,RecursionError) as error: raise ValueError('Invalid bounded base JSON') from error


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_archive(data, limits=None):
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            entries = z.infolist()
            if limits is None:
                if len(entries)!=3 or len({e.filename for e in entries})!=3:
                    raise ValueError('Native OTA requires three unique members')
                limits={}
                for e in entries:
                    n=e.filename
                    if '/' in n or '\\' in n or ':' in n or n in ('.','..'):
                        raise ValueError('Unsafe native OTA member')
                    if n=='manifest.json': limits[n]=4096
                    elif n.endswith('.bin'): limits[n]=163840
                    elif n.endswith('.dat'): limits[n]=512
                    else: raise ValueError('Unknown native OTA member')
            if len(entries) != len(limits) or {e.filename for e in entries} != set(limits):
                raise ValueError('base archive requires exact unique native members')
            if any(e.flag_bits & 1 or e.is_dir() or stat.S_ISLNK(e.external_attr >> 16)
                   or not 0 < e.file_size <= limits[e.filename] for e in entries):
                raise ValueError('Unsafe or oversized base archive member')
            return {e.filename:z.read(e) for e in entries}
    except (zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError('Invalid base archive') from error


def fields(data):
    if type(data) is not bytes or len(data) > 512:
        raise ValueError('Invalid native init object')
    offset = 0
    def integer():
        nonlocal offset
        value = 0
        for shift in range(0,35,7):
            if offset >= len(data): break
            b=data[offset];offset+=1;value|=(b&127)<<shift
            if b < 128:
                if value > 0xffffffff: break
                return value
        raise ValueError('Invalid native init varint')
    result={}
    while offset < len(data):
        tag=integer();key,wire=tag>>3,tag&7
        if not key or key in result or wire not in (0,2):
            raise ValueError('Duplicate/unsupported native init field')
        value=integer()
        if wire==2:
            if value > len(data)-offset: raise ValueError('Truncated native init field')
            value,offset=data[offset:offset+value],offset+value
        result[key]=value
    return result


def unsigned_app(data):
    files=read_archive(data)
    manifest=json_document(files.get('manifest.json',b'{}'))
    if (type(manifest) is not dict or set(manifest)!={'manifest'} or
            type(manifest['manifest']) is not dict or set(manifest['manifest'])!={'application'}):
        raise ValueError('Unknown base OTA manifest')
    item=manifest['manifest']['application']
    if type(item) is not dict or set(item)!={'bin_file','dat_file'}:
        raise ValueError('Unknown base OTA application members')
    bn,dn=item['bin_file'],item['dat_file']
    if (not isinstance(bn,str) or not isinstance(dn,str) or not bn.endswith('.bin')
            or not dn.endswith('.dat') or bn==dn or set(files)!={'manifest.json',bn,dn}):
        raise ValueError('base OTA member pairing rejected')
    packet=fields(files[dn])
    if set(packet)!={1}:
        raise ValueError('Explicit base input requires unsigned command; signed input never falls back')
    command=fields(packet[1])
    if set(command)!={1,2} or command[1]!=1:
        raise ValueError('base native INIT required')
    init=fields(command[2]);app=files[bn];version=init.get(1)
    if (set(init)-set(range(1,11)) or not isinstance(version,int) or version>>16!=1
            or not version&65535 or init.get(2)!=56 or init.get(3)!=b'\x81\x02'
            or init.get(4)!=0 or init.get(5,0) or init.get(6,0) or init.get(9,0)
            or init.get(7)!=len(app) or not 8<=len(app)<=163840
            or fields(init.get(8,b''))!={1:3,2:hashlib.sha256(app).digest()[::-1]}
            or fields(init.get(10,b''))!={1:1,2:b''}):
        raise ValueError('base App HW56/SDreq/epoch/length/hash/CRC context rejected')
    stack,reset=struct.unpack_from('<II',app)
    if stack&7 or not 0x20004000<stack<=0x20010000 or not reset&1 or not 0x26000<=reset&~1<0x26000+len(app):
        raise ValueError('base App vectors rejected')
    return app,version


def verify_bundle(path, *, full=None):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or not 0<path.stat().st_size<=2500000:
        raise ValueError('Bounded external base Full package required')
    data=read_archive(path.read_bytes(),{'base.json':4096,'full.hex':1500000,'app-ota.zip':1048576})
    manifest=json_document(data['base.json'])
    context=dict(format_version=1,mcu='nrf52832',firmware_family='nrf52832_priceetag',
        backend_ids=['nrf52832_priceetag'],hw_version=56,compatibility_epoch=1,config_schema=1,
        baseline='inktag-release-v1',layout_version=1,layout_marker='N3V1')
    if (type(manifest) is not dict or set(manifest)!=set(context)|{'images'}
            or any(type(manifest[k]) is not type(v) or manifest[k]!=v for k,v in context.items())
            or manifest['images']!={n:{'bytes':len(data[n]),'sha256':sha(data[n])}
                                   for n in ('full.hex','app-ota.zip')}):
        raise ValueError('base Full native family/layout/epoch/member context rejected')
    if full is None: full=runpy.run_path(str(ROOT/'tools/sdk/nordic-full-settings.py'))
    import tempfile
    with tempfile.TemporaryDirectory(prefix='inktag-base-full-') as folder:
        hp=Path(folder)/'full.hex';hp.write_bytes(data['full.hex'])
        memory=full['HEX']['read_hex'](hp)
    if not memory or any(a<0 or a>=0x60000 for a in memory):
        raise ValueError('base Full overlaps image store, UICR or unknown partition')
    image=bytes(memory.get(a,255) for a in range(0x80000))
    full['TAIL']['validate_selectors'](image)
    size,crc,version=full['dfu_validate'](image)
    # Exact native S132 information structure, not the DAT's requested FWID
    # alone: SDK17 nrf_sdm.h uses MBR_SIZE + 0x2000 + {size:8,FWID:12}.
    if (struct.unpack_from('<I',image,0x3008)[0]!=0x26000 or
            struct.unpack_from('<H',image,0x300c)[0]!=0x0101):
        raise ValueError('base Full S132 native size/FWID mismatch')
    app,ota_version=unsigned_app(data['app-ota.zip'])
    if app!=image[0x26000:0x26000+size] or version!=ota_version:
        raise ValueError('base Full/OTA App pairing rejected')
    recovery_bytes=full['recovery_extent'](image)
    stack,reset=struct.unpack_from('<II',image,0x58000)
    if (stack&7 or not 0x20004000<stack<=0x20010000 or not reset&1 or
            not 0x58000<=reset&~1<0x58000+recovery_bytes):
        raise ValueError('base Full Recovery vectors/range rejected')
    full['assets_validate'](image)
    _,records=full['journal_decode'](image[0x56000:0x58000])
    full['validate_template_records'](records,size,crc)
    if image[0x4e000:0x56000]!=b'\xff'*32768:
        raise ValueError('base base requires neutral empty user-font region')
    return memory,dict(sourceProfile='base',fullAdmissionPolicy='crc-integrity',
        package_verified=True,package_sha256=sha(path.read_bytes()),app_bytes=size,
        app_crc32=f'{crc:08X}',app_sha256=sha(app),recovery_bytes=recovery_bytes,
        recovery_limit=24576,recovery_headroom=24576-recovery_bytes,
        application_limit=163840,application_headroom=163840-size,compatibility_epoch=1,
        signature_verified=False,recovery_source_authenticated=False,
        recovery_trust='base Full member integrity only; no publisher authentication or runtime policy attestation',
        base_firmware_family='nrf52832_priceetag')
