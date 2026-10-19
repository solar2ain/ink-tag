#!/usr/bin/env python3
"""Canonical InkTag CLI driver for Goodix OTAS v2 over BLE."""

from __future__ import annotations
from inktag_cli.generated_commands_v2 import PROTOCOL_V2_VERSION
from inktag_cli.generated_release_v1 import encode_request_flags

import argparse
import asyncio
import hashlib
import json
import re
import struct
import sys
import time
import zipfile
from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path

from bleak import BleakClient, BleakScanner

from inktag_cli.ota_rejection import describe_ota_rejection as describe_common_ota_rejection
from inktag_cli.ota_password import authorize_ota_application
from inktag_cli.board_ids import firmware_family_target
from inktag_cli.goodix_package import (
    IMAGE_INFO_PATTERN,
    IMAGE_INFO_SIZE,
    IMAGE_INFO_STRUCT_SIZE,
    IMAGE_TRAILER_SIZE,
    SIGNATURE_EXTENSION_SIZE,
    SIGNED_TRAILER_SIZE,
    SIGNATURE_PATTERN,
    SIGNED_FIRMWARE_TYPE,
    RSA_PUBLIC_KEY_OFFSET,
    RSA_PUBLIC_KEY_SIZE,
    RSA_SIGNATURE_OFFSET,
    RSA_SIGNATURE_SIZE,
    ECDSA_PUBLIC_KEY_SIZE,
    ECDSA_SIGNATURE_SIZE,
    TRUSTED_SIGNING_KEYS,
    LAYOUT_PROFILES,
    Package,
    layout_profile,
    load_package,
)
from inktag_cli.transport import InkTagV2StatusError


SERVICE_UUID = "a6ed0401-d344-460a-8075-b9e8ec90d71b"
TX_UUID = "a6ed0402-d344-460a-8075-b9e8ec90d71b"
RX_UUID = "a6ed0403-d344-460a-8075-b9e8ec90d71b"
CTRL_UUID = "a6ed0404-d344-460a-8075-b9e8ec90d71b"
APPLICATION_SERVICE_UUID = "62750001-d828-918d-fb46-b6c11c675aec"
# Goodix defines OTAS_CTRL_PT_OP_DFU_ENTER as the integer 0x474F4F44 and
# decodes the four received octets with le32toh(). Its wire representation is
# therefore 44 4F 4F 47 ("DOOG"), despite the visually tempting reverse.
ENTER_OTA = struct.pack("<I", 0x474F4F44)
INKTAG_COMMAND_UUID = "62750002-d828-918d-fb46-b6c11c675aec"
INKTAG_VERSION_UUID = "62750003-d828-918d-fb46-b6c11c675aec"
INKTAG_CMD_GET_TIME = 0x05
INKTAG_CMD_SET_TIME = 0x06

CMD_GET_INFO = 0x01
CMD_PROGRAM_START = 0x23
CMD_PROGRAM_FLASH = 0x24
CMD_PROGRAM_END = 0x25
CMD_DFU_MODE_SET = 0x41
CMD_DFU_FW_INFO_GET = 0x42
# Goodix's native command table ends at slot 0x1D. Recovery therefore reuses
# DFU_MODE_SET: its normal one-byte payload keeps the vendor behavior, while an
# empty payload is the guarded SAFE_RESUME_APP operation.
CMD_SAFE_RESUME_APP = CMD_DFU_MODE_SET
ACK_SUCCESS = 0x01
ACK_ERROR = 0x02
COPY_UPGRADE = 0x01
NON_COPY_UPGRADE = 0x02
MODE_SETTLE_SECONDS = 1.0


def recovery_name_from_identity(address: str, application_name: str) -> str | None:
    """Return the immutable GR Recovery name, never a Pro-renamed app name."""
    address_match = re.search(
        r"(?:^|[:-])([0-9A-Fa-f]{2})[:-]([0-9A-Fa-f]{2})$", address
    )
    if address_match:
        suffix = "".join(address_match.groups()).upper()
        return f"IKT_GR_{suffix}_OTA"
    name_match = re.fullmatch(
        r"(?:IKT|InkTag)_GR_([0-9A-Fa-f]{4})(?:_OTA)?", application_name
    )
    if name_match:
        return f"IKT_GR_{name_match.group(1).upper()}_OTA"
    return None

