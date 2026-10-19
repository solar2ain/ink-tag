from __future__ import annotations

import re
import struct
import zlib
from dataclasses import dataclass

from .models import ScreenConfig
from .protocol import SCREEN_WIRE_SIZE, encode_screen_config


TRANSFER_MONO_1BPP = 1
TRANSFER_DUAL_1BPP = 2
TRANSFER_INDEXED_2BPP = 3
TRANSFER_PLANAR_2X1BPP = 4
TRANSFER_INDEXED_4BPP = 5
TRANSFER_INDEXED_2X2BPP = 6

CONTENT_TYPE_IDS = {
    "image": 0,
    "schedule": 1,
    "wifi": 2,
    "todo": 3,
    "business": 4,
    "page": 5,
}


@dataclass(frozen=True)
class RgbaFrame:
    width: int
    height: int
    data: bytes

    def __post_init__(self) -> None:
        if self.width < 1 or self.height < 1:
            raise ValueError("image dimensions must be positive")
        if len(self.data) != self.width * self.height * 4:
            raise ValueError("RGBA byte count does not match image dimensions")


@dataclass(frozen=True)
class ImageTransfer:
    step: str
    data: bytes


@dataclass(frozen=True)
class EncodedImage:
    format: int
    transfers: tuple[ImageTransfer, ...]

    @property
    def total_bytes(self) -> int:
        return sum(len(transfer.data) for transfer in self.transfers)

    @property
    def black_mask(self) -> int:
        return sum(
            1 << index
            for index, transfer in enumerate(self.transfers)
            if transfer.step in {"bw", "gray-msb", "e6-area0"}
        )


def canvas_size_for_screen(screen: ScreenConfig) -> tuple[int, int]:
    rotation = screen.transform & 3
    if rotation in {1, 3}:
        return screen.visible_height, screen.visible_width
    return screen.visible_width, screen.visible_height


def transfer_format_for(screen: ScreenConfig) -> int:
    if screen.driver == 0x32 and screen.recipe == 1:
        return TRANSFER_INDEXED_2X2BPP
    if screen.driver in {0x30, 0x31, 0x33, 0x3F}:
        return TRANSFER_INDEXED_2BPP
    if screen.driver in {0x12, 0x40, 0x41}:
        return TRANSFER_INDEXED_4BPP
    if screen.color == 4:
        return TRANSFER_PLANAR_2X1BPP
    return TRANSFER_MONO_1BPP if screen.color == 1 else TRANSFER_DUAL_1BPP


def transform_rgba(image: RgbaFrame, transform: int) -> RgbaFrame:
    rotation = transform & 3
    mirror_x = bool(transform & 4)
    mirror_y = bool(transform & 8)
    width = image.height if rotation in {1, 3} else image.width
    height = image.width if rotation in {1, 3} else image.height
    output = bytearray(width * height * 4)
    for y in range(image.height):
        for x in range(image.width):
            tx, ty = x, y
            if rotation == 1:
                tx, ty = image.height - 1 - y, x
            elif rotation == 2:
                tx, ty = image.width - 1 - x, image.height - 1 - y
            elif rotation == 3:
                tx, ty = y, image.width - 1 - x
            if mirror_x:
                tx = width - 1 - tx
            if mirror_y:
                ty = height - 1 - ty
            source = (y * image.width + x) * 4
            target = (ty * width + tx) * 4
            output[target : target + 4] = image.data[source : source + 4]
    return RgbaFrame(width, height, bytes(output))


