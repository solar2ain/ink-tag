"""Portable hardware-only GR Full sequence with MCU-scoped acceptance.

No SDK/ELF/compiler/private-key loading. Exact chip/capacity, identity and protected
vendor NVDS and same-device Pro are reviewed BEFORE erase; Full clears user data and
initializes the selected candidate's native Recovery state. No post-write App
execution context, RAM helper or startup-stage polling. No write replay.
"""
from __future__ import annotations
import importlib.util
from contextlib import chdir
import json
from pathlib import Path
import re
import struct
import sys
import time

import goodix_configured_full as full
import goodix_host_nvds as nvds
import goodix_release_full as release_full
import goodix_full_reset as full_reset
from goodix_tool_log import log_class, report_confirmed_mcu

ROOT=Path(__file__).resolve().parents[2]

def require_host_recipe(metadata,chip):
    return nvds.check_recipe(metadata.get('hostNvds'),chip)


def expected_bluetooth_names(address):
    """Factory identity prediction, never evidence of an advertisement."""
    full.factory.require(isinstance(address,str) and
        re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',address) is not None,
        'Cannot derive GR Bluetooth names: invalid immutable MAC address')
    suffix=''.join(address.upper().split(':')[-2:])
    name='IKT_GR_'+suffix
    return dict(expectedAppBluetoothName=name,expectedRecoveryBluetoothName=name+'_OTA',
                bluetoothNameSource='immutable-identity-derived; not scanned',bluetoothObserved=False)


def target_voltage(native):
    values=re.findall(r'VTref\s*=\s*(\d+(?:\.\d+)?)V',native)
    full.factory.require(values and 2.7<=float(values[-1])<=3.6,
                         'No safe live target voltage (2.7..3.6 V); no Full write')
    return float(values[-1])


def report_full_success(log,result,evidence):
    """Report only the completed hardware checks; radio/display stay separate."""
    full.factory.require(result.get('status')=='tool-success' and
        result.get('full_readback') is True and result.get('host_nvds_verified') is True,
        'Full success cannot be reported before complete image/NVDS readback')
    log.emit('result','Congratulations! Full flash verified. App Bluetooth name (expected): '+result['expectedAppBluetoothName'],level='result')
    preservation=result.get('ordinaryAddress',{}).get('releaseReview',{})
    if preservation.get('proPreserved') is True:
        log.emit('result','Same-device Pro authorization preserved and read back.',level='notice')
    elif preservation.get('proAbsenceProven') is True:
        log.emit('result','Pro authorization absent (Free); no activation record was written.',level='notice')
    elif preservation.get('proPreservationUnavailable') is True:
        log.emit('result','Old authorization could not be verified or recovered; initialized Free. No activation record was written.',level='warning')
    log.emit('result',f'Full readback matched, including protected identity/calibration and fresh native Recovery state. Evidence: {evidence}',level='diagnostic')
    log.emit('result','App startup reset requested; name derived from immutable identity, not scanned. First App boot handles the one-shot display request after startup; display completion has not been checked.',level='diagnostic')
    log.emit('result','Expected Recovery Bluetooth name: '+result['expectedRecoveryBluetoothName']+'; startup/display/connection still require separate confirmation.',level='diagnostic')
    log.emit('result','Full is SWD-only: no InkTag CLI, BLE connection or clock synchronization was requested.',level='diagnostic')
    if result.get('externalFlash') is not None:
        log.emit('result','GR5513 external SPI Flash is outside this MCU Full image; it was not erased or verified.',level='diagnostic')


