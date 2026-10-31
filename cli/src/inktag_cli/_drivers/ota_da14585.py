#!/usr/bin/env python3
"""Canonical CLI driver for DA14585 SDK6 pQ SUOTA transfer."""

from __future__ import annotations
from inktag_cli.generated_release_v1 import encode_request_flags

import argparse
import asyncio
import functools
import re
import struct
import sys
import time
import zlib
from pathlib import Path

from bleak import BleakClient, BleakScanner
from inktag_cli.connection_status import report_ble_connection
from inktag_cli.recovery_feedback import driver_print as print, recovery_phase
from bleak.exc import BleakError


SERVICE_UUID = "0000fef5-0000-1000-8000-00805f9b34fb"
APP_SERVICE_UUID = "62750001-d828-918d-fb46-b6c11c675aec"
APP_COMMAND_UUID = "62750002-d828-918d-fb46-b6c11c675aec"
V2_SYSTEM_ACTION = 0x0E
V2_GET_DEVICE_INFO = 0x03
V2_OTA_RECOVERY = 0x04
V2_STATUS_OK = 0x00
V2_TRANSACTION_ID = 0x01
OTA_RECOVERY_COMMAND = bytes((
    V2_SYSTEM_ACTION,
    V2_TRANSACTION_ID,
    encode_request_flags(0),
    V2_OTA_RECOVERY,
))
MEM_DEV_UUID = "8082caa8-41a6-4021-91c6-56f9b954cc34"
GPIO_MAP_UUID = "724249f0-5ec3-4b5f-8804-42345af08651"
MEM_INFO_UUID = "6c53db25-47a1-45fe-a022-7c92fb334fd4"
PATCH_LEN_UUID = "9d84b9a3-000c-49d8-9183-855b673fda31"
PATCH_DATA_UUID = "457871e8-d516-4ca1-9116-57d0b17b9cb2"
STATUS_UUID = "5f78df94-798c-46f5-990a-b3eb6a065c88"
VERSION_UUID = "64b4e8b5-0de5-401b-a21d-acc8db3b913a"
PD_SIZE_UUID = "42c3dfdd-77be-4d9c-8454-8f875267fb3b"

SUOTA_IMAGE_SPI_FLASH = 0x13
SUOTA_SERVICE_EXIT = 0xFF
SUOTA_IMAGE_END = 0xFE
SUOTA_REBOOT = 0xFD
SUOTA_IMAGE_STARTED = 0x10
SUOTA_COMPLETE_OK = 0x02
SUOTA_SERVICE_EXITED = 0x03
SPI_GPIO_MAP = 0x05060300  # MISO=P0_5, MOSI=P0_6, CS=P0_3, SCK=P0_0
MAX_IMAGE_SIZE = 0x17600 + 64
STATUS_NAMES = {
    0x01: "service started",
    0x02: "complete",
    0x03: "service exited",
    0x04: "XOR checksum error",
    0x05: "patch length error",
    0x06: "external memory error",
    0x07: "internal memory error",
    0x08: "invalid memory type",
    0x09: "application error",
    0x10: "image transfer started",
    0x11: "invalid image bank",
    0x12: "invalid image header",
    0x13: "invalid image size",
    0x14: "invalid product header",
    0x15: "same image version",
    0x16: "external memory read error",
    0x17: "invalid image version",
    0x18: "image version rollback rejected",
    0x19: "SUOTA session locked until reboot",
}


async def release_client(client: BleakClient, timeout: float = 5.0) -> None:
    """Bound cleanup across the intentional application/Recovery role reset."""
    from inktag_cli.transport import release_bleak_client

    try:
        await asyncio.wait_for(release_bleak_client(client), timeout=timeout)
    except (AssertionError, BleakError, OSError, RuntimeError, TimeoutError):
        # A role reset drops the peripheral while WinRT can still expose a
        # connected GATT session. The image commit is attested only by the
        # fresh application connection below, so cleanup failure is transient.
        pass


def recovery_name_from_identity(address: str, application_name: str) -> str | None:
    """Return the immutable DA Recovery name, never a saved application alias."""
    address_match = re.search(
        r"(?:^|[:-])([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})$", address
    )
    if address_match:
        suffix = "".join(address_match.groups()).upper()
        return f"IKT_DA_{suffix}_OTA"
    name_match = re.fullmatch(
        r"(?:IKT|InkTag)_DA_([0-9A-Fa-f]{4})(?:_OTA)?", application_name
    )
    if name_match:
        return f"IKT_DA_{name_match.group(1).upper()}_OTA"
    return None