def encode_image(image: RgbaFrame, screen: ScreenConfig) -> EncodedImage:
    frame = _framed_image(image, screen)
    transfer_format = transfer_format_for(screen)
    if transfer_format == TRANSFER_MONO_1BPP:
        return EncodedImage(transfer_format, (ImageTransfer("bw", _mono(frame)),))
    if transfer_format == TRANSFER_DUAL_1BPP:
        black, accent = _dual(frame, screen.color)
        return EncodedImage(
            transfer_format,
            (ImageTransfer("bw", black), ImageTransfer("red", accent)),
        )
    if transfer_format == TRANSFER_INDEXED_2BPP:
        return EncodedImage(transfer_format, (ImageTransfer("color", _indexed2(frame)),))
    if transfer_format == TRANSFER_PLANAR_2X1BPP:
        ram1, ram2 = _gray4(frame)
        return EncodedImage(
            transfer_format,
            (ImageTransfer("gray-msb", ram1), ImageTransfer("gray-lsb", ram2)),
        )
    if transfer_format == TRANSFER_INDEXED_4BPP:
        if screen.driver in {0x40, 0x41}:
            return EncodedImage(transfer_format, (ImageTransfer("bw", _osprey_e6(frame)),))
        if screen.color == 1:
            black = _mono(frame)
            accent = bytes((0xFF,)) * len(black)
        else:
            black, accent = _dual(frame, screen.color)
        return EncodedImage(
            transfer_format,
            (ImageTransfer("bw", _indexed4_from_planes(black, accent)),),
        )
    if transfer_format == TRANSFER_INDEXED_2X2BPP:
        area0, area6 = _lenmory_e6(frame)
        return EncodedImage(
            transfer_format,
            (ImageTransfer("e6-area0", area0), ImageTransfer("e6-area6", area6)),
        )
    raise ValueError("the selected screen transfer format is not implemented")


def decode_stored_image(
    payload: bytes,
    screen: ScreenConfig,
    transfer_format: int,
    segments: int,
) -> RgbaFrame:
    """Decode device-ready image-record bytes into the logical visible frame."""
    pixels = screen.width * screen.height
    byte_width = (screen.width + 7) // 8
    mono_bytes = byte_width * screen.height
    two_bit_bytes = (pixels + 3) // 4
    output = bytearray(pixels * 4)
    black = (0, 0, 0)
    white = (255, 255, 255)
    red = _ACCENTS[2]
    yellow = _ACCENTS[5]
    green = (41, 204, 20)
    blue = (0, 0, 255)
    accent = _ACCENTS.get(screen.color, red)

    def put_pixel(pixel: int, rgb: tuple[int, int, int]) -> None:
        target = pixel * 4
        output[target : target + 4] = bytes((*rgb, 0xFF))

    if transfer_format == TRANSFER_MONO_1BPP:
        if segments != 1 or len(payload) != mono_bytes:
            raise ValueError("invalid monochrome image-record length")
        for y in range(screen.height):
            for x in range(screen.width):
                value = (payload[y * byte_width + (x >> 3)] >> (7 - (x & 7))) & 1
                put_pixel(y * screen.width + x, white if value else black)
    elif transfer_format == TRANSFER_DUAL_1BPP:
        if segments != 2 or len(payload) != mono_bytes * 2:
            raise ValueError("invalid dual-plane image-record length")
        accent_plane = payload[mono_bytes:]
        for y in range(screen.height):
            for x in range(screen.width):
                index = y * byte_width + (x >> 3)
                bit = 7 - (x & 7)
                is_accent = ((accent_plane[index] >> bit) & 1) == 0
                is_white = ((payload[index] >> bit) & 1) != 0
                put_pixel(
                    y * screen.width + x,
                    accent if is_accent else white if is_white else black,
                )
    elif transfer_format == TRANSFER_INDEXED_2BPP:
        if segments != 1 or len(payload) != two_bit_bytes:
            raise ValueError("invalid packed two-bit image-record length")
        palette = (black, white, yellow, red)
        for pixel in range(pixels):
            value = (payload[pixel >> 2] >> (6 - (pixel & 3) * 2)) & 3
            put_pixel(pixel, palette[value])
    elif transfer_format == TRANSFER_PLANAR_2X1BPP:
        if segments != 2 or len(payload) != mono_bytes * 2:
            raise ValueError("invalid grayscale image-record length")
        second_plane = payload[mono_bytes:]
        levels = (0, 85, 170, 255)
        for y in range(screen.height):
            for x in range(screen.width):
                index = y * byte_width + (x >> 3)
                bit = 7 - (x & 7)
                low = (((~payload[index]) & 0xFF) >> bit) & 1
                high = (((~second_plane[index]) & 0xFF) >> bit) & 1
                level = levels[(high << 1) | low]
                put_pixel(y * screen.width + x, (level, level, level))
    elif transfer_format == TRANSFER_INDEXED_4BPP:
        expected = (pixels + 1) // 2
        if segments != 1 or len(payload) != expected:
            raise ValueError("invalid packed four-bit image-record length")
        for pixel in range(pixels):
            shift = 0 if pixel & 1 else 4
            value = (payload[pixel >> 1] >> shift) & 0x0F
            if screen.driver in {0x40, 0x41}:
                palette = {0: black, 1: white, 2: yellow, 3: red, 5: blue, 6: green}
                if value not in palette:
                    raise ValueError("invalid Osprey E6 color code")
                put_pixel(pixel, palette[value])
            else:
                put_pixel(pixel, black if value == 0 else accent if value == 4 else white)
    elif transfer_format == TRANSFER_INDEXED_2X2BPP:
        if segments != 2 or len(payload) != two_bit_bytes * 2:
            raise ValueError("invalid six-color image-record length")
        second_area = payload[two_bit_bytes:]
        palette = {
            (1, 0): black,
            (1, 1): white,
            (3, 1): red,
            (1, 2): yellow,
            (2, 1): green,
            (0, 1): blue,
        }
        for pixel in range(pixels):
            shift = 6 - (pixel & 3) * 2
            first = (payload[pixel >> 2] >> shift) & 3
            second = (second_area[pixel >> 2] >> shift) & 3
            put_pixel(pixel, palette.get((first, second), white))
    else:
        raise ValueError(f"unsupported image-record format {transfer_format}")

    return _logical_image(RgbaFrame(screen.width, screen.height, bytes(output)), screen)


