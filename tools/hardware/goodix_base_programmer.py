"""Hardware-only transport for the unsigned GR base first-install format.

Transport methods are adapted from the maintained portable programmer. Pro
package admission and migration are excluded. Runtime dependencies are stdlib,
explicit vendor tools, and the local programmer-private codec/log namespace.
No hardware operation is performed at import or during offline preparation.
"""
from contextlib import chdir
import importlib.util
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/sdk'))
from goodix_base_full import configured_image, first_install_image, require
import goodix_full_reset as full_reset
from goodix_tool_log import log_class, report_confirmed_mcu


def sha(raw):return hashlib.sha256(raw).hexdigest()


def target_voltage(native):
    values=re.findall(r'VTref\s*=\s*(\d+(?:\.\d+)?)V',native)
    require(values and 2.7<=float(values[-1])<=3.6,
            'No safe live target voltage (2.7..3.6 V); no write')
    return float(values[-1])


class GProgrammerTransport:
    def __init__(self,options):
        self.options=options
        self.folder=Path(options.evidence).resolve()
        self.console=Path(options.console).resolve(strict=True)
        self.jlink=Path(options.jlink).resolve(strict=True)
        require(not options.snr or re.fullmatch(r'\d+',options.snr) is not None,'Invalid J-Link serial')
        require(not options.address or re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',options.address) is not None,
                             'Invalid exact preflight address')
        spec=importlib.util.spec_from_file_location('gr_portable_workflow',ROOT/'tools/hardware/flash-workflow.py')
        self.workflow=importlib.util.module_from_spec(spec)
        sys.modules[spec.name]=self.workflow;spec.loader.exec_module(self.workflow)
        self.log=log_class(self.workflow)(self.folder,self.workflow.Board(options.chip,options.chip,'goodix'),
                                   'full',1,verbose=options.verbose)
        self.counter=0
        self.identity=None
        self.write_started=False
        self.write_acknowledged=False
        self.host_nvds_verified=False
        self.application_start_reset_requested=False
        (self.folder/'operator-options.json').write_text(json.dumps(dict(
            retryDetect=options.retry_detect, detectTimeoutSeconds=options.detect_timeout,
            operationTimeoutSeconds=getattr(options,'operation_timeout',300),
            diagnosticFrequencyHz=getattr(options,'frequency_hz',500000),
            flashTransferFrequency='vendor-managed', fullReadbackRequired=True),indent=2)+'\n')
        self._selection()  # Freeze the unique probe before any target access.

    def command(self,args,label,timeout=None):
        budget=getattr(self.options,'operation_timeout',300)
        timeout=budget if timeout is None else min(timeout,budget)
        self.counter+=1
        self.last_stage=label
        # Vendor loaders/configs are relative to the installed console, not
        # the caller's cwd or a session worktree. Do not copy SDKs into it.
        cwd=self.console.parent if Path(args[0])==self.console else Path.cwd()
        with chdir(cwd):
            result=self.workflow.run_command(list(map(str,args)),self.log,label,self.counter,operation_timeout=timeout)
        self.last_native_result=(label,result.code)
        text=result.text
        (self.folder/f'{self.counter:03d}-{label}.log').write_text(text,encoding='utf-8')
        require(result.code==0 and not re.search(
            r'(?i)could not connect|cannot connect|\bfailed\b|not_supported|could not read|syntax error|unknown command',text),
            'Hardware stage refused/failed: '+label)
        return text

    def detect(self,*,timeout=None):
        result=self.folder/f'identity-{self.counter}.json'
        cmd=[self.options.powershell,'-NoProfile','-File',ROOT/'tools/platforms/goodix/common/programmer/internal/goodix-stock-entry.ps1',
             '-Action','probe','-Chip',self.options.chip,'-EvidenceDir',self.folder/f'probe-{self.counter}',
             '-ResultFile',result,'-Snr',self.options.snr,'-JLinkExe',self.jlink,'-LeaveHalted']
        cmd+=['-FrequencyHz',str(getattr(self.options,'frequency_hz',500000))]
        if self.options.retry_detect:cmd+=['-CaptureWindow']
        cmd+=['-ReadProductionCache']
        try:self.command(cmd,'detect',min(60,timeout) if timeout is not None else 60)
        except ValueError:
            text=(self.folder/f'{self.counter:03d}-detect.log').read_text(errors='replace')
            # No mismatch/denial/unknown result is treated as mere no contact.
            if (getattr(self,'last_native_result',None) in (('detect',0),('detect',1))
                and re.search(r'(?i)\bno target (?:found|connected)\b|Cannot connect to target; (?:native capture window expired|probe-only no-contact evidence retained)',text)
                and not re.search(r'(?i)mismatch|denied|unknown|unconfirmed|reset fallback|no (?:device|probe) (?:found|connected)|timed out|transport interrupted|E000ED00\s*=|00803214\s*=|GR_CAPTURE_HALTED:',text)):
                return None
            raise
        self.identity=json.loads(result.read_text(encoding='utf-8-sig'))
        native='\n'.join(p.read_text(errors='replace') for p in (self.folder/f'probe-{self.counter-1}').glob('*.log'))
        self.identity['targetVoltage']=target_voltage(native)
        if self.identity.get('chipConfirmed') is True:
            report_confirmed_mcu(self.log, self.identity, phase='detect')
            self.log.emit('detect',f'J-Link {self.options.snr}; VTref={self.identity["targetVoltage"]:.3f} V. '
                          'MCU detection complete; Full erase/program has not started.',level='notice',
                          detection_only=True,flash_write_started=False)
        return self.identity

    def _selection(self):
        text=self.command([self.console,'device','1'],'probes',30)
        rows=re.findall(r'(?m)^\s*(\d+)\s+.+?\s+(\d{6,})\s*$',text)
        if not self.options.snr:
            require(len(rows)==1,'Multiple/no J-Link probes; select an exact J-Link serial')
            self.options.snr=rows[0][1]
        selected=[index for index,serial in rows if serial==self.options.snr]
        require(len(selected)==1,'Exact J-Link serial not unique')
        return selected[0]

    def _read(self,identity,size,label,*,address=None):
        path=self.folder/f'{self.counter}-{label}.bin'
        self.command([self.console,'readFlash','--chip',identity['chip'].upper(),
            '--flashSize',identity['flashBytes']//1024,'--address',hex(identity['baseAddress'] if address is None else address),
            '--size',size,'--save',path,'--jlink',self._selection()],label)
        data=path.read_bytes()
        require(len(data)==size,'Incomplete '+label)
        return data

    def read_full(self,identity): return self._read(identity,identity['flashBytes'],'full-readback')

    def write_once(self,image,identity):
        require(sha(self.image.read_bytes())==self.report['image_sha256'], 'Prepared image changed')
        # Identity was captured atomically with package detection, before the
        # physical loader used SRAM. Two fresh protected-NVDS reads agree.
        # Do not reset/relaunch the old App just to recreate this RAM cache:
        # that introduced another manual-RESET window and checkpoint writes.
        # Every hardware stage still selects the frozen exact probe; the
        # vendor writer uses the detected MCU/capacity, then full readback.
        self.log.emit('write', 'Identity and protected Factory data reviewed; starting one Full write.',level='notice')
        self.write_started=True
        self.command([self.options.powershell,'-NoProfile','-File',ROOT/'tools/platforms/goodix/common/programmer/internal/flash-goodix-gprogrammer.ps1',
            '-Target','gr5331' if identity['chip']=='gr5331' else 'gr5515','-Mode','download','-Layout','ota',
            '-Firmware',self.image,'-Snr',self.options.snr,'-ConsoleExe',self.console,
            '-GProgrammerDir',Path(self.console).parent,
            '-FlashStart',hex(identity['baseAddress']),'-DownloadAddress',hex(identity['baseAddress']),
            '-FlashSizeKB',identity['flashBytes']//1024,'-ProductType','2' if identity['chip']=='gr5331' else '0',
            '-EraseAll','-NoRunImmediately'],'single-full-write')
        self.write_acknowledged=True

    def _jlink(self,commands,label):
        # Install close behavior before explicit connect. AutoConnect would
        # otherwise run before the Commander script's first command.
        script=self.folder/f'{self.counter}-{label}.jlink';script.write_text('\n'.join(
            ['exec SetRestartOnClose = 0','connect']+commands+['qc','']))
        return self.command([self.jlink,'-SelectEmuBySN',self.options.snr,'-Device','Cortex-M4','-If','SWD',
            '-Speed',str(getattr(self.options,'frequency_hz',500000)//1000),'-NoGui','1','-AutoConnect','0','-ExitOnError','1','-CommanderScript',script],label,10)

    def _otp(self,address,size,label):
        # Vendor's documented READ operation, never writeOtp/readNvds (the
        # latter can initialize storage). Only SDK-defined factory OTP fields.
        text=self.command([self.console,'readOtp','--chip','GR5331',
            '--address',hex(address),'--size',hex(size),'--jlink',self._selection()],label,30)
        rows=re.findall(r'(?m)^Read\(Hex\)\s*:\s*([0-9a-fA-F ]+)\s*$',text)
        require(len(rows)==1,'Missing/ambiguous factory OTP read; no erase')
        raw=bytes.fromhex(rows[0])
        require(len(raw)==size,'Incomplete factory OTP read; no erase')
        return raw

    def _factory_5331(self,identity,trim):
        # Exact SDK sys_trim_info_sync supports security-register layouts
        # (trim0=0,trim1=0x100) and (trim0=0x1000,trim1=0x2000).
        # Match the fresh SDK-validated immutable production header, not a
        # host archive, MAC prefix, temporary GAP address or guessed RAM tail.
        matches=[]
        for start,bt in ((0,0x100),(0x1000,0x2000)):
            first=self._otp(start,96,f'factory-trim-{start:x}-0')
            again=self._otp(start,96,f'factory-trim-{start:x}-1')
            require(len(first)==len(again)==96 and first==again,'Unstable factory OTP; no erase')
            if first[:32]==trim[:32] and first[:2]==b'DG':
                addr=self._otp(bt,8,f'factory-address-{bt:x}-0')
                repeat=self._otp(bt,8,f'factory-address-{bt:x}-1')
                require(len(addr)==len(repeat)==8 and addr==repeat,'Unstable factory address; no erase')
                matches.append((bt,addr))
        if not matches or len({raw for bt,raw in matches})!=1:
            self.factory_source=dict(kind='factory-address-unavailable',
                reason='Factory BLE address missing or conflicting; using the verified immutable UID')
            return None,None
        # The vendor readOtp interface may normalize both documented logical
        # layouts to the same physical security field. Accept only identical
        # complete values, never choose between conflicting address records.
        bt,raw=matches[0]
        self.factory_source=dict(kind='gr5331-chip-trim1-public',otpAddresses=[p for p,v in matches],
            productionMatched=True,doubleReadSame=True,sha256=sha(raw))
        return raw[:6][::-1],0

    def cleanup_before_reset(self,identity):
        """Only after full readback, while our exact probe still owns halt."""
        require(self.identity==identity and self.host_nvds_verified,
                             'Reset cleanup lacks verified target/readback ownership; App remains halted')
        chip=identity['chip'].upper()
        require(chip in full_reset.REGISTERS,'Unknown reset backend; App remains halted')
        address=full_reset.REGISTERS[chip][0]
        before=self._read_reset_status(address,'full-reset-cause-before')
        prepared=full_reset.plan(chip,before)
        commands=['halt']
        if prepared['write'] is not None:
            commands.append(f'w4 0x{address:08X}, 0x{prepared["write"]:08X}')
        commands.append(f'mem32 0x{address:08X}, 1')
        native=self._jlink(commands,'full-reset-cause-readback')
        after=self._decode_reset_status(native,address)
        return full_reset.verify(prepared,after)

    @staticmethod
    def _decode_reset_status(native,address):
        rows=re.findall(r'(?mi)^\s*(?:0x)?([0-9a-f]{8})\s*=\s*([0-9a-f]{8})\s*$',native)
        require(len(rows)==1 and int(rows[0][0],16)==address,
                             'Missing/ambiguous exact reset status read; App remains halted')
        return int(rows[0][1],16)

    def _read_reset_status(self,address,label):
        return self._decode_reset_status(self._jlink(['halt',f'mem32 0x{address:08X}, 1'],label),address)

    def start_app(self,identity):
        self.reset_target(identity,'start-verified-app')

    def reset_target(self,identity,label):
        # Match the tested Backup cleanup API. The named --jlink parameter is
        # the enumerated index, not the serial or the old positional grammar.
        self.command([self.console,'reset','--chip',identity['chip'].upper(),
                      '--jlink',self._selection()],label)

    def halt_only(self):
        # A failed/unknown initial probe has no admitted target. Do not launch
        # another attach after its deadline just to attempt cleanup.
        if self.identity is None:
            return
        try:self._jlink(['halt','regs'],'abort-halt')
        except Exception:
            self.log.emit('halt','HALT unconfirmed: remove target power; never resume or replay this context.',level='error')

    def prepare_first_install(self,identity,raw,config,review):
        g=review['geometry'];chip=review['chip']
        snapshot=Path(identity.get('factorySnapshotFile','')).resolve()
        require(snapshot.is_relative_to(self.folder),
                'Factory snapshot outside this detection; no erase')
        data=snapshot.read_bytes()
        require(sha(data)==identity.get('factorySnapshotSha256'),
                'Factory snapshot changed; no erase')
        if chip=='GR5331':
            offset=int(identity['cacheAddress'],16)-0x20000000
            require(len(data)==0x18000 and 0<=offset<=len(data)-96,
                    'Incomplete production snapshot; no erase')
            uid=data[0x24:0x34];trim=data[offset:offset+96]
            require(trim[:2]==b'DG' and struct.unpack_from('<4H',trim,12)==
                (0x5331,identity['packageId'],512,96), 'Conflicting exact trim/package; no erase')
            address,kind=self._factory_5331(identity,trim)
        else:
            require(len(data)==60 and data[28:30]==b'DG' and
                    data[2]&31==identity['packageId'], 'Conflicting eFuse/package; no erase')
            uid=data[44:60];address,kind=data[20:26][::-1],0
        require(type(address) is bytes and kind==0,
                'Complete immutable Factory address unavailable; no erase')
        require(not self.options.address or self.options.address.upper()==
                ':'.join(f'{v:02X}' for v in address), 'Expected address mismatch; no erase')
        current=self.read_full(identity)
        first=self._read(identity,g['nvdsBytes'],'first-nvds',address=g['base']+g['nvds'])
        second=self._read(identity,g['nvdsBytes'],'second-nvds',address=g['base']+g['nvds'])
        report,image=first_install_image(raw,config,current_image=current,
            current_nvds=first,second_nvds=second,uid=uid,
            factory_address=address,factory_type=kind)
        report.update(factorySnapshotSha256=sha(data),physicalIdentityAttested=True,
                      hardwareAccessed=True,image_sha256=sha(image),hardwareQualified=False)
        return report,image

    def bound(self,report,image):
        self.report=report
        self.image=self.folder/'prepared-full.bin';self.image.write_bytes(image)
        (self.folder/'binding.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


def sequence(raw,config,transport):
    """No retries: detection, immutable capture, write, complete readback, reset."""
    review,_=configured_image(raw,config)
    packages={'GR5331':(0x0204,0x0205,0x0208),'GR5513':(5,7),'GR5515':(4,)}[review['chip']]
    if review['family']=='gr5513_zkel_internal':packages=(7,)
    released=False
    try:
        identity=transport.detect(timeout=transport.options.detect_timeout)
        require(type(identity) is dict and identity.get('chipConfirmed') is True and
            identity.get('chip','').upper()==review['chip'] and
            type(identity.get('packageId')) is int and identity['packageId'] in packages and
            identity.get('flashBytes')==review['geometry']['capacity'] and
            identity.get('baseAddress')==review['geometry']['base'],
            'Actual MCU/package/capacity mismatch; no erase')
        report,image=transport.prepare_first_install(identity,raw,config,review)
        require(report['config']==review['config'] and report['chip']==review['chip'] and
                report['geometry']==review['geometry'], 'Configuration changed before write; no erase')
        transport.bound(report,image)
        transport.write_once(image,identity)
        require(transport.read_full(identity)==image,
                'Complete Full readback mismatch; no reset or write replay')
        transport.host_nvds_verified=True
        reset=transport.cleanup_before_reset(identity)
        require(reset.get('staleWatchdogCauseCleared') is True and
                reset.get('unrelatedBitsPreserved') is True, 'Reset cleanup not verified')
        transport.start_app(identity);released=True
        return dict(report,fullReadback=True,applicationStartResetRequested=True,
                    displayObserved=False,bleConnected=False,clockSynchronized=False,
                    resetBoundary=reset,hardwareQualified=False)
    finally:
        if not released:transport.halt_only()
