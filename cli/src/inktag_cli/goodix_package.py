"""Pure Goodix package verification shared by CLI and offline Full tools.

No discovery, connection, authentication or BLE runtime imports belong here.
Loading this module by file path does not require an installed InkTag CLI.
"""
from __future__ import annotations

import hashlib
import json
import re
import struct
import zipfile
from dataclasses import dataclass
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

IMAGE_INFO_PATTERN = 0x4744
IMAGE_INFO_SIZE = 48
IMAGE_INFO_STRUCT_SIZE = 40
IMAGE_TRAILER_SIZE = 48
SIGNATURE_EXTENSION_SIZE = 856
SIGNED_TRAILER_SIZE = IMAGE_TRAILER_SIZE + SIGNATURE_EXTENSION_SIZE
SIGNATURE_PATTERN = 0xDEADBEEF
SIGNED_FIRMWARE_TYPE = 0x10
RSA_PUBLIC_KEY_OFFSET = 128
RSA_PUBLIC_KEY_SIZE = 520
RSA_SIGNATURE_OFFSET = 648
RSA_SIGNATURE_SIZE = 256
ECDSA_PUBLIC_KEY_SIZE = 65
ECDSA_SIGNATURE_SIZE = 64
TRUSTED_SIGNING_KEYS = {
    "gr5331_zkc21v": {
        "trust_profile": "production",
        "key_id": "inktag-gr5331-ota-v1",
        "algorithm": "ecdsa-p256-sha256",
        "public_key_hash": bytes.fromhex("3d96ce5cf20e58cca5f5e291ca2bfd81"),
        "chip": "GR5331",
    },
    "gr551x_zkc42v": {
        "trust_profile": "production",
        "key_id": "inktag-gr551x-ota-v1",
        "algorithm": "rsa-pss-sha256",
        "public_key_hash": bytes.fromhex("a410a4f0ce51dd2c89ba8cca1dade9b9"),
        "chip": "GR551x",
    },
}
# Layout identity is independent of signing-key identity and compatibility epoch.
# Each exact manifest/marker pair is separately pinned; never infer it from size.
LAYOUT_PROFILES = {
    ("gr5331_zkc21v", "layout-v1"): {
        "active_address": 0x00220000,
        "slot_size": 0x00040000,
        "layout_marker": int.from_bytes(b"G3V1", "little"),
        "image_store_start": 0x0026A000,
        "image_store_max_size": 0x00014000,
        "recovery_state_addresses": [0x00268000, 0x00269000],
    },
    ("gr551x_zkc42v", "layout-v1"): {
        "active_address": 0x01020000,
        "slot_size": 0x00052000,
        "layout_marker": int.from_bytes(b"G5V1", "little"),
        "image_store_start": 0x0107C000,
        "image_store_max_size": 0x00080000,
        "recovery_state_addresses": [0x0107A000, 0x0107B000],
    },
}

def layout_profile(manifest: dict) -> dict:
    profile = LAYOUT_PROFILES.get((manifest.get("board"), manifest.get("layout")))
    epoch = manifest.get("compatibilityEpoch")
    if profile is None or type(epoch) is not int or not 0 <= epoch <= 0xFFFF:
        raise ValueError("Goodix OTA package requires the board-qualified Full-migrated layout")
    return profile


@dataclass
class Package:
    path: Path
    manifest: dict
    firmware: bytes
    image_info: bytes
    firmware_type: int