def recovery_advertisement_matches(
    expected_name: str | None,
    candidate_name: str,
    application_address: str | None,
    candidate_address: str,
) -> bool:
    """Match this device's fixed Recovery, including its installed legacy name."""
    same_address = bool(
        application_address
        and candidate_address.upper() == application_address.upper()
    )
    if expected_name is None:
        return same_address
    # Some installed fixed Recoveries omit the scan-response name. An exact
    # address match is still unambiguous; the caller checks SUOTA GATT before
    # accepting the role. Never use this fallback for a different address.
    if not candidate_name:
        return same_address
    allowed_names = {expected_name}
    if expected_name.startswith("IKT_DA_"):
        allowed_names.add("InkTag_DA_" + expected_name[len("IKT_DA_"):])
    if candidate_name not in allowed_names:
        return False
    # A canonical BLE address identifies the target independently of its
    # name; reject a nearby device spoofing the same four-character suffix.
    if application_address and re.fullmatch(
        r"[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}", application_address
    ):
        return same_address
    return True


def load_image(path: Path) -> tuple[bytes, str, int, int]:
    image = path.read_bytes()
    from inktag_cli.signed_preflight import verify_dialog
    verify_dialog(image)
    if len(image) < 64 or len(image) > MAX_IMAGE_SIZE:
        raise ValueError(f"SUOTA image size is outside 64..{MAX_IMAGE_SIZE} bytes")
    if image[:2] != b"pQ" or image[2] != 0xAA:
        raise ValueError("not an SDK6 pQ SUOTA image")
    code_size, expected_crc = struct.unpack_from("<II", image, 4)
    if code_size + 64 != len(image):
        raise ValueError(f"pQ code size mismatch: {code_size} + 64 != {len(image)}")
    if zlib.crc32(image[64:]) != expected_crc:
        raise ValueError("pQ application CRC32 mismatch")
    version = image[12:28].split(b"\0", 1)[0].decode("ascii", errors="strict")
    checksum = functools.reduce(lambda value, byte: value ^ byte, image, 0)
    return image + bytes((checksum,)), version, code_size, expected_crc


async def discover(name: str | None, address: str | None, timeout: float, require_unique: bool = False):
    if address:
        from inktag_cli.transport import _collect_scan_matches

        # Address-only fast selection may return a synthetic WinRT device or
        # an ADV before its scan response. Reuse the bounded shared collector;
        # never invent a Recovery identity from the address or requested name.
        # Use role discovery's one empty-watcher restart inside the same budget,
        # retaining the full window once any report arrives (including late names).
        matches = await _collect_scan_matches(
            timeout, address=address, wait_for_full_timeout=True,
        )
        if len(matches) != 1:
            raise RuntimeError("no unique DA14585 advertised name was found at the selected address")
        device, _advertisement, advertised_name = matches[0]
        if not advertised_name or advertised_name.lower() == address.lower():
            raise RuntimeError("DA14585 advertised name is unavailable at the selected address")
        return device, advertised_name
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    candidates = []
    for device, advertisement in devices.values():
        advertised_name = advertisement.local_name or device.name or ""
        services = {value.lower() for value in advertisement.service_uuids or []}
        accepted_names = (
            ("IKT_DA_", "INKTAG_DA_")
            if name and name.upper() == "IKT_DA_" else (name.upper(),) if name else ()
        )
        if accepted_names and not advertised_name.upper().startswith(accepted_names):
            continue
        if SERVICE_UUID in services or advertised_name.startswith(("IKT_DA_", "InkTag_DA_")):
            candidates.append((device, advertised_name))
    if not candidates:
        raise RuntimeError("no DA14585 InkTag device with SUOTA was found")
    if len(candidates) > 1 and (not name or require_unique):
        raise RuntimeError("multiple DA14585 devices found; pass --name or --address")
    device, advertised_name = candidates[0]
    return device, advertised_name


