from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import zipfile
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

from .tlsr_layout import (
    tlsr_layout_profile, tlsr_package_profile, validate_tlsr_epoch,
)
from .safe_archive import OtaArchive


MAX_PACKAGE_MEMBER_BYTES = 4 * 1024 * 1024
ONMICRO_SIGNATURE_TRAILER_SIZE = 96
TLSR_SIGNATURE_MAGIC = 0x31534B49
TLSR_SIGNATURE_VERSION = 1
TLSR_SIGNATURE_PREFIX_SIZE = 32
TLSR_SIGNATURE_SIZE = 64
TLSR_SIGNATURE_TRAILER_SIZE = 96
TLSR_BOARD_SIGNATURE = 0x53484C54
TLSR_VERIFIER_SIGNATURE = 0x314B4C54
TLSR_BOARD = "tlsr8359_hanshow"
TLSR_TRUSTED_KEY = {
    "trust_profile": "production",
    "algorithm": "ecdsa-p256-sha256",
    "key_id": "inktag-tlsr8359-ota-prod-v1",
    "public_key_hash": "f34e6d37e3d2b88454d01d149446dcc5",
}
P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 16
)
DRIVER_FILES = {
    "nordic": "ota_nrf52811.py",
    "goodix": "ota_goodix.py",
    "onmicro": "ota_om6626.py",
    "dialog": "ota_da14585.py",
    "telink": "ota_tlsr8359.py",
}

@dataclass(frozen=True)
class OtaPackageInfo:
    transport: str
    path: Path
    board: str | None
    version: str | int | None
    application_bytes: int
    sha256: str
    details: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["path"] = str(self.path)
        return result


def inspect_package(path: Path) -> OtaPackageInfo:
    package = path.expanduser().resolve()
    if not package.is_file():
        raise ValueError(f"OTA package does not exist: {package}")
    if package.suffix.lower() == ".img":
        return _inspect_dialog(package)
    if package.suffix.lower() != ".zip":
        raise ValueError("OTA package must be a .zip or DA14585 .img file")
    return _inspect_zip(package)


def _inspect_dialog(path: Path) -> OtaPackageInfo:
    if not 64 <= path.stat().st_size <= MAX_PACKAGE_MEMBER_BYTES:
        raise ValueError("DA14585 image size is outside the supported range")
    image = path.read_bytes()
    if len(image) < 64 or len(image) > MAX_PACKAGE_MEMBER_BYTES:
        raise ValueError("DA14585 image size is outside the supported range")
    if image[:2] != b"pQ" or image[2] != 0xAA:
        raise ValueError("file is not an SDK6 pQ SUOTA image")
    code_size, expected_crc = struct.unpack_from("<II", image, 4)
    if code_size + 64 != len(image):
        raise ValueError("DA14585 pQ code size does not match the file length")
    actual_crc = zlib.crc32(image[64:]) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise ValueError("DA14585 pQ CRC32 verification failed")
    version = image[12:28].split(b"\0", 1)[0].decode("ascii", errors="replace") or None
    return OtaPackageInfo(
        transport="dialog",
        path=path,
        board="da14585_aesl",
        version=version,
        application_bytes=code_size,
        sha256=hashlib.sha256(image).hexdigest(),
        details={"container": "sdk6-pq-suota", "crc32": f"0x{actual_crc:08X}"},
    )


def _inspect_zip(path: Path) -> OtaPackageInfo:
    try:
        with OtaArchive(path) as archive:
            manifest = archive.manifest()
            if manifest.get("format") == "inktag-ota-v1":
                transport = manifest.get("transport")
                if transport == "goodix-otas-v2":
                    return _inspect_goodix(path, archive, manifest)
                if transport == "onmicro-dfu-v3":
                    return _inspect_onmicro(
                        path,
                        archive,
                        manifest,
                    )
                if transport == "telink-fixed-recovery-v2":
                    return _inspect_telink(path, archive, manifest)
                raise ValueError(f"unsupported InkTag OTA transport: {transport}")
            if "manifest" in manifest:
                return _inspect_nordic(path, archive, manifest)
    except (zipfile.BadZipFile, json.JSONDecodeError, KeyError) as exc:
        raise ValueError(f"invalid OTA ZIP: {exc}") from exc
    raise ValueError("ZIP is not a supported InkTag OTA package")