def _logical_image(frame: RgbaFrame, screen: ScreenConfig) -> RgbaFrame:
    """Crop controller padding and reverse the saved screen transform."""
    if (frame.width, frame.height) != (screen.width, screen.height):
        raise ValueError("image-record dimensions do not match the current screen")
    visible = bytearray(screen.visible_width * screen.visible_height * 4)
    row_bytes = screen.visible_width * 4
    for y in range(screen.visible_height):
        source = ((screen.visible_y + y) * screen.width + screen.visible_x) * 4
        target = y * row_bytes
        visible[target : target + row_bytes] = frame.data[source : source + row_bytes]

    rotation = screen.transform & 3
    mirror_x = bool(screen.transform & 4)
    mirror_y = bool(screen.transform & 8)
    width = screen.visible_height if rotation in {1, 3} else screen.visible_width
    height = screen.visible_width if rotation in {1, 3} else screen.visible_height
    output = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            tx, ty = x, y
            if rotation == 1:
                tx, ty = height - 1 - y, x
            elif rotation == 2:
                tx, ty = width - 1 - x, height - 1 - y
            elif rotation == 3:
                tx, ty = y, width - 1 - x
            if mirror_x:
                tx = screen.visible_width - 1 - tx
            if mirror_y:
                ty = screen.visible_height - 1 - ty
            source = (ty * screen.visible_width + tx) * 4
            target = (y * width + x) * 4
            output[target : target + 4] = visible[source : source + 4]
    return RgbaFrame(width, height, bytes(output))


def image_crc32(encoded: EncodedImage) -> int:
    checksum = 0
    for transfer in encoded.transfers:
        checksum = zlib.crc32(transfer.data, checksum)
    return checksum & 0xFFFFFFFF


