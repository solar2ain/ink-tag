#!/usr/bin/env python3
"""Prepare/verify a unit-bound Nordic Full image offline; never accesses a device."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/hardware'))
from nordic_full_config import validate_profile, screen_record as native_screen_record, configure_base
from nordic_full_retention import activation_authority, verifier_available
from nordic_full_cleanup import cleanup_plan, observation, verify_cleanup
from nordic_tool_format import factory as _factory, font_package as _font_package
FACTORY = vars(_factory)
HEX = runpy.run_path(str(ROOT / 'tools/sdk/merge-ihex.py'))
ASSETS = vars(_font_package)
FLASH_BYTES, PAGE = 0x80000, 4096
SETTINGS, IMAGE, FONTS, RECOVERY = 0x56000, 0x60000, 0x4E000, 0x58000
MBR_PARAMS, DFU_SETTINGS, APP_END = 0x5E000, 0x5F000, 0x4E000
TAIL = runpy.run_path(str(ROOT / 'tools/sdk/nrf52832-tail-layout.py'))
LAYOUT_V56 = (SETTINGS, IMAGE, FONTS, RECOVERY, MBR_PARAMS, DFU_SETTINGS)
# SDK17 packed boot validations end here. Full resets entire native pages;
# no old peer_data/adv_name, tails or compatibility floor is imported.
DFU_METADATA_BYTES = 803
# Exact source/binary provenance, not an inferred address window or magic.
# Source-bound offline HW56 build; physical layout qualification is separate.
KNOWN_RECOVERY = {
    17156: '6259d53f663d4b0a76e6d4d57e45153c63279003678dd9d8276fb0d2812a7a90',
    # e95bf8b4 address-adapter source, independently audited against SDK17;
    # unchanged in 440975cf. Offline admission only, not Full qualification.
    17316: 'f49a09c65786674b4bcd488b64872e66da1e5958175ae84d04a050c74bf78bcc',
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from('<I', data, offset)[0]


def journal_decode(pair: bytes) -> tuple[int, dict[int, bytes]]:
    """Strict selected-template validation of release ISP1/ISR1/ISC1 live set.

    Never weaken validation of the new image being installed. Old successfully
    captured user pages use installed_journal_decode instead, under explicit
    Full clearing authority. The original capture always remains intact.
    """
    if len(pair) != 8192:
        raise ValueError('Incomplete Settings capture')
    candidates = []
    for base in (0, PAGE):
        page = pair[base:base + PAGE]
        if page == b'\xff' * PAGE:
            continue
        magic, generation, inverse, reserved = struct.unpack_from('<IIII', page)
        if (magic != FACTORY['COMPACT_PAGE_MAGIC'] or inverse != (~generation & 0xffffffff)
                or reserved != 0xffffffff):
            raise ValueError('Unknown/partial Settings generation; not stock or empty')
        records, offset = {}, 16
        while offset + 16 <= PAGE and page[offset:offset + 4] != b'\xff' * 4:
            magic, kind, size, checksum = struct.unpack_from('<IHHI', page, offset)
            end = offset + 12 + ((size + 3) & ~3)
            if (magic != FACTORY['COMPACT_RECORD_MAGIC'] or not 1 <= kind <= 32
                    or size > 420 or end + 4 > PAGE):
                raise ValueError('Unknown/partial Settings record')
            payload = page[offset + 12:offset + 12 + size]
            if (u32(page, end) != FACTORY['COMPACT_RECORD_COMMIT'] or
                    FACTORY['fnv1a'](struct.pack('<HH', kind, size) + payload) != checksum):
                raise ValueError('Settings checksum/commit rejected')
            if size:
                records[kind] = payload
            else:
                records.pop(kind, None)
            offset = end + 4
        if page[offset:] != b'\xff' * (PAGE - offset):
            raise ValueError('Unclean Settings tail')
        candidates.append((generation, records))
    if not candidates:
        raise ValueError('No trusted current Settings; blank is not proof of stock')
    selected = candidates[0]
    if len(candidates) == 2:
        delta = (candidates[1][0] - selected[0]) & 0xffffffff
        if delta in (0, 0x80000000):
            raise ValueError('Ambiguous Settings generation')
        if delta < 0x80000000:
            selected = candidates[1]
    if 32 in selected[1]:
        raise ValueError('Pending owner operation must not be hidden by Full')
    return selected


def journal_encode(generation: int, records: dict[int, bytes]) -> bytes:
    page = bytearray(b'\xff' * PAGE)
    struct.pack_into('<IIII', page, 0, FACTORY['COMPACT_PAGE_MAGIC'], generation,
                     ~generation & 0xffffffff, 0xffffffff)
    offset = 16
    for kind, data in sorted(records.items()):
        if not 1 <= kind <= 32 or not 0 < len(data) <= 420:
            raise ValueError('Bounded journal record rejected')
        encoded = FACTORY['compact_record'](kind, data)
        if offset + len(encoded) > PAGE:
            raise ValueError('Complete Settings live set does not fit; no partial result')
        page[offset:offset + len(encoded)] = encoded
        offset += len(encoded)
    return bytes(page) + b'\xff' * PAGE


def installed_journal_decode(pair: bytes) -> tuple[int | None, dict[int, bytes], list[str]]:
    """Read only a bounded current native prefix; old user data is disposable.

    Unlike the strict selected-image decoder, unknown old layouts are normal
    Full inputs. Never search for tokens in fragments or older generations.
    A committed current prefix follows the unchanged native cold reader.
    """
    if len(pair) != 2 * PAGE:
        raise ValueError('Incomplete physical Settings capture; no defaulting on read failure')
    if pair == b'\xff' * (2 * PAGE):
        return None, {}, []
    pages, warnings = [], []
    for base in (0, PAGE):
        page = pair[base:base + PAGE]
        magic, generation, inverse, reserved = struct.unpack_from('<IIII', page)
        if (magic == FACTORY['COMPACT_PAGE_MAGIC'] and
                inverse == (~generation & 0xffffffff) and reserved == 0xffffffff):
            pages.append((generation, page))
        elif page != b'\xff' * PAGE:
            warnings.append('Unrecognized old Settings page cleared; no records recovered from it.')
    if not pages:
        return None, {}, warnings
    selected = pages[0]
    if len(pages) == 2:
        delta = (pages[1][0] - selected[0]) & 0xffffffff
        if delta in (0, 0x80000000):
            return None, {}, ['Ambiguous old Settings generations cleared; old Pro not restored.']
        if delta < 0x80000000:
            selected = pages[1]
    generation, page = selected
    records, offset = {}, 16
    while offset + 16 <= PAGE:
        magic, kind, size, checksum = struct.unpack_from('<IHHI', page, offset)
        if magic == 0xffffffff:
            break
        end = offset + 12 + ((size + 3) & ~3)
        if (magic != FACTORY['COMPACT_RECORD_MAGIC'] or not 1 <= kind <= 32
                or size > 420 or end + 4 > PAGE):
            break
        payload = page[offset + 12:offset + 12 + size]
        if (u32(page, end) != FACTORY['COMPACT_RECORD_COMMIT'] or
                FACTORY['fnv1a'](struct.pack('<HH', kind, size) + payload) != checksum):
            break
        if size:
            records[kind] = payload
        else:
            records.pop(kind, None)
        offset = end + 4
    if page[offset:] != b'\xff' * (PAGE - offset):
        warnings.append('Damaged old Settings tail cleared; only the committed current prefix was considered.')
    if 32 in records:
        warnings.append('Old pending user operation cleared by explicit Full; initialized selected defaults.')
    return generation, records, warnings


def password_valid(data: bytes | None) -> bool:
    """Structural admission matching the existing password/QR store decoder."""
    if data is None:
        return False  # clean journal absence is trusted OFF
    if len(data) < 9 or len(data) > 420:
        raise ValueError('Unknown password/QR authority')
    if data[:2] == b'\x01\x00':
        modules, n, offset = data[2], int.from_bytes(data[3:5], 'little'), 9
        if zlib.crc32(data[offset:]) & 0xffffffff != u32(data, 5):
            raise ValueError('Password-OFF QR CRC rejected')
        enabled = False
    elif data[:2] == b'\x01\x01' and len(data) >= 73:
        modules, n, offset = data[2], int.from_bytes(data[3:5], 'little'), 73
        if not u32(data, 5) or not any(data[9:41]) or not any(data[41:73]) or data[9:41] == data[41:73]:
            raise ValueError('Password-ON V/W authority rejected')
        enabled = True
    else:
        raise ValueError('Unknown password/QR authority')
    if modules not in range(21, 58, 4) or n != (modules * modules + 7) // 8 or len(data) != offset + n:
        raise ValueError('Password/QR shape rejected')
    padding = n * 8 - modules * modules
    if padding and data[-1] & ((1 << padding) - 1):
        raise ValueError('Password/QR padding rejected')
    return enabled


def factory_qr(ficr: bytes) -> tuple[str, bytes]:
    # Same immutable DEVICEADDR low word as the shipping App/Recovery adapter.
    from nordic_tool_format.device_link import build_device_link
    from nordic_tool_format.device_qr import encode_device_qr
    name = FACTORY['factory_names'](ficr)[0].decode('ascii')
    qr = encode_device_qr(build_device_link(name))
    return name, struct.pack('<BBBHI', 1, 0, qr.modules, len(qr.data), zlib.crc32(qr.data) & 0xffffffff) + qr.data


def validate_template_records(records: dict[int, bytes], size: int, crc: int) -> bytes | None:
    if not {1,5,7,12}.issubset(records):
        raise ValueError('New Full requires matching Factory and basic user records')
    wire = validate_profile(records[1])
    expected = {1,5,7,12} | ({3} if wire is not None else set())
    FACTORY['device_settings_names'](records[7])
    marker = struct.pack('<IBBHII',FACTORY['BOOT_DISPLAY_MAGIC'],1,int(wire is not None),0,size,crc)
    if (set(records) != expected or records[12] != marker or password_valid(records[5]) or
        records[7][78] != 0 or records[7][96:108] != records[1][20:32] or
        (wire is not None and records[3] != native_screen_record(wire))):
        raise ValueError('Full basic defaults, screen, QR or first-display binding rejected')
    return wire


def identity(ficr: bytes, uicr: bytes) -> str:
    if (len(ficr) != PAGE or len(uicr) != PAGE or u32(ficr, 0x100) != 0x52832 or
            u32(ficr, 0x10) != PAGE or u32(ficr, 0x14) != 128 or
            u32(ficr, 0x10c) != 64 or u32(ficr, 0x110) != 512):
        raise ValueError('Exact FICR/UICR identity/layout gate rejected')
    value = f'{u32(ficr, 0x64):08X}{u32(ficr, 0x60):08X}'
    if value in ('0000000000000000', 'FFFFFFFFFFFFFFFF'):
        raise ValueError('Unusable immutable identity')
    if ficr[0xa4:0xaa] in (bytes(6), b'\xff' * 6):
        raise ValueError('Unusable immutable factory BLE identity')
    return value


def recovery_extent(flash: bytes) -> int:
    """Bounded Full payload extent, NOT Recovery origin/ABI authentication."""
    size = len(flash[RECOVERY:MBR_PARAMS].rstrip(b'\xff'))
    if not 8 <= size <= MBR_PARAMS-RECOVERY:
        raise ValueError('Missing/out-of-range Full Recovery payload')
    return size


def known_recovery(flash: bytes, *, candidate=False) -> int:
    """Internal producer/release provenance, never a user Full admission gate."""
    start,end = RECOVERY,MBR_PARAMS
    for n, digest in KNOWN_RECOVERY.items():
        if (sha(flash[start:start + n]) == digest and
                flash[start + n:end] == b'\xff' * (end - start - n)):
            return n
    raise ValueError('Recovery is not a reviewed build for this exact layout')


def dfu_validate(flash: bytes) -> tuple[int, int, int]:
    backup_start, primary_start = MBR_PARAMS,DFU_SETTINGS
    primary, backup = flash[primary_start:primary_start + 812], flash[backup_start:backup_start + 812]
    if (primary[:DFU_METADATA_BYTES] != backup[:DFU_METADATA_BYTES] or u32(primary, 4) != 2 or u32(primary, 12) != 0 or
            u32(primary, 32) != 1 or u32(primary, 0) != zlib.crc32(primary[4:92]) & 0xffffffff or
            u32(primary, 604) != zlib.crc32(primary[608:803]) & 0xffffffff):
        raise ValueError('DFU metadata CRC/backup/pending gate rejected')
    size, crc = u32(primary, 24), u32(primary, 28)
    sd_size = u32(primary, 52)
    limit = 163840
    if (not 8 <= size <= limit or zlib.crc32(flash[0x26000:0x26000 + size]) & 0xffffffff != crc or
            primary[673] != 1 or u32(primary, 674) != crc or
            not 8 <= sd_size <= 0x25000 or primary[608] != 1 or
            u32(primary, 609) != zlib.crc32(flash[0x1000:0x1000 + sd_size]) & 0xffffffff or
            u32(primary, 20) != 0 or u32(primary, 44) != 0):
        raise ValueError('Application fingerprint/boot CRC rejected')
    return size, crc, u32(primary, 8)


def assets_validate(flash: bytes) -> None:
    region = flash[FONTS:SETTINGS]
    if region == b'\xff' * 32768:
        return
    size = u32(region, 12)
    if not 88 <= size <= 32768 or region[size:] != b'\xff' * (32768 - size):
        raise ValueError('Unknown/partial native user-font region')
    ASSETS['validate'](region[:size], 32768)




def full_template(candidate: dict[int, int]) -> bytes:
    if (not candidate or any(a < 0 or a >= IMAGE for a in candidate)
            or not all(a in candidate for a in (0, 0x26000, FONTS, SETTINGS, RECOVERY, MBR_PARAMS, DFU_SETTINGS))):
        raise ValueError('Full image required; forbidden image/UICR/out-of-range records')
    template = bytes(candidate.get(a, 255) for a in range(FLASH_BYTES))
    TAIL['validate_selectors'](template)
    recovery_extent(template)
    size, crc, version = dfu_validate(template)
    assets_validate(template)
    _, template_records = journal_decode(template[SETTINGS:RECOVERY])
    validate_template_records(template_records,size,crc)
    return template


def preservation_ranges(candidate: dict[int, int]) -> list[dict]:
    """Read only the native Pro authority and undeclared necessary low-Flash gap.

    FICR/UICR are separate fixed 4 KiB reads. The selected MBR/SoftDevice spans
    (including executable FF gaps) are replaced, not inferred from old Flash.
    A low gap is read only if the selected package leaves some of it unspecified.
    """
    template = full_template(candidate)
    start = 0x1000 + u32(template, DFU_SETTINGS + 52)
    ranges = [dict(address=SETTINGS, bytes=8192, file='native-activation-journal.bin')]
    if start < 0x26000 and not all(a in candidate for a in range(start, 0x26000)):
        ranges.insert(0, dict(address=start, bytes=0x26000-start, file='low-flash-gap.bin'))
    return ranges


def prepare(candidate: dict[int, int], current: bytes, ficr: bytes, uicr: bytes,
            ) -> tuple[bytes, dict[int, int], dict]:
    # current is an in-memory address projection of the necessary reads, not a
    # whole-Flash capture. Unread bytes are never used as preservation evidence.
    if len(current) != FLASH_BYTES:
        raise ValueError('Incomplete preservation projection')
    device_id = identity(ficr, uicr)
    template = full_template(candidate)
    size, crc, version = dfu_validate(template)
    cleanup = cleanup_plan(ficr)
    token, authority = activation_authority(current[SETTINGS:RECOVERY], ficr, installed_journal_decode)
    _, template_records = journal_decode(template[SETTINGS:RECOVERY])
    ranges = preservation_ranges(candidate)
    result = bytearray(b'\xff' * FLASH_BYTES)
    for part in ranges:
        if part['address'] == SETTINGS:
            continue  # authority input only; never copy old journal/user records
        start, end = part['address'], part['address'] + part['bytes']
        result[start:end] = current[start:end]
    # MBR and the selected native SoftDevice extent are executable regions,
    # including their FF gaps. Old code there is not Factory authority.
    sd_end=0x1000+u32(template,DFU_SETTINGS+52)
    result[:sd_end]=template[:sd_end]
    # Full initializes application-owned regions; only verified native Pro is
    # restored below. No old DFU epoch is imported; both native pages come
    # from the selected producer output, not a surviving old high-floor page.
    result[0x26000:IMAGE] = template[0x26000:IMAGE]
    result[IMAGE:FLASH_BYTES] = b'\xff'*(FLASH_BYTES-IMAGE)
    result[FONTS:SETTINGS] = b'\xff'*(SETTINGS-FONTS)
    # Preserve only actually captured unknown low-Flash gaps.
    # UICR/FICR/calibration are never part of the programming image.
    for address, value in candidate.items():
        if address < 0x26000:
            result[address] = value
    qr_name, qr = factory_qr(ficr)
    changed_records = dict(template_records)
    changed_records[5] = qr
    changed_records[7] = FACTORY['factory_device_settings_record'](ficr, aux=changed_records[1][20:32])
    if token is not None:
        changed_records[8] = token
    result[SETTINGS:RECOVERY] = journal_encode(1, changed_records)
    # Check the newly synthesized native state, never old metadata. Unknown
    # low-Flash gaps outside declared code remain byte-exact.
    dfu_validate(bytes(result))
    # Every main-Flash page is explicitly initialized. The old authority pages
    # were read only to classify/preserve Pro, never as whole-page restore data.
    pages = list(range(0, FLASH_BYTES, PAGE))
    effective = {a + i: value for a in pages for i, value in enumerate(result[a:a + PAGE])}
    plan = {'schemaVersion': 1, 'board': 'nrf52832_priceetag', 'deviceId': device_id,
            'userData': 'clear-except-verified-native-pro', 'oldEpochRead': False,
            'targetLayout': 56, 'nativeDfuState': 'Both complete native pages initialized from selected Full',
            'uicrPolicy': 'byte-exact UICR retained; selected Full MBR selectors take precedence',
            'preservationRanges': ranges, 'wholeFlashPreRead': False,
            'preservationSha256': sha(b''.join(current[p['address']:p['address']+p['bytes']] for p in ranges)),
            'ficrSha256': sha(ficr), 'uicrSha256': sha(uicr),
            'expectedFlashSha256': sha(result), 'candidateVersion': version, 'appBytes': size, 'appCrc32': f'{crc:08X}',
            'writePages': pages, 'readbackBytes': FLASH_BYTES, 'resetOnlyAfterReadback': True,
            'imagesPreserved': False, 'fontsPreserved': False,
            'retainedRecordIds': [8] if token is not None else [], 'recordCount': len(changed_records),
            'activationRetention': authority, 'warnings': authority.get('warnings', []),
            'diagnosticCleanup': cleanup,
            'externalFlash': 'not accessed or erased',
            'generatedAppName': qr_name, 'nfcEeprom': 'unsupported', 'bleLifecycleSemanticsUnchanged': True,
            'oldSecurityEpochPreserved': False, 'vendorDfuPeerAndTailPreserved': False,
            'consequences': 'Clear user settings/password/QR/Find/fonts/internal images/health; preserve only verified same-device native Pro; initialize native DFU state; retain FICR/UICR/calibration and low-Flash gaps outside selected executable spans; external Flash not accessed'}
    return bytes(result), effective, plan


def preservation_metadata(directory: Path, candidate: dict[int, int], firmware: Path, configuration: Path) -> dict:
    probe_path = directory / 'probe.json'
    if probe_path.is_symlink() or not probe_path.is_file():
        raise ValueError('Missing same-operation probe binding')
    probe = json.loads(probe_path.read_text(encoding='utf-8-sig'))
    if (probe.get('kind') != 'jlink' or not str(probe.get('serial', '')).isdigit()
            or probe.get('sourceFirmwareSha256') != sha(firmware.read_bytes())):
        raise ValueError('Probe/source binding rejected')
    entries = [dict(address=0x10000000, bytes=PAGE, file='nrf52832-priceetag-ficr.bin'),
               dict(address=0x10001000, bytes=PAGE, file='nrf52832-priceetag-uicr.bin')] + preservation_ranges(candidate)
    for entry in entries:
        path = directory / entry['file']
        if path.is_symlink() or not path.is_file() or path.stat().st_size != entry['bytes']:
            raise ValueError('Missing/short preservation read; never infer erased bytes')
        entry['sha256'] = sha(path.read_bytes())
    ficr, uicr = ((directory / e['file']).read_bytes() for e in entries[:2])
    return dict(schemaVersion=1, kind='nrf52832-full-preservation', deviceId=identity(ficr,uicr),
                sourceFirmwareSha256=sha(firmware.read_bytes()), sourceConfigSha256=sha(configuration.read_bytes()),
                probeSerial=probe['serial'], probeBindingSha256=sha(probe_path.read_bytes()),
                wholeFlashPreRead=False, files=entries)


def capture(directory: Path, candidate: dict[int, int], firmware: Path, configuration: Path) -> tuple[bytes, bytes, bytes]:
    info = json.loads((directory / 'preservation-info.json').read_text(encoding='utf-8-sig'))
    if info != preservation_metadata(directory,candidate,firmware,configuration):
        raise ValueError('Preservation capture identity/checksum/source binding changed')
    projection = bytearray(b'\xff' * FLASH_BYTES)
    for part in preservation_ranges(candidate):
        at = part['address']
        projection[at:at+part['bytes']] = (directory / part['file']).read_bytes()
    return (bytes(projection), (directory / 'nrf52832-priceetag-ficr.bin').read_bytes(),
            (directory / 'nrf52832-priceetag-uicr.bin').read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('describe', 'capture', 'prepare', 'check', 'verify', 'cleanup-observe', 'cleanup-verify'))
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--config',type=Path,required=True)
    parser.add_argument('--readback', type=Path)
    args = parser.parse_args()
    candidate,_ = configure_base(args.firmware,args.config)
    if args.action == 'describe':
        ranges = preservation_ranges(candidate)
        print(json.dumps(dict(wholeFlashPreRead=False, ranges=ranges,
                              sharedActivationVerifierAvailable=verifier_available(),
                              mainFlashPreReadBytes=sum(p['bytes'] for p in ranges), ficrBytes=PAGE, uicrBytes=PAGE)))
        return 0
    if not args.capture:
        raise ValueError('Preservation directory required')
    if args.action == 'capture':
        metadata = args.capture / 'preservation-info.json'
        if metadata.exists():
            raise ValueError('Refusing to overwrite preservation evidence')
        metadata.write_text(json.dumps(preservation_metadata(args.capture,candidate,args.firmware,args.config),indent=2))
        return 0
    if not args.output:
        raise ValueError('Plan directory required')
    current, ficr, uicr = capture(args.capture,candidate,args.firmware,args.config)
    if args.action == 'prepare':
        if not args.firmware or args.output.exists():
            raise ValueError('Firmware required and output must be a new per-unit directory')
        expected, effective, plan = prepare(candidate,current,ficr,uicr)
        args.output.mkdir(parents=True)
        HEX['write_hex'](args.output / 'prepared.hex', effective)
        (args.output / 'expected-flash.bin').write_bytes(expected)
        plan['sourceFirmwareSha256'] = sha(args.firmware.read_bytes())
        plan['sourceConfigSha256'] = sha(args.config.read_bytes())
        plan['preparedHexSha256'] = sha((args.output / 'prepared.hex').read_bytes())
        (args.output / 'plan.json').write_text(json.dumps(plan, indent=2))
    else:
        if not args.firmware:
            raise ValueError('Original firmware required for immutable plan validation')
        plan = json.loads((args.output / 'plan.json').read_text())
        expected = (args.output / 'expected-flash.bin').read_bytes()
        recomputed, effective, recomputed_plan = prepare(candidate,current,ficr,uicr)
        if (plan['deviceId'] != identity(ficr, uicr) or
                plan['expectedFlashSha256'] != sha(expected) or len(expected) != FLASH_BYTES or
                plan['sourceFirmwareSha256'] != sha(args.firmware.read_bytes()) or
                plan['sourceConfigSha256'] != sha(args.config.read_bytes()) or
                plan['preparedHexSha256'] != sha((args.output / 'prepared.hex').read_bytes()) or
                expected != recomputed or HEX['read_hex'](args.output / 'prepared.hex') != effective or
                any(plan.get(k) != v for k, v in recomputed_plan.items())):
            raise ValueError('Prepared image/capture binding changed')
        if args.action == 'check':
            print('Unit-bound Full plan verified offline')
            return 0
        if not args.readback:
            raise ValueError('Independent readback directory required')
        if args.action == 'cleanup-observe':
            print(observation(plan['diagnosticCleanup'], (args.readback / 'reset-before.bin').read_bytes()))
            return 0
        if args.action == 'cleanup-verify':
            verify_cleanup(plan['diagnosticCleanup'], (args.readback / 'ram-after.bin').read_bytes(),
                           (args.readback / 'reset-before.bin').read_bytes(), (args.readback / 'reset-after.bin').read_bytes())
            if ((args.readback / 'cleanup-ficr.bin').read_bytes() != ficr or
                    (args.readback / 'cleanup-uicr.bin').read_bytes() != uicr or
                    (args.readback / 'cleanup-boot.bin').read_bytes() != expected[MBR_PARAMS:IMAGE]):
                raise ValueError('Post-cleanup identity/vendor/boot state changed; no reset')
            print('Physical SRAM/reset-cause cleanup and unchanged identity/native boot verified; target remains halted')
            return 0
        actual = tuple((args.readback / f'{kind}.bin').read_bytes() for kind in ('flash', 'ficr', 'uicr'))
        if actual != (expected, ficr, uicr):
            raise ValueError('Independent Flash/FICR/UICR readback mismatch; do not reset or replay')
        print('Exact Full readback verified; no device operation performed by this helper')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
