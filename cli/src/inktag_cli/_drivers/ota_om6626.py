#!/usr/bin/env python3
"""Canonical InkTag CLI driver for OM6626 fixed BLE Recovery."""

import argparse
import asyncio
import hashlib
import json
import re
import struct
import sys
import time
import zipfile
from pathlib import Path

from bleak import BleakClient, BleakScanner
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

from inktag_cli.generated_commands_v2 import StatusV2
from inktag_cli.ota_rejection import describe_ota_rejection


SERVICE_UUID = "000001bf-5f00-0000-0000-4f4d2d4f5441"
APPLICATION_SERVICE_UUID = "62750001-d828-918d-fb46-b6c11c675aec"
CONTROL_UUID = "000001bf-5f01-0000-0000-4f4d2d4f5441"
DATA_UUID = "000001bf-5f02-0000-0000-4f4d2d4f5441"
RECOVERY_PROFILE_UUID = "000001bf-5f03-0000-0000-4f4d2d4f5441"
INKTAG_CHAR_UUID = "62750002-d828-918d-fb46-b6c11c675aec"
INKTAG_VERSION_UUID = "62750003-d828-918d-fb46-b6c11c675aec"
IMAGE_TYPE_CUSTOM = 0x10
COMMON_APPLICATION_BOARD = "om6626_zkcxxb"
RUNTIME_BOARDS = {"om6626_zkc21b", "om6626_zkc21v", "om6626_zkc42b"}
RESPONSE, SUCCESS = 0x60, 0x01
DFU_OPERATION_FAILED = 0x0A
CREATE, SET_PRN, CHECKSUM, EXECUTE, SELECT = 0x01, 0x02, 0x03, 0x04, 0x06
SAFE_RESUME_APP = 0x07
OBJECT_COMMAND, OBJECT_DATA = 0x01, 0x02
CHECKPOINT_PACKETS = 8
V2_GET_DEVICE_INFO = 0x03
V2_SYSTEM_ACTION = 0x0E
V2_SYSTEM_ACTION_OTA_RECOVERY = 0x04
V2_STATUS_OK = 0x00
SIGNATURE_MAGIC = 0x31534B49
SIGNATURE_FORMAT_VERSION = 2
SIGNATURE_PREFIX_SIZE = 32
SIGNATURE_SIZE = 64
SIGNATURE_TRAILER_SIZE = 96
BOARD_SIGNATURE = 0x42584D4F
LAYOUT_SIGNATURE = 0x31524D4F
VERIFIER_SIGNATURE = 0x314B4D4F
SIGNED_LAYOUT = "single-with-ble-recovery-v1"
LAYOUT_PROFILES = {
    SIGNED_LAYOUT: {"signature": LAYOUT_SIGNATURE, "address": 0x2A000, "slot": 0x48000},
    "single-with-ble-recovery-v2": {
        "signature": 0x32524D4F, "address": 0x2B000, "slot": 0x4A000,
    },
}
TRUSTED_SIGNING_KEY = {
    "trust_profile": "production",
    "algorithm": "ecdsa-p256-sha256",
    "key_id": "inktag-om6626-ota-v1",
    "public_key_hash": "fc0e79e61ce05df76dcff00bf122e86e",
}

OM_RECOVERY_NAME = re.compile(r"^(?:IKT|INKTAG)_OM_([0-9A-F]{4})_OTA$")
LEGACY_RECOVERY_NAMES = {"INKTAG_OTA", "INKTAG_OM_OTA"}


def stable_id4_from_address(address: str) -> str | None:
    """Derive OM's stable advertised suffix from a public MAC address."""
    fields = address.replace("-", ":").split(":")
    if len(fields) != 6 or any(
            len(field) != 2 or any(ch not in "0123456789abcdefABCDEF" for ch in field)
            for field in fields):
        return None
    return (fields[-2] + fields[-1]).upper()


def stable_id4_from_name(advertised_name: str) -> str | None:
    match = re.fullmatch(
        r"(?:IKT|INKTAG)_OM_([0-9A-F]{4})(?:_OTA)?", advertised_name.upper()
    )
    return match.group(1) if match else None


def advertisement_matches_role(advertised_name: str, recovery: bool,
                               service_uuids=(), expected_id4: str | None = None,
                               allow_legacy_recovery: bool = False) -> bool:
    """Validate an OM role from both its service and role-specific name."""
    advertised_services = {str(value).lower() for value in service_uuids}
    expected_service = SERVICE_UUID if recovery else APPLICATION_SERVICE_UUID
    if expected_service not in advertised_services:
        return False
    normalized = advertised_name.upper()
    if recovery:
        if expected_id4 and normalized in {
                f"IKT_OM_{expected_id4.upper()}_OTA",
                f"INKTAG_OM_{expected_id4.upper()}_OTA"}:
            return True
        if expected_id4:
            return allow_legacy_recovery and normalized in LEGACY_RECOVERY_NAMES
        if OM_RECOVERY_NAME.fullmatch(normalized):
            return True
        return allow_legacy_recovery and normalized in LEGACY_RECOVERY_NAMES
    if not normalized.startswith(("IKT_", "INKTAG_")) or normalized.endswith("_OTA"):
        return False
    return True


