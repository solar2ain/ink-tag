"""Offline-only OM three-pass writer with bounded NOR and full readback.

There is deliberately no transport/plugin/subprocess/debugger extension point.
Only an exact MemoryFixture is accepted here. The public Full scheduler is
separate; successful simulation is not physical qualification. No device IO.
"""
from dataclasses import dataclass
import struct
from onmicro_factory_init import canonical,sha
from onmicro_rom_loader_model import predict_rom_metadata

XIP=0x400000
SIZE=0x100000
PAGE=4096
CODE_ERASE_END=0x78000 # Current OSH3/OMR4 App end; fonts start here.
READ_LIMIT=8192
SIMULATED_FLASH_MIN_MV=2400 # Matches common policy; NOT a physical ADC preflight.
ORDER=('factory-state','factory-code','factory-device-info')
LOADERS=('OM662XX_NOMBR','OM662XX','OM662XX_NOMBR')

@dataclass(frozen=True)
class Segment:
    address:int
    data:bytes

@dataclass(frozen=True)
class Stage:
    name:str
    loader:str
    segments:tuple

@dataclass(frozen=True)
class Plan:
    hardware:str
    layout:str
    signature:int
    epoch:int
    scope:str
    config_sha256:str
    base_sha256:str
    capsule_sha256:str
    source:bytes
    stages:tuple
    seal:str

def segments(memory):
    result=[];start=None;last=None;data=bytearray()
    for address in sorted(memory):
        value=memory[address]
        if type(address) is not int or type(value) is not int or not 0<=value<=255:
            raise ValueError('invalid emitted byte')
        if last is not None and address!=last+1:
            result.append(Segment(start,bytes(data)));start=None;data.clear()
        if start is None:start=address
        data.append(value);last=address
    if start is not None:result.append(Segment(start,bytes(data)))
    return tuple(result)

def ledger(plan):
    return {'hardware':plan.hardware,'layout':plan.layout,'signature':plan.signature,
        'epoch':plan.epoch,'scope':plan.scope,
        'config_sha256':plan.config_sha256,'base_sha256':plan.base_sha256,
        'capsule_sha256':plan.capsule_sha256,'source_sha256':sha(plan.source),
        'stages':[{'name':s.name,'loader':s.loader,'segments':[
            {'address':p.address,'bytes':len(p.data),'sha256':sha(p.data)} for p in s.segments]} for s in plan.stages]}

def validate(plan):
    if type(plan) is not Plan or type(plan.source) is not bytes or len(plan.source)!=SIZE:
        raise ValueError('fixed offline plan required')
    from onmicro_full_config import BACKENDS
    if plan.hardware not in BACKENDS or (plan.layout,plan.signature)!=BACKENDS[plan.hardware][2:]:
        raise ValueError('wrong exact hardware/layout')
    for digest in (plan.config_sha256,plan.base_sha256,plan.capsule_sha256,plan.seal):
        if type(digest) is not str or len(digest)!=64 or any(c not in '0123456789abcdef' for c in digest):
            raise ValueError('invalid plan binding')
    if type(plan.stages) is not tuple or len(plan.stages)!=3:
        raise ValueError('three explicit stages required')
    base=0xfe000 if plan.hardware=='om6626_hsela' else 0xfd000
    if plan.scope!='mcu/main-flash' or type(plan.epoch) is not int or not 0<=plan.epoch<=0xffff:
        raise ValueError('fixed main-Flash scope and verified target epoch required')
    from onmicro_configured_full import factory_display_marker
    marker=factory_display_marker(plan.signature,target_epoch=plan.epoch)+b'\xff'*(8192-32)
    for stage,name,loader in zip(plan.stages,ORDER,LOADERS):
        if type(stage) is not Stage or (stage.name,stage.loader)!=(name,loader) or type(stage.segments) is not tuple or len(stage.segments)!=2:
            raise ValueError('frozen loader order or emitted ranges changed')
        for segment in stage.segments:
            if type(segment) is not Segment or type(segment.address) is not int or type(segment.data) is not bytes or not segment.data:
                raise ValueError('invalid immutable segment')
        a,b=stage.segments
        if name=='factory-state':
            expected_a=b'\xff'*0xe000
            expected_b=b'\xff'*(base-0x82000)
            if (a.address,len(a.data),b.address,len(b.data))!=(0x472000,0xe000,0x482000,base-0x82000) or a.data!=expected_a or b.data!=expected_b:
                raise ValueError('state erase map changed')
        elif name=='factory-code':
            if a.address!=0x404000 or not 0<len(a.data)<=155648 or b.address!=0x42a000 or not 0x10c+96<=len(b.data)<=319488:
                raise ValueError('code overlaps fixed slots')
            if struct.unpack_from('<I',b.data,4)[0]!=0x404101:
                raise ValueError('code does not route through fixed Recovery')
        elif (a.address,len(a.data),b.address,len(b.data))!=(0x482000,8192,XIP+base,SIZE-base):
            raise ValueError('final marker/settings/CONF map changed')
        elif a.data!=marker:
            raise ValueError('complete native Recovery pages differ from selected firmware')
    if sha(canonical(ledger(plan)))!=plan.seal:raise ValueError('plan payload/input binding changed')
    return plan