def load_package(path: Path) -> Package:
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        if manifest.get("format") != "inktag-ota-v1" or manifest.get("transport") != "goodix-otas-v2":
            raise ValueError("not an InkTag Goodix OTAS v2 package")
        application = manifest["application"]
        firmware = package.read(application["file"])
    if len(firmware) != application["transfer_size"]:
        raise ValueError("package transfer size does not match manifest")
    if hashlib.sha256(firmware).hexdigest() != application["sha256"]:
        raise ValueError("package SHA-256 verification failed")
    app_size = application["size"]
    board = str(manifest.get("board") or "")
    trusted_key = TRUSTED_SIGNING_KEYS.get(board)
    if trusted_key is None:
        raise ValueError("Goodix OTA target board is not trusted")
    if "version" in manifest or "releaseVersion" not in manifest or \
            "compatibilityEpoch" not in manifest:
        raise ValueError("Goodix OTA package uses the retired version-floor format")
    profile = layout_profile(manifest)
    if manifest.get("chip") != trusted_key["chip"] or \
            int(application.get("active_address", -1)) != profile["active_address"] or \
            int(application.get("slot_size", -1)) != profile["slot_size"] or \
            int(application.get("image_store_start", -1)) != profile["image_store_start"] or \
            int(application.get("image_store_max_size", -1)) != profile["image_store_max_size"] or \
            application.get("recovery_state_addresses") != profile["recovery_state_addresses"]:
        raise ValueError("Goodix OTA board/chip/address policy does not match")
    release = str(manifest.get("release") or "")
    release_match = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", release)
    if release_match is None:
        raise ValueError("Goodix OTA release must be MAJOR.MINOR.PATCH")
    release_parts = tuple(int(value) for value in release_match.groups())
    protocol = int(manifest.get("protocol", -1))
    release_version = int(manifest["releaseVersion"])
    compatibility_epoch = int(manifest["compatibilityEpoch"])
    if any(value > 0xFF for value in release_parts) or not 0 <= protocol <= 0xFF:
        raise ValueError("Goodix OTA release/protocol bytes are out of range")
    if release_version != (
        release_parts[0] * 10000 + release_parts[1] * 100 + release_parts[2]
    ):
        raise ValueError("Goodix OTA releaseVersion does not match release")
    if not 0 <= compatibility_epoch <= 0xFFFF:
        raise ValueError("Goodix OTA compatibilityEpoch is out of range")
    if bytes((protocol, *release_parts)) not in firmware[:app_size]:
        raise ValueError("Goodix signed application does not contain the declared GATT release")
    firmware_type = 0
    expected_trailer_size = SIGNED_TRAILER_SIZE
    signature = manifest.get("signature")
    if not isinstance(signature, dict) or signature.get("required") is not True:
        raise ValueError("Goodix OTA package must carry a required publisher signature")
    if signature.get("trust_profile") != trusted_key["trust_profile"]:
        raise ValueError("Goodix OTA trust profile is not production")
    if signature.get("algorithm") != trusted_key["algorithm"]:
        raise ValueError("Goodix OTA signature algorithm is not trusted")
    if signature.get("key_id") != trusted_key["key_id"]:
        raise ValueError("Goodix OTA signing key ID is not trusted")
    if str(signature.get("public_key_hash") or "").lower() != trusted_key[
        "public_key_hash"
    ].hex():
        raise ValueError("Goodix OTA signing-key hash does not match the trusted key")
    if (
        signature.get("signature_extension_size") != SIGNATURE_EXTENSION_SIZE
        or signature.get("signed_trailer_size") != SIGNED_TRAILER_SIZE
    ):
        raise ValueError("Goodix OTA signature container size is invalid")
    firmware_type = SIGNED_FIRMWARE_TYPE
    if app_size + expected_trailer_size != len(firmware):
        raise ValueError("Goodix package trailer size does not match its board policy")
    if len(firmware) > profile["slot_size"]:
        raise ValueError("Goodix signed application exceeds its board-qualified slot")
    image_info = firmware[app_size : app_size + IMAGE_INFO_SIZE]
    pattern, signed_epoch = struct.unpack_from("<HH", image_info)
    if pattern != IMAGE_INFO_PATTERN:
        raise ValueError("Goodix image-info pattern is invalid")
    if signed_epoch != compatibility_epoch:
        raise ValueError("Goodix signed compatibility epoch does not match manifest")
    marker, inverse = struct.unpack_from("<II", image_info, IMAGE_INFO_STRUCT_SIZE)
    if marker != profile["layout_marker"] or inverse != ((~marker) & 0xFFFFFFFF):
        raise ValueError("Goodix signed image-info has the wrong board-layout marker")
    body_size, body_checksum, load_addr, run_addr = struct.unpack_from("<IIII", image_info, 4)
    if body_size != app_size or load_addr != application["active_address"] or run_addr != load_addr:
        raise ValueError("Goodix image-info address/size does not match manifest")
    if body_checksum != int(application.get("checksum", -1)):
        raise ValueError("Goodix image-info checksum does not match manifest")
    if body_checksum != (sum(firmware[:app_size]) & 0xFFFFFFFF):
        raise ValueError("Goodix application checksum is invalid")
    if trusted_key is not None:
        if struct.unpack_from("<I", firmware, app_size + IMAGE_TRAILER_SIZE)[0] != SIGNATURE_PATTERN:
            raise ValueError("Goodix OTA signature-extension pattern is invalid")
        if firmware[app_size + 120 : app_size + 124] != b"SIGN":
            raise ValueError("Goodix OTA package is not a sign-only container")
        public_key_size = (
            RSA_PUBLIC_KEY_SIZE
            if trusted_key["algorithm"] == "rsa-pss-sha256"
            else ECDSA_PUBLIC_KEY_SIZE
        )
        public_key = firmware[
            app_size + RSA_PUBLIC_KEY_OFFSET : app_size + RSA_PUBLIC_KEY_OFFSET + public_key_size
        ]
        if hashlib.sha256(public_key).digest()[:16] != trusted_key["public_key_hash"]:
            raise ValueError("Goodix OTA embedded public key is not trusted")
        if trusted_key["algorithm"] == "rsa-pss-sha256":
            raw_signature = firmware[
                app_size + RSA_SIGNATURE_OFFSET : app_size + RSA_SIGNATURE_OFFSET + RSA_SIGNATURE_SIZE
            ]
            if len(raw_signature) != RSA_SIGNATURE_SIZE or not any(raw_signature):
                raise ValueError("Goodix OTA RSA signature is missing")
            modulus = int.from_bytes(public_key[:256], "little")
            exponent = int.from_bytes(public_key[256:260], "little")
            try:
                rsa.RSAPublicNumbers(exponent, modulus).public_key().verify(
                    raw_signature[::-1],
                    firmware[: app_size + RSA_SIGNATURE_OFFSET],
                    padding.PSS(
                        mgf=padding.MGF1(hashes.SHA256()), salt_length=32
                    ),
                    hashes.SHA256(),
                )
            except (InvalidSignature, ValueError) as error:
                raise ValueError("Goodix OTA RSA-PSS signature verification failed") from error
        else:
            raw_signature = firmware[
                app_size + RSA_SIGNATURE_OFFSET : app_size + RSA_SIGNATURE_OFFSET + ECDSA_SIGNATURE_SIZE
            ]
            if len(raw_signature) != ECDSA_SIGNATURE_SIZE or not any(raw_signature):
                raise ValueError("Goodix OTA ECDSA signature is missing")
            try:
                verification_key = ec.EllipticCurvePublicKey.from_encoded_point(
                    ec.SECP256R1(), public_key
                )
                verification_key.verify(
                    encode_dss_signature(
                        int.from_bytes(raw_signature[:32], "big"),
                        int.from_bytes(raw_signature[32:], "big"),
                    ),
                    firmware[: app_size + RSA_SIGNATURE_OFFSET],
                    ec.ECDSA(hashes.SHA256()),
                )
            except (InvalidSignature, ValueError) as error:
                raise ValueError("Goodix OTA ECDSA P-256 signature verification failed") from error
    return Package(path, manifest, firmware, image_info, firmware_type)