OTA_REJECT_MAGIC = b"IK"
OTA_REJECT_VERSION = 1
OTA_REJECT_WIRE_SIZE = 15
OTA_REJECT_FLAG_SAMPLED_MV_VALID = 0x01
OTA_REJECT_FLAG_THRESHOLD_MV_VALID = 0x02
OTA_REJECT_FLAG_DETAIL_IS_ADDRESS = 0x04
OTA_REJECT_FLAG_DETAIL_IS_OFFSET = 0x08
OTA_REJECT_REASONS = {
    0: "UNSPECIFIED",
    1: "POWER_SAMPLE_INVALID",
    2: "VOLTAGE_BELOW_MIN",
    3: "IMAGE_INFO_INVALID",
    4: "ADDRESS_OR_LAYOUT_MISMATCH",
    5: "ERASE_FAILED",
    6: "WRITE_FAILED",
    7: "CHECKSUM_OR_SIGNATURE_FAILED",
    8: "INTERNAL",
}
OTA_REJECT_STAGES = {
    0: "UNKNOWN",
    1: "MODE_SWITCH",
    2: "PROGRAM_START",
    3: "ERASE",
    4: "PROGRAM_WRITE",
    5: "PROGRAM_END",
    6: "IMAGE_VERIFY",
    7: "SIGNATURE_VERIFY",
    8: "METADATA_COMMIT",
    9: "BOOT_CONFIRM",
}


def describe_ota_rejection(
    operation: str, response: bytes | bytearray | None, *, fallback_offset: int | None = None
) -> str:
    """Decode the mandatory current InkTag rejection extension."""
    return describe_common_ota_rejection(
        operation, response, error_status=ACK_ERROR,
        fallback_offset=fallback_offset,
    )


def make_bleak_client(
    device, timeout: float, *, uncached_services: bool = False,
    include_product_services: bool = False,
) -> BleakClient:
    options = {"timeout": timeout}
    client_device = device
    if sys.platform == "win32" and uncached_services:
        # The application and fixed Recovery share an address but expose
        # different services. WinRT's cached application table can otherwise
        # cancel a valid connection after the device switches to Recovery.
        # Filtering discovery to OTAS also avoids enumerating the stale
        # application table before WinRT asks the live peripheral for OTAS.
        # Re-resolve the address instead of carrying the application's scanned
        # BLEDevice instance across the GATT-generation change.
        options["winrt"] = {"use_cached_services": False}
        options["services"] = {SERVICE_UUID}
        if include_product_services:
            options["services"].add(APPLICATION_SERVICE_UUID)
        client_device = str(getattr(device, "address", device))
    return BleakClient(client_device, **options)


@asynccontextmanager
async def retrying_bleak_client(
    device,
    timeout: float,
    *,
    uncached_services: bool = False,
    include_product_services: bool = False,
    attempts: int = 3,
):
    from inktag_cli.transport import release_bleak_client

    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        client = make_bleak_client(
            device, timeout, uncached_services=uncached_services,
            include_product_services=include_product_services,
        )
        try:
            await client.connect()
        except Exception as error:
            last_error = error
            try:
                await release_bleak_client(client)
            except Exception:
                pass
            if attempt == attempts:
                raise
            print(f"BLE connect retry {attempt}/{attempts}: {type(error).__name__}")
            await asyncio.sleep(float(attempt))
            continue
        try:
            yield client
        finally:
            try:
                await release_bleak_client(client)
            except Exception:
                pass
        return
    raise RuntimeError(f"BLE connection failed: {last_error!r}")


def frame(command: int, payload: bytes = b"") -> bytes:
    header = struct.pack("<HHH", 0x4744, command, len(payload))
    check = sum(header[2:] + payload) & 0xFFFF
    return header + payload + struct.pack("<H", check)


class FrameParser:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, bytes]]:
        self.buffer.extend(data)
        output = []
        while True:
            start = self.buffer.find(b"DG")
            if start < 0:
                self.buffer.clear()
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 8:
                break
            command, length = struct.unpack_from("<HH", self.buffer, 2)
            total = length + 8
            if len(self.buffer) < total:
                break
            raw = bytes(self.buffer[:total])
            del self.buffer[:total]
            expected = struct.unpack_from("<H", raw, total - 2)[0]
            if (sum(raw[2 : total - 2]) & 0xFFFF) != expected:
                raise RuntimeError("Goodix response frame checksum failed")
            output.append((command, raw[6 : 6 + length]))
        return output


