"""Owner-side Find v2 scalar derivation; never send the master scalar to BLE."""

from __future__ import annotations

import hashlib
import hmac
import secrets


VERSION = 2
PERIOD_SECONDS = 900
ORDER = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D
LABEL = b"InkTag/Find/public-only/v2"
REPLACEMENT_LABEL = b"InkTag/Find/public-only/replace/v1\x00"
REPLACEMENT_STAGE_BYTES = 113


def _coefficient(key: bytes, slot: int, retry: int, role: bytes) -> int:
    if len(key) != 16 or not 0 <= slot < 1 << 64 or not 0 <= retry < 1 << 32:
        raise ValueError("invalid Find v2 key, slot, or retry")
    if role not in (b"u", b"v"):
        raise ValueError("invalid Find v2 coefficient role")
    prefix = LABEL + b"\x00" + role + slot.to_bytes(8, "big") + retry.to_bytes(4, "big")
    for sample in range(1 << 32):
        digest = hmac.digest(key, prefix + sample.to_bytes(4, "big"), "sha256")
        value = int.from_bytes(digest[:28], "big")
        if value < ORDER and (role == b"v" or value != 0):
            return value
    raise OverflowError("Find v2 coefficient sample counter exhausted")


def derive_private_scalar(master_scalar: int, key: bytes, slot: int) -> int:
    if not 1 <= master_scalar < ORDER:
        raise ValueError("invalid Find v2 master scalar")
    if not 0 <= slot < 1 << 64:
        raise ValueError("invalid Find v2 slot")
    for retry in range(1 << 32):
        u = _coefficient(key, slot, retry, b"u")
        v = _coefficient(key, slot, retry, b"v")
        scalar = (u * master_scalar + v) % ORDER
        if scalar:
            return scalar
    raise OverflowError("Find v2 slot retry counter exhausted")


def new_owner_material() -> tuple[int, bytes]:
    """Generate independent secrets using the host OS cryptographic source."""
    key = secrets.token_bytes(16)
    while key in (bytes(16), bytes([0xFF]) * 16):
        key = secrets.token_bytes(16)
    return secrets.randbelow(ORDER - 1) + 1, key


def master_public_bytes(master_scalar: int) -> bytes:
    if not 1 <= master_scalar < ORDER:
        raise ValueError("invalid Find v2 master scalar")
    from cryptography.hazmat.primitives.asymmetric import ec

    point = ec.derive_private_key(master_scalar, ec.SECP224R1()).public_key().public_numbers()
    return point.x.to_bytes(28, "big") + point.y.to_bytes(28, "big")


def identity_fingerprint(public_xy: bytes, key: bytes) -> str:
    if len(public_xy) != 56 or len(key) != 16:
        raise ValueError("invalid Find v2 public owner material")
    return hashlib.sha256(LABEL + public_xy + key).hexdigest()[:32]


def replacement_stage(
    old_key: bytes, activation_id: bytes, nonce: bytes,
    old_fingerprint: bytes, new_public_xy: bytes, new_key: bytes,
) -> bytes:
    """Authenticate a v2 owner replacement without sending either master scalar."""
    if (len(old_key), len(activation_id), len(nonce), len(old_fingerprint),
            len(new_public_xy), len(new_key)) != (16, 8, 8, 16, 56, 16):
        raise ValueError("invalid Find v2 owner replacement fields")
    payload = bytes((VERSION,)) + nonce + old_fingerprint + new_public_xy + new_key
    tag = hmac.digest(
        old_key, REPLACEMENT_LABEL + activation_id + payload, "sha256"
    )[:16]
    return payload + tag