def ble_client_options(timeout: float, *, fresh_services: bool = False) -> dict:
    options = {"timeout": timeout}
    if fresh_services and sys.platform == "win32":
        # Application and Recovery intentionally share one BLE address but
        # expose different GATT databases. WinRT otherwise reuses the previous
        # role's service cache immediately after the controlled reboot.
        options["winrt"] = {"use_cached_services": False}
    return options


def parse_build_message(message: str) -> dict[str, int] | None:
    if not message.startswith("build="):
        return None
    fields = {}
    for field in message.split():
        if "=" not in field:
            continue
        key, value = field.split("=", 1)
        try:
            fields[key] = int(value, 0)
        except ValueError:
            # Composite diagnostics such as start=0,0,0,0 are useful to a
            # person but are not part of the numeric OTA attestation fields.
            continue
    return fields if "build" in fields else None


def parse_v2_device_info(payload: bytes) -> tuple[str, dict[str, int]]:
    if len(payload) < 23 or payload[0] != 1:
        raise ValueError("device returned invalid V2 identity information")
    board_length, name_length, label_length = payload[1:4]
    strings_end = 23 + board_length + name_length + label_length
    if strings_end > len(payload):
        raise ValueError("device returned invalid V2 identity lengths")
    board = payload[23:23 + board_length].decode("ascii", errors="replace")
    app_size = struct.unpack_from("<I", payload, 15)[0]
    if len(payload) < strings_end + 16 or payload[strings_end] != 1:
        raise ValueError(
            "device does not report V2 OTA Recovery attestation; install the "
            "current board-specific factory image once before using OTA"
        )
    swap_result = payload[strings_end + 1]
    swap_state = payload[strings_end + 2]
    build_version, generation, confirm = struct.unpack_from(
        "<III", payload, strings_end + 4
    )
    return board, {
        "build": build_version,
        "app_bytes": app_size,
        "swap": swap_result,
        "state": swap_state,
        "gen": generation,
        "confirm": confirm,
    }


def crc32(data: bytes, initial: int = 0) -> int:
    crc = (~initial) & 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def package_profile(manifest: dict) -> dict:
    profile = LAYOUT_PROFILES.get(manifest.get("layout"))
    application = manifest.get("application", {})
    if profile is None or manifest.get("board") != COMMON_APPLICATION_BOARD or \
            application.get("image_type") != IMAGE_TYPE_CUSTOM or \
            application.get("active_address") != profile["address"] or \
            application.get("slot_size") != profile["slot"]:
        raise ValueError("package has an incompatible application/Recovery layout")
    if manifest["layout"] != SIGNED_LAYOUT and manifest.get("compatibilityEpoch") != 1:
        raise ValueError("OnMicro layout-v2 requires compatibility epoch 1")
    if manifest["layout"] != SIGNED_LAYOUT and (
            manifest.get("chip") != "OM6626" or
            application.get("recovery_state_addresses") != [0x7D000, 0x7E000] or
            application.get("recovery_request_address") != 0x7F000):
        raise ValueError("OnMicro layout-v2 Recovery context does not match")
    return profile


def validate_recovery_preflight(data: bytes, manifest: dict, firmware: bytes) -> None:
    """Validate fixed Recovery's read-only policy before any DFU mutation."""
    profile = package_profile(manifest)
    expected = struct.pack("<IBBHIII", 0x50524B49, 1, 0, 20,
                           profile["signature"], profile["slot"] - SIGNATURE_TRAILER_SIZE,
                           profile["slot"])
    if manifest["layout"] == SIGNED_LAYOUT or bytes(data) != expected:
        raise ValueError(
            "Recovery preflight is missing or incompatible; a separately authorized "
            "fixed-Recovery Full upgrade is required"
        )
    signature = manifest.get("signature", {})
    if signature.get("signed_board") != COMMON_APPLICATION_BOARD or \
            signature.get("signed_layout") != manifest["layout"] or \
            signature.get("board_signature") != BOARD_SIGNATURE or \
            signature.get("layout_signature") != profile["signature"] or \
            signature.get("verifier_signature") != VERIFIER_SIGNATURE:
        raise ValueError("Recovery preflight package context does not match")
    app_size = manifest["application"].get("size", -1)
    if not 0 < app_size <= profile["slot"] - SIGNATURE_TRAILER_SIZE or \
            len(firmware) != app_size + SIGNATURE_TRAILER_SIZE or \
            manifest["application"].get("transfer_size") != len(firmware):
        raise ValueError("Recovery preflight package exceeds the signed profile")


