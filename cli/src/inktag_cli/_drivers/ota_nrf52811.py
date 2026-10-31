#!/usr/bin/env python3
"""Canonical InkTag CLI driver for Nordic Secure DFU over BLE."""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import struct
import sys
import time
import zipfile
import zlib
from dataclasses import dataclass
from contextlib import asynccontextmanager
from pathlib import Path

from bleak import BleakClient, BleakScanner
from inktag_cli.connection_status import report_ble_connection
from inktag_cli.recovery_feedback import driver_print as print, recovery_phase
from inktag_cli.generated_commands_v2 import CommandV2
from inktag_cli.ota_password import authorize_ota_application, ota_application_requests
from inktag_cli.transport import InkTagV2StatusError, release_bleak_client, select_device
from inktag_cli.safe_archive import OtaArchive


DFU_SERVICE_UUID = "0000fe59-0000-1000-8000-00805f9b34fb"
DFU_CONTROL_UUID = "8ec90001-f315-4f60-9fb8-838830daea50"
DFU_PACKET_UUID = "8ec90002-f315-4f60-9fb8-838830daea50"
BUTTONLESS_UUID = "8ec90003-f315-4f60-9fb8-838830daea50"
GAP_NAME_UUID = "00002a00-0000-1000-8000-00805f9b34fb"
RECOVERY_NAME_PATTERN = re.compile(r"^(?:IKT|InkTag)_NRF_([0-9A-F]{4})_OTA$", re.IGNORECASE)
APPLICATION_NAME_PATTERN = re.compile(r"^(?:IKT|InkTag)_NRF_([0-9A-F]{4})$", re.IGNORECASE)
BUTTONLESS_ENTER = 0x01
BUTTONLESS_RESPONSE = 0x20
BUTTONLESS_SUCCESS = 0x01

OP_CREATE = 0x01
OP_SET_PRN = 0x02
OP_CALCULATE_CRC = 0x03
OP_EXECUTE = 0x04
OP_SELECT = 0x06
OP_ABORT = 0x0C
OP_RESPONSE = 0x60
RESULT_SUCCESS = 0x01
OBJECT_COMMAND = 0x01
OBJECT_DATA = 0x02
DEFAULT_PRN = 4
MAX_OBJECT_ATTEMPTS = 3


@dataclass(frozen=True)
class DfuPackage:
    init_packet: bytes
    firmware: bytes


class DfuResponseTimeout(TimeoutError):
    """No native response, not a device refusal or permission to replay."""


class FinalExecuteAckUnknown(RuntimeError):
    """Final object CRC passed, but its one EXECUTE has no known reply."""


def load_package(path: Path) -> DfuPackage:
    with OtaArchive(path) as archive:
        manifest = archive.manifest()
        root = manifest.get("manifest", manifest)
        application = root.get("application")
        if not application:
            raise ValueError("Only an application-only Nordic Secure DFU package is supported")
        if any(root.get(name) for name in ("bootloader", "softdevice", "softdevice_bootloader")):
            raise ValueError("Package must not contain a SoftDevice or bootloader")
        packet = archive.read(application['dat_file'])
        firmware = archive.read(application['bin_file'])
        from inktag_cli.signed_preflight import verify_nordic
        verify_nordic(firmware, packet)
        return DfuPackage(init_packet=packet, firmware=firmware)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def progress(done: int, total: int, started: float) -> None:
    width = 34
    ratio = min(1.0, done / total) if total else 1.0
    filled = int(width * ratio)
    elapsed = max(time.monotonic() - started, 0.001)
    rate = done / elapsed / 1024
    bar = "#" * filled + "." * (width - filled)
    print(f"\r[{bar}] {ratio * 100:6.2f}%  {done}/{total} bytes  {rate:6.1f} KiB/s", end="", flush=True)
    if done >= total:
        print()


def characteristic_uuids(client: BleakClient) -> set[str]:
    return {
        characteristic.uuid.lower()
        for service in client.services
        for characteristic in service.characteristics
    }


