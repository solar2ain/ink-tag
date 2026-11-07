from __future__ import annotations

from dataclasses import dataclass

import qrcode
from qrcode.constants import ERROR_CORRECT_M


MIN_QR_MODULES = 21
DEFAULT_QR_MODULES = 33
MAX_QR_MODULES = 57


@dataclass(frozen=True)
class DeviceQr:
    modules: int
    data: bytes


def encode_device_qr(value: str, minimum_modules: int = DEFAULT_QR_MODULES, *, error_correction=ERROR_CORRECT_M) -> DeviceQr:
    """Encode the one device-information QR as firmware row-major packed bits."""
    content = value.strip()
    if not content:
        raise ValueError("device-information QR content must not be empty")
    if (minimum_modules < MIN_QR_MODULES or minimum_modules > MAX_QR_MODULES or
            (minimum_modules - 21) % 4):
        raise ValueError("minimum QR size must be 21..57 modules in QR version steps")

    qr = qrcode.QRCode(
        version=(minimum_modules - 17) // 4,
        error_correction=error_correction,
        box_size=1,
        border=0,
    )
    qr.add_data(content)
    try:
        qr.make(fit=True)
    except (qrcode.exceptions.DataOverflowError, ValueError) as exc:
        raise ValueError("device-information QR content exceeds the firmware limit") from exc

    matrix = qr.get_matrix()
    modules = len(matrix)
    if modules > MAX_QR_MODULES:
        raise ValueError("device-information QR content exceeds the firmware limit")
    packed = bytearray((modules * modules + 7) // 8)
    for row, values in enumerate(matrix):
        for column, dark in enumerate(values):
            if dark:
                index = row * modules + column
                packed[index >> 3] |= 0x80 >> (index & 7)
    return DeviceQr(modules=modules, data=bytes(packed))
