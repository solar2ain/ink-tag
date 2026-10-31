"""Bounded OM MCU-base inspection. No probe, device or production signing."""
import io
import json
import stat
import struct
import tempfile
import zipfile
import zlib
from pathlib import Path
from onmicro_factory_init import sha,decode,Program,MAX_CODE,unique
from _om_full_formats import onmicro_package as ota

NAMES={'base.json','app-ota.zip','recovery.bin','factory-init.omfi'}
LIMITS={'base.json':8192,'app-ota.zip':512*1024,'recovery.bin':155648,'factory-init.omfi':336000}

def bounded_zip(value,limits):
    if len(value)>2*1024*1024:raise ValueError('Full ZIP exceeds 2 MiB')
    try:
        with zipfile.ZipFile(io.BytesIO(value)) as z:
            entries=z.infolist()
            if len(entries)!=len(limits) or {e.filename for e in entries}!=set(limits):
                raise ValueError('unexpected/duplicate ZIP members')
            for e in entries:
                name=e.filename
                if name!=e.orig_filename or any(c in '/\\:' or ord(c)<32 or ord(c)==127 for c in name) or\
                   e.is_dir() or e.flag_bits&(1|32|64) or stat.S_IFMT(e.external_attr>>16) not in (0,stat.S_IFREG) or\
                   e.compress_type not in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED) or not 0<e.file_size<=limits[name]:
                    raise ValueError('oversized/encrypted/abnormal ZIP member')
            # Admit the COMPLETE directory before opening any member. Central
            # file_size/CRC may lie; never use ZipExtFile.read() without a bound.
            result={}
            for name,limit in limits.items():
                with z.open(name) as stream:content=stream.read(limit+1)
                if len(content)!=z.getinfo(name).file_size or len(content)>limit:
                    raise ValueError('ZIP expanded output exceeds fixed limit')
                result[name]=content
            return result
    except (zipfile.BadZipFile,RuntimeError,NotImplementedError,zlib.error) as e:raise ValueError('invalid Full ZIP') from e

def inspect(path):
    path=Path(path)
    if path.is_symlink():raise ValueError('Full base must be a regular file')
    with path.open('rb') as f:blob=f.read(2*1024*1024+1)
    files=bounded_zip(blob,LIMITS)
    meta=json.loads(files['base.json'].decode('utf-8'),object_pairs_hook=unique)
    if type(meta) is not dict or set(meta)!={'format','baseline','mcu','family','layout','epoch','config_schema','images','execution_qualified'} or\
       meta['format']!='inktag-om-full-v1' or meta['baseline']!='inktag-release-v1' or meta['mcu']!='om6626' or\
       type(meta['epoch']) is not int or not 0<=meta['epoch']<=0xffff or type(meta['config_schema']) is not int or\
       meta['config_schema']!=1 or meta['execution_qualified'] is not False:
        raise ValueError('OM Full base schema mismatch; review-only delivery')
    if type(meta['images']) is not dict or set(meta['images'])!=NAMES-{'base.json'}:
        raise ValueError('Full base image bindings missing')
    for name,item in meta['images'].items():
        if item!={'bytes':len(files[name]),'sha256':sha(files[name])}:raise ValueError('Full base image binding mismatch')
    # Inner manifest/firmware bounds are checked before maintained App verifier
    # opens/decompresses either member. No second OTA crypto implementation.
    app_members=bounded_zip(files['app-ota.zip'],{'manifest.json':8192,'application.bin':319488})
    with tempfile.TemporaryDirectory(prefix='inktag-om-base-') as d:
        p=Path(d)/'app-ota.zip'
        # Original bytes remain hash-bound above. The maintained verifier sees
        # identical admitted payload bytes, never the hostile compressed stream.
        with zipfile.ZipFile(p,'w',compression=zipfile.ZIP_STORED) as z:
            for name,content in app_members.items():z.writestr(name,content)
        manifest,signed=ota.load_package(p)
    app_epoch=manifest.get('compatibilityEpoch')
    if (manifest['layout']!=meta['layout'] or type(app_epoch) is not int or
            not 0<=app_epoch<=0xffff or app_epoch!=meta['epoch']):
        raise ValueError('Full/App layout/epoch mismatch')
    profile=ota.package_profile(manifest)
    if profile['signature'] not in (0x3156484f,0x31565a4f):raise ValueError('retired OM Full layout')
    family='om6626_hsela' if profile['signature']==0x3156484f else 'om6626_zkc'
    if meta['family']!=family:raise ValueError('Full family mismatch')
    rec=files['recovery.bin']
    # Metadata alone cannot make an old signed consumer release-v1 capable.
    if b'INKTAG-OM-RELEASE-V1\0' not in signed[:-96] or b'INKTAG-OM-RELEASE-V1\0' not in rec:
        raise ValueError('Full schema requires actual App/Recovery routing consumers')
    capacity=struct.pack('<IBBHIII',0x50524b49,1,0,20,profile['signature'],319392,319488)
    dfu=struct.pack('<IIII',16,0x2a000,0x2a000,0x4e000)
    if not rec or rec.count(capacity)!=1 or rec.count(dfu)!=1:
        raise ValueError('Recovery preflight/actual SDK DFU table mismatch')
    raw=bytearray(signed[:-96])
    if struct.unpack_from('<I',raw,4)[0]!=0x404101:raise ValueError('App fixed-Recovery route missing')
    struct.pack_into('<I',raw,4,struct.unpack_from('<I',raw,0x104)[0])
    return meta,manifest,bytes(raw),signed,files,sha(blob)

def authenticated_program(inspected):
    meta,app,raw,signed,files,_=inspected
    key=bytes.fromhex(app['signature']['public_key'])
    header,payload=decode(files['factory-init.omfi'],raw,signed,files['recovery.bin'],meta['layout'],
        key,ota.TRUSTED_SIGNING_KEY['public_key_hash'],expected_epoch=app['compatibilityEpoch'])
    return Program(header,payload)
