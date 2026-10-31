"""Fixed complete Full coverage; no old user-state/floor migration or pre-read."""
from __future__ import annotations
import io,json
from pathlib import Path
from tlsr_full_flash_state import BASE_BYTES,PAGE,SETTINGS,FLOOR,FACTORY,sha,floor_seed

BASELINE='inktag-release-v1'


def make_plan(template: bytes, factory: bytes, state: dict, uid: str, *, capacity: int, layout: str = 'layout-v1'):
    if len(template)!=BASE_BYTES or len(factory)!=PAGE or capacity not in (BASE_BYTES,2*BASE_BYTES):
        raise ValueError('Exact Full template/Factory/qualified capacity required')
    image=bytearray(template+b'\xff'*(capacity-BASE_BYTES));image[FACTORY:BASE_BYTES]=factory
    rows=[]
    def add(start,data,kind):
        image[start:start+len(data)]=data
        rows.append(dict(start=start,bytes=len(data),kind=kind,sha256=sha(data)))
    for start,data,kind in state['writes']:add(start,data,kind)
    add(0,template[:0x51000],'firmware')
    add(0x51000,b'\xff'*0x8000,'font-clear')
    add(0x5d000,b'\xff'*0x22000,'images-clear')
    if capacity>BASE_BYTES:add(BASE_BYTES,b'\xff'*BASE_BYTES,'high-images-clear')
    add(FACTORY,factory,'factory')
    plan=dict(format_version=1,required_baseline=BASELINE,layout=layout,capacity=capacity,
        image_sha256=sha(image),segments=rows,uid=uid.upper(),clear_all=True,
        candidate_epoch=state['epoch'],written_bytes=sum(r['bytes'] for r in rows),
        verification_coverage='complete Full image; Factory identity/calibration preserved in prepared bytes')
    validate_plan(plan,bytes(image))
    return bytes(image),plan


def validate_plan(plan: dict,image: bytes):
    if (type(plan) is not dict or type(plan.get('format_version')) is not int or plan['format_version']!=1 or
            plan.get('required_baseline')!=BASELINE or plan.get('layout') != 'layout-v1' or
            type(plan.get('capacity')) is not int or plan['capacity'] not in (BASE_BYTES,2*BASE_BYTES) or
            len(image)!=plan['capacity'] or plan.get('image_sha256')!=sha(image) or plan.get('clear_all') is not True or
            type(plan.get('candidate_epoch')) is not int or not 0<=plan['candidate_epoch']<=65535):
        raise ValueError('Invalid complete Full image/plan')
    from tlsr_full_flash_address import check_uid
    check_uid(bytes.fromhex(plan.get('uid','')))
    expected={'firmware':(0,0x51000),'font-clear':(0x51000,0x8000),'images-clear':(0x5d000,0x22000),
        'settings-generation':(SETTINGS,PAGE),'settings-clear':(SETTINGS+PAGE,PAGE),
        'security-floor':(FLOOR,PAGE),'recovery-state-clear':(FLOOR+PAGE,PAGE),'factory':(FACTORY,PAGE)}
    if len(image)>BASE_BYTES:expected['high-images-clear']=(BASE_BYTES,BASE_BYTES)
    rows=plan.get('segments')
    if type(rows) is not list or len(rows)!=len(expected):raise ValueError('Incomplete Full coverage')
    seen=set();total=0
    for row in rows:
        if type(row) is not dict or set(row)!={'start','bytes','kind','sha256'}:
            raise ValueError('Unknown Full span fields')
        kind=row['kind'];at=row['start'];n=row['bytes']
        if (type(kind) is not str or kind not in expected or kind in seen or type(at) is not int or type(n) is not int or
                (at,n)!=expected[kind] or row['sha256']!=sha(image[at:at+n])):
            raise ValueError('Invalid/changed/duplicated Full span')
        if kind.endswith('-clear') and image[at:at+n]!=b'\xff'*n:
            raise ValueError('Full clear region is not empty')
        seen.add(kind);total+=n
    if total!=len(image) or total!=plan.get('written_bytes'):
        raise ValueError('Full does not cover the complete actual writable image')
    if image[FLOOR:FLOOR+PAGE]!=floor_seed(plan['candidate_epoch']):
        raise ValueError('Full must initialize current Recovery state at generation1')