@asynccontextmanager
async def connected_client(device, timeout: float, *, fresh_services: bool = False):
    """Use the shared WinRT link release, without new retries or error masking."""
    options = {"winrt": {"use_cached_services": False}} if fresh_services and sys.platform == "win32" else {}
    client = BleakClient(device, timeout=timeout, **options)
    primary_error = None
    try:
        await client.connect()
        report_ble_connection(device)
        yield client
    except BaseException as error:
        primary_error = error
        raise
    finally:
        try:
            await release_bleak_client(client)
        except Exception as cleanup_error:
            if primary_error is None:
                raise
            # Preserve the original refusal/verification/connection failure.
            # The command still fails, with both errors retained for diagnosis.
            primary_error.add_note(f"BLE link cleanup also failed: {type(cleanup_error).__name__}")


def stable_id4(name: str | None, address: str | None = None) -> str | None:
    normalized = (name or "").strip()
    match = APPLICATION_NAME_PATTERN.fullmatch(normalized) or RECOVERY_NAME_PATTERN.fullmatch(normalized)
    if match:
        return match.group(1).upper()

    parts = (address or "").split(":")
    if len(parts) == 6 and all(len(part) == 2 and all(char in "0123456789abcdefABCDEF" for char in part) for part in parts):
        return f"{parts[-2]}{parts[-1]}".upper()
    return None


def recovery_name_from_identity(application_name: str | None, application_address: str | None = None) -> str:
    identity_suffix = stable_id4(application_name, application_address)
    if identity_suffix is None:
        raise ValueError(
            "Cannot determine the stable Nordic ID4 from the application name or BLE address"
        )
    return f"IKT_NRF_{identity_suffix}_OTA"


def application_name_from_bootloader(bootloader_name: str, requested_name: str | None) -> str | None:
    if requested_name and not RECOVERY_NAME_PATTERN.fullmatch(requested_name.strip()):
        return requested_name
    identity_suffix = stable_id4(bootloader_name)
    return f"IKT_NRF_{identity_suffix}" if identity_suffix else None


async def discover_device(name: str | None, address: str | None, timeout: float, bootloader: bool = False,
                          require_unique: bool = False, return_name: bool = False):
    target_label = address or name or ("IKT_NRF_<ID4>_OTA" if bootloader else "InkTag Nordic")
    if address and not bootloader:
        # Reuse the maintained exact-address WinRT/cache selection and the
        # filtered/merged watcher on other hosts. A missing fresh advertisement
        # is not proof that this explicitly selected App cannot be connected.
        device, advertisement = await select_device(name, address, timeout)
        advertised_name = (advertisement.local_name if advertisement else None) or device.name or ""
        print(f"Selected exact BLE address: {device.address}; GATT role not yet verified")
        return (device, advertised_name) if return_name else device
    print(f"Scanning for {target_label}...")
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    candidates = []
    for device, advertisement in found.values():
        advertised_name = advertisement.local_name or device.name or ""
        services = {uuid.lower() for uuid in advertisement.service_uuids}
        if address and device.address.lower() != address.lower():
            continue
        if name and not advertised_name.lower().startswith(name.lower()):
            continue
        if not bootloader and not address and not name and not advertised_name.startswith(("IKT_NRF", "InkTag_NRF")):
            continue
        if bootloader:
            if DFU_SERVICE_UUID not in services or RECOVERY_NAME_PATTERN.fullmatch(advertised_name) is None:
                continue
        candidates.append((device, advertised_name, DFU_SERVICE_UUID in services))

    if not candidates:
        raise RuntimeError(f"No matching BLE device found: {target_label}")
    if require_unique and len(candidates) != 1:
        raise RuntimeError("Multiple Nordic Recovery devices match; select one exact identity")

    candidates.sort(key=lambda item: (not item[2], item[1]))
    device, advertised_name, _ = candidates[0]
    print(f"Found: {advertised_name or '(unnamed)'} [{device.address}]")
    return (device, advertised_name) if return_name else device


