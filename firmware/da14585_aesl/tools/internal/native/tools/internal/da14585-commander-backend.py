"""Commander-only Full candidate, sharing the guarded transaction and helper.

No PyLink calls, Flash mutation batching, reset-on-error or transport fallback.
The public portable-writer gate and native helper/transaction limits remain fixed.
"""
from pathlib import Path
import hashlib
import re
import runpy
import struct
import time
import zlib

_fixed = runpy.run_path(str(Path(__file__).with_name('da14585-full-backend.py')))
_sessions = runpy.run_path(str(Path(__file__).with_name('da14585-commander-session.py')))


class CommanderDap(_fixed['Dap']):
    def __init__(self, serial, speed, sram_chunk_bytes, jlink_exe, directory):
        self.serial, self.speed, self.sram_chunk_bytes = serial, speed, sram_chunk_bytes
        self.directory = Path(directory)
        self.session_number = 0
        self.executable = Path(jlink_exe) if jlink_exe else _fixed['DLL'].with_name('JLink.exe')
        self.opened = self.native = False
        self.native_messages, self.sram_transfer_notes = [], []
        self.link = self
        self._open_session()

    def _open_session(self):
        self.session_number += 1
        self.cache_halted = False
        self.session = _sessions['Session'](self.executable,
            _fixed['input_file']('firmware/ports/dialog/da14585/tools/vendor-dependencies.json'),
            self.serial, self.speed, self.directory/f'native-{self.session_number:04d}')
        self.serial = self.session.serial
        self.opened = True
        try:
            self.session.exchange('connect')
            if self.unit(0xE000ED00) != 0x410CC200 or bytes(self.unit(0x50003200+i, 1) for i in range(3)) != b'585':
                raise RuntimeError('Commander silicon mismatch; no helper uploaded')
            self.native = True
        except BaseException:
            self.session.failed = True
            self.close()
            raise

    def exec_command(self, command):
        if command not in ('SetRestartOnClose = 0', 'SetSkipDebugDeInit = 0', 'SetSkipDebugDeInit = 1'):
            raise ValueError('Unallocated Commander close policy')
        self.session.exchange('exec '+command)
        return 0

    def register_read(self, index):
        if index not in (15,16):
            raise ValueError('Only PC/xPSR observation is allocated')
        text = self.session.exchange('regs')
        value = re.search(r'(?i)\b'+('PC' if index==15 else 'XPSR')+r'\s*=\s*([0-9a-f]{8})', text)
        if value is None:
            raise RuntimeError('Commander omitted xPSR')
        return int(value[1], 16)

    def start(self, data, deadline):
        self.checkpoint_mode = False
        super().start(data, deadline)
        if hashlib.sha256(data).hexdigest() == _fixed['HELPER_SHA']:
            marker = bytes.fromhex('bff34f8f00be00bf')
            if data.count(marker) != 2:
                raise RuntimeError('Fixed helper checkpoint identity differs')
            first = data.index(marker)
            second = data.index(marker, first+len(marker))
            self.checkpoint_address = 0x07FC0000+first+4
            self.completion_address = 0x07FC0000+second+4
            self.checkpoint_mode = True
            self.wait_checkpoint(deadline)

    @staticmethod
    def scalar_response(text, address):
        values = re.findall(rf'(?im)^\s*{address:08x}\s*=\s*([0-9a-f]{{8}})\s*$', text)
        if len(values) != 1:
            raise RuntimeError('Incomplete/ambiguous helper boundary readback')
        return int(values[0], 16)

    def qualify_checkpoint(self, wait_text, status_text, deadline):
        if not self.scalar_response(status_text, 0xE000EDF0) & 0x20000:
            raise RuntimeError('Helper did not stop at completion; no replay')
        # Pinned Commander's WH already observes the stop and prints its fresh
        # register snapshot. Require that successful frame and independent
        # DHCSR S_HALT; do not repeat Halt/Regs on a known self-stopped core.
        if time.monotonic() > deadline or not re.search(r'(?im)^\s*CPU halted\.\s*$',wait_text):
            raise RuntimeError('Native wait did not qualify a stopped core; no replay')
        pcs = re.findall(r'(?i)\bPC\s*=\s*([0-9a-f]{8})', wait_text)
        if len(pcs) != 1:
            raise RuntimeError('Commander omitted/duplicated completion PC')
        pc = int(pcs[0], 16)
        if pc not in (self.checkpoint_address, self.checkpoint_address+2):
            raise RuntimeError(f'Unexpected helper stop PC 0x{pc:08X}; no replay')
        self.stopped_pc, self.cache_halted = pc, True

    def wait_checkpoint(self, deadline):
        remaining = deadline-time.monotonic()
        if remaining <= 0:
            raise TimeoutError('Helper completion deadline expired; no replay')
        try:
            replies = self.session.helper_group('wait', wait_ms=max(1,int(remaining*1000)), timeout=remaining)
            self.qualify_checkpoint(replies[-2], replies[-1], deadline)
        except BaseException:
            self.session.failed = True
            raise

    def stage_command(self, header, payload, deadline):
        # Payload/header bytes only: this does not include the command word,
        # CPU registers, a Go, or a second Flash operation. Every staged block
        # receives native verify plus an independent exact host-side capture.
        if len(header) != 32 or len(payload) > 256 or len(payload) % 4:
            raise ValueError('Unallocated helper staging lengths')
        commands, captures = [], []
        regions = ([(_fixed['DATA'],payload)] if payload else []) + [(_fixed['BASE']+8,header)]
        for address,data in regions:
            for offset in range(0,len(data),self.sram_chunk_bytes):
                block, at = data[offset:offset+self.sram_chunk_bytes], address+offset
                source, capture = self.session.new_file(block), self.session.new_file()
                quote = self.session.reader['quote']
                commands += [f'loadbin {quote(source)}, 0x{at:08x}, noreset',
                             f'verifybin {quote(source)}, 0x{at:08x}',
                             f'savebin {quote(capture)}, 0x{at:08x}, 0x{len(block):x}']
                captures.append((capture,block,at))
        self.session.memory_batch(commands, deadline-time.monotonic())
        for path,block,address in captures:
            if path.read_bytes() != block:
                self.session.failed = True
                raise RuntimeError(f'Independent staged SRAM readback differs at 0x{address:08x}; command not issued')

    def commit_mailbox(self, command, deadline):
        self.__dict__.pop('_committed_checkpoint', None)
        try:
            replies = self.session.helper_group('commit', command=command, timeout=deadline-time.monotonic())
            if self.scalar_response(replies[1], _fixed['BASE']+4) != command:
                raise RuntimeError('Mailbox commit not confirmed while held; no run or resend')
            if not self.scalar_response(replies[2], 0xE000EDF0) & 0x20000:
                raise RuntimeError('Committed helper is not held; no run or resend')
            # Fresh physical proof, bound to this session's exact command
            # boundary. It is consumed once before SetPC, even on failure.
            self._committed_checkpoint = (self.session,self.session.counter,self.checkpoint_address)
        except BaseException:
            self.session.failed = True
            raise

    def execute_checkpoint(self, deadline):
        proof = self.__dict__.pop('_committed_checkpoint', None)
        if not getattr(self,'checkpoint_mode',False) or proof != (self.session,self.session.counter,self.checkpoint_address):
            self.session.failed = True
            raise RuntimeError('No fresh committed held checkpoint; no run or replay')
        try:
            self.session.exchange(f'SetPC 0x{self.checkpoint_address+2:08x}',deadline-time.monotonic())
            self.checkpoint_address = self.completion_address
            self.cache_halted = False
            remaining = deadline-time.monotonic()
            replies = self.session.helper_group('run-wait', wait_ms=max(1,int(remaining*1000)), timeout=remaining)
            self.qualify_checkpoint(replies[-2], replies[-1], deadline)
        except BaseException:
            self.session.failed = True
            raise

    def resume_checkpoint(self):
        if not getattr(self,'checkpoint_mode',False) or not self.unit(0xE000EDF0)&0x20000:
            raise RuntimeError('No confirmed helper checkpoint; no run')
        # Skip only this pinned BKPT; continue at its following NOP.
        self.session.exchange(f'SetPC 0x{self.checkpoint_address+2:08x}')
        self.session.exchange('g')
        self.cache_halted = False
        self.checkpoint_address = self.completion_address

    def reset(self, *, halt):
        if halt is not True:
            raise ValueError('Only confirmed preparation reset-and-halt is allocated')
        self.session.exchange('r')
        self.halt()
        return 0

    def unit(self, address, size=4, value=None):
        if size not in (1, 2, 4) or address % size:
            raise ValueError('Commander scalar width/alignment')
        if value is None:
            if not (0x07F80000 <= address < 0x07FD8000 or 0x50000000 <= address < 0x50004000 or 0xE0000000 <= address < 0xE0100000):
                raise ValueError('Unallocated Commander scalar read')
            text = self.session.exchange(f'mem{size*8} 0x{address:08x}, 1')
            found = re.search(rf'(?i)\b{address:08x}\s*=\s*([0-9a-f]{{{size*2}}})(?![0-9a-f])', text)
            if found is None:
                raise RuntimeError('Commander scalar read proof incomplete')
            return int(found[1], 16)
        allowed = {0x50000012:2, 0x50003100:4, 0x50003300:4,
            0xE000EDF0:4, 0xE000EDF4:4, 0xE000EDF8:4, 0xE000E010:4, 0xE000E180:4, 0xE000E280:4,
            0x07FC7C04:4}
        if allowed.get(address) != size or type(value) is not int or not 0 <= value < 1 << (size*8):
            raise ValueError('Unallocated Commander scalar write')
        if address == 0xE000EDF0:
            if value != 0xA05F0001:
                raise ValueError('Commander owns CPU run/halt state')
            # A raw DHCSR run write leaves J-Link's CPU-state cache halted;
            # its next native Halt may then be skipped. Go updates both.
            self.session.exchange('g')
            self.cache_halted = False
            return
        self.session.exchange(f'w{size} 0x{address:08x}, 0x{value:0{size*2}x}')

    def reg(self, index, value):
        name = {20:'CFBP', 17:'MSP', 18:'PSP', 16:'XPSR', 15:'R15'}.get(index)
        if name is None or type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
            raise ValueError('Unallocated Commander core register')
        if index == 15:
            self.session.exchange(f'SetPC 0x{value:08x}')
            return
        self.session.exchange(f'wreg {name}, 0x{value:08x}')

    def read(self, address, length, deadline):
        if address % 4 or length % 4 or not 0x07FC0000 <= address < address+length <= 0x07FD8000:
            raise ValueError('Commander buffer read must stay in aligned SRAM')
        output, captures, commands = bytearray(), [], []
        for at in range(address, address+length, self.sram_chunk_bytes):
            count = min(self.sram_chunk_bytes, address+length-at)
            path = self.session.new_file()
            commands.append(f'savebin {self.session.reader["quote"](path)}, 0x{at:08x}, 0x{count:x}')
            captures.append((path,count))
        self.session.memory_batch(commands, deadline-time.monotonic())
        for path,count in captures:
            data = path.read_bytes()
            if len(data) != count:
                raise RuntimeError('Commander buffer read length differs; no retry')
            output.extend(data)
        return bytes(output)

    def write(self, address, data, deadline):
        if address % 4 or len(data) % 4 or not 0x07FC0000 <= address < address+len(data) <= 0x07FD8000:
            raise ValueError('Commander buffer write must stay in aligned SRAM')
        for offset in range(0, len(data), self.sram_chunk_bytes):
            block, at = data[offset:offset+self.sram_chunk_bytes], address+offset
            path = self.session.new_file(block)
            capture = self.session.new_file()
            self.session.memory_batch([
                f'loadbin {self.session.reader["quote"](path)}, 0x{at:08x}, noreset',
                f'verifybin {self.session.reader["quote"](path)}, 0x{at:08x}',
                f'savebin {self.session.reader["quote"](capture)}, 0x{at:08x}, 0x{len(block):x}',
            ], deadline-time.monotonic())
            if capture.read_bytes() != block:
                raise RuntimeError('Independent Commander SRAM readback differs; no resend')

    def halt(self):
        initial = self.unit(0xE000EDF0)
        if initial & 0x20000 and getattr(self, 'cache_halted', False):
            return
        if getattr(self,'checkpoint_mode',False) and not initial & 0x20000:
            self.session.failed = True
            raise RuntimeError('Completion helper still running; no forced halt or replay')
        if not initial & 1:
            self.physical_control(0xA05F0001)
            if not self.unit(0xE000EDF0) & 1:
                raise RuntimeError('Commander debug enable not established')
        if not initial & 0x20000:
            # Normal physical halt request, not a fallback after Native Halt
            # failure. Qualify S_HALT before synchronizing the native cache.
            self.physical_control(0xA05F0003)
            deadline = time.monotonic()+1
            while not self.unit(0xE000EDF0) & 0x20000:
                if time.monotonic() >= deadline:
                    self.session.failed = True
                    raise RuntimeError('Commander physical CPU halt not established; no replay')
        self.session.exchange('halt')
        if not self.unit(0xE000EDF0) & 0x20000:
            raise RuntimeError('Commander CPU halt not established; no replay')
        self.cache_halted = True

    def physical_control(self, value):
        # Avoid Commander's single-word memory-write path for DHCSR. Issue
        # one qualified AP DRW write and explicitly drain the posted write.
        # No repeat/fallback follows a missing ACK, bad TAR or sticky error.
        def result(text):
            match = re.search(r'=\s*0x([0-9a-f]{8})', text, re.I)
            if match is None:
                raise RuntimeError('Incomplete native CoreSight register proof')
            return int(match[1],16)
        try:
            selected = self.session.debug_control_group('select')
            csw = result(selected[-1])
            if csw & 7 != 2 or (csw >> 4) & 3 not in (0,1):
                raise RuntimeError('Unqualified CoreSight word-access state')
            addressed = self.session.debug_control_group('address')
            if result(addressed[-1]) != 0xE000EDF0:
                raise RuntimeError('CoreSight TAR differs; no control write issued')
            completed = self.session.debug_control_group('control',value)
            status = result(completed[-1])
            if status & 0xA2:
                raise RuntimeError('CoreSight posted control write failed; no retry')
        except BaseException:
            self.session.failed = True
            raise

    def reopen_halted(self):
        if not self.unit(0xE000EDF0) & 0x20000:
            raise RuntimeError('Commander boundary requires an already halted CPU')
        self.close()
        self._open_session()
        if not self.unit(0xE000EDF0) & 0x20000:
            raise RuntimeError('Commander boundary released the held CPU')

    def close(self):
        if self.opened:
            self.opened = False
            self.session.close()