class SuotaTransport:
    def __init__(self, client: BleakClient) -> None:
        self.client = client
        self.statuses: asyncio.Queue[int] = asyncio.Queue()

    def on_status(self, _sender, data: bytearray) -> None:
        if data:
            status = int(data[0])
            print(f"STATUS|0x{status:02X}|{STATUS_NAMES.get(status, 'unknown')}")
            self.statuses.put_nowait(status)

    async def open(self) -> None:
        await self.client.start_notify(STATUS_UUID, self.on_status)

    async def close(self) -> None:
        if self.client.is_connected:
            try:
                await self.client.stop_notify(STATUS_UUID)
            except Exception:
                pass

    async def wait_status(self, expected: int, action: str, timeout: float = 30.0) -> None:
        while True:
            status = await asyncio.wait_for(self.statuses.get(), timeout)
            if status == expected:
                return
            if status not in (0x01,):
                detail = STATUS_NAMES.get(status, "unknown error")
                raise RuntimeError(f"{action} failed with SUOTA status 0x{status:02X}: {detail}")

    async def command(self, command: int, argument: int = 0) -> None:
        value = ((command & 0xFF) << 24) | (argument & 0x00FFFFFF)
        await self.client.write_gatt_char(MEM_DEV_UUID, struct.pack("<I", value), response=True)

    def reject_queued_failure(self, action: str) -> None:
        # START/COMPLETE can be a stale block acknowledgment, never final proof.
        while not self.statuses.empty():
            status = self.statuses.get_nowait()
            if status not in (0x01, SUOTA_COMPLETE_OK):
                detail = STATUS_NAMES.get(status, "unknown error")
                raise RuntimeError(f"{action} failed with SUOTA status 0x{status:02X}: {detail}")


async def resume_recovery_transport(client: BleakClient, timeout: float = 10.0) -> None:
    """Single guarded FC request. A fresh native ACK is acceptance, not return."""
    from inktag_cli.ota_rejection import describe_ota_rejection

    proof = bytes(await client.read_gatt_char(VERSION_UUID))
    if proof != b"\x0dIK\x01\x01":
        raise RuntimeError("installed DA Recovery does not support guarded safe resume")
    replies: asyncio.Queue[bytes] = asyncio.Queue()
    def on_status(_sender, data):
        replies.put_nowait(bytes(data))
    await client.start_notify(STATUS_UUID, on_status)
    try:
        # Attribute read / already queued COMPLETE is never this operation's ACK.
        await client.read_gatt_char(STATUS_UUID)
        while not replies.empty(): replies.get_nowait()
        try:
            await client.write_gatt_char(MEM_DEV_UUID, b"\x00\x00\x00\xfc", response=True)
            reply = await asyncio.wait_for(replies.get(), timeout)
        except Exception as exc:
            raise RuntimeError("DA Recovery exit result unknown; do not replay or force reboot") from exc
        if reply == b"\x02": return
        if reply and reply[0] == 0x06:
            raise RuntimeError(describe_ota_rejection("SAFE_RESUME_APP", reply, error_status=0x06))
        if reply in (b"\x19", b"\x08"):
            raise RuntimeError(f"DA Recovery rejected safe resume: SUOTA status 0x{reply[0]:02X}")
        raise RuntimeError("DA Recovery exit result unknown; malformed acknowledgment, do not replay")
    finally:
        if client.is_connected:
            try: await client.stop_notify(STATUS_UUID)
            except Exception: pass


async def resume_safe_application(name: str | None, address: str | None,
                                  scan_timeout: float, connect_timeout: float,
                                  reconnect_timeout: float) -> dict:
    started = time.monotonic()
    def phase(stage: str) -> None:
        print(f"DA_RESUME|stage={stage}|elapsed={time.monotonic() - started:.3f}s",
              file=sys.stderr, flush=True)
    phase("recovery-discovery:start")
    try:
        device, advertised = await discover(name, address, scan_timeout, True)
    except Exception as exc:
        phase(f"recovery-discovery:failed:{type(exc).__name__}: {exc}")
        raise
    phase("recovery-discovery:done")
    if not re.fullmatch(r"(?:IKT|InkTag)_DA_[0-9A-Fa-f]{4}_OTA", advertised):
        raise RuntimeError("selected device is not fixed DA Recovery")
    resolved = device.address
    options = {"timeout": connect_timeout}
    if sys.platform == "win32": options["winrt"] = {"use_cached_services": False}
    client = BleakClient(device, **options)
    try:
        phase("recovery-connect:start")
        await client.connect()
        report_ble_connection(device)
        phase("recovery-connect:done")
        if SERVICE_UUID not in {str(service.uuid).lower() for service in client.services}:
            raise RuntimeError("selected device does not expose DA Recovery SUOTA")
        phase("safe-resume-request:start")
        recovery_phase('request')
        await resume_recovery_transport(client)
        phase("safe-resume-request:acknowledged")
    except Exception as exc:
        phase(f"recovery-operation:failed:{type(exc).__name__}: {exc}")
        raise
    finally:
        # Native firmware does its full CRC/voltage/marker validation only after
        # this ordinary disconnect. Never resend FC on ACK loss or late refusal.
        phase("recovery-release:start")
        await release_client(client)
        phase("recovery-release:done")
    recovery_phase('waiting-app')
    info = await reconnect(advertised[:-4], resolved, reconnect_timeout, None, None)
    return {"ok": True, "action": "safe-resume-app", "platform": "dialog", "address": resolved,
            "applicationVerified": True, "applicationImage": info,
            "originalImageCompared": False, "guardedByRecovery": True}