def image_store_begin_payload(
    encoded: EncodedImage,
    screen: ScreenConfig,
    name: str,
    content_type: str = "image",
) -> bytes:
    if content_type not in CONTENT_TYPE_IDS:
        raise ValueError(f"unsupported image content type: {content_type}")
    safe_name = re.sub(r"[^a-zA-Z0-9_-]", "_", name or "image")[:19].encode("ascii")
    screen_offset = 4
    metadata_offset = screen_offset + SCREEN_WIRE_SIZE
    payload = bytearray(metadata_offset + 17 + len(safe_name))
    payload[0] = 3
    payload[1] = encoded.format
    payload[2] = len(encoded.transfers)
    payload[3] = encoded.black_mask
    payload[screen_offset:metadata_offset] = encode_screen_config(screen)
    struct.pack_into("<I", payload, metadata_offset, encoded.total_bytes)
    struct.pack_into(
        "<I", payload, metadata_offset + 4,
        len(encoded.transfers[0].data) if encoded.transfers else 0,
    )
    struct.pack_into(
        "<I", payload, metadata_offset + 8,
        len(encoded.transfers[1].data) if len(encoded.transfers) > 1 else 0,
    )
    struct.pack_into("<I", payload, metadata_offset + 12, image_crc32(encoded))
    payload[metadata_offset + 16] = CONTENT_TYPE_IDS[content_type]
    payload[metadata_offset + 17:] = safe_name
    return bytes(payload)


def _framed_image(image: RgbaFrame, screen: ScreenConfig) -> RgbaFrame:
    transformed = transform_rgba(image, screen.transform)
    if (transformed.width, transformed.height) != (screen.visible_width, screen.visible_height):
        raise ValueError("image dimensions do not match the screen's visible area")
    output = bytearray((0xFF,)) * (screen.width * screen.height * 4)
    row_bytes = transformed.width * 4
    for y in range(transformed.height):
        source = y * row_bytes
        target = ((screen.visible_y + y) * screen.width + screen.visible_x) * 4
        output[target : target + row_bytes] = transformed.data[source : source + row_bytes]
    return RgbaFrame(screen.width, screen.height, bytes(output))


def _luminance(r: int, g: int, b: int) -> float:
    return 0.299 * r + 0.587 * g + 0.114 * b


def _distance(r: int, g: int, b: int, target: tuple[int, int, int]) -> int:
    return (r - target[0]) ** 2 + (g - target[1]) ** 2 + (b - target[2]) ** 2


def _mono(frame: RgbaFrame) -> bytes:
    byte_width = (frame.width + 7) // 8
    output = bytearray(byte_width * frame.height)
    for y in range(frame.height):
        for x in range(frame.width):
            pixel = (y * frame.width + x) * 4
            if _luminance(*frame.data[pixel : pixel + 3]) >= 140:
                output[y * byte_width + (x >> 3)] |= 1 << (7 - (x & 7))
    return bytes(output)


_ACCENTS = {
    2: (220, 48, 38),
    5: (235, 194, 25),
    6: (44, 97, 165),
    7: (19, 116, 99),
    8: (225, 139, 35),
}


def _dual(frame: RgbaFrame, color: int) -> tuple[bytes, bytes]:
    byte_width = (frame.width + 7) // 8
    black = bytearray(byte_width * frame.height)
    accent = bytearray((0xFF,)) * (byte_width * frame.height)
    target = _ACCENTS.get(color, _ACCENTS[2])
    for y in range(frame.height):
        for x in range(frame.width):
            pixel = (y * frame.width + x) * 4
            r, g, b = frame.data[pixel : pixel + 3]
            index = y * byte_width + (x >> 3)
            bit = 7 - (x & 7)
            accent_distance = _distance(r, g, b, target)
            if accent_distance < _distance(r, g, b, (0, 0, 0)) and accent_distance < _distance(r, g, b, (255, 255, 255)):
                accent[index] &= ~(1 << bit)
            elif _luminance(r, g, b) >= 140:
                black[index] |= 1 << bit
    return bytes(black), bytes(accent)


