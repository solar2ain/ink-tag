"""Package locally built Base App/Recovery files; no signing, delivery or device IO."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/hardware'))
from onmicro_base_native import verify
from onmicro_image import route_application_through_fixed_boot
from onmicro_release_evidence import recovery_identity,release_path_evidence
from _om_full_formats import onmicro_package as ota
from _om_full_formats.app_integrity import append as append_integrity

TARGETS={'om6626_zkel':('om6626_zkc','om6626_zkcxxb')}
def sha(data):return hashlib.sha256(data).hexdigest()
def entry(archive,name,data):
    info=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0));info.compress_type=archive.compression
    info.create_system=3;info.external_attr=0o100644<<16;archive.writestr(info,data)
def package(app_elf,app_bin,recovery_elf,recovery_bin,target,output):
    family,board=TARGETS[target];profile=ota.LAYOUT_PROFILES[(board,'layout-v1')]
    output=Path(output)
    if output.exists():raise ValueError('Output must be new; existing packages remain unchanged')
    rec_output=output.with_name(output.stem+'-common-recovery.bin')
    components=output.with_name(output.stem+'-components.json')
    if rec_output.exists() or components.exists():raise ValueError('Preserve existing component outputs')
    raw=Path(app_bin).read_bytes();recovery=Path(recovery_bin).read_bytes()
    verify(Path(app_elf).read_bytes(),'app',raw,profile='base')
    verify(Path(recovery_elf).read_bytes(),'recovery',recovery,profile='base')
    layout=struct.pack('<6I',0x4c4d4b49,profile['signature'],0x84000,0x79000,0xfd000,4096)
    capacity=struct.pack('<IBBHIII',0x50524b49,1,0,20,profile['signature'],319392,319488)
    if (raw.count(layout)!=1 or recovery.count(capacity+b'IKSP\x01\x02')!=1 or
            struct.unpack_from('<II',raw,36)!=(0x42a000,0x46533252)):
        raise ValueError('Actual native target/layout/capacity/CRC profile mismatch')
    identity=recovery_identity(ROOT,recovery,Path(recovery_elf))
    for data in (raw,recovery):release_path_evidence(data)
    version=json.loads((ROOT/'version.json').read_bytes())
    routed=route_application_through_fixed_boot(raw,app_xip_base=0x42a000,app_xip_end=0x478000)
    payload=append_integrity(routed,board=profile['board_signature'],layout=profile['signature'],epoch=1,release=version['otaVersion'])
    manifest=dict(format='inktag-ota-v1',transport='onmicro-dfu-v3',board=board,chip='OM6626',
        layout='layout-v1',compatibilityEpoch=1,releaseVersion=version['otaVersion'],
        release=version['version'],sourceProfile='base',signature=dict(required=False,algorithm='none',policy='crc-only'),
        application=dict(file='application.bin',size=len(payload),transfer_size=len(payload),sha256=sha(payload),
            image_type=16,active_address=0x2a000,slot_size=319488,recovery_state_addresses=[0x82000,0x83000],
            recovery_request_storage='transaction-journal'))
    output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_STORED) as archive:
        entry(archive,'manifest.json',json.dumps(manifest,sort_keys=True,indent=2)+'\n')
        entry(archive,'application.bin',payload)
    if ota.load_package(output)!=(manifest,payload):raise ValueError('OTA package readback differs')
    rec_output.write_bytes(recovery)
    material=dict(format='inktag-om-components-v1',sourceProfile='base',family=family,
        layout='layout-v1',epoch=1,recoveryVersion=identity['recoveryVersion'],
        images={'app-ota.zip':dict(file=output.name,bytes=output.stat().st_size,sha256=sha(output.read_bytes())),
                'recovery.bin':dict(file=rec_output.name,bytes=len(recovery),sha256=sha(recovery))})
    components.write_text(json.dumps(material,sort_keys=True,indent=2)+'\n',encoding='utf-8')
    result=dict(scope='Local Base file packaging; source/build provenance is not independently attested',
        appSha256=sha(raw),recoverySha256=sha(recovery),otaSha256=sha(output.read_bytes()),
        recoveryVersion=identity['recoveryVersion'],components=str(components),
        componentsSha256=sha(components.read_bytes()),signingSteps=0,hardwareOperated=False)
    return result
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('app-elf','app-bin','recovery-elf','recovery-bin'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--build-target',choices=TARGETS,required=True);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(package(args.app_elf,args.app_bin,args.recovery_elf,args.recovery_bin,args.build_target,args.out),indent=2))
