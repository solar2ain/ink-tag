"""App-only OTA context; Full partition validation belongs to the Full tools."""

import struct

TLSR_APP_LIMIT = 0x3F000
TLSR_LAYOUT = "layout-v1"
TLSR_LAYOUT_SIGNATURE = int.from_bytes(b"TLV1", "little")
TLSR_RECOVERY_CONTEXT_UUID = "00010203-0405-0607-0809-0a0b0c0d2b13"
TLSR_RECOVERY_START = 0x40000


def tlsr_layout_profile(layout: str) -> dict:
    if layout != TLSR_LAYOUT:
        raise ValueError("Telink package does not use a supported fixed Recovery layout")
    return {"limit": TLSR_APP_LIMIT, "signature": TLSR_LAYOUT_SIGNATURE}


def validate_tlsr_epoch(layout: str, epoch: int) -> None:
    tlsr_layout_profile(layout)
    if type(epoch) is not int or not 0 <= epoch <= 0xFFFF:
        raise ValueError("Telink compatibility epoch does not match the layout contract")


def signed_tlsr_context(image: bytes) -> tuple[int, int, int]:
    """Read context from an already publisher-verified image, not its JSON map."""
    if len(image) < 128:
        raise ValueError("Telink signed image is too short")
    app_size = struct.unpack_from("<I", image, 24)[0] + 4
    if app_size + 96 != len(image):
        raise ValueError("Telink signed image length mismatch")
    return struct.unpack_from("<III", image, app_size + 12)  # epoch, board, layout


def validate_tlsr_recovery_context(raw: bytes, image: bytes) -> None:
    if len(raw) != 20 or raw[:4] != b"ITC1":
        raise ValueError("Telink Recovery compatibility response is invalid")
    limit, epoch, layout, board = struct.unpack_from("<IIII", raw, 4)
    if (limit < 0x1000 or limit > TLSR_RECOVERY_START or limit % 0x1000
            or len(image) > limit or epoch > 0xFFFF or layout != TLSR_LAYOUT_SIGNATURE
            or (epoch, board, layout) != signed_tlsr_context(image)):
        raise ValueError("Telink Recovery is incompatible with the signed image; Full is required")
