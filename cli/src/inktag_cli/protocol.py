from __future__ import annotations

import re
import struct
from dataclasses import dataclass
from datetime import datetime
from typing import Callable

from .generated_commands_v2 import (
    SCREEN_PROFILE_V2_VERSION,
    SCREEN_PROFILE_V2_WIRE_SIZE,
    SCREEN_RULES_V2,
    TestOperationV2,
)
from .models import (
    ScreenConfig,
)


INKTAG_SERVICE_UUID = "62750001-d828-918d-fb46-b6c11c675aec"
COMMAND_UUID = "62750002-d828-918d-fb46-b6c11c675aec"
VERSION_UUID = "62750003-d828-918d-fb46-b6c11c675aec"

OTA_SERVICE_UUIDS = {
    "nordic": "0000fe59-0000-1000-8000-00805f9b34fb",
    "goodix": "a6ed0401-d344-460a-8075-b9e8ec90d71b",
    "onmicro": "000001bf-5f00-0000-0000-4f4d2d4f5441",
    "dialog": "0000fef5-0000-1000-8000-00805f9b34fb",
}

KNOWN_NAME_PREFIXES = (
    "IKT_",
    "InkTag_",
)

PRIMARY_NAME_PREFIX = "IKT_"
LEGACY_NAME_PREFIX = "InkTag_"
RESOURCE_ALIAS_MAX_BYTES = 19
_RESERVED_RESOURCE_ALIASES = {
    "CALENDAR", "CLOCK", "DEVICE", "SLIDESHOW", "NEXT", "PREVIOUS", "CLEAR",
    "日历", "时间", "设备", "轮播", "下一页", "上一页", "清屏",
}


SCREEN_WIRE_SIZE = SCREEN_PROFILE_V2_WIRE_SIZE
SCREEN_SCHEMA_VERSION = SCREEN_PROFILE_V2_VERSION
_INPUT_ACTION_IDS = {
    "disabled": 0,
    "nextPage": 1,
    "showSlot": 2,
    "showDeviceInfo": 3,
    "showCalendar": 4,
    "showClock": 5,
    "toggleSlideshow": 6,
    "stop": 7,
    "controlUrl": 8,
    "toggleClearPrevious": 9,
}
_INPUT_SOURCE_IDS = {"nfc": 1, "single": 2, "double": 3, "long": 4}
_SCREEN_PROFILE_NAMES = {
    (0x10, 1, 2, 5, 128, 250, 0, 0, 122, 250): "HANSHOW_NOWA_213_BWR_EXPERIMENTAL",
    (0x3F, 1, 2, 5, 128, 250, 0, 0, 122, 250): "HANSHOW_NEBULAR_213_BWR_EXPERIMENTAL",
    (0x3F, 1, 3, 5, 128, 250, 0, 0, 122, 250): "HANSHOW_NEBULAR_213_BWRY_EXPERIMENTAL",
    (0x20, 1, 2, 3, 128, 250, 0, 0, 122, 250): "SSD1619_128x250_BWR_HEMA213",
    (0x21, 0, 1, 0, 152, 152, 0, 0, 152, 152): "SSD1675_152x152_BW_STANDARD",
    (0x21, 1, 2, 5, 152, 152, 0, 0, 152, 152): "HANSHOW_NOWA_154_BWR",
    (0x11, 1, 2, 1, 104, 212, 0, 0, 104, 212): "SES_213_BWR",
    (0x11, 2, 2, 1, 152, 296, 0, 0, 152, 296): "SES_266_BWR",
    (0x23, 1, 2, 9, 128, 250, 0, 0, 122, 250): "ZKC21V_213_BWR",
    (0x24, 1, 2, 0, 400, 300, 0, 0, 400, 300): "ZKC42VM_420_BWR",
    (0x13, 1, 2, 0, 400, 300, 0, 0, 400, 300): "ZKC42V_420_BWR_LEGACY_UC8176",
    (0x15, 1, 2, 0, 400, 300, 0, 0, 400, 300): "ZKC42V_420_BWR",
    (0x30, 1, 3, 9, 128, 250, 0, 0, 122, 250): "ZKC21B_213_BWRY",
    (0x33, 2, 3, 6, 400, 300, 0, 0, 400, 300): "ZKC42B_420_BWRY",
    (0x33, 1, 3, 8, 400, 300, 0, 0, 400, 300): "HANSHOW_NEBULAR_420_BWRY",
    (0x13, 0, 2, 0, 400, 300, 0, 0, 400, 300): "UC8176_400x300_BWR_STANDARD",
    (0x15, 0, 2, 0, 400, 300, 0, 0, 400, 300): "UC8276_400x300_BWR_STANDARD",
    (0x20, 0, 2, 1, 128, 296, 0, 0, 128, 296): "SSD1619_128x296_BWR_STANDARD",
    (0x22, 0, 4, 0, 800, 480, 0, 0, 800, 480): "SSD1677_800x480_GRAY4_STANDARD",
    (0x31, 1, 3, 2, 768, 552, 0, 0, 768, 552): "HUAWEI_398_BWRY_A0",
    (0x31, 2, 3, 2, 768, 552, 0, 0, 768, 552): "HUAWEI_398_BWRY_A1",
    (0x32, 1, 10, 0, 760, 568, 0, 0, 760, 568): "LENMORY_364_E6",
}
_SCREEN_PROFILE_NATIVE_UI_SCALES = {
    "HUAWEI_398_BWRY_A0": 2,
    "HUAWEI_398_BWRY_A1": 2,
    "LENMORY_364_E6": 2,
}
_SCREEN_PROFILES = {
    name.upper(): ScreenConfig(
        SCREEN_SCHEMA_VERSION,
        key[0],
        *key[2:],
        recipe=key[1],
        native_ui_scale=_SCREEN_PROFILE_NATIVE_UI_SCALES.get(name, 0),
    )
    for key, name in _SCREEN_PROFILE_NAMES.items()
}
def encode_screen_config(config: ScreenConfig) -> bytes:
    validate_screen_config(config)
    return struct.pack(
        "<BBBBBHHHHHH",
        config.schema_version,
        config.driver,
        config.recipe,
        config.color,
        config.transform | (config.native_ui_scale << 4),
        config.width,
        config.height,
        config.visible_x,
        config.visible_y,
        config.visible_width,
        config.visible_height,
    )


