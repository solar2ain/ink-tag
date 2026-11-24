"""Version-1 three-button envelope; reserved bytes are opaque and never GPIOs."""
from .generated_release_v1 import AUX_VERSION, AUX_BYTES

PULLS = ("none", "up", "down")


def aux_pins(config: dict) -> set[int]:
    return {button["gpio"] for button in config["buttons"]} - {255}


def encode_aux_configuration(config: dict) -> bytes:
    required = {"version", "buttons", "reserved"}
    if not isinstance(config, dict) or set(config) != required or type(config["version"]) is not int or config["version"] != AUX_VERSION:
        raise ValueError("IO requires version 1, three fixed button slots and five reserved bytes")
    buttons, reserved = config["buttons"], config["reserved"]
    if not isinstance(buttons, list) or len(buttons) != 3:
        raise ValueError("Exactly three fixed button slots are required; GPIO 255 disables a slot")
    if not isinstance(reserved, list) or len(reserved) != 5 or any(type(v) is not int or not 0 <= v <= 255 for v in reserved):
        raise ValueError("IO requires five opaque reserved bytes")
    wire = [AUX_VERSION]
    for button in buttons:
        if not isinstance(button, dict) or set(button) != {"gpio", "activeLow", "pull"}:
            raise ValueError("Each button requires gpio, activeLow and pull")
        gpio, low, pull = button["gpio"], button["activeLow"], button["pull"]
        if type(gpio) is not int or not 0 <= gpio <= 255 or type(low) is not bool or pull not in PULLS:
            raise ValueError("Invalid button GPIO, polarity or pull")
        if gpio == 255 and (low or pull != "none"):
            raise ValueError("Disabled button slots require activeLow=false and pull=none")
        wire.extend((gpio, int(low) | PULLS.index(pull) << 1))
    pins = [b["gpio"] for b in buttons if b["gpio"] != 255]
    if len(set(pins)) != len(pins):
        raise ValueError("Button GPIOs cannot overlap")
    return bytes(wire + reserved)


def decode_aux_configuration(payload: bytes) -> dict:
    if len(payload) != AUX_BYTES or payload[0] != AUX_VERSION or any(payload[i] >> 1 >= len(PULLS) for i in (2, 4, 6)):
        raise ValueError("Invalid three-button IO version, length or flags")
    config = {"version": AUX_VERSION,
              "buttons": [{"gpio": payload[i], "activeLow": bool(payload[i + 1] & 1), "pull": PULLS[payload[i + 1] >> 1]} for i in (1, 3, 5)],
              "reserved": list(payload[7:])}
    if encode_aux_configuration(config) != payload:
        raise ValueError("Noncanonical three-button IO response")
    return config
