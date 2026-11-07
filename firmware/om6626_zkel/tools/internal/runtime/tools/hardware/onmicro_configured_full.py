"""Config/MCU-base validation and maintained SDK Full settings encoding."""
import argparse
import json
import hashlib
import struct
import zlib
from onmicro_full_config import read_config,require_family
from onmicro_full_config import factory_record,default_preferences
from onmicro_full_base import inspect,authenticated_program
from onmicro_factory_init import Session,RAM_ABI
from activation_preservation import PUBLIC_KEY,verify_token
from _om_full_formats.device_qr import encode_device_qr
from _om_full_formats.device_link import build_device_link


def preserved_authorization(program,value,device_id):
    if value is None:
        return {'state':'free','nativeRecordPresent':False,'appProBootVerified':False}
    if (type(value) is not bytes or len(value)!=108 or value[:8]!=b'IAC1\x01\x01\0\0' or
            zlib.crc32(value[:104])!=struct.unpack_from('<I',value,104)[0]):
        raise ValueError('existing authorization record is damaged; refused before erase')
    if PUBLIC_KEY not in program.payload:
        raise ValueError('selected authenticated App activation public key is not bound')
    verified=verify_token(value[8:104],product_id=1,feature_mask=1,
                          security_epoch=0,device_id=device_id,public_key=PUBLIC_KEY)
    return {'state':'signed-authorization-preserved',**verified,'nativeRecordPresent':True,
            'nativeBytesPreserved':True,'appProBootVerified':False,
            'hardwareBindingAuthority':'host-immutable-UID' if device_id is not None else 'unchanged-App-NFC-UID-on-boot'}

def valid_native_address(address):
    if type(address) is not bytes or len(address)!=6:return False
    # Exact ZKC/HS address_load normalization before the shared validator:
    # preserve the stored value, but require a nontrivial46-bit static root.
    value=int.from_bytes(address,'little')&((1<<46)-1)
    return value not in (0,(1<<46)-1)


def identity_names(records,hardware,*,immutable_address=None):
    """Same qualified OM suffix rule as init_device_identity/Full settings.

    Valid installed addresses take precedence. A missing/invalid address can
    use the human-authorized Full-only default from repeated JEDEC/Flash UID;
    these immutable preflight fields are NOT public Config properties.
    """
    address=records.get(1)
    if not valid_native_address(address):address=immutable_address
    if not valid_native_address(address):
        raise ValueError('stable same-operation immutable Flash UID default address missing/invalid')
    suffix=f'{address[1]:02X}{address[0]:02X}'
    return 'IKT_OM_'+suffix,'InkTag '+suffix

def prepare_selected(program,source,config,old_base,*,immutable_address=None,activation_device_id=None,
                     authorization_allowed=True,warnings=None):
    """Initialize user data and retain only an existing validated authorization."""
    return _initialize_selected(program,source,config,old_base,immutable_address=immutable_address,
                                activation_device_id=activation_device_id,
                                authorization_allowed=authorization_allowed,warnings=warnings)


