"""Fixed AESL Full backend. No reset-on-error or restart-on-close.

The vendor's bounded SRAM transfers and the pinned sequenced helper are used.
All Flash writes remain behind the transaction core's image/identity gates.
"""
import hashlib
import json
from pathlib import Path
import struct
import time
import runpy
import zlib

HELPER_SHA = '0f183172ce2d0ce86bf32c27dde76d2d77ceb0ce1a4f974dea1489ba6395dd7c'
HELPER_REL = '_build/dialog/da14585_guarded_helper/InkTag-da14585-guarded-helper-0f183172ce2d.bin'
DLL = Path('C:/Program Files/SEGGER/JLink_V788j/JLink_x64.dll')
DLL_SHA = '6b4a878202defb496ee0344fb0583929a40d36d2268548d723b60442d9a3909e'
BASE, DATA, MAGIC = 0x07FC7C00, 0x07FC8000, 0x31484744
DHCSR, DCRSR, DCRDR = 0xE000EDF0, 0xE000EDF4, 0xE000EDF8
_LIBRARIES=[]


class ReadinessUnavailable(RuntimeError):
    """Known pre-CPU readiness refusal; constructor must close cleanly first."""


def portable_context():
    """File-only deployment detection; a missing portable map cannot fall back."""
    here = Path(__file__).resolve().parent
    marker = here/'portable-files.json'
    if not marker.is_file():
        if here.name == 'internal':
            raise RuntimeError('Portable dependency map missing; no target accessed')
        return None
    mapping = json.loads(marker.read_bytes())
    if mapping.get('format') != 1 or mapping.get('chip') != 'DA14585':
        raise RuntimeError('Invalid portable dependency map; no target accessed')
    return here.parent.parent, mapping