def decode_screen_config(data: bytes) -> ScreenConfig | None:
    if len(data) != SCREEN_WIRE_SIZE or data[0] != SCREEN_SCHEMA_VERSION:
        return None
    values = struct.unpack("<BBBBBHHHHHH", data)
    return ScreenConfig(
        values[0], values[1], values[3], values[4] & 0x0F, *values[5:],
        recipe=values[2], native_ui_scale=(values[4] >> 4) & 0x0F,
    )


def screen_profile_name(config: ScreenConfig) -> str:
    return next(
        (name for name in _SCREEN_PROFILE_NAMES.values() if config == _SCREEN_PROFILES[name.upper()]),
        "",
    )


def screen_profile_names() -> tuple[str, ...]:
    return tuple(sorted(_SCREEN_PROFILE_NAMES.values()))


def screen_config_from_profile(name: str) -> ScreenConfig:
    normalized = name.strip().upper()
    try:
        return _SCREEN_PROFILES[normalized]
    except KeyError as exc:
        available = ", ".join(screen_profile_names())
        raise ValueError(f"unknown screen profile '{name}'; available profiles: {available}") from exc


def validate_screen_config(config: ScreenConfig) -> None:
    if config.schema_version != SCREEN_SCHEMA_VERSION:
        raise ValueError("unsupported screen schema")
    rule = SCREEN_RULES_V2.get((config.driver, config.recipe))
    if not rule or config.color not in rule["colors"]:
        raise ValueError("invalid screen driver or color")
    if not 0 <= config.transform <= 0x0F:
        raise ValueError("invalid screen transform")
    if not 0 <= config.native_ui_scale <= 5:
        raise ValueError("invalid native UI scale")
    max_width, max_height = rule["max"]
    if not 1 <= config.width <= max_width or not 1 <= config.height <= max_height:
        raise ValueError("invalid screen dimensions")
    if config.width % int(rule["align"]) != 0:
        raise ValueError("screen width does not meet the controller alignment")
    if config.visible_width < 1 or config.visible_height < 1:
        raise ValueError("visible viewport is empty")
    if config.visible_x + config.visible_width > config.width:
        raise ValueError("visible viewport exceeds screen width")
    if config.visible_y + config.visible_height > config.height:
        raise ValueError("visible viewport exceeds screen height")
    size = (config.width, config.height)
    if rule["fixed_sizes"] and size not in rule["fixed_sizes"]:
        raise ValueError("screen dimensions do not match the panel recipe")
    color_sizes = rule["color_fixed_sizes"].get(config.color, ())
    if color_sizes and size not in color_sizes:
        raise ValueError("screen dimensions do not match the selected color mode")


def encode_v2_time_shift_test(delta_seconds: int, display_mode: int) -> bytes:
    if display_mode not in (1, 2):
        raise ValueError("test display mode must be 1 (calendar) or 2 (clock)")
    if delta_seconds == 0 or not -(366 * 86400) <= delta_seconds <= 366 * 86400:
        raise ValueError("test time shift must be non-zero and within 366 days")
    return struct.pack("<BBi", int(TestOperationV2.TIME_SHIFT), display_mode, delta_seconds)