class GoodixOtas:
    def __init__(self, client: BleakClient, packet_size: int) -> None:
        self.client = client
        self.packet_size = packet_size
        self.responses: asyncio.Queue[tuple[int, bytes]] = asyncio.Queue()
        self.parser = FrameParser()

    def notification(self, _sender, data: bytearray) -> None:
        for response in self.parser.feed(bytes(data)):
            self.responses.put_nowait(response)

    async def open(self) -> None:
        for attempt in range(1, 4):
            try:
                await self.client.start_notify(TX_UUID, self.notification)
                break
            except OSError:
                if attempt == 3 or not self.client.is_connected:
                    raise
                await asyncio.sleep(0.2 * attempt)
        await self.client.write_gatt_char(CTRL_UUID, ENTER_OTA, response=False)
        await asyncio.sleep(0.1)

    async def close(self) -> None:
        if self.client.is_connected:
            try:
                await self.client.stop_notify(TX_UUID)
            except Exception:
                pass

    async def send_raw(self, data: bytes) -> None:
        for offset in range(0, len(data), self.packet_size):
            await self.client.write_gatt_char(RX_UUID, data[offset : offset + self.packet_size], response=False)

    async def send(self, command: int, payload: bytes = b"") -> None:
        await self.send_raw(frame(command, payload))

    async def request(self, command: int, payload: bytes = b"", timeout: float = 30.0) -> bytes:
        await self.send(command, payload)
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise TimeoutError(f"Goodix OTAS command 0x{command:02X} timed out")
            response_command, response = await asyncio.wait_for(self.responses.get(), remaining)
            if response_command == command:
                return response

    async def resume_application_if_safe(self) -> None:
        response = await self.request(CMD_SAFE_RESUME_APP)
        if not response or response[0] != ACK_SUCCESS:
            raise RuntimeError(describe_ota_rejection("SAFE_RESUME_APP", response))


async def discover(name: str | None, address: str | None, timeout: float):
    # Reuse the maintained product scanner. In particular, an explicit
    # Windows address must not launch a second discovery pass: a WebBluetooth
    # or WinRT reference can retain the physical link and suppress advertising
    # even though GATT remains reachable for OTA takeover.
    from inktag_cli.transport import InkTagError, select_device

    try:
        device, advertisement = await select_device(
            name or (None if address else "IKT_GR_"), address, timeout
        )
    except InkTagError as error:
        if name is None and address is None:
            try:
                device, advertisement = await select_device(
                    "InkTag_GR_", None, timeout
                )
            except InkTagError:
                raise RuntimeError("no Goodix InkTag/OTA device found") from error
        elif address:
            raise RuntimeError(f"Goodix device address not found: {address}") from error
        else:
            raise RuntimeError("no Goodix InkTag/OTA device found") from error
    advertised_name = (
        getattr(advertisement, "local_name", None)
        or getattr(device, "name", None)
        or ""
    ).strip()
    print(f"Found: {advertised_name or '(unnamed)'} [{device.address}]")
    return device, advertised_name


def recovery_advertisement_matches(
    name: str, service_uuids, address: str | None = None
) -> bool:
    """Accept only the fixed Goodix Recovery role and its stable ID4 name."""
    match = re.fullmatch(r"(?:IKT|InkTag)_GR_([0-9A-Fa-f]{4})_OTA", name.strip())
    services = {str(value).lower() for value in (service_uuids or ())}
    if match is None or SERVICE_UUID.lower() not in services:
        return False
    if address:
        expected = recovery_name_from_identity(address, "")
        if expected is not None:
            expected_match = re.fullmatch(
                r"IKT_GR_([0-9A-Fa-f]{4})_OTA", expected
            )
            if expected_match is None or \
                    match.group(1).casefold() != expected_match.group(1).casefold():
                return False
    return True


async def discover_recovery(
    name: str | None, address: str | None, timeout: float
):
    from inktag_cli.transport import _collect_scan_matches

    # Recovery needs the actual name AND DFU service, not the fast address-only
    # connection selector. Merge ADV/scan-response inside the original budget;
    # reuse the shared single empty-watcher restart rather than guessing a name.
    discovered = await _collect_scan_matches(
        timeout, name=name, address=address,
        inventory=not bool(address), wait_for_full_timeout=bool(address),
    )
    candidates = {}
    for device, advertisement, advertised_name in discovered:
        if address and device.address.casefold() != address.casefold():
            continue
        if name and not advertised_name.casefold().startswith(name.casefold()):
            continue
        if not recovery_advertisement_matches(
            advertised_name,
            getattr(advertisement, "service_uuids", None),
            device.address,
        ):
            continue
        candidates[device.address.casefold()] = (device, advertised_name)
    if not candidates:
        raise RuntimeError("no fixed Goodix Recovery device matched the request")
    if len(candidates) != 1:
        names = ", ".join(sorted(value[1] for value in candidates.values()))
        raise RuntimeError(f"multiple fixed Goodix Recovery devices matched: {names}")
    device, advertised_name = next(iter(candidates.values()))
    print(f"Found: {advertised_name} [{device.address}]")
    return device, advertised_name