class CommanderFullBackend(_fixed['FullBackend']):
    def __init__(self, *args, **kwargs):
        if 'poll_interval_ms' in kwargs:
            raise ValueError('Commander completion stops do not use polling intervals')
        super().__init__(*args, **kwargs)
        if self.evidence_root is None:
            raise ValueError('Commander candidate requires owned evidence')
        longest = self.evidence_root/'session-ffff'/'native-ffff'/'capture-ffffffff.bin'
        self.reader['quote'](longest)
        if len(str(longest.resolve())) >= 250:
            raise ValueError('Commander session path too long; no target accessed')
        self.native_sessions = 0

    def transport_options(self):
        return dict(speedKHz=self.speed_khz,sramChunkBytes=self.sram_chunk_bytes,
                    completionWait='helper-self-stop',hostExchangeProfile='bounded-helper-groups-v2')

    def open_dap(self, serial):
        self.native_sessions += 1
        return CommanderDap(serial, self.speed_khz, self.sram_chunk_bytes, self.jlink_exe,
                            self.evidence_root/f'session-{self.native_sessions:04d}')

    def _command(self, cmd, address, length, payload, timeout, context):
        end=time.monotonic()+timeout;dap=self.dap;dap.halt()
        self.sequence+=1
        context['phase']='stage-and-verify-arguments-and-payload'
        header=struct.pack('<8I',self.sequence,address,length,zlib.crc32(payload),0,0,0,0)
        dap.stage_command(header,payload+b'\0'*(-len(payload)%4),end)
        context.update(phase='commit-mailbox-once',commitAttempted=True)
        dap.commit_mailbox(cmd,end)
        context['phase']='run-once-to-completion'
        dap.execute_checkpoint(end)
        context['phase']='independent-completion-record'
        box=struct.unpack('<10I',dap.read(_fixed['BASE'],40,end))
        magic,command,sequence,actual_address,actual_length,crc,result,outlen,ack,detail=box
        self._last=dict(command=cmd,sequence=ack,result=result,outputBytes=outlen,detail=detail,
                        transport='Commander self-stopping helper v2')
        if (magic,command,sequence,actual_address,actual_length,crc,ack)!= \
                (_fixed['MAGIC'],0,self.sequence,address,length,zlib.crc32(payload),self.sequence) or result!=1:
            raise RuntimeError('Helper rejected/incomplete command: '+str(self._last))
        if cmd in (1,2):
            if outlen>4096 or (cmd==2 and outlen!=length):
                raise RuntimeError('Partial helper read')
            output = dap.read(_fixed['DATA'],(outlen+3)&~3,end)[:outlen]
            if cmd == 1 and getattr(self,'_capture_detect_identity',False):
                self._detect_identity = dict(info=output,sequence=self.sequence,dap=dap,
                    session=dap.session,counter=dap.session.counter,observedAt=time.monotonic())
            return output
        return detail if cmd==3 else outlen

    def detect(self, timeout=60):
        # IDENTITY1 already returns JEDEC, OTP address and calibration in one
        # fresh observation. Retain it only for the immediately adjacent
        # read_identity API call, never across native activity or timeouts.
        self._detect_identity = None
        self._capture_detect_identity = True
        try:
            return super().detect(timeout=timeout)
        except BaseException:
            self._detect_identity = None
            raise
        finally:
            self._capture_detect_identity = False

    def read_identity(self, timeout=60):
        snapshot = self.__dict__.pop('_detect_identity',None)
        if snapshot is not None and snapshot['sequence'] == self.sequence and snapshot['dap'] is self.dap and \
                snapshot['session'] is self.dap.session and snapshot['counter'] == self.dap.session.counter and \
                0 <= time.monotonic()-snapshot['observedAt'] <= 2 and len(snapshot['info']) == 72:
            info = snapshot['info']
            self._last['identityObservation'] = dict(sharedWithImmediatelyPrecedingDetect=True,
                nativeSequence=self.sequence,observedMonotonic=snapshot['observedAt'])
            return self.reply(self.types['Identity'](info[16:22],info[24:72]))
        return super().read_identity(timeout=timeout)

    @property
    def native_power_guard(self):
        return self.types['NativePowerGuard'](_fixed['HELPER_SHA'],2400,3600)

    def guarded_mutation(self, command, address, length, payload, timeout):
        self.wrote_flash = True
        before = time.monotonic()
        actual = self.command(command,address,length,payload,timeout)
        # _command checked the whole response, including address/length/CRC,
        # successful result and exact sequence, before this receipt is made.
        sample = self.types['Voltage'](self._last['detail'],before,True)
        proof = self.types['GuardedMutation'](actual,command,address,zlib.crc32(payload),
            self.sequence,sample,_fixed['HELPER_SHA'])
        self._last['nativePowerGuard'] = dict(helperSha256=_fixed['HELPER_SHA'],
            minimumMillivolts=2400,maximumMillivolts=3600,measuredMillivolts=sample.millivolts,
            measuredBeforeFlashOpcode=True,command=command,address=address,
            bytes=length,payloadCRC32=zlib.crc32(payload),sequence=self.sequence)
        return self.reply(proof)

    def erase_sector(self, address, length, timeout=60):
        return self.guarded_mutation(4,address,length,b'',timeout)

    def program_page(self, address, data, timeout=60):
        return self.guarded_mutation(5,address,len(data),data,timeout)

    def read_flash(self, address, length, timeout=60):
        if address < 0 or length <= 0 or address+length > 0x40000:
            raise ValueError('Flash read range')
        end, output = time.monotonic()+timeout, bytearray()
        for at in range(address, address+length, 4096):
            count = min(4096, address+length-at)
            output.extend(self.command(2, at, count, timeout=end-time.monotonic()))
        return self.reply(bytes(output))