def _initialize_selected(program,source,config,old_base,*,immutable_address=None,activation_device_id=None,
                         authorization_allowed=True,warnings=None):
    """Encode one explicit Full from SDK-selected current-unit records.

    Retain native Factory records and a complete valid authorization only.
    Full never imports old ordinary settings, password, health or Find state.
    """
    hardware=config['hardware'];digest=hashlib.sha256(source).hexdigest();warnings=list(warnings or ())
    # A recognized code revision is NOT permission to initialize/GC damaged or
    # missing NVDS. Old-state selection must be read-only, even on stock.
    old=None
    if old_base is None:
        records={tag:None for tag in range(256)}
    else:
        old=Session(program,source,old_base)
        if old.call('nvds_init',old_base)!=0:raise ValueError('source SDK initialization refused')
        records=old.snapshot()
    original_address=records.get(1)
    if not valid_native_address(original_address):
        if not valid_native_address(immutable_address):
            raise ValueError('stable same-operation immutable Flash UID default address missing/invalid')
        records[1]=immutable_address
        warnings.append('Old BLE address is missing/invalid; a deterministic same-operation Flash-UID default is initialized using the existing native tag1 format.')
    # SDK nvds_tags.h defines these calibration/power parameters as one byte.
    # Invalid old payloads are omitted, allowing the existing SDK/CPFT defaults;
    # this host never invents a calibration value.
    for tag in (3,4,5,6,7,8):
        if records.get(tag) is not None and len(records[tag])!=1:
            records[tag]=None
            warnings.append(f'Invalid old native parameter tag{tag} is not restored; existing factory/SDK defaults apply.')
    authorization_discarded=not authorization_allowed
    if not authorization_allowed:records[111]=None
    try:authorization=preserved_authorization(program,records.get(111),activation_device_id)
    except ValueError:
        records[111]=None
        authorization_discarded=True
        authorization=preserved_authorization(program,None,activation_device_id)
        warnings.append('Old authorization is invalid or incompatible; it is not restored. Full initializes Free; no entitlement is issued or repaired.')
    name,label=identity_names(records,hardware,immutable_address=immutable_address)
    hs=hardware=='om6626_hsela';base=0xfe000 if hs else 0xfd000
    target=bytearray([255])*0x100000
    if not hs and old_base is not None:
        old_conf=old_base+8192
        target[0xff000:]=source[old_conf:old_conf+4096]
    sdk=Session(program,target,base,allow_initialization=True)
    if sdk.call('nvds_init',base)!=0:raise ValueError('new native NVDS initialization refused')
    # Start from an erased pair, not an old NVDS clone. Deleting tags alone
    # would leave stale password/Pro/Find payloads in retired native records.
    # Re-encode only immutable/vendor payloads and the exact original signed
    # authorization. Old health and retired user-record generations are absent.
    for tag,value in records.items():
        # Native tag2 is the old BLE name, tag9 is SMP bonding user state.
        # Do not even copy them before deletion, which would retain stale bytes.
        if value is not None and (tag<100 and tag not in (2,9) or tag==111):sdk.put(tag,value)
    # Explicit new-machine Full only. Protected unknown/calibration
    # records remain byte-identical; board108 is replaced by the selected PCB.
    cleared={2,9,100,101,102,103,104,105,106,107,109,110,112,113,114,115,116,117,118,119,132}
    cleared|={tag for tag,value in records.items() if value is not None and tag>=100 and tag not in (108,111)}
    for tag in sorted(cleared):
        if sdk.call('nvds_del',tag) not in (0,2) or sdk.get(tag) is not None:
            raise ValueError('SDK explicit user-data deletion failed')
    sdk.put(108,factory_record(config));sdk.put(104,default_preferences())
    for field,text in (('name',name),('label',label)):
        data=text.encode('utf-8');sdk.u.mem_write(RAM_ABI['buffer'],data+b'\0')
        if sdk.call('inktag_om_device_'+field+'_write',RAM_ABI['buffer'],len(data),0)!=1:
            raise ValueError('maintained OM '+field+' writer refused')
    pins=config['pin_wire'][4:]
    cfg=bytes(pins[:6])+bytes((255,255,255,255,pins[6],1,1))
    checksum=1
    for b in cfg:checksum^=b
    sdk.put(100,struct.pack('<IB',0x494b5447,1)+cfg+bytes((checksum,)))
    screen=config['screen_wire']
    # Host ABI is fixed and private-build verified against epd_screen_config_t.
    value=screen[:4]+bytes((screen[4]&15,0))+screen[5:]+bytes((screen[4]>>4,0))
    sdk.u.mem_write(RAM_ABI['mailbox'],value)
    if sdk.call('epd_screen_config_write',RAM_ABI['mailbox'])!=1 or sdk.get(101) is None:
        raise ValueError('common screen record seed rejected')
    # Explicit trusted Full creates the existing common OFF/QR record. No
    # credentials are accepted from Config and no private password codec.
    link=build_device_link(name)
    qr=encode_device_qr(link)
    C,M,P,L=0x20010000,0x20010100,0x20010300,0x200100f0
    authority=struct.pack('<I32s32s??2x',0,bytes(32),bytes(32),True,False)
    sdk.u.mem_write(C,authority);sdk.u.mem_write(M,qr.data)
    if sdk.call('inktag_device_password_store_qr',C,qr.modules,M,len(qr.data),P,L)!=1:
        raise ValueError('common first-OFF/QR encoder refused')
    n=struct.unpack('<H',sdk.u.mem_read(L,2))[0]
    if not 9<=n<=420:raise ValueError('common QR size exceeds fixed ABI')
    wire=bytes(sdk.u.mem_read(P,n))
    if wire[:2]!=b'\x01\x00':raise ValueError('release-v1 common OFF record producer required')
    sdk.put(106,struct.pack('<IBBBBHI',0x3152514f,1,0,0,0,n,zlib.crc32(wire))+wire)
    sdk.u.mem_write(M,name.encode()+b'\0')
    n=sdk.call('inktag_nfc_link_build',P,122,M,len(name))
    if not 0<n<=122:raise ValueError('common name-bound NFC link seed refused')
    ndef=bytes(sdk.u.mem_read(P,n))
    sdk.u.mem_write(C,bytes((0,0,n))+ndef+bytes(122-n))
    if sdk.call('inktag_nfc_binding_encode',C,P,L)!=1:raise ValueError('common NFC authority seed refused')
    n=struct.unpack('<H',sdk.u.mem_read(L,2))[0]
    if not 0<n<=126:raise ValueError('common NFC authority size refused')
    sdk.put(103,bytes(sdk.u.mem_read(P,n)))
    intended=sdk.snapshot()
    cold=Session(program,sdk.flash,base)
    if cold.call('nvds_init',base)!=0 or cold.snapshot()!=intended:
        raise ValueError('cold SDK-selected state differs from intended Full state')
    for field,text in (('name',name),('label',label)):
        cold.u.mem_write(M,bytes(32))
        if cold.call('inktag_om_device_'+field+'_read',M)!=1 or bytes(cold.u.mem_read(M,len(text)+1))!=text.encode()+b'\0':
            raise ValueError('cold complete '+field+' readback differs')
    for tag in range(256):
        if tag not in cleared|{108} and intended[tag]!=records[tag]:
            raise ValueError('unknown/protected vendor record changed')
    cold.call('inktag_om_pins_reload')
    if cold.call('inktag_om_pins_info',P)!=1:raise ValueError('cold runtime routing refused')
    report=bytes(cold.u.mem_read(P,36))
    if report[4:20]!=config['pin_wire'] or report[20:]!=config['pin_wire']:
        raise ValueError('cold complete routing differs from PCB data')
    if sdk.flash[:base]!=target[:base] or sdk.flash[base+8192:]!=target[base+8192:]:
        raise ValueError('SDK wrote outside the target live/backup pair')
    return bytes(sdk.flash[base:]),{'source_sha256':digest,'source_kind':'native-factory-and-default-initialization','settings_base':base,
        'sdk_writes':sdk.writes,'cold_records_equal':True,'unknown_vendor_retained':old_base is not None,
        'scope':'mcu/main-flash','cleared_user_tags':sorted(cleared),
        'settings_sha256':hashlib.sha256(sdk.flash[base:base+8192]).hexdigest(),
        'name_and_label_preseeded':True,'cold_names_equal':True,'identity_tag_retained':intended[1]==original_address,
        'identityDefaulted':not valid_native_address(original_address),'warnings':warnings,
        'managed_link':link,'name_bound_qr_and_nfc':True,
        'authorization':authorization,'authorizationRecordDiscarded':authorization_discarded,
        'healthHistoryCleared':cold.get(112) is None,
        'expectedAppBluetoothName':name,'expectedRecoveryBluetoothName':name+'_OTA',
        'bluetoothNameSource':'retained-native-tag1' if valid_native_address(original_address) else 'same-operation-stable-Flash-UID-default-tag1',
        'bluetoothObserved':False,
        'firstBootDisplayRequest':'one-shot-calendar-after-startup',
        'displayObserved':False,
        'portableExecutionQualified':False}