def compile_plan(bound,config,source,images):
    if type(source) is not bytes or len(source)!=SIZE or set(images)!=set(ORDER):
        raise ValueError('whole source and three maps required')
    stages=tuple(Stage(n,l,segments(images[n])) for n,l in zip(ORDER,LOADERS))
    from onmicro_full_config import require_family
    require_family(config,bound[0])
    if not 0x10c<=len(bound[2])<=319392 or len(bound[3])!=len(bound[2])+96:
        raise ValueError('code overlaps fixed slots')
    if stages[1].segments!=(Segment(0x404000,bound[4]['recovery.bin']),Segment(0x42a000,bound[3])):
        raise ValueError('emitted code differs from verified base bytes')
    from onmicro_configured_full import factory_display_marker
    marker=factory_display_marker(config['layout_signature'],target_epoch=bound[1]['compatibilityEpoch'])+b'\xff'*(8192-32)
    if stages[2].segments[0]!=Segment(0x482000,marker):
        raise ValueError('emitted final marker differs from fixed policy')
    plan=Plan(config['hardware'],config['layout'],config['layout_signature'],bound[1]['compatibilityEpoch'],
        'mcu/main-flash',config['config_sha256'],
        bound[-1],sha(bound[4]['factory-init.omfi']),source,stages,'')
    plan=Plan(*tuple(getattr(plan,f) for f in Plan.__dataclass_fields__ if f!='seal'),sha(canonical(ledger(plan))))
    return validate(plan)

class UnknownWriteOutcome(RuntimeError):pass

class SimulationStopped(RuntimeError):
    def __init__(self,message,report):super().__init__(message);self.report=report

class MemoryFixture:
    """RAM-backed NOR ONLY; virtual voltage/identity are not physical evidence.

    fault=(stage,kind) is an internal test fixture, never a CLI/Config option.
    Stages: detect, preflight, factory-state/code/device-info, final-readback.
    """
    def __init__(self,source,hardware,*,absent_attempts=0,fault=None):
        if type(source) is not bytes or len(source)!=SIZE or type(absent_attempts) is not int or not 0<=absent_attempts<=3:
            raise ValueError('invalid memory fixture')
        self.flash=bytearray(source);self.hardware=hardware
        self.absent=absent_attempts;self.fault=fault;self.triggered=False
        self.events=[];self.phase='detect';self.closed=False;self.virtual_mv=3000
    def event(self,operation,**detail):
        if self.closed:raise ValueError('fixture is closed; never resume an interrupted run')
        self.events.append({'phase':self.phase,'operation':operation,**detail})
    def trip(self,kind):
        if not self.triggered and self.fault==(self.phase,kind):self.triggered=True;return True
        return False
    def detect(self):
        self.event('detect')
        if self.trip('wrong-identity'):return 'wrong-identity'
        if self.absent:self.absent-=1;return 'no-target'
        return 'ready'
    def power(self):
        self.event('virtual-power',millivolts=self.virtual_mv,physical_adc=False)
        if self.trip('power-low') or self.virtual_mv<SIMULATED_FLASH_MIN_MV:raise ValueError('simulated low power')
        if self.trip('power-stale'):raise ValueError('simulated stale power')
    def read(self,address,n):
        self.event('read',address=address,bytes=n)
        if type(address) is not int or type(n) is not int or not 0<n<=READ_LIMIT or not 0<=address<address+n<=SIZE:
            raise ValueError('bounded read refused')
        data=bytes(self.flash[address:address+n])
        if self.trip('short-read'):return data[:-1]
        if self.trip('read-error'):raise ValueError('simulated read failure')
        if self.trip('corrupt-read'):return bytes([data[0]^1])+data[1:]
        return data
    def erase(self,address):
        self.power();self.event('erase',address=address,bytes=PAGE)
        if type(address) is not int or address%PAGE or not 0x4000<=address<SIZE or 0x80000<=address<0x82000:
            raise ValueError('protected/unaligned erase')
        if self.trip('erase-error'):raise ValueError('simulated erase refusal')
        self.flash[address:address+PAGE]=b'\xff'*PAGE
        if self.trip('erase-unknown'):raise UnknownWriteOutcome('erase ACK unknown; no replay')
    def program(self,address,data):
        self.power();self.event('program',address=address,bytes=len(data))
        if type(address) is not int or address%PAGE or type(data) is not bytes or len(data)!=PAGE or not 0x4000<=address<SIZE or 0x80000<=address<0x82000:
            raise ValueError('protected/unaligned program')
        if self.trip('program-error'):raise ValueError('simulated program refusal')
        for i,b in enumerate(data):self.flash[address+i]&=b
        if self.trip('program-unknown'):raise UnknownWriteOutcome('program ACK unknown; no replay')

