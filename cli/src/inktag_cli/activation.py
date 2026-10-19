from __future__ import annotations

import base64
import struct

from .generated_commands_v2 import ActivationResultV2, ActivationStateV2
from .models import ActivationRequest, ActivationStatus


CODE_CHARS = 128
CODE_BYTES = 96
STATUS_BYTES = 24
REQUEST_BYTES = 32


def normalize_code(value: str) -> str:
    return value.strip()


def decode_code(value: str) -> bytes:
    compact = normalize_code(value)
    if len(compact) != CODE_CHARS:
        raise ValueError("activation code must contain exactly 128 characters")
    if any(not (character.isalnum() or character in "-_") for character in compact):
        raise ValueError("activation code contains a non-Base64URL character")
    padding = "=" * ((4 - len(compact) % 4) % 4)
    try:
        token = base64.b64decode(compact + padding, altchars=b"-_", validate=True)
    except ValueError as exc:
        raise ValueError("activation code is not valid Base64URL") from exc
    canonical = base64.urlsafe_b64encode(token).rstrip(b"=").decode("ascii")
    if len(token) != CODE_BYTES or canonical != compact:
        raise ValueError("activation code is not canonical Base64URL")
    if token[:3] != b"IA\x01":
        raise ValueError("activation code is not IKAC1 format version 1")
    return token


def parse_status(payload: bytes) -> ActivationStatus:
    if len(payload) != STATUS_BYTES or payload[0] != 2:
        raise ValueError("device returned an invalid activation status")
    phases = {
        int(ActivationStateV2.UNAVAILABLE): "unavailable",
        int(ActivationStateV2.FREE): "free",
        int(ActivationStateV2.PRO): "pro",
        int(ActivationStateV2.INVALID): "invalid",
    }
    if payload[1] not in phases:
        raise ValueError("device returned an unknown activation state")
    result = payload[3]
    return ActivationStatus(
        state=phases[payload[1]],
        activation_id=payload[16:24].hex().upper(),
        scheme=payload[2],
        last_result=result,
        result_name=(
            ActivationResultV2(result).name.lower()
            if result in {int(value) for value in ActivationResultV2}
            else f"unknown_{result}"
        ),
        code_length=payload[6],
        key_id=payload[4],
        sequence=struct.unpack_from("<I", payload, 8)[0],
        feature_mask=struct.unpack_from("<I", payload, 12)[0],
        request_active=bool(payload[5] & 0x02),
        active=bool(payload[5] & 0x01),
    )


def parse_request(payload: bytes) -> ActivationRequest:
    if len(payload) != REQUEST_BYTES or payload[0] != 1:
        raise ValueError("device returned an invalid activation request")
    device_id = payload[16:24].hex().upper()
    challenge = payload[24:32].hex().upper()
    if device_id == "0" * 16 or challenge == "0" * 16:
        raise ValueError("device returned an empty activation identity or challenge")
    return ActivationRequest(
        scheme=payload[1],
        key_id=payload[2],
        product_id=struct.unpack_from("<H", payload, 4)[0],
        security_epoch=struct.unpack_from("<H", payload, 6)[0],
        supported_features=struct.unpack_from("<I", payload, 8)[0],
        installed_sequence=struct.unpack_from("<I", payload, 12)[0],
        device_id=device_id,
        challenge=challenge,
        request_code=f"{device_id}:{challenge}",
    )