async def wait_for_safe_resumed_application(
    address: str, recovery_name: str, timeout: float
):
    expected_name = recovery_name.removesuffix("_OTA")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        discovered = await BleakScanner.discover(
            timeout=min(3.0, max(0.5, deadline - time.monotonic())),
            return_adv=True,
        )
        for device, advertisement in discovered.values():
            advertised_name = (
                getattr(advertisement, "local_name", None)
                or getattr(device, "name", None)
                or ""
            ).strip()
            services = {
                str(value).lower()
                for value in (getattr(advertisement, "service_uuids", None) or ())
            }
            identity_matches = (
                device.address.casefold() == address.casefold()
                or advertised_name.casefold() == expected_name.casefold()
            )
            if (identity_matches and not advertised_name.casefold().endswith("_ota")
                    and APPLICATION_SERVICE_UUID.lower() in services):
                return device, advertised_name
        await asyncio.sleep(0.4)
    raise TimeoutError("Goodix application did not advertise after safe resume")


async def application_identity(
    client: BleakClient,
    manifest: dict,
    timeout: float = 6.0,
    verify_release: bool = False,
    require_build: bool = False,
    sync_time: bool = False,
    authorize_password: bool = False,
    password_data_dir: Path | None = None,
    retain_notifications: bool = False,
) -> dict:
    version = bytes(await client.read_gatt_char(INKTAG_VERSION_UUID))
    if len(version) < 4:
        raise RuntimeError("InkTag application version characteristic is incomplete")
    protocol = version[0]
    release = f"{version[1]}.{version[2]}.{version[3]}"
    board = None
    build = None
    authorization_succeeded = False

    if protocol != PROTOCOL_V2_VERSION:
        raise RuntimeError(
            f"unsupported pre-release InkTag protocol 0x{protocol:02X}; "
            "install the current Full image before OTA"
        )

    if protocol == PROTOCOL_V2_VERSION:
        responses: asyncio.Queue[bytes] = asyncio.Queue()

        def v2_notification(_sender, data: bytearray) -> None:
            responses.put_nowait(bytes(data))

        async def v2_request(
            opcode: int, transaction: int, payload: bytes = b""
        ) -> bytes:
            await client.write_gatt_char(
                INKTAG_COMMAND_UUID,
                bytes((opcode, transaction, encode_request_flags(0))) + payload,
                response=True,
            )
            deadline = asyncio.get_running_loop().time() + timeout
            while True:
                remaining = deadline - asyncio.get_running_loop().time()
                if remaining <= 0:
                    raise TimeoutError(
                        f"InkTag V2 identity command 0x{opcode:02X} timed out"
                    )
                response = await asyncio.wait_for(responses.get(), remaining)
                if len(response) < 3 or response[0] != opcode or response[1] != transaction:
                    continue
                if response[2] != 0:
                    raise InkTagV2StatusError(response[2], opcode,
                        f"InkTag V2 identity command 0x{opcode:02X} failed: "
                        f"status=0x{response[2]:02X}"
                    )
                return response[3:]

        await client.start_notify(INKTAG_COMMAND_UUID, v2_notification)
        try:
            hello = await v2_request(0x01, 1)
            if len(hello) < 2 or hello[0] != 1 or hello[1] != PROTOCOL_V2_VERSION:
                raise RuntimeError("InkTag application returned an invalid V2 handshake")
            info = await v2_request(0x03, 2)
            if len(info) < 23 or info[0] != 1:
                raise RuntimeError("InkTag application returned invalid V2 identity information")
            board_length, name_length, label_length = info[1:4]
            if 23 + board_length + name_length + label_length > len(info):
                raise RuntimeError("InkTag application returned invalid V2 identity lengths")
            board = info[23 : 23 + board_length].decode("ascii", errors="replace")
            expected_board = str(manifest.get("board") or board)
            if board != expected_board and firmware_family_target(board) != expected_board and not (
                expected_board == "gr551x_zkc42v"
                and board in {"gr551x_zkc42vm", "gr551x_zkc42v"}
            ):
                raise RuntimeError(f"OTA board mismatch: device={board}, package={expected_board}")
            build = {
                "version": struct.unpack_from("<I", info, 11)[0],
                "size": struct.unpack_from("<I", info, 15)[0],
                "checksum": struct.unpack_from("<I", info, 19)[0],
                # V2 identifies the exact active image. Recovery generation is
                # intentionally not part of the public device-info payload.
                "state": None,
                "generation": None,
            }
            if authorize_password:
                transaction = 16

                async def password_request(opcode, payload=b""):
                    nonlocal transaction
                    transaction += 1
                    return await v2_request(opcode, transaction, payload)

                await authorize_ota_application(password_request, password_data_dir)
                authorization_succeeded = True
            if sync_time:
                saved_time = await v2_request(INKTAG_CMD_GET_TIME, 3)
                if len(saved_time) != 7 or saved_time[6] > 6:
                    raise RuntimeError(
                        "InkTag application returned invalid V2 time information"
                    )
                current = datetime.now().astimezone()
                offset = current.utcoffset()
                timezone_minutes = int(
                    (offset.total_seconds() if offset else 0) // 60
                )
                if not -32768 <= timezone_minutes <= 32767:
                    raise RuntimeError("host timezone does not fit the V2 protocol")
                await v2_request(
                    INKTAG_CMD_SET_TIME,
                    4,
                    struct.pack(
                        "<IhB",
                        int(current.timestamp()),
                        timezone_minutes,
                        saved_time[6],
                    ),
                )
        finally:
            if not (retain_notifications and authorization_succeeded):
                await client.stop_notify(INKTAG_COMMAND_UUID)
    expected_board = str(manifest.get("board") or board or "")
    expected_protocol = int(manifest.get("protocol", protocol))
    expected_release = str(manifest.get("release") or release)
    compatible_board = board == expected_board or firmware_family_target(board) == expected_board or (
        expected_board == "gr551x_zkc42v"
        and board in {"gr551x_zkc42vm", "gr551x_zkc42v"}
    )
    if not compatible_board:
        raise RuntimeError(f"OTA board mismatch: device={board}, package={expected_board}")
    if verify_release and protocol != expected_protocol:
        raise RuntimeError(
            f"OTA protocol mismatch: device={protocol}, package={expected_protocol}"
        )
    if verify_release and release != expected_release:
        raise RuntimeError(
            f"OTA release mismatch: device={release}, package={expected_release}"
        )
    if require_build:
        if build is None:
            raise RuntimeError("upgraded application did not report an exact build identity")
        application = manifest["application"]
        expected_build = {
            "version": int(manifest["compatibilityEpoch"]),
            "size": int(application["size"]),
            "checksum": int(application["checksum"]),
        }
        actual_build = {key: build[key] for key in expected_build}
        if actual_build != expected_build:
            raise RuntimeError(
                "OTA build mismatch: "
                f"device=epoch{actual_build['version']}/{actual_build['size']}/"
                f"{actual_build['checksum']:08x}, "
                f"package=epoch{expected_build['version']}/{expected_build['size']}/"
                f"{expected_build['checksum']:08x}"
            )
    return {
        "board": board,
        "protocol": protocol,
        "release": release,
        "build": build,
    }


