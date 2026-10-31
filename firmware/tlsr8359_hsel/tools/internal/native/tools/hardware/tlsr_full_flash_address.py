"""Exact TLSR Factory mapping for explicit Full, never a boot migration."""
import argparse,json
from pathlib import Path
from ble_factory_address import decode,derive,encode,validate

ADDRESS=0x7f160
BYTES=16
CAPABILITY=b'INKTAG-TL-ADDR1R\0'

def selector(record: bytes, *, unconfigured: bool = False):
    """Current checksummed ITB1, including the required-typed flag."""
    h=2166136261
    for byte in record[:8]:h=((h^byte)*16777619)&0xffffffff
    flags=int.from_bytes(record[6:8],'little')
    if (len(record)!=12 or record[:5]!=b'ITB1\1' or
            not (record[5] in range(1,11) or (unconfigured and record[5]==0 and flags==4))
            or flags & ~7 or (flags & 1 and record[5]!=2)
            or int.from_bytes(record[8:],'little')!=h):
        raise ValueError('Unknown/damaged exact-board Factory selector')
    return record[5],flags

def preservation_selector(record: bytes, *, unconfigured: bool = False):
    """Old selector may identify immutable data only, never user/App compatibility."""
    if record[:5] == b'ITBD\1':
        # Recheck the old checksum before normalizing only the pure validation
        # input. No caller writes this translated record or reads old users.
        from tlsr_factory_config import fnv
        if len(record)!=12 or fnv(record[:8])!=int.from_bytes(record[8:],'little'):
            raise ValueError('Damaged historical Factory selector')
        body=b'ITB1'+record[4:8]
        record=body+fnv(body).to_bytes(4,'little')
    return selector(record,unconfigured=unconfigured)

def typed_pair(application: bytes, recovery: bytes) -> bool:
    app,rec=CAPABILITY in application,CAPABILITY in recovery
    if app!=rec:raise ValueError('Full requires matching typed-address App and Recovery; old peer refused')
    return app

def slot(factory: bytes):
    if len(factory)!=4096:raise ValueError('Complete current Factory page required')
    data=factory[0x160:0x170]
    if data==b'\xff'*16:return None
    if data[11:]!=b'\xff'*5:raise ValueError('Unknown/partial typed address padding; no overwrite')
    return decode(data[:11])

def legacy(stored: bytes, uid: bytes) -> bytes:
    if len(stored)!=8:raise ValueError('Exactly8 legacy identity bytes required')
    if stored[:6]!=b'\xff'*6:return validate(stored[:6][::-1],0)
    check_uid(uid)
    h=2166136261
    for byte in uid:h=((h^byte)*16777619)&0xffffffff
    return validate((h.to_bytes(4,'little')[:3]+b'\x38\xc1\xa4')[::-1],0)

def check_uid(uid: bytes):
    if len(uid)!=16 or uid in (bytes(16),b'\xff'*16,b'\x51\x01'*8):
        raise ValueError('Erased MAC needs qualified same-unit Flash UID; no selector fallback')

def check_stock_selector(factory: bytes):
    if len(factory)!=4096:raise ValueError('Complete current Factory page required')
    record=factory[0x100:0x10c]
    if any(record[:4]==magic or any(record==magic[:cut]+b'\xff'*(12-cut) for cut in range(1,4))
           for magic in (b'ITBD',b'ITB1')):
        raise ValueError('Installed/partial Factory selector cannot fall back to stock')


def complete_source(factory: bytes) -> tuple[str, list[str]]:
    """Locate factory data for complete flashing, without admitting an old App.

    A retired/damaged board selector is optional here. Independent typed-address
    or owned configuration envelopes still locate installed Factory data. With
    no installed footprint the bounded stock calibration capture is required;
    opaque occupied relocation destinations are not treated as user settings.
    Other callers keep their strict selector/boot contract.
    """
    if len(factory) != 4096:
        raise ValueError('Complete 4-KiB Factory evidence required')
    record = factory[0x100:0x10c]
    try:
        preservation_selector(record)
    except ValueError:
        pass
    else:
        return ('layout-v1' if record[:4] == b'ITB1' else 'installed'), []
    try:
        typed = slot(factory)
    except ValueError:
        typed = None
    owned = (record[:4] in (b'ITB1', b'ITBD') or
             (factory[0x110:0x114] == b'IPN1' and factory[0x12c:0x130] == b'PCM1') or
             (factory[0x140:0x144] == b'ISC1' and factory[0x15c:0x160] == b'SCM1'))
    if typed is not None or owned:
        return 'installed', ['Old Factory selector is missing/unrecognized/damaged; '
                             'rebuild configuration from the selected package. '
                             'Installed identity/calibration mapping is retained.']
    return 'stock', ['No recognized installed Factory mapping; read the bounded '
                     'stock identity/calibration map before preparation.']