async def wait_for_application(
    name: str | None, address: str | None, timeout: float, connect_timeout: float,
    expected_firmware: bytes | None, password_data_dir: Path | None = None,
) -> dict:
    """Read the returned App's V2 image; compare an OTA image when supplied.

    Nordic's final EXECUTE response is sent before dual-bank activation, reset
    and Windows' address cache have fully settled. Treat transfer completion
    and application recovery as separate gates so a silent post-OTA device is
    never reported as success. Direct guarded exit has no prior App snapshot;
    its result reports that honestly instead of inventing a pre-exit checksum.
    """
    started = time.monotonic()
    deadline = started + timeout
    label = address or name or "InkTag application"
    print(f"Waiting for application BLE reconnect ({label})", end="", flush=True)
    attempts = 0
    last_phase = "selection"
    last_error = "no candidate"

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        print(".", end="", flush=True)
        try:
            # Use the same exact-address path as ordinary CLI connections.
            # WinRT may have an authorized requester even when a fresh bulk
            # scanner yields no advertisements after the DFU role switch.
            candidate, advertisement = await select_device(
                None if address else name, address, min(3.0, remaining),
            )
        except Exception as exc:
            last_phase, last_error = "selection", type(exc).__name__
            await asyncio.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
            continue
        if address and candidate.address.lower() != address.lower():
            raise RuntimeError("Post-OTA selection returned a different device address")
        candidate_name = getattr(advertisement, "local_name", None) or candidate.name or ""
        if (candidate_name.lower() == candidate.address.lower() or
                (sys.platform == "win32" and getattr(candidate, "details", True) is None)):
            candidate_name = ""  # A cached requester is not an observed GAP name.
        if RECOVERY_NAME_PATTERN.fullmatch(candidate_name):
            last_phase, last_error = "role", "Recovery still advertised"
            continue

        app_found = False
        try:
            # The first post-DFU connection already crosses a service layout;
            # do not spend it on the Recovery's cached characteristic table.
            options = {"fresh_services": True} if sys.platform == "win32" else {}
            attempts += 1
            last_phase = "connection/service discovery"
            async with asyncio.timeout(max(0.01, deadline - time.monotonic())):
                async with connected_client(candidate, min(connect_timeout, 10.0, max(0.01, deadline - time.monotonic())), **options) as client:
                    if BUTTONLESS_UUID not in characteristic_uuids(client):
                        last_phase, last_error = "role", "App channels not available"
                        await asyncio.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
                        continue
                    if GAP_NAME_UUID in characteristic_uuids(client):
                        candidate_name = bytes(await client.read_gatt_char(GAP_NAME_UUID)).decode("utf-8").rstrip("\0")
                    if candidate_name and (RECOVERY_NAME_PATTERN.fullmatch(candidate_name) or
                            (name and not candidate_name.lower().startswith(name.lower()))):
                        raise ValueError("Returned device GAP name differs from the expected App")
                    last_phase = "App protocol handshake"
                    async with ota_application_requests(client) as request:
                        app_found = True
                        last_phase = "App authorization/attestation"
                        if expected_firmware is not None:
                            await authorize_ota_application(request, password_data_dir)
                        info = await request(CommandV2.GET_DEVICE_INFO)
                        if (len(info) < 23 or info[0] != 1 or
                                23 + sum(info[1:4]) > len(info)):
                            raise RuntimeError("OTA application returned invalid V2 identity information")
                        size, checksum = struct.unpack_from("<II", info, 15)
                        # Current HW56/N3V1 PriceETag uses a 160-KiB App window.
                        # Do not infer that window from a shared NRF GAP name or
                        # submitted size: other/missing families keep the old cap.
                        family = info[23:23 + info[1]]
                        app_limit = 163840 if family == b"nrf52832_priceetag" else 131072
                        if not 0 < size <= app_limit:
                            raise RuntimeError("Returned Nordic application has invalid image extent")
                        if expected_firmware is not None and (size != len(expected_firmware) or checksum != crc32(expected_firmware)):
                            raise RuntimeError(
                                f"OTA application attestation failed: app_bytes={size}, CRC32=0x{checksum:08X}"
                            )
        except asyncio.CancelledError:
            # WinRT can cancel its service-discovery future while this task is
            # still live during the Recovery -> App cache transition. Retry
            # only that pre-protocol stage within the existing deadline. A
            # caller cancellation or any in-App AUTH/attestation cancellation
            # must still propagate; never restart the firmware transfer.
            task = asyncio.current_task()
            if app_found or task is None or task.cancelling():
                raise
            last_error = "native pre-App future cancelled"
            await asyncio.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
            continue
        except Exception as exc:
            # Never repeatedly retry a password refusal or accept an older
            # running image. Only cached/not-yet-connectable adverts may retry.
            if app_found or isinstance(exc, (ValueError, InkTagV2StatusError)):
                raise
            # Windows may return a cached advertisement while the address is
            # still moving from Bootloader back to application. Keep polling.
            last_error = type(exc).__name__
            await asyncio.sleep(min(0.25, max(0.0, deadline - time.monotonic())))
            continue

        elapsed = time.monotonic() - started
        print(f"\nApplication BLE OK: {candidate_name or '(unnamed)'} [{candidate.address}] after {elapsed:.1f}s")
        print(f"ATTESTED|app_bytes={size}|crc32=0x{checksum:08X}")
        return {"applicationName": candidate_name, "appBytes": size, "checksum": checksum}

    print()
    raise RuntimeError(
        f"OTA payload completed but application BLE did not recover within {timeout:.0f}s: {label}; "
        f"stage={last_phase}, attempts={attempts}, last={last_error}; firmware was not resent"
    )


