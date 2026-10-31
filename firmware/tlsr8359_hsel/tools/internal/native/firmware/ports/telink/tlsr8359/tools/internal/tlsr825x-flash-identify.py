#!/usr/bin/env python3
"""Read TLSR825x-family Flash status and JEDEC ID over USB-UART SWS."""

from __future__ import annotations

import argparse
import importlib.util
import hashlib
import json
from datetime import datetime, timezone
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[6] / 'tools/hardware'))
from tlsr_flash_jedec import FLASH_BYTES, checked_pair, consent_fields


def confirm_registered_jedec(observed, registered, *, ask=None):
    observed, registered = checked_pair(observed, registered)
    mismatch = observed != registered
    if mismatch:
        if registered is None:
            print(f'WARNING: Config has no registered Flash model; detected {observed} ({FLASH_BYTES[observed] // 1024} KiB).', flush=True)
        else:
            print(f'WARNING: Config Flash JEDEC {registered} ({FLASH_BYTES[registered] // 1024} KiB) '
                  f'differs from detected {observed} ({FLASH_BYTES[observed] // 1024} KiB).', flush=True)
        print('Only the lower 512-KiB Full window may be programmed. Upper Flash remains untouched. '
              'PCB wiring, exact-unit backup/identity, protection and signature checks still apply.', flush=True)
        print('Continue with the detected Flash model for this run? [y/N]', flush=True)
        try:
            answer = (input if ask is None else ask)().strip().lower()
        except (EOFError, OSError):
            answer = ''
        if answer != 'y':
            raise RuntimeError('Flash model mismatch cancelled; no UID requested or Flash erased/programmed.')
        print(f'Flash model mismatch confirmed for this run: {registered} -> {observed}. '
              'This does not qualify another PCB or its backup.', flush=True)
    return consent_fields(observed, registered, confirmed=mismatch)


def load_adapter(path: Path):
    spec = importlib.util.spec_from_file_location("inktag_tlsr825x_adapter", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load TLSR825x adapter: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_status(backend, serial_port, opcode):
    if not backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01])):
        print('SWS setup failed: UART echo incomplete while requesting Flash CS idle; no status command sent.')
        return None
    try:
        for address, value in ((0x0D, 0), (0x0C, opcode), (0x0C, 0)):
            if not backend.rd_sws_wr_addr_usbcom(serial_port, address, bytearray([value])):
                return None
        data = backend.sws_read_data(serial_port, 0x0C)
    finally:
        idle = backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([1]))
    return data[0] if idle and data is not None and len(data) == 1 else None


def confirmed_status(backend, serial_port, samples):
    """Bounded read-only settling, comparing every bit of consecutive pairs.

    No activation/reset/status write is repeated. Retain every complete or
    missing observation in the log; a stable BUSY response is never admission.
    """
    history, consecutive = [], []
    for _ in range(samples * 3):
        pair = (bounded_read(read_status, backend, serial_port, 0x05),
                bounded_read(read_status, backend, serial_port, 0x35))
        history.append(pair)
        print('Flash status pair {0}: SR1={1}, SR2={2}'.format(len(history),
              'ERR' if pair[0] is None else f'0x{pair[0]:02X}',
              'ERR' if pair[1] is None else f'0x{pair[1]:02X}'), flush=True)
        if not all(type(v) is int and 0 <= v <= 255 for v in pair) or pair[0] & 1:
            consecutive = []
        elif consecutive and consecutive[-1] != pair:
            consecutive = [pair]
        else:
            consecutive.append(pair)
        if len(consecutive) == samples:
            if len(history) > samples:
                print(f'IDENTIFY: read-only status settled after {len(history)} pairs; {samples} consecutive complete pairs match. Earlier samples retained.', flush=True)
            return history, pair
    raise RuntimeError(f'Flash status samples remain missing/inconsistent/busy after {samples * 3} read-only pairs; no UID requested or Flash erased/programmed')


def read_jedec_id(backend, serial_port):
    if not backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01])):
        print('SWS setup failed: UART echo incomplete while requesting Flash CS idle; no RDID command sent.')
        return None
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0x80]))
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x00]))
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0C, bytearray([0x9F, 0x00]))
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x0A]))
    data = backend.sws_read_data(serial_port, 0x0C, 3)
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([0x01]))
    backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0x00]))
    return None if data is None else bytes(data)


def read_uid(backend, serial_port):
    """Same read-only 0x4B + four dummy bytes as inktag_tlsr_identity.c.

    Never WREN/program/OTP. UART echo is transport completeness, not proof of
    chip ACK; three complete matching non-sentinel reads are required by main.
    """
    try:
        for address, data in ((0x0D, [1]), (0x0B3, [0x80]), (0x0D, [0]),
                              (0x0C, [0x4B, 0, 0, 0, 0, 0]), (0x0D, [0x0A])):
            if not backend.rd_sws_wr_addr_usbcom(serial_port, address, bytearray(data)):
                return None
        data = backend.sws_read_data(serial_port, 0x0C, 16)
        return bytes(data) if data is not None and len(data) == 16 else None
    finally:
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0D, bytearray([1]))
        backend.rd_sws_wr_addr_usbcom(serial_port, 0x0B3, bytearray([0]))


def confirmed_uid(samples):
    if (len(samples) != 3 or any(item is None or len(item) != 16 for item in samples)
            or len(set(samples)) != 1 or samples[0] in (bytes(16), b'\xff' * 16, b'\x51\x01' * 8)):
        raise RuntimeError('Flash UID missing/inconsistent/unsupported; no identity guessed.')
    return samples[0]


