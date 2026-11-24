"""Package a compiled InkTag native pair without a publisher signing path."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/hardware'))
from tlsr_base_package import (SIGNATURE,validate_native,validate_full_pair,
                               verify_ota_bytes,write_zip,recovery_policy)
from tlsr_native_integrity import verify


def create(application,recovery,stage0,output):
    # Bind the actual native profile, not an edition string supplied by a caller.
    verify(application.with_suffix('.elf'),'app',application,source_profile='base')
    verify(recovery.with_suffix('.elf'),'recovery',recovery,source_profile='base')
    app=application.read_bytes();rec=recovery.read_bytes();stub=stage0.read_bytes()
    validate_full_pair(app,rec,stub)
    declared,crc=validate_native(app,0x3f000,'InkTag application')
    version=json.loads((ROOT/'version.json').read_text())
    sha=lambda x:hashlib.sha256(x).hexdigest()
    # Read the actual public native enum; no private/full-writer dependency.
    header=(ROOT/'firmware/ports/telink/tlsr8359/src/inktag_tlsr.h').read_text()
    backends=sorted({int(x) for x in re.findall(r'INKTAG_BOARD_[A-Z0-9_]+\s*=\s*(\d+)',header)}-{0})
    if not backends:raise ValueError('Missing public native board contexts')
    manifest=dict(requiredBaseline='inktag-release-v1',format='inktag-ota-v1',
        transport='telink-fixed-recovery-v2',layout='layout-v1',board='tlsr8359_hanshow',
        chip='TLSR8359',edition='InkTag',releaseVersion=version['otaVersion'],
        release=version['version'],protocol=version['protocolVersion'],compatibilityEpoch=1,
        peripheral_backend_ids=backends,signature=SIGNATURE,
        application=dict(file='application.bin',size=len(app),transfer_size=len(app),
            declared_size=declared,crc32=crc,sha256=sha(app),active_address=0x1000,
            slot_size=0x3f000,region_size=0x40000,recovery_address=0x40000,marker_offset=8))
    if output.exists():raise ValueError('Preserve existing InkTag output directory')
    output.mkdir(parents=True)
    ota=output/'InkTag-tlsr8359_hsel-base-ota.zip'
    write_zip(ota,{'manifest.json':(json.dumps(manifest,indent=2,sort_keys=True)+'\n').encode(),
                   'application.bin':app})
    verify_ota_bytes(ota.read_bytes())
    members={'app-ota.zip':ota.read_bytes(),'recovery.bin':rec,'stage0.bin':stub}
    base=dict(format_version=1,required_baseline='inktag-release-v1',mcu='tlsr8359',
        layout='layout-v1',compatibility_epoch=1,config_schema=1,backend_ids=backends,
        peripheral_backend_ids=backends,edition='InkTag',recovery_integrity_policy='crc-only',
        images={name:dict(bytes=len(data),sha256=sha(data).upper()) for name,data in members.items()})
    members={'base.json':(json.dumps(base,indent=2,sort_keys=True)+'\n').encode(),**members}
    full=output/'InkTag-tlsr8359_hsel-base-full.zip';write_zip(full,members)
    report=dict(sourceProfile='base',signed=False,signerDependency=False,deviceAccess=False,
        recovery=recovery_policy(rec),artifacts={p.name:dict(bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
                                                for p in (ota,full)})
    (output/'package-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('application','recovery','stage0','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(create(a.application,a.recovery,a.stage0,a.output)))
