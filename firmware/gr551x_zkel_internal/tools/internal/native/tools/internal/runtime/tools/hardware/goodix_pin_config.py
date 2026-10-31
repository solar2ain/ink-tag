"""Offline GR wiring proposal; no BLE, NVDS serializer or programmer access.

These codecs do not compose a Full image or enable PIN_CONFIG. The frozen
shared pin/screen wire is reused; GR envelopes, bank and exact-PCB admission
remain vendor-owned. Factory checksums detect corruption, not authority.
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

from board_config import ConfigurationError, load_board_config

ROOT = Path(__file__).resolve().parents[2]
from goodix_offline.pin_config import PIN_ROLES, decode_pin_block, encode_pin_block
from goodix_offline.protocol import decode_screen_config, encode_screen_config, SCREEN_SCHEMA_VERSION
from goodix_offline.models import ScreenConfig
from goodix_offline.generated_commands_v2 import ScreenControllerV2, ScreenColorV2
from goodix_offline.generated_busy_defaults import SCREEN_BUSY_DEFAULTS
from goodix_offline.generated_release_v1 import VERSION, AUX_BYTES, AUX_VERSION, EDITABLE_ACTIVE_LOW_MASK

SELECTOR_OFFSET = 0xF00
FACTORY_OFFSET = 0xF20
EDITABLE_ROLES = 0x03FF
DEFAULT_AUX = bytes((AUX_VERSION,255,0,255,0,255,0,0,255,255,0xD0,7))
FACTS = json.loads((ROOT / "firmware/boards/goodix-pin-backends.json").read_text(encoding="utf-8"))["backends"]


def _require(ok: bool, message: str) -> None:
    if not ok:
        raise ConfigurationError(message)


def _exact_mapping(value, keys, label):
    _require(type(value) is dict and set(value) == set(keys), f"{label} requires exactly {', '.join(keys)}")


def _facts(hardware: str) -> dict:
    _require(type(hardware) is str and hardware in FACTS, "Unreviewed exact GR hardware backend")
    return FACTS[hardware]


def resolve_backend(board_marking: str) -> str:
    _require(type(board_marking) is str and bool(board_marking), "Exact board_marking required")
    matches = [name for name, facts in FACTS.items() if facts["board_marking"] == board_marking]
    _require(bool(matches), "Unregistered board_marking")
    _require(len(matches) == 1, "Ambiguous board_marking")
    return matches[0]


def preset_filename(backend: str) -> str:
    facts = _facts(backend)
    return facts["board_marking"] + ".toml"


def fnv(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def validate_aux(hardware: str, aux: bytes) -> None:
    facts = _facts(hardware)
    _require(len(aux) == AUX_BYTES and aux[0] == AUX_VERSION, "Invalid GR auxiliary block length/version")
    button, buzzer, _ = facts["aux"]
    _require((aux[1] == 255 and aux[2] == 0) or
             (aux[1] == button and aux[1] != 255 and not aux[2] & ~7 and aux[2] >> 1 <= 2),
             "Unqualified button route/mode")
    _require(aux[3:7] == bytes((255,0,255,0)), "Unqualified additional button route")
    if aux[7] == 0:
        _require(aux[8] == aux[9] == 255, "Disabled buzzer must have no GPIO")
    else:
        _require(aux[7] == 3 and buzzer != 255 and aux[8] == buzzer and aux[9] == 255
                 and 500 <= int.from_bytes(aux[10:12], "little") <= 8000, "Unqualified buzzer route/frequency")


def validate_pins(hardware: str, wire: bytes, aux: bytes = DEFAULT_AUX) -> dict:
    facts = _facts(hardware)
    config = decode_pin_block(wire)
    validate_aux(hardware, aux)
    occupied = set(facts["aux"]) - {255}
    occupied.update(set((aux[1], aux[3], aux[5], aux[8], aux[9])) - {255})
    for role, pin in zip(PIN_ROLES, wire[4:]):
        if pin == 255:
            continue
        group, index = pin >> 6, pin & 63
        _require(role not in ("txd", "rxd") and group < 3 and index < 32
                 and index in facts["usable"][group] and pin not in occupied
                 and ((role == "enable") == (pin == facts["rail"])),
                 f"Unqualified/protected GR pin {role}={pin}")
    _require(not config["activeLow"] & ~EDITABLE_ACTIVE_LOW_MASK,
             "Only EN/LED polarity may appear in GR pin wire")
    return config


def _screen_wire(screen: dict) -> bytes:
    geometry = ("width", "height", "visible_x", "visible_y", "visible_width", "visible_height")
    _exact_mapping(screen, ("ic", "recipe", "color", "rotation", "mirror_x", "mirror_y", "native_ui_scale", *geometry), "screen")
    _require(type(screen["ic"]) is str and type(screen["color"]) is str, "Screen IC/colour must be registered names")
    try:
        driver, color = int(ScreenControllerV2[screen["ic"]]), int(ScreenColorV2[screen["color"]])
    except KeyError as exc:
        raise ConfigurationError("Unregistered screen IC/colour") from exc
    _require(type(screen["recipe"]) is int and 0 <= screen["recipe"] <= 255, "Invalid screen recipe")
    _require(type(screen["rotation"]) is int and screen["rotation"] in (0, 90, 180, 270), "Invalid screen rotation")
    _require(type(screen["mirror_x"]) is bool and type(screen["mirror_y"]) is bool, "Screen mirrors must be boolean")
    _require(type(screen["native_ui_scale"]) is int and 0 <= screen["native_ui_scale"] <= 5, "Invalid UI scale")
    _require(all(type(screen[key]) is int and 0 <= screen[key] <= 65535 for key in geometry), "Invalid screen geometry")
    transform = screen["rotation"] // 90 | (4 if screen["mirror_x"] else 0) | (8 if screen["mirror_y"] else 0)
    return encode_screen_config(ScreenConfig(schema_version=SCREEN_SCHEMA_VERSION,
        driver=driver, recipe=screen["recipe"], color=color, transform=transform,
        native_ui_scale=screen["native_ui_scale"], **{key: screen[key] for key in geometry}))


def _pin_name(value: int) -> str:
    return "NONE" if value == 255 else ("GPIO", "AON", "MSIO")[value >> 6] + str(value & 63)


def _pin_wire(gpio: dict, screen: bytes) -> bytes:
    # Release v1: routing owns GPIOs/EN only; IC+recipe owns BUSY/CS/reset.
    _exact_mapping(gpio, ("display", "led"), "gpio")
    pins = {role: 255 for role in PIN_ROLES}
    polarity = 0
    for group, roles in (("display", PIN_ROLES[:7]), ("led", PIN_ROLES[7:10])):
        block = gpio[group]
        if group == 'display':
            _exact_mapping(block, (*roles, "enable_active_low"), "gpio.display")
            _require(type(block['enable_active_low']) is bool, 'enable_active_low must be boolean')
            high = SCREEN_BUSY_DEFAULTS.get(screen[1] * 256 + screen[2])
            _require(type(high) is bool, 'Unknown exact screen BUSY requirements')
            low = ['enable'] if block['enable_active_low'] else []
        else:
            _exact_mapping(block, (*roles, "active_low"), "gpio.led")
            low = block["active_low"]
            _require(type(low) is list and all(type(role) is str and role in roles for role in low)
                     and len(set(low)) == len(low), "Invalid active_low role list")
        for role in roles:
            name = block[role]
            _require(type(name) is str, "GR pins must use GPIO/AON/MSIO names or NONE")
            if name == "NONE":
                _require(group == 'display' and role != 'enable' or role not in low,
                         "Disconnected pin cannot have configured polarity")
                continue
            match = re.fullmatch(r"(GPIO|AON|MSIO)(0|[1-9][0-9]?)", name)
            _require(match is not None and int(match[2]) < 32, "Invalid GR bank/pin name")
            pins[role] = ("GPIO", "AON", "MSIO").index(match[1]) * 64 + int(match[2])
            if role in low:
                polarity |= 1 << PIN_ROLES.index(role)
    return encode_pin_block({"pins": pins, "activeLow": polarity})


def default_config(hardware: str) -> dict:
    facts = _facts(hardware)
    # Independent copies: editing a caller's preset must not change admission.
    gpio = {}
    for group, roles in (("display", PIN_ROLES[:7]), ("led", PIN_ROLES[7:10])):
        gpio[group] = {role: _pin_name(facts["default_pins"][PIN_ROLES.index(role)]) for role in roles}
        if group == 'display':
            gpio[group]['enable_active_low'] = bool(facts['default_active_low'] & (1 << 6))
        else:
            gpio[group]["active_low"] = [role for role in roles if facts["default_active_low"] & (1 << PIN_ROLES.index(role))]
    data = {"format_version": VERSION, "board_marking": facts["board_marking"],
            "screen": dict(facts["screen"]), "gpio": gpio}
    return data


def validate_board_config(data: dict) -> dict:
    _exact_mapping(data, ("format_version", "board_marking", "screen", "gpio"), "GR configuration")
    _require(type(data["format_version"]) is int and data["format_version"] == VERSION,
             "Only the release-v1 configuration is accepted")
    hardware = resolve_backend(data["board_marking"])
    facts = _facts(hardware)
    screen = _screen_wire(data["screen"])
    pins = _pin_wire(data["gpio"], screen)
    validate_pins(hardware, pins)
    return {"backend": hardware, "board_id": facts["board_id"],
            "board_marking": facts["board_marking"], "assembly": facts["assembly"],
            "product_name": facts["product_name"], "pin_wire_hex": pins.hex(), "screen_wire_hex": screen.hex(),
            "portableExecutionQualified": False, "runtimePinConfigQualified": False}


def read_board_config(path: str | Path) -> dict:
    return validate_board_config(load_board_config(path))


def factory_records(hardware: str, pins: bytes, screen: bytes) -> tuple[bytes, bytes]:
    result = _factory_records(hardware, pins, screen)
    high = SCREEN_BUSY_DEFAULTS.get(screen[1] * 256 + screen[2])
    _require(type(high) is bool, 'New Full requires exact recipe BUSY semantics')
    return result


def _factory_records(hardware: str, pins: bytes, screen: bytes) -> tuple[bytes, bytes]:
    facts = _facts(hardware)
    validate_pins(hardware, pins)
    decoded = decode_screen_config(screen)
    _require(screen[0] == VERSION and decoded is not None and encode_screen_config(decoded) == screen,
             "Invalid shared screen profile")  # Not exact-panel qualification.
    selector = b"IBD1" + bytes((VERSION, facts["family"], facts["board_id"], 1))
    selector += struct.pack("<I", fnv(selector))
    body = b"GPF1" + bytes((VERSION, facts["family"], facts["board_id"], facts["mcu"])) + pins + screen + bytes(15)
    _require(len(body) == 56, "Invalid factory screen/pin block length")
    return selector, body + struct.pack("<I", fnv(body)) + b"GPF!"


def decode_factory_records(hardware: str, selector: bytes, record: bytes) -> tuple[bytes, bytes]:
    _require(len(selector) == 12 and len(record) == 64, "Invalid GR factory envelope length")
    pins, screen = record[8:24], record[24:41]
    expected = _factory_records(hardware, pins, screen)
    _require((selector, record) == expected, "Corrupt/mismatched/absent GR factory envelope")
    return pins, screen


def user_record(hardware: str, defaults: bytes, aux: bytes = DEFAULT_AUX,
                override: bytes | None = None) -> bytes:
    validate_pins(hardware, defaults, aux)
    if override is not None:
        validate_pins(hardware, override, aux)
    body = b"GUF1" + bytes((VERSION, int(override is not None))) + aux + (override if override is not None else bytes(16)) + bytes(6)
    _require(len(body) == 40, "Invalid GR settings payload length")
    return body + struct.pack("<I", fnv(body)) + b"GUF!"


def decode_user_record(hardware: str, defaults: bytes, data: bytes) -> dict:
    override = None
    _require(len(data) == 48 and data[:5] == b"GUF1\x01" and data[5] <= 1,
             "Invalid GR release-v1 settings envelope")
    aux = data[6:18]
    override = data[18:34] if data[5] else None
    _require(data == user_record(hardware, defaults, aux, override), "Corrupt GR settings payload/reserved bytes")
    validate_pins(hardware, defaults, aux)
    effective = defaults if override is None else override
    validate_pins(hardware, effective, aux)
    return {"aux": aux, "override": override, "effective": effective}
