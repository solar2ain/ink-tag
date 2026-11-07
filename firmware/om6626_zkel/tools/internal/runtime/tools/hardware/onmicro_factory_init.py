"""Bounded, independently purpose-signed OM host-replay capsule. No device IO.

The public consumer never loads ELF, SDK files, plugins, arbitrary entry names,
addresses from Config or code onto a device. Only actual SDK routines from an
authenticated capsule run in a memory/instruction-bounded host emulator.
"""
import hashlib
import json
import struct
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

MAGIC=b'IKOMFI1\0'
DOMAIN=b'InkTag OM factory-init host-replay v1\0'
MAX_META=8192
MAX_CODE=319392
# Match the fixed RAM_RO linker region, excluding the 64-byte retained tail.
# The former 0x201180 bound was one historical image's linked end, not an ABI.
RAM_CODE_START=0x200000
RAM_CODE_END=0x201fc0
RAM_ABI={'version':1,'mailbox':0x20010000,'buffer':0x20010300,'length':0x200100f0,
         'stack':0x20013d00,'stop':0x800000,'instruction_limit':2000000,
         'resource_instruction_limit':32000000}
# Host scratch starts below the physical RAM_HIGH end. Authenticated firmware
# sections may use only the prefix before that scratch region, including stack.
FIRMWARE_RAM_END=RAM_ABI['mailbox']
ENTRY_NAMES={'nvds_init','nvds_get','nvds_put','nvds_del',
             'epd_screen_config_write','inktag_device_password_store_qr',
             'inktag_nfc_binding_encode','inktag_nfc_link_build',
             'inktag_om_device_name_write','inktag_om_device_label_write',
             'inktag_om_device_name_read','inktag_om_device_label_read',
             'inktag_om_pins_reload','inktag_om_pins_info',
             'inktag_om_pin_factory_decode','inktag_om_pin_pref_decode',
             'epd_screen_config_read','inktag_nfc_binding_decode',
             'inktag_om_password_load','inktag_display_settings_decode',
             'inktag_om_findmy_public_state','epd_display_state_read',
             'inktag_image_store_prepare_profile','inktag_image_store_publish_profile',
             'inktag_image_store_get','inktag_om_gui_asset_info'}
HOOK_NAMES={'drv_sf_read','drv_sf_write','drv_sf_erase','drv_sfs_cur_obj',
            'drv_sfs_located','memcpy','memset','inktag_om_health_operation_allowed'}
LAYOUTS={('om6626_hsela','layout-v1'):0x3156484f,
         ('om6626_zkc','layout-v1'):0x31565a4f}
ORDER=int('FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551',16)
def sha(data):return hashlib.sha256(data).hexdigest()
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')
def unique(pairs):
    out={}
    for k,v in pairs:
        if k in out:raise ValueError('duplicate capsule JSON key')
        out[k]=v
    return out
def uint(value,maximum=0xffffffff):
    return type(value) is int and 0<=value<=maximum
def code_address(at):
    return uint(at) and (RAM_CODE_START<=at<RAM_CODE_END or 0x42a000<=at<0x478000)

