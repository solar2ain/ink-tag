"""Selected signed firmware and bounded Factory preservation for OM Full.

No candidate approvals, old App/journal/epoch input or user-settings import.
No transport is loaded; callers supply same-operation protected reads.
"""
import argparse
import json
import struct
from pathlib import Path
from onmicro_factory_init import sha
from onmicro_full_base import inspect,authenticated_program
from onmicro_full_config import read_config,require_family
from onmicro_flash_identity import address_from_result,capacity_from_result
from onmicro_source_capture import Capture,Part,UnitWitness,_ReadOnlySDK,CaptureError,required_ranges
from onmicro_configured_full import prepare_selected,compose_review_images
from onmicro_full_writer import compile_plan,ledger
from onmicro_full_execution import expected_stages

ROOT=Path(__file__).resolve().parents[2]
OWNED=set(range(100,120))|{132}

def configuration(path):
    return read_config(path)

def chunks():
    return [(a,min(8192,end-a)) for _,start,end in required_ranges() for a in range(start,end,8192)]

def local_file(path,size):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size!=size:raise ValueError('exact regular captured file required')
    return path.read_bytes()

def capture_from_directory(directory):
    root=Path(directory);nonce=local_file(root/'identity-request.bin',64)[48:]
    before=local_file(root/'identity-before.bin',64);after=local_file(root/'identity-after.bin',64)
    witness=UnitWitness('om6626',nonce,before,sha(before),after,sha(after))
    capacity_from_result(before)
    return Capture(witness,tuple(Part(a,n,data:=local_file(root/f'source-{a:06x}.bin',n),sha(data),witness.unit_id,nonce) for a,n in chunks()))

def selected_source(program,capture,cfg):
    """Native vendor identity/calibration selection, never old user approval."""
    modern,legacy=(0xfe000,0xf9000) if cfg['board_id']==4 else (0xfd000,0xfc000)
    candidates=[];warnings=[]
    for base in (modern,legacy):
        candidate_warnings=[]
        pair=capture.read(base,8192)
        if pair==b'\xff'*8192:continue
        try:
            sdk=_ReadOnlySDK(program,capture,base)
            if sdk.call('nvds_init',base)!=0:continue
            rows=sdk.snapshot()
        except CaptureError as error:
            if str(error)!='source classification cannot mutate SDK Flash':raise
            continue
        except ValueError:
            continue
        authorization_allowed=True
        try:require_authorization_consistency(pair,rows.get(111))
        except ValueError:
            authorization_allowed=False
            candidate_warnings.append('Old authorization storage is inconsistent; authorization will not be restored and Full will initialize Free.')
        if not any(v is not None for v in rows.values()):continue
        if any(v is not None and tag>=100 and tag not in OWNED for tag,v in rows.items()):
            candidate_warnings.append('Unrecognized old application NVDS records will be cleared; selected PCB defaults will be initialized.')
        candidates.append((base,rows,authorization_allowed,candidate_warnings))
    if not candidates:
        warnings.append('No readable old native settings were selected; Full will initialize defaults and Free. Missing BLE identity uses the stable same-operation Flash UID; no calibration values are invented.')
        return None,False,warnings
    warnings=list(candidates[0][3])
    if len(candidates)>1:
        protected=lambda rows:{k:v for k,v in rows.items() if v is not None and k<100}
        if protected(candidates[0][1])!=protected(candidates[1][1]):
            warnings.append('Multiple readable native settings pairs differ; the current-address pair takes precedence for valid vendor records.')
        if candidates[0][1].get(111)!=candidates[1][1].get(111):
            base,rows,_,notes=candidates[0];candidates[0]=(base,rows,False,notes)
            warnings.append('Old authorization differs between native pairs; Full will initialize Free rather than restore an ambiguous entitlement.')
    base=candidates[0][0]
    if base==legacy:
        start=base+8192+(0 if cfg['board_id']==4 else 4096)
        if capture.read(start,0x100000-start)!=b'\xff'*(0x100000-start):
            warnings.append('Unrecognized old settings tail will be cleared by the existing Full range; protected boot/CPFT and runtime MBR remain preserved.')
    return base,candidates[0][2],warnings


