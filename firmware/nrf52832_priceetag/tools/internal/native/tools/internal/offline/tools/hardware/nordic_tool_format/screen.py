"""Frozen native screen encoder/validator, without CLI command or transport code."""
from __future__ import annotations
from dataclasses import dataclass,asdict
from typing import Any
import struct
from .screen_registry import ScreenControllerV2,ScreenColorV2,SCREEN_PROFILE_V2_VERSION,SCREEN_PROFILE_V2_WIRE_SIZE,SCREEN_RULES_V2
SCREEN_SCHEMA_VERSION=SCREEN_PROFILE_V2_VERSION
SCREEN_WIRE_SIZE=SCREEN_PROFILE_V2_WIRE_SIZE
SCREEN_VERSION=1

@dataclass(frozen=True)
class ScreenConfig:
    schema_version: int
    driver: int
    color: int
    transform: int
    width: int
    height: int
    visible_x: int
    visible_y: int
    visible_width: int
    visible_height: int
    recipe: int = 0
    native_ui_scale: int = 0

    def to_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["driverHex"] = f"0x{self.driver:02X}"
        result["recipeHex"] = f"0x{self.recipe:02X}"
        return result

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
    if rule.get("full_viewport") and (
        config.visible_x != 0 or config.visible_y != 0
        or config.visible_width != config.width or config.visible_height != config.height
    ):
        raise ValueError("this panel recipe requires the full viewport")
    if rule["fixed_sizes"] and size not in rule["fixed_sizes"]:
        raise ValueError("screen dimensions do not match the panel recipe")
    color_sizes = rule["color_fixed_sizes"].get(config.color, ())
    if color_sizes and size not in color_sizes:
        raise ValueError("screen dimensions do not match the selected color mode")
