"""Bounded offline nRF52811 HW52 Full bundle. No transport or device access.

The signed init ABI is SDK17 dfu-cc.proto; verify the exact InitCommand bytes,
native little-endian P256 key/signature and reversed SHA256 just as Recovery.
This is a host container, not a new OTA format or device qualification.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import runpy
import stat
import sys
import struct
import tempfile
import zipfile
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils


ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from board_config import load_board_config
from programmer_codec.models import ScreenConfig
from programmer_codec.protocol import encode_screen_config
from programmer_codec.generated_commands_v2 import ScreenControllerV2, ScreenColorV2
from programmer_codec.pin_config import encode_pin_block
from programmer_codec.safe_archive import OtaArchive
HEX = runpy.run_path(str(ROOT/'validate-full.py'))
PUBLIC_KEY_LE = bytes.fromhex(
    '0edf3d346f2be4a0a641930c3bba0f21fff2bf1913ed68d1a81b47a87e0bf9ae'
    'c2a2697fc77f10daba686704f3323fed849591ec9539b288ae5be98f221fe7d1')
MEMBERS = {'base.json': 4096, 'full.hex': 1500000, 'app-ota.zip': 1048576, 'gui-assets.bin': 12288}
FACTORY_PIN_MARKER=b'N11PINCFG1\0'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_archive(data, limits=None):
    """Apply shared pre-allocation ZIP checks, then exact native limits.

    The shared offline reader accepts a path. Stage only the bounded input;
    never extract member paths or replace native signature/layout checks.
    """
    if type(data) is not bytes or not 0 < len(data) <= 2097152:
        raise ValueError('Invalid Full/OTA ZIP size')
    try:
        with tempfile.TemporaryDirectory(prefix='nrf11-archive-') as temporary:
            path = Path(temporary)/'input.zip'
            path.write_bytes(data)
            return _read_archive(path, limits)
    except (zipfile.BadZipFile, RuntimeError, KeyError) as error:
        raise ValueError('Invalid Full/OTA ZIP') from error


def _read_archive(path, limits):
    with OtaArchive(path) as archive:
        entries = archive.infolist()
        if len(entries) != (len(limits) if limits else 3) or len({e.filename for e in entries}) != len(entries):
            raise ValueError('ZIP requires three unique members')
        if limits is not None and {e.filename for e in entries} != set(limits):
            raise ValueError('Full ZIP requires exactly the declared Full members')
        for e in entries:
            limit = (limits[e.filename] if limits is not None else
                     4096 if e.filename == 'manifest.json' else
                     512 if e.filename.endswith('.dat') else 69632)
            if (e.flag_bits & 1 or e.is_dir() or stat.S_ISLNK(e.external_attr >> 16)
                    or '/' in e.filename or '\\' in e.filename or e.filename in ('.', '..')
                    or not 0 < e.file_size <= limit):
                raise ValueError('Invalid/encrypted/oversized ZIP member')
        if limits is None:
            archive.manifest()
        return {e.filename: archive.read(e) for e in entries}


def varint(data, offset):
    value = 0
    for shift in range(0, 35, 7):
        if offset >= len(data):
            break
        byte = data[offset]; offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError('Invalid bounded Nordic init varint')


def fields(data):
    """Only the SDK init's varint/length-delimited subset, no generic decoder."""
    if type(data) is not bytes or len(data)>512:
        raise ValueError('Invalid Nordic init field type/size')
    result, offset = {}, 0
    while offset < len(data):
        tag, offset = varint(data, offset)
        key, wire = tag >> 3, tag & 7
        if not key or key in result or wire not in (0, 2):
            raise ValueError('Duplicate/unsupported Nordic init field')
        value, offset = varint(data, offset)
        if wire == 2:
            if value > len(data) - offset:
                raise ValueError('Truncated Nordic init field')
            value, offset = data[offset:offset + value], offset + value
        result[key] = value
    return result