def load_package(path: Path, *, verify_signature: bool = True) -> tuple[dict, bytes]:
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        application = manifest["application"]
        firmware = package.read(application["file"])
    if (manifest.get("format"), manifest.get("transport")) != (
        "inktag-ota-v1", "onmicro-dfu-v3"
    ):
        raise ValueError("not an InkTag OnMicro single-slot Recovery package")
    if manifest.get("board") != "om6626_zkcxxb":
        raise ValueError("package is not for a supported OM6626 InkTag board")
    app_size = int(application["size"])
    transfer_size = int(application.get("transfer_size", len(firmware)))
    if len(firmware) != transfer_size or len(firmware) > application["slot_size"]:
        raise ValueError("application size does not match package/slot")
    profile = package_profile(manifest)
    if hashlib.sha256(firmware).hexdigest() != application["sha256"]:
        raise ValueError("package SHA-256 verification failed")
    signature = manifest.get("signature")
    if not isinstance(signature, dict) or signature.get("required") is not True:
        raise ValueError("OnMicro OTA package must carry a required publisher signature")
    if signature.get("trust_profile") != TRUSTED_SIGNING_KEY["trust_profile"] or \
            signature.get("algorithm") != TRUSTED_SIGNING_KEY["algorithm"] or \
            signature.get("key_id") != TRUSTED_SIGNING_KEY["key_id"] or \
            str(signature.get("public_key_hash", "")).lower() != \
            TRUSTED_SIGNING_KEY["public_key_hash"]:
        raise ValueError("OnMicro OTA publisher signing key is not trusted")
    if int(signature.get("signed_context_size", 0)) != 16 or \
            signature.get("signed_board") != COMMON_APPLICATION_BOARD or \
            signature.get("signed_layout") != manifest["layout"] or \
            int(signature.get("board_signature", -1)) != BOARD_SIGNATURE or \
            int(signature.get("layout_signature", -1)) != profile["signature"] or \
            int(signature.get("verifier_signature", -1)) != VERIFIER_SIGNATURE or \
            int(signature.get("signed_prefix_size", 0)) != SIGNATURE_PREFIX_SIZE or \
            int(signature.get("signed_trailer_size", 0)) != SIGNATURE_TRAILER_SIZE or \
            app_size + SIGNATURE_TRAILER_SIZE != transfer_size:
        raise ValueError("OnMicro OTA publisher signature trailer is invalid")
    try:
        public_key = bytes.fromhex(str(signature.get("public_key", "")))
    except ValueError as error:
        raise ValueError("OnMicro OTA publisher public key is malformed") from error
    if len(public_key) != 65 or public_key[0] != 0x04 or \
            hashlib.sha256(public_key).digest()[:16].hex() != \
            TRUSTED_SIGNING_KEY["public_key_hash"]:
        raise ValueError("OnMicro OTA publisher public key is not trusted")
    trailer = firmware[app_size:]
    (magic, format_version, algorithm, trailer_size, signed_size,
     compatibility_epoch, board_signature, layout_signature,
     verifier_signature, signed_release_version) = \
        struct.unpack_from("<IBBHIIIIII", trailer)
    release_version = int(manifest["releaseVersion"])
    manifest_epoch = int(manifest["compatibilityEpoch"])
    if not 0 <= manifest_epoch <= 0xFFFF:
        raise ValueError("OnMicro compatibility epoch exceeds the product contract")
    if magic != SIGNATURE_MAGIC or format_version != SIGNATURE_FORMAT_VERSION or algorithm != 1 or \
            trailer_size != SIGNATURE_TRAILER_SIZE or signed_size != app_size or \
            compatibility_epoch != manifest_epoch or \
            board_signature != BOARD_SIGNATURE or \
            layout_signature != profile["signature"] or \
            signed_release_version != release_version or \
            verifier_signature != VERIFIER_SIGNATURE:
        raise ValueError("OnMicro OTA signed trailer does not match the manifest")
    raw_signature = trailer[SIGNATURE_PREFIX_SIZE:]
    if len(raw_signature) != SIGNATURE_SIZE or not any(raw_signature):
        raise ValueError("OnMicro OTA ECDSA signature is missing")
    verification_key = ec.EllipticCurvePublicKey.from_encoded_point(
        ec.SECP256R1(), public_key
    )
    if verify_signature:
        try:
            verification_key.verify(
                encode_dss_signature(
                    int.from_bytes(raw_signature[:32], "big"),
                    int.from_bytes(raw_signature[32:], "big"),
                ),
                firmware[:app_size + SIGNATURE_PREFIX_SIZE],
                ec.ECDSA(hashes.SHA256()),
            )
        except InvalidSignature as error:
            raise ValueError("OnMicro OTA ECDSA P-256 signature verification failed") from error
    return manifest, firmware


