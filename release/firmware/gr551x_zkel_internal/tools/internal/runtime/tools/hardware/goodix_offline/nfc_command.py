from __future__ import annotations

from dataclasses import dataclass
import zlib

from .generated_commands_v2 import NfcCommandV2


NFC_COMMAND_FORMAT_VERSION = 1
NFC_COMMAND_MAX_PAYLOAD_BYTES = 8
NFC_COMMAND_REUSABLE_SEQUENCE = 0
NFC_NDEF_MAX_BYTES = 192


@dataclass(frozen=True)
class NfcCommandRecord:
    sequence: int
    opcode: int
    payload: bytes = b""


_NO_PAYLOAD = {
    int(NfcCommandV2.PING),
    int(NfcCommandV2.DEVICE_INFO),
    int(NfcCommandV2.CALENDAR),
    int(NfcCommandV2.CLOCK),
    int(NfcCommandV2.NEXT_IMAGE),
    int(NfcCommandV2.PREVIOUS_IMAGE),
    int(NfcCommandV2.PAUSE),
    int(NfcCommandV2.OPEN_BLE_TRANSFER),
    int(NfcCommandV2.SLIDESHOW),
    int(NfcCommandV2.CLEAR),
}


def _payload_valid(opcode: int, payload: bytes) -> bool:
    if opcode in _NO_PAYLOAD:
        return not payload
    return opcode == int(NfcCommandV2.SHOW_SLOT) and len(payload) == 1


def sequence_is_newer(candidate: int, previous: int) -> bool:
    candidate &= 0xFFFFFFFF
    previous &= 0xFFFFFFFF
    if candidate == 0:
        return False
    if previous == 0:
        return True
    difference = (candidate - previous) & 0xFFFFFFFF
    return 0 < difference < 0x80000000


def encode_command_text(command: NfcCommandRecord) -> str:
    sequence = command.sequence & 0xFFFFFFFF
    payload = bytes(command.payload)
    if len(payload) > NFC_COMMAND_MAX_PAYLOAD_BYTES or not _payload_valid(command.opcode, payload):
        raise ValueError("invalid NFC command sequence, opcode, or payload")
    prefix = f"IKNC1:{sequence:08X}:{command.opcode:02X}:{payload.hex().upper()}:"
    checksum = zlib.crc32(prefix.encode("ascii"))
    return f"{prefix}{checksum:08X}"


def decode_command_text(text: str) -> NfcCommandRecord:
    if not 27 <= len(text) <= 43:
        raise ValueError("invalid NFC command length")
    fields = text.split(":")
    if len(fields) != 5 or not fields[0].startswith("IKNC"):
        raise ValueError("invalid NFC command format")
    if fields[0] != f"IKNC{NFC_COMMAND_FORMAT_VERSION}":
        raise ValueError("unsupported NFC command version")
    if len(fields[1]) != 8 or len(fields[2]) != 2 or len(fields[4]) != 8 or len(fields[3]) % 2:
        raise ValueError("invalid NFC command format")
    if len(fields[3]) // 2 > NFC_COMMAND_MAX_PAYLOAD_BYTES:
        raise ValueError("NFC command payload is too long")
    try:
        sequence = int(fields[1], 16)
        opcode = int(fields[2], 16)
        payload = bytes.fromhex(fields[3])
        expected = int(fields[4], 16)
    except ValueError as exc:
        raise ValueError("invalid NFC command hex") from exc
    prefix = text[:-8]
    if zlib.crc32(prefix.encode("ascii")) != expected:
        raise ValueError("NFC command CRC mismatch")
    if not _payload_valid(opcode, payload):
        raise ValueError("invalid NFC command opcode or payload")
    return NfcCommandRecord(sequence, opcode, payload)


def decode_capabilities(payload: bytes) -> dict[str, int]:
    if len(payload) != 10 or payload[0] != 1:
        raise ValueError("invalid NFC capabilities response")
    result = {
        "flags": int.from_bytes(payload[1:3], "little"),
        "commandMask": int.from_bytes(payload[3:5], "little"),
        "maxNdefBytes": int.from_bytes(payload[5:7], "little"),
        "maxCommandPayload": payload[7],
        "maxUidBytes": payload[8],
        "commandFormatVersion": payload[9],
    }
    if (
        result["maxNdefBytes"] > NFC_NDEF_MAX_BYTES
        or result["maxCommandPayload"] > NFC_COMMAND_MAX_PAYLOAD_BYTES
        or result["maxUidBytes"] > 10
        or result["commandFormatVersion"] != 1
    ):
        raise ValueError("NFC capabilities exceed the 0.2.0 bounds")
    return result


def decode_status(payload: bytes) -> dict[str, int | str]:
    if len(payload) != 20 or payload[0] != 1 or payload[8] > 10:
        raise ValueError("invalid NFC status response")
    return {
        "flags": payload[1],
        "lastResult": payload[2],
        "lastOpcode": payload[3],
        "lastSequence": int.from_bytes(payload[4:8], "little"),
        "uid": payload[9:9 + payload[8]].hex().upper(),
        "diagnosticFlags": payload[19],
    }


def encode_ndef(ndef: bytes) -> bytes:
    if len(ndef) > NFC_NDEF_MAX_BYTES:
        raise ValueError("NDEF data cannot exceed 192 bytes")
    return bytes((1,)) + len(ndef).to_bytes(2, "little") + ndef


def decode_ndef(payload: bytes) -> bytes:
    if len(payload) < 3 or payload[0] != 1:
        raise ValueError("invalid NDEF response")
    length = int.from_bytes(payload[1:3], "little")
    if length > NFC_NDEF_MAX_BYTES or len(payload) != length + 3:
        raise ValueError("invalid NDEF response length")
    return payload[3:]


def encode_uri_ndef(url: str) -> bytes:
    """Encode one unchunked short NFC Forum URI record for an HTTPS URL."""
    normalized = url.strip()
    prefix = "https://"
    if not normalized.lower().startswith(prefix):
        raise ValueError("NFC WebUI link must use HTTPS")
    suffix = normalized[len(prefix):].encode("utf-8")
    payload = bytes((0x04,)) + suffix  # NFC URI prefix code for https://
    if len(payload) > 0xFF:
        raise ValueError("NFC WebUI link is too long for a short URI record")
    record = bytes((0xD1, 0x01, len(payload), 0x55)) + payload
    if len(record) > NFC_NDEF_MAX_BYTES:
        raise ValueError("NDEF data cannot exceed 192 bytes")
    return record