class SecureDfu:
    def __init__(self, client: BleakClient, packet_size: int | None = None, prn: int = DEFAULT_PRN):
        self.client = client
        self.packet_size_limit = packet_size
        self.packet_size = 20
        self.prn = prn
        self.responses: asyncio.Queue[bytes] = asyncio.Queue()

    def _notification(self, _sender, data: bytearray) -> None:
        self.responses.put_nowait(bytes(data))

    async def start(self) -> None:
        await self.client.start_notify(DFU_CONTROL_UUID, self._notification)
        # Retain the four-packet receipt window; use its first native ACK
        # before reading MTU limits, without introducing a negotiation sleep.
        await self.request(OP_SET_PRN, struct.pack("<H", self.prn))
        # The packet characteristic and actual ATT MTU bound an automatic
        # write size. Unknown/stale native capabilities keep the old 20 B;
        # a command-line limit cannot override the negotiated maximum.
        size = 20
        getter = getattr(getattr(self.client, "services", None), "get_characteristic", None)
        packet = getter(DFU_PACKET_UUID) if callable(getter) else None
        native_limit = getattr(packet, "max_write_without_response_size", 20)
        mtu = getattr(self.client, "mtu_size", 23)
        if type(native_limit) is int and type(mtu) is int and native_limit >= 20 and mtu >= 23:
            size = min(native_limit, mtu - 3, 244)
        if self.packet_size_limit is not None:
            size = min(size, self.packet_size_limit)
        self.packet_size = max(20, size - size % 4)
        print(f"Secure DFU payload {self.packet_size} B; CRC receipt every {self.prn} packets")

    async def stop(self) -> None:
        if self.client.is_connected:
            try:
                await self.client.stop_notify(DFU_CONTROL_UUID)
            except Exception:
                pass

    async def request(self, opcode: int, payload: bytes = b"", timeout: float = 15.0) -> bytes:
        try:
            await self.client.write_gatt_char(DFU_CONTROL_UUID, bytes([opcode]) + payload, response=True)
        except Exception as exc:
            exc.add_note(f"Secure DFU opcode 0x{opcode:02X}, stage=control-write; do not infer acceptance")
            raise
        return await self.wait_response(opcode, timeout)

    async def wait_response(self, opcode: int, timeout: float = 15.0) -> bytes:
        while True:
            try:
                response = await asyncio.wait_for(self.responses.get(), timeout=timeout)
            except TimeoutError as exc:
                raise DfuResponseTimeout(
                    f"Secure DFU opcode 0x{opcode:02X} response timed out after {timeout:g}s; result unknown"
                ) from exc
            if len(response) < 3 or response[0] != OP_RESPONSE:
                continue
            if response[1] != opcode:
                continue
            if response[2] != RESULT_SUCCESS:
                extended = response[3] if len(response) > 3 else None
                suffix = f", extended=0x{extended:02X}" if extended is not None else ""
                raise RuntimeError(f"Secure DFU opcode 0x{opcode:02X} failed: result=0x{response[2]:02X}{suffix}")
            return response[3:]

    async def select(self, object_type: int) -> tuple[int, int, int]:
        response = await self.request(OP_SELECT, bytes([object_type]))
        if len(response) != 12:
            raise RuntimeError(f"Malformed Secure DFU SELECT response ({len(response)} bytes)")
        return struct.unpack("<III", response)

    async def calculate_crc(self) -> tuple[int, int]:
        response = await self.request(OP_CALCULATE_CRC)
        if len(response) != 8:
            raise RuntimeError(f"Malformed Secure DFU CRC response ({len(response)} bytes)")
        return struct.unpack("<II", response)

    async def write_packets(
        self,
        data: bytes,
        reference: bytes | None = None,
        base_offset: int = 0,
    ) -> None:
        reference = data if reference is None else reference
        packet_count = 0
        for offset in range(0, len(data), self.packet_size):
            packet = data[offset : offset + self.packet_size]
            await self.client.write_gatt_char(DFU_PACKET_UUID, packet, response=False)
            packet_count += 1
            if self.prn and packet_count % self.prn == 0:
                payload = await self.wait_response(OP_CALCULATE_CRC)
                if len(payload) != 8:
                    raise RuntimeError(f"Malformed Secure DFU PRN response ({len(payload)} bytes)")
                remote_offset, remote_crc = struct.unpack("<II", payload)
                expected_offset = base_offset + offset + len(packet)
                expected_crc = crc32(reference[:expected_offset])
                if remote_offset != expected_offset or remote_crc != expected_crc:
                    raise RuntimeError(
                        f"Secure DFU PRN verification failed at {expected_offset}: "
                        f"device offset={remote_offset}, device CRC=0x{remote_crc:08X}"
                    )

    async def send_command(self, init_packet: bytes) -> None:
        max_size, offset, remote_crc = await self.select(OBJECT_COMMAND)
        if offset == len(init_packet) and remote_crc == crc32(init_packet):
            print("Init packet already present; executing it.")
            await self.request(OP_EXECUTE, timeout=60.0)
            return
        if len(init_packet) > max_size:
            raise RuntimeError(f"Init packet is {len(init_packet)} bytes; device limit is {max_size}")
        # Native command CREATE replaces incomplete/mismatched init state. ABORT
        # is a guarded exit, not an OTA cleanup operation; never use it here.
        await self.request(OP_CREATE, bytes([OBJECT_COMMAND]) + struct.pack("<I", len(init_packet)))
        await self.write_packets(init_packet, init_packet, 0)
        checked_offset, checked_crc = await self.calculate_crc()
        if checked_offset != len(init_packet) or checked_crc != crc32(init_packet):
            raise RuntimeError("Init packet CRC verification failed")
        # Small Nordic parts may need more than 15 s for signed-init ECDSA.
        # Only init EXECUTE gets this budget; ordinary/data requests stay bounded.
        await self.request(OP_EXECUTE, timeout=60.0)

    async def prepare_firmware(self, firmware: bytes) -> tuple[int, int]:
        max_size, offset, remote_crc = await self.select(OBJECT_DATA)
        if not max_size:
            raise RuntimeError("Secure DFU returned a zero data-object size")
        if offset > len(firmware):
            raise RuntimeError(f"Remote DFU offset {offset} exceeds this firmware image")
        committed_offset = offset - (offset % max_size)
        crc_matches = remote_crc == crc32(firmware[:offset])
        if not crc_matches and offset == committed_offset and offset:
            raise RuntimeError("Remote DFU resume state belongs to a different firmware image")
        if offset != committed_offset:
            print(f"Discarding incomplete data object at byte {offset}; resuming from {committed_offset}.")
        elif offset:
            print(f"Resuming at byte {offset}.")
        return max_size, committed_offset

    async def send_firmware(
        self,
        firmware: bytes,
        state: tuple[int, int],
    ) -> None:
        max_size, offset = state
        started = time.monotonic()
        progress(offset, len(firmware), started)
        while offset < len(firmware):
            object_size = min(max_size, len(firmware) - offset)
            chunk = firmware[offset : offset + object_size]
            next_offset = offset + len(chunk)
            last_error: Exception | None = None
            for attempt in range(1, MAX_OBJECT_ATTEMPTS + 1):
                try:
                    # CREATE rewinds an unfinished object to the last executed
                    # boundary, so a dropped ATT packet cannot poison retries.
                    await self.request(OP_CREATE, bytes([OBJECT_DATA]) + struct.pack("<I", object_size))
                    await self.write_packets(chunk, firmware, offset)
                    checked_offset, checked_crc = await self.calculate_crc()
                    if checked_offset != next_offset or checked_crc != crc32(firmware[:next_offset]):
                        raise RuntimeError(
                            f"Firmware CRC verification failed at {next_offset}: "
                            f"device offset={checked_offset}, device CRC=0x{checked_crc:08X}"
                        )
                    last_error = None
                    break
                except Exception as exc:
                    last_error = exc
                    if attempt < MAX_OBJECT_ATTEMPTS:
                        print(f"Retrying data object {offset}-{next_offset} ({attempt}/{MAX_OBJECT_ATTEMPTS}): {exc}")
                        await asyncio.sleep(0.2)
            if last_error is not None:
                raise RuntimeError(
                    f"Data object {offset}-{next_offset} failed {MAX_OBJECT_ATTEMPTS} times: {last_error}"
                )
            try:
                await self.request(OP_EXECUTE, timeout=30.0)
            except DfuResponseTimeout as exc:
                if next_offset != len(firmware):
                    raise
                raise FinalExecuteAckUnknown(
                    f"Final data EXECUTE 0x04 ACK unknown after verified bytes {next_offset}/{len(firmware)}: "
                    f"{type(exc).__name__}: {exc}; do not replay"
                ) from exc
            offset = next_offset
            progress(offset, len(firmware), started)