async def reconnect(
    name: str,
    address: str | None,
    timeout: float,
    expected_size: int | None,
    expected_crc: int | None,
) -> dict:
    from inktag_cli.transport import release_bleak_client

    started = time.monotonic()
    deadline = started + timeout
    # Keep a portion of the same total budget for every provisional client's
    # cleanup, including a connect that failed before is_connected became true.
    work_deadline = deadline - min(2.0, timeout / 10.0)
    last_error: Exception | None = None
    last_stage = "settle"

    def phase(stage: str, outcome: str) -> None:
        print(f"DA_RECONNECT|stage={stage}|{outcome}"
              f"|elapsed={time.monotonic() - started:.3f}s"
              f"|remaining={max(0.0, deadline - time.monotonic()):.3f}s",
              file=sys.stderr, flush=True)

    async def bounded(stage, operation, limit=None, *, cleanup=False):
        nonlocal last_stage
        if not cleanup and stage != "retry-wait":
            last_stage = stage
        remaining = (deadline if cleanup else work_deadline) - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"{stage}: reconnect budget exhausted")
        phase(stage, "start")
        try:
            result = await asyncio.wait_for(operation(), min(remaining, limit) if limit else remaining)
        except Exception as exc:
            phase(stage, f"failed={type(exc).__name__}: {exc}")
            raise
        phase(stage, "done")
        return result

    try:
        await bounded("settle", lambda: asyncio.sleep(min(2.0, max(0.0, work_deadline - time.monotonic()))))
    except TimeoutError as exc:
        last_error = exc
    while time.monotonic() < work_deadline:
        try:
            scan_timeout = min(4.0, work_deadline - time.monotonic())
            devices = await bounded("app-discovery", lambda: BleakScanner.discover(
                timeout=scan_timeout, return_adv=True))
        except Exception as exc:
            last_error = exc
            if time.monotonic() >= work_deadline:
                break
            try:
                await bounded("retry-wait", lambda: asyncio.sleep(min(0.5, max(0.0, work_deadline - time.monotonic()))))
            except TimeoutError:
                break
            continue
        for device, advertisement in devices.values():
            advertised_name = advertisement.local_name or device.name or ""
            if not (
                device.address.upper() == address.upper() if address else advertised_name == name
            ):
                continue
            services = {value.lower() for value in getattr(advertisement, "service_uuids", None) or []}
            if advertised_name.casefold().endswith("_ota") or SERVICE_UUID in services:
                # The role can still be Recovery at the same immutable address.
                # Never spend the App GATT budget connecting to that role.
                last_stage = "app-role"
                last_error = RuntimeError("selected address is still advertising Recovery")
                phase(last_stage, f"skip=Recovery;name={advertised_name or '(unnamed)'}"
                      f";suota={int(SERVICE_UUID in services)}")
                continue
            remaining = work_deadline - time.monotonic()
            if remaining <= 0:
                break
            phase("app-role", f"candidate={advertised_name or '(unnamed)'}"
                  f";app-service={int(APP_SERVICE_UUID in services)}")
            options: dict = {"timeout": min(15.0, remaining)}
            if sys.platform == "win32":
                # Recovery and the application reuse one BLE identity but
                # expose different GATT tables. Keep the freshly scanned device
                # to avoid Bleak's implicit second scan, while bypassing WinRT's
                # Recovery service cache during live App-service discovery.
                options["winrt"] = {"use_cached_services": False}
                options["services"] = {APP_SERVICE_UUID}
            client = BleakClient(device, **options)
            try:
                await bounded("app-connect", client.connect, 15.0)
                report_ble_connection(device)
                services = {str(service.uuid).lower() for service in client.services}
                if APP_SERVICE_UUID not in services:
                    raise RuntimeError(
                        "upgraded application reconnected without the InkTag service"
                    )
                replies: asyncio.Queue[bytes] = asyncio.Queue()

                def on_reply(_sender, data: bytearray) -> None:
                    packet = bytes(data)
                    if len(packet) >= 3 and packet[0] == V2_GET_DEVICE_INFO:
                        replies.put_nowait(packet)

                await bounded("app-notify", lambda: client.start_notify(APP_COMMAND_UUID, on_reply))
                if sys.platform == "win32":
                    await bounded("notify-settle", lambda: asyncio.sleep(0.5))
                await bounded("device-info-write", lambda: client.write_gatt_char(
                    APP_COMMAND_UUID,
                    bytes((V2_GET_DEVICE_INFO, V2_TRANSACTION_ID, encode_request_flags(0))),
                    response=True,
                ))
                reply = await bounded("device-info-reply", replies.get, 5.0)
                if (
                    reply[1] != V2_TRANSACTION_ID
                    or reply[2] != V2_STATUS_OK
                    or len(reply) < 26
                    or reply[3] != 1
                ):
                    raise RuntimeError(
                        "upgraded application returned invalid V2 device information"
                    )
                app_size, app_crc = struct.unpack_from("<II", reply, 18)
                if not 0 < app_size <= MAX_IMAGE_SIZE - 64:
                    raise RuntimeError("returned DA application has invalid image extent")
                if expected_size is not None and (app_size != expected_size or app_crc != expected_crc):
                    raise RuntimeError(
                        "upgraded application does not match the submitted image "
                        f"(size={app_size}, crc32=0x{app_crc:08X}; "
                        f"expected size={expected_size}, crc32=0x{expected_crc:08X})"
                    )
                print(f"RECONNECTED|{advertised_name}|{device.address}")
                print(f"ATTESTED|app_bytes={app_size}|crc32=0x{app_crc:08X}")
                return {"applicationName": advertised_name, "appBytes": app_size, "checksum": app_crc}
            except Exception as exc:
                last_error = exc
                phase(last_stage, f"rejected={type(exc).__name__}: {exc}")
            finally:
                try:
                    await bounded("app-release", lambda: release_bleak_client(client), 5.0, cleanup=True)
                except (AssertionError, BleakError, OSError, RuntimeError, TimeoutError) as exc:
                    phase("app-release", f"cleanup={type(exc).__name__}: {exc}")
        if time.monotonic() < work_deadline:
            try:
                await bounded("retry-wait", lambda: asyncio.sleep(min(0.5, max(0.0, work_deadline - time.monotonic()))))
            except TimeoutError:
                break
    detail = f"; last error={type(last_error).__name__}: {last_error}" if last_error else ""
    raise TimeoutError(
        f"upgraded DA14585 application not confirmed within {timeout:g}s reconnect budget"
        f"; stage={last_stage}; elapsed={time.monotonic() - started:.3f}s" + detail
    )