def encode_v2_time_set_test(when: datetime, display_mode: int, week_start: int) -> bytes:
    if display_mode not in (1, 2):
        raise ValueError("test display mode must be 1 (calendar) or 2 (clock)")
    if not 0 <= week_start <= 6:
        raise ValueError("week start must be in 0..6")
    if when.tzinfo is None or when.utcoffset() is None:
        raise ValueError("test time must include a timezone")
    timezone_minutes = int(when.utcoffset().total_seconds() // 60)
    if not -840 <= timezone_minutes <= 840:
        raise ValueError("timezone offset must be within -14:00..+14:00")
    epoch = int(when.timestamp())
    if not 946684800 <= epoch <= 4102444799:
        raise ValueError("test time must be between 2000 and 2099")
    return struct.pack(
        "<BBIhB",
        int(TestOperationV2.TIME_SET),
        display_mode,
        epoch,
        timezone_minutes,
        week_start,
    )


def encode_bluetooth_name(name: str, max_bytes: int = 19) -> bytes:
    value = name.strip()
    prefix = next((candidate for candidate in KNOWN_NAME_PREFIXES
                   if value.startswith(candidate)), "")
    if not prefix:
        raise ValueError("Bluetooth name must start with IKT_ (legacy InkTag_ is also accepted)")
    if len(value) == len(prefix):
        raise ValueError(f"Bluetooth name needs a suffix after {prefix}")
    suffix = value[len(prefix):]
    if not all(
        character.isascii() and (character.isalnum() or character in "_-")
        or ord(character) == 0x3007
        or 0x3400 <= ord(character) <= 0x4DBF
        or 0x4E00 <= ord(character) <= 0x9FFF
        for character in suffix
    ):
        raise ValueError(
            "Bluetooth name suffix may contain only ASCII letters, digits, underscore, "
            "hyphen and common Chinese ideographs"
        )
    encoded = value.encode("utf-8")
    if len(encoded) > max_bytes:
        raise ValueError(f"Bluetooth name must fit in {max_bytes} UTF-8 bytes")
    return encoded


def normalize_resource_alias(value: str) -> str:
    return value.strip()


def resource_alias_key(value: str) -> str:
    normalized = normalize_resource_alias(value)
    return "".join(character.upper() if character.isascii() else character
                   for character in normalized)


def encode_resource_alias(value: str, max_bytes: int = RESOURCE_ALIAS_MAX_BYTES) -> bytes:
    normalized = normalize_resource_alias(value)
    if not normalized:
        raise ValueError("Image-slot alias cannot be empty")
    if any(ord(character) < 0x20 or ord(character) == 0x7F
           for character in normalized):
        raise ValueError("Image-slot alias cannot contain control characters")
    key = resource_alias_key(normalized)
    if key in _RESERVED_RESOURCE_ALIASES or key.startswith("IKNC") or re.fullmatch(r"SLOT-[1-9][0-9]*", key) \
            or re.fullmatch(r"图槽-[1-9][0-9]*", normalized):
        raise ValueError("Image-slot alias conflicts with a built-in NFC command")
    encoded = normalized.encode("utf-8")
    if len(encoded) > max_bytes:
        raise ValueError(f"Image-slot alias must fit in {max_bytes} UTF-8 bytes")
    return encoded


def encode_device_label(label: str, max_bytes: int = 24) -> bytes:
    value = label.strip()
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        raise ValueError("Device label cannot contain control characters")
    encoded = value.encode("utf-8")
    if len(encoded) > max_bytes:
        raise ValueError(f"Device label must fit in {max_bytes} UTF-8 bytes")
    return encoded


def input_action_mask(action: str) -> int:
    try:
        return 1 << _INPUT_ACTION_IDS[action]
    except KeyError as exc:
        raise ValueError(f"unsupported input action: {action}") from exc


def encode_input_action(
    source: str,
    action: str,
    *,
    slot_id: int = 0,
) -> bytes:
    if source not in _INPUT_SOURCE_IDS:
        raise ValueError("input source must be nfc, single, double or long")
    if action not in _INPUT_ACTION_IDS:
        raise ValueError(f"unsupported input action: {action}")
    if action == "controlUrl":
        raise ValueError("protocol V2 input configuration does not carry a control URL")
    if source == "nfc" and action == "toggleClearPrevious":
        raise ValueError("NFC does not support the clear/previous toggle")
    if not 0 <= slot_id <= 255:
        raise ValueError("slot id must be in 0..255")
    return bytes((1, _INPUT_SOURCE_IDS[source], _INPUT_ACTION_IDS[action], slot_id))


@dataclass(frozen=True)
class Notification:
    sequence: int
    received_at: float
    raw: bytes
    text: str | None = None
    screen: ScreenConfig | None = None


NotificationPredicate = Callable[[Notification], bool]
