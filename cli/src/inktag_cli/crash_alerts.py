from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from .local_server import default_data_directory


CRASH_REASON_LABELS = {
    1: "watchdog",
    2: "hardfault",
    3: "low-voltage reset",
}

HEALTH_OPERATION_LABELS = {
    0: "boot",
    1: "BLE",
    2: "display",
    3: "transfer",
    4: "storage",
    5: "OTA",
    6: "NFC",
}


def _state_path() -> Path:
    override = os.environ.get("INKTAG_CRASH_ACK_PATH")
    return Path(override) if override else default_data_directory() / "crash-acknowledgements.json"


def _read_state(path: Path) -> dict[str, int]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, ValueError, TypeError):
        return {}
    if not isinstance(value, dict):
        return {}
    return {
        str(key): int(count)
        for key, count in value.items()
        if isinstance(count, int) and count >= 0
    }


def crash_warning(device_id: str, health: dict[str, Any], *, path: Path | None = None) -> dict[str, Any] | None:
    """Return an unacknowledged crash warning without changing persistent state."""
    total = int(health.get("totalCrashes", 0))
    if total <= 0:
        return None
    acknowledged = _read_state(path or _state_path()).get(device_id)
    if acknowledged is not None and total <= acknowledged:
        return None
    latest = health.get("latestCrash") if isinstance(health.get("latestCrash"), dict) else None
    warning: dict[str, Any] = {
        "type": "new-device-crash",
        "deviceId": device_id,
        "newCrashes": total if acknowledged is None else total - acknowledged,
        "totalCrashes": total,
        "resetReason": int(latest.get("resetReason", health.get("resetReason", 0))) if latest else int(health.get("resetReason", 0)),
    }
    if latest:
        reason = int(latest.get("reason", 0))
        operation = int(latest.get("operation", 0))
        warning.update({
            "reason": CRASH_REASON_LABELS.get(reason, f"unknown ({reason})"),
            "reasonCode": reason,
            "operation": HEALTH_OPERATION_LABELS.get(operation, f"unknown ({operation})"),
            "operationCode": operation,
            "stage": latest.get("stage"),
            "voltageMv": int(latest.get("voltageMv", 0)),
            "uptimeMs": int(latest.get("uptimeMs", 0)),
        })
        registers = {
            key: value for key in ("pc", "lr", "cfsr", "hfsr")
            if (value := latest.get(key)) not in (None, 0, "0x00000000")
        }
        if registers:
            warning["registers"] = registers
    return warning


def acknowledge_crashes(device_id: str, total_crashes: int, *, path: Path | None = None) -> None:
    target = path or _state_path()
    state = _read_state(target)
    state[device_id] = max(0, int(total_crashes))
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(target.suffix + ".tmp")
    temporary.write_text(json.dumps(state, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    temporary.replace(target)