async def authorize_application_link(client, manifest, password_data_dir=None):
    """Authorize the actual App link before any vendor OTAS control write.

    Fixed Recovery has no product-command characteristic and does not need a
    password. Never use advertisement names or a prior connection's AUTH as
    evidence. Keep product notifications enabled through the vendor handoff:
    disabling its CCCD clears GR's authorization. The enclosing client context
    disconnects and releases notifications, including on any failure.
    """
    if client.services.get_characteristic(INKTAG_COMMAND_UUID) is None:
        return None
    return await application_identity(
        client, manifest, timeout=15.0, authorize_password=True,
        password_data_dir=password_data_dir, retain_notifications=True,
    )


async def reconnect_application(
    args,
    package: Package,
    advertised_name: str,
    address: str,
    previous_identity: dict,
    require_generation_advance: bool,
) -> None:
    print("Waiting for upgraded application reconnect", end="", flush=True)
    await asyncio.sleep(2.0)
    deadline = time.monotonic() + args.reconnect_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        scan_time = min(4.0, max(0.5, remaining))
        try:
            discovered = await BleakScanner.discover(timeout=scan_time, return_adv=True)
            exact_candidates = []
            for device, advertisement in discovered.values():
                current_name = advertisement.local_name or device.name or ""
                if device.address.casefold() == address.casefold() or current_name == advertised_name:
                    exact_candidates.append(device)
            candidates = exact_candidates
            for device in candidates:
                try:
                    async with retrying_bleak_client(
                        device, args.connect_timeout, uncached_services=True,
                        include_product_services=True,
                    ) as client:
                        identity = await application_identity(
                            client,
                            package.manifest,
                            timeout=min(4.0, max(1.0, remaining)),
                            verify_release=True,
                            require_build=True,
                            sync_time=not args.no_sync,
                            authorize_password=True,
                            password_data_dir=getattr(args, "password_data_dir", None),
                        )
                    build = identity["build"]
                    assert build is not None
                    if require_generation_advance:
                        if build.get("state") is not None and build["state"] != 4:
                            raise RuntimeError(
                                f"OTA swap is not confirmed: state={build['state']}"
                            )
                        previous_build = previous_identity.get("build")
                        if (
                            previous_build is not None
                            and previous_build.get("generation") is not None
                            and build.get("generation") is not None
                            and build["generation"] <= previous_build["generation"]
                        ):
                            raise RuntimeError(
                                "OTA swap generation did not advance: "
                                f"before={previous_build['generation']} "
                                f"after={build['generation']}"
                            )
                    print()
                    print(
                        "Application reconnect verified: "
                        f"board={identity['board']} release={identity['release']} "
                        f"protocol={identity['protocol']} epoch={build['version']} "
                        f"app_bytes={build['size']} "
                        f"state={build['state'] if build.get('state') is not None else 'active'} "
                        f"generation={build['generation'] if build.get('generation') is not None else 'n/a'}"
                    )
                    return
                except Exception as error:
                    last_error = error
        except Exception as error:
            last_error = error
        print(".", end="", flush=True)
        await asyncio.sleep(0.5)
    print()
    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(f"upgraded Goodix application did not reconnect{detail}")