def signed_app(ota_data):
    files = read_archive(ota_data)
    manifest = checked_json(files['manifest.json'])
    root = manifest['manifest']
    if set(root) != {'application'}:
        raise ValueError('Nordic OTA must be application-only')
    item = root['application']; bn, dn = item['bin_file'], item['dat_file']
    if bn == dn or not bn.endswith('.bin') or not dn.endswith('.dat') or set(files) != {'manifest.json', bn, dn}:
        raise ValueError('Unknown Nordic OTA members')
    app, packet = files[bn], fields(files[dn])
    if set(packet) != {2}:
        raise ValueError('Signed Nordic init required')
    signed = fields(packet[2])
    if set(signed) != {1, 2, 3} or signed[2] != 0 or len(signed[3]) != 64:
        raise ValueError('ECDSA-P256 Nordic init required')
    command = fields(signed[1])
    if set(command) != {1, 2} or command[1] != 1:
        raise ValueError('INIT command required')
    init = fields(command[2]); digest = fields(init[8])
    if (set(init) - set(range(1, 11)) or init.get(2) != 52 or init.get(4, 0) != 0
            or init.get(5, 0) or init.get(6, 0) or init.get(7) != len(app)
            or not 8 <= len(app) <= 69632 or init.get(9, 0)
            or init.get(3) != b'\xa6\x02' or set(digest) != {1, 2}
            or digest[1] != 3 or digest[2] != hashlib.sha256(app).digest()[::-1]
            or type(init.get(1)) is not int or not 0 <= init[1] <= 0xffffffff or not init[1] & 65535):
        raise ValueError('Nordic signed App HW52/epoch/hash/size context mismatch')
    validation = fields(init[10])
    if validation != {1: 1, 2: b''}:
        raise ValueError('Nordic generated-CRC boot validation required')
    raw = signed[3]
    key = ec.EllipticCurvePublicNumbers(int.from_bytes(PUBLIC_KEY_LE[:32], 'little'),
        int.from_bytes(PUBLIC_KEY_LE[32:], 'little'), ec.SECP256R1()).public_key()
    try:
        key.verify(utils.encode_dss_signature(int.from_bytes(raw[:32], 'little'),
                   int.from_bytes(raw[32:], 'little')), command[2], ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError) as error:
        raise ValueError('Nordic App signature verification failed') from error
    return app, init[1]



import zlib
import tomllib