def decode(blob,app_raw,app_signed,recovery,layout,public_key,expected_pin,*,expected_epoch):
    # The caller obtains this value from the maintained, verified App
    # manifest. Format version1 must never reset the real security counter.
    if not uint(expected_epoch,0xffff):
        raise ValueError('verified App compatibility epoch required')
    if len(blob)<80 or len(blob)>16+MAX_META+MAX_CODE+64 or blob[:8]!=MAGIC:
        raise ValueError('invalid factory-init capsule bounds/magic')
    n,size=struct.unpack_from('<II',blob,8)
    if not 0<n<=MAX_META or not 0<size<=MAX_CODE or len(blob)!=16+n+size+64:
        raise ValueError('factory-init length mismatch')
    # Pin and purpose are independent of App signing; App signature is not accepted here.
    if len(public_key)!=65 or sha(public_key)[:32]!=expected_pin:
        raise ValueError('factory-init public key is not the maintained App trust pin')
    raw_sig=blob[-64:];r=int.from_bytes(raw_sig[:32],'big');s=int.from_bytes(raw_sig[32:],'big')
    if not 0<r<ORDER or not 0<s<=ORDER//2:raise ValueError('invalid canonical factory-init signature')
    try:ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(),public_key).verify(
        encode_dss_signature(r,s),DOMAIN+blob[:-64],ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature,ValueError) as e:raise ValueError('factory-init purpose signature failed') from e
    meta=json.loads(blob[16:16+n].decode('ascii'),object_pairs_hook=unique)
    keys={'purpose','version','vendor','mcu','family','layout','layout_signature','epoch','app_raw_sha256',
          'app_signed_sha256','recovery_sha256','receipt_sha256','code_sha256','ram_abi','segments','zero_ranges','entries','hooks','load_plan'}
    if type(meta) is not dict or set(meta)!=keys or meta['purpose']!='InkTag factory-init host-replay' or\
       type(meta['version']) is not int or meta['version']!=1 or meta['vendor']!='onmicro' or meta['mcu']!='om6626' or\
       (meta['family'],layout) not in LAYOUTS or meta['layout']!=layout or\
       meta['layout_signature']!=LAYOUTS[(meta['family'],layout)] or type(meta['epoch']) is not int or meta['epoch']!=expected_epoch or\
       meta['ram_abi']!=RAM_ABI or type(meta['ram_abi']) is not dict or\
       any(type(v) is not int for v in meta['ram_abi'].values()) or\
       type(meta['layout_signature']) is not int or canonical(meta)!=blob[16:16+n]:
        raise ValueError('factory-init purpose/MCU/family/layout/ABI mismatch')
    payload=blob[16+n:-64]
    if meta['app_raw_sha256']!=sha(app_raw) or meta['app_signed_sha256']!=sha(app_signed) or\
       meta['recovery_sha256']!=sha(recovery) or meta['code_sha256']!=sha(payload):
        raise ValueError('factory-init artifact binding mismatch')
    if type(meta['receipt_sha256']) is not str or len(meta['receipt_sha256'])!=64 or\
       any(c not in '0123456789abcdef' for c in meta['receipt_sha256']):
        raise ValueError('factory-init requires private build receipt binding')
    if meta['load_plan']!=['OM662XX_NOMBR','OM662XX','OM662XX_NOMBR']:
        raise ValueError('factory-init does not bind the frozen loader order')
    segments=meta['segments']
    if type(segments) is not list or not 1<=len(segments)<=16:raise ValueError('invalid load segment count')
    loaded=[];cursor=0
    for item in segments:
        if type(item) is not dict or set(item)!={'vaddr','paddr','size','offset'} or any(not uint(x) for x in item.values()):
            raise ValueError('invalid load descriptor')
        v,p,count,off=(item[k] for k in ('vaddr','paddr','size','offset'))
        if not count or off!=cursor or count>len(payload)-cursor:
            raise ValueError('invalid capsule segment span')
        if not (0x42a000<=p<p+count<=0x42a000+len(app_raw)):
            raise ValueError('capsule source is not within authenticated raw App')
        if not (RAM_CODE_START<=v<v+count<=RAM_CODE_END or 0x42a000<=v<v+count<=0x478000 or
                0x20000000<=v<v+count<=FIRMWARE_RAM_END):
            raise ValueError('capsule destination exceeds fixed host ABI')
        if any(v<end and start<v+count for start,end in loaded):raise ValueError('overlapping capsule sections')
        if payload[off:off+count]!=app_raw[p-0x42a000:p-0x42a000+count]:
            raise ValueError('capsule segment differs from signed App load bytes')
        loaded.append((v,v+count));cursor+=count
    if cursor!=len(payload):raise ValueError('unowned capsule payload tail')
    zeros=meta['zero_ranges']
    if type(zeros) is not list or not 1<=len(zeros)<=8:raise ValueError('invalid zero section count')
    for item in zeros:
        if type(item) is not dict or set(item)!={'vaddr','size'} or any(not uint(v) for v in item.values()):
            raise ValueError('invalid zero section descriptor')
        v,count=item['vaddr'],item['size']
        if not 0x20000000<=v<v+count<=FIRMWARE_RAM_END or any(v<end and start<v+count for start,end in loaded):
            raise ValueError('zero section overlaps host ABI/load bytes')
        loaded.append((v,v+count))
    for key,names in (('entries',ENTRY_NAMES),('hooks',HOOK_NAMES)):
        mapping=meta[key]
        if type(mapping) is not dict or set(mapping)!=names or any(not uint(v) for v in mapping.values()):
            raise ValueError('capsule function whitelist mismatch')
        if len(set(mapping.values()))!=len(mapping):
            raise ValueError('capsule function whitelist mismatch')
        if any(not code_address(v&~1) for v in mapping.values() if uint(v)) or any(not uint(v) for v in mapping.values()):
            raise ValueError('capsule entry/hook exceeds code whitelist')
        if any(not any(start<=v&~1<end for start,end in loaded) for v in mapping.values()):
            raise ValueError('capsule function is not in authenticated load bytes')
    return meta,payload

class Program:
    def __init__(self,meta,payload):self.meta,self.payload=meta,payload

