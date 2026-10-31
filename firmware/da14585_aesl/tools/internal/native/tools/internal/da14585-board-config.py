#!/usr/bin/env python3
"""Pure DA family/preset preflight. No debugger, BLE or device writes.

Maintained screen profiles and available DA GPIOs are checked before hardware.
Full identity/calibration/range/readback admission is NOT inferred from a
successful DryRun. The existing common pin16 is used for Full defaults.
"""
import argparse
import hashlib
import json
import re
import runpy
from pathlib import Path
import struct
import tomllib
import zlib

FAMILY = "da14585_aesl"
# Existing model aliases, NOT newly established literal PCB silkscreens.
# Numeric values are the stock selector byte at 0x39000 and stay unchanged.
BOARD_MARKINGS = {
    "asel29": {"board_id": 2, "product": "Hema AESL-2.9", "wire_board": "aesl29_da",
               "ic": "SSD1619", "driver": 32, "width": 128, "height": 296, "rotation": 90},
    "asel42": {"board_id": 3, "product": "Hema AESL-4.2", "wire_board": "aesl42_da",
               "ic": "UC8176", "driver": 19, "width": 400, "height": 300, "rotation": 0},
}
DISPLAY = {"mosi": "P2.0", "clock": "P0.1", "cs": "P2.1", "dc": "P0.7",
           "reset": "P1.0", "busy": "P1.1", "bs": "P2.2", "enable": "P2.3",
           "enable_active_low": False}
SIGNAL_ROLES = ("mosi", "clock", "cs", "dc", "reset", "busy")
_pin_state = runpy.run_path(str(Path(__file__).with_name('da14585-full-state.py')))


def display_pins(gpio):
    """Use actual GPIO ranges and fixed resource conflicts, not a route whitelist."""
    if not isinstance(gpio, dict) or set(gpio) != {"display"}:
        raise ValueError("unsafe/unqualified GPIO routing or optional peripheral")
    display = gpio["display"]
    if not isinstance(display, dict) or set(display) != set(DISPLAY) or \
            display["bs"] != DISPLAY["bs"] or \
            type(display["enable_active_low"]) is not bool or \
            any(not isinstance(display[role], str) for role in (*SIGNAL_ROLES, 'enable')):
        raise ValueError("GPIO fields/enable polarity invalid; screen-signal polarity belongs to the driver; BS rerouting is not supported")
    def parse(pin):
        if pin == 'unused': return 255
        if not re.fullmatch(r'P[0-3]\.[0-9]', pin):
            raise ValueError('GPIO must be a DA port pin such as P0.2')
        return int(pin[1:].replace('.', ''), 16)
    pins = bytes(parse(display[role]) for role in SIGNAL_ROLES)
    enable = 255 if display['enable'] == 'unused' else parse(display['enable'])
    # Exact common pin16; signal polarity stays with the driver/recipe.
    wire = bytes((1, 0, 0x40 if display['enable_active_low'] else 0, 0)) + pins + bytes((enable,)) + b'\xff' * 5
    if not _pin_state['valid_pin_defaults'](wire):
        raise ValueError('nonexistent/occupied/duplicate DA GPIO')
    return wire


def screen_profile(marking, screen):
    board = BOARD_MARKINGS[marking]
    # One existing, separately confirmed panel on the same board and pins.
    # This is not a new board selector or an arbitrary controller override.
    if marking == "asel42" and screen.get("ic") == "SSD1683":
        return {**board, "ic": "SSD1683", "driver": 36, "recipe": 1}
    return {**board, "recipe": 0}


def screen_wire(plan):
    screen = plan["screen"]
    profile = screen_profile(plan["board_marking"], screen)
    return struct.pack('<BBBBBHHHHHH', 1, profile['driver'], screen['recipe'],
        2, 1 if screen['rotation'] == 90 else 0, screen['width'], screen['height'],
        screen['visible_x'], screen['visible_y'], screen['visible_width'], screen['visible_height'])


def screen_record(plan):
    """Existing SP/version1 record, derived from the confirmed configuration."""
    payload = b'SP\x01\x11' + screen_wire(plan) + b'\0' * 3
    return payload + struct.pack('<I', _pin_state['fnv1a'](payload))