def is_expected_signature_rejection(error: Exception) -> bool:
    message = str(error)
    return ("reason=CHECKSUM_OR_SIGNATURE_FAILED" in message and
            "stage=SIGNATURE_VERIFY" in message)


def command_object(manifest: dict, firmware: bytes) -> bytes:
    return struct.pack(
        "<IIHHIII", 0x01BFDF55, 3, 0, 1, IMAGE_TYPE_CUSTOM,
        int(manifest["releaseVersion"]), len(firmware)
    )


class Transport:
    def __init__(self, client: BleakClient):
        self.client = client
        self.responses: asyncio.Queue[bytes] = asyncio.Queue()

    def notification(self, _sender, data: bytearray) -> None:
        self.responses.put_nowait(bytes(data))

    async def open(self) -> None:
        await self.client.start_notify(CONTROL_UUID, self.notification)

    async def close(self) -> None:
        if self.client.is_connected:
            try:
                await self.client.stop_notify(CONTROL_UUID)
            except Exception:
                pass

    async def request(self, payload: bytes, timeout: float = 15.0) -> bytes:
        opcode = payload[0]
        await self.client.write_gatt_char(CONTROL_UUID, payload, response=True)
        while True:
            response = await asyncio.wait_for(self.responses.get(), timeout)
            if len(response) >= 3 and response[0] == RESPONSE and response[1] == opcode:
                if response[2] != SUCCESS:
                    operation = {
                        CREATE: "CREATE",
                        CHECKSUM: "CHECKSUM",
                        EXECUTE: "EXECUTE",
                        SELECT: "SELECT",
                        SAFE_RESUME_APP: "SAFE_RESUME_APP",
                    }.get(opcode, f"opcode 0x{opcode:02X}")
                    raise RuntimeError(describe_ota_rejection(
                        operation, response[2:],
                        error_status=DFU_OPERATION_FAILED,
                    ))
                return response

    async def resume_application_if_safe(self) -> None:
        """Ask idle Recovery to boot an untouched, locally valid application."""
        await self.request(bytes((SAFE_RESUME_APP,)))

    async def checksum(self, offset: int, crc: int) -> None:
        response = await self.request(bytes((CHECKSUM,)), 30.0)
        if len(response) < 11:
            raise RuntimeError("checksum response is truncated")
        actual_offset, actual_crc = struct.unpack_from("<II", response, 3)
        if (actual_offset, actual_crc) != (offset, crc):
            raise RuntimeError(f"checksum mismatch at {actual_offset}/{offset}")

    async def send_object(self, object_type: int, data: bytes, packet_size: int,
                          progress, base: int, total: int) -> None:
        await self.request(bytes((SELECT, object_type)))
        await self.request(struct.pack("<BBI", CREATE, object_type, len(data)))
        await self.request(bytes((SET_PRN, 0, 0)))
        offset = 0
        crc = 0
        while offset < len(data):
            chunk = data[offset:offset + packet_size]
            await self.client.write_gatt_char(DATA_UUID, chunk, response=False)
            crc = crc32(chunk, crc)
            offset += len(chunk)
            if offset == len(data) or offset % (packet_size * CHECKPOINT_PACKETS) == 0:
                await self.checksum(offset, crc)
            progress(base + offset, total)
        try:
            await self.request(bytes((EXECUTE,)), 45.0 if object_type == OBJECT_DATA else 15.0)
        except Exception:
            # The final data-object EXECUTE commits the swap metadata and
            # immediately reboots.  Windows can report the intentional link
            # loss before delivering the success notification.
            if object_type != OBJECT_DATA:
                raise
            await asyncio.sleep(0.5)
            if self.client.is_connected:
                raise