def _sequence(base, config, transport, *, retry_detect=False, timeout=120, sleep=time.sleep, monotonic=time.monotonic):
    """Internal sequence shared by the closed hardware front and fake tests.

    The transport is hardware-only. Only a positively transient *detection*
    result can repeat; malformed identity, read, erase, write or verify
    failures terminate and retain evidence. Before releasing the verified App,
    cleanup is HALT only. Full ends after releasing the verified App; it never
    opens BLE, invokes the InkTag CLI or manages a separate SPI Flash device.
    """
    review,image,data,verified=full.prepare(base,config)
    full.require_execution_family(review)
    require_host_recipe(verified['metadata'],verified['chip'])
    release_full.require_backend(review['chip'])
    started=monotonic()
    app_released=False
    try:
        while True:
            remaining=timeout-(monotonic()-started)
            full.factory.require(remaining>0,'Detection deadline expired; no write started')
            identity=transport.detect(timeout=remaining)
            full.factory.require(monotonic()-started<=timeout,'Detection exceeded deadline; no write started')
            if identity is not None: break
            full.factory.require(retry_detect and monotonic()-started+1 < timeout,
                                 'No contact; no write started')
            sleep(1)
        full.factory.require(identity.get('chipConfirmed') is True and
            identity.get('chip','').upper()==review['chip'] and
            type(identity.get('packageId')) is int and identity['packageId'] in review['package_ids'] and
            identity.get('flashBytes')==review['flash_bytes'], 'Actual chip/package/capacity mismatch or unconfirmed')
        image,authority=transport.prepare_nvds(identity,verified,image,signed_app=data['app'])
        full.factory.require(authority.get('nativeRecoveryReinitialized') is True and
            authority.get('releaseDataMode')=='full-all',
            'Full lacks native candidate Recovery initialization')
        report=dict(review,image_sha256=full.factory.sha(image),hostPreparedNvds=True,
                    ordinaryAddress=authority,data_scope=authority['releaseDataMode'],
                    settings_clear_authorized=True)
        transport.bound(report, verified, data, image)
        # Admission depends on the live MCU/package/capacity, not existing
        # firmware or a host's factory-backup collection. Verify only the
        # resulting image after the single write.
        transport.write_once(image,identity)
        full.factory.require(transport.read_full(identity)==image, 'Complete Full readback mismatch; outcome unknown')
        transport.host_nvds_verified=True
        names=expected_bluetooth_names(authority['address'])
        reset_review=transport.cleanup_before_reset(identity)
        full.factory.require(reset_review.get('staleWatchdogCauseCleared') is True and
            reset_review.get('unrelatedBitsPreserved') is True,
            'Full reset-boundary cleanup is incomplete; App remains halted')
        transport.start_app(identity)
        app_released=True
        transport.application_start_reset_requested=True
        return dict(report,status='tool-success',hardware_started=True,device_attested=False,
                    full_readback=True,host_nvds_verified=True,postWriteHelperRequested=False,**names,
                    resetBoundary=reset_review,
                    cliInvoked=False,bleConnectionAttempted=False,clockSyncAttempted=False,
                    externalFlash=(dict(scope='outside-mcu-full',accessed=False,erased=False,verified=False)
                                   if review['chip']=='GR5513' and
                                      review['firmwareFamily']!=full.factory.INTERNAL_FAMILY else None),
                    applicationStartResetRequested=True,
                    firstBootDisplayRequest='one-shot-after-startup',displayObserved=False)
    except BaseException:
        if not app_released:
            transport.halt_only()
        raise


def execute(base,config,options):
    # Normal signature/config/actual-device checks, never candidate approval.
    report,_,payloads,verified=full.prepare(base,config)
    full.require_execution_family(report)
    full.factory.require(report['chip'] in full.EXECUTION_CANDIDATE_CHIPS,
                         'Portable execution is unqualified for this MCU; no hardware accessed')
    require_host_recipe(verified['metadata'],verified['chip'])  # before even probe enumeration
    release_full.require_backend(report['chip'])
    transport=GProgrammerTransport(options)
    status='failed'
    result=None
    try:
        arguments=dict(retry_detect=options.retry_detect,timeout=options.detect_timeout)
        result=_sequence(base,config,transport,**arguments)
        status='tool-success'
        return result
    except BaseException as error:
        result=dict(error=str(error),last_stage=getattr(transport,'last_stage',None),
                    flash_write_started=getattr(transport,'write_started',False),
                    flash_write_acknowledged=getattr(transport,'write_acknowledged',False),
                    host_nvds_verified=getattr(transport,'host_nvds_verified',False),
                    applicationStartResetRequested=getattr(transport,'application_start_reset_requested',False))
        raise
    finally:
        try:
            (transport.folder/'result.json').write_text(json.dumps(dict(result or {},**transport.log.common,
                status=status,hardware_started=True,device_attested=False,automatic_write_retry=False),indent=2)+'\n')
            if status=='tool-success' and not getattr(options,'console_progress',False):
                report_full_success(transport.log,result,transport.folder/'result.json')
        finally:
            transport.log.close()