async def resume_recovery_transport(client: BleakClient, timeout: float = 10.0) -> None:
    """Capability query, then exactly one guarded ABORT; never legacy fallback."""
    dfu = SecureDfu(client)
    await client.start_notify(DFU_CONTROL_UUID, dfu._notification)
    try:
        if await dfu.request(0x00, timeout=timeout) != b"\x81":
            raise RuntimeError("Installed Nordic Recovery does not support guarded safe resume")
        while not dfu.responses.empty(): dfu.responses.get_nowait()
        try:
            payload = await dfu.request(OP_ABORT, timeout=timeout)
        except Exception as exc:
            if str(exc) == "Secure DFU opcode 0x0C failed: result=0x08":
                raise RuntimeError("Nordic Recovery rejected safe resume: operation not permitted") from exc
            raise RuntimeError("Nordic exit result unknown; do not replay or force reboot") from exc
        if payload:
            raise RuntimeError("Nordic exit result unknown; malformed acknowledgment, do not replay")
    finally:
        await dfu.stop()


async def resume_safe_application(name: str | None, address: str | None,
                                  scan_timeout: float, connect_timeout: float,
                                  reconnect_timeout: float) -> dict:
    device, advertised = await discover_device(name, address, scan_timeout, bootloader=True,
                                               require_unique=True, return_name=True)
    async with connected_client(device, connect_timeout) as client:
        if (DFU_SERVICE_UUID not in {service.uuid.lower() for service in client.services}
                or not {DFU_CONTROL_UUID, DFU_PACKET_UUID} <= characteristic_uuids(client)):
            raise RuntimeError("Selected device does not expose Nordic Recovery channels")
        recovery_phase('request')
        await resume_recovery_transport(client)
    recovery_phase('waiting-app')
    info = await wait_for_application(None, device.address, reconnect_timeout, connect_timeout, None)
    return {"ok": True, "action": "safe-resume-app", "platform": "nordic", "address": device.address,
            "applicationVerified": True, "applicationImage": info, "originalImageCompared": False,
            "guardedByRecovery": True, "recoveryName": advertised}


