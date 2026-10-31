"""DA Full transaction core. Importing never opens a programmer.

The CLI selects only the fixed pinned physical backend. Internal dependency
injection is for offline regression, never a public writer/trust override.
There is no legacy writer, mutation retry or implicit resume path.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import runpy
import stat
import struct
import time
import zlib

HERE = Path(__file__).resolve().parent
FLASH_BYTES = 0x40000
SECTOR_BYTES = 4096
PAGE_BYTES = 256
# Existing vendor product/selector/dimensions, not user Settings. Preserve the
# complete two selector/calibration sectors; only product slot offsets change.
FACTORY_START = 0x38000
FACTORY_BYTES = 0x3000
PROTECTED = ((0x39000, 0x3B000),)
WRITE_RANGES = ((0, 0x39000), (0x3B000, FLASH_BYTES))
FLASH_MIN_MV = 2400


def transfer_options(speed_khz=500, program_chunk_bytes=256, read_chunk_bytes=4096,
                     sram_chunk_bytes=256):
    options=dict(speed_khz=speed_khz,program_chunk_bytes=program_chunk_bytes,
                 read_chunk_bytes=read_chunk_bytes,sram_chunk_bytes=sram_chunk_bytes)
    choices={'program_chunk_bytes':(16,32,64,128,256),
             'read_chunk_bytes':(256,512,1024,2048,4096),
             'sram_chunk_bytes':(64,128,256,512,1024,2048,4096)}
    for name,value in options.items():
        if type(value) is not int or (name in choices and value not in choices[name]) or \
                (name=='speed_khz' and not 1<=value<=4000):
            raise FullFailure('Invalid transfer option '+name+'; no target accessed')
    return options


class FullFailure(RuntimeError):
    pass


class NotReady(FullFailure):
    """Only this pre-hold, known-absent result permits readiness retry."""


@dataclass(frozen=True)
class Reply:
    value: object = None
    native_exit: int = 0
    unknown: bool = False
    raw: bytes = b''


# Exact existing v2 helper: ADC/LOW_POWER precedes every write opcode and
# its checked completion detail returns that operation's measured voltage.
POWER_GUARD_HELPER_SHA = '0f183172ce2d0ce86bf32c27dde76d2d77ceb0ce1a4f974dea1489ba6395dd7c'


@dataclass(frozen=True)
class NativePowerGuard:
    helper_sha256: str
    minimum_mv: int
    maximum_mv: object = None


@dataclass(frozen=True)
class GuardedMutation:
    byte_count: int
    command: int
    address: int
    payload_crc32: int
    sequence: int
    voltage: object
    helper_sha256: str


@dataclass(frozen=True)
class Target:
    serial: str
    chip_id: str
    cpuid: int
    jedec: int
    capacity: int
    sector: int = SECTOR_BYTES
    page: int = PAGE_BYTES


@dataclass(frozen=True)
class Identity:
    # Public BD_ADDR in SDK/OTP little-endian order, not an advertised name.
    otp_address_le: bytes
    calibration: bytes
    source: str = 'otp'

    @property
    def suffix(self):
        return self.otp_address_le[1::-1].hex().upper()


@dataclass(frozen=True)
class HoldState:
    halted: bool
    restart_on_close: bool
    error_stop: bool
    implicit_start: bool


@dataclass(frozen=True)
class Voltage:
    millivolts: int | None
    sampled_monotonic: float
    qualified: bool


def sha(data):
    return hashlib.sha256(data).hexdigest()


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.exists() and (part.is_symlink() or getattr(part.lstat(), 'st_file_attributes', 0) & 0x400):
            raise FullFailure('Linked input/output path refused')
    return path


def write_new(path, data):
    with plain(path).open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def json_new(path, value):
    write_new(path, (json.dumps(value, indent=2, ensure_ascii=True)+'\n').encode())


def production_validators(require_target_defaults=True):
    verifier = runpy.run_path(str(HERE/'da14585-image-verify.py'))
    config = runpy.run_path(str(HERE/'da14585-board-config.py'))
    return verifier['verify_full' if require_target_defaults else 'verify_full_template'], config['load_config'], config['check_base_family']


def validate_inputs(firmware, config, validators=None, *, require_target_defaults=True):
    verify, load_config, check_base = validators or production_validators(require_target_defaults)
    firmware, config = plain(firmware), plain(config)
    data, config_data = firmware.read_bytes(), config.read_bytes()
    try:
        authenticated = verify(data)
    except ValueError as error:
        raise FullFailure(str(error)) from error
    plan = load_config(config, 'DA14585')
    family = check_base(firmware, plan)
    if authenticated.get('appPublisherVerified') is not True or authenticated.get('layout') != 'DAV1' or \
            authenticated.get('fullBytes') != FLASH_BYTES or family.get('bytes') != FLASH_BYTES or \
            authenticated.get('fullSha256', '').lower() != sha(data) or family.get('sha256', '').lower() != sha(data):
        raise FullFailure('Incomplete signature/family verification or changing package')
    # Read the configuration again: a validation-time change is not confirmed.
    if config.read_bytes() != config_data or firmware.read_bytes() != data:
        raise FullFailure('Inputs changed during local validation')
    boot_length = int.from_bytes(data[5:8], 'big')
    if not 8 <= boot_length <= 0x4000-8 or boot_length % 4:
        raise FullFailure('Packaged pP bootloader bound/alignment')
    boot = data[8:8+boot_length]
    if validators is None:
        if authenticated.get('diagnosticCleanupProfile', {}).get('supported') is not True:
            raise FullFailure('Unknown native RAM diagnostic ownership profile; no target accessed')
        fixed = runpy.run_path(str(HERE/'da14585-full-backend.py'))
        fixed['verify_executables'](data, boot)
    sp, reset = struct.unpack_from('<II', boot)
    if not (0x07FC0000 < sp <= 0x07FD8000 and sp % 4 == 0 and
            0x07FC0001 <= reset < 0x07FC0000+boot_length and reset & 1):
        raise FullFailure('Packaged pP bootloader vectors')
    return dict(image=data, config=config_data, authentication=authenticated,
                plan=plan, firmware_sha=sha(data), config_sha=sha(config_data), boot=boot)


def check_target(target, serial):
    if type(target) is not Target or not target.serial.isdecimal() or not 4 <= len(target.serial) <= 16 or \
            (serial and target.serial != serial) or target.chip_id != '585' or target.cpuid != 0x410CC200:
        raise FullFailure('Exact probe/silicon identity mismatch')
    # The existing raw SPI path knows this part. No loader bank maximum or
    # filename is accepted as measured capacity, nor is a new part guessed.
    if target.jedec != 0xA14013 or target.capacity != 1 << (target.jedec & 255) or \
            target.capacity < FLASH_BYTES or target.sector != SECTOR_BYTES or target.page != PAGE_BYTES:
        raise FullFailure('Unqualified/measured Flash capacity or geometry')


def check_identity(identity):
    if type(identity) is not Identity or identity.source != 'otp' or \
            type(identity.otp_address_le) is not bytes or len(identity.otp_address_le) != 6 or \
            identity.otp_address_le in (b'\0'*6, b'\xff'*6, bytes.fromhex('0a00f4352348'), bytes.fromhex('0b00f4352348')) or \
            identity.otp_address_le[5] & 1 or type(identity.calibration) is not bytes or \
            not 16 <= len(identity.calibration) <= 4096 or set(identity.calibration) in ({0}, {255}):
        raise FullFailure('Missing/invalid immutable OTP identity or calibration')


def assess_legacy_data(factory, plan):
    """Old formats are diagnostic hints, never a source of write permission.

    A complete, repeated raw read is still mandatory for byte-for-byte
    preservation. Unknown protected bytes are not defaults and are not erased.
    """
    if type(factory) is not bytes or len(factory) != FACTORY_BYTES:
        raise FullFailure('Incomplete native capture; defaults cannot replace a failed read')
    product_known = factory[:2] == b'pR' and \
        struct.unpack_from('<I', factory, 4)[0] == 0x4000 and \
        struct.unpack_from('<I', factory, 8)[0] in (0x1C000, 0x1F000)
    board = factory[0x1000]
    dimensions = list(struct.unpack_from('<HH', factory, 0x200A))
    warnings = []
    if not product_known:
        warnings.append('Old product header/layout is absent or unknown; rebuild the writable product sector from the validated Full.')
    if board not in (2, 3):
        warnings.append('Protected factory selector is unknown; preserve its raw byte. Screen/pin defaults use the confirmed config, but board identity/capabilities may remain unknown.')
    elif board != plan['board_id']:
        warnings.append('Protected factory selector differs from selected config; preserve it. Stored screen/pin defaults use the confirmed config; do not relabel the board.')
    if dimensions != [plan['screen']['width'], plan['screen']['height']]:
        warnings.append('Old display dimensions/NVDS are missing or different; preserve protected raw bytes and initialize screen settings from the confirmed config.')
    return dict(productHeaderRecognized=product_known, protectedSelector=board,
                protectedSelectorRecognized=board in (2, 3), legacyDimensions=dimensions,
                writableProductSectorDefaulted=True, settingsAndImagesDefaulted=True,
                protectedBytesPreserved=True, proApplicable=False, warnings=warnings)


def prepare_image(review, identity, factory):
    """Build exact current-unit bytes without requiring old user/layout records."""
    check_identity(identity)
    assess_legacy_data(factory, review['plan'])
    plan = review['plan']
    codec = runpy.run_path(str(HERE/'da14585-full-state.py'))
    image = bytearray(review['image'])
    # The validated template owns the writable product sector, including the
    # new layout's pR pointers. Only the already protected sectors retain old
    # bytes; neither unknown NVDS nor erased metadata is parsed or synthesized.
    for start, end in PROTECTED:
        image[start:end] = factory[start-FACTORY_START:end-FACTORY_START]
    epoch = review['authentication']['compatibilityEpoch']
    image[0x37000:0x38000], image[0x3B000:0x3C000] = codec['recovery_pages'](epoch)
    # The same authenticated AESL executable is used for every board/panel.
    # Regenerate screen and capable-App pin defaults from the confirmed TOML,
    # plus the sole ID7 and immutable-derived label in the same journal.
    defaults = codec['verify_settings'](review['image'])
    config_codec = runpy.run_path(str(HERE/'da14585-board-config.py'))
    screen = config_codec['screen_record'](plan)
    pins = b''
    if defaults['pinDefaultsCapable']:
        pins = bytes.fromhex(plan['pin_wire_hex'])
        if not codec['valid_pin_defaults'](pins):
            raise FullFailure('Confirmed GPIO configuration is invalid')
        codec['require_pin_support'](review['image'][0x4000:0x1C000], pins)
    elif plan.get('gpio_changes'):
        raise FullFailure('Application does not consume configured pin defaults')
    def record(kind, payload):
        checksum = codec['fnv1a'](struct.pack('<HH', kind, len(payload))+payload)
        return struct.pack('<IHHI', 0x31525349, kind, len(payload), checksum)+payload+\
            b'\xff'*(-len(payload)%4)+struct.pack('<I', 0x31435349)
    label = ('InkTag '+identity.suffix).encode()
    payload = struct.pack('<IBB2x', 0x31544944, 1, len(label))+label.ljust(24, b'\0')
    payload += struct.pack('<I', codec['fnv1a'](payload))
    settings = struct.pack('<IIII', 0x31505349, 1, 0xFFFFFFFE, 0xFFFFFFFF)+\
        (record(1, pins) if pins else b'')+record(3, screen)+\
        record(7, bytes((2,1,1,0,0,0,0,0,0)))+record(6, payload)
    image[0x3C000:0x3E000] = settings.ljust(8192, b'\xff')
    image[0x24000:0x35000] = b'\xff'*0x11000
    for start, end in PROTECTED:
        if image[start:end] != factory[start-FACTORY_START:end-FACTORY_START]:
            raise FullFailure('Protected factory bytes changed in prepared image')
    return bytes(image)


def validate_prepared_image(path, config, review, identity, factory, validators=None):
    """Verify actual bytes against this transaction's captured unit and plan.

    check_base_family admits a common AESL template independently of its
    packaged defaults. A prepared image can preserve an unknown selector.
    It must instead equal the exact result of the authenticated template,
    immutable identity, confirmed config and preserved current-unit capture.
    This is not a public old-format/force mode or a reusable plan loader.
    """
    path, config = plain(path), plain(config)
    data = path.read_bytes()
    expected = prepare_image(review, identity, factory)
    if data != expected or config.read_bytes() != review['config']:
        raise FullFailure('Prepared image/config differs from the frozen current-unit plan')
    verify, load_config, _ = validators or production_validators()
    authenticated = verify(data)
    if load_config(config, 'DA14585') != review['plan'] or \
            authenticated.get('appPublisherVerified') is not True or \
            authenticated.get('layout') != 'DAV1' or authenticated.get('fullBytes') != FLASH_BYTES or \
            authenticated.get('fullSha256', '').lower() != sha(data):
        raise FullFailure('Prepared image signature/native/config verification failed')
    if validators is None:
        if authenticated.get('diagnosticCleanupProfile', {}).get('supported') is not True:
            raise FullFailure('Unknown prepared image RAM diagnostic ownership profile')
        fixed = runpy.run_path(str(HERE/'da14585-full-backend.py'))
        fixed['verify_executables'](data, review['boot'])
    if path.read_bytes() != data or config.read_bytes() != review['config']:
        raise FullFailure('Prepared evidence changed during verification')
    return dict(firmware_sha=sha(data), authentication=authenticated)


class Transaction:
    """One transaction, one backend session. All failures are terminal.

    Backend methods return Reply and must bound their native operation by the
    supplied timeout. The core also rejects late replies. A future physical
    backend still needs independent timeout/close/voltage qualification.
    """
    def __init__(self, backend_factory, directory, timeout=60, clock=time.monotonic, emit=None,
                 program_chunk_bytes=256, read_chunk_bytes=4096):
        transfer_options(program_chunk_bytes=program_chunk_bytes,read_chunk_bytes=read_chunk_bytes)
        self.program_chunk_bytes=program_chunk_bytes;self.read_chunk_bytes=read_chunk_bytes
        if type(timeout) not in (int, float) or not 0 < timeout <= 86400:
            raise FullFailure('Invalid operation deadline')
        self.backend_factory, self.backend = backend_factory, None
        self.directory, self.timeout, self.clock = plain(directory), timeout, clock
        self.emit = emit or (lambda text: None)
        self.stage, self.events, self.started, self.mutations = 'initial', 0, False, 0
        self.active = False
        self.used = False
        self.native_power_guard_count, self.native_power_sequence = 0, 0
        self.timing_started = self.phase_started = time.perf_counter()
        self.phase_timings = []

    def event(self, action, **fields):
        self.events += 1
        data = dict(sequence=self.events, stage=self.stage, action=action, **fields)
        with (self.directory/'events.jsonl').open('ab') as stream:
            stream.write((json.dumps(data, ensure_ascii=True)+'\n').encode())
            stream.flush(); os.fsync(stream.fileno())

    def phase(self, name):
        now = time.perf_counter()
        if self.stage != 'initial':
            self.phase_timings.append(dict(stage=self.stage,seconds=now-self.phase_started))
        self.phase_started = now
        self.stage = name
        self.event('phase',transactionElapsedSeconds=now-self.timing_started)
        self.emit('FULL_STAGE: '+name)

    def raw_evidence(self, name, reply):
        if type(reply) is Reply and reply.raw:
            if type(reply.raw) is not bytes: raise FullFailure('Invalid raw native evidence')
            name_on_disk='native-{0:06d}.log'.format(self.events)
            write_new(self.directory/name_on_disk, reply.raw)
            self.event('raw', command=name, file=name_on_disk, sha256=sha(reply.raw))

    def call(self, name, *args, mutation=False, limit=None):
        # Durable intent precedes every native call, including unknown ACKs.
        self.event('intent', command=name, mutation=mutation)
        if mutation: self.mutations += 1
        before = self.clock()
        budget = self.timeout if limit is None else min(self.timeout, limit)
        if budget <= 0: raise TimeoutError('Operation deadline expired before dispatch')
        if name == 'start_application': self.started = True
        try:
            reply = getattr(self.backend, name)(*args, timeout=budget)
        except NotReady:
            if self.clock()-before > budget:
                raise TimeoutError('Late readiness refusal; no replay')
            raise
        elapsed = self.clock()-before
        self.raw_evidence(name, reply)
        if elapsed > budget:
            raise TimeoutError('Late native reply; unknown result, no replay')
        if type(reply) is not Reply or type(reply.native_exit) is not int or reply.native_exit != 0 or reply.unknown is not False:
            self.event('refusal', command=name, nativeExit=getattr(reply, 'native_exit', None), unknown=getattr(reply, 'unknown', True))
            raise FullFailure('Native failure/unknown '+name+'; exit='+str(getattr(reply,'native_exit',None))+'; no replay')
        self.event('ack', command=name, nativeExit=reply.native_exit)
        return reply.value

    def voltage(self):
        sample = self.call('sample_voltage')
        now = self.clock()
        if type(sample) is not Voltage or sample.qualified is not True or type(sample.millivolts) is not int or \
                not FLASH_MIN_MV <= sample.millivolts <= 3600 or not 0 <= now-sample.sampled_monotonic <= 2:
            raise FullFailure('Unknown/stale/unqualified/unsafe Flash supply voltage')

    def powered_write(self, name, expected, address, data):
        guard = getattr(self.backend,'native_power_guard',None)
        if guard is None:
            self.voltage()
            if self.call(name,address,data,mutation=True) != expected:
                raise FullFailure('Partial/unknown '+name+' ACK')
            return
        if type(guard) is not NativePowerGuard or guard.helper_sha256 != POWER_GUARD_HELPER_SHA or \
                type(guard.minimum_mv) is not int or guard.minimum_mv != FLASH_MIN_MV:
            raise FullFailure('Unqualified native per-write power guard; no mutation issued')
        # A lower-bound-only declaration cannot replace the original complete
        # pre-operation range gate. Only a qualified complete native range
        # permits the single-command path.
        if guard.maximum_mv is None:
            self.voltage()
        elif type(guard.maximum_mv) is not int or guard.maximum_mv != 3600:
            raise FullFailure('Unqualified native upper voltage bound; no mutation issued')
        # One native command takes and enforces a fresh ADC sample immediately
        # before its Flash opcode. No cached sample or disabled voltage gate.
        proof = self.call(name,address,data,mutation=True)
        command = 4 if name == 'erase_sector' else 5
        crc = 0 if command == 4 else zlib.crc32(data)
        if type(proof) is not GuardedMutation or any(type(v) is not int for v in \
                (proof.command,proof.address,proof.byte_count,proof.payload_crc32)) or \
                (proof.command,proof.address,proof.byte_count,proof.payload_crc32) != \
                (command,address,expected,crc) or proof.helper_sha256 != POWER_GUARD_HELPER_SHA or \
                type(proof.sequence) is not int or proof.sequence <= self.native_power_sequence:
            raise FullFailure('Incomplete/mismatched native mutation/power receipt; no replay')
        sample = proof.voltage
        if type(sample) is not Voltage or sample.qualified is not True or type(sample.millivolts) is not int or \
                not FLASH_MIN_MV <= sample.millivolts <= 3600 or not 0 <= self.clock()-sample.sampled_monotonic <= 2:
            raise FullFailure('Unsafe/unknown/stale native mutation voltage; no replay')
        self.native_power_guard_count += 1
        self.native_power_sequence = proof.sequence

    def unchanged(self, target, identity, factory):
        if self.call('detect') != target or self.call('read_identity') != identity or \
                self.call('read_flash', FACTORY_START+SECTOR_BYTES, 2*SECTOR_BYTES) != factory[SECTOR_BYTES:]:
            raise FullFailure('Current target or immutable bytes changed')

    def execute(self, review, firmware, config, *, serial='', retry_detect=False, interval=1, readiness_timeout=0,
                validators=None):
        if self.used: raise FullFailure('Transaction already consumed; no replay')
        self.used = True
        report = dict(success=False, status='failed', startIssued=False, mutationIntents=0,
                      expectedBootObserved=False, configuredWriterQualified=False)
        prepared = b''
        owned_directory = False
        try:
            if type(interval) is not int or not 1 <= interval <= 300 or type(readiness_timeout) is not int or not 0 <= readiness_timeout <= 86400:
                raise FullFailure('Invalid readiness budget/interval')
            self.directory.mkdir(parents=True, exist_ok=False)
            owned_directory = True
            write_new(self.directory/'events.jsonl', b'')
            self.event('transfer_options',programChunkBytes=self.program_chunk_bytes,readChunkBytes=self.read_chunk_bytes,
                       pageBytes=PAGE_BYTES,sectorBytes=SECTOR_BYTES,fullReadbackBytes=FLASH_BYTES)
            self.phase('CONFIRMED INPUT SNAPSHOTS')
            for name, original, data in (('full.bin', firmware, review['image']), ('config.toml', config, review['config'])):
                if plain(original).read_bytes() != data:
                    raise FullFailure('Confirmed input changed before snapshot')
                path = self.directory/name
                write_new(path, data)
                path.chmod(stat.S_IREAD)
            frozen = validate_inputs(self.directory/'full.bin', self.directory/'config.toml', validators,
                                     require_target_defaults=False)
            if (frozen['firmware_sha'], frozen['config_sha']) != (review['firmware_sha'], review['config_sha']):
                raise FullFailure('Snapshot does not match confirmed inputs')
            self.backend = self.backend_factory()
            self.active = True
            self.phase('EXACT TARGET READINESS')
            deadline = self.clock()+readiness_timeout if readiness_timeout else None
            attempts = 0
            while True:
                if attempts and deadline is not None and self.clock() >= deadline:
                    raise TimeoutError('Readiness deadline; no target mutation')
                attempts += 1
                try:
                    remaining = max(0, deadline-self.clock()) if deadline is not None else None
                    target = self.call('detect', limit=remaining)
                    break
                except NotReady as error:
                    self.event('readiness_refusal', attempt=attempts, reason=str(error))
                    if not retry_detect or (deadline is not None and self.clock() >= deadline): raise
                    self.emit('Readiness attempt '+str(attempts)+' refused: '+str(error)+
                              '; retrying detection only')
                    time.sleep(min(interval, max(0, deadline-self.clock())) if deadline is not None else interval)
            check_target(target, serial)
            cleanup_codec = runpy.run_path(str(HERE/'da14585-full-state.py'))
            report['cleanupScope'] = cleanup_codec['cleanup_scope'](target.capacity)
            self.phase('HOLD / ERROR STOP / NO IMPLICIT START')
            held = self.call('hold_no_start')
            if type(held) is not HoldState or held.halted is not True or held.restart_on_close is not False or held.error_stop is not True or held.implicit_start is not False:
                raise FullFailure('Halt/close/error-stop guarantees absent')
            self.phase('NECESSARY IMMUTABLE CAPTURE')
            identity = self.call('read_identity'); check_identity(identity)
            factory = self.call('read_flash', FACTORY_START, FACTORY_BYTES)
            if type(factory) is not bytes or len(factory) != FACTORY_BYTES:
                raise FullFailure('Partial native identity/calibration capture')
            if self.call('read_flash', FACTORY_START, FACTORY_BYTES) != factory:
                raise FullFailure('Independent required factory captures disagree; no erase')
            write_new(self.directory/'factory-required.bin', factory)
            write_new(self.directory/'otp-address.bin', identity.otp_address_le)
            write_new(self.directory/'otp-calibration.bin', identity.calibration)
            json_new(self.directory/'identity.json', dict(address=identity.otp_address_le.hex(), calibrationSha256=sha(identity.calibration), serial=target.serial))
            self.phase('PREPARE DEFAULTS / BOTH RECOVERY PAGES')
            report['legacyData'] = assess_legacy_data(factory, frozen['plan'])
            for warning in report['legacyData']['warnings']:
                self.emit('WARNING: '+warning)
            prepared = prepare_image(frozen, identity, factory)
            path = self.directory/'prepared.bin'; write_new(path, prepared); path.chmod(stat.S_IREAD)
            validate_prepared_image(path, self.directory/'config.toml', frozen, identity, factory, validators)
            json_new(self.directory/'plan.json', dict(sourceSha256=review['firmware_sha'], configSha256=review['config_sha'],
                preparedSha256=sha(prepared), protected=PROTECTED, writeRanges=WRITE_RANGES,
                actualFlashCapacity=target.capacity, clearImageBytes=0x11000, clearSettingsBytes=8192,
                cleanupScope=report['cleanupScope'],
                legacyData=report['legacyData'],
                compatibilityEpoch=frozen['authentication']['compatibilityEpoch'], oldFloorCompared=False))
            self.phase('PRE-WRITE SAME UNIT / VOLTAGE')
            self.unchanged(target, identity, factory); self.voltage()
            if path.read_bytes() != prepared or (self.directory/'full.bin').read_bytes() != frozen['image'] or \
                    (self.directory/'config.toml').read_bytes() != frozen['config']:
                raise FullFailure('Frozen plan/input modified before mutation')
            self.phase('SINGLE PROTECTED PROGRAM TRANSACTION')
            total_sectors=sum((upper-lower)//SECTOR_BYTES for lower,upper in WRITE_RANGES)
            completed_sectors=0
            for lower, upper in WRITE_RANGES:
                for address in range(lower, upper, SECTOR_BYTES):
                    if self.call('detect') != target or self.call('read_identity') != identity:
                        raise FullFailure('Same-unit session changed before sector erase')
                    self.powered_write('erase_sector',SECTOR_BYTES,address,SECTOR_BYTES)
                    for offset in range(address, address+SECTOR_BYTES, self.program_chunk_bytes):
                        page = prepared[offset:offset+self.program_chunk_bytes]
                        if page == b'\xff'*self.program_chunk_bytes: continue
                        self.powered_write('program_page',self.program_chunk_bytes,offset,page)
                    completed_sectors+=1
                    self.emit('Programming '+str(round(completed_sectors/total_sectors*100, 1))+'%')
            self.phase('INDEPENDENT COMPLETE READBACK')
            with (self.directory/'readback.partial.bin').open('xb') as stream:
                digest = hashlib.sha256()
                for address in range(0, FLASH_BYTES, self.read_chunk_bytes):
                    data = self.call('read_flash', address, self.read_chunk_bytes)
                    if type(data) is not bytes or len(data) != self.read_chunk_bytes:
                        raise FullFailure('Partial independent complete readback')
                    stream.write(data); stream.flush(); os.fsync(stream.fileno()); digest.update(data)
                    if data != prepared[address:address+self.read_chunk_bytes]:
                        raise FullFailure('Independent complete readback mismatch')
                    self.emit('Verifying '+str(round((address+self.read_chunk_bytes)/FLASH_BYTES*100, 1))+'%')
            if digest.hexdigest() != sha(prepared): raise FullFailure('Readback SHA mismatch')
            # Re-authenticate from actual readback, not a target CRC or write buffer.
            validate_prepared_image(self.directory/'readback.partial.bin', self.directory/'config.toml', frozen, identity, factory, validators)
            if (self.directory/'readback.partial.bin').read_bytes() != prepared:
                raise FullFailure('Readback evidence changed before final checks')
            write_new(self.directory/'verified-readback.bin', prepared)
            self.phase('FINAL SAME UNIT / PRESERVED BYTES / VOLTAGE')
            self.unchanged(target, identity, factory); self.voltage()
            self.phase('SOLE FINAL START')
            if self.call('start_application', frozen['boot'], sha(prepared)) is not True:
                raise FullFailure('Final start not verified; no reset/start replay')
            report.update(status='verified', preparedSha256=sha(prepared), readbackSha256=digest.hexdigest(),
                          cleanupScope=cleanup_codec['cleanup_scope'](target.capacity, verified=True),
                          expectedLabel='InkTag '+identity.suffix, expectedAppName='IKT_DA_'+identity.suffix,
                          expectedRecoveryName='IKT_DA_'+identity.suffix+'_OTA')
        except Exception as error:
            report.update(error=type(error).__name__+': '+str(error), failedStage=self.stage)
        finally:
            if self.active:
                # Closing is necessary even if the evidence disk failed. Keep
                # it exactly once; never fall back to reset/resume/reconnect.
                try:
                    self.phase('CLOSE WITHOUT START')
                except Exception as error:
                    report.update(status='failed', cleanupError=type(error).__name__+': '+str(error))
                try:
                    before = self.clock()
                    closed = self.backend.close_no_start(timeout=self.timeout)
                    self.raw_evidence('close_no_start', closed)
                    self.event('close_result', nativeExit=getattr(closed,'native_exit',None), unknown=getattr(closed,'unknown',True))
                    if self.clock()-before > self.timeout or type(closed) is not Reply or type(closed.native_exit) is not int or closed.native_exit != 0 or closed.unknown is not False or closed.value is not True:
                        raise FullFailure('Close policy/result unknown')
                except Exception as error:
                    report.update(status='failed', cleanupError=type(error).__name__+': '+str(error))
                finally:
                    self.active = False
            timing_end = time.perf_counter()
            report.update(timing=dict(transactionSeconds=timing_end-self.timing_started,stages=self.phase_timings +
                [dict(stage=self.stage,seconds=timing_end-self.phase_started)]),
                nativePowerGuardMutations=self.native_power_guard_count)
            report.update(success=report['status']=='verified', startIssued=self.started, mutationIntents=self.mutations)
            report.update(noReplay=True, outcomeUnknown=not report['success'] and (self.started or self.mutations > 0 or 'cleanupError' in report))
            if owned_directory and (self.directory/'events.jsonl').exists():
                json_new(self.directory/'result.json', report)
        return report


def run_full(firmware, config, directory, backend_factory, *, yes=False, confirm=input, validators=None, emit=None,
             expected_firmware_sha=None, expected_config_sha=None, program_chunk_bytes=256, read_chunk_bytes=4096, **options):
    transfer_options(program_chunk_bytes=program_chunk_bytes,read_chunk_bytes=read_chunk_bytes)
    # An internal Python dependency boundary. Neither factory nor validators
    # are accepted from CLI, TOML, environment, receipts or a plugin path.
    review = validate_inputs(firmware, config, validators, require_target_defaults=False)
    for expected, actual in ((expected_firmware_sha,review['firmware_sha']),(expected_config_sha,review['config_sha'])):
        if expected is not None and (type(expected) is not str or expected.lower()!=actual):
            raise FullFailure('Package/config changed since displayed preflight')
    warning=('Full '+review['plan']['board_marking']+'; clear settings/time/replay and all 68 KiB images plus supported RAM reset history. Extra Flash beyond256KiB remains preserved/unverified. '
             'Reinstall packaged defaults/assets; initialize both Recovery pages from selected App epoch. '
             'No automatic backup. Package SHA256='+review['firmware_sha']+'; config SHA256='+review['config_sha']+'. Continue? [y/N] ')
    if not yes and confirm(warning).strip().lower() not in ('y', 'yes'):
        return dict(success=False, status='cancelled', startIssued=False, mutationIntents=0)
    emit = emit or (lambda text: print(text, flush=True))
    return Transaction(backend_factory, directory, timeout=options.pop('operation_timeout', 60), emit=emit,
                       program_chunk_bytes=program_chunk_bytes,read_chunk_bytes=read_chunk_bytes).execute(
        review, firmware, config, validators=validators, **options)


def production_backend(serial='', **transport):
    fixed = runpy.run_path(str(HERE/'da14585-full-backend.py'))
    if fixed['portable_context']() is not None:
        raise FullFailure('Portable Full writer is not qualified; no target accessed')
    commander = runpy.run_path(str(HERE/'da14585-commander-backend.py'))
    return commander['CommanderFullBackend']({name: globals()[name] for name in
        ('Reply','Target','Identity','HoldState','Voltage','NotReady','NativePowerGuard','GuardedMutation')}, serial, **transport)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--yes', action='store_true')
    parser.add_argument('--validate-only', action='store_true', help='Offline signed-App/native-format/config/helper check; no target access')
    parser.add_argument('--serial', default='')
    parser.add_argument('--jlink-exe', default='')
    parser.add_argument('--speed-khz', type=int, default=500)
    parser.add_argument('--program-chunk-bytes', type=int, default=256)
    parser.add_argument('--read-chunk-bytes', type=int, default=4096)
    parser.add_argument('--sram-chunk-bytes', type=int, default=256)
    parser.add_argument('--retry-detect', action='store_true')
    parser.add_argument('--interval', type=int, default=1)
    parser.add_argument('--timeout', type=int, default=0)
    parser.add_argument('--operation-timeout', type=int, default=60)
    parser.add_argument('--expected-firmware-sha')
    parser.add_argument('--expected-config-sha')
    args = parser.parse_args()
    transport_validated=False
    try:
        tuning=transfer_options(args.speed_khz,args.program_chunk_bytes,args.read_chunk_bytes,args.sram_chunk_bytes)
        transport_validated=True
        if args.validate_only:
            review=validate_inputs(args.firmware,args.config,require_target_defaults=False)
            fixed=runpy.run_path(str(HERE/'da14585-full-backend.py'))
            fixed['verify_local_backend_inputs'](args.jlink_exe)
            print(json.dumps(dict(validated=True,deviceAccess=False,firmwareSha256=review['firmware_sha'],transferOptions=tuning)))
            return 0
        # Validate/confirm/snapshot first. No USB/debugger access from import.
        result = run_full(args.firmware, args.config, args.output,
                          lambda: production_backend(args.serial,speed_khz=args.speed_khz,sram_chunk_bytes=args.sram_chunk_bytes,
                              jlink_exe=args.jlink_exe,
                              evidence_root=args.output/'commander-readback'), yes=args.yes,
                          program_chunk_bytes=args.program_chunk_bytes,read_chunk_bytes=args.read_chunk_bytes,
                          serial=args.serial, retry_detect=args.retry_detect, interval=args.interval,
                          readiness_timeout=args.timeout, operation_timeout=args.operation_timeout,
                          expected_firmware_sha=args.expected_firmware_sha, expected_config_sha=args.expected_config_sha)
        print(json.dumps(result))
        return 0 if result['success'] or result['status']=='cancelled' else 1
    except Exception as error:
        # execute() may already have mutated/started/closed the target before
        # final result persistence itself fails. Only file-only validation
        # proves no device access; all other uncaught outcomes stay unknown.
        print(json.dumps(dict(success=False, status='failed', error=str(error),
                              deviceAccess=False if args.validate_only or not transport_validated else None,
                              outcomeUnknown=not args.validate_only and transport_validated, noReplay=True)))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
