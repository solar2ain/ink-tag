from __future__ import annotations

from dataclasses import dataclass


LED_COLORS = {
    "off": 0x00,
    "red": 0x01,
    "green": 0x02,
    "yellow": 0x03,
    "blue": 0x04,
    "magenta": 0x05,
    "cyan": 0x06,
    "white": 0x07,
}

LED_EFFECTS = {
    "off",
    "solid",
    "slow-flash",
    "fast-flash",
    "flash-once",
    "flash-twice",
    "flash-three",
    "sequence",
}

_EFFECT_ALIASES = {
    "slow": "slow-flash",
    "fast": "fast-flash",
    "once": "flash-once",
    "twice": "flash-twice",
    "three": "flash-three",
    "rgb": "sequence",
    "rgb-sequence": "sequence",
}


@dataclass(frozen=True)
class LedStep:
    color_mask: int
    duration_ms: int


def normalize_led_color(value: str) -> str:
    normalized = value.strip().lower()
    if normalized not in LED_COLORS:
        raise ValueError(
            "LED color must be off, red, green, blue, yellow, magenta, cyan or white"
        )
    return normalized


def normalize_led_effect(value: str) -> str:
    normalized = value.strip().lower().replace("_", "-")
    normalized = _EFFECT_ALIASES.get(normalized, normalized)
    if normalized not in LED_EFFECTS:
        raise ValueError(
            "LED effect must be off, solid, slow-flash, fast-flash, "
            "flash-once, flash-twice, flash-three or sequence"
        )
    return normalized


def build_led_effect(
    color: str,
    effect: str,
    *,
    repeat: int = 1,
    step_ms: int | None = None,
    duration_ms: int = 0,
) -> tuple[list[LedStep], bool]:
    """Expand a named diagnostic effect into host-timed LED channel writes.

    The returned boolean tells the caller whether it must send OFF after the
    final delay. A zero-duration solid effect uses the device's existing
    bounded diagnostic lifetime and can be stopped early with ``led off``.
    """

    color_name = normalize_led_color(color)
    effect_name = normalize_led_effect(effect)
    if repeat < 1 or repeat > 100:
        raise ValueError("repeat must be between 1 and 100")
    if duration_ms < 0 or duration_ms > 600_000:
        raise ValueError("duration-ms must be between 0 and 600000")

    mask = LED_COLORS[color_name]
    if effect_name == "off" or color_name == "off":
        return [LedStep(0, 0)], False
    if effect_name == "solid":
        return [LedStep(mask, duration_ms)], duration_ms > 0

    default_step = 120 if effect_name in {
        "fast-flash", "flash-once", "flash-twice", "flash-three"
    } else 240
    interval = default_step if step_ms is None else step_ms
    if interval < 1 or interval > 60_000:
        raise ValueError("step-ms must be between 1 and 60000")

    if effect_name == "sequence":
        colors = [bit for bit in (0x01, 0x02, 0x04) if mask & bit]
        if not colors:
            return [LedStep(0, 0)], False
        steps = [LedStep(bit, interval) for _ in range(repeat) for bit in colors]
        return steps, True

    pulses = {
        "slow-flash": 1,
        "fast-flash": 1,
        "flash-once": 1,
        "flash-twice": 2,
        "flash-three": 3,
    }[effect_name]
    steps = [
        LedStep(mask if phase % 2 == 0 else 0, interval)
        for _ in range(repeat)
        for phase in range(pulses * 2)
    ]
    return steps, True


def parse_led_pattern(value: str, *, repeat: int = 1) -> list[LedStep]:
    """Parse ``color:milliseconds`` comma-separated diagnostic steps."""

    if repeat < 1 or repeat > 100:
        raise ValueError("repeat must be between 1 and 100")
    parsed: list[LedStep] = []
    for raw_step in value.split(","):
        token = raw_step.strip()
        if not token:
            continue
        color, separator, raw_duration = token.partition(":")
        if not separator:
            raise ValueError("each LED pattern step must use color:milliseconds")
        color_name = normalize_led_color(color)
        try:
            duration_ms = int(raw_duration.strip(), 10)
        except ValueError as exc:
            raise ValueError("LED pattern duration must be an integer number of milliseconds") from exc
        if duration_ms < 1 or duration_ms > 60_000:
            raise ValueError("each LED pattern duration must be between 1 and 60000 ms")
        parsed.append(LedStep(LED_COLORS[color_name], duration_ms))
    if not parsed:
        raise ValueError("LED pattern must contain at least one step")

    steps = parsed * repeat
    if sum(step.duration_ms for step in steps) > 600_000:
        raise ValueError("LED pattern must not exceed 600000 ms in total")
    return steps