async def discover(name: str | None, address: str | None, timeout: float,
                   include_role: bool = False,
                   allow_legacy_recovery: bool = False):
    expected_address = address.upper() if address else None
    deadline = time.monotonic() + timeout
    candidates = {}
    visible_inktags = {}
    observations = {}

    # A single Windows discovery pass can omit a peripheral that is between
    # advertising events. Keep the BLEDevice from a live advertisement, since
    # connecting BleakClient to a bare address is also unreliable under WinRT.
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        devices = await BleakScanner.discover(
            timeout=min(4.0, max(1.0, remaining)), return_adv=True
        )
        for device, advertisement in devices.values():
            advertised_name = advertisement.local_name or device.name or ""
            device_address = device.address.upper()
            services = {value.lower() for value in advertisement.service_uuids or []}
            previous_name, previous_services = observations.get(device_address, ("", set()))
            advertised_name = advertised_name or previous_name
            services |= previous_services
            observations[device_address] = (advertised_name, services)
            is_inktag = (
                SERVICE_UUID in services or APPLICATION_SERVICE_UUID in services or
                advertised_name.upper().startswith(("IKT_", "INKTAG_"))
            )
            if is_inktag:
                visible_inktags[device_address] = advertised_name or "(unnamed)"
            if expected_address:
                expected_id4 = (stable_id4_from_address(device_address) or
                                stable_id4_from_name(advertised_name))
                is_recovery = advertisement_matches_role(
                    advertised_name, True, services, expected_id4,
                    allow_legacy_recovery
                )
                is_application = advertisement_matches_role(
                    advertised_name, False, services
                )
                # An exact OS identity is authoritative for the ordinary
                # application even after a Pro rename. Recovery still has to
                # prove both its dedicated service and immutable suffix.
                if (device_address == expected_address and
                        (is_recovery or is_application)):
                    print(f"Found: {advertised_name or '(unnamed)'} [{device.address}]")
                    if include_role:
                        return device, is_recovery
                    return device
                continue
            requested_names = (
                ("IKT_OM_", "INKTAG_OM_")
                if name and name.upper() == "IKT_OM_" else (name.upper(),) if name else ()
            )
            if requested_names and not advertised_name.upper().startswith(requested_names):
                continue
            if is_inktag:
                candidates[device_address] = (device, advertised_name)
        if expected_address:
            await asyncio.sleep(0.2)

    if expected_address:
        visible = ", ".join(
            f"{visible_name} [{visible_address}]"
            for visible_address, visible_name in sorted(visible_inktags.items())
        ) or "none"
        raise RuntimeError(
            f"OM6626 InkTag {address} was not found while advertising; "
            f"visible InkTag devices: {visible}"
        )
    if not candidates:
        raise RuntimeError("no OM6626 InkTag device found")
    if len(candidates) > 1:
        visible = ", ".join(
            f"{candidate_name or '(unnamed)'} [{candidate.address}]"
            for candidate, candidate_name in candidates.values()
        )
        raise RuntimeError(
            f"multiple OM6626 devices found ({visible}); pass an exact --address"
        )
    device, advertised_name = next(iter(candidates.values()))
    print(f"Found: {advertised_name or '(unnamed)'} [{device.address}]")
    if include_role:
        _, services = observations.get(device.address.upper(), (advertised_name, set()))
        # Name-only safe-resume is accepted only when both the dedicated
        # Recovery service and the stable role-specific name were observed.
        # An ordinary/custom application name therefore cannot be mistaken
        # for Recovery.
        return device, advertisement_matches_role(
            advertised_name, True, services,
            stable_id4_from_name(advertised_name),
        )
    return device


def validate_attested_board(package_board: str, reported_board: str,
                            expected_board: str | None = None) -> None:
    if package_board == COMMON_APPLICATION_BOARD:
        if reported_board not in RUNTIME_BOARDS:
            raise RuntimeError(
                f"upgraded common application reported unsupported board {reported_board!r}"
            )
    elif reported_board != package_board:
        raise RuntimeError(
            f"upgraded application reported board {reported_board!r}, expected {package_board!r}"
        )
    if expected_board and reported_board != expected_board:
        raise RuntimeError(
            f"upgraded application reported board {reported_board!r}, expected target {expected_board!r}"
        )


def validate_recovery_attestation(build: dict[str, int],
                                  previous_generation: int | None = None) -> None:
    generation = build.get("gen", 0)
    if build.get("state") != 4 or generation < 1:
        raise RuntimeError(
            "application is online, but the Recovery trial was not confirmed: "
            f"state={build.get('state')} generation={build.get('gen')}"
        )
    if previous_generation is not None and generation <= previous_generation:
        raise RuntimeError(
            "application is online, but this OTA did not advance the Recovery generation: "
            f"generation={generation} previous={previous_generation}"
        )


def validate_ota_baseline(build: dict[str, int]) -> int:
    state = build.get("state")
    if state is None:
        raise RuntimeError(
            "device firmware does not report fixed Recovery state; install the current "
            "board-specific factory image once before using OTA"
        )
    if state not in (0, 4):
        raise RuntimeError(
            "device requires fixed Recovery; connect the _OTA device and resend the package"
        )
    return build.get("gen", 0)


