"""Validate and write generated TLSR Free bins; no preservation or CLI/BLE runtime."""
from __future__ import annotations

import argparse
import io
import json
from pathlib import Path
import re
import struct
import tempfile
import zipfile

from ble_factory_address import decode
from inktag_programmer_formats.ota import inspect_package
from tlsr_factory_config import validate_records, validate_aux_record, PIN_CONFIG_CAPABILITY, AUX_CAPABILITY
from tlsr_full_flash_address import selector, typed_pair, AUTO_CAPABILITY, INIT_MARKER, INIT_PENDING
from tlsr_full_flash_config import ROOT, PCB_BACKENDS
from tlsr_full_flash_contract import resolve_contract
from tlsr_full_flash_state import BASE_BYTES, FACTORY, defaults, initialize_full_state, sha
from tlsr_trusted_entry_profile import check_pair

PUBLIC_KEY = ROOT / 'firmware/ports/telink/tlsr8359/ota-production-public.json'


def inspect_bin(image: bytes) -> dict:
    if len(image) != BASE_BYTES:
        raise ValueError('Requires a complete 512-KiB bin from generate-bin.ps1')
    board_id, flags = selector(image[0x7F100:0x7F10C])
    if flags & 6 != 6:
        raise ValueError('Bin requires complete pins and typed-address configuration')
    rows = [(mark, row) for mark, row in PCB_BACKENDS.items() if row[4] == board_id]
    if len(rows) != 1:
        raise ValueError('Unknown bin board selector')
    marking, board = rows[0][0], rows[0][1][0]
    generic = image[FACTORY:FACTORY+8] == b'\xff'*8 and image[0x7F160:0x7F170] == b'\xff'*16
    address, kind = (None, 1) if generic else decode(image[0x7F160:0x7F16B])
    if not generic and (image[FACTORY:FACTORY+6] != address[::-1] or image[0x7F16B:0x7F170] != b'\xff' * 5):
        raise ValueError('Bin legacy and typed MAC records disagree')
    pins, screen = validate_records(board_id, image[0x7F110:0x7F130], image[0x7F140:0x7F160])
    aux = validate_aux_record(image[0x7F130:0x7F140], pins, board_id)
    if aux is None:
        raise ValueError('Bin requires complete AUX defaults')

    def native(at, limit, signature_bytes=0):
        length = int.from_bytes(image[at+24:at+28], 'little') + 4 + signature_bytes
        if not 32 <= length <= limit:
            raise ValueError('Invalid native bin image size')
        return image[at:at+length]

    application = native(0x1000, 0x3F000, 96)
    recovery = native(0x40000, 0x11000)
    stage0 = native(0, 0x1000)
    check_pair(application[:-96], recovery, 'layout-v1', stage0)
    if not typed_pair(application, recovery) or any(marker not in application for marker in (PIN_CONFIG_CAPABILITY, AUX_CAPABILITY)):
        raise ValueError('Bin requires current typed-address/pin/AUX-aware App and Recovery')
    automatic = all(AUTO_CAPABILITY in role for role in (application, recovery))
    if generic and not automatic:
        raise ValueError('Generic bin requires automatic-MAC-aware App AND Recovery')
    # Reconstruct only public envelope metadata from the native signed trailer.
    # The maintained verifier validates the real body, signed context and key.
    epoch, _, _, _, version = struct.unpack_from('<IIIII', application, len(application)-84)
    signature = json.loads(PUBLIC_KEY.read_text(encoding='utf-8'))
    signature.update(required=True, signed_context_size=16, signed_board='tlsr8359_hanshow',
                     signed_layout='layout-v1', board_signature=0x53484C54,
                     layout_signature=0x31564C54, verifier_signature=0x314B4C54,
                     signed_prefix_size=32, signed_trailer_size=96)
    manifest = dict(format='inktag-ota-v1', transport='telink-fixed-recovery-v2',
                    requiredBaseline='inktag-release-v1', board='tlsr8359_hanshow', layout='layout-v1',
                    releaseVersion=version, compatibilityEpoch=epoch, signature=signature,
                    application=dict(file='application.bin', size=len(application)-96,
                        transfer_size=len(application), declared_size=len(application)-100,
                        crc32=int.from_bytes(application[-100:-96], 'big'),
                        sha256=sha(application), active_address=0x1000))
    with tempfile.TemporaryDirectory(prefix='inktag-bin-verify-') as temp:
        package = Path(temp) / 'verify.zip'
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr('manifest.json', json.dumps(manifest))
            archive.writestr('application.bin', application)
        inspect_package(package)

    records, links = defaults(address[::-1] if address is not None else None, screen, pins, aux, board_id)
    state = initialize_full_state(records, epoch, confirmed=True)
    expected = bytearray(b'\xff' * BASE_BYTES)
    for at, data in ((0, stage0), (0x1000, application), (0x40000, recovery),
                     (FACTORY, address[::-1] if address is not None else b'\xff'*6), (0x7F100, image[0x7F100:0x7F10C]),
                     (0x7F10C, b'\xa5'), (0x7F110, image[0x7F110:0x7F170])):
        expected[at:at+len(data)] = data
    if automatic:
        expected[INIT_MARKER] = INIT_PENDING if generic else 0
    for at, data, _ in state['writes']:
        expected[at:at+len(data)] = data
    if image != expected:
        raise ValueError('Bin is not a complete freshly generated Free image; defaults, padding or code changed')
    entry = json.loads((ROOT / 'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards'][board]
    contract, warnings = resolve_contract(entry, board_id)
    return dict(schema='inktag-tlsr-bin-flash-v1', image_sha256=sha(image), image_bytes=len(image),
                board=board, board_id=board_id, board_marking=marking, flash_contract=contract,
                write_admitted=True, mac=address.hex(':').upper() if address is not None else None,
                address_type=kind, automatic_mac=generic,
                upper_slots_enabled=board_id == 2 and not flags & 1,
                application_name=links['applicationName'], compatibility_epoch=epoch,
                application_crc32=manifest['application']['crc32'], release_version=version,
                qualification_warnings=warnings, hardware_started=False,
                write_range='0x00000-0x7FFFF only; upper Flash untouched',
                preservation='None: replaces lower512-KiB identity/calibration/Pro/password/settings/images with supplied Free bin')


def inspect_file(source: Path) -> tuple[bytes, dict]:
    if source.is_symlink() or not source.is_file() or source.stat().st_size != BASE_BYTES:
        raise ValueError('Requires a regular complete 512-KiB generated bin')
    image = source.read_bytes()
    return image, inspect_bin(image)


def check_capacity(review: dict, jedec: bytes) -> None:
    from tlsr_flash_jedec import FLASH_BYTES
    capacity = FLASH_BYTES.get(jedec.hex().upper(), 0)
    if capacity < BASE_BYTES:
        raise ValueError('Unsupported or insufficient Flash; no erase')
    if review['upper_slots_enabled'] and (capacity != 2 * BASE_BYTES or jedec.hex().upper() != 'EB6014'):
        raise ValueError('Enabled high slots require qualified actual1-MiB Flash; no erase')


def extract_request(arguments):
    option = '--inktag-bin-receipt'
    args = list(arguments)
    if option not in args:
        return args, None
    if args.count(option) != 1 or args.index(option) + 1 >= len(args):
        raise ValueError('Bin write requires one new receipt path')
    at = args.index(option)
    receipt = Path(args[at+1]).resolve()
    args = args[:at] + args[at+2:]
    hash_option = '--inktag-bin-sha256'
    if args.count(hash_option) != 1 or args.index(hash_option) + 1 >= len(args):
        raise ValueError('Bin write requires the confirmed input SHA256')
    at = args.index(hash_option)
    expected_hash = args[at+1]
    args = args[:at] + args[at+2:]
    if args.count('wf') != 1 or any(op in args for op in ('rf', 'we', 'es', 'ea')):
        raise ValueError('Bin mode permits one range write only')
    at = args.index('wf')
    if at + 3 != len(args) or int(args[at+1], 0) != 0:
        raise ValueError('Bin must be written once at address0')
    source = Path(args[at+2]).resolve(strict=True)
    image, review = inspect_file(source)
    if not re.fullmatch('[0-9a-fA-F]{64}', expected_hash) or sha(image).lower() != expected_hash.lower():
        raise ValueError('Bin changed since confirmation; no device access')
    if receipt.exists() or receipt == source:
        raise ValueError('Receipt exists or aliases source; no device access')
    return args, (source, image, receipt, review)


def install(backend, source: Path, receipt: Path, image: bytes, expected_jedec: bytes,
            *, output: Path | None = None, progress=None):
    """One exact bin write, optional readback; no UID/Factory/Settings capture."""
    review = inspect_bin(image)  # Before backend.main opens the port.
    check_capacity(review, expected_jedec)
    if (source.read_bytes() != image or source.is_symlink() or receipt.exists() or
            (output is not None and (output.exists() or output in (source, receipt)))):
        raise ValueError('Bin/evidence changed or output exists; no device access')
    original_unlock, original_write = backend.FlashUnlock, backend.FlashWriteBlock
    original_reset = backend.sws_wr_addr_usbcom
    admitted = unlock_attempted = attempted = completed = reset_started = False
    mutation_started = write_finished = transport_failed = False
    result = dict(schema='inktag-tlsr-bin-flash-v1', status='BIN-NOT-WRITTEN',
                  imageSha256=sha(image), writtenBytes=None, readbackSha256=None,
                  jedec=expected_jedec.hex().upper(), readbackRequested=output is not None,
                  wholeChipResetRequested=False, hardwareBootObserved=False,
                  upperFlashTouched=False, writeRetryAllowed=False,
                  writeTransportError=None)

    def save():
        receipt.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')

    def fail_transport(operation, reason):
        nonlocal transport_failed, completed
        if not transport_failed:
            transport_failed = True
            completed = False
            result.update(status='BIN-TRANSPORT-FAILED-OR-UNKNOWN',
                          writeTransportError=dict(operation=operation, reason=reason))
            save()
        raise ValueError('Bin transport failed; no further Flash command/reset/retry: ' +
                         result['writeTransportError']['operation'])

    def strict_transport(name, original):
        def checked(*args, **kwargs):
            if transport_failed:
                fail_transport(name, 'previous error latched')
            active = (mutation_started or attempted) and not write_finished
            try:
                value = original(*args, **kwargs)
            except Exception as error:
                if active and not transport_failed:
                    fail_transport(name, type(error).__name__)
                raise
            if active:
                if name == 'wr_usbcom_blk':
                    valid = type(value) is int and value == len(args[1])
                elif name == 'sws_read_data':
                    size = args[2] if len(args) > 2 else kwargs.get('size', 1)
                    valid = isinstance(value, (bytes, bytearray, list, tuple)) and len(value) == size
                else:
                    valid = value is True
                if not valid:
                    fail_transport(name, 'incomplete UART write/echo or SWS response during programming')
            return value
        return checked

    def unlock(port):
        nonlocal admitted, unlock_attempted
        if unlock_attempted or source.read_bytes() != image:
            raise ValueError('Repeated unlock or changed bin; no erase')
        unlock_attempted = True
        result.update(port=getattr(port, 'port', None), uartBaud=getattr(port, 'baudrate', None))
        # Maintained original unlock checks actual JEDEC and supported status
        # registers before WREN. No old Flash array or UID read is requested.
        admitted = bool(original_unlock(port))
        return admitted

    def write(port, stream, offset, size, erase=True):
        nonlocal attempted, completed, write_finished
        if attempted or not admitted or offset != 0 or size != BASE_BYTES or not erase or stream.tell() != 0:
            raise ValueError('Repeated/invalid/unadmitted bin write')
        attempted = True
        if stream.read() != image:
            raise ValueError('Bin changed before erase')
        result['status'] = 'BIN-WRITE-FAILED-OR-UNKNOWN'
        save()
        from contextlib import nullcontext
        with (output.open('xb') if output is not None else nullcontext(None)) as readback:
            data = io.BytesIO(image)
            if progress:
                data = progress(data, len(image))
            if not original_write(port, data, 0, len(image), True):
                return False
            write_finished = True  # Read-only verification may retain bounded read retries.
            if progress:
                data.finish()
            result.update(status='BIN-WRITTEN-NOT-VERIFIED', writtenBytes=len(image))
            save()
            if output is None:
                completed = True
                print('BIN-WRITTEN-NOT-VERIFIED: 524288 bytes; no readback requested.', flush=True)
                return True
            print('BIN READBACK: complete512-KiB comparison; no reset or write retry.', flush=True)
            backend.InkTagSetExpectedReadback(image)
            if not backend.FlashReadBlock(port, readback, 0, len(image)):
                return False
        if output.read_bytes() != image:
            return False
        completed = True
        result.update(status='BIN-READBACK-MATCHED', readbackSha256=sha(image))
        save()
        print('BIN-READBACK-MATCHED: 524288 bytes; SHA256=' + sha(image), flush=True)
        return True

    def reset(port, address, data):
        nonlocal reset_started
        is_reset = address == 0x6F and bytes(data) in (b'\x20', b'\x22')
        if is_reset and transport_failed:
            raise ValueError('Write transport failed; reset forbidden')
        if is_reset and attempted:
            if not completed or reset_started:
                raise ValueError('Unverified/repeated bin reset forbidden')
            reset_started = True
        sent = original_reset(port, address, data)
        if is_reset and completed:
            if type(sent) is not int or sent != len(backend.sws_wr_addr(address, b'\x20')):
                raise ValueError('Final reset transport incomplete; startup unknown')
            result['wholeChipResetRequested'] = True
            save()
        return sent

    with receipt.open('x', encoding='utf-8') as handle:
        handle.write(json.dumps(result, indent=2) + '\n')
    # The pinned writer ignores some command-echo return values and its status
    # reader can recover after missing frames. During erase/program, that is
    # an unknown transaction, not permission to continue or eventually run App.
    for name in ('wr_usbcom_blk', 'rd_wr_usbcom_blk', 'rd_sws_wr_addr_usbcom', 'sws_read_data'):
        original = getattr(backend, name, None)
        if callable(original):
            setattr(backend, name, strict_transport(name, original))
    original_enable = getattr(backend, 'FlashWriteEnable', None)
    if callable(original_enable):
        def enable(port):
            nonlocal mutation_started
            mutation_started = True
            if transport_failed:
                fail_transport('FlashWriteEnable', 'previous error latched')
            return original_enable(port)
        backend.FlashWriteEnable = enable
    backend.FlashUnlock, backend.FlashWriteBlock, backend.sws_wr_addr_usbcom = unlock, write, reset
    if hasattr(backend, 'signal_handler'):
        def interrupt(*_):
            result['status'] = 'BIN-INTERRUPTED-NOT-VERIFIED'
            save()
            raise SystemExit(130)
        backend.signal_handler = interrupt
    return review


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin', type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_file(args.bin)[1], indent=2))
    except (ValueError, OSError, KeyError, struct.error) as error:
        parser.error(str(error))