def bounded_read(reader, backend, serial_port, *args, attempts=6):
    """Retry only an incomplete read-only transaction, not activation/write."""
    for attempt in range(attempts):
        value=reader(backend,serial_port,*args)
        if value is not None:
            if attempt: print(f'IDENTIFY: complete {reader.__name__} after {attempt} incomplete read(s).')
            return value
        print(f'IDENTIFY: incomplete {reader.__name__}; bounded read {attempt+1}/{attempts}.')
    return None


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Read-only TLSR825x Flash status/JEDEC identification"
    )
    parser.add_argument("backend", type=Path)
    parser.add_argument("--port", "-p", required=True)
    parser.add_argument("--baud", "-b", type=int, default=921600)
    parser.add_argument("--activation-ms", "-t", type=int, default=20000)
    parser.add_argument('--reset-pulse-ms', type=int, default=500)
    parser.add_argument("--clock-mhz", "-c", type=int, default=0)
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--debug", "-d", action="store_true")
    parser.add_argument('--uid-output', type=Path, help='Fresh confirmed read-only UID receipt; no overwrite.')
    parser.add_argument('--expected-jedec', help='Optional historical Config JEDEC. Missing registration or a supported model difference requires interactive y/N confirmation.')
    parser.add_argument("--retry-detect", action="store_true")
    parser.add_argument("--detect-timeout", type=int, default=0)
    parser.add_argument("--detect-interval", type=int, default=1)
    args = parser.parse_args()
    if not 50 <= args.reset_pulse_ms <= 2000:
        raise SystemExit('RESET pulse must be 50..2000 ms; no device accessed.')
    if args.uid_output and (args.uid_output.exists() or (args.expected_jedec is not None and
            (len(args.expected_jedec) != 6 or any(c not in '0123456789abcdefABCDEF' for c in args.expected_jedec)))):
        raise SystemExit('UID requires a new receipt path and valid optional expected JEDEC; no device accessed.')
    if args.samples < 2 or args.samples > 64:
        raise SystemExit("samples must be between 2 and 64")
    if args.detect_timeout < 0 or args.detect_interval < 1:
        raise SystemExit("invalid contact timing")

    adapter_path = Path(__file__).with_name("tlsr825x-early-activate.py")
    adapter = load_adapter(adapter_path)
    backend = adapter.load_backend(args.backend.resolve())
    adapter.install_exact_sws_frames(backend)
    adapter.install_early_activation(backend, retry_detect=args.retry_detect,
        detect_timeout_seconds=args.detect_timeout, detect_interval_seconds=args.detect_interval,
        clock_hz=args.clock_mhz * 1_000_000, reset_pulse_ms=args.reset_pulse_ms)
    adapter.install_whole_chip_reset(backend)
    backend.debug = args.debug

    serial_port = backend.serial.Serial(args.port, args.baud)
    serial_port.reset_input_buffer()
    serial_port.timeout = 0.1
    try:
        backend.activate(serial_port, args.activation_ms)
        if args.clock_mhz:
            speed_ready = backend.set_sws_speed(
                serial_port, args.clock_mhz * 1_000_000
            )
        else:
            speed_ready = backend.set_sws_auto_speed(serial_port)
        if not speed_ready:
            raise RuntimeError("SWS speed validation failed")

        print('FLASH BUS: requesting CS idle before each new read-only transaction; UART echo is not a target ACK.')
        jedec_ids = [bounded_read(read_jedec_id,backend, serial_port) for _ in range(3)]
        status_pairs, stable_status = confirmed_status(backend, serial_port, args.samples)
        status1 = [pair[0] for pair in status_pairs]
        status2 = [pair[1] for pair in status_pairs]
        print(
            "Flash status-1 samples: "
            + " ".join("ERR" if value is None else f"0x{value:02X}" for value in status1)
        )
        print(
            "Flash status-2 samples: "
            + " ".join("ERR" if value is None else f"0x{value:02X}" for value in status2)
        )
        print(
            "Flash JEDEC samples:  "
            + " ".join(
                "ERR" if value is None else value.hex().upper() for value in jedec_ids
            )
        )
        valid_jedec_ids = [value for value in jedec_ids if value is not None]
        if len(valid_jedec_ids) != 3 or len(set(valid_jedec_ids)) != 1:
            raise RuntimeError("Flash JEDEC samples are missing or inconsistent")
        if valid_jedec_ids[0] in (b"\x00\x00\x00", b"\xFF\xFF\xFF"):
            raise RuntimeError("Flash JEDEC ID is an invalid all-zero/all-FF response")
        if args.uid_output:
            observed_jedec = valid_jedec_ids[0].hex().upper()
            consent = confirm_registered_jedec(observed_jedec, args.expected_jedec)
            samples = [bounded_read(read_uid,backend, serial_port) for _ in range(3)]
            uid = confirmed_uid(samples)
            receipt = {'schema': 'inktag-tlsr-live-flash-uid-v1', 'port': args.port,
                       'jedec': observed_jedec, 'uidHex': uid.hex().upper(),
                       'samples': [x.hex().upper() for x in samples],
                       'statusPairs': [list(pair) for pair in status_pairs],
                       'stableStatus': list(stable_status),
                       'backendSha256': hashlib.sha256(args.backend.read_bytes()).hexdigest().upper(),
                       'finishedUtc': datetime.now(timezone.utc).isoformat(),
                       'identityAlgorithm': 'shipping Flash UID FNV1a; no selector fallback',
                       'flashWrites': 0, **consent}
            args.uid_output.parent.mkdir(parents=True, exist_ok=True)
            with args.uid_output.open('x', encoding='utf-8') as stream:
                stream.write(json.dumps(receipt, indent=2) + '\n')
            print('Flash UID: three matching read-only samples captured; no MAC/trim written.')
    finally:
        backend.sws_wr_addr_usbcom(serial_port, 0x006F, bytearray([0x22]))
        serial_port.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Flash identification failed: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