def _read_member(archive: zipfile.ZipFile, name: str) -> bytes:
    info = archive.getinfo(name)
    if info.is_dir() or info.file_size > MAX_PACKAGE_MEMBER_BYTES:
        raise ValueError(f"OTA member has an invalid size: {name}")
    return archive.read(info)


def _application_bytes(archive: zipfile.ZipFile, application: dict[str, Any]) -> tuple[str, bytes]:
    name = str(application["file"])
    if Path(name).is_absolute() or ".." in Path(name).parts:
        raise ValueError("OTA application path is unsafe")
    return name, _read_member(archive, name)


def _verify_sha(data: bytes, expected: str | None) -> str:
    actual = hashlib.sha256(data).hexdigest()
    if expected and actual.lower() != str(expected).lower():
        raise ValueError("OTA application SHA-256 verification failed")
    return actual


def _inspect_goodix(path: Path, archive: zipfile.ZipFile, manifest: dict[str, Any]) -> OtaPackageInfo:
    # Keep the generic inspector on the exact same production trust policy as
    # the transport driver. A superficial ZIP/hash check must not turn an old,
    # unsigned or unknown-board package into a user-selectable OTA candidate.
    from inktag_programmer_formats.goodix_package import load_package

    application = manifest["application"]
    _, member = _application_bytes(archive, application)
    parsed = load_package(path)
    manifest = parsed.manifest
    application = manifest["application"]
    firmware = parsed.firmware
    if firmware != member:
        raise ValueError("Goodix package changed while it was being inspected")
    digest = hashlib.sha256(firmware).hexdigest()
    transfer_size = len(firmware)
    app_size = int(application["size"])
    trailer_size = transfer_size - app_size
    return OtaPackageInfo(
        transport="goodix",
        path=path,
        board=manifest.get("board"),
        version=int(manifest["releaseVersion"]),
        application_bytes=app_size,
        sha256=digest,
        details={
            "container": "inktag-ota-v1",
            "wireTransport": "goodix-otas-v2",
            "transferBytes": transfer_size,
            "trailerBytes": trailer_size,
            "chip": manifest.get("chip"),
            "release": manifest.get("release"),
            "compatibilityEpoch": int(manifest["compatibilityEpoch"]),
            "signingKey": manifest["signature"]["key_id"],
            "layout": manifest["layout"],
            "payloadLimit": int(application["slot_size"]) - trailer_size,
        },
    )


def _inspect_onmicro(
    path: Path,
    archive: zipfile.ZipFile,
    manifest: dict[str, Any],
) -> OtaPackageInfo:
    # Use the same exact verifier as the transport and standalone Full tools.
    # Never maintain a parallel signature/layout policy in this inspector.
    from inktag_programmer_formats.onmicro_package import load_package

    _, member = _application_bytes(archive, manifest["application"])
    manifest, firmware = load_package(path)
    if member != firmware:
        raise ValueError("OnMicro package changed while it was being inspected")
    application = manifest["application"]
    return OtaPackageInfo(
        transport="onmicro", path=path, board=manifest["board"],
        version=int(manifest["releaseVersion"]),
        application_bytes=int(application["size"]),
        sha256=hashlib.sha256(firmware).hexdigest(),
        details={
            "container": "inktag-ota-v1", "wireTransport": "onmicro-dfu-v3",
            "slotBytes": int(application["slot_size"]),
            "transferBytes": len(firmware),
            "trailerBytes": ONMICRO_SIGNATURE_TRAILER_SIZE,
            "signingKey": manifest["signature"]["key_id"],
            "releaseVersion": int(manifest["releaseVersion"]),
            "compatibilityEpoch": manifest["compatibilityEpoch"],
            "signedBoard": manifest["board"], "signedLayout": manifest["layout"],
        },
    )


