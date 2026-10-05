#!/usr/bin/env python3
"""Canonical InkTag CLI driver for TLSR8359 fixed BLE Recovery."""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import struct
import sys
import time
import zipfile
import zlib
from contextlib import asynccontextmanager
from pathlib import Path
from inktag_cli.ota_password import authorize_ota_application
from inktag_cli.transport import InkTagV2StatusError

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

from inktag_cli.tlsr_layout import (
    TLSR_APP_LIMIT, TLSR_LAYOUT, TLSR_LAYOUT_SIGNATURE, validate_tlsr_data_layout,
)


INKTAG_SERVICE = "62750001-d828-918d-fb46-b6c11c675aec"
INKTAG_COMMAND = "62750002-d828-918d-fb46-b6c11c675aec"
INKTAG_VERSION = "62750003-d828-918d-fb46-b6c11c675aec"
INKTAG_MANUFACTURER_ID = 0x04F7
INKTAG_MANUFACTURER_MARKER = b"\x02\x03"
DEFAULT_NAME_PREFIX = "IKT_TL_"
LEGACY_NAME_PREFIX = "InkTag_TL_"
OTA_CHARACTERISTIC = "00010203-0405-0607-0809-0a0b0c0d2b12"
APP_LIMIT = TLSR_APP_LIMIT
FLASH_SECTOR_BLOCKS = 0x1000 // 16
FAST_PACKET_MAX_BLOCKS = 14
FAST_WINDOW_PACKETS = 2
TRANSFER_ATTEMPTS = 8
FAST_STATUS_MAGIC = b"IT"
CMD_HELLO = 0x01
CMD_GET_DEVICE_INFO = 0x03
CMD_SYSTEM_ACTION = 0x0E
SYSTEM_ACTION_OTA_RECOVERY = 0x04
STATUS_OK = 0x00
RECOVERY_STATUS_RECEIVING = 0x01
RECOVERY_STATUS_COMPLETE = 0x02
RECOVERY_STATUS_QUEUE_FULL = 0x07
RECOVERY_STATUS_SAFE_RESUME_COMPLETE = 0x08
RECOVERY_STATUS_LOW_POWER = 0x09
RECOVERY_STATUS_UNSAFE_RESUME = 0x0A
RECOVERY_CONTROL_SAFE_RESUME_APP = b"\x03\xFF"
SIGNATURE_MAGIC = 0x31534B49
SIGNATURE_VERSION = 2
SIGNATURE_ALGORITHM_ECDSA_P256 = 1
SIGNATURE_PREFIX_SIZE = 32
SIGNATURE_SIZE = 64
SIGNATURE_TRAILER_SIZE = SIGNATURE_PREFIX_SIZE + SIGNATURE_SIZE
BOARD_SIGNATURE = 0x53484C54
LAYOUT_SIGNATURE = TLSR_LAYOUT_SIGNATURE
VERIFIER_SIGNATURE = 0x314B4C54
SIGNED_BOARD = "tlsr8359_hanshow"
SIGNED_LAYOUT = TLSR_LAYOUT
TRUSTED_SIGNING_KEY = {
    "trust_profile": "development",
    "algorithm": "ecdsa-p256-sha256",
    "key_id": "inktag-tlsr8359-ota-fixture-v1",
    "public_key_hash": "c5a7e097d40b64859fbfea22cf5970c0",
}
P256_ORDER = int("FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16)


class RecoveryQueueFullError(RuntimeError):
    """Known v3 data-queue overflow: transfer is aborted, not resumable."""


def recovery_queue_full(status: bytes, start: int, end: int) -> bool:
    # Only the reviewed v3 status layout/semantics permit a fresh transfer.
    # Never infer a retry from an error string or an unknown Recovery version.
    return (
        len(status) == 14 and status[0] == RECOVERY_STATUS_QUEUE_FULL
        and status[8:10] == FAST_STATUS_MAGIC and status[10] == 3
        and 2 <= status[11] <= FAST_PACKET_MAX_BLOCKS
        and start <= recovery_acknowledged_index(status) <= end
    )


def recovery_acknowledged_index(status: bytes) -> int:
    if len(status) < 3:
        raise RuntimeError(
            "fixed Recovery lacks indexed flow control; install the current "
            "Recovery with the guarded first-flash tool"
        )
    return struct.unpack_from("<H", status, 1)[0]


def recovery_fast_block_limit(status: bytes, write_limit: int) -> int:
    """Require the current Recovery-v2-or-newer large-packet transport."""
    if (
        len(status) < 12
        or status[8:10] != FAST_STATUS_MAGIC
        or status[10] < 2
        or status[11] < 2
    ):
        raise RuntimeError(
            "fixed Recovery is older than large-packet v2; install the current "
            "full image once with SWS"
        )
    by_mtu = max(0, (write_limit - 4) // 16)
    result = min(status[11], FAST_PACKET_MAX_BLOCKS, by_mtu)
    if result < 2:
        raise RuntimeError(
            f"host negotiated only a {write_limit}-byte write; Recovery v2 requires "
            "at least 36 bytes"
        )
    return result


def crc16_telink(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        current = value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if ((crc ^ current) & 1) else 0)
            current >>= 1
    return crc


def data_packet(index: int, payload: bytes) -> bytes:
    if (
        len(payload) < 16
        or len(payload) > FAST_PACKET_MAX_BLOCKS * 16
        or len(payload) % 16 != 0
        or not 0 <= index <= 0xFFFF
    ):
        raise ValueError("Telink OTA payload must contain 1..14 indexed 16-byte blocks")
    body = struct.pack("<H", index) + payload
    return body + struct.pack("<H", crc16_telink(body))


def fast_packet_end(start: int, total: int, block_limit: int) -> int:
    """Return the exclusive end of one extended packet without crossing erase."""
    if not 0 <= start < total or block_limit < 2:
        raise ValueError("fast OTA packet arguments are outside the image")
    sector_end = ((start // FLASH_SECTOR_BLOCKS) + 1) * FLASH_SECTOR_BLOCKS
    if start == 0:
        # Deliver the complete 32-byte Telink header in one acknowledged
        # packet. Recovery validates marker and declared size before erasing
        # the first application sector.
        return min(start + 2, total)
    if start % FLASH_SECTOR_BLOCKS == 0:
        return min(start + 1, total)
    return min(start + block_limit, sector_end, total)


def fast_packet_window(
    start: int, total: int, block_limit: int,
    packet_limit: int = FAST_WINDOW_PACKETS,
) -> list[tuple[int, int]]:
    """Build one bounded packet window without hiding an erase boundary."""
    if not 1 <= packet_limit <= FAST_WINDOW_PACKETS:
        raise ValueError("fast OTA packet window limit is invalid")
    ranges: list[tuple[int, int]] = []
    current = start
    for _ in range(packet_limit):
        end = fast_packet_end(current, total, block_limit)
        ranges.append((current, end))
        current = end
        if current >= total or start % FLASH_SECTOR_BLOCKS == 0 or \
                current % FLASH_SECTOR_BLOCKS == 0:
            break
    return ranges


def packet_requires_delivery_ack(index: int) -> bool:
    """Use an ATT write response before a block can start a sector erase."""
    return index % FLASH_SECTOR_BLOCKS == 0


def is_transient_ble_error(error: Exception) -> bool:
    """Recognize transport loss without retrying a Recovery image rejection."""
    if isinstance(error, RecoveryQueueFullError):
        return False
    name = type(error).__name__.lower()
    detail = str(error).lower()
    return (
        name in {"bleakerror", "timeouterror", "connectionerror"}
        or (
            isinstance(error, OSError)
            and getattr(error, "winerror", None) == -2147023673
        )
        or "gatt server is disconnected" in detail
        or "unreachable" in detail
        or "device disconnected" in detail
        or "operation cancelled" in detail
        or "operation canceled" in detail
        # Recovery is still receiving and exposes the exact committed index
        # after either timeout, so reconnecting can resume without data loss.
        or "did not acknowledge blocks" in detail
        or "did not notify blocks" in detail
    )


def validate_application_bytes(image: bytes) -> bytes:
    if len(image) < 32 + SIGNATURE_TRAILER_SIZE or len(image) > APP_LIMIT:
        raise ValueError(f"application size {len(image)} is outside 32..{APP_LIMIT} bytes")
    if image[8:12] != b"KNLT":
        raise ValueError("application is missing the Telink TLNK marker at offset 8")
    declared_size = struct.unpack_from("<I", image, 24)[0]
    application_size = declared_size + 4
    if application_size + SIGNATURE_TRAILER_SIZE != len(image):
        raise ValueError(
            f"Telink size field {declared_size} does not match the signed "
            f"{len(image)}-byte transfer"
        )
    expected_crc = struct.unpack_from(">I", image, declared_size)[0]
    actual_crc = zlib.crc32(image[:declared_size]) & 0xFFFFFFFF
    if expected_crc != actual_crc:
        raise ValueError(
            f"application CRC32 mismatch: stored {expected_crc:08X}, computed {actual_crc:08X}"
        )
    return image


def validate_publisher_signature(image: bytes, manifest: dict) -> None:
    declared_size = struct.unpack_from("<I", image, 24)[0]
    application_size = declared_size + 4
    (magic, version, algorithm, trailer_size, signed_application_size,
     compatibility_epoch, board_signature, layout_signature,
     verifier_signature, signed_release_version) = struct.unpack_from(
        "<IBBHIIIIII", image, application_size
    )
    raw_signature = image[-SIGNATURE_SIZE:]
    signature = manifest.get("signature") or {}
    try:
        public_key = bytes.fromhex(signature["public_key"])
    except (KeyError, ValueError) as error:
        raise ValueError("TLSR8359 package publisher public key is invalid") from error
    if (
        signature.get("trust_profile") != TRUSTED_SIGNING_KEY["trust_profile"]
        or signature.get("algorithm") != TRUSTED_SIGNING_KEY["algorithm"]
        or signature.get("key_id") != TRUSTED_SIGNING_KEY["key_id"]
        or str(signature.get("public_key_hash", "")).lower()
            != TRUSTED_SIGNING_KEY["public_key_hash"]
        or len(public_key) != 65
        or public_key[0] != 0x04
        or hashlib.sha256(public_key).digest()[:16].hex()
            != TRUSTED_SIGNING_KEY["public_key_hash"]
    ):
        raise ValueError("TLSR8359 package publisher signing key is not trusted")
    release_version = int(manifest.get("releaseVersion", -1))
    manifest_epoch = int(manifest.get("compatibilityEpoch", -1))
    if not 0 <= manifest_epoch <= 0xFFFF:
        raise ValueError("TLSR8359 compatibility epoch exceeds the product contract")
    if (
        magic != SIGNATURE_MAGIC
        or version != SIGNATURE_VERSION
        or algorithm != SIGNATURE_ALGORITHM_ECDSA_P256
        or trailer_size != SIGNATURE_TRAILER_SIZE
        or signed_application_size != application_size
        or compatibility_epoch != manifest_epoch
        or board_signature != BOARD_SIGNATURE
        or layout_signature != LAYOUT_SIGNATURE
        or verifier_signature != VERIFIER_SIGNATURE
        or signed_release_version != release_version
        or signature.get("required") is not True
        or int(signature.get("signed_context_size", -1)) != 16
        or signature.get("signed_board") != SIGNED_BOARD
        or signature.get("signed_layout") != SIGNED_LAYOUT
        or int(signature.get("board_signature", -1)) != BOARD_SIGNATURE
        or int(signature.get("layout_signature", -1)) != LAYOUT_SIGNATURE
        or int(signature.get("verifier_signature", -1)) != VERIFIER_SIGNATURE
        or int(signature.get("signed_prefix_size", -1)) != SIGNATURE_PREFIX_SIZE
        or int(signature.get("signed_trailer_size", -1)) != SIGNATURE_TRAILER_SIZE
    ):
        raise ValueError("TLSR8359 package publisher signature metadata is invalid")
    r = int.from_bytes(raw_signature[:32], "big")
    s = int.from_bytes(raw_signature[32:], "big")
    if r == 0 or s == 0 or s > P256_ORDER // 2:
        raise ValueError("TLSR8359 package publisher signature is non-canonical")
    try:
        key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public_key)
        key.verify(
            encode_dss_signature(r, s),
            image[: application_size + SIGNATURE_PREFIX_SIZE],
            ec.ECDSA(hashes.SHA256()),
        )
    except (InvalidSignature, ValueError) as error:
        raise ValueError("TLSR8359 package publisher signature verification failed") from error


def validate_application(path: Path) -> bytes:
    if path.suffix.lower() != ".zip":
        raise ValueError("TLSR8359 OTA requires a signed manifest-bearing ZIP package")
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        if (
            manifest.get("format") != "inktag-ota-v1"
            or manifest.get("transport") != "telink-fixed-recovery-v2"
            or manifest.get("layout") != SIGNED_LAYOUT
            or manifest.get("board") != SIGNED_BOARD
        ):
            raise ValueError("not an InkTag Hanshow TLSR8359 fixed-Recovery package")
        application = manifest["application"]
        validate_tlsr_data_layout(application)
        image = package.read(application["file"])
    image = validate_application_bytes(image)
    validate_publisher_signature(image, manifest)
    declared_size = struct.unpack_from("<I", image, 24)[0]
    application_size = declared_size + 4
    crc = zlib.crc32(image[:declared_size]) & 0xFFFFFFFF
    if (
        application_size != int(application.get("size", -1))
        or len(image) != int(application.get("transfer_size", -1))
        or declared_size != int(application.get("declared_size", -1))
        or crc != int(application.get("crc32", -1))
        or int(application.get("active_address", -1)) != 0
        or int(application.get("slot_size", -1)) != APP_LIMIT
        or int(application.get("region_size", -1)) != APP_LIMIT
        or int(application.get("recovery_address", -1)) != 0x40000
        or application.get("recovery_state_addresses") != [0x50000, 0x51000]
        or hashlib.sha256(image).hexdigest() != application.get("sha256")
    ):
        raise ValueError("TLSR8359 package manifest or SHA-256 does not match its application")
    return image


def client_options(timeout: float, uncached_services: bool = True) -> dict:
    options: dict = {"timeout": timeout}
    if sys.platform == "win32" and uncached_services:
        options["winrt"] = {"use_cached_services": False}
    return options


@asynccontextmanager
async def connected_client(device, timeout: float):
    """Connect with the same WinRT cache recovery used by the main CLI."""
    from bleak import BleakClient
    from inktag_cli.transport import release_bleak_client

    attempts = (True, False) if sys.platform == "win32" else (False,)
    last_error: BaseException | None = None
    client = None
    for uncached_services in attempts:
        client = BleakClient(
            device, **client_options(timeout, uncached_services)
        )
        try:
            await client.connect()
            break
        except (Exception, asyncio.CancelledError) as error:
            last_error = error
            try:
                await release_bleak_client(client)
            except Exception:
                pass
            client = None
    if client is None:
        detail = str(last_error).strip() if last_error is not None else ""
        raise RuntimeError(
            "GATT service discovery failed with both fresh and cached services"
            + (f": {detail}" if detail else "")
        ) from last_error
    try:
        yield client
    finally:
        try:
            await release_bleak_client(client)
        except Exception:
            pass


def advertised_name(device, advertisement) -> str:
    return (
        getattr(advertisement, "local_name", None)
        or getattr(device, "name", None)
        or ""
    ).strip()


def is_inktag_advertisement(advertisement) -> bool:
    services = {
        str(value).lower()
        for value in (getattr(advertisement, "service_uuids", None) or ())
    }
    manufacturer = (
        getattr(advertisement, "manufacturer_data", None) or {}
    ).get(INKTAG_MANUFACTURER_ID)
    return (
        INKTAG_SERVICE in services
        and manufacturer is not None
        and bytes(manufacturer).startswith(INKTAG_MANUFACTURER_MARKER)
    )


def discovery_candidates(discovered, name: str):
    """Prefer named tags, with a strict sole-device fallback for old TLSR apps."""
    named = []
    fallback = []
    accepted_prefixes = (
        (DEFAULT_NAME_PREFIX, LEGACY_NAME_PREFIX)
        if name == DEFAULT_NAME_PREFIX else (name,)
    )
    for device, advertisement in discovered.values():
        shown_name = advertised_name(device, advertisement)
        if any(shown_name.startswith(prefix) for prefix in accepted_prefixes):
            named.append((device, shown_name))
        elif name == DEFAULT_NAME_PREFIX and is_inktag_advertisement(advertisement):
            fallback.append((device, shown_name))
    return (named, False) if named else (fallback, bool(fallback))


async def discover_one(address: str | None, name: str, timeout: float):
    from bleak import BleakScanner

    if address:
        # Use the same direct-address path as every other maintained CLI
        # command. On Windows a retained GATT link can make a reachable tag
        # disappear from discovery, so requiring another advertisement here
        # incorrectly blocks Recovery/OTA takeover.
        from inktag_cli.transport import InkTagError, select_device

        try:
            device, _advertisement = await select_device(None, address, timeout)
        except InkTagError as error:
            raise RuntimeError(f"no BLE device matched {address}") from error
        return device
    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
    matches, used_fallback = discovery_candidates(discovered, name)
    if not matches:
        suffix = (
            " or the InkTag service/manufacturer advertisement"
            if name == DEFAULT_NAME_PREFIX else ""
        )
        raise RuntimeError(f"no BLE device matched name prefix {name!r}{suffix}")
    if len(matches) > 1:
        found = ", ".join(
            f"{shown_name or '(unnamed)'} ({device.address})"
            for device, shown_name in matches
        )
        raise RuntimeError(f"multiple devices matched; pass --address: {found}")
    device, shown_name = matches[0]
    if used_fallback:
        print(
            "Found an unnamed InkTag advertisement by service UUID and "
            f"manufacturer marker: {device.address}"
        )
    return device


def characteristic(client, uuid: str):
    return client.services.get_characteristic(uuid)


async def request_v2(client, opcode: int, transaction: int, payload: bytes = b"") -> bytes:
    responses: asyncio.Queue[bytes] = asyncio.Queue()

    def notification(_sender, value: bytearray) -> None:
        packet = bytes(value)
        if len(packet) >= 3 and packet[0] == opcode and packet[1] == transaction:
            responses.put_nowait(packet)

    await client.start_notify(INKTAG_COMMAND, notification)
    try:
        # TLSR needs one connection event after the CCCD write before the
        # first notification can be relied on.
        await asyncio.sleep(0.50)
        packet = bytes((opcode, transaction, 0)) + payload
        await client.write_gatt_char(INKTAG_COMMAND, packet, response=True)
        try:
            response = await asyncio.wait_for(responses.get(), 8.0)
        except TimeoutError as error:
            raise RuntimeError(
                f"timed out waiting for InkTag V2 opcode 0x{opcode:02X}"
            ) from error
        if response[2] != STATUS_OK:
            raise InkTagV2StatusError(response[2], opcode,
                f"InkTag command 0x{opcode:02X} failed with status {response[2]}"
            )
        return response[3:]
    finally:
        if client.is_connected:
            try:
                await client.stop_notify(INKTAG_COMMAND)
            except Exception:
                pass


async def current_role(device, connect_timeout: float) -> str:
    async with connected_client(device, connect_timeout) as client:
        if characteristic(client, OTA_CHARACTERISTIC) is not None:
            return "recovery"
        if characteristic(client, INKTAG_COMMAND) is not None:
            return "application"
    return "unknown"


async def wait_for_role(
    address: str, role: str, timeout: float, connect_timeout: float
):
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        try:
            device = await discover_one(address, "", min(4.0, remaining))
            if await current_role(device, connect_timeout) == role:
                return device
        except Exception as error:
            last_error = error
        await asyncio.sleep(0.5)
    detail = f": {last_error}" if last_error else ""
    raise RuntimeError(f"device did not expose the {role} role before timeout{detail}")


async def resume_safe_application(
    name: str | None, address: str | None, scan_timeout: float,
    connect_timeout: float, reconnect_timeout: float,
) -> dict:
    """Ask an already-running fixed Recovery to restore its untouched App."""
    if name and not name.endswith("_OTA"):
        raise RuntimeError(
            "TLSR safe resume requires the fixed Recovery _OTA name; it never "
            "moves an application into Recovery"
        )
    device = await discover_one(address, name or DEFAULT_NAME_PREFIX, scan_timeout)
    resolved_address = device.address
    async with connected_client(device, connect_timeout) as client:
        ota = characteristic(client, OTA_CHARACTERISTIC)
        if ota is None:
            raise RuntimeError(
                "selected device is not fixed TLSR Recovery; this command does "
                "not reboot an application into Recovery"
            )
        status = bytes(await client.read_gatt_char(ota))
        if (len(status) < 12 or status[8:10] != FAST_STATUS_MAGIC or
                status[10] < 3):
            raise RuntimeError(
                "fixed TLSR Recovery does not support guarded safe resume; "
                "install the current Recovery once with SWS"
            )
        await client.write_gatt_char(
            ota, RECOVERY_CONTROL_SAFE_RESUME_APP, response=True
        )
        deadline = time.monotonic() + 15.0
        while True:
            await asyncio.sleep(0.05)
            status = bytes(await client.read_gatt_char(ota))
            code = status[0] if status else -1
            if code == RECOVERY_STATUS_SAFE_RESUME_COMPLETE:
                break
            if code == RECOVERY_STATUS_LOW_POWER:
                raise RuntimeError(
                    "SAFE_RESUME_APP rejected: LOW_POWER or invalid voltage sample; "
                    "Recovery remains read-only"
                )
            if code == RECOVERY_STATUS_UNSAFE_RESUME:
                raise RuntimeError(
                    "SAFE_RESUME_APP rejected: application, request marker, "
                    "transaction, or Recovery journal is not safe"
                )
            if code != RECOVERY_STATUS_RECEIVING:
                raise RuntimeError(
                    f"SAFE_RESUME_APP rejected: Recovery status {status.hex()}"
                )
            if time.monotonic() >= deadline:
                raise RuntimeError("SAFE_RESUME_APP validation timed out")
    application = await wait_for_role(
        resolved_address, "application", reconnect_timeout, connect_timeout
    )
    return {
        "ok": True,
        "action": "safe-resume-app",
        "platform": "telink",
        "address": resolved_address,
        "applicationName": application.name or "",
    }


async def enter_recovery(device, connect_timeout: float, password_data_dir: Path | None = None) -> str:
    """Detect the initial role and request Recovery without a reconnect gap."""
    async with connected_client(device, connect_timeout) as client:
        if characteristic(client, OTA_CHARACTERISTIC) is not None:
            return "recovery"
        if characteristic(client, INKTAG_COMMAND) is None:
            return "unknown"
        print(f"Request fixed Recovery on {device.name} ({device.address})")
        version = bytes(await client.read_gatt_char(INKTAG_VERSION))
        if len(version) < 1 or version[0] != 0x20:
            raise RuntimeError("device does not advertise InkTag protocol v2")
        transaction = 20
        async def password_request(opcode, payload=b""):
            nonlocal transaction
            transaction += 1
            return await request_v2(client, opcode, transaction, payload)
        await request_v2(client, CMD_HELLO, 19)
        await authorize_ota_application(password_request, password_data_dir)
        # Current v2 firmware leaves a bounded 500 ms acknowledgement window.
        # Read the command result so a board-level capability denial is
        # reported directly instead of being misdiagnosed as missing Recovery.
        await request_v2(
            client, CMD_SYSTEM_ACTION, 2,
            bytes((SYSTEM_ACTION_OTA_RECOVERY,)),
        )
        await asyncio.sleep(0.15)
    return "application"


async def upload_recovery(
    device,
    image: bytes,
    connect_timeout: float,
    start_delay: float,
    sector_erase_delay: float,
    resume: bool = False,
    single_packet_window: bool = False,
) -> None:
    padded = image + b"\xFF" * ((-len(image)) % 16)
    blocks = len(padded) // 16
    print(f"Connect fixed Recovery {device.name} ({device.address})")
    started = time.monotonic()
    async with connected_client(device, connect_timeout) as client:
        ota = characteristic(client, OTA_CHARACTERISTIC)
        if ota is None:
            raise RuntimeError("fixed Recovery does not expose the Telink OTA characteristic")
        properties = set(getattr(ota, "properties", ()) or ())
        if "write" not in properties or "write-without-response" not in properties:
            raise RuntimeError(
                "fixed Recovery does not expose the current WRITE and WRITE WITHOUT RESPONSE contract"
            )
        use_notifications = "notify" in properties
        status_updates: asyncio.Queue[bytes] = asyncio.Queue()
        if use_notifications:
            def status_notification(_, data: bytearray) -> None:
                status_updates.put_nowait(bytes(data))

            await client.start_notify(ota, status_notification)
        async def begin_transfer() -> bytes:
            await client.write_gatt_char(
                ota, b"\x01\xFF", response=True
            )
            await asyncio.sleep(start_delay)
            started_status = bytes(await client.read_gatt_char(ota))
            if started_status[:1] != bytes((RECOVERY_STATUS_RECEIVING,)):
                raise RuntimeError(
                    f"fixed Recovery rejected OTA start: status {started_status.hex()}"
                )
            if recovery_acknowledged_index(started_status) != 0:
                raise RuntimeError(
                    f"fixed Recovery started with a stale index: {started_status.hex()}"
                )
            return started_status

        if resume:
            status = bytes(await client.read_gatt_char(ota))
            if status[:1] == bytes((RECOVERY_STATUS_COMPLETE,)):
                print("  Recovery already accepted the complete image")
                return
            if status[:1] != bytes((RECOVERY_STATUS_RECEIVING,)):
                status = await begin_transfer()
        else:
            status = await begin_transfer()
        write_limit = int(getattr(ota, "max_write_without_response_size", 20))
        fast_blocks = recovery_fast_block_limit(status, write_limit)
        print(
            f"  transport v2: {fast_blocks * 16}-byte payloads "
            f"(ATT limit {write_limit}, sector delivery "
            f"confirmed, "
            f"flow {'notify' if use_notifications else 'read'})"
        )
        index = recovery_acknowledged_index(status)
        if index > blocks:
            raise RuntimeError(
                f"fixed Recovery acknowledged block {index} beyond image end {blocks}"
            )
        resumed_transfer = single_packet_window or (resume and index != 0)
        if resume and index != 0:
            print(f"  resume from committed block {index}/{blocks}")
        while index < blocks:
            # Resume/restart after backpressure uses one packet per window.
            # large packet per indexed window thereafter so WinRT cannot drop
            # the second queue entry repeatedly on the same radio path.
            packet_ranges = fast_packet_window(
                index, blocks, fast_blocks,
                1 if resumed_transfer else FAST_WINDOW_PACKETS,
            )
            for packet_start, packet_end in packet_ranges:
                payload = padded[packet_start * 16 : packet_end * 16]
                await client.write_gatt_char(
                    ota, data_packet(packet_start, payload),
                    response=packet_requires_delivery_ack(packet_start),
                )
            batch_end = packet_ranges[-1][1]
            if packet_ranges[0][0] % FLASH_SECTOR_BLOCKS == 0:
                # Recovery erases a 4 KiB sector synchronously when it consumes
                # the first block. Keep that block isolated and retain the
                # qualified erase allowance.
                await asyncio.sleep(sector_erase_delay)
            # Recovery-v2 has two usable queue entries, so ordinary traffic
            # sends at most two large packets before reading the exact
            # committed index. A sector-opening packet stays isolated and uses
            # an ATT write response so the erase allowance starts only after
            # the controller has actually delivered that packet.
            deadline = time.monotonic() + 3.0
            while True:
                if use_notifications:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise RuntimeError(
                            f"fixed Recovery did not notify blocks "
                            f"{index}..{batch_end - 1}"
                        )
                    try:
                        status = await asyncio.wait_for(
                            status_updates.get(), timeout=remaining
                        )
                    except TimeoutError:
                        status = bytes(await client.read_gatt_char(ota))
                        print(
                            "  notification unavailable; using indexed reads "
                            f"for this connection (status {status.hex()})"
                        )
                        use_notifications = False
                else:
                    status = bytes(await client.read_gatt_char(ota))
                if status[:1] != bytes((RECOVERY_STATUS_RECEIVING,)):
                    if recovery_queue_full(status, index, batch_end):
                        raise RecoveryQueueFullError(
                            f"fixed Recovery queue full at blocks "
                            f"{index}..{batch_end - 1}: status {status.hex()}"
                        )
                    raise RuntimeError(
                        f"fixed Recovery rejected blocks {index}..{batch_end - 1}: "
                        f"status {status.hex()}"
                    )
                acknowledged = recovery_acknowledged_index(status)
                if acknowledged == batch_end:
                    break
                if acknowledged > batch_end or time.monotonic() >= deadline:
                    raise RuntimeError(
                        f"fixed Recovery did not acknowledge blocks "
                        f"{index}..{batch_end - 1}: "
                        f"status {status.hex()}"
                    )
                if not use_notifications:
                    await asyncio.sleep(0.005)
            if index == 0 or batch_end % 256 == 0 or batch_end == blocks:
                print(f"  {batch_end}/{blocks} blocks ({batch_end * 100 // blocks}%)")
            index = batch_end
        # Make the end marker a separate, observable queue item even when the
        # final data block did not coincide with a sector boundary.
        await asyncio.sleep(0.05)
        status = bytes(await client.read_gatt_char(ota))
        if status[:1] != bytes((RECOVERY_STATUS_RECEIVING,)):
            raise RuntimeError(
                f"fixed Recovery rejected the transfer before finish: status {status.hex()}"
            )
        maximum = blocks - 1
        end = b"\x02\xFF" + struct.pack("<HH", maximum, maximum ^ 0xFFFF)
        await client.write_gatt_char(ota, end, response=True)
        # Recovery hashes the installed bytes cooperatively in 64-byte steps
        # and then performs one P-256 verification. Keep polling while it is
        # truthfully still RECEIVING instead of treating a normal validation
        # interval as a failed OTA.
        validation_deadline = time.monotonic() + 10.0
        while True:
            await asyncio.sleep(0.05)
            status = bytes(await client.read_gatt_char(ota))
            if status[:1] == bytes((RECOVERY_STATUS_COMPLETE,)):
                break
            if status[:1] != bytes((RECOVERY_STATUS_RECEIVING,)):
                raise RuntimeError(
                    f"fixed Recovery rejected the image: status {status.hex()}"
                )
            if time.monotonic() >= validation_deadline:
                raise RuntimeError("fixed Recovery image validation timed out")
    print(f"Recovery accepted and armed the image in {time.monotonic() - started:.1f}s")


async def attest_application(
    device, image: bytes, manifest: dict, connect_timeout: float,
    password_data_dir: Path | None = None,
) -> None:
    async with connected_client(device, connect_timeout) as client:
        version = bytes(await client.read_gatt_char(INKTAG_VERSION))
        try:
            release_parts = tuple(int(part) for part in manifest["release"].split("."))
        except (KeyError, ValueError, AttributeError) as error:
            raise RuntimeError("OTA manifest release version is invalid") from error
        if len(release_parts) != 3 or any(not 0 <= part <= 0xFF for part in release_parts):
            raise RuntimeError("OTA manifest release version is outside the GATT range")
        expected_gatt_version = bytes((int(manifest["protocol"]), *release_parts))
        if version != expected_gatt_version:
            raise RuntimeError(
                "updated application version attestation mismatch: "
                f"device={version.hex()} package={expected_gatt_version.hex()}"
            )
        hello = await request_v2(client, CMD_HELLO, 3)
        if len(hello) < 2 or hello[1] != 0x20:
            raise RuntimeError("updated application returned an invalid V2 handshake")
        transaction = 20
        async def password_request(opcode, payload=b""):
            nonlocal transaction
            transaction += 1
            return await request_v2(client, opcode, transaction, payload)
        await authorize_ota_application(password_request, password_data_dir)
        info = await request_v2(client, CMD_GET_DEVICE_INFO, 4)
        if len(info) < 23 or info[0] != 1:
            raise RuntimeError("updated application returned invalid build identity")
        installed_size, installed_crc = struct.unpack_from("<II", info, 15)
        installed_release = struct.unpack_from("<I", info, 11)[0]
        expected_release = (
            (release_parts[0] << 16) | (release_parts[1] << 8) |
            release_parts[2]
        )
        # The transfer appends the 96-byte publisher trailer after Telink's
        # boot-visible image. Device identity continues to attest the declared
        # application body and its native CRC, not transport metadata.
        expected_size = struct.unpack_from("<I", image, 24)[0]
        expected_crc = zlib.crc32(image[:expected_size]) & 0xFFFFFFFF
        if (installed_release, installed_size, installed_crc) != (
            expected_release, expected_size, expected_crc
        ):
            raise RuntimeError(
                "updated application attestation mismatch: "
                f"device={installed_release:06X}/{installed_size}/"
                f"{installed_crc:08X}, package={expected_release:06X}/"
                f"{expected_size}/{expected_crc:08X}"
            )
    print(
        f"Updated application attested: {expected_size} bytes, CRC32 {expected_crc:08X}"
    )


async def run(args: argparse.Namespace) -> None:
    image = validate_application(args.package)
    with zipfile.ZipFile(args.package) as package:
        manifest = json.loads(package.read("manifest.json"))
    device = await discover_one(args.address, args.name, args.scan_timeout)
    address = device.address
    role = await enter_recovery(device, args.connect_timeout, getattr(args, "password_data_dir", None))
    if role == "application":
        device = await wait_for_role(
            address, "recovery", args.reconnect_timeout, args.connect_timeout
        )
    elif role != "recovery":
        raise RuntimeError("device exposes neither InkTag application nor fixed Recovery")
    single_packet_window = False
    restart_transfer = False
    for attempt in range(1, TRANSFER_ATTEMPTS + 1):
        try:
            await upload_recovery(
                device, image, args.connect_timeout, args.start_delay,
                args.sector_erase_delay,
                resume=(role == "recovery" or attempt > 1) and not restart_transfer,
                single_packet_window=single_packet_window,
            )
            break
        except Exception as error:
            queue_retry = isinstance(error, RecoveryQueueFullError) and not single_packet_window
            if attempt == TRANSFER_ATTEMPTS or (
                not queue_retry and not is_transient_ble_error(error)
            ):
                raise
            if queue_retry:
                single_packet_window = True
                print(
                    f"Recovery queue full ({error}); reconnecting once and "
                    "restarting the same validated image with a single-packet window"
                )
            else:
                print(
                    f"Transient BLE loss during OTA ({error}); transfer attempt "
                    f"{attempt}/{TRANSFER_ATTEMPTS} failed, reconnecting and "
                    "resuming from the last committed block"
                )
            restart_transfer = queue_retry
            await asyncio.sleep(1.0)
            device = await wait_for_role(
                address, "recovery", args.reconnect_timeout,
                args.connect_timeout
            )
    if args.skip_reconnect_check:
        return
    device = await wait_for_role(
        address, "application", args.reconnect_timeout, args.connect_timeout
    )
    await attest_application(device, image, manifest, args.connect_timeout, getattr(args, "password_data_dir", None))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--address")
    parser.add_argument("--password-data-dir", type=Path)
    parser.add_argument("--name", default=DEFAULT_NAME_PREFIX)
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=20.0)
    parser.add_argument("--reconnect-timeout", type=float, default=60.0)
    parser.add_argument("--start-delay", type=float, default=0.25)
    parser.add_argument("--sector-erase-delay", type=float, default=0.300)
    parser.add_argument("--skip-reconnect-check", action="store_true")
    return parser.parse_args()


def main() -> int:
    try:
        asyncio.run(run(parse_args()))
    except Exception as error:
        detail = str(error).strip() or repr(error)
        print(f"error: {type(error).__name__}: {detail}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