def factory_display_marker(layout_signature,*,target_epoch):
    """Native fresh Recovery record; Full never reads an old epoch/journal."""
    if layout_signature not in (0x3156484f,0x31565a4f) or type(target_epoch) is not int or not 0<=target_epoch<=0xffff:
        raise ValueError('verified target layout/epoch required')
    value=struct.pack('<4sIIIBBHII',b'IRJ1',layout_signature,1,0,7,1,0xffff,target_epoch,0xffffffff)
    return value+struct.pack('<I',zlib.crc32(value))

def compose_review_images(bound,config,settings_tail,*,source):
    """Three explicit HEX maps in the frozen order, never a programmer call."""
    meta,_,raw,signed,files,_=bound
    require_family(config,meta)
    hs=config['hardware']=='om6626_hsela';base=0xfe000 if hs else 0xfd000
    if len(settings_tail)!=0x100000-base or config['layout']!=meta['layout']:
        raise ValueError('incompatible data tail/MCU base')
    if type(source) is not bytes or len(source)!=0x100000:raise ValueError('fixed preservation projection required')
    state={0x400000+i:255 for start,end in ((0x72000,0x80000),(0x82000,base)) for i in range(start,end)}
    code={0x404000+i:b for i,b in enumerate(files['recovery.bin'])}
    # Cold boot authenticates the installed bytes as well as OTA admission.
    # The already verified 96-byte evidence belongs in the same App slot.
    code.update({0x42a000+i:b for i,b in enumerate(signed)})
    marker=factory_display_marker(config['layout_signature'],target_epoch=bound[1]['compatibilityEpoch'])
    final={0x400000+base+i:b for i,b in enumerate(settings_tail)}
    # Emit BOTH complete native pages. No previous high epoch or torn tail
    # survives, regardless of the old source layout or capture gaps.
    final.update({0x482000+i:b for i,b in enumerate(marker+b'\xff'*(8192-len(marker)))})
    if any(not 0x404000<=a<0x500000 or 0x480000<=a<0x482000 for pool in (state,code,final) for a in pool):
        raise ValueError('Full candidate overlaps boot/CPFT/runtime MBR')
    return {'factory-state':state,'factory-code':code,'factory-device-info':final}