def input_file(relative):
    context = portable_context()
    if context is None:
        return Path(__file__).resolve().parents[2]/relative
    root, mapping = context
    name = mapping.get('files', {}).get(relative)
    if name is None:
        name = mapping.get('externalFiles', {}).get(relative)
    if not isinstance(name, str) or not name or '\\' in name or ':' in name or \
            Path(name).is_absolute() or any(p in ('', '.', '..') for p in name.split('/')):
        raise RuntimeError('Missing/unsafe portable dependency: '+relative+'; no target accessed')
    path = root/name
    for part in (path, *path.parents):
        if part.exists() and (part.is_symlink() or getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise RuntimeError('Linked portable dependency refused; no target accessed')
    if not path.is_file():
        raise RuntimeError('Required portable dependency unavailable: '+name+'; no target accessed')
    return path


def verify_local_backend_inputs(jlink_exe=''):
    """File-only preflight: no native import, USB enumeration or target access."""
    manifest=input_file('firmware/ports/dialog/da14585/tools/vendor-dependencies.json')
    entries=json.loads(manifest.read_bytes())['segger']['files']
    entry=next(item for item in entries if item['file']=='JLink_x64.dll')
    dll=DLL
    if jlink_exe:
        exe=Path(jlink_exe).resolve(strict=True)
        expected=next(item for item in entries if item['file']=='JLink.exe')
        if exe.name.lower()!='jlink.exe' or exe.stat().st_size!=expected['bytes'] or hashlib.sha256(exe.read_bytes()).hexdigest()!=expected['sha256'].lower():
            raise RuntimeError('Pinned J-Link executable differs; no target accessed')
        dll=exe.with_name('JLink_x64.dll')
    if len(DLL_SHA)!=64 or entry['sha256'].lower()!=DLL_SHA or entry['bytes']!=22173848:
        raise RuntimeError('Pinned J-Link manifest/backend binding differs; no target accessed')
    if dll.stat().st_size!=entry['bytes'] or hashlib.sha256(dll.read_bytes()).hexdigest()!=DLL_SHA:
        raise RuntimeError('Pinned J-Link 7.88j DLL differs; no target accessed')
    helper_bytes()
    return dll


def verify_executables(image, boot):
    """Native format/CRC/bounds, not per-firmware hash authentication.

    App publisher authentication is mandatory in the caller. Non-App sections
    are not publisher authenticated. Vendor DLL/helper pins remain separate.
    """
    import runpy
    verifier = runpy.run_path(str(Path(__file__).with_name('da14585-image-verify.py')))
    verifier['verify_full_native_executables'](image)
    verifier['_full_state']['verify_assets_and_first_display'](image)
    size = int.from_bytes(image[5:8], 'big')
    if boot != image[8:8 + size]:
        raise ValueError('Bootloader argument differs from native Full bytes')
    helper_bytes()


def helper_bytes():
    path=input_file(HELPER_REL)
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=HELPER_SHA or len(data)!=2832:
        raise RuntimeError('Fixed Full helper missing/corrupt; no target accessed')
    return data


class Dap:
    def __init__(self, serial='', speed=1000, sram_chunk_bytes=4096, jlink_exe=''):
        self.opened=False;self.native=False;self.link=None
        self.dll=verify_local_backend_inputs(jlink_exe)
        self.sram_chunk_bytes=sram_chunk_bytes
        try:
            self._open(serial, speed)
        except Exception as error:
            stage=getattr(self,'attach_stage','open-exact-dap')
            try:
                self.close()
            except Exception as cleanup:
                raise RuntimeError(f'J-Link {stage} failed: {type(error).__name__}: {error}; '
                                   f'no-start cleanup failed: {cleanup}; no retry') from cleanup
            if isinstance(error,ReadinessUnavailable):raise
            raise RuntimeError(f'J-Link {stage} failed: {type(error).__name__}: {error}; no retry') from error

    def _open(self, serial, speed):
        self.attach_stage='load-pinned-library'
        import pylink
        self.lib=pylink.library.Library(dllpath=str(self.dll),use_tmpcpy=False)
        _LIBRARIES.append(self.lib)
        self.native_messages=[]
        def native_note(level,message):
            self.native_messages.append(dict(level=level,message=str(message)[:2048]))
            del self.native_messages[:-64]
        self.link=pylink.JLink(lib=self.lib,unsecure_hook=lambda *unused:0,
            log=lambda message:native_note('info',message),
            detailed_log=lambda message:native_note('debug',message),
            warn=lambda message:native_note('warning',message),
            error=lambda message:native_note('error',message))
        self.opened=False;self.native=False;self.speed=speed;self.sram_transfer_notes=[]
        self.attach_stage='enumerate-usb-probes'
        serials=[str(x.SerialNumber) for x in self.link.connected_emulators()]
        if serial:
            if serial not in serials:raise ReadinessUnavailable('Exact J-Link absent; no fallback')
        elif len(serials)==1:serial=serials[0]
        elif not serials:raise ReadinessUnavailable('J-Link unavailable; no target accessed')
        else:raise RuntimeError('Select an exact J-Link serial')
        self.serial=serial
        self.attach_stage='open-exact-usb-probe'
        self.link.open(serial_no=int(serial));self.opened=True
        # Configure after USB open, before any target attachment. A command
        # sent before opening the emulator cannot configure that session.
        self.attach_stage='set-no-start-policies'
        if self.link.exec_command('device = Cortex-M0')!=0:raise RuntimeError('Cortex-M0 selection rejected')
        # This setter returns the previous restart state (initially 1).
        # Reasserting this host-only policy returns 0 once disabled.
        previous=self.link.exec_command('SetRestartOnClose = 0')
        if previous not in (0,1) or self.link.exec_command('SetRestartOnClose = 0')!=0:
            raise RuntimeError('No-restart-on-close policy not established')
        if self.link.exec_command('SetSkipDebugDeInit = 1')!=0:raise RuntimeError('Debug hold policy rejected')
        self.attach_stage='select-swd-interface'
        self.link.set_tif(pylink.enums.JLinkInterfaces.SWD)
        self.attach_stage='set-swd-speed'
        self.link.set_speed(speed)
        self.attach_stage='configure-coresight'
        session=runpy.run_path(str(Path(__file__).with_name('da14585-dap-session.py')))
        session['configure_preserving_debug_state'](self.link)
        self.attach_stage='read-sw-dp'
        try:
            dp_id=self.link.coresight_read(0,ap=False)
        except Exception as error:
            # This single DP read precedes all DP/AP writes, CPU access and
            # SRAM. Only after clean constructor close may readiness retry it.
            raise ReadinessUnavailable('SW-DP ID unavailable; check target power and SWD') from error
        if dp_id!=0x0BB11477:raise RuntimeError('Wrong SW-DP')
        self.attach_stage='dap-power-ack'
        self.link.coresight_write(0,0x1E,ap=False)
        self.ctrl=self.link.coresight_read(1,ap=False)
        self.link.coresight_write(1,(self.ctrl&14)|0x50000000,ap=False)
        end=time.monotonic()+1
        while self.link.coresight_read(1,ap=False)&0xA0000000!=0xA0000000:
            if time.monotonic()>end:
                # Still before AP/CPU reads, native Connect, halt or SRAM.
                # Restore the original debug power requests; an error here or
                # in constructor close remains terminal, never NotReady.
                self.link.coresight_write(1,self.ctrl&0x5000000F,ap=False)
                raise ReadinessUnavailable('DAP power ACK unavailable; target may be asleep')
        self.attach_stage='read-ahb-ap'
        self.link.coresight_write(2,0xF0,ap=False)
        if self.link.coresight_read(3,ap=True)!=0x04770021:raise RuntimeError('Wrong AHB-AP')
        self.link.coresight_write(2,0,ap=False)
        self.csw=self.link.coresight_read(0,ap=True)
        self.attach_stage='read-cpu-id'
        if self.unit(0xE000ED00)!=0x410CC200:raise RuntimeError('Wrong CPU')
        self.attach_stage='read-da14585-id'
        if bytes(self.unit(0x50003200+i,1) for i in range(3))!=b'585':raise RuntimeError('Wrong chip')
        # Qualify silicon through DAP before allowing the vendor's one normal
        # Cortex-M0 attach. No flash loader/device algorithm is selected.
        self.link.exec_command('DisableFlashDL')
        self.link.exec_command('DisableFlashBPs')
        self.attach_stage='native-cpu-connect'
        self.link.connect('Cortex-M0',speed=speed,verbose=False)
        self.native=True
        self.attach_stage='recheck-no-start-policy'
        if self.link.exec_command('SetRestartOnClose = 0')!=0:raise RuntimeError('Attach changed no-start close policy')

    def reopen_halted(self):
        """Planned SRAM-only transport boundary, never a Flash-command replay."""
        if not self.unit(DHCSR)&0x20000:raise RuntimeError('SRAM boundary requires an already halted CPU')
        if self.link.exec_command('SetRestartOnClose = 0')!=0:
            raise RuntimeError('No-start policy changed before SRAM close')
        self.link.close();self.opened=False
        self.link.open(serial_no=int(self.serial));self.opened=True
        self.link.exec_command('device = Cortex-M0')
        self.link.exec_command('SetRestartOnClose = 0')
        if self.link.exec_command('SetRestartOnClose = 0')!=0:raise RuntimeError('SRAM boundary close policy absent')
        if self.link.exec_command('SetSkipDebugDeInit = 1')!=0:raise RuntimeError('SRAM boundary debug hold absent')
        self.link.set_tif(1);self.link.connect('Cortex-M0',speed=self.speed,verbose=False)
        if self.link.exec_command('SetRestartOnClose = 0')!=0:
            raise RuntimeError('Reattach changed no-start close policy')
        if not self.unit(DHCSR)&0x20000:raise RuntimeError('Intermediate close/attach resumed the CPU')
        if self.unit(0xE000ED00)!=0x410CC200 or bytes(self.unit(0x50003200+i,1)for i in range(3))!=b'585':
            raise RuntimeError('Silicon changed at SRAM boundary')

    def select(self,address,size=4):
        # AHB-AP debug master and privileged data attributes. CoreSight-only
        # configuration does not set these like native Connect would.
        # CMSIS debug-description initialization uses CSW=0x23000052.
        self.link.coresight_write(0,(self.csw&~0x37)|0x23000000|{1:0x10,2:0x11,4:0x12}[size],ap=True)
        self.link.coresight_write(1,address,ap=True)

    def unit(self,address,size=4,value=None):
        try:
            if self.native:
                if value is None:return getattr(self.link,'memory_read'+str(size*8))(address,1)[0]
                written=getattr(self.link,'memory_write'+str(size*8))(address,[value])
                if written!=size:raise RuntimeError('Short native memory write')
                return
            self.select(address,size)
            if value is None:
                return (self.link.coresight_read(3,ap=True)>>(8*(address&3)))&((1<<(size*8))-1)
            self.link.coresight_write(3,value<<(8*(address&3)),ap=True)
        except Exception as error:
            raise RuntimeError(f'DAP {"read" if value is None else "write"} {address:#010x}/{size}: {error}') from error

    def read(self,address,length,deadline):
        if address%4 or length%4:raise ValueError('Aligned DAP read required')
        out=bytearray()
        if self.native:
            chunk=getattr(self,'sram_chunk_bytes',4096)
            for offset in range(0,length,chunk):
                if time.monotonic()>deadline:raise TimeoutError('Native SRAM read deadline')
                count=min(chunk,length-offset)
                words=self.link.memory_read32(address+offset,count//4)
                if len(words)*4!=count:raise RuntimeError('Short native SRAM read')
                out.extend(struct.pack('<'+'I'*len(words),*words))
            return bytes(out)
        for at in range(address,address+length,4):
            if time.monotonic()>deadline:raise TimeoutError('DAP read deadline; no replay')
            if at==address or at%1024==0:self.select(at)
            try:out.extend(self.link.coresight_read(3,ap=True).to_bytes(4,'little'))
            except Exception as error:raise RuntimeError(f'DAP buffer read {at:#010x}: {error}') from error
        return bytes(out)

    def write(self,address,data,deadline):
        if address%4 or len(data)%4:raise ValueError('Aligned DAP write required')
        if self.native:
            chunk=getattr(self,'sram_chunk_bytes',4096)
            for offset in range(0,len(data),chunk):
                if time.monotonic()>deadline:raise TimeoutError('Native SRAM write deadline')
                block=data[offset:offset+chunk]
                error=None
                try:
                    written=self.link.memory_write32(address+offset,list(struct.unpack('<'+'I'*(len(block)//4),block)))
                    if written!=len(block):error='Short native SRAM write'
                except Exception as failure:error=str(failure)
                # Normal transfers retain the qualified continuous session.
                # Only a failed SRAM ACK uses a fresh held read to resolve the
                # exact bytes; no SRAM or Flash write is ever resent.
                if error:self.reopen_halted()
                actual=self.link.memory_read32(address+offset,len(block)//4)
                actual_bytes=struct.pack('<'+'I'*len(actual),*actual)
                if actual_bytes!=block:
                    first=next((i for i,(a,b) in enumerate(zip(block,actual_bytes)) if a!=b),min(len(block),len(actual_bytes)))
                    self.observe_failure()
                    raise RuntimeError(f'Independent SRAM write readback mismatch at {address+offset:#x}; '
                        f'expected={len(block)}B, actual={len(actual_bytes)}B, firstDifference=+{first:#x}, '
                        f'expectedSHA256={hashlib.sha256(block).hexdigest()}, '
                        f'actualSHA256={hashlib.sha256(actual_bytes).hexdigest()}; no resend')
                if error:self.sram_transfer_notes.append(dict(address=address+offset,bytes=len(block),nativeError=error,resolvedByExactSramReadback=True,flashCommandReplayed=False))
            return
        for index in range(0,len(data),4):
            if time.monotonic()>deadline:raise TimeoutError('DAP write deadline; no replay')
            at=address+index
            if not index or at%1024==0:self.select(at)
            try:self.link.coresight_write(3,int.from_bytes(data[index:index+4],'little'),ap=True)
            except Exception as error:raise RuntimeError(f'DAP buffer write {at:#010x}: {error}') from error
            if index%64==60 or index+4==len(data):
                # Complete posted AP writes inside this same session. Never
                # reopen or resend an unknown write to make a burst appear OK.
                self.link.coresight_read(3,ap=False)

    def observe_failure(self):
        # One read-only DP observation, never ABORT/clear, reset or replay.
        # Its failure must not replace the original transport error.
        try:self.failure_state=dict(dpCtrlStat=hex(self.link.coresight_read(1,ap=False)))
        except Exception as error:self.failure_state=dict(dpReadError=f'{type(error).__name__}: {error}')

    def halt(self):
        # Enable debug first; do not combine first enable and halt on a
        # sleeping DA14585. The separate write/read is observable and bounded.
        initial=self.unit(DHCSR)
        if not initial&1:
            self.unit(DHCSR,value=0xA05F0001)
            if not self.unit(DHCSR)&1:raise RuntimeError('CPU debug enable failed')
        self.unit(DHCSR,value=0xA05F0003)
        end=time.monotonic()+1
        while True:
            last=self.unit(DHCSR)
            if last&0x20000:return
            if time.monotonic()>end:
                self.observe_failure()
                raise TimeoutError(f'CPU halt failed: DHCSR initial={initial:#010x}, last={last:#010x}; no replay')

    def reg(self,index,value):
        self.unit(DCRDR,value=value);self.unit(DCRSR,value=0x10000|index)
        end=time.monotonic()+1
        while not self.unit(DHCSR)&0x10000:
            if time.monotonic()>end:raise TimeoutError('Core register transfer failed')

    def prepare_helper_context(self):
        """Confirmed Full only: do not launch a C helper inside an old ISR/fault.

        Debug xPSR writes do not establish Thread mode. A single standard
        reset-and-halt is allowed before helper/Flash operations when the
        stopped core is in Handler mode; failure never retries or falls back.
        The public read-only probe does not call this method.
        """
        if not self.native or not self.unit(DHCSR)&0x20000:
            raise RuntimeError('Helper preparation requires a qualified halted CPU')
        before=self.link.register_read(16)&0xFFFFFFFF
        self.cpu_preparation=dict(xpsrBefore=f'0x{before:08X}',activeExceptionBefore=before&0x1FF,resetIssued=False)
        if before&0x1FF:
            self.cpu_preparation['resetIssued']=True
            result=self.link.reset(halt=True)
            self.cpu_preparation['nativeResetResult']=result
            if result!=0 or not self.unit(DHCSR)&0x20000:
                raise RuntimeError('Helper preparation reset/halt failed; no retry')
        after=self.link.register_read(16)&0xFFFFFFFF
        self.cpu_preparation['xpsrAfter']=f'0x{after:08X}'
        self.cpu_preparation['activeExceptionAfter']=after&0x1FF
        if after&0x1FF:
            raise RuntimeError('CPU remains in Handler mode; no helper or Flash operation')
        return self.cpu_preparation

    def start(self,data,deadline):
        self.halt()
        self.unit(0xE000E010,value=0)
        self.unit(0xE000E180,value=0xFFFFFFFF);self.unit(0xE000E280,value=0xFFFFFFFF)
        self.write(0x07FC0000,data,deadline)
        # One independent held transport boundary for an executable upload,
        # instead of reopening USB for each 64 bytes of every RAM transfer.
        self.reopen_halted()
        if self.read(0x07FC0000,len(data),deadline)!=data:raise RuntimeError('SRAM image readback mismatch')
        sp,pc=struct.unpack_from('<II',data)
        if not 0x07FC0000<sp<=0x07FD8000 or sp%4 or not 0x07FC0001<=pc<0x07FC0000+len(data) or not pc&1:
            raise RuntimeError('Invalid SRAM entry vectors')
        self.reg(20,1);self.reg(17,sp);self.reg(18,sp)
        self.reg(16,0x01000000);self.reg(15,pc&~1)
        self.unit(DHCSR,value=0xA05F0001)

    def close(self):
        # Every intermediate/failure close has explicit no-restart and retained
        # debug hold. No q/qc assumption and no implicit application start.
        if self.opened:
            link=self.link
            try:
                previous=link.exec_command('SetRestartOnClose = 0')
                if previous not in (0,1) or link.exec_command('SetRestartOnClose = 0')!=0:
                    raise RuntimeError('Final no-start close policy unknown')
                link.close()
            finally:
                self.opened=False;self.link=None
                del link


class FullBackend:
    def __init__(self, types, serial='', *, speed_khz=500, sram_chunk_bytes=256, poll_interval_ms=20, jlink_exe='', evidence_root=None):
        if type(speed_khz) is not int or not 1<=speed_khz<=4000 or type(sram_chunk_bytes) is not int or sram_chunk_bytes not in (64,128,256,512,1024,2048,4096) or type(poll_interval_ms) is not int or not 1<=poll_interval_ms<=1000:
            raise ValueError('Invalid Full transport options; no target accessed')
        self.speed_khz=speed_khz;self.sram_chunk_bytes=sram_chunk_bytes
        self.poll_interval=poll_interval_ms/1000;self.jlink_exe=jlink_exe
        self.types=types;self.serial=serial;self.dap=None
        self.sequence=0;self.target=None;self.started=False;self.helper=helper_bytes()
        self._last={}
        self.command_failure=None
        self.wrote_flash=False
        self.evidence_root=Path(evidence_root) if evidence_root is not None else None
        self.reader=runpy.run_path(str(Path(__file__).with_name('da14585-commander-readback.py')))
        if self.evidence_root is not None:
            # Validate native-path constraints before detection/programming,
            # never discover an unusable readback path after erase.
            self.reader['quote'](self.evidence_root/'ffffffff-3ffff'/'header-readback.bin')
            if len(str((self.evidence_root/'ffffffff-3ffff'/'header-readback.bin').resolve()))>=250:
                raise ValueError('Commander evidence path too long; no target accessed')
            executable=Path(jlink_exe) if jlink_exe else DLL.with_name('JLink.exe')
            self.commander=self.reader['verify_executable'](executable,
                input_file('firmware/ports/dialog/da14585/tools/vendor-dependencies.json'))

    def ensure_native(self):
        if self.dap is not None and not getattr(self.dap,'opened',True):
            self.dap=self.open_dap(self.target.serial)

    def open_dap(self, serial):
        return Dap(serial,self.speed_khz,self.sram_chunk_bytes,self.jlink_exe)

    def transport_options(self):
        return dict(speedKHz=self.speed_khz,sramChunkBytes=self.sram_chunk_bytes,
                    pollIntervalMilliseconds=round(self.poll_interval*1000))

    def reply(self,value):
        evidence=dict(self._last)
        evidence['transportOptions']=self.transport_options()
        if self.dap is not None:
            evidence['sramTransferNotes']=self.dap.sram_transfer_notes
            self.dap.sram_transfer_notes=[]
        preparation=getattr(self,'cpu_preparation',getattr(self.dap,'cpu_preparation',None))
        if preparation is not None:evidence['cpuPreparation']=preparation
        return self.types['Reply'](value=value,raw=(json.dumps(evidence,sort_keys=True)+'\n').encode())

    def command(self,cmd,address=0,length=0,payload=b'',timeout=60):
        context=dict(command=cmd,address=address,bytes=length,sequence=self.sequence+1,
                     commitAttempted=False,phase='halt-before-arguments')
        try:
            self.ensure_native()
            return self._command(cmd,address,length,payload,timeout,context)
        except Exception as error:
            # Freeze the failed boundary before cleanup, without another
            # target operation, command replay or replacement exception.
            self.command_failure=dict(context,error=f'{type(error).__name__}: {error}')
            if hasattr(self.dap,'native_messages'):
                self.command_failure['nativeDiagnosticTail']=list(self.dap.native_messages)
            if hasattr(self.dap,'failure_state'):
                self.command_failure['debugPortObservation']=dict(self.dap.failure_state)
            raise

    def _command(self,cmd,address,length,payload,timeout,context):
        end=time.monotonic()+timeout;dap=self.dap;dap.halt()
        if payload:
            context['phase']='write-and-check-payload'
            padded=payload+b'\0'*(-len(payload)%4);dap.write(DATA,padded,end)
            if dap.read(DATA,len(padded),end)!=padded:raise RuntimeError('Mailbox payload SRAM mismatch; command not issued')
        self.sequence+=1
        context['phase']='write-and-check-arguments'
        dap.write(BASE+8,struct.pack('<8I',self.sequence,address,length,zlib.crc32(payload),0,0,0,0),end)
        header=dap.read(BASE+8,32,end)
        if header!=struct.pack('<8I',self.sequence,address,length,zlib.crc32(payload),0,0,0,0):
            raise RuntimeError('Mailbox SRAM mismatch; command not issued')
        # Commit exactly once, after every argument/payload was checked.
        context.update(phase='commit-mailbox-once',commitAttempted=True)
        dap.unit(BASE+4,value=cmd)
        context['phase']='run-committed-helper'
        dap.unit(DHCSR,value=0xA05F0001)
        while True:
            if time.monotonic()>end:raise TimeoutError('Helper command result unknown; no replay')
            # Inspect SRAM while halted. Resuming the same in-progress helper
            # never commits or repeats its mailbox command.
            time.sleep(min(self.poll_interval,max(0,end-time.monotonic())))
            if time.monotonic()>end:raise TimeoutError('Helper command result unknown; no replay')
            context['phase']='halt-to-poll-result'
            dap.halt()
            context['phase']='read-result-flag'
            result=dap.unit(BASE+24)
            if result:break
            context['phase']='continue-same-helper-command'
            dap.unit(DHCSR,value=0xA05F0001)
        context['phase']='read-completion-sequence'
        outlen,seq,detail=struct.unpack('<III',dap.read(BASE+28,12,end))
        self._last=dict(command=cmd,sequence=seq,result=result,outputBytes=outlen,detail=detail)
        if seq!=self.sequence or result!=1:raise RuntimeError('Helper rejected command: '+json.dumps(self._last))
        if cmd in (1,2):
            if outlen>4096 or (cmd==2 and outlen!=length):raise RuntimeError('Partial helper read')
            context['phase']='read-completed-output'
            return dap.read(DATA,(outlen+3)&~3,end)[:outlen]
        return detail if cmd==3 else outlen

    def detect(self,timeout=60):
        if self.dap is None:
            self._last={'stage':'open-exact-dap'}
            try:
                self.dap=self.open_dap(self.serial)
            except ReadinessUnavailable as error:
                # Dap.__init__ has already completed no-start cleanup. Only
                # these explicit pre-CPU refusals reach the readiness loop.
                # Generic J-Link errors and SRAM/helper failures are terminal.
                raise self.types['NotReady'](str(error)) from error
            # No reconnect/reset fallback. From this point any error is terminal.
            self._last={'stage':'halt-before-helper'};self.dap.halt()
            self._last={'stage':'normalize-helper-context'}
            self.cpu_preparation=self.dap.prepare_helper_context()
            # Volatile programming state used by the vendor booter (DEV_PHASE).
            # Restore the previous bit before the sole final App start.
            self.sys_ctrl_before=self.dap.unit(0x50000012,2)
            self.dap.unit(0x50000012,2,self.sys_ctrl_before|4)
            self._last={'stage':'freeze-and-verify-watchdog'}
            # Aligned word writes avoid the old ARM-OB halfword path. Upper
            # half is WDOG_CTRL=0 / RESET_FREEZE=0 respectively.
            self.dap.unit(0x50003100,value=0xC8)
            self.dap.unit(0x50003300,value=8)
            time.sleep(.15)
            if self.dap.unit(0x50003100,2)&0x1FF!=0xC8:raise RuntimeError('Watchdog freeze/reload readback failed')
            end=time.monotonic()+timeout
            self.dap.write(BASE,b'\0'*40,end)
            self._last={'stage':'load-readback-start-helper'};self.dap.start(self.helper,end)
            while True:
                if time.monotonic()>end:raise TimeoutError('Fixed helper did not start')
                time.sleep(.01);self.dap.halt()
                if self.dap.unit(BASE)==MAGIC:break
                self.dap.unit(DHCSR,value=0xA05F0001)
        self._last={'stage':'read-helper-identity'};info=self.command(1,timeout=timeout)
        if len(info)<16:
            raise RuntimeError('Unqualified helper/Flash identity: short header '
                               f'({len(info)} bytes; expected at least 16); '
                               'no Flash erase/program issued')
        magic,version,jedec,mv=struct.unpack_from('<4I',info)
        if (magic,version)!=(MAGIC,2) or jedec!=0xA14013:
            # Report only the non-secret header, never the following OTP or
            # calibration bytes. A mismatch remains terminal, not NotReady.
            raise RuntimeError('Unqualified helper/Flash identity: '
                               f'observed magic=0x{magic:08X}, version={version}, '
                               f'JEDEC=0x{jedec:06X}, voltage={mv}mV, bytes={len(info)}; '
                               f'expected magic=0x{MAGIC:08X}, version=2, JEDEC=0xA14013; '
                               'no Flash erase/program issued')
        target=self.types['Target'](self.dap.serial,'585',0x410CC200,jedec,1<<(jedec&255))
        if self.target is not None and target!=self.target:raise RuntimeError('Same-session identity changed')
        self.target=target
        return self.reply(target)

    def hold_no_start(self,timeout=60):
        self.ensure_native()
        self.dap.halt()
        return self.reply(self.types['HoldState'](True,False,True,False))

    def read_identity(self,timeout=60):
        info=self.command(1,timeout=timeout)
        return self.reply(self.types['Identity'](info[16:22],info[24:72]))

    def read_flash(self,address,length,timeout=60):
        if address<0 or length<=0 or address+length>0x40000:raise ValueError('Flash read range')
        end=time.monotonic()+timeout;out=bytearray()
        for at in range(address,address+length,4096):
            remaining=end-time.monotonic()
            if remaining<=0:raise TimeoutError('Flash read deadline')
            count=min(4096,address+length-at)
            if self.wrote_flash:
                if self.evidence_root is None or self.target is None:
                    raise RuntimeError('Owned Commander readback evidence is required; no fallback')
                if self.dap.opened:self.dap.close()
                self.sequence+=1
                folder=self.evidence_root/f'{self.sequence:08x}-{at:05x}'
                try:
                    data,self._last=self.reader['read'](self.commander,self.target.serial,self.speed_khz,
                        self.helper,self.sequence-1,at,count,folder,remaining,
                        manifest=input_file('firmware/ports/dialog/da14585/tools/vendor-dependencies.json'))
                except Exception as error:
                    self.command_failure=dict(command=2,address=at,bytes=count,sequence=self.sequence,
                        phase='commander-independent-readback',commitAttempted=None,
                        error=f'{type(error).__name__}: {error}',evidence=str(folder))
                    raise
                out.extend(data)
            else:
                out.extend(self.command(2,at,count,timeout=remaining))
        return self.reply(bytes(out))

    def sample_voltage(self,timeout=60):
        mv=self.command(3,timeout=timeout)
        return self.reply(self.types['Voltage'](mv,time.monotonic(),0<mv<=3600))

    def erase_sector(self,address,length,timeout=60):
        self.wrote_flash=True
        return self.reply(self.command(4,address,length,timeout=timeout))

    def program_page(self,address,data,timeout=60):
        self.wrote_flash=True
        return self.reply(self.command(5,address,len(data),data,timeout))

    def start_application(self,boot,image_sha,timeout=60):
        if self.started or len(image_sha)!=64:raise RuntimeError('Invalid/repeated final start')
        if self.target is None:raise RuntimeError('Unowned target; no diagnostic RAM write or start')
        self.ensure_native()
        import runpy
        state=runpy.run_path(str(Path(__file__).with_name('da14585-full-state.py')))
        address,size=state['RESET_TRACE_ADDRESS'],state['RESET_TRACE_BYTES']
        # The fixed helper owns low SRAM (code/RW/stack ends 0x07FC1120),
        # mailbox 0x07FC7C00 and data 0x07FC8000. The supported signed App's
        # UNINIT32 at 0x07FD7398 is outside them and below loader ER_2.
        # No helper command or Flash mutation follows this held RAM clear.
        deadline=time.monotonic()+timeout
        self.dap.halt()
        self.dap.write(address,b'\0'*size,deadline)
        self.dap.reopen_halted()
        if self.dap.read(address,size,deadline)!=b'\0'*size:
            raise RuntimeError('Diagnostic RAM clear readback mismatch; no start or replay')
        current=self.dap.unit(0x50000012,2)
        # Full may begin from an interrupted programmer whose DEV_PHASE bit
        # was already set. Finish in normal product mode, preserving all other
        # bits, rather than leaking that temporary programming state to App.
        self.dap.unit(0x50000012,2,current&~4)
        self.dap.start(boot,deadline);self.started=True
        self._last=dict(finalStart=True,preparedSha256=image_sha,bootSha256=hashlib.sha256(boot).hexdigest(),
                        diagnosticRamCleared=True,diagnosticRamAddress=address,diagnosticRamBytes=size,
                        diagnosticRamReadbackVerified=True)
        return self.reply(True)

    def close_no_start(self,timeout=60):
        attached=self.dap is not None
        if self.dap is not None and getattr(self.dap,'opened',True):
            try:
                if not self.started:self.dap.halt()
            finally:
                if self.started:self.dap.link.exec_command('SetSkipDebugDeInit = 0')
                self.dap.close()
        elif self.command_failure is not None:
            # The read-only child has exited; a failed/unknown halt is not
            # permission to reconnect/reset during cleanup. Preserve it.
            raise RuntimeError('Commander close/halt unqualified; no cleanup reconnect or replay')
        self._last=dict(closed=True,implicitStart=False,nativeConnectCalled=attached,finalStartPreviouslyIssued=self.started)
        if self.command_failure is not None:self._last['failedHelperExchange']=self.command_failure
        if attached:self._last['nativeDiagnosticTail']=getattr(self.dap,'native_messages',[])
        if attached and hasattr(self.dap,'failure_state'):
            self._last['debugPortObservation']=dict(self.dap.failure_state)
        return self.reply(True)