async def attest_application(device, manifest: dict, firmware: bytes,
                             expected_board: str | None = None,
                             previous_generation: int | None = None,
                             timeout: float = 15.0) -> None:
    state = await inspect_application(device, timeout, fresh_services=True)
    board = state["board"]
    build = state["build"]

    expected_version = int(manifest["releaseVersion"])
    expected_app_size = int(manifest["application"]["size"])
    validate_attested_board(manifest["board"], board, expected_board)
    if build.get("build") != expected_version or \
            build.get("app_bytes") != expected_app_size:
        raise RuntimeError(
            "device came back online with a different application: "
            f"build={build.get('build')} app_bytes={build.get('app_bytes')}, expected "
            f"build={expected_version} app_bytes={expected_app_size}"
        )
    validate_recovery_attestation(build, previous_generation)
    print(
        "Post-attestation: "
        f"release version {expected_version}, compatibility epoch "
        f"{int(manifest['compatibilityEpoch'])} confirmed."
    )


async def request_v2(client, opcode: int, transaction_id: int,
                     payload: bytes = b"", flags: int = 0,
                     timeout: float = 15.0) -> bytes:
    loop = asyncio.get_running_loop()
    response = loop.create_future()

    def notification(_sender, data: bytearray) -> None:
        packet = bytes(data)
        if len(packet) < 3 or packet[0] != opcode or packet[1] != transaction_id:
            return
        status = packet[2]
        if status != V2_STATUS_OK:
            if not response.done():
                try:
                    label = StatusV2(status).name.lower().replace("_", " ")
                except ValueError:
                    label = f"status 0x{status:02X}"
                if status == int(StatusV2.LOW_POWER) and \
                        opcode == V2_SYSTEM_ACTION:
                    label += (
                        "; OTA requires at least 2.60 V and was refused "
                        "before entering Recovery"
                    )
                response.set_exception(RuntimeError(
                    f"V2 command 0x{opcode:02X} failed: {label}"
                ))
        elif not response.done():
            response.set_result(packet[3:])

    await client.start_notify(INKTAG_CHAR_UUID, notification)
    try:
        # OM reports the CCCD change through its scheduler. Give that event one
        # short turn before writing the request so the response is not dropped.
        await asyncio.sleep(0.25)
        request = bytes((opcode, transaction_id, flags)) + payload
        await client.write_gatt_char(INKTAG_CHAR_UUID, request, response=True)
        return await asyncio.wait_for(response, timeout)
    finally:
        if client.is_connected:
            await client.stop_notify(INKTAG_CHAR_UUID)


async def inspect_application(device, timeout: float = 15.0,
                              fresh_services: bool = False) -> dict:
    """Query runtime identity without changing persistent state or display mode."""
    from inktag_cli.transport import release_bleak_client

    client_options = ble_client_options(timeout, fresh_services=fresh_services)
    print("Connecting to InkTag GATT service...")
    client = BleakClient(device, **client_options)
    await client.connect()
    try:
        print("Connected; querying board/build/swap state...")
        try:
            version = bytes(await client.read_gatt_char(INKTAG_VERSION_UUID))
            print(f"Protocol version: {version.hex()}")
        except Exception as error:
            print(f"Protocol version unavailable: {type(error).__name__}")
        payload = await request_v2(client, V2_GET_DEVICE_INFO, 1, timeout=timeout)
        board, build = parse_v2_device_info(payload)
        ble_address = (
            ":".join(f"{value:02X}" for value in payload[5:11])
            if payload[4] else ""
        )
    finally:
        await release_bleak_client(client)
    return {
        "board": board,
        "build": build,
        "ble_address": ble_address,
        "messages": [],
    }


async def inspect(args) -> None:
    device = await discover(args.name, args.address, args.scan_timeout)
    if str(device.name or "").upper().endswith("_OTA"):
        print("Mode: fixed BLE Recovery (application is not running)")
        return
    state = await inspect_application(device, args.connect_timeout)
    build = state["build"] or {}
    print(f"Board: {state['board'] or 'unknown'}")
    print(
        "Application: "
        f"build={build.get('build', 'unknown')} "
        f"app_bytes={build.get('app_bytes', 'unknown')}"
    )
    print(
        "Recovery: "
        f"result={build.get('swap', 'unknown')} "
        f"state={build.get('state', 'unknown')} "
        f"generation={build.get('gen', 'unknown')}"
    )
    startup_fields = ("ble", "gatt", "adv", "confirm")
    if any(field in build for field in startup_fields):
        print("Startup: " + " ".join(f"{field}={build.get(field, 'unknown')}" for field in startup_fields))


async def request_recovery_mode(device, timeout: float) -> None:
    from inktag_cli.transport import release_bleak_client

    print("Requesting one controlled reboot into fixed BLE Recovery...")
    client = BleakClient(device, timeout=timeout)
    await client.connect()
    try:
        await request_v2(
            client, V2_SYSTEM_ACTION, 1,
            payload=bytes((V2_SYSTEM_ACTION_OTA_RECOVERY,)), timeout=timeout
        )
        await asyncio.sleep(0.6)
    finally:
        await release_bleak_client(client)