def _inspect_telink(path: Path, archive: zipfile.ZipFile, manifest: dict[str, Any]) -> OtaPackageInfo:
    layout = manifest.get("layout")
    profile = tlsr_layout_profile(layout)
    if manifest.get("board") != TLSR_BOARD:
        raise ValueError("Telink package is not for the common Hanshow TLSR8359 application")
    application = manifest["application"]
    _, firmware = _application_bytes(archive, application)
    profile = tlsr_package_profile(layout, firmware)
    digest = _verify_sha(firmware, application.get("sha256"))
    if len(firmware) < 32 + TLSR_SIGNATURE_TRAILER_SIZE or len(firmware) > profile["limit"]:
        raise ValueError("Telink application size is outside its layout's signed payload limit")
    if firmware[8:12] != b"KNLT":
        raise ValueError("Telink application is missing the TLNK marker")
    declared_size = struct.unpack_from("<I", firmware, 24)[0]
    app_size = declared_size + 4
    if app_size + TLSR_SIGNATURE_TRAILER_SIZE != len(firmware):
        raise ValueError("Telink application size field does not match the package")
    stored_crc = struct.unpack_from(">I", firmware, declared_size)[0]
    actual_crc = zlib.crc32(firmware[:declared_size]) & 0xFFFFFFFF
    if stored_crc != actual_crc:
        raise ValueError("Telink application CRC32 verification failed")
    signature = manifest.get("signature")
    if not isinstance(signature, dict) or signature.get("required") is not True:
        raise ValueError("Telink OTA package must carry a required publisher signature")
    if signature.get("trust_profile") != TLSR_TRUSTED_KEY["trust_profile"] or \
            signature.get("algorithm") != TLSR_TRUSTED_KEY["algorithm"] or \
            signature.get("key_id") != TLSR_TRUSTED_KEY["key_id"] or \
            str(signature.get("public_key_hash", "")).lower() != \
            TLSR_TRUSTED_KEY["public_key_hash"]:
        raise ValueError("Telink OTA publisher signing key is not trusted")
    try:
        public_key = bytes.fromhex(str(signature.get("public_key", "")))
    except ValueError as error:
        raise ValueError("Telink OTA publisher public key is malformed") from error
    trailer = firmware[app_size:]
    (magic, version, algorithm, trailer_size, signed_app_size,
     compatibility_epoch, board_signature, layout_signature,
     verifier_signature, signed_release_version) = \
        struct.unpack_from("<IBBHIIIIII", trailer)
    release_version = int(manifest["releaseVersion"])
    manifest_epoch = manifest["compatibilityEpoch"]
    validate_tlsr_epoch(layout, manifest_epoch)
    if (
        int(signature.get("signed_context_size", -1)) != 16
        or signature.get("signed_board") != TLSR_BOARD
        or signature.get("signed_layout") != layout
        or int(signature.get("board_signature", -1)) != TLSR_BOARD_SIGNATURE
        or int(signature.get("layout_signature", -1)) != profile["signature"]
        or int(signature.get("verifier_signature", -1)) != TLSR_VERIFIER_SIGNATURE
        or int(signature.get("signed_prefix_size", -1)) != TLSR_SIGNATURE_PREFIX_SIZE
        or int(signature.get("signed_trailer_size", -1)) != TLSR_SIGNATURE_TRAILER_SIZE
        or len(public_key) != 65
        or public_key[0] != 0x04
        or hashlib.sha256(public_key).digest()[:16].hex()
            != TLSR_TRUSTED_KEY["public_key_hash"]
        or magic != TLSR_SIGNATURE_MAGIC
        or version != TLSR_SIGNATURE_VERSION
        or algorithm != 1
        or trailer_size != TLSR_SIGNATURE_TRAILER_SIZE
        or signed_app_size != app_size
        or compatibility_epoch != manifest_epoch
        or board_signature != TLSR_BOARD_SIGNATURE
        or layout_signature != profile["signature"]
        or verifier_signature != TLSR_VERIFIER_SIGNATURE
        or signed_release_version != release_version
    ):
        raise ValueError("Telink OTA publisher signature metadata is invalid")
    raw_signature = trailer[TLSR_SIGNATURE_PREFIX_SIZE:]
    r = int.from_bytes(raw_signature[:32], "big")
    s = int.from_bytes(raw_signature[32:], "big")
    if r == 0 or s == 0 or s > P256_ORDER // 2:
        raise ValueError("Telink OTA publisher signature is non-canonical")
    try:
        ec.EllipticCurvePublicKey.from_encoded_point(
            ec.SECP256R1(), public_key
        ).verify(
            encode_dss_signature(r, s),
            firmware[:app_size + TLSR_SIGNATURE_PREFIX_SIZE],
            ec.ECDSA(hashes.SHA256()),
        )
    except (InvalidSignature, ValueError) as error:
        raise ValueError("Telink OTA publisher signature verification failed") from error
    if (
        int(application.get("size", -1)) != app_size
        or int(application.get("transfer_size", -1)) != len(firmware)
        or int(application.get("declared_size", -1)) != declared_size
        or int(application.get("crc32", -1)) != actual_crc
        or int(application.get("active_address", -1)) != profile['start']
    ):
        raise ValueError("Telink application/Recovery layout does not match the manifest")
    return OtaPackageInfo(
        transport="telink",
        path=path,
        board=manifest.get("board"),
        version=release_version,
        application_bytes=declared_size,
        sha256=digest,
        details={
            "container": "inktag-ota-v1",
            "wireTransport": "telink-fixed-recovery-v2",
            "transferBytes": len(firmware),
            "trailerBytes": TLSR_SIGNATURE_TRAILER_SIZE,
            "signingKey": signature.get("key_id"),
            "releaseVersion": release_version,
            "compatibilityEpoch": compatibility_epoch,
            "signedBoard": TLSR_BOARD,
            "signedLayout": layout,
            "crc32": f"0x{actual_crc:08X}",
            "compatibleBoards": manifest.get("compatible_boards", []),
        },
    )


