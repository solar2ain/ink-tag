"""Shared decoder for the versioned InkTag OTA rejection record."""

from __future__ import annotations

import struct


OTA_REJECT_MAGIC = b"IK"
OTA_REJECT_VERSION = 1
OTA_REJECT_WIRE_SIZE = 15
OTA_REJECT_REASONS = {
    0: "UNSPECIFIED",
    1: "POWER_SAMPLE_INVALID",
    2: "VOLTAGE_BELOW_MIN",
    3: "IMAGE_INFO_INVALID",
    4: "ADDRESS_OR_LAYOUT_MISMATCH",
    5: "ERASE_FAILED",
    6: "WRITE_FAILED",
    7: "CHECKSUM_OR_SIGNATURE_FAILED",
    8: "INTERNAL",
}
OTA_REJECT_STAGES = {
    0: "UNKNOWN",
    1: "MODE_SWITCH",
    2: "PROGRAM_START",
    3: "ERASE",
    4: "PROGRAM_WRITE",
    5: "PROGRAM_END",
    6: "IMAGE_VERIFY",
    7: "SIGNATURE_VERIFY",
    8: "METADATA_COMMIT",
    9: "BOOT_CONFIRM",
}


def describe_ota_rejection(
    operation: str,
    response: bytes | bytearray | None,
    *,
    error_status: int,
    fallback_offset: int | None = None,
) -> str:
    """Describe one vendor-wrapped common rejection record."""
    data = bytes(response or b"")
    fallback = f", offset={fallback_offset}" if fallback_offset is not None else ""
    prefix = f"target rejected {operation}"
    if (
        len(data) < OTA_REJECT_WIRE_SIZE
        or data[0] != error_status
        or data[1:3] != OTA_REJECT_MAGIC
    ):
        return f"{prefix}: malformed rejection response{fallback}"
    if data[3] != OTA_REJECT_VERSION:
        return f"{prefix}: unsupported rejection extension v{data[3]}{fallback}"

    reason = OTA_REJECT_REASONS.get(data[4], f"UNKNOWN_0x{data[4]:02X}")
    stage = OTA_REJECT_STAGES.get(data[5], f"UNKNOWN_0x{data[5]:02X}")
    flags = data[6]
    detail = struct.unpack_from("<I", data, 7)[0]
    sampled_mv, threshold_mv = struct.unpack_from("<HH", data, 11)
    diagnostics = [f"reason={reason}", f"stage={stage}"]
    if flags & 0x04:
        diagnostics.append(f"address=0x{detail:08X}")
    elif flags & 0x08:
        diagnostics.append(f"offset={detail}")
    elif fallback_offset is not None:
        diagnostics.append(f"offset={fallback_offset}")
    if flags & 0x01:
        diagnostics.append(f"sampledMv={sampled_mv}")
    if flags & 0x02:
        diagnostics.append(f"thresholdMv={threshold_mv}")
    return f"{prefix}: " + ", ".join(diagnostics)