def _capture(fixture):
    data=bytearray()
    for address in range(0,SIZE,READ_LIMIT):
        value=fixture.read(address,READ_LIMIT)
        if type(value) is not bytes or len(value)!=READ_LIMIT:raise ValueError('short/invalid capture; no retry')
        data.extend(value)
    return bytes(data)

def _pages(stage):
    pages={}
    for segment in stage.segments:
        for i,b in enumerate(segment.data):
            at=segment.address-XIP+i;page=at&~(PAGE-1)
            if page not in pages:pages[page]=bytearray(b'\xff'*PAGE)
            pages[page][at-page]=b
    return {at:bytes(data) for at,data in sorted(pages.items())}

def run_memory(plan,fixture,*,retry_detect=False):
    """Execute once on private memory, full compare each pass, no retry/rollback.

    Success is simulation ONLY. No physical adapter or callback can be passed.
    """
    validate(plan)
    if type(fixture) is not MemoryFixture or type(retry_detect) is not bool:
        raise ValueError('only exact memory fixture accepted; no physical transport')
    if fixture.events or fixture.closed:raise ValueError('fixture already used; no resume/replay')
    report={'schema':1,'simulation_only':True,'physical_execution_qualified':False,'hardware_operated':False,
        'plan_sha256':plan.seal,'source_sha256':sha(plan.source),'completed_stages':[],
        'scope':plan.scope,'compatibility_epoch':plan.epoch,
        'auxiliary_flash_accessed':False,'auxiliary_flash_modified':False,
        'retry_detect':retry_detect,'unknown_write_outcome':False,'automatic_write_replays':0,
        'voltage_evidence':'virtual fixture only, not ADC','status':'running'}
    expected=plan.source
    try:
        for attempt in range(1,5 if retry_detect else 2):
            status=fixture.detect()
            if status=='ready':break
            if status!='no-target' or not retry_detect or attempt==4:raise ValueError('detection refused: '+status)
        if fixture.hardware!=plan.hardware:raise ValueError('exact assembly mismatch')
        fixture.phase='preflight';fixture.power()
        if _capture(fixture)!=plan.source:raise ValueError('source changed since preparation')
        # Validate known ROM shape before ANY erase, without emitting MBR bytes.
        code={p.address+i:b for p in plan.stages[1].segments for i,b in enumerate(p.data)}
        predict_rom_metadata(plan.source,plan.source,code)
        for stage in plan.stages:
            fixture.phase=stage.name;fixture.power()
            fixture.event('select-loader',loader=stage.loader)
            if stage.name=='factory-code':
                fixture.event('fixed-code-erase',start=0x4000,end_exclusive=CODE_ERASE_END)
                for at in range(0x4000,CODE_ERASE_END,PAGE):fixture.erase(at)
                image=bytearray(expected);image[0x4000:CODE_ERASE_END]=b'\xff'*(CODE_ERASE_END-0x4000);expected=bytes(image)
            pages=_pages(stage)
            image=bytearray(expected)
            for at,data in pages.items():
                fixture.erase(at);fixture.program(at,data);image[at:at+PAGE]=data
            expected=bytes(image)
            if stage.name=='factory-code':
                expected,mbr=predict_rom_metadata(plan.source,expected,code)
                # This fixture models the loader side effect; it is not an MBR
                # programming command, and must never become a physical hook.
                fixture.event('virtual-ROM-loader-effect',**mbr)
                fixture.flash[0x1184:0x118e]=expected[0x1184:0x118e]
                fixture.flash[0x2000:0x2200]=expected[0x2000:0x2200]
                report.update(mbr)
            fixture.event('full-readback')
            if _capture(fixture)!=expected:raise ValueError('whole-chip readback differs after '+stage.name)
            report['completed_stages'].append(stage.name)
        fixture.phase='final-readback'
        if _capture(fixture)!=expected:raise ValueError('independent final readback differs')
        report.update(status='simulation-complete',result_sha256=sha(expected))
        return report
    except Exception as error:
        report.update(status='simulation-stopped',failed_phase=fixture.phase,error=str(error),
            unknown_write_outcome=isinstance(error,UnknownWriteOutcome),partial_result_sha256=sha(bytes(fixture.flash)))
        raise SimulationStopped(str(error),report) from error
    finally:
        report['events']=list(fixture.events);fixture.closed=True
