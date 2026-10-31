"""Current configured Full preparation from bounded, fresh same-unit evidence."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct

from tlsr_full_flash_state import defaults, initialize_full_state, sha, fnv
from tlsr_full_flash_write_plan import make_plan
from tlsr_full_flash_address import complete_source, select_complete
from tlsr_flash_jedec import validate_uid_jedec


def read_exact(path: Path, size: int) -> bytes:
    if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
        raise ValueError('Missing/unsafe/short bounded evidence: '+str(path))
    return path.read_bytes()


def source_family(factory: bytes, target: int) -> str:
    return complete_source(factory)[0]


def configured_factory(template: bytes, current: bytes, target: int, uid: bytes,
                       *, source: str, calibration: bytes | None, clear_all: bool):
    result=bytearray(current)
    if source=='stock':
        if calibration is None or len(calibration)!=8192:
            raise ValueError('Stock requires the fresh8KiB identity/calibration data map')
        stored=calibration[:8]
        if current[:8]!=b'\xff'*8 or current[0x100:0x10d]!=b'\xff'*13:
            if current[0x180:0x1a8]!=b'\xff'*40:
                raise ValueError('Stock prefix calibration needs the 0x7F180..0x7F1A7 shadow; occupied bytes have no established relocation path')
            body=b'STF1'+bytes((1,21,0,0))+current[:8]+current[0x100:0x10d]+bytes(3)
            result[0x180:0x1a8]=body+struct.pack('<I',fnv(body))+b'SFC1'
        for at,data in ((0,stored),(0x20,calibration[0x1000:0x1001]),
                        (0x40,calibration[0x1180:0x1190])):
            if at and current[at:at+len(data)] not in (b'\xff'*len(data),data):
                raise ValueError(f'Stock calibration relocation destination at 0x{0x7F000+at:05X} contains conflicting bytes; no established preservation path')
            result[at:at+len(data)]=data
    else:
        if calibration is not None:
            raise ValueError('Installed identity cannot use stock calibration relocation')
        stored=current[:8]
    # Stock7F000 begins with RF/calibration bytes, not an ordinary address.
    # Its non-erased prefix has just been preserved in STF1 above; select on
    # the relocated76000 identity view, so erased stock MAC + verified UID
    # does not mistake that calibration prefix for a conflicting MAC.
    identity=select_complete(bytes(result),stored,source,uid)
    identity['warnings'][:0] = complete_source(current)[1]
    result[:6]=bytes.fromhex(identity['legacy_hex'])
    # Independent installed envelopes identify host-owned configuration slots,
    # even if an optional old selector/default is damaged. Stock occupancy
    # outside that established ownership remains protected.
    for at,size in ((0x110,32),(0x130,16),(0x140,32)):
        old=current[at:at+size]
        if source=='stock' and old!=b'\xff'*size:
            raise ValueError(f'Stock bytes occupy the configuration relocation slot at 0x{0x7F000+at:05X}; no established preservation path')
        result[at:at+size]=template[0x7F000+at:0x7F000+at+size]
    result[0x100:0x10d]=template[0x7F100:0x7F10d]
    result[0x160:0x170]=bytes.fromhex(identity['typed_record_hex'])
    return bytes(result),identity


def prepare(base: Path, config: Path, factory_file: Path,
            uid_receipt: Path, output: Path, *, calibration_file: Path | None = None,
            settings_file: Path | None = None,
            destructive_confirmed: bool = False, expected_mac: str = '') -> dict:
    from tlsr_full_flash_plan import plan as admission
    from tlsr_full_flash_config import ROOT
    template,review=admission(base,config,require_write=True)
    factory=read_exact(factory_file,4096)
    if destructive_confirmed is not True:
        raise ValueError('Full needs per-unit y/N or explicit Yes confirmation')
    source=source_family(factory,review['board_id'])
    from tlsr_full_flash_entitlement import read_capture, authority
    if settings_file is None:
        raise ValueError('Fresh same-operation Settings capture is required to preserve Pro/prove Free; no erase')
    settings = read_capture(settings_file)
    registry=json.loads((ROOT/'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards'][review['board']]
    if uid_receipt.is_symlink() or uid_receipt.stat().st_size>16384:
        raise ValueError('Unsafe live UID receipt')
    uid=json.loads(uid_receipt.read_text(encoding='utf-8-sig'))
    registered=review['flash_contract']['flashJedecId']
    validate_uid_jedec(uid,registered)
    uid_hex=uid.get('uidHex','')
    if (uid.get('schema')!='inktag-tlsr-live-flash-uid-v1' or uid.get('samples')!=[uid_hex]*3 or
            uid.get('flashWrites')!=0):
        raise ValueError('Fresh three-sample read-only UID evidence required')
    calibration=read_exact(calibration_file,8192) if calibration_file else None
    current_factory,identity=configured_factory(template,factory,review['board_id'],bytes.fromhex(uid_hex),
        source=source,calibration=calibration,clear_all=True)
    if expected_mac and identity['address']!=expected_mac.upper():
        raise ValueError('Fresh selected immutable identity differs from ExpectedMac')
    activation, retention = authority(settings, bytes.fromhex(uid_hex), source)
    seeds,links=defaults(bytes.fromhex(identity['legacy_hex']),bytes.fromhex(review['screen_wire_hex']),
        bytes.fromhex(review['pin_wire_hex']),bytes.fromhex(review['aux_wire_hex']),review['board_id'])
    state=initialize_full_state(seeds,review['compatibility_epoch'],confirmed=destructive_confirmed,
                                activation=activation)
    # Only exact Nebular420's independently qualified upper extent, with its
    # registered observed part, may be included. JEDEC mismatch consent alone
    # never authorizes any extra physical address.
    high=(review['board_id']==2 and uid['jedec'].upper()==registered and
          registry.get('firstFlash',{}).get('flashJedecId','').upper()==registered and
          registry.get('physicalFlashSizeBytes')==1048576)
    if review['board_id']==2 and not high:
        raise ValueError('Cannot claim all-data clear without qualified Nebular upper-Flash coverage')
    capacity=1048576 if high else 524288
    image,write_plan=make_plan(template,current_factory,state,uid_hex,capacity=capacity,layout=review['layout'])
    write_plan.update(base_package=str(base.resolve()),configuration=str(config.resolve()),
        board_id=review['board_id'],observed_jedec=uid['jedec'].upper(),
        board=review['board'],identity=identity['address'],source_layout=source,
        base_package_sha256=review['base_package_sha256'],configuration_sha256=review['configuration_sha256'])
    write_plan['retentionCapture'] = dict(settings=str(settings_file.resolve()),
        settingsSha256=sha(settings), factory=str(factory_file.resolve()),
        factorySha256=sha(factory), proof=retention)
    report=dict(schema='inktag-tlsr-full-v1-preparation',required_baseline='inktag-release-v1',
        prepared_sha256=sha(image),write_plan_sha256=None,configuration=review,
        identity=identity,links=links,source_layout=source,capacity=capacity,
        application_name_hint=state['application_name_hint'],
        name_hint_authority='fresh Settings defaults; runtime startup not observed',
        written_bytes=write_plan['written_bytes'],clear_all=True,
        proPreservation=retention,
        diagnosticsCleared='old Settings generations are replaced; no qualified TLSR persistent crash/reset-history backend exists',
        externalRetained='NFC EEPROM is not accessed by this programmer; no external erasure claim',
        recovery_epoch=review['compatibility_epoch'],recovery_state='both pages initialized from selected firmware; old pages not read',
        current_factory_sha256=sha(factory),uid_receipt_sha256=sha(uid_receipt.read_bytes()),
        upper_flash_touched=high,hardware_started=False)
    plan_file=output.with_suffix('.plan.json');report_file=output.with_suffix('.json')
    if any(p.exists() for p in (output,plan_file,report_file)):
        raise ValueError('Full preparation evidence exists; no overwrite')
    output.parent.mkdir(parents=True,exist_ok=True)
    plan_bytes=(json.dumps(write_plan,indent=2)+'\n').encode('utf-8')
    report['write_plan_sha256']=sha(plan_bytes)
    with output.open('xb') as f:f.write(image)
    with plan_file.open('xb') as f:f.write(plan_bytes)
    with report_file.open('x',encoding='utf-8') as f:json.dump(report,f,indent=2)
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('base','config','factory','uid-receipt','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--calibration',type=Path)
    p.add_argument('--settings', type=Path, required=True, help='Internal fresh same-operation 8-KiB Settings capture; not an archive/token import')
    p.add_argument('--destructive-confirmed',action='store_true',help='Internal receipt of this unit\'s y/N or Yes confirmation; never bypasses trust/identity/capacity')
    p.add_argument('--expected-mac',default='')
    a=p.parse_args()
    try:
        result=prepare(a.base,a.config,a.factory,a.uid_receipt,a.output,
            calibration_file=a.calibration,
            settings_file=a.settings,
            destructive_confirmed=a.destructive_confirmed,expected_mac=a.expected_mac)
        print(json.dumps(result,indent=2))
    except ImportError:
        p.error('Missing bundled programmer format dependency; reinstall the complete tools and run setup.ps1. No CLI installation is required; no erase.')
    except (ValueError,OSError,KeyError,TypeError) as error:p.error(str(error))


if __name__=='__main__':main()