async def enter_buttonless_dfu(client: BleakClient, expected_name: str) -> str:
    response_event = asyncio.Event()
    result = bytearray()

    def notification(_sender, data: bytearray) -> None:
        result[:] = data
        response_event.set()

    await client.start_notify(BUTTONLESS_UUID, notification)

    async def request(opcode: int, payload: bytes = b"") -> None:
        response_event.clear()
        result.clear()
        await client.write_gatt_char(BUTTONLESS_UUID, bytes((opcode,)) + payload, response=True)
        try:
            await asyncio.wait_for(response_event.wait(), timeout=8.0)
        except asyncio.TimeoutError:
            if client.is_connected:
                raise RuntimeError(f"No response to buttonless DFU opcode 0x{opcode:02X}")
            return
        if len(result) < 3 or result[0] != BUTTONLESS_RESPONSE or result[1] != opcode:
            raise RuntimeError(f"Malformed buttonless DFU response: {result.hex()}")
        if result[2] != BUTTONLESS_SUCCESS:
            raise RuntimeError(f"Buttonless DFU opcode 0x{opcode:02X} rejected: {result.hex()}")

    print("Requesting buttonless DFU mode...")
    await request(BUTTONLESS_ENTER)
    for _ in range(30):
        if not client.is_connected:
            break
        await asyncio.sleep(0.1)
    return expected_name