async def wait_for_recovery(
    reconnect_timeout: float,
    address: str,
    application_name: str,
    deadline: float | None = None,
):
    expected_name = recovery_name_from_identity(address, application_name)
    expected_id4 = expected_name.split("_")[-2] if expected_name is not None else None
    expected_label = expected_name or f"same identity as {address}"
    print(f"Waiting for BLE Recovery ({expected_label})", end="", flush=True)
    if deadline is None:
        deadline = time.monotonic() + reconnect_timeout
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(
            timeout=min(3.0, max(0.5, deadline - time.monotonic())),
            return_adv=True,
        )
        for device, advertisement in devices.values():
            current_name = advertisement.local_name or device.name or ""
            services = getattr(advertisement, "service_uuids", None)
            if not recovery_advertisement_matches(current_name, services):
                continue
            current_id4 = current_name.strip().split("_")[-2]
            if (expected_id4 is not None and current_id4.casefold() == expected_id4.casefold()) or (
                expected_id4 is None and device.address.casefold() == address.casefold()
            ):
                print()
                print(f"Recovery found: {current_name or '(unnamed)'} [{device.address}]")
                return device, current_name
        print(".", end="", flush=True)
        await asyncio.sleep(0.4)
    print()
    raise TimeoutError("Goodix BLE Recovery did not appear after mode switch")


async def program_open_transport(
    otas: GoodixOtas, package: Package, args, save_address: int, is_application: bool
) -> None:
    app = package.manifest["application"]
    if is_application:
        raise RuntimeError("refusing to overwrite a running XIP application")
    program_address = app["active_address"]
    if save_address != program_address:
        raise RuntimeError(
            f"recovery address mismatch: device=0x{save_address:08X}, "
            f"package=0x{program_address:08X}"
        )
    print(f"Mode: fixed BLE Recovery direct to 0x{program_address:08X}")

    start_payload = bytes([package.firmware_type]) + package.image_info
    response = await otas.request(CMD_PROGRAM_START, start_payload, timeout=45.0)
    if not response or response[0] != ACK_SUCCESS:
        raise RuntimeError(describe_ota_rejection("PROGRAM_START", response))

    started = time.monotonic()
    sent = 0
    while sent < len(package.firmware):
        chunk = package.firmware[sent : sent + 1024]
        payload = b"\x01" + struct.pack("<IH", program_address + sent, len(chunk)) + chunk
        response = await otas.request(CMD_PROGRAM_FLASH, payload, timeout=30.0)
        if not response or response[0] != ACK_SUCCESS:
            raise RuntimeError(
                describe_ota_rejection("PROGRAM_FLASH", response, fallback_offset=sent)
            )
        sent += len(chunk)
        percent = sent * 100 / len(package.firmware)
        speed = sent / max(time.monotonic() - started, 0.001) / 1024
        print(f"\rProgramming {percent:5.1f}% ({speed:5.1f} KiB/s)", end="", flush=True)
    print()

    transfer_checksum = sum(package.firmware) & 0xFFFFFFFF
    response = await otas.request(
        CMD_PROGRAM_END,
        b"\x01" + struct.pack("<I", transfer_checksum),
        timeout=30.0,
    )
    if not response or response[0] != ACK_SUCCESS:
        raise RuntimeError(describe_ota_rejection("PROGRAM_END", response))


async def query_otas_target(
    otas: GoodixOtas, package: Package | None = None
) -> tuple[int, bool]:
    info = await otas.request(CMD_GET_INFO)
    if len(info) < 18 or info[0] != ACK_SUCCESS or info[17] != 2:
        raise RuntimeError("target does not report Goodix OTAS v2")
    target = await otas.request(CMD_DFU_FW_INFO_GET)
    if len(target) < 54 or target[0] != ACK_SUCCESS:
        raise RuntimeError("target rejected DFU firmware-info query")
    if target[5] not in (0, 1):
        raise RuntimeError("target reports an unknown Goodix OTA role")
    if package is not None:
        pattern, active_epoch = struct.unpack_from("<HH", target, 6)
        active_size, _, active_load, active_run = struct.unpack_from("<IIII", target, 10)
        marker, inverse = struct.unpack_from("<II", target, 6 + IMAGE_INFO_STRUCT_SIZE)
        expected_epoch = int(package.manifest["compatibilityEpoch"])
        expected_address = int(package.manifest["application"]["active_address"])
        profile = layout_profile(package.manifest)
        if pattern != IMAGE_INFO_PATTERN or active_epoch != expected_epoch or \
                marker != profile["layout_marker"] or inverse != ((~marker) & 0xFFFFFFFF) or \
                active_load != expected_address or active_run != active_load or \
                expected_address != profile["active_address"] or \
                struct.unpack_from("<I", target, 1)[0] != expected_address or \
                not 0 < active_size <= profile["slot_size"] - SIGNED_TRAILER_SIZE:
            raise RuntimeError(
                "target is not on the board-qualified Goodix layout; install the chip-qualified Full image first"
            )
    return struct.unpack_from("<I", target, 1)[0], target[5] == 1


