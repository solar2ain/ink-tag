"""Pure OnMicro package verification shared by CLI and offline Full tools.

No BLE runtime or installed InkTag CLI is needed to load this file directly.
"""
import hashlib
import json
import struct
import zipfile
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

IMAGE_TYPE_CUSTOM = 0x10
COMMON_APPLICATION_BOARD = "om6626_zkcxxb"
SIGNATURE_MAGIC = 0x31534B49
SIGNATURE_FORMAT_VERSION = 1
SIGNATURE_PREFIX_SIZE = 32
SIGNATURE_SIZE = 64
SIGNATURE_TRAILER_SIZE = 96
BOARD_SIGNATURE = 0x42584D4F
LAYOUT_SIGNATURE = int.from_bytes(b"OZV1", "little")
VERIFIER_SIGNATURE = 0x314B4D4F
SIGNED_LAYOUT = "layout-v1"
LAYOUT_PROFILES = {
    (COMMON_APPLICATION_BOARD, SIGNED_LAYOUT): {
        "signature": LAYOUT_SIGNATURE, "address": 0x2A000, "slot": 0x4E000,
        "board": COMMON_APPLICATION_BOARD, "board_signature": BOARD_SIGNATURE,
        "journal_session": True,
    },
    ("om6626_hsela", SIGNED_LAYOUT): {
        "signature": int.from_bytes(b"OHV1", "little"), "address": 0x2A000, "slot": 0x4E000,
        "board": "om6626_hsela", "board_signature": 0x53484D4F,
        "journal_session": True,
    },
}
TRUSTED_SIGNING_KEY = {
    "trust_profile": "production",
    "algorithm": "ecdsa-p256-sha256",
    "key_id": "inktag-om6626-ota-v1",
    "public_key_hash": "fc0e79e61ce05df76dcff00bf122e86e",
}


def crc32(data: bytes, initial: int = 0) -> int:
    crc = (~initial) & 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return (~crc) & 0xFFFFFFFF


def package_profile(manifest: dict) -> dict:
    profile = LAYOUT_PROFILES.get((manifest.get("board"), manifest.get("layout")))
    application = manifest.get("application", {})
    if profile is None or manifest.get("board") != profile["board"] or \
            application.get("image_type") != IMAGE_TYPE_CUSTOM or \
            application.get("active_address") != profile["address"] or \
            application.get("slot_size") != profile["slot"]:
        raise ValueError("package has an incompatible application/Recovery layout")
    epoch = manifest.get("compatibilityEpoch")
    if type(epoch) is not int or not 0 <= epoch <= 0xFFFF:
        raise ValueError("OnMicro compatibility epoch must be an explicit uint16")
    if (manifest.get("chip") != "OM6626" or
            application.get("recovery_state_addresses") != [0x82000, 0x83000] or
            "recovery_request_address" in application or
            application.get("recovery_request_storage") != "transaction-journal"):
        raise ValueError("OnMicro Recovery context does not match")
    return profile


def validate_recovery_preflight(data: bytes, manifest: dict, firmware: bytes) -> None:
    """Validate fixed Recovery's read-only policy before any DFU mutation."""
    profile = package_profile(manifest)
    expected = struct.pack("<IBBHIII", 0x50524B49, 1, 0, 20,
                           profile["signature"], profile["slot"] - SIGNATURE_TRAILER_SIZE,
                           profile["slot"])
    if bytes(data) != expected:
        raise ValueError(
            "Recovery preflight is missing or incompatible; a separately authorized "
            "fixed-Recovery Full upgrade is required"
        )
    signature = manifest.get("signature", {})
    if signature.get("signed_board") != profile["board"] or \
            signature.get("signed_layout") != manifest["layout"] or \
            signature.get("board_signature") != profile["board_signature"] or \
            signature.get("layout_signature") != profile["signature"] or \
            signature.get("verifier_signature") != VERIFIER_SIGNATURE:
        raise ValueError("Recovery preflight package context does not match")
    app_size = manifest["application"].get("size", -1)
    if not 0 < app_size <= profile["slot"] - SIGNATURE_TRAILER_SIZE or \
            len(firmware) != app_size + SIGNATURE_TRAILER_SIZE or \
            manifest["application"].get("transfer_size") != len(firmware):
        raise ValueError("Recovery preflight package exceeds the signed profile")


def load_package(path: Path) -> tuple[dict, bytes]:
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        application = manifest["application"]
        firmware = package.read(application["file"])
    if (manifest.get("format"), manifest.get("transport")) != (
        "inktag-ota-v1", "onmicro-dfu-v3"
    ):
        raise ValueError("not an InkTag OnMicro single-slot Recovery package")
    if manifest.get("board") not in {COMMON_APPLICATION_BOARD, "om6626_hsela"}:
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
            signature.get("signed_board") != profile["board"] or \
            signature.get("signed_layout") != manifest["layout"] or \
            int(signature.get("board_signature", -1)) != profile["board_signature"] or \
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
    manifest_epoch = manifest["compatibilityEpoch"]
    if not 0 <= manifest_epoch <= 0xFFFF:
        raise ValueError("OnMicro compatibility epoch exceeds the product contract")
    if magic != SIGNATURE_MAGIC or format_version != SIGNATURE_FORMAT_VERSION or algorithm != 1 or \
            trailer_size != SIGNATURE_TRAILER_SIZE or signed_size != app_size or \
            compatibility_epoch != manifest_epoch or \
            board_signature != profile["board_signature"] or \
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