def require_authorization_consistency(pair,selected):
    """Contain native SDK CRC filtering before interpreting a missing tag.

    The authenticated SDK remains the value/CRC reader. This fixed native
    pair walk only checks occupancy and SDK-selection consistency; it never
    recovers a filtered value, constructs a token or grants authorization.
    Ambiguous/deleted legacy authorization is not proven pristine Free.
    """
    if len(pair)!=8192:
        raise ValueError('unrecognized native NVDS authority header; refused before erase')
    # Native reclamation can swap the two physical sectors. Their markers,
    # not lower/higher addresses, identify live append and packed backup roles.
    # The SDK above must already have opened this exact pair read-only.
    sectors={}
    for offset in (0,4096):
        sector=pair[offset:offset+4096];magic=bytes(sector[:4])
        if sector==b'\xff'*4096:continue
        if (magic not in (b'NVDS',b'BKUP') or magic in sectors or
                sector[4:256]!=b'\xff'*252):
            raise ValueError('unrecognized native NVDS authority header; refused before erase')
        sectors[magic]=sector
    if b'NVDS' not in sectors:
        raise ValueError('native live NVDS authority sector missing; refused before erase')
    authorities=[]
    for magic in (b'NVDS',b'BKUP'):
        sector=sectors.get(magic)
        if sector is None:
            authorities.append([]);continue
        packed=magic==b'BKUP'
        cursor=256;records=[]
        while cursor<4096:
            if sector[cursor:]==b'\xff'*(4096-cursor):break
            # SDK live appends round up to a page. Reclaimed backup groups
            # pack headers/values contiguously, then pad to the next page.
            # A packed record can cross a page boundary.
            if packed and sector[cursor]==255:
                end=((cursor+255)//256)*256
                if end==cursor or sector[cursor:end]!=b'\xff'*(end-cursor):
                    raise ValueError('ambiguous/torn native backup padding; refused before erase')
                cursor=end;continue
            header=sector[cursor:cursor+8]
            if len(header)!=8:raise ValueError('truncated native NVDS authority header')
            _,tag,length,generation=struct.unpack('<BBHI',header)
            value_end=cursor+8+length
            end=value_end if packed else ((value_end+255)//256)*256
            if (not 1<=tag<=255 or not 0<=length<=504 or generation==0 or end>4096 or
                    sector[value_end:end]!=b'\xff'*(end-value_end)):
                raise ValueError('ambiguous/torn native NVDS authority; refused before erase')
            value=sector[cursor+8:value_end]
            if tag==111:records.append(value)
            elif value.startswith(b'IAC1'):
                raise ValueError('native authorization ownership is damaged; refused before erase')
            cursor=end
        authorities.append(records)
    live,backup=authorities
    if selected is None:
        if live or backup:
            raise ValueError('SDK missing authorization is not proven empty: native records remain; refused before erase')
        return
    if not live and not backup:
        raise ValueError('SDK authorization lacks a corresponding native record')
    # SDK prefers the last matching live record, otherwise the last matching
    # packed backup record. Generation maxima across banks are not authority.
    # CRC remains SDK-owned: a filtered latest record must never silently fall
    # back to an older authorization or to Free.
    if (live or backup)[-1]!=selected:
        raise ValueError('native latest authorization differs from SDK selection; refused before erase')

def prepare_bound(bound,program,cfg,capture):
    require_family(cfg,bound[0]);capture.require(required_ranges());capacity_from_result(capture.unit.before)
    old_base,authorization_allowed,warnings=selected_source(program,capture,cfg);source=capture._ram_projection()
    address=address_from_result(capture.unit.before)
    activation_id=None
    if cfg['board_id']==4:
        from activation_preservation import derive_device_id
        # The actual App's HS root is 'HS' + repeated Flash UID padded to16.
        # Its activation epoch remains0, independently of OTA epoch1.
        activation_id=derive_device_id(0x0201,1,b'HS'+capture.unit.before[16:32])
    tail,audit=prepare_selected(program,source,cfg,old_base,immutable_address=address,
                               activation_device_id=activation_id,
                               authorization_allowed=authorization_allowed,warnings=warnings)
    maps=compose_review_images(bound,cfg,tail,source=source)
    plan=compile_plan(bound,cfg,source,maps);expected=expected_stages(plan)['factory-device-info']
    # Existing App/Recovery v1 retained mailbox ABI, not arbitrary SRAM.
    retained_start,retained_end=0x20001fc0,0x20002000
    retained_descriptor={'vaddr':retained_start,'size':64}
    if retained_descriptor not in program.meta['zero_ranges']:
        raise ValueError('selected App lacks the fixed host-replay retained descriptor')
    for item in [*program.meta['segments'],*program.meta['zero_ranges']]:
        # Host replay initializes all NOBITS, including this explicit noinit
        # section. It is not a target startup zero-table entry or a loader.
        if item==retained_descriptor:continue
        at=item['vaddr'];end=at+item['size']
        if 0x200000<=at<0x220000:
            at+=0x1fe00000;end+=0x1fe00000
        if at<retained_end and retained_start<end:
            raise ValueError('selected App does not exclude the fixed retained history window')
    covered=set()
    for part in capture.parts:covered.update(range(part.address,part.address+part.length))
    replaced=set(range(0x4000,0x80000))|set(range(0x82000,0x100000))
    if len(covered|replaced)!=0x100000:raise ValueError('uncaptured unaffected extent')
    audit['executionProjectionSHA256']=audit.pop('source_sha256')
    return plan,expected,{**audit,'sourceCoverage':capture.manifest(),'oldUserStateImported':False,
        'oldRecoveryStateRead':False,'RecoveryPagesInitialized':2,'selectedEpoch':bound[1]['compatibilityEpoch'],
        'physicalCapacityBytes':0x100000,'scope':'mcu/main-flash',
        'externalFlashAccessed':False,'externalFlashModified':False,
        'retainedHistoryMailbox':{'address':retained_start,'bytes':44,
            'bootCauseClearance':'mandatory-complete-power-cycle','postBootHistoryVerified':False},
        'externalImageDataPreserved':cfg['board_id']==2,'physicalExecutionQualified':False}

def prepare(base,config,directory,output):
    from intelhex import IntelHex
    cfg=configuration(config)
    bound=inspect(base);program=authenticated_program(bound)
    plan,expected,audit=prepare_bound(bound,program,cfg,capture_from_directory(directory))
    out=Path(output);out.mkdir(parents=True,exist_ok=False);paths={};hashes={}
    for stage in plan.stages:
        image=IntelHex()
        for segment in stage.segments:image.puts(segment.address,segment.data)
        path=out/(stage.name+'.hex');image.write_hex_file(str(path));paths[stage.name]=str(path.resolve());hashes[stage.name]=sha(path.read_bytes())
    (out/'expected.bin').write_bytes(expected)
    result={**audit,'plan':ledger(plan),'planSha256':plan.seal,'images':paths,'imageSha256':hashes,
        'expectedSha256':sha(expected),'expected':str((out/'expected.bin').resolve())}
    (out/'preparation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result

def verify_initialization(expected,expected_sha256,directory,cfg):
    """Mandatory new native pages even when ordinary readback is skipped."""
    image=local_file(expected,0x100000)
    if sha(image)!=expected_sha256:raise ValueError('expected image changed')
    base=0xfe000 if cfg['board_id']==4 else 0xfd000
    ranges=[(0x82000,8192)]+[(a,min(8192,0x100000-a)) for a in range(base,0x100000,8192)]
    for at,count in ranges:
        if local_file(Path(directory)/f'init-{at:06x}.bin',count)!=image[at:at+count]:
            raise ValueError('new Recovery/Settings verification failed; no success or replay')
    return {'nativeInitializationVerified':True,'wholeReadbackVerified':False}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('describe','prepare','verify-initialization'))
    for name in ('base','config'):p.add_argument('--'+name,required=True)
    for name in ('capture-dir','out'):p.add_argument('--'+name)
    p.add_argument('--expected-sha256')
    a=p.parse_args();cfg=configuration(a.config)
    if a.action=='describe':
        import unicorn,intelhex
        if unicorn.__version__!='2.1.4':raise ValueError('tested Unicorn 2.1.4 required before device access')
        bound=inspect(a.base);authenticated_program(bound);require_family(cfg,bound[0])
        print(json.dumps({'family':cfg['firmware_family'],'board':cfg['board_marking'],
            'scope':'mcu/main-flash','externalFlashAccessed':False,'externalFlashModified':False,
            'externalImageDataPreserved':cfg['board_id']==2,
            'clears':'main-Flash user settings, password/QR, Find, fonts/images and crash/health history; valid same-unit Pro preserved; external images retained',
            'chunks':[{'offset':at,'length':n,'file':f'source-{at:06x}.bin'} for at,n in chunks()]}))
    elif a.action=='prepare':
        if not a.capture_dir or not a.out:p.error('--capture-dir and --out required')
        print(json.dumps(prepare(a.base,a.config,a.capture_dir,a.out)))
    else:
        if not a.capture_dir or not a.out or not a.expected_sha256:p.error('--capture-dir, --out, --expected-sha256 required')
        print(json.dumps(verify_initialization(a.out,a.expected_sha256,a.capture_dir,cfg)))

if __name__=='__main__':main()