def load_config(path, chip):
    if chip != "DA14585":
        raise ValueError("foreign chip; only DA14585 is accepted")
    with Path(path).open("rb") as stream:
        config = tomllib.load(stream)
    if set(config) != {"format_version", "board_marking", "screen", "gpio"} or \
            type(config["format_version"]) is not int or config["format_version"] != 1:
        raise ValueError("unknown/legacy configuration fields or format_version")
    marking = config["board_marking"]
    if not isinstance(marking, str) or marking not in BOARD_MARKINGS:
        raise ValueError("unknown board_marking; no guessed hardware backend")
    if not isinstance(config["screen"], dict):
        raise ValueError("screen must be a maintained profile")
    board = screen_profile(marking, config["screen"])
    screen = {"ic": board["ic"], "recipe": board["recipe"], "color": "BWR",
              "rotation": board["rotation"], "mirror_x": False, "mirror_y": False,
              "native_ui_scale": 0, "width": board["width"], "height": board["height"],
              "visible_x": 0, "visible_y": 0,
              "visible_width": board["width"], "visible_height": board["height"]}
    pin_block = display_pins(config["gpio"])
    if config["screen"] != screen or any(type(config["screen"][key]) is not type(value)
                                        for key, value in screen.items()):
        raise ValueError("only existing shipped screen defaults are admitted by this preflight")
    return {"chip": chip, "firmware_family": FAMILY, "board_marking": marking,
            "board_id": board["board_id"], "product": board["product"],
            "wire_board": board["wire_board"],
            "marking_status": "existing-model-alias-unverified-silkscreen",
            "screen": screen, "gpio": config["gpio"], "pin_wire_hex": pin_block.hex(),
            "gpio_changes": pin_block != bytes((1,0,0,0,0x20,1,0x21,7,0x10,0x11,0x23,255,255,255,255,255)),
            "configured_writer_qualified": False}


def check_base_family(path, plan):
    if plan.get("firmware_family") != FAMILY or plan.get("board_id") not in (2, 3):
        raise ValueError("foreign firmware family/selector")
    data = Path(path).read_bytes()
    if len(data) != 0x40000 or data[:2] != b"pP" or \
            data[0x38000:0x38002] != b"pR":
        raise ValueError("base firmware family/container mismatch; no hardware tool opened")
    app = data[0x4000:0x1C000]
    size, crc = struct.unpack_from("<II", app, 4)
    preflight = struct.unpack_from("<IIIIII", app, 40)
    if app[:3] != b"pQ\xaa" or not 96 < size <= 95744 or \
            zlib.crc32(app[64:64 + size]) != crc or \
            preflight[0] != 0x31504B49 or preflight[1] + 96 != size or \
            preflight[3:] != (0x42584144, 0x31564144, 0x314B4144) or \
            b"da14585_aesl\0" not in app[64:64 + size]:
        raise ValueError("base firmware family preflight mismatch; signature admission remains separate")
    defaults = runpy.run_path(str(Path(__file__).with_name('da14585-full-state.py')))['verify_settings'](data)
    # A common AESL executable consumes the persisted screen/pin records.
    # Valid template defaults are checked as input bytes, not treated as a
    # second source of configuration or a board-specific firmware identity.
    if defaults.get('pinWireHex') is None and plan['gpio_changes']:
        raise ValueError('base firmware has no configured pin default; refusing changed GPIO')
    if defaults['pinDefaultsCapable']:
        _pin_state['require_pin_support'](app, bytes.fromhex(plan['pin_wire_hex']))
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest().upper(),
            "pinDefaultsFromConfig": defaults['pinDefaultsCapable'],
            "screenDefaultsFromConfig": True,
            "note": "family preflight only, not signature/physical identity/calibration/write qualification"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chip", required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--firmware", type=Path)
    args = parser.parse_args()
    try:
        plan = load_config(args.config, args.chip)
        if args.firmware:
            plan["base"] = check_base_family(args.firmware, plan)
        print(json.dumps(plan))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, str(error) + "\n")


if __name__ == "__main__":
    main()
