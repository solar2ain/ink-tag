"""Host preparation for the pending TLSR password pilot; never log credentials."""
from __future__ import annotations

import hashlib
import hmac
import re
import secrets
import string
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

from .device_link import build_device_link
from .generated_release_v1 import VERSION

PASSWORD_WARNING = "The QR code contains a control credential. Do not share it."
RESET_SETTINGS_PROOF_DATA = b"IKT-RESET-SETTINGS-v1" + bytes((0x0E, 0x01, 0x05))


def validate_password(password: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9]{4,16}", password):
        raise ValueError("Password must be 4-16 ASCII letters/digits (case-sensitive)")
    return password


def validate_password_change(old_password: str, new_password: str, confirmation: str) -> None:
    """Input validation only; the device must independently verify the old password."""
    validate_password(old_password)
    validate_password(new_password)
    if new_password != confirmation:
        raise ValueError("New passwords do not match")


def generate_password(length: int = 8) -> str:
    if not isinstance(length, int) or isinstance(length, bool) or not 4 <= length <= 16:
        raise ValueError("Password length must be 4-16")
    return "".join(secrets.choice(string.ascii_letters + string.digits) for _ in range(length))


def device_id_bytes(device_id: str) -> bytes:
    if not re.fullmatch(r"[0-9a-fA-F]{16}", device_id) or int(device_id, 16) == 0:
        raise ValueError("A nonzero immutable device ID is required")
    return bytes.fromhex(device_id)


def password_credential(device_id: str, password: str) -> bytes:
    return hashlib.sha256(b"IKT-DEVICE-PASSWORD-KEY-v1" + device_id_bytes(device_id)
                          + validate_password(password).encode("ascii")).digest()


def password_change_key(device_id: str, password: str) -> bytes:
    """Independent W: never stored in a QR or derived from its bearer V."""
    return hashlib.sha256(b"IKT-DEVICE-PASSWORD-CHANGE-v1" + device_id_bytes(device_id)
                          + validate_password(password).encode("ascii")).digest()


def decode_status(payload: bytes) -> dict:
    if (len(payload) != 12 or payload[0] != 1 or payload[1] > 1 or payload[2] > 1
            or (not payload[1] and payload[2])):
        raise ValueError("Invalid device password status")
    revision = int.from_bytes(payload[8:12], "little")
    if payload[1] and not revision:
        raise ValueError("Invalid device password revision")
    return {"enabled": bool(payload[1]), "authorized": bool(payload[2]),
            "failures": payload[3], "retryAfterMs": int.from_bytes(payload[4:8], "little"),
            "revision": revision}


def password_proof(device_id: str, credential: bytes, challenge: bytes,
                   candidate_bundle: bytes | None = None) -> bytes:
    """For purpose 1/2 pass W and the exact staged bundle; AUTH uses V only."""
    if (len(credential) != 32 or not any(credential) or len(challenge) != 22
            or challenge[0] != 1 or challenge[1] > 2 or not any(challenge[2:18])
            or not any(challenge[18:22])):
        raise ValueError("Invalid password credential or challenge")
    purpose = challenge[1]
    if (purpose == 0) != (candidate_bundle is None):
        raise ValueError("Change/disable proof requires the exact candidate bundle")
    if candidate_bundle is not None and not (
        purpose == 2 and candidate_bundle == RESET_SETTINGS_PROOF_DATA
    ):
        if (len(candidate_bundle) < 73 or candidate_bundle[0] != VERSION
                or candidate_bundle[1] != int(purpose == 1)):
            raise ValueError("Candidate does not match challenge purpose")
    digest = hashlib.sha256(candidate_bundle).digest() if candidate_bundle is not None else b""
    message = (b"IKT-DEVICE-PASSWORD-PROOF-v1" + device_id_bytes(device_id)
               + challenge[18:22] + challenge[2:18] + bytes((purpose,)) + digest)
    return bytes((1, purpose)) + digest + hmac.new(credential, message, hashlib.sha256).digest()[:16]


def build_password_qr_link(name: str, device_id: str, credential: bytes) -> str:
    device_id_bytes(device_id)
    if len(credential) != 32 or not any(credential):
        raise ValueError("Invalid QR credential")
    return build_device_link(name) + "#/?" + urlencode(
        {"device": device_id.upper(), "auth": credential.hex().upper()})


def parse_password_qr_link(url: str) -> tuple[str, bytes, str]:
    """Return an untrusted import candidate, NOT an authenticated enrollment."""
    candidate = urlsplit(url)
    base = urlsplit(build_device_link())
    if (candidate.scheme != base.scheme or candidate.netloc != base.netloc
            or candidate.path != base.path or not candidate.fragment.startswith("/?")):
        raise ValueError("Invalid password QR link")
    fields = parse_qs(candidate.fragment[2:], keep_blank_values=True, strict_parsing=True)
    if set(fields) != {"device", "auth"} or any(len(value) != 1 for value in fields.values()):
        raise ValueError("Invalid password QR fields")
    device_id = fields["device"][0].upper()
    device_id_bytes(device_id)
    if not re.fullmatch(r"[0-9a-fA-F]{64}", fields["auth"][0]):
        raise ValueError("Invalid password QR credential")
    credential = bytes.fromhex(fields["auth"][0])
    if not any(credential):
        raise ValueError("Invalid password QR credential")
    # Remove credentials before any navigation, logging or NFC synchronization.
    clean = urlunsplit((candidate.scheme, candidate.netloc, candidate.path, candidate.query, ""))
    return device_id, credential, clean


def encode_password_bundle(credential: bytes | None, modules: int, packed_qr: bytes,
                           *, change_key: bytes | None = None, revision: int = 1) -> bytes:
    if modules not in range(21, 58, 4) or len(packed_qr) != (modules * modules + 7) // 8:
        raise ValueError("Invalid password QR matrix")
    padding = len(packed_qr) * 8 - modules * modules
    if padding and packed_qr[-1] & ((1 << padding) - 1):
        raise ValueError("Invalid QR padding")
    if not isinstance(revision, int) or isinstance(revision, bool) or not 1 <= revision <= 0xFFFFFFFF:
        raise ValueError("Invalid password revision")
    if (credential is None) != (change_key is None):
        raise ValueError("Both independent password keys are required")
    if credential is not None and (len(credential) != 32 or not any(credential)
                                  or len(change_key) != 32 or not any(change_key)
                                  or credential == change_key):
        raise ValueError("Invalid password credential")
    if credential is not None and 73 + len(packed_qr) > 420:
        raise ValueError("Password QR exceeds existing Settings record capacity")
    return (bytes((VERSION, int(credential is not None), modules))
            + len(packed_qr).to_bytes(2, "little") + revision.to_bytes(4, "little")
            + (credential or bytes(32)) + (change_key or bytes(32)) + packed_qr)
