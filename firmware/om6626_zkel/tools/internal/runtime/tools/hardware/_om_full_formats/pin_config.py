"""Capability-negotiated complete routing, not arbitrary GPIO access."""
import struct
from .generated_busy_defaults import SCREEN_BUSY_DEFAULTS
from .generated_commands_v2 import PinReportFlagV2
from .generated_release_v1 import EDITABLE_ACTIVE_LOW_MASK, PIN_REPORT_VERSION

PIN_ROLES = ("mosi", "clock", "cs", "dc", "reset", "busy", "enable",
             "red", "green", "blue", "txd", "rxd")
ACTIVE_LOW_MASK = EDITABLE_ACTIVE_LOW_MASK
PUBLIC_PIN_ROLES = PIN_ROLES[:10]


def user_pin_configuration(config: dict) -> dict:
    """User JSON exposes display/LED only; the two existing UART bytes stay reserved."""
    if not isinstance(config, dict) or set(config) - {'pins', 'activeLow'} or not isinstance(config.get('pins'), dict):
        raise ValueError('Routing requires pins and optional activeLow')
    pins = config['pins']
    if set(pins) != set(PUBLIC_PIN_ROLES):
        raise ValueError('Provide the ten display/LED pins; TXD and RXD are not configurable')
    result = {'pins': {**pins, 'txd': 255, 'rxd': 255}, 'activeLow': config.get('activeLow', 0)}
    if type(result['activeLow']) is not int or result['activeLow'] & 0xC00:
        raise ValueError('TXD and RXD polarity bits are reserved')
    encode_pin_block(result)
    return result


def gpio_label(pin: int, family: str) -> str:
    if pin == 255:
        return 'Disconnected'
    if family.startswith('tlsr8359_') and 0 <= pin < 32:
        return f'P{"ABCD"[pin >> 3]}{pin & 7} ({pin})'
    return f'GPIO {pin}'


def screen_busy_active_high(screen) -> bool | None:
    if screen is None:
        return None
    from .protocol import validate_screen_config
    try:
        validate_screen_config(screen)
    except ValueError:
        return None
    return SCREEN_BUSY_DEFAULTS.get(screen.driver * 256 + screen.recipe)


def describe_pin_report(report: dict, family: str, screen=None) -> dict:
    effective = report['effective']
    suggested = screen_busy_active_high(screen)
    actual = None if effective['pins']['busy'] == 255 else suggested
    return {'source': 'user override' if report['overridePresent'] else 'device default',
            'pins': {role: gpio_label(effective['pins'][role], family) for role in PUBLIC_PIN_ROLES},
            'busyActiveHigh': actual, 'controllerBusyActiveHigh': suggested,
            'busyMismatch': actual is not None and suggested is not None and actual != suggested,
            'recommendationScope': 'selected controller/recipe; not a measured pin voltage',
            'busyPolaritySource': 'screen-recipe'}


def encode_pin_block(config: dict) -> bytes:
    if not isinstance(config, dict) or set(config) - {"pins", "activeLow"}:
        raise ValueError("Pin configuration requires pins and optional activeLow")
    pins = config.get("pins", {})
    if not isinstance(pins, dict) or set(pins) != set(PIN_ROLES):
        raise ValueError("A complete routing with all twelve named pins is required")
    values = [pins[name] for name in PIN_ROLES]
    if any(type(pin) is not int or not 0 <= pin <= 255 for pin in values):
        raise ValueError("Pin numbers must be integers 0..255; 255 is disconnected")
    if values[10:] != [255, 255]:
        raise ValueError('TXD and RXD positions are reserved and must be 255')
    low = config.get("activeLow", 0)
    if type(low) is not int or not 0 <= low <= ACTIVE_LOW_MASK or low & ~ACTIVE_LOW_MASK:
        raise ValueError("Invalid active-low role mask")
    active = [pin for pin in values if pin != 255]
    if len(set(active)) != len(active):
        raise ValueError("Active pin roles cannot overlap")
    if any(pin == 255 and low & (1 << i) for i, pin in enumerate(values)):
        raise ValueError("Disconnected pins cannot have polarity flags")
    display = sum(pin != 255 for pin in values[:6])
    if display != 6 and (display != 0 or values[6] != 255):
        raise ValueError("Provide all six display signals, or disable the complete display and rail")
    return bytes((1, 0)) + struct.pack("<H", low) + bytes(values)


def decode_pin_block(payload: bytes) -> dict:
    if len(payload) != 16 or payload[:2] != b"\x01\0":
        raise ValueError("Invalid pin block version, reserved byte or length")
    config = {"activeLow": struct.unpack_from("<H", payload, 2)[0],
              "pins": dict(zip(PIN_ROLES, payload[4:]))}
    encode_pin_block(config)
    return config


def decode_pin_report(payload: bytes) -> dict:
    known_flags = sum(flag.value for flag in PinReportFlagV2)
    if len(payload) != 36 or payload[0] != PIN_REPORT_VERSION or payload[1] & ~known_flags:
        raise ValueError("Invalid pin configuration response")
    override, valid = bool(payload[1] & 1), bool(payload[1] & 2)
    editable = struct.unpack_from("<H", payload, 2)[0]
    if editable & ~0x3FF:
        raise ValueError("Invalid editable pin role mask")
    default = decode_pin_block(payload[4:20]) if valid else None
    if not valid and payload[4:20] != bytes(16):
        raise ValueError("Invalid absent-default encoding")
    effective = decode_pin_block(payload[20:])
    if not override and ((valid and effective != default) or
                         (not valid and any(pin != 255 for pin in effective["pins"].values()))):
        raise ValueError("Effective pin source does not match the response flags")
    return {"overridePresent": override, "defaultValid": valid,
            "recipeBusyDerived": bool(payload[1] & PinReportFlagV2.RECIPE_BUSY_DERIVED), "editableRoles": editable,
            "default": default, "effective": effective}


def validate_pin_update(config: dict, current: dict) -> None:
    encode_pin_block(config)
    for index, role in enumerate(PIN_ROLES):
        if not current["editableRoles"] & (1 << index) and (
                config["pins"][role] != current["effective"]["pins"][role] or
                (config.get("activeLow", 0) ^ current["effective"]["activeLow"]) & (1 << index)):
            raise ValueError(f"Firmware does not permit changing pin role {role}")