async def enter_recovery(
    device, address: str | None, application_name: str, args
):
    expected_name = recovery_name_from_identity(address or "", application_name)
    client = BleakClient(device, timeout=args.connect_timeout)
    owned_by_caller = False
    try:
        await client.connect()
        report_ble_connection(device)
        services = {str(service.uuid).lower() for service in client.services}
        if SERVICE_UUID in services:
            if not recovery_advertisement_matches(
                expected_name, application_name, address, getattr(device, "address", "")
            ):
                raise RuntimeError(
                    f"DA14585 Recovery identity mismatch: expected {expected_name}, "
                    f"found {application_name or '(unnamed)'}"
                )
            owned_by_caller = True
            return client
        if APP_SERVICE_UUID not in services:
            raise RuntimeError("device exposes neither the InkTag application nor DA14585 Recovery")

        print("Switching the application to fixed BLE Recovery...")
        replies: asyncio.Queue[bytes] = asyncio.Queue()

        def on_application_reply(_sender, data: bytearray) -> None:
            packet = bytes(data)
            if (
                len(packet) >= 3
                and packet[0] == V2_SYSTEM_ACTION
                and packet[1] == V2_TRANSACTION_ID
            ):
                replies.put_nowait(packet)

        await client.start_notify(APP_COMMAND_UUID, on_application_reply)
        await client.write_gatt_char(APP_COMMAND_UUID, OTA_RECOVERY_COMMAND, response=True)
        try:
            reply = await asyncio.wait_for(replies.get(), timeout=5.0)
        except TimeoutError as exc:
            raise RuntimeError("application did not acknowledge V2 OTA Recovery entry") from exc
        if reply[2] != V2_STATUS_OK:
            raise RuntimeError(f"application rejected V2 OTA Recovery entry: status 0x{reply[2]:02X}")
        for _ in range(30):
            if not client.is_connected:
                break
            await asyncio.sleep(0.1)
    finally:
        # A failed discovery can own native resources despite is_connected=False.
        # The already-verified Recovery return is the only ownership handoff.
        if not owned_by_caller:
            await release_client(client)

    deadline = time.monotonic() + args.recovery_timeout
    last_error = None
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(timeout=2.0, return_adv=True)
        for candidate, advertisement in devices.values():
            candidate_name = advertisement.local_name or candidate.name or ""
            if not recovery_advertisement_matches(
                expected_name, candidate_name, address, candidate.address
            ):
                continue
            recovery = BleakClient(candidate, timeout=args.connect_timeout)
            owned_by_caller = False
            try:
                await recovery.connect()
                report_ble_connection(candidate)
                recovery_services = {str(service.uuid).lower() for service in recovery.services}
                if SERVICE_UUID in recovery_services:
                    print(f"RECOVERY|{candidate_name}|{candidate.address}")
                    owned_by_caller = True
                    return recovery
            except Exception as exc:
                last_error = exc
            finally:
                if not owned_by_caller:
                    await release_client(recovery)
        await asyncio.sleep(0.3)
    raise TimeoutError(
        f"DA14585 fixed Recovery did not appear before timeout"
        + (f": {last_error}" if last_error else "")
    )