async def wait_for_role(address: str, recovery: bool, timeout: float,
                        allow_legacy_recovery: bool = False,
                        expected_id4: str | None = None):
    expected_address = address.upper()
    expected_id4 = expected_id4 or stable_id4_from_address(address)
    deadline = time.monotonic() + timeout
    observed_name = ""
    observed_services = set()
    while time.monotonic() < deadline:
        devices = await BleakScanner.discover(
            timeout=min(4.0, max(1.0, deadline - time.monotonic())),
            return_adv=True,
        )
        for candidate, advertisement in devices.values():
            if candidate.address.upper() != expected_address:
                continue
            observed_name = advertisement.local_name or candidate.name or observed_name
            observed_services.update(
                str(value).lower() for value in advertisement.service_uuids or []
            )
            if advertisement_matches_role(
                observed_name, recovery, observed_services, expected_id4,
                allow_legacy_recovery
            ):
                print(f"Found: {observed_name} [{candidate.address}]")
                return candidate
        await asyncio.sleep(0.2)
    role = "fixed Recovery" if recovery else "application"
    raise RuntimeError(f"OM6626 {role} did not advertise at {address}")


async def open_recovery_transport(device, address: str, timeout: float,
                                  *, manifest: dict | None = None,
                                  firmware: bytes | None = None):
    """Open Recovery with a bounded retry for Windows role-switch latency."""
    from inktag_cli.transport import release_bleak_client

    last_error = None
    for attempt in range(1, 4):
        client = BleakClient(
            device, **ble_client_options(timeout, fresh_services=True)
        )
        transport = None
        try:
            await client.connect()
            if manifest is not None:
                # Do not subscribe/open DFU before this read. Every retry uses
                # a new GATT client and discards the earlier verdict.
                record = await asyncio.wait_for(
                    client.read_gatt_char(RECOVERY_PROFILE_UUID), timeout
                )
                validate_recovery_preflight(record, manifest, firmware or b"")
            transport = Transport(client)
            await transport.open()
            return client, transport
        except Exception as error:
            last_error = error
            if transport is not None:
                try:
                    await transport.close()
                except Exception:
                    pass
            # WinRT can time out while service discovery is still completing,
            # report is_connected=False, yet leave the peripheral in a real
            # link with no advertising.  Always request a disconnect.
            try:
                await release_bleak_client(client)
            except Exception:
                pass
            if attempt == 3:
                raise
            print(
                f"Recovery connection attempt {attempt} failed "
                f"({type(error).__name__}); retrying the same device..."
            )
            await asyncio.sleep(1.0)
            # The failed WinRT discovery may already own a real connection,
            # which suppresses advertisements.  Reconnect by the exact known
            # address instead of requiring the peripheral to be scanned again.
            device = address
    raise last_error  # pragma: no cover - loop either returns or raises


async def resume_safe_application(name: str | None, address: str | None,
                                  scan_timeout: float, connect_timeout: float,
                                  reconnect_timeout: float) -> dict:
    """Request Recovery's guarded return to an existing application.

    Discovery must prove the fixed-Recovery role.  The device remains the
    authority for voltage, mutation-marker, journal and application validity.
    """
    from inktag_cli.transport import release_bleak_client

    device, recovery = await discover(
        name, address, scan_timeout, include_role=True
    )
    if not recovery:
        raise RuntimeError(
            "selected device is not fixed OM Recovery; this command does not "
            "reboot an application into Recovery"
        )
    resolved_address = device.address
    client, transport = await open_recovery_transport(
        device, resolved_address, connect_timeout
    )
    try:
        await transport.resume_application_if_safe()
    finally:
        await transport.close()
        await release_bleak_client(client)

    application = await wait_for_role(
        resolved_address, False, reconnect_timeout
    )
    return {
        "ok": True,
        "action": "safe-resume-app",
        "platform": "onmicro",
        "address": resolved_address,
        "applicationName": application.name or "",
    }