def review(base,config,*,progress=None):
    from onmicro_full_prepare import configuration
    cfg=configuration(config)
    if progress:progress(1,'PCB Config validated')
    bound=inspect(base)
    if progress:progress(2,'MCU base and signed application validated')
    meta,app,raw,signed,files,base_sha=bound
    require_family(cfg,meta)
    program=authenticated_program(bound)
    if progress:progress(3,'Factory-init purpose validated')
    if progress:progress(4,'Offline review complete; no device accessed')
    return {'mcu':'om6626','family':meta['family'],'layout':meta['layout'],
        'board_marking':cfg['board_marking'],'board_id':cfg['board_id'],
        'base_sha256':base_sha,'config_sha256':cfg['config_sha256'],
        'screen_wire_hex':cfg['screen_wire'].hex(),'pin_wire_hex':cfg['pin_wire'].hex(),
        'app_raw_bytes':len(raw),'app_raw_limit':319392,'app_raw_headroom':319392-len(raw),
        'recovery_bytes':len(files['recovery.bin']),'recovery_limit':155648,
        'factory_init_purpose_verified':True,'factory_init_bytes':len(program.payload),
        'portableExecutionQualified':False,'deviceAccessed':False,
        'executionPolicy':'explicit destructive Full; signature, physical capacity and Factory protection; no BLE',
        'firstBootDisplayRequest':'one-shot-calendar-after-startup',
        'displayObserved':False,
        'scope':'mcu/main-flash','qualifiedPhysicalScopes':[],
        'externalFlashAccessed':False,'externalFlashModified':False,
        'externalImageDataPreserved':cfg['board_id']==2,
        'proposedClears':['main-flash-images','user-fonts','ordinary-settings','password/QR','Find','crash/health-history'],
        'preserves':['validated-native-Pro-authorization','immutable-identity','calibration','unknown-vendor-records','protected-boot/CPFT','ZKC-4KiB-CONF']}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base',required=True);p.add_argument('--config',required=True)
    p.add_argument('--console-progress',action='store_true',help=argparse.SUPPRESS)
    args=p.parse_args()
    progress=(lambda step,label:print(f'OM_REVIEW_STAGE {step}/4: {label}',flush=True)) if args.console_progress else None
    print(json.dumps(review(args.base,args.config,progress=progress),indent=2))
if __name__=='__main__':main()