async def transfer(args) -> None:
    payload, version, expected_size, expected_crc = load_image(args.image)
    image_length = len(payload) - 1
    device, advertised_name = await discover(args.name, args.address, args.scan_timeout)
    address = getattr(device, "address", args.address)
    print(f"Found: {advertised_name} [{address}]")
    print(f"Image: {args.image}")
    print(f"  Version: {version}")
    print(f"  Image:   {image_length} / {MAX_IMAGE_SIZE} bytes (+1 XOR byte on wire)")

    started = time.monotonic()
    client = await enter_recovery(device, address, advertised_name, args)
    try:
        service_uuids = {str(service.uuid).lower() for service in client.services}
        if SERVICE_UUID not in service_uuids:
            raise RuntimeError("DA14585 SUOTA service FEF5 is missing")
        transport = SuotaTransport(client)
        await transport.open()
        session_started = False
        finalization_requested = False
        finalization_write_confirmed = False
        try:
            pd_size = 20
            try:
                receiver_version = bytes(await client.read_gatt_char(VERSION_UUID)).hex()
                pd_size = int.from_bytes(await client.read_gatt_char(PD_SIZE_UUID), "little")
                print(f"Receiver: SUOTA={receiver_version}, patch-data={pd_size} bytes")
            except Exception as exc:
                print(f"Receiver metadata unavailable: {type(exc).__name__}")

            receiver_chunk_size = min(max(pd_size, 1), 244)
            chunk_size = args.chunk_size or receiver_chunk_size
            if chunk_size > receiver_chunk_size:
                raise RuntimeError(
                    f"requested chunk size {chunk_size} exceeds receiver patch-data size {receiver_chunk_size}"
                )
            block_size = args.block_size or (512 // chunk_size) * chunk_size
            if block_size < 80:
                block_size = min(512, chunk_size * ((80 + chunk_size - 1) // chunk_size))
            print(f"Transfer: block={block_size} bytes, ATT payload={chunk_size} bytes")

            print("Starting SPI Flash transfer to the single application slot...")
            await transport.command(SUOTA_IMAGE_SPI_FLASH, 0)
            await transport.wait_status(SUOTA_IMAGE_STARTED, "start")
            session_started = True
            await client.write_gatt_char(GPIO_MAP_UUID, struct.pack("<I", SPI_GPIO_MAP), response=True)
            try:
                memory_info = int.from_bytes(await client.read_gatt_char(MEM_INFO_UUID), "little")
                print(f"Initial memory info: 0x{memory_info:08X}")
            except Exception:
                pass

            offset = 0
            while offset < len(payload):
                block = payload[offset : offset + block_size]
                await client.write_gatt_char(PATCH_LEN_UUID, struct.pack("<H", len(block)), response=True)
                for chunk_offset in range(0, len(block), chunk_size):
                    await client.write_gatt_char(
                        PATCH_DATA_UUID,
                        block[chunk_offset : chunk_offset + chunk_size],
                        response=False,
                    )
                    if args.packet_delay:
                        await asyncio.sleep(args.packet_delay)
                await transport.wait_status(SUOTA_COMPLETE_OK, f"block at offset {offset}", args.block_timeout)
                offset += len(block)
                speed = offset / max(time.monotonic() - started, 0.001) / 1024
                print(f"\rProgramming {offset * 100 / len(payload):5.1f}% ({speed:5.1f} KiB/s)", end="", flush=True)
            print()

            memory_info = int.from_bytes(await client.read_gatt_char(MEM_INFO_UUID), "little")
            print(f"Final memory info: 0x{memory_info:08X}")
            transport.reject_queued_failure("image finalize")
            # The fixed receiver acknowledges FE at ATT, then disconnects for
            # offline signature/journal finalization. No connected final02 is
            # promised. Once FE may have been sent, never send FE/FD/FF again.
            finalization_requested = True
            try:
                await transport.command(SUOTA_IMAGE_END)
                finalization_write_confirmed = True
                print("SUOTA end request accepted; disconnecting for finalization and App attestation...")
            except Exception as exc:
                print(f"SUOTA end result unknown ({type(exc).__name__}); no replay, checking returned App only...")
            transport.reject_queued_failure("image finalize")
        except BaseException:
            if session_started and not finalization_requested and client.is_connected:
                print("SUOTA failed; requesting service exit before disconnect...")
                try:
                    await transport.command(SUOTA_SERVICE_EXIT)
                    await transport.wait_status(SUOTA_SERVICE_EXITED, "service exit", 5.0)
                except Exception as recovery_error:
                    print(f"SUOTA service-exit recovery unavailable: {type(recovery_error).__name__}")
            raise
        finally:
            await transport.close()
    finally:
        await release_client(client)

    transport.reject_queued_failure("image finalize")
    if not args.skip_reconnect_check:
        await reconnect(
            advertised_name,
            address,
            args.reconnect_timeout,
            expected_size,
            expected_crc,
        )
        print("DA14585 SUOTA complete.")
    else:
        if not finalization_write_confirmed:
            raise RuntimeError("DA14585 SUOTA end result unknown; application attestation is required, do not replay")
        print(
            "DA14585 SUOTA end requested, application unverified; reconnect and attest application "
            "size/CRC32 before treating the update as complete."
        )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--name", default="IKT_DA_")
    parser.add_argument("--address")
    parser.add_argument("--scan-timeout", type=float, default=30.0)
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--recovery-timeout", type=float, default=30.0)
    parser.add_argument("--reconnect-timeout", type=float, default=45.0)
    parser.add_argument("--block-size", type=int, default=0, help="0 selects a multiple of receiver patch-data size")
    parser.add_argument("--chunk-size", type=int, default=0, help="0 uses the receiver patch-data size, up to 244")
    parser.add_argument("--block-timeout", type=float, default=30.0)
    parser.add_argument("--packet-delay", type=float, default=0)
    parser.add_argument("--skip-reconnect-check", action="store_true")
    args = parser.parse_args()
    if args.block_size != 0 and not 80 <= args.block_size <= 512:
        parser.error("--block-size must be 0 (auto) or between 80 and the SDK6 receiver buffer size (512)")
    if not 0 <= args.chunk_size <= 244:
        parser.error("--chunk-size must be 0 (auto) or between 1 and 244")
    if args.packet_delay < 0:
        parser.error("--packet-delay must be non-negative")
    return args


if __name__ == "__main__":
    try:
        asyncio.run(transfer(parse_args()))
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"ERROR|{type(exc).__name__}|{exc}")
        raise SystemExit(1)
