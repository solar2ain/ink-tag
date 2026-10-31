from __future__ import annotations

import re
import struct
from typing import Any
from .generated_release_v1 import FIND_CONFIG_VERSION, FIND_CAPABILITIES_VERSION, FIND_PUBLIC_VERSION


CONFIG_VERSION = FIND_CONFIG_VERSION
PUBLIC_KEY_BYTES = 28
CONFIG_WIRE_BYTES = 3
CONFIG_RESPONSE_WIRE_BYTES = 5
CAPABILITIES_VERSION_DERIVED = FIND_CAPABILITIES_VERSION
OWNER_NONCE_BYTES = 8
PUBLIC_ONLY_OWNER_VERSION = FIND_PUBLIC_VERSION
PUBLIC_ONLY_CAPABILITY = 0x100
PUBLIC_ONLY_OWNER_REPLACEMENT_CAPABILITY = 0x200
PUBLIC_ONLY_OWNER_STATUS_BYTES = 62
PUBLIC_ONLY_OWNER_COMMIT_BYTES = 53

OWNER_STATUS_IDENTITY_PRESENT = 0x01
OWNER_STATUS_CHALLENGE_OPEN = 0x02
OWNER_STATUS_SEED_STAGED = 0x04
OWNER_STATUS_TIME_VALID = 0x08


def normalize_public_key(value: str) -> str:
    normalized = re.sub(r"[^0-9a-fA-F]", "", value).upper()
    if len(normalized) != PUBLIC_KEY_BYTES * 2 or not re.fullmatch(r"[0-9A-F]+", normalized):
        raise ValueError("Find advertisement public key must be exactly 28 bytes of hexadecimal data")
    if normalized == "0" * (PUBLIC_KEY_BYTES * 2) or normalized == "F" * (PUBLIC_KEY_BYTES * 2):
        raise ValueError("Find advertisement public key is invalid")
    return normalized


def decode_capabilities(payload: bytes) -> dict[str, Any]:
    if len(payload) != 16 or payload[0] != CAPABILITIES_VERSION_DERIVED:
        raise ValueError("device returned invalid Find capabilities")
    flags = struct.unpack_from("<I", payload, 1)[0]
    if not (flags & 0x40) or (flags & 0x80):
        raise ValueError("legacy Find key formats are no longer supported; update device firmware")
    result: dict[str, Any] = {
        "version": payload[0],
        "flags": flags,
        "timeMultiplex": bool(flags & 0x02),
        "persistentConfig": bool(flags & 0x04),
        "pausesWhileConnected": bool(flags & 0x08),
        "experimental": bool(flags & 0x10),
        "normalWindowMs": struct.unpack_from("<H", payload, 5)[0],
        "findWindowMs": struct.unpack_from("<H", payload, 7)[0],
        "advertisementIntervalUnits": struct.unpack_from("<H", payload, 9)[0],
    }
    result.update({
        "rotationPeriodSeconds": struct.unpack_from("<H", payload, 14)[0],
        "derivedIdentity": True,
        "utcSlotRotation": True,
        "invalidTimeFallback": False,
        "ownerRotation": bool(flags & 0x20),
        "publicOnlyP224": bool(flags & PUBLIC_ONLY_CAPABILITY),
        "publicOnlyOwnerReplacement": bool(flags & PUBLIC_ONLY_OWNER_REPLACEMENT_CAPABILITY),
    })
    return result


def decode_config(payload: bytes) -> dict[str, Any]:
    if (
        len(payload) != CONFIG_RESPONSE_WIRE_BYTES
        or payload[0] != CONFIG_VERSION
        or payload[1] > 1
        or payload[2] != 1
        or payload[3] > 1
        or payload[4] > 1
    ):
        raise ValueError("device returned an invalid Find configuration")
    return {
        "version": payload[0],
        "enabled": payload[1] == 1,
        "mode": "time-derived-p224",
        "identityPresent": payload[3] == 1,
        "pausedForConnection": payload[4] == 1,
    }


def encode_config(enabled: bool) -> bytes:
    return bytes((CONFIG_VERSION, int(enabled), 1))


def decode_owner_rotation_challenge(payload: bytes) -> bytes:
    if len(payload) != 1 + OWNER_NONCE_BYTES or payload[0] != PUBLIC_ONLY_OWNER_VERSION:
        raise ValueError("device returned an invalid Find owner challenge")
    return payload[1:]


def decode_owner_status(payload: bytes) -> dict[str, object]:
    if (len(payload) != PUBLIC_ONLY_OWNER_STATUS_BYTES or
            payload[0] != PUBLIC_ONLY_OWNER_VERSION or payload[1] & ~0x0F):
        raise ValueError("device returned an invalid Find owner status")
    flags = payload[1]
    result: dict[str, object] = {
        "identityPresent": bool(flags & OWNER_STATUS_IDENTITY_PRESENT),
        "challengeOpen": bool(flags & OWNER_STATUS_CHALLENGE_OPEN),
        "seedStaged": bool(flags & OWNER_STATUS_SEED_STAGED),
        "timeValid": bool(flags & OWNER_STATUS_TIME_VALID),
        "nonce": bytes(payload[2:10]),
        "slot": int.from_bytes(payload[10:18], "little"),
        "publicKeyHex": payload[18:46].hex().upper(),
        "fingerprintHex": payload[46:62].hex().upper(),
        "version": PUBLIC_ONLY_OWNER_VERSION,
    }
    return result


def decode_owner_rotation_proof(payload: bytes) -> dict[str, object]:
    if len(payload) != PUBLIC_ONLY_OWNER_COMMIT_BYTES or payload[0] != PUBLIC_ONLY_OWNER_VERSION:
        raise ValueError("device returned an invalid Find owner rotation proof")
    result: dict[str, object] = {
        "slot": int.from_bytes(payload[1:9], "little"),
        "publicKeyHex": payload[9:37].hex().upper(),
        "fingerprintHex": payload[37:53].hex().upper(),
        "version": PUBLIC_ONLY_OWNER_VERSION,
    }
    return result


def decode_owner_rotation_status(payload: bytes) -> dict[str, object]:
    if (
        (len(payload) != PUBLIC_ONLY_OWNER_STATUS_BYTES or payload[0] != PUBLIC_ONLY_OWNER_VERSION)
        or payload[1] & ~0x0F
    ):
        raise ValueError("device returned an invalid Find owner rotation status")
    flags = payload[1]
    challenge_open = bool(flags & 0x02)
    nonce = payload[2:10]
    if not challenge_open and any(nonce):
        raise ValueError("device returned a stale Find owner nonce")
    result: dict[str, object] = {
        "version": payload[0],
        "identityPresent": bool(flags & 0x01),
        "challengeOpen": challenge_open,
        "seedStaged": bool(flags & 0x04),
        "timeValid": bool(flags & 0x08),
        "nonceHex": nonce.hex().upper() if challenge_open else "",
        "slot": int.from_bytes(payload[10:18], "little"),
        "publicKeyHex": payload[18:46].hex().upper(),
        "fingerprintHex": payload[46:62].hex().upper(),
    }
    return result