def select_complete(factory: bytes, stored: bytes, source: str, uid: bytes):
    """Independent address admission for a confirmed complete-flash operation.

    Never require an optional prior selector or revive an invalid typed record.
    Preserve a valid typed address/type, otherwise use the already qualified
    legacy-public or hardware-UID derivation. Conflicting valid identities still
    stop; a failed read is never passed here as erased bytes.
    """
    if len(factory) != 4096 or len(stored) != 8:
        raise ValueError('Bounded current Factory and legacy identity required')
    if source not in ('stock', 'installed', 'layout-v1'):
        raise ValueError('Explicit source family required')
    if source == 'stock' and factory[0x160:0x170] != b'\xff' * 16:
        raise ValueError('Stock bytes occupy the typed-address relocation destination at 0x7F160; no established preservation path')
    warnings = []
    existing = None
    if source != 'stock':
        try:
            existing = slot(factory)
        except ValueError:
            warnings.append('Old typed BLE record is invalid; it is not restored. '
                            'Use the verified legacy/UID initialization path.')
        else:
            if existing is None:
                warnings.append('Old typed BLE record is absent; initialize it from the verified legacy/UID path.')
    legacy_address = None
    if stored[:6] != b'\xff' * 6:
        try:
            legacy_address = validate(stored[:6][::-1], 0)
        except ValueError:
            warnings.append('Old legacy BLE address is invalid; it is not restored.')
    if existing is not None:
        address, kind = existing
        if legacy_address is not None and legacy_address != address:
            raise ValueError('Two valid Factory BLE identities conflict; cannot select this unit address safely')
    elif legacy_address is not None:
        address, kind = legacy_address, 0
        # Recover the established static-random type only when it exactly
        # matches this unit's qualified UID algorithm, never from top bits alone.
        check_uid(uid)
        if derive(0x0301, uid)[0] == address:
            kind = 1
    else:
        check_uid(uid)
        historical_public = False
        if source != 'stock':
            try:
                _, flags = preservation_selector(factory[0x100:0x10c])
                historical_public = not flags & 4
            except ValueError:
                pass
        address, kind = (legacy(b'\xff' * 8, uid), 0) if historical_public else derive(0x0301, uid)
        warnings.append('No valid stored BLE address; initialize from this unit verified Flash UID.')
    return dict(address=address.hex(':').upper(), address_type=kind,
                legacy_hex=address[::-1].hex(), typed_record_hex=(encode(address, kind)+b'\xff'*5).hex(),
                typed_record_existing=existing is not None, warnings=warnings)

def select(factory: bytes, stored: bytes, source: str, uid: bytes, typed: bool,
           *, explicit_unconfigured: bool = False):
    if len(factory)!=4096 or len(stored)!=8:raise ValueError('Bounded current Factory and legacy identity required')
    if source not in ('stock','installed','layout-v1'):raise ValueError('Explicit source family required')
    if explicit_unconfigured and source=='stock':
        raise ValueError('Explicit unconfigured identity must already be typed; no stock/UID fallback')
    if source=='stock':check_stock_selector(factory)
    existing=slot(factory) if source!='stock' else None
    if source!='stock':
        board,flags=preservation_selector(factory[0x100:0x10c],unconfigured=explicit_unconfigured)
        if explicit_unconfigured and board!=0:
            raise ValueError('Explicit unconfigured intent requires exactly selector0+4')
        if flags & 4 and existing is None:
            raise ValueError('Required typed address is missing; no legacy fallback')
    if existing:
        if not typed:raise ValueError('Typed installed identity requires updated App and Recovery')
        address,kind=existing
        if address[::-1]!=stored[:6]:raise ValueError('Typed and legacy identity bytes differ')
    elif not typed:
        address,kind=legacy(stored,uid),0
    else:
        if factory[0x160:0x170]!=b'\xff'*16:raise ValueError('Stock data occupies typed address destination')
        if stored[:6]!=b'\xff'*6 or source!='stock':
            address,kind=legacy(stored,uid),0
        else:
            if factory[:6]!=b'\xff'*6:
                raise ValueError('Unknown current Factory identity; new-device absence is not confirmed')
            check_uid(uid)
            address,kind=derive(0x0301,uid)
    record=encode(address,kind)+b'\xff'*5 if typed else None
    return dict(address=address.hex(':').upper(),address_type=kind,
                legacy_hex=address[::-1].hex(),typed_record_hex=record.hex() if record else None,
                typed_record_existing=existing is not None)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--factory',type=Path,required=True)
    p.add_argument('--stored',type=Path,required=True)
    p.add_argument('--stored-offset',type=lambda v:int(v,0),required=True)
    p.add_argument('--source',required=True)
    p.add_argument('--uid',required=True)
    p.add_argument('--typed',choices=('0','1'),required=True)
    a=p.parse_args()
    try:
        if a.factory.is_symlink() or a.stored.is_symlink():raise ValueError('Regular evidence files required')
        data=a.stored.read_bytes();at=a.stored_offset
        factory=a.factory.read_bytes()
        if len(factory)==0x80000:factory=factory[0x7f000:]
        print(json.dumps(select(factory,data[at:at+8],a.source,bytes.fromhex(a.uid),a.typed=='1')))
    except (ValueError,OSError) as error:p.error(str(error))