async def run(args) -> None:
    package = load_package(args.package)
    print(f"Package: {args.package}")
    print(f"  Init packet: {len(package.init_packet)} bytes")
    print(f"  Application: {len(package.firmware)} bytes, CRC32=0x{crc32(package.firmware):08X}")

    application_name = None
    application_address = None
    packet_size_limit = args.packet_size
    if packet_size_limit is None:
        packet_size_limit = 20  # Larger-packet pilot requires the exact App family.
    final_ack_unknown = None
    device = await discover_device(args.name, args.address, args.scan_timeout)
    async with connected_client(device, args.connect_timeout) as client:
        uuids = characteristic_uuids(client)
        observed_name = device.name or ""
        address_placeholder = observed_name.lower() == device.address.lower() or (
            sys.platform == "win32" and args.address and getattr(device, "details", True) is None
        )
        if address_placeholder:
            # The shared WinRT address selector deliberately uses a placeholder,
            # never an invented advertisement. Prefer the actual read-only GAP
            # name when present before deriving the Recovery identity.
            observed_name = ""
            if GAP_NAME_UUID in uuids:
                observed_name = bytes(await client.read_gatt_char(GAP_NAME_UUID)).decode("utf-8").rstrip("\0")
            if args.name and (not observed_name or not observed_name.lower().startswith(args.name.lower())):
                raise RuntimeError("Selected exact address reports a different application name")
        if DFU_CONTROL_UUID in uuids and DFU_PACKET_UUID in uuids:
            bootloader_device = device
            bootloader_name = observed_name
            if RECOVERY_NAME_PATTERN.fullmatch(bootloader_name) is None:
                raise RuntimeError(
                    "Nordic Recovery must advertise as IKT_NRF_<ID4>_OTA"
                )
            application_name = application_name_from_bootloader(bootloader_name, args.name)
        elif BUTTONLESS_UUID in uuids:
            application_name = observed_name or args.name or "IKT_NRF"
            application_address = device.address
            candidate_name = recovery_name_from_identity(application_name, application_address)
            async with ota_application_requests(client, disconnect_on_exit=True) as request:
                await authorize_ota_application(request, getattr(args, "password_data_dir", None))
                if args.packet_size is None:
                    info = await request(CommandV2.GET_DEVICE_INFO)
                    if len(info) >= 23 and info[0] == 1 and 23 + sum(info[1:4]) <= len(info):
                        family = info[23:23 + info[1]]
                        if family == b"nrf52832_priceetag":
                            packet_size_limit = None
                        elif family == b"aesl22_nrf":
                            # Exact nRF52811 pilot passed 200 B / PRN4. Keep
                            # its cap below Nordic's separately qualified244.
                            packet_size_limit = 200
                # Stopping COMMAND notifications before this write would revoke
                # this connection's password authorization on qualified Apps.
                bootloader_name = await enter_buttonless_dfu(client, candidate_name)
            bootloader_device = None
        else:
            raise RuntimeError("Device exposes neither InkTag buttonless DFU nor Nordic Secure DFU")

    if bootloader_device is None:
        await asyncio.sleep(1.0)
        bootloader_device = await discover_device(bootloader_name, None, args.scan_timeout, bootloader=True)

    print(f"Connecting to {bootloader_name}...")
    async with connected_client(bootloader_device, args.connect_timeout) as client:
        uuids = characteristic_uuids(client)
        if DFU_CONTROL_UUID not in uuids or DFU_PACKET_UUID not in uuids:
            raise RuntimeError("Selected device does not expose Nordic Secure DFU control and packet characteristics")
        dfu = SecureDfu(client, packet_size=packet_size_limit)
        await dfu.start()
        try:
            print("Sending signed init packet...")
            await dfu.send_command(package.init_packet)
            firmware_state = await dfu.prepare_firmware(package.firmware)
            print("Sending application...")
            try:
                await dfu.send_firmware(package.firmware, firmware_state)
            except FinalExecuteAckUnknown as exc:
                final_ack_unknown = exc
        finally:
            await dfu.stop()
    if final_ack_unknown is not None:
        if args.skip_reconnect_check or not application_address or args.interrupt_after_percent is not None:
            raise final_ack_unknown
        print(f"{final_ack_unknown}; checking the same App address and submitted image, not resending.", file=sys.stderr)
    else:
        print("OTA complete. The device is validating and restarting the application.")
    if not args.skip_reconnect_check and (application_address or application_name):
        try:
            await wait_for_application(
                application_name, application_address, args.reconnect_timeout, args.connect_timeout,
                package.firmware, getattr(args, "password_data_dir", None),
            )
        except Exception as exc:
            if final_ack_unknown is not None:
                exc.add_note(str(final_ack_unknown))
            raise
    elif not args.skip_reconnect_check:
        print("Application reconnect check skipped: OTA was started directly in Bootloader mode.")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="signed application-only Nordic DFU ZIP")
    parser.add_argument("--name", help="application BLE name/prefix, for example IKT_NRF_1A2B")
    parser.add_argument("--address", help="application or bootloader BLE address")
    parser.add_argument("--scan-timeout", type=float, default=30.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--password-data-dir", type=Path, help="absolute shared password credential directory")
    parser.add_argument("--reconnect-timeout", type=float, default=45.0)
    parser.add_argument("--skip-reconnect-check", action="store_true")
    parser.add_argument("--packet-size", type=int, default=None, choices=range(20, 245))
    return parser.parse_args()


def main() -> int:
    try:
        asyncio.run(run(parse_args()))
        return 0
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"\nOTA failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        for note in getattr(exc, "__notes__", ()):
            print(note, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
