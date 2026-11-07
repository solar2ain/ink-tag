"""Bounded Factory coverage contract. No reader, writer or public CLI.

    Hashes/nonce establish internal consistency, NOT physical acquisition
    authenticity, snapshot quiescence or permission to erase. A future trusted
    acquisition backend must bind these descriptors to actual reads. Unknown
    bytes never prove erased/Factory, even in a private RAM projection.
"""
from dataclasses import dataclass
import hashlib
import struct

from onmicro_factory_init import Program, Session, RAM_ABI
from onmicro_flash_identity import address_from_result

FLASH_BYTES=0x100000
BASE_RANGES=(('protected',0,0x4000),('runtime-mbr',0x80000,0x82000),
             ('factory-records',0xf9000,0x100000))

class CaptureError(ValueError):pass

def digest(data):return hashlib.sha256(data).hexdigest()

def required_ranges():
    return BASE_RANGES

@dataclass(frozen=True)
class UnitWitness:
    mcu:str
    operation_nonce:bytes
    before:bytes
    before_sha256:str
    after:bytes
    after_sha256:str

    def __post_init__(self):
        if (self.mcu!='om6626' or type(self.operation_nonce) is not bytes or
                len(self.operation_nonce)!=16 or not any(self.operation_nonce)):
            raise CaptureError('bounded OM unit/operation witness required')
        for raw,sha in ((self.before,self.before_sha256),(self.after,self.after_sha256)):
            if type(raw) is not bytes or digest(raw)!=sha or len(raw)!=64:
                raise CaptureError('unit witness raw SHA/length mismatch')
            try:address_from_result(raw) # Consistency grammar only; no device action.
            except ValueError as error:raise CaptureError(str(error)) from error
            if raw[48:]!=self.operation_nonce:raise CaptureError('unit witness operation mismatch')
        if self.before[:48]!=self.after[:48]:raise CaptureError('unit changed within capture')

    @property
    def unit_id(self):return digest(b'OM source Flash identity\0'+self.before[:48])

@dataclass(frozen=True)
class Part:
    address:int
    length:int
    data:bytes
    raw_sha256:str
    unit_id:str
    operation_nonce:bytes

    def __post_init__(self):
        if (type(self.address) is not int or type(self.length) is not int or
                not 0<=self.address<self.address+self.length<=FLASH_BYTES or
                type(self.data) is not bytes or len(self.data)!=self.length or digest(self.data)!=self.raw_sha256):
            raise CaptureError('captured address/length/raw SHA mismatch')

@dataclass(frozen=True)
class Capture:
    unit:UnitWitness
    parts:tuple

    def __post_init__(self):
        if type(self.unit) is not UnitWitness or type(self.parts) is not tuple or not 1<=len(self.parts)<=256:
            raise CaptureError('bounded immutable capture required')
        end=0
        for part in sorted(self.parts,key=lambda p:p.address if type(p) is Part else -1):
            if type(part) is not Part:raise CaptureError('exact captured part required')
            if part.unit_id!=self.unit.unit_id or part.operation_nonce!=self.unit.operation_nonce:
                raise CaptureError('part belongs to another unit/operation')
            if part.address<end:raise CaptureError('overlap/replayed part refused')
            end=part.address+part.length

    def read(self,address,length):
        if (type(address) is not int or type(length) is not int or
                not 0<=address<address+length<=FLASH_BYTES):raise CaptureError('read span out of bounds')
        cursor=address;end=address+length;pieces=[]
        for part in sorted(self.parts,key=lambda p:p.address):
            if part.address+part.length<=cursor:continue
            if part.address>cursor:break
            count=min(end-cursor,part.address+part.length-cursor)
            pieces.append(part.data[cursor-part.address:cursor-part.address+count]);cursor+=count
            if cursor==end:return b''.join(pieces)
        raise CaptureError(f'uncaptured source bytes at 0x{cursor:06x}; no blank inference')

    def require(self,ranges):
        for _,start,end in ranges:self.read(start,end-start)

    def manifest(self):
        return {'unit_id':self.unit.unit_id,'identity_before_sha256':self.unit.before_sha256,
                'identity_after_sha256':self.unit.after_sha256,
                'parts':[{'address':p.address,'length':p.length,'raw_sha256':p.raw_sha256}
                         for p in sorted(self.parts,key=lambda p:p.address)],
                'capturedBytes':sum(p.length for p in self.parts),'physicalAcquisitionAuthenticated':False}

    def _ram_projection(self):
        # Private execution backing only. Uncovered zeroes are NEVER evidence.
        model=bytearray(FLASH_BYTES)
        for part in self.parts:model[part.address:part.address+part.length]=part.data
        return bytes(model)

class _ReadOnlySDK(Session):
    def __init__(self,program,capture,base):
        self.capture=capture;self.source_reads=[]
        super().__init__(program,capture._ram_projection(),base)

    def _hook(self,u,at,size,unused):
        name=self.hooks.get(at)
        if name in ('drv_sf_write','drv_sf_erase'):raise CaptureError('source classification cannot mutate SDK Flash')
        if name=='drv_sf_read':
            from unicorn.arm_const import UC_ARM_REG_R2,UC_ARM_REG_SP
            address=u.reg_read(UC_ARM_REG_R2)
            length=struct.unpack('<I',u.mem_read(u.reg_read(UC_ARM_REG_SP),4))[0]
            data=self.capture.read(address,length)
            self.source_reads.append({'address':address,'length':length,'raw_sha256':digest(data)})
        return super()._hook(u,at,size,unused)