def _inspect_nordic(path: Path, archive: zipfile.ZipFile, manifest: dict[str, Any]) -> OtaPackageInfo:
    root = manifest["manifest"]
    application = root.get("application")
    if not application:
        raise ValueError("Nordic package has no application image")
    if any(root.get(name) for name in ("bootloader", "softdevice", "softdevice_bootloader")):
        raise ValueError("Nordic OTA must be application-only")
    firmware = _read_member(archive, str(application["bin_file"]))
    init_packet = _read_member(archive, str(application["dat_file"]))
    if not init_packet or not firmware:
        raise ValueError("Nordic package contains an empty init packet or application")
    return OtaPackageInfo(
        transport="nordic",
        path=path,
        board=None,
        version=None,
        application_bytes=len(firmware),
        sha256=hashlib.sha256(firmware).hexdigest(),
        details={
            "container": "nordic-secure-dfu",
            "initPacketBytes": len(init_packet),
        },
    )


def resolve_driver_script(transport: str) -> Path:
    if transport not in DRIVER_FILES:
        raise ValueError(f"unsupported OTA transport: {transport}")
    packaged = Path(__file__).resolve().parent / "_drivers" / DRIVER_FILES[transport]
    if packaged.is_file():
        return packaged
    raise RuntimeError("the installed InkTag CLI is missing its OTA driver")


def driver_command(
    info: OtaPackageInfo,
    *,
    device: str | None,
    address: str | None,
    scan_timeout: float,
    connect_timeout: float,
    reconnect_timeout: float,
    skip_reconnect_check: bool,
    no_sync: bool = False,
    legacy_recovery_name_fallback: bool = False,
    password_data_dir: Path | None = None,
    single_packet_window: bool = False,
) -> list[str]:
    if single_packet_window and info.transport != "telink":
        raise ValueError("--single-packet-window is supported only by TLSR OTA")
    command = [sys.executable, str(resolve_driver_script(info.transport)), str(info.path)]
    if single_packet_window:
        command.append("--single-packet-window")
    if password_data_dir is not None:
        if not password_data_dir.is_absolute():
            raise ValueError("OTA password data directory must be absolute")
        if info.transport in {"telink", "goodix", "nordic", "onmicro"}:
            command.extend(("--password-data-dir", str(password_data_dir.resolve())))
    if device:
        command.extend(("--name", device))
    if address:
        command.extend(("--address", address))
    command.extend(("--scan-timeout", str(scan_timeout), "--connect-timeout", str(connect_timeout)))
    if info.transport in {"nordic", "goodix", "onmicro", "dialog", "telink"}:
        command.extend(("--reconnect-timeout", str(reconnect_timeout)))
    if skip_reconnect_check:
        if info.transport not in {"nordic", "dialog"}:
            raise ValueError("--skip-reconnect-check is supported only by Nordic and Dialog OTA")
        command.append("--skip-reconnect-check")
    # Goodix historically synchronized inside its transport driver. Disable
    # that platform-local behavior unconditionally: the shared CLI performs
    # one explicit post-attestation sync for every transport unless the user
    # selected --no-sync.
    if info.transport == "goodix":
        command.append("--no-sync")
    if legacy_recovery_name_fallback:
        if info.transport != "onmicro":
            raise ValueError(
                "--legacy-om-recovery is supported only by OnMicro OTA"
            )
        command.append("--legacy-om-recovery")
    return command


def run_update(command: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        capture_output=capture_output,
        text=capture_output,
    )