def fnv(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value

def checked_json(data):
    def pairs(items):
        result={}
        for key,value in items:
            if key in result:raise ValueError('Duplicate JSON member')
            result[key]=value
        return result
    return json.loads(data,object_pairs_hook=pairs)

def verify_components(full_data,ota_data,assets):
    if not 0<len(full_data)<=1500000:raise ValueError('Full image size invalid')
    with tempfile.TemporaryDirectory(prefix='nrf11-verify-') as temporary:
        p=Path(temporary)/'full.hex';p.write_bytes(full_data)
        memory=HEX['parse_hex'](p)
    HEX['validate_assets'](assets)
    if any(a>=0x30000 for a in memory):raise ValueError('Full must not supply UICR/FICR')
    size,sd_size=HEX['validate'](memory,assets)
    app,version=signed_app(ota_data)
    if version>>16<1 or app.count(FACTORY_PIN_MARKER)!=1:raise ValueError('N1P1/N1R1 requires signed pin-contract App epoch1 or higher')
    image=bytes(memory.get(a,255) for a in range(0x30000))
    if (len(app)!=size or image[0x19000:0x19000+size]!=app or
        struct.unpack_from('<I',image,0x2f008)[0]!=version or struct.unpack_from('<I',image,0x2f00c)[0]!=0):
        raise ValueError('Full/OTA application and version disagree')
    end=max(a for a in memory if 0x2a000<=a<0x2e000)+1
    if not 0<end-0x2a000<=16384:raise ValueError('Recovery outside 16KiB')
    return memory,dict(appBytes=size,appLimit=69632,appHeadroom=69632-size,
        appSha256=sha(app),appCrc32=f'{zlib.crc32(app)&0xffffffff:08X}',
        recoveryBytes=end-0x2a000,recoveryLimit=16384,recoveryHeadroom=0x2e000-end,
        recoverySha256=sha(image[0x2a000:end]),version=version,compatibilityEpoch=version>>16,
        signatureVerified=True,recoveryTrust='hash integrity only; no separate Recovery publisher signature',
        hardwareTested=False,factoryDisplayPins=app.count(FACTORY_PIN_MARKER)==1)

def verify_bundle(path):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size>2097152:raise ValueError('Invalid Full ZIP')
    data=read_archive(path.read_bytes(),MEMBERS)
    manifest=checked_json(data['base.json'])
    expected=dict(format_version=1,mcu='nrf52811',firmware_family='aesl22_nrf',
        backend_ids=['nrf52811_asel22'],hw_version=52,config_schema=1,
        baseline='inktag-release-v1',layout_version=1,layout_marker='N1V1',factory_format='N1P1',user_format='N1R1')
    if type(manifest) is not dict or set(manifest)!=set(expected)|{'images','compatibility_epoch'} or any(type(manifest[k]) is not type(v) or manifest[k]!=v for k,v in expected.items()):
        raise ValueError('Wrong nRF52811 Full format/identity')
    if manifest['images']!={n:dict(bytes=len(v),sha256=sha(v)) for n,v in data.items() if n!='base.json'}:
        raise ValueError('Full member binding mismatch')
    memory,review=verify_components(data['full.hex'],data['app-ota.zip'],data['gui-assets.bin'])
    if type(manifest['compatibility_epoch']) is not int or manifest['compatibility_epoch']!=review['compatibilityEpoch']:
        raise ValueError('Full epoch differs from signed native DAT')
    review.update(packageSha256=sha(path.read_bytes()),packageVerified=True)
    return memory,data,review

def require_release_binding(app_hash,recovery_hash,assets_hash):
    # Internal producer authority; deliberately not required by end-user verification.
    accepted=checked_json((ROOT/'nrf11_release_artifacts.json').read_bytes())
    if accepted.get('baseline')!='inktag-release-v1' or accepted.get('target')!='aesl22_nrf':
        raise ValueError('Missing source-owned release acceptance')
    for row in accepted['acceptedArtifacts']:
        if row.get('appSha256')==app_hash and row.get('recoverySha256')==recovery_hash and row.get('guiAssetsSha256')==assets_hash:
            if all(type(row.get(k)) is str and len(row[k])==n and all(c in '0123456789abcdef' for c in row[k]) for k,n in [('sourceCommit',40),('compilerClosureSha256',64),('reviewCommit',40)]):return row
    raise ValueError('App/Recovery pair has no reviewed v1 source/compiler binding')

def create_bundle(full,ota,assets,output):
    files={'full.hex':Path(full).read_bytes(),'app-ota.zip':Path(ota).read_bytes(),'gui-assets.bin':Path(assets).read_bytes()}
    _,review=verify_components(files['full.hex'],files['app-ota.zip'],files['gui-assets.bin'])
    require_release_binding(review['appSha256'],review['recoverySha256'],sha(files['gui-assets.bin']))
    manifest=dict(format_version=1,mcu='nrf52811',firmware_family='aesl22_nrf',backend_ids=['nrf52811_asel22'],hw_version=52,config_schema=1,
        baseline='inktag-release-v1',layout_version=1,layout_marker='N1V1',factory_format='N1P1',user_format='N1R1',compatibility_epoch=review['compatibilityEpoch'],
        images={n:dict(bytes=len(v),sha256=sha(v)) for n,v in files.items()})
    files['base.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    output=Path(output)
    with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,payload in sorted(files.items()):
            info=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,payload)
    return verify_bundle(output)[2]

SCREEN=struct.pack('<BBBBBHHHHHH',1,32,1,2,3,128,250,0,0,122,250)
FACTORY_PINS=bytes((20,19,6,5,4,3,2,16,17,18,7,1,1))

def read_config(path):
    path=Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size>16384:raise ValueError('Invalid Config')
    value=load_board_config(path)
    def exact(data,keys,label):
        if type(data) is not dict or set(data)!=set(keys):raise ValueError('Unknown or missing '+label)
    exact(value,('format_version','board_marking','note','screen','gpio'),'Config fields')
    if (type(value['format_version']) is not int or value['format_version']!=1 or
        value['board_marking']!='EL022HWRL' or value['note']!='CAL_VER06'):
        raise ValueError('Unknown PCB/revision or schema')
    screen=value['screen'];geometry=('width','height','visible_x','visible_y','visible_width','visible_height')
    exact(screen,('ic','recipe','color','rotation','mirror_x','mirror_y','native_ui_scale',*geometry),'screen')
    if (type(screen['rotation']) is not int or screen['rotation'] not in (0,90,180,270) or
        any(type(screen[k]) is not bool for k in ('mirror_x','mirror_y')) or
        any(type(screen[k]) is not int for k in ('recipe','native_ui_scale',*geometry))):
        raise ValueError('Invalid screen types/absolute orientation')
    try:
        controller=int(ScreenControllerV2[screen['ic']]);color=int(ScreenColorV2[screen['color']])
        model=ScreenConfig(schema_version=1,driver=controller,recipe=screen['recipe'],color=color,
            transform=screen['rotation']//90|(4 if screen['mirror_x'] else 0)|(8 if screen['mirror_y'] else 0),
            native_ui_scale=screen['native_ui_scale'],**{k:screen[k] for k in geometry})
        wire=encode_screen_config(model)
    except (KeyError,TypeError,ValueError) as error:raise ValueError('Invalid registered screen configuration') from error
    # Only the independent target's already-retired drivers are absent.
    if (controller in tuple(int(ScreenControllerV2[n]) for n in ('UC8151','UC8159','JD79665CA','SSD1675','SSD1XXX')) or
        (controller==int(ScreenControllerV2.SSD1677) and color==int(ScreenColorV2.GRAY4))):
        raise ValueError('Screen path is unsupported by this target')
    gpio=value['gpio'];exact(gpio,('display','led'),'gpio')
    display_roles=('mosi','clock','cs','dc','reset','busy','enable');led_roles=('red','green','blue')
    def pin(text):
        if text=='NONE':return 255
        if (type(text) is not str or len(text)!=5 or not text.startswith('P0.') or
            not text[3:].isascii() or not text[3:].isdigit() or int(text[3:])>=32):
            raise ValueError('GPIO must use P0.xx or NONE')
        value=int(text[3:])
        if not ((1<<value)&0xffdf0ff8):raise ValueError('GPIO conflicts with protected board resources')
        return value
    pins={};low=0
    for group,roles,allowed in [('display',display_roles,{'enable'}),('led',led_roles,set(led_roles))]:
        block=gpio[group]
        if group=='display':
            exact(block,(*roles,'enable_active_low'),'gpio.display')
            if type(block['enable_active_low']) is not bool:
                raise ValueError('enable_active_low must be boolean')
            active=['enable'] if block['enable_active_low'] else []
        else:
            exact(block,(*roles,'active_low'),'gpio.led')
            active=block['active_low']
            if type(active) is not list or any(type(k) is not str for k in active) or len(set(active))!=len(active) or not set(active)<=allowed:
                raise ValueError('Invalid LED polarity')
        for role in roles:
            pins[role]=pin(block[role])
            if role in active:low|=1<<((*display_roles,*led_roles).index(role))
    pin_wire=encode_pin_block(dict(pins={**pins,'txd':255,'rxd':255},activeLow=low))
    native=bytes([*[pins[k] for k in display_roles[:6]],2,pins['red'],pins['green'],pins['blue'],pins['enable'],1,1])
    return dict(**value,mcu='nrf52811',firmware_family='aesl22_nrf',board='nrf52811_asel22',
        screen_wire_hex=wire.hex(),factory_pins_hex=native.hex(),pin_wire_hex=pin_wire.hex(),
        active_low=low,sha256=sha(path.read_bytes()))


def require_config_admission(review,config):
    if review.get('compatibilityEpoch',0)<1 or not review.get('factoryDisplayPins'):
        raise ValueError('Selected signed firmware requires N1P1/N1R1 pin configuration and compatibility epoch1 or higher')


def identity(ficr):
    if (len(ficr)!=4096 or struct.unpack_from('<I',ficr,0x100)[0]!=0x52811 or
        struct.unpack_from('<II',ficr,0x10c)!=(24,192) or struct.unpack_from('<II',ficr,0x10)!=(4096,48)):
        raise ValueError('Exact nRF52811 24KiB/192KiB FICR required')
    uid,addr=ficr[0x60:0x68],ficr[0xa4:0xaa]
    if uid in (bytes(8),b'\xff'*8) or addr in (bytes(6),b'\xff'*6):raise ValueError('Missing manufactured identity')
    mac=bytearray(addr[::-1]);kind=struct.unpack_from('<I',ficr,0xa0)[0]&1
    if kind:
        mac[0]=(mac[0]&63)|192
        if bytes((mac[0]&63,))+mac[1:] in (bytes(6),b'\x3f'+b'\xff'*5):raise ValueError('Invalid static-random identity')
    return dict(deviceId=uid.hex(),address=':'.join(f'{b:02X}' for b in mac),bleName=f'IKT_NRF_{addr[1]:02X}{addr[0]:02X}',ficrSha256=sha(ficr))

def compact_record(kind,payload):
    head=struct.pack('<IHHI',0x31525349,kind,len(payload),fnv(struct.pack('<HH',kind,len(payload))+payload))
    return head+payload+b'\xff'*((-len(payload))%4)+b'ISC1'

def seed(ficr,review,config):
    ident=identity(ficr)
    from qr_codec.device_link import build_device_link
    from qr_codec.device_qr import encode_device_qr
    qr=encode_device_qr(build_device_link(ident['bleName']))
    factory=b'N1P1'+bytes.fromhex(config['factory_pins_hex'])+bytes.fromhex(config['screen_wire_hex'])+struct.pack('<H',config['active_low'])
    factory+=struct.pack('<I',fnv(factory))
    user=bytearray(96);user[:4]=b'N1R1';user[4]=1
    name=ident['bleName'].encode('ascii');user[5]=len(name);user[7:7+len(name)]=name
    user[50:56]=bytes((1,0,9,0,6,0))
    user[56]=9;user[57:66]=bytes((2,1,1,0,0,0,0,0,0))
    user+=struct.pack('<I',fnv(user))
    screen=b'SP\x01\x11'+bytes.fromhex(config['screen_wire_hex'])+bytes(3);screen+=struct.pack('<I',fnv(screen))
    qr_payload=struct.pack('<BB B H I',1,0,qr.modules,len(qr.data),zlib.crc32(qr.data)&0xffffffff)+qr.data
    boot=struct.pack('<IBBHII',0x3153424e,1,1,0,review['appBytes'],int(review['appCrc32'],16))
    records={1:factory,3:screen,5:qr_payload,7:bytes(user),12:boot}
    page=struct.pack('<IIII',0x31505349,1,0xfffffffe,0xffffffff)+b''.join(compact_record(k,v) for k,v in sorted(records.items()))
    if len(page)>4096:raise ValueError('Seed exceeds journal page')
    return page.ljust(8192,b'\xff'),ident

def write_hex(path,memory):
    def record(kind,address,data):
        raw=bytes((len(data),address>>8,address&255,kind))+data
        return ':'+raw.hex().upper()+f'{(-sum(raw))&255:02X}'
    rows=[];upper=None;addresses=sorted(memory);i=0
    while i<len(addresses):
        start=addresses[i];high=start>>16
        if high!=upper:rows.append(record(4,0,high.to_bytes(2,'big')));upper=high
        chunk=bytearray()
        while i<len(addresses) and len(chunk)<16 and addresses[i]==start+len(chunk) and addresses[i]>>16==high:
            chunk.append(memory[addresses[i]]);i+=1
        rows.append(record(0,start&65535,bytes(chunk)))
    rows.append(':00000001FF')
    with Path(path).open('x',encoding='ascii',newline='\n') as stream:stream.write('\n'.join(rows)+'\n')

def normalize_full(source,output):
    memory=HEX['parse_hex'](source)
    expected={0x10001014:0x2a000,0x10001018:0x2e000}
    for address,value in expected.items():
        if bytes(memory.get(address+i,255) for i in range(4))!=struct.pack('<I',value):raise ValueError('Unknown producer Recovery/UICR selection')
    if any(a>=0x30000 and a not in range(0x10001014,0x1000101c) for a in memory):raise ValueError('Producer contains unrelated factory/vendor bytes')
    memory={a:v for a,v in memory.items() if a<0x30000}
    for address,value in ((0xff8,0x2a000),(0xffc,0x2e000)):
        old=bytes(memory.get(address+i,255) for i in range(4))
        if old not in (b'\xff'*4,struct.pack('<I',value)):raise ValueError('Vendor MBR tail already has an unrelated selection')
        memory.update({address+i:byte for i,byte in enumerate(struct.pack('<I',value))})
    HEX['validate'](memory)
    write_hex(output,memory)

def input_pair(application,recovery,assets):
    for p in map(Path,(application,recovery,assets)):
        if p.is_symlink() or not p.is_file() or p.stat().st_size>1500000:raise ValueError('Invalid producer input')
    app_map,rec_map=HEX['parse_hex'](application),HEX['parse_hex'](recovery)
    size=HEX['validate_component'](app_map,0x19000,0x2a000,'App')
    rec_size=HEX['validate_component'](rec_map,0x2a000,0x2e000,'Recovery')
    app=bytes(app_map.get(a,255) for a in range(0x19000,0x19000+size))
    rec=bytes(rec_map.get(a,255) for a in range(0x2a000,0x2a000+rec_size))
    asset_data=Path(assets).read_bytes();HEX['validate_assets'](asset_data)
    accepted=require_release_binding(sha(app),sha(rec),sha(asset_data))
    return dict(appSha256=sha(app),recoverySha256=sha(rec),appBytes=size,recoveryBytes=rec_size,acceptance=accepted)

def missing_low_ranges(memory):
    result=[];start=None
    for address in range(0x19001):
        missing=address<0x19000 and address not in memory
        if missing and start is None:start=address
        elif not missing and start is not None:result.append((start,address-start));start=None
    return result

def prepare(firmware,configuration,ficr,low_spans,output):
    memory,data,review=verify_bundle(firmware);config=read_config(configuration)
    require_config_admission(review,config)
    if set(low_spans)!=set(missing_low_ranges(memory)):raise ValueError('Necessary low-Flash span set differs from selected Full')
    image=bytearray(b'\xff'*0x30000)
    for address,value in memory.items():image[address]=value
    for (address,length),value in low_spans.items():
        if len(value)!=length:raise ValueError('Truncated low-Flash capture')
        image[address:address+length]=value
    # Preserved low-Flash holes must never invalidate either vendor boot CRC.
    # Fail before producing a writable plan if a supplied sparse image did not
    # actually leave those bytes outside its selected validation coverage.
    HEX['validate'](dict(enumerate(image)),data['gui-assets.bin'])
    journal,ident=seed(ficr,review,config)
    output=Path(output);output.mkdir(exist_ok=False)
    (output/'flash.bin').write_bytes(image)
    write_hex(output/'prepared.hex',dict(enumerate(image)))
    (output/'settings.bin').write_bytes(journal)
    (output/'gui-assets.bin').write_bytes(data['gui-assets.bin'])
    plan=dict(schemaVersion=1,baseline='inktag-release-v1',identity=ident,review=review,config=config,
        ranges=dict(internalFlash=[0,196608],settings=[0,8192],gui=[8192,12288],invalidatedImageHeads=[40960,483328,4096,4],
            untouchedExternalReserved=[20480,20480],immutablePages=['FICR','UICR']),
        userData='new native defaults; images invalidated by four-byte heads; pixel payload not erased',
        clock='not written',bleAccess=False,oldEpochCompared=False,
        files={n:dict(bytes=len(v),sha256=sha(v)) for n,v in [('flash.bin',bytes(image)),('settings.bin',journal),('gui-assets.bin',data['gui-assets.bin'])]})
    (output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    return plan

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description='Offline nRF52811 native v1 complete-firmware validation')
    sub=parser.add_subparsers(dest='command',required=True)
    for action in ('inspect','create','admit','normalize'):
        p=sub.add_parser(action)
        if action=='admit':
            p.add_argument('--application',required=True);p.add_argument('--recovery',required=True);p.add_argument('--assets',required=True)
        elif action=='normalize':p.add_argument('--input',required=True);p.add_argument('--output',required=True)
        elif action=='create':
            for n in ('full','ota','assets','output'):p.add_argument('--'+n,required=True)
        else:p.add_argument('--firmware',required=True);p.add_argument('--config',required=True)
    args=parser.parse_args()
    if args.command=='inspect':
        _,_,result=verify_bundle(args.firmware);result['config']=read_config(args.config)
        require_config_admission(result,result['config'])
        result['scope']='Complete MCU + new native settings/health + GUI; invalidate image heads; clear RAM crash mailbox and old reset/retention state before final reset. Preserve FICR/UICR/calibration and verified boot metadata. No automatic backup, BLE or clock write.'
        print(json.dumps(result,indent=2))
    elif args.command=='admit':print(json.dumps(input_pair(args.application,args.recovery,args.assets),indent=2))
    elif args.command=='normalize':normalize_full(args.input,args.output)
    else:print(json.dumps(create_bundle(args.full,args.ota,args.assets,args.output),indent=2))