class Session:
    """Host-only execution. No programmer import or arbitrary call API."""
    def __init__(self,program,flash,base,*,allow_initialization=False):
        from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
        # Actual Cortex-M instructions include PRIMASK/barriers in the qualified
        # read-only resource adapters. Do not bypass their IRQ discipline.
        self.u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        self.program=program;self.base=base;self.allow_initialization=allow_initialization
        if len(flash)!=0x100000 or base not in (0xf9000,0xfc000,0xfd000,0xfe000):
            raise ValueError('invalid SDK replay source/pair')
        self.flash=bytearray(flash);self.writes=0
        for at,n in ((0x200000,0x20000),(0x20000000,0x20000),(0x400000,0x100000),(0x800000,0x1000)):
            self.u.mem_map(at,n)
        for item in program.meta['segments']:
            self.u.mem_write(item['vaddr'],program.payload[item['offset']:item['offset']+item['size']])
        for item in program.meta['zero_ranges']:
            self.u.mem_write(item['vaddr'],bytes(item['size']))
        self.hooks={v&~1:k for k,v in program.meta['hooks'].items()}
        self.u.hook_add(UC_HOOK_CODE,self._hook)
        self.u.hook_add(UC_HOOK_MEM_WRITE,self._memory_write)

    def _memory_write(self,u,access,at,size,value,_):
        if not (0x20000000<=at<at+size<=FIRMWARE_RAM_END or 0x20010000<=at<at+size<=0x20014000):
            raise ValueError('SDK machine write outside fixed RAM/ABI')

    def _hook(self,u,at,size,_):
        from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_LR
        name=self.hooks.get(at)
        if not name:return
        a,b,c,d=[u.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)]
        if name.startswith('drv_sf_'):
            n=d if name=='drv_sf_erase' else struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP),4))[0]
            if not 0<n<=8192 or not 0<=c<c+n<=0x100000:raise ValueError('SDK physical span out of bounds')
            if name=='drv_sf_read':
                if not (self.base<=c<c+n<=min(0x100000,self.base+0x3000) or
                        0x78000<=c<c+n<=0x80000 or 0x84000<=c<c+n<=self.base):
                    raise ValueError('SDK read outside pair/CONF or current user-resource extents')
                u.mem_write(d,bytes(self.flash[c:c+n]))
            else:
                if not self.allow_initialization or not self.base<=c<c+n<=self.base+8192:
                    raise ValueError('SDK mutation outside admitted target pair')
                if name=='drv_sf_erase' and (c%4096 or n%4096):raise ValueError('unaligned SDK erase')
                data=bytes([255])*n if name=='drv_sf_erase' else bytes(u.mem_read(d,n))
                for i,v in enumerate(data):self.flash[c+i]=v if name=='drv_sf_erase' else self.flash[c+i]&v
                self.writes+=1
        elif name=='drv_sfs_cur_obj':u.reg_write(UC_ARM_REG_R0,0x40000000)
        elif name=='drv_sfs_located':u.reg_write(UC_ARM_REG_R0,0)
        elif name=='inktag_om_health_operation_allowed':
            # Host codec fixture only; real Full admission is a separate gate.
            u.reg_write(UC_ARM_REG_R0,1)
        elif name in ('memcpy','memset'):
            if c>0x100000 or not 0x20000000<=a<=a+c<=0x20020000:raise ValueError('libc host write out of RAM')
            u.mem_write(a,bytes(u.mem_read(b,c)) if name=='memcpy' else bytes([b&255])*c)
        u.reg_write(UC_ARM_REG_PC,u.reg_read(UC_ARM_REG_LR))

    def call(self,name,*args):
        from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_LR
        if name not in ENTRY_NAMES:raise ValueError('SDK entry is not permitted')
        if len(args)>8 or any(not uint(v) for v in args):raise ValueError('invalid host ABI arguments')
        for r,v in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),args):self.u.reg_write(r,v)
        if len(args)>4:self.u.mem_write(RAM_ABI['stack'],struct.pack('<'+'I'*(len(args)-4),*args[4:]))
        self.u.reg_write(UC_ARM_REG_SP,RAM_ABI['stack']);self.u.reg_write(UC_ARM_REG_LR,RAM_ABI['stop']|1)
        # CRC/native-font validation covers up to32 KiB and the image scan up
        # to255 headers. Only these fixed read-only consumers get this larger
        # signed ABI budget; NVDS/mutators keep the original2M instruction cap.
        limit=RAM_ABI['resource_instruction_limit'] if name in (
            'inktag_om_gui_asset_info','inktag_image_store_prepare_profile') else RAM_ABI['instruction_limit']
        self.u.emu_start(self.program.meta['entries'][name]|1,RAM_ABI['stop'],count=limit)
        if self.u.reg_read(UC_ARM_REG_PC)!=RAM_ABI['stop']:raise ValueError('SDK replay instruction bound exhausted')
        return self.u.reg_read(UC_ARM_REG_R0)

    def get(self,tag):
        self.u.mem_write(RAM_ABI['length'],struct.pack('<H',504))
        status=self.call('nvds_get',tag,RAM_ABI['length'],RAM_ABI['buffer'])
        n=struct.unpack('<H',self.u.mem_read(RAM_ABI['length'],2))[0]
        if status not in (0,2) or n>504:raise ValueError('unknown SDK record state/size')
        return None if status==2 else bytes(self.u.mem_read(RAM_ABI['buffer'],n))
    def snapshot(self):return {tag:self.get(tag) for tag in range(256)}
    def put(self,tag,value):
        if not value or len(value)>504:raise ValueError('invalid fixed SDK record length')
        self.u.mem_write(RAM_ABI['buffer'],value)
        if self.call('nvds_put',tag,len(value),RAM_ABI['buffer'])!=0 or self.get(tag)!=value:
            raise ValueError('SDK put/readback failure')