class GProgrammerTransport:
    def __init__(self,options):
        self.options=options
        self.folder=Path(options.evidence).resolve()
        self.console=Path(options.console).resolve(strict=True)
        self.jlink=Path(options.jlink).resolve(strict=True)
        full.factory.require(not options.snr or re.fullmatch(r'\d+',options.snr) is not None,'Invalid J-Link serial')
        full.factory.require(not options.address or re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}',options.address) is not None,
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
        full.factory.require(result.code==0 and not re.search(
            r'(?i)could not connect|cannot connect|\bfailed\b|not_supported|could not read|syntax error|unknown command',text),
            'Hardware stage refused/failed: '+label)
        return text

    def detect(self,*,timeout=None):
        result=self.folder/f'identity-{self.counter}.json'
        cmd=[self.options.powershell,'-NoProfile','-File',ROOT/'firmware/ports/goodix/common/tools/internal/goodix-stock-entry.ps1',
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
            full.factory.require(len(rows)==1,'Multiple/no J-Link probes; select an exact J-Link serial')
            self.options.snr=rows[0][1]
        selected=[index for index,serial in rows if serial==self.options.snr]
        full.factory.require(len(selected)==1,'Exact J-Link serial not unique')
        return selected[0]

    def _read(self,identity,size,label,*,address=None):
        path=self.folder/f'{self.counter}-{label}.bin'
        self.command([self.console,'readFlash','--chip',identity['chip'].upper(),
            '--flashSize',identity['flashBytes']//1024,'--address',hex(identity['baseAddress'] if address is None else address),
            '--size',size,'--save',path,'--jlink',self._selection()],label)
        data=path.read_bytes()
        full.factory.require(len(data)==size,'Incomplete '+label)
        return data

    def read_full(self,identity): return self._read(identity,identity['flashBytes'],'full-readback')

    def bound(self,report,verified,data,image):
        self.report,self.verified=report,verified
        self.image=self.folder/'prepared-full.bin';self.image.write_bytes(image)
        (self.folder/'binding.json').write_text(json.dumps(dict(report,verified=verified),indent=2)+'\n')

    def write_once(self,image,identity):
        full.factory.require(full.factory.sha(self.image.read_bytes())==self.report['image_sha256'], 'Prepared image changed')
        # Identity was captured atomically with package detection, before the
        # physical loader used SRAM. Two fresh protected-NVDS reads agree.
        # Do not reset/relaunch the old App just to recreate this RAM cache:
        # that introduced another manual-RESET window and checkpoint writes.
        # Every hardware stage still selects the frozen exact probe; the
        # vendor writer uses the detected MCU/capacity, then full readback.
        self.log.emit('write', 'Identity and protected Factory data reviewed; starting one Full write.',level='notice')
        self.write_started=True
        self.command([self.options.powershell,'-NoProfile','-File',ROOT/'firmware/ports/goodix/common/tools/internal/flash-goodix-gprogrammer.ps1',
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
        full.factory.require(len(rows)==1,'Missing/ambiguous factory OTP read; no erase')
        raw=bytes.fromhex(rows[0])
        full.factory.require(len(raw)==size,'Incomplete factory OTP read; no erase')
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
            full.factory.require(len(first)==len(again)==96 and first==again,'Unstable factory OTP; no erase')
            if first[:32]==trim[:32] and first[:2]==b'DG':
                addr=self._otp(bt,8,f'factory-address-{bt:x}-0')
                repeat=self._otp(bt,8,f'factory-address-{bt:x}-1')
                full.factory.require(len(addr)==len(repeat)==8 and addr==repeat,'Unstable factory address; no erase')
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
            productionMatched=True,doubleReadSame=True,sha256=full.factory.sha(raw))
        return raw[:6][::-1],0

    def prepare_nvds(self,identity,verified,image,*,signed_app=None):
        """Fresh immutable cache + two physical NVDS reads, before erase.

        A halted ROM's uninitialized XIP mapping can return all FF without a
        bus error. Therefore only the maintained physical readFlash loader,
        never J-Link's raw XIP window, establishes stored-address presence.
        """
        chip=verified['chip']
        family=verified['firmwareFamily']
        offset,size=release_full.preservation_extent(chip,family)
        base=identity['baseAddress']
        # Consume only this detection's own snapshot, never a saved per-device
        # cache or another Commander connection after the MCU was released.
        snapshot=Path(identity.get('factorySnapshotFile','')).resolve()
        full.factory.require(snapshot.is_relative_to(self.folder.resolve()),
                             'Factory snapshot is outside the current detection; no erase')
        raw=snapshot.read_bytes()
        full.factory.require(full.factory.sha(raw)==identity.get('factorySnapshotSha256'),
                             'Factory snapshot changed after detection; no erase')
        if chip=='GR5331':
            cache_offset=int(identity['cacheAddress'],16)-0x20000000
            full.factory.require(len(raw)==0x18000 and 0<=cache_offset<=len(raw)-96,
                                 'Incomplete detected production snapshot; no erase')
            uid=raw[0x24:0x34];efuse=raw[cache_offset:cache_offset+96]
            full.factory.require(efuse[:2]==b'DG' and
                struct.unpack_from('<4H',efuse,12)==(0x5331,identity['packageId'],512,96),
                'Uninitialized/conflicting exact-package trim cache; no erase')
        else:
            efuse=raw;uid=raw[44:60]
            full.factory.require(len(raw)==60 and efuse[28:30]==b'DG' and
                (efuse[2]&31)==identity['packageId'],
                'Uninitialized/conflicting exact-package eFuse cache; no erase')
        full.factory.require(len(uid)==16 and uid not in (bytes(16),b'\xff'*16),
                             'Detected factory snapshot has no complete immutable UID; no erase')
        current=self._read(identity,size,'identity-nvds-physical-0',address=base+offset)
        again=self._read(identity,size,'identity-nvds-physical-1',address=base+offset)
        old=release_full.review_current(chip,current,family)
        repeated=release_full.review_current(chip,again,family)
        protected_equal=(old['vendor']==repeated['vendor'] and old['identity']==repeated['identity'] and
                         old['activation']==repeated['activation'])
        # Authorization is taken only from stable complete native reads in this
        # operation. Product-only differences are no longer safe to discard.
        full.factory.require(protected_equal and current==again,
            'Protected Factory/identity reads differ; no erase')
        full_reads_equal=current==again
        current=again
        installed=repeated['identity']
        # Exact SDK nvds_get(C001) imports chip_trim1.bt_addr; the successful
        # ble_sdk_gap_init/ble_nvds_get branch selects PUBLIC, independently
        # of the current selected GAP identity. See the source-bound audit.
        factory_address,factory_type=None,None
        if installed is None:
            if chip=='GR5331':factory_address,factory_type=self._factory_5331(identity,efuse)
            else:factory_address,factory_type=efuse[20:26][::-1],0
        address,kind,source=release_full.select_full_identity(
            installed,factory_address,factory_type,chip=chip,uid=uid)
        if installed is not None:
            source=repeated['identitySource']
        if source=='installed-address-native-public-default':
            self.log.emit('identity','Preserving legacy address bytes; initializing native public type (old type was not stored).',level='notice')
        formatted=':'.join(f'{b:02X}' for b in address)
        full.factory.require(not self.options.address or self.options.address.upper()==formatted,
                             'Explicit expected address differs from selected live identity; no erase')
        final,release_review=release_full.compose(chip,current,image,address,kind,
            verified['metadata']['hostNvds'],verified['metadata']['compatibilityEpoch'],family=family,
            raw_uid=uid,signed_app=signed_app)
        for warning in release_review.get('warnings',()):
            self.log.emit('identity',warning,level='warning')
        if getattr(self,'factory_source',{}).get('kind')=='factory-address-unavailable':
            self.log.emit('identity',self.factory_source['reason'],level='warning')
        target_offset,target_size=nvds.extent(chip,family)
        prepared=final[target_offset:target_offset+target_size]
        full.factory.require(release_full.read_prepared_identity(prepared,chip,family)==(address,kind),
                             'Host-prepared identity roundtrip failed')
        receipt=dict(address=formatted,addressType=kind,source=source,
                     preservationOffset=offset,preservationBytes=size,
                     targetNvdsOffset=target_offset,targetNvdsBytes=target_size,
                     uidSha256=full.factory.sha(uid),currentNvdsSha256=full.factory.sha(current),
                     preparedNvdsSha256=full.factory.sha(prepared),twoFreshReadsMatched=full_reads_equal,
                     twoFreshProtectedReadsMatched=True,
                     factorySnapshotSha256=identity['factorySnapshotSha256'],
                     factorySnapshotSource='same-connection-as-mcu-detection',
                     factorySource=getattr(self,'factory_source',None),
                     nativeRecoveryReinitialized=release_review['nativeRecoveryReinitialized'],
                     releaseDataMode=release_review['mode'],releaseReview=release_review,
                     postWriteHelperRequested=False)
        (self.folder/'host-identity.json').write_text(json.dumps(receipt,indent=2)+'\n')
        self.log.emit('identity',f'Identity confirmed before erase: {formatted} (type {kind}, {source}). '
                      'Preparing complete image on host.',level='notice')
        return bytes(final),receipt

    def cleanup_before_reset(self,identity):
        """Only after full readback, while our exact probe still owns halt."""
        full.factory.require(self.identity==identity and self.host_nvds_verified,
                             'Reset cleanup lacks verified target/readback ownership; App remains halted')
        chip=identity['chip'].upper()
        full.factory.require(chip in full_reset.REGISTERS,'Unknown reset backend; App remains halted')
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
        full.factory.require(len(rows)==1 and int(rows[0][0],16)==address,
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
