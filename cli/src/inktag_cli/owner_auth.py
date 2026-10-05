"""Version 1 owner-action proof shared by Pro recovery and Find management.

This module constructs a proof; only the device can decide whether the
currently installed credential matches it. Never log the code, K, or key.
"""

from __future__ import annotations

import hashlib
import hmac
import struct

from .activation import decode_code


VERSION = 1
METHOD_INSTALLED_PRO_CODE = 1
METHOD_CURRENT_FIND_K = 2
NONCE_BYTES = 16
ACTION_HASH_BYTES = 32
TAG_BYTES = 16
MAX_ACTION_PAYLOAD_BYTES = 192
CHALLENGE_RESPONSE_BYTES = 1 + NONCE_BYTES
AUTHORIZE_REQUEST_BYTES = 2 + ACTION_HASH_BYTES + TAG_BYTES

_PRO_DOMAIN = b"IKT-OWNER-PRO-v1"
_FIND_DOMAIN = b"IKT-OWNER-FIND-v1"
_ACTION_DOMAIN = b"IKT-OWNER-ACTION-v1"


def parse_challenge(payload: bytes) -> bytes:
    if len(payload) != CHALLENGE_RESPONSE_BYTES or payload[0] != VERSION:
        raise ValueError("device returned an invalid owner challenge")
    nonce = payload[1:]
    if not any(nonce):
        raise ValueError("device returned an empty owner challenge")
    return nonce


def action_hash(opcode: int, flags: int, payload: bytes = b"") -> bytes:
    if not 0 <= opcode <= 255 or not 0 <= flags <= 255:
        raise ValueError("owner action opcode or flags out of range")
    if len(payload) > MAX_ACTION_PAYLOAD_BYTES:
        raise ValueError("owner action payload is too long")
    return hashlib.sha256(
        bytes((opcode, flags)) + struct.pack("<H", len(payload)) + payload
    ).digest()


def _encode_proof_hash(
    method: int, secret: bytes, device_id: bytes, nonce: bytes,
    digest: bytes,
) -> bytes:
    if len(device_id) != 8 or not any(device_id):
        raise ValueError("owner proof needs an immutable 8-byte device ID")
    if len(nonce) != NONCE_BYTES or not any(nonce):
        raise ValueError("owner proof needs a nonempty 16-byte challenge")
    if len(digest) != ACTION_HASH_BYTES:
        raise ValueError("owner proof needs a 32-byte action hash")
    domain = _PRO_DOMAIN if method == METHOD_INSTALLED_PRO_CODE else _FIND_DOMAIN
    derived_key = hashlib.sha256(domain + secret).digest()
    tag = hmac.digest(
        derived_key, _ACTION_DOMAIN + device_id + nonce + digest, "sha256"
    )[:TAG_BYTES]
    return bytes((VERSION, method)) + digest + tag


def _encode_proof(
    method: int, secret: bytes, device_id: bytes, nonce: bytes,
    opcode: int, flags: int, payload: bytes,
) -> bytes:
    return _encode_proof_hash(
        method, secret, device_id, nonce, action_hash(opcode, flags, payload)
    )


def prove_with_pro_code(
    code: str, device_id: bytes, nonce: bytes,
    opcode: int, flags: int, payload: bytes = b"",
) -> bytes:
    token = decode_code(code)
    if len(device_id) != 8 or token[16:24] != device_id:
        raise ValueError("Pro activation code belongs to a different device")
    return _encode_proof(
        METHOD_INSTALLED_PRO_CODE, token, device_id,
        nonce, opcode, flags, payload,
    )


def prove_with_pro_action_hash(
    code: str, device_id: bytes, nonce: bytes, digest: bytes,
) -> bytes:
    token = decode_code(code)
    if len(device_id) != 8 or token[16:24] != device_id:
        raise ValueError("Pro activation code belongs to a different device")
    return _encode_proof_hash(
        METHOD_INSTALLED_PRO_CODE, token, device_id, nonce, digest
    )


def prove_with_find_key(
    key: bytes, device_id: bytes, nonce: bytes,
    opcode: int, flags: int, payload: bytes = b"",
) -> bytes:
    if len(key) != 16 or not any(key):
        raise ValueError("Find owner K must contain 16 nonzero bytes")
    return _encode_proof(
        METHOD_CURRENT_FIND_K, key, device_id,
        nonce, opcode, flags, payload,
    )


def prove_with_find_action_hash(
    key: bytes, device_id: bytes, nonce: bytes, digest: bytes,
) -> bytes:
    """Sign an exact request hash without returning the local Find secret."""
    if len(key) != 16 or not any(key):
        raise ValueError("Find owner K must contain 16 nonzero bytes")
    return _encode_proof_hash(
        METHOD_CURRENT_FIND_K, key, device_id, nonce, digest
    )
