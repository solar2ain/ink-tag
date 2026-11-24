"""Offline admission for an explicitly requested second stock-capture read."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

SCHEMA = "inktag-tlsr-backup-verify-request-v1"
GEOMETRY = {"EB6013": 524288, "EB6014": 1048576, "856013": 524288}


def text(path: Path) -> str:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError(f"Missing/unsafe capture evidence: {path}")
    data = path.read_bytes()
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def bound(path: Path) -> dict:
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest().upper()}


def find_request(root: Path, *, chip: str, pcb: str, note: str, port: str) -> dict:
    if not re.fullmatch(r"COM[1-9][0-9]*", port, re.IGNORECASE):
        raise ValueError("VerifyOnly requires the exact COM port used by the original capture.")
    parents = [root / ".local/backups" / chip / pcb,
               root / ".local/backup-archive" / chip,
               root / "factory_backups" / chip / pcb]
    candidates = []
    for parent in parents:
        if not parent.exists():
            continue
        for console in parent.rglob("console.log"):
            # Continuations cannot become new primary captures or hide a
            # newer incomplete original attempt behind an older valid image.
            if any(part.startswith("verify-only-") for part in console.parts):
                continue
            events_path = console.parent / "events.jsonl"
            if not events_path.exists():
                continue
            events = [json.loads(line) for line in text(events_path).splitlines() if line.strip()]
            if not events:
                continue
            meta = events[0]
            if (meta.get("schema") != "inktag-flash-workflow-v1" or meta.get("action") != "backup" or
                    meta.get("pcb_silkscreen") != pcb or meta.get("board_note", "") != note or
                    meta.get("verification_of") or meta.get('resumption_of')):
                continue
            commands = [event["command"] for event in events if event.get("phase") == "backup" and
                        event.get("message") == "Starting maintained tool" and isinstance(event.get("command"), list)]
            if len(commands) != 1:
                raise ValueError("Matching capture lacks an unambiguous original command; no older fallback.")
            command = commands[0]
            try:
                original_port = command[command.index("-Port") + 1]
                original_chip = command[command.index("-Chip") + 1]
            except (ValueError, IndexError):
                continue
            if original_port.upper() != port.upper() or original_chip != chip:
                continue
            candidates.append((console.stat().st_mtime_ns, console.parent, meta, command))
    if not candidates:
        raise ValueError("No matching first capture found; VerifyOnly will not start a first read.")
    _, unit, meta, command = max(candidates, key=lambda item: item[0])
    result_path = unit / "result.json"
    if not result_path.exists():
        raise ValueError("Latest matching capture has not exited; no concurrent verification allowed.")
    result = json.loads(text(result_path))
    for key in ("schema", "board", "action", "unit", "pcb_silkscreen", "board_note"):
        if result.get(key) != meta.get(key):
            raise ValueError("Capture result identity differs from its events; no device access.")
    if result.get("status") not in ("failed", "tool-success") or not result.get("ended_utc"):
        raise ValueError("Original capture outcome is incomplete/unknown; no device access.")
    capture = unit / "backup"
    geometry_path = capture / "jedec-identification.log"
    read_path = capture / "read-1.log"
    primary = capture / "tlsr8359-flash.bin"
    geometry = text(geometry_path)
    match = re.search(r"Flash JEDEC samples:\s+([0-9A-F]{6})\s+\1\s+\1\s*(?:\r?\n|$)", geometry)
    if not match or match[1] not in GEOMETRY:
        raise ValueError("Original capture lacks three matching admitted JEDEC samples.")
    jedec, size = match[1], GEOMETRY[match[1]]
    for register in (1, 2):
        if not re.search(rf"Flash status-{register} samples:\s+(0x[0-9A-Fa-f]{{2}})\s+\1\s+\1\s*(?:\r?\n|$)", geometry):
            raise ValueError("Original Flash status evidence is missing/inconsistent.")
    read_log = text(read_path)
    if (not re.search(rf"Read Flash from 0x000000 to 0x{size:06x}\.\.\.", read_log, re.IGNORECASE) or
            f"Observed Flash JEDEC: {jedec}" not in read_log or
            not re.search(r"Reading 100\.0%", read_log) or
            not re.search(r"(?:^|\n)Done!\s*(?:\n|$)", read_log)):
        raise ValueError("Latest first read did not finish; no fallback to an older capture.")
    if not primary.is_file() or primary.is_symlink() or primary.stat().st_size != size:
        raise ValueError("Latest primary backup is missing/incomplete; VerifyOnly will not re-read it.")
    def original_value(name, default):
        if name not in command:
            return default
        try:
            return int(command[command.index(name) + 1])
        except (IndexError, ValueError):
            raise ValueError("Malformed original transport setting; no device access.") from None
    original_transport = dict(baud=original_value('-Baud', 460800) or 460800,
        activationBaud=original_value('-ActivationBaud', 0), activationMs=original_value('-ActivationMs', 20000),
        clockMHz=original_value('-ClockMHz', 0), unqualifiedBaud='-AllowUnqualifiedBaud' in command)
    return {"schema": SCHEMA, "chip": chip, "pcb": pcb, "boardNote": note, "port": port.upper(),
            "address": 0, "sizeBytes": size, "flashJedec": jedec, "baud": original_transport['baud'],
            "originalTransport": original_transport,
            "originalUnit": str(unit.resolve()), "primary": bound(primary),
            "evidence": [bound(result_path), bound(unit / "events.jsonl"), bound(geometry_path), bound(read_path)],
            "firstReadReplayed": False, "flashWrites": False,
            "qualification": "second-read comparison only; not exact-board/stock-origin/Full qualification"}