async def transfer(args) -> None:
    from inktag_cli.transport import release_bleak_client

    qualification = args.qualification_expect_signature_reject
    manifest, firmware = load_package(
        args.package, verify_signature=not qualification
    )
    command = command_object(manifest, firmware)
    total = len(command) + len(firmware)
    print(f"Package: {args.package}")
    print(f"  Board: {manifest['board']}")
    print(f"  Release version: {manifest['releaseVersion']}")
    print(f"  Compatibility epoch: {manifest['compatibilityEpoch']}")
    print(
        f"  Application: {manifest['application']['size']} / "
        f"{manifest['application']['slot_size'] - SIGNATURE_TRAILER_SIZE} bytes"
    )
    print(f"  Signed transfer: {len(firmware)} bytes ({manifest['signature']['key_id']})")
    device, direct_recovery = await discover(
        args.name, args.address, args.scan_timeout, include_role=True,
        allow_legacy_recovery=args.legacy_recovery_name_fallback
    )
    address = device.address
    expected_id4 = (stable_id4_from_address(address) or
                    stable_id4_from_name(str(device.name or "")))
    previous_generation = None
    direct_recovery = direct_recovery or str(device.name or "").upper().endswith("_OTA")
    if not direct_recovery:
        try:
            before = await inspect_application(device, args.connect_timeout)
        except Exception as application_error:
            try:
                device = await wait_for_role(
                    address, True, min(6.0, args.scan_timeout),
                    args.legacy_recovery_name_fallback
                )
                direct_recovery = True
            except Exception:
                raise application_error
        if not direct_recovery:
            before_build = before["build"] or {}
            expected_id4 = (expected_id4 or
                            stable_id4_from_address(before.get("ble_address", "")))
            if expected_id4 is None:
                raise RuntimeError(
                    "cannot prove the stable OM ID4 before entering Recovery"
                )
            previous_generation = validate_ota_baseline(before_build)
            print(f"Baseline Recovery generation: {previous_generation}")
            await request_recovery_mode(device, args.connect_timeout)
            device = await wait_for_role(
                address, True, args.reconnect_timeout,
                args.legacy_recovery_name_fallback, expected_id4
            )
    else:
        print("Fixed Recovery is already active; retransmitting the complete application.")

    started = time.monotonic()

    def progress(done: int, total_bytes: int) -> None:
        speed = done / max(time.monotonic() - started, 0.001) / 1024
        print(f"\rProgramming {done * 100 / total_bytes:5.1f}% ({speed:5.1f} KiB/s)", end="", flush=True)

    client, transport = await open_recovery_transport(
        device, address, args.connect_timeout, manifest=manifest, firmware=firmware
    )
    expected_rejection = False
    try:
        print("OTA command: validating board, release version, compatibility epoch and image size...")
        try:
            await transport.send_object(OBJECT_COMMAND, command, args.packet_size, progress, 0, total)
            print("\nOTA command accepted. Replacing the single application...")
            await transport.send_object(OBJECT_DATA, firmware, args.packet_size, progress, len(command), total)
        except RuntimeError as error:
            if qualification and is_expected_signature_rejection(error):
                expected_rejection = True
            else:
                raise
    finally:
        await transport.close()
        await release_bleak_client(client)
    if qualification:
        if not expected_rejection:
            raise RuntimeError(
                "qualification failed: Recovery accepted an invalid publisher signature"
            )
        await wait_for_role(
            address, True, args.reconnect_timeout,
            args.legacy_recovery_name_fallback, expected_id4,
        )
        print("\nQualification passed: Recovery rejected the invalid publisher signature.")
        return
    print("\nOTA transfer complete. Fixed Recovery will trial the new application.")
    print("Waiting for the upgraded application to advertise...")
    application = await wait_for_role(address, False, args.reconnect_timeout)
    await attest_application(
        application, manifest, firmware, args.expect_board, previous_generation
    )
    print(
        f"OTA verified: {application.name or '(unnamed)'} [{application.address}] "
        "runs the packaged application."
    )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, nargs="?")
    parser.add_argument(
        "--inspect", action="store_true",
        help="query board/build/swap state without uploading firmware",
    )
    parser.add_argument(
        "--resume-pending", action="store_true",
        help="deprecated alias: reconnect fixed Recovery and resend the package",
    )
    parser.add_argument("--name", default="IKT_OM_", help="BLE name/prefix")
    parser.add_argument("--address")
    parser.add_argument(
        "--legacy-om-recovery", dest="legacy_recovery_name_fallback",
        action="store_true",
        help=("rescue old OM firmware advertising a generic Recovery name; "
              "never use for current stable-identity devices"),
    )
    parser.add_argument(
        "--test-bad-signature",
        dest="qualification_expect_signature_reject", action="store_true",
        help=("qualification only: skip host ECDSA verification and require an "
              "explicit device-side signature rejection"),
    )
    parser.add_argument("--expect-board", choices=sorted(RUNTIME_BOARDS))
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--reconnect-timeout", type=float, default=75.0)
    parser.add_argument("--packet-size", type=int, default=200, choices=range(20, 245))
    args = parser.parse_args()
    if args.inspect and args.resume_pending:
        parser.error("--inspect and --resume-pending are mutually exclusive")
    if not args.inspect and args.package is None:
        parser.error("package is required unless --inspect is used")
    return args


def main() -> int:
    try:
        args = parse_args()
        if args.inspect:
            asyncio.run(inspect(args))
        else:
            asyncio.run(transfer(args))
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as error:
        print(f"\nOTA failed: {type(error).__name__}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