async def enter_verified_recovery(
    name: str | None,
    address: str | None,
    scan_timeout: float,
    connect_timeout: float,
    reconnect_timeout: float,
    password_data_dir: Path | None = None,
) -> dict:
    """Complete and attest the Goodix application-to-Recovery handoff.

    The product SYSTEM_ACTION asks the application to expose its OTA-ready
    identity, but Goodix still requires the vendor OTAS DFU_MODE_SET command.
    An ``_OTA`` name is therefore not proof that Recovery is running.  Query
    OTAS before the mode switch and again after reconnect, and report success
    only when the live target identifies itself as fixed Recovery.
    """
    try:
        device, advertised_name = await discover(name, address, scan_timeout)
    except Exception as error:
        raise RuntimeError(
            f"Goodix Recovery discovery stage failed: {error}"
        ) from error

    resolved_address = str(device.address)
    try:
        async with retrying_bleak_client(
            device, connect_timeout, uncached_services=True,
            include_product_services=True,
        ) as client:
            authorized_identity = await authorize_application_link(
                client, {}, password_data_dir
            )
            otas = GoodixOtas(client, 240)
            await otas.open()
            try:
                _, is_application = await query_otas_target(otas)
                if is_application:
                    if authorized_identity is None:
                        raise RuntimeError("Goodix App lacks its authenticated product link")
                    await otas.send(
                        CMD_DFU_MODE_SET, bytes((NON_COPY_UPGRADE,))
                    )
            finally:
                await otas.close()
    except Exception as error:
        raise RuntimeError(
            f"Goodix Recovery mode-set stage failed: {error}"
        ) from error

    if not is_application:
        return {
            "platform": "goodix",
            "role": "recovery",
            "address": resolved_address,
            "recoveryName": advertised_name,
            "modeSetSent": False,
        }

    # Goodix commits its Recovery request after the OTAS connection closes.
    await asyncio.sleep(MODE_SETTLE_SECONDS)
    deadline = time.monotonic() + reconnect_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            recovery_device, recovery_name = await wait_for_recovery(
                reconnect_timeout,
                resolved_address,
                advertised_name,
                deadline,
            )
            async with retrying_bleak_client(
                recovery_device, connect_timeout, uncached_services=True
            ) as client:
                otas = GoodixOtas(client, 240)
                await otas.open()
                try:
                    _, recovery_is_application = await query_otas_target(otas)
                finally:
                    await otas.close()
            if recovery_is_application:
                raise RuntimeError(
                    "candidate still reports the running application role"
                )
            return {
                "platform": "goodix",
                "role": "recovery",
                "address": resolved_address,
                "recoveryName": recovery_name,
                "modeSetSent": True,
            }
        except Exception as error:
            last_error = error
            if time.monotonic() < deadline:
                await asyncio.sleep(0.8)

    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(
        f"Goodix Recovery role-verification stage failed{detail}"
    )


async def resume_safe_application(
    name: str | None,
    address: str | None,
    scan_timeout: float,
    connect_timeout: float,
    reconnect_timeout: float,
) -> dict:
    """Ask idle fixed Recovery to resume its untouched valid application."""
    device, recovery_name = await discover_recovery(name, address, scan_timeout)
    resolved_address = str(device.address)
    async with retrying_bleak_client(
        device, connect_timeout, uncached_services=True
    ) as client:
        otas = GoodixOtas(client, 240)
        await otas.open()
        try:
            _, is_application = await query_otas_target(otas)
            if is_application:
                raise RuntimeError(
                    "safe resume requires fixed Goodix Recovery, not an application"
                )
            await otas.resume_application_if_safe()
        finally:
            await otas.close()
    _, application_name = await wait_for_safe_resumed_application(
        resolved_address, recovery_name, reconnect_timeout
    )
    return {
        "ok": True,
        "action": "safe-resume-app",
        "platform": "goodix",
        "address": resolved_address,
        "applicationName": application_name,
    }


