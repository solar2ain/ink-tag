"""Strict shared three-button envelope; no GPIO qualification or BLE access."""
import struct
from .generated_release_v1 import AUX_VERSION, AUX_BYTES

PULLS = ("none", "up", "down")
MODES = ("off", "activeHigh", "activeLow", "passiveSingle", "passiveDifferential")


def aux_pins(config: dict) -> set[int]:
    return {*(button["gpio"] for button in config["buttons"]),
            config["buzzerGpioA"], config["buzzerGpioB"]} - {255}


def encode_aux_configuration(config: dict) -> bytes:
    required = {"version", "buttons", "buzzerMode", "buzzerGpioA", "buzzerGpioB", "buzzerFrequencyHz"}
    if not isinstance(config, dict) or set(config) != required or type(config["version"]) is not int or config["version"] != AUX_VERSION:
        raise ValueError("IO requires version 1, three fixed button slots and complete buzzer settings")
    buttons = config["buttons"]
    if not isinstance(buttons, list) or len(buttons) != 3:
        raise ValueError("Exactly three fixed button slots are required; GPIO 255 disables a slot")
    if not isinstance(config['buzzerMode'], str) or config['buzzerMode'] not in MODES:
        raise ValueError('Invalid buzzer mode')
    flags = []
    for button in buttons:
        if not isinstance(button, dict) or set(button) != {"gpio", "activeLow", "pull"}:
            raise ValueError("Each button requires gpio, activeLow and pull")
        gpio, low, pull = button["gpio"], button["activeLow"], button["pull"]
        if type(gpio) is not int or not 0 <= gpio <= 255 or type(low) is not bool or pull not in PULLS:
            raise ValueError("Invalid button GPIO, polarity or pull")
        if gpio == 255 and (low or pull != "none"):
            raise ValueError("Disabled button slots require activeLow=false and pull=none")
        flags.append(int(low) | PULLS.index(pull) << 1)
    a, b, frequency = config['buzzerGpioA'], config['buzzerGpioB'], config['buzzerFrequencyHz']
    mode = MODES.index(config['buzzerMode'])
    if any(type(pin) is not int or not 0 <= pin <= 255 for pin in (a, b)):
        raise ValueError('Buzzer pins must be integers 0..255')
    if mode and (a == 255 or (mode == 4) != (b != 255)):
        raise ValueError('Buzzer GPIOs do not match its mode')
    if type(frequency) is not int or not 0 <= frequency <= 65535 or (mode >= 3 and not 500 <= frequency <= 8000):
        raise ValueError('Passive buzzer frequency must be 500..8000 Hz')
    if config["buzzerMode"] == "off" and (config["buzzerGpioA"] != 255 or config["buzzerGpioB"] != 255):
        raise ValueError("Disabled buzzer requires disconnected GPIO A and B")
    all_pins = [*(button["gpio"] for button in buttons), config["buzzerGpioA"], config["buzzerGpioB"]]
    active = [pin for pin in all_pins if pin != 255]
    if len(set(active)) != len(active):
        raise ValueError("Button and buzzer GPIOs cannot overlap")
    return bytes((AUX_VERSION, buttons[0]["gpio"], flags[0], buttons[1]["gpio"], flags[1], buttons[2]["gpio"], flags[2], mode, a, b)) + struct.pack('<H', frequency)


def decode_aux_configuration(payload: bytes) -> dict:
    if len(payload) != AUX_BYTES or payload[0] != AUX_VERSION or payload[7] >= len(MODES) or any(payload[i] >> 1 >= len(PULLS) for i in (2, 4, 6)):
        raise ValueError("Invalid three-button IO version, length or flags")
    config = {"version": AUX_VERSION,
              "buttons": [{"gpio": payload[i], "activeLow": bool(payload[i + 1] & 1), "pull": PULLS[payload[i + 1] >> 1]} for i in (1, 3, 5)],
              "buzzerMode": MODES[payload[7]], "buzzerGpioA": payload[8], "buzzerGpioB": payload[9],
              "buzzerFrequencyHz": struct.unpack_from("<H", payload, 10)[0]}
    if encode_aux_configuration(config) != payload:
        raise ValueError("Noncanonical three-button IO response")
    return config
