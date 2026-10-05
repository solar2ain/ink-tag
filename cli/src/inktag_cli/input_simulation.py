"""Directed INPUT_SIMULATE completion contract; legacy ACK is explicit."""
from .generated_commands_v2 import TestOperationV2

ACTION_NAMES = ("disabled", "nextPage", "showSlot", "showDeviceInfo",
                "showCalendar", "showClock", "toggleSlideshow", "stop",
                "controlUrl", "toggleClearPrevious")


def structured_input_ack(payload: bytes) -> bool:
    if not payload:
        return False
    if payload != bytes((int(TestOperationV2.INPUT_SIMULATE), 1)):
        raise ValueError("Unknown INPUT_SIMULATE completion declaration")
    return True


def decode_input_completion(payload: bytes, selector: int) -> str:
    if (selector not in range(4) or len(payload) != 4 or
            payload[:2] != bytes((int(TestOperationV2.INPUT_SIMULATE), selector))):
        raise ValueError("Mismatched INPUT_SIMULATE completion")
    action, outcome = payload[2:]
    if (action >= len(ACTION_NAMES) or outcome not in (0, 1) or
            (selector == 0 and action == 9) or (selector != 0 and action == 8) or
            (action in (0, 8) and outcome != 0)):
        raise ValueError("Invalid INPUT_SIMULATE action/outcome")
    prefix = "nfc" if selector == 0 else "button"
    return f"{prefix}:done action={ACTION_NAMES[action]} outcome={'completed' if outcome else 'no-op'}"