async def transfer(args) -> None:
    package = load_package(args.package)
    layout_profile(package.manifest)
    app = package.manifest["application"]
    print(f"Package: {args.package}")
    print(f"  Board: {package.manifest['board']} / {package.manifest['chip']}")
    print(
        f"  Release: {package.manifest['release']} "
        f"({package.manifest['releaseVersion']}); "
        f"compatibility epoch {package.manifest['compatibilityEpoch']}"
    )
    print(f"  Application: {app['size']} bytes; transfer {len(package.firmware)} bytes")

    device, advertised_name = await discover(args.name, args.address, args.scan_timeout)
    address = str(device.address)
    identity = {"board": None, "protocol": None, "release": None, "build": None}
    recovery_device = device
    recovery_name = advertised_name
    async with retrying_bleak_client(
        device,
        args.connect_timeout,
        uncached_services=True,
        include_product_services=True,
    ) as client:
        # Authenticate on this very connection before ENTER_OTA or mode-set.
        # An App may already advertise _OTA; live service presence, not the
        # name, distinguishes it from password-free fixed Recovery.
        preflight_identity = await authorize_application_link(
            client, package.manifest, getattr(args, "password_data_dir", None)
        )
        otas = GoodixOtas(client, args.packet_size)
        await otas.open()
        try:
            save_address, is_application = await query_otas_target(otas, package)
            if is_application:
                if preflight_identity is None:
                    raise TimeoutError(
                        "InkTag application did not report its board identity before OTAS opened"
                    )
                identity = preflight_identity
                build = identity.get("build")
                print(
                    f"Preflight: board={identity['board']} release={identity['release']} "
                    f"protocol={identity['protocol']}"
                    + (
                        f" epoch={build['version']} app_bytes={build['size']} "
                        f"state={build['state'] if build.get('state') is not None else 'active'} "
                        f"generation={build['generation'] if build.get('generation') is not None else 'n/a'}"
                        if build is not None else " build=unreported"
                    )
                )
                print("Switching the application to fixed BLE Recovery")
                await otas.send(CMD_DFU_MODE_SET, bytes([NON_COPY_UPGRADE]))
            else:
                print("Preflight: device is already in fixed BLE Recovery")
                await program_open_transport(otas, package, args, save_address, False)
        finally:
            await otas.close()
    if is_application:
        # The Goodix application handles DFU_MODE_SET by waiting for this
        # GATT connection to close, then committing Recovery metadata and
        # resetting. Do not scan/reconnect in that handoff window: reconnecting
        # the application can keep wait_for_disconnection() alive indefinitely.
        await asyncio.sleep(MODE_SETTLE_SECONDS)
        recovery_deadline = time.monotonic() + args.reconnect_timeout
        last_recovery_error: Exception | None = None
        while time.monotonic() < recovery_deadline:
            recovery_device, recovery_name = await wait_for_recovery(
                args.reconnect_timeout, address, advertised_name, recovery_deadline
            )
            try:
                async with retrying_bleak_client(
                    recovery_device,
                    args.connect_timeout,
                    uncached_services=True,
                ) as client:
                    otas = GoodixOtas(client, args.packet_size)
                    await otas.open()
                    try:
                        save_address, recovery_is_application = await query_otas_target(otas, package)
                        if recovery_is_application:
                            raise RuntimeError(
                                "Recovery candidate is still the running application"
                            )
                        await program_open_transport(
                            otas, package, args, save_address, False
                        )
                        break
                    finally:
                        await otas.close()
            except Exception as error:
                last_recovery_error = error
                print(f"Recovery candidate not ready: {error}")
                await asyncio.sleep(0.8)
        else:
            detail = f": {last_recovery_error}" if last_recovery_error else ""
            raise TimeoutError(f"Goodix BLE Recovery did not become ready{detail}")
    print("OTA transfer complete. Waiting for the trial application to confirm startup.")
    await reconnect_application(
        args,
        package,
        recovery_name.removesuffix("_OTA"),
        address,
        identity,
        require_generation_advance=True,
    )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="InkTag Goodix OTA ZIP")
    parser.add_argument("--name", help="BLE name/prefix, for example IKT_GR_000F")
    parser.add_argument("--address", help="BLE address")
    parser.add_argument("--password-data-dir", type=Path)
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--reconnect-timeout", type=float, default=75.0)
    parser.add_argument("--packet-size", type=int, default=240, choices=range(20, 245))
    parser.add_argument(
        "--no-sync",
        action="store_true",
        help="Do not set the application clock during post-update verification",
    )
    return parser.parse_args()


def main() -> int:
    try:
        asyncio.run(transfer(parse_args()))
        return 0
    except KeyboardInterrupt:
        print("\nCancelled.", file=sys.stderr)
        return 130
    except Exception as exc:
        detail = str(exc) or repr(exc)
        print(f"\nOTA failed ({type(exc).__name__}): {detail}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