def load_bound_plan(path: Path, image: bytes) -> dict:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 16384:
        raise ValueError('Full plan must be a bounded regular file')
    plan=json.loads(path.read_text(encoding='utf-8'))
    validate_plan(plan,image)
    # Repeat the same package/config trust gate at the actual programmer
    # entry, not only in a preceding planner whose JSON could be replaced.
    from tlsr_full_flash_base import configure
    template,review=configure(Path(plan['base_package']),Path(plan['configuration']))
    if (image[:0x51000] != template[:0x51000] or review['board_id'] != plan['board_id'] or
            review['layout'] != plan['layout'] or
            review['compatibility_epoch'] != plan['candidate_epoch'] or review['board'] != plan['board'] or
            review['base_package_sha256'] != plan['base_package_sha256'] or
            review['configuration_sha256'] != plan['configuration_sha256']):
        raise ValueError('Full plan differs from the accepted signed firmware/config pair')
    for start,length in ((0x7F100,13),(0x7F110,80)):
        if image[start:start+length] != template[start:start+length]:
            raise ValueError('Full Factory defaults differ from explicit Config')
    from tlsr_full_flash_state import defaults, encode_settings
    factory=image[FACTORY:BASE_BYTES]
    seeds,_=defaults(factory[:6],bytes.fromhex(review['screen_wire_hex']),
        bytes.fromhex(review['pin_wire_hex']),bytes.fromhex(review['aux_wire_hex']),review['board_id'])
    from tlsr_full_flash_entitlement import bound_capture
    activation, _, _, _ = bound_capture(plan)
    if activation is not None:
        seeds[8] = activation
    if image[SETTINGS:FLOOR]!=encode_settings(1,seeds)+b'\xff'*PAGE:
        raise ValueError('Full must use generation1 defaults plus only the original same-device verified Pro')
    from tlsr_full_flash_config import ROOT
    from tlsr_flash_jedec import FLASH_BYTES
    registry=json.loads((ROOT/'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards'][review['board']]
    observed=plan.get('observed_jedec')
    if observed not in FLASH_BYTES or FLASH_BYTES[observed] < plan['capacity']:
        raise ValueError('Full span exceeds the observed supported Flash part')
    if plan['capacity'] > BASE_BYTES and not (
            plan['clear_all'] and review['board_id']==2 and registry.get('physicalFlashSizeBytes')==1048576 and
            registry['firstFlash']['flashJedecId'].upper()==observed):
        raise ValueError('Upper Flash lacks independent exact-board qualification')
    return plan



def execute(backend,serial_port,original_write,image: bytes,plan: dict,readback,*,skip_readback=False,progress=None,write_completed=None):
    """One complete write, then readback. Plan spans describe ownership only."""
    validate_plan(plan,image)
    print(f'FULL WRITE: one {len(image)//1024}-KiB image; no interleaved verification.',flush=True)
    stream=io.BytesIO(image)
    if progress:stream=progress(stream,len(image))
    if not original_write(serial_port,stream,0,len(image),True):return False
    if progress:stream.finish()
    if write_completed:write_completed()
    print(f'FULL WRITE COMPLETE: {len(image)} bytes; NOT VERIFIED yet.',flush=True)
    if not skip_readback:
        print(f'FULL READBACK: one {len(image)//1024}-KiB read; Ctrl+C stops verification, not another write.',flush=True)
        backend.InkTagSetExpectedReadback(image)
        if not backend.FlashReadBlock(serial_port,readback,0,len(image)):return False
    else:
        print('FULL READBACK SKIPPED: NOT VERIFIED; checking only new Recovery/Settings pages.',flush=True)
        for row in plan['segments']:
            if row['kind'] not in ('settings-generation','settings-clear','security-floor','recovery-state-clear'):continue
            at=row['start'];data=image[at:at+row['bytes']]
            backend.InkTagSetExpectedReadback(data);actual=io.BytesIO()
            if not backend.FlashReadBlock(serial_port,actual,at,len(data)) or actual.getvalue()!=data:return False
    return True
