"""App-only OTA context; Full partition validation belongs to the Full tools."""

import struct

TLSR_APP_LIMIT = 0x3F000
TLSR_LAYOUT = "layout-v1"
TLSR_LAYOUT_SIGNATURE = int.from_bytes(b"TLV1", "little")
TLSR_RECOVERY_CONTEXT_UUID = "00010203-0405-0607-0809-0a0b0c0d2b13"
TLSR_RECOVERY_ENTRY_UUID = "00010203-0405-0607-0809-0a0b0c0d2b14"
TLSR_RECOVERY_START = 0x40000
TLSR_BOOT_BINDING_OFFSET = 0x140


def tlsr_layout_profile(layout: str, *, application_start: int = 0) -> dict:
    if layout != TLSR_LAYOUT:
        raise ValueError("Telink package does not use the fixed Recovery layout-v1")
    if type(application_start) is not int or application_start not in (0, 0x1000):
        raise ValueError("Unknown Telink App entry address; matched complete firmware required")
    return {"limit": TLSR_APP_LIMIT, "signature": TLSR_LAYOUT_SIGNATURE, "start": application_start}


def tlsr_boot_binding(start: int) -> bytes:
    tlsr_layout_profile(TLSR_LAYOUT, application_start=start)
    return b'IAB1' + struct.pack('<IIII', start, TLSR_APP_LIMIT, TLSR_RECOVERY_START, 0x11000)


def tlsr_package_profile(layout: str, image: bytes) -> dict:
    if signed_tlsr_context(image)[2] != TLSR_LAYOUT_SIGNATURE:
        raise ValueError('Telink publisher signature metadata has an unsupported layout marker; no reinterpretation')
    at=TLSR_BOOT_BINDING_OFFSET
    if image[at:at+4] == b'IAB1':
        body_end=int.from_bytes(image[24:28], 'little')
        if at+20 > body_end or image[at:at+20] != tlsr_boot_binding(0x1000):
            raise ValueError('Invalid signed TLSR App address binding')
        return tlsr_layout_profile(layout, application_start=0x1000)
    # Historical signed zero-linked images have no such body record. This
    # never authorizes relocating them, and a new receiver proves 0x1000.
    return tlsr_layout_profile(layout)


def validate_tlsr_recovery_entry(raw: bytes, image: bytes) -> None:
    profile=tlsr_package_profile(TLSR_LAYOUT,image)
    if raw != tlsr_boot_binding(profile['start']):
        raise ValueError('Telink Recovery compiled entry address differs; Full is required')


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
    if (limit != TLSR_APP_LIMIT or layout != TLSR_LAYOUT_SIGNATURE
            or len(image) > limit or epoch > 0xFFFF
            or (epoch, board, layout) != signed_tlsr_context(image)):
        raise ValueError("Telink Recovery is incompatible with the signed image; Full is required")