def _indexed2(frame: RgbaFrame) -> bytes:
    output = bytearray((frame.width * frame.height + 3) // 4)
    palette = (
        (0, (0, 0, 0)),
        (1, (255, 255, 255)),
        (3, (220, 48, 38)),
        (2, (235, 194, 25)),
    )
    for y in range(frame.height):
        for x in range(frame.width):
            pixel = (y * frame.width + x) * 4
            r, g, b = frame.data[pixel : pixel + 3]
            selected = min(palette, key=lambda item: _distance(r, g, b, item[1]))
            output_index = (y * frame.width + x) // 4
            output[output_index] |= selected[0] << (6 - (x & 3) * 2)
    return bytes(output)


def _osprey_e6(frame: RgbaFrame) -> bytes:
    palette = (
        ((0, 0, 0), 0), ((255, 255, 255), 1), ((235, 194, 25), 2),
        ((220, 48, 38), 3), ((0, 0, 255), 5), ((41, 204, 20), 6),
    )
    output = bytearray((frame.width * frame.height + 1) // 2)
    for pixel in range(frame.width * frame.height):
        r, g, b = frame.data[pixel * 4:pixel * 4 + 3]
        _, code = min(palette, key=lambda item: _distance(r, g, b, item[0]))
        output[pixel >> 1] |= code << (0 if pixel & 1 else 4)
    return bytes(output)


def _lenmory_e6(frame: RgbaFrame) -> tuple[bytes, bytes]:
    area0 = bytearray((frame.width * frame.height + 3) // 4)
    area6 = bytearray(len(area0))
    palette = (
        ((0, 0, 0), 1, 0),
        ((255, 255, 255), 1, 1),
        ((220, 48, 38), 3, 1),
        ((235, 194, 25), 1, 2),
        ((41, 204, 20), 2, 1),
        ((0, 0, 255), 0, 1),
    )
    for y in range(frame.height):
        for x in range(frame.width):
            pixel = (y * frame.width + x) * 4
            r, g, b = frame.data[pixel : pixel + 3]
            _, pass0, pass6 = min(
                palette,
                key=lambda item: _distance(r, g, b, item[0]),
            )
            output_index = (y * frame.width + x) // 4
            shift = 6 - (x & 3) * 2
            area0[output_index] |= pass0 << shift
            area6[output_index] |= pass6 << shift
    return bytes(area0), bytes(area6)


def _gray4(frame: RgbaFrame) -> tuple[bytes, bytes]:
    byte_width = (frame.width + 7) // 8
    high = bytearray(byte_width * frame.height)
    low = bytearray(byte_width * frame.height)
    for y in range(frame.height):
        for x in range(frame.width):
            pixel = (y * frame.width + x) * 4
            value = _luminance(*frame.data[pixel : pixel + 3])
            level = 0 if value < 43 else 1 if value < 128 else 2 if value < 213 else 3
            index = y * byte_width + (x >> 3)
            bit = 7 - (x & 7)
            high[index] |= ((level >> 1) & 1) << bit
            low[index] |= (level & 1) << bit
    return bytes((~value) & 0xFF for value in low), bytes((~value) & 0xFF for value in high)


def _indexed4_from_planes(black: bytes, accent: bytes) -> bytes:
    output = bytearray(len(black) * 4)
    target = 0
    for black_value, accent_value in zip(black, accent, strict=True):
        for _ in range(4):
            value = 0x04 if (accent_value & 0x80) == 0 else 0x00 if (black_value & 0x80) == 0 else 0x03
            value <<= 4
            black_value = (black_value << 1) & 0xFF
            accent_value = (accent_value << 1) & 0xFF
            value |= 0x04 if (accent_value & 0x80) == 0 else 0x00 if (black_value & 0x80) == 0 else 0x03
            black_value = (black_value << 1) & 0xFF
            accent_value = (accent_value << 1) & 0xFF
            output[target] = value
            target += 1
    return bytes(output)
