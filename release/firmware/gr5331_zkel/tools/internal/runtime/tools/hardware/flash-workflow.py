#!/usr/bin/env python3
"""Internal TLSR stock-read engine and shared bounded logging/process helpers.

No BLE, vendor command generation, firmware build, automatic unlock, or write
retry lives here. --dry-run and --list-boards never start a hardware tool.
"""
from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "inktag-flash-workflow-v1"
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
TOOL_ERROR = re.compile(r"(?i)\b(error|exception|failed|failure|refused|denied|mismatch|invalid|timeout|timed out|cannot|not found)\b|Flash JEDEC differs|no array read|错误|失败|拒绝|超时")
TOOL_EXPLANATION = "A validated SWS response ends the activation window immediately; the full duration is only a timeout."
TOOL_PROGRESS = re.compile(r"(?i)\b(read|reading|write|writing|program|programming|verify|verifying|verification|erase|erasing|flash|download|progress)\b")
TOOL_RESET = re.compile(r"(?i)^\s*(?:RESET (?:RTS|SWS):|Reset module \(RTS low\)|Run application with TLSR8359 whole-chip reset|Activation detected after)")
TOOL_CONTACT = re.compile(r"(?i)^\s*(?:CONTACT WAIT:|BACKUP STAGE:|FLASH BUS:|PC readback incomplete|PC = 0x|-{5,} DETECTION RETRY \d+ -{5,})")
TOOL_WARNING = re.compile(r"(?i)^\s*(?:WARNING:|WARNING \[|Unstable 256-byte SWS frame)")
TOOL_IDENTITY = re.compile(r"(?i)^\s*(?:Observed Flash JEDEC:|Observed Flash status:|Flash JEDEC samples:|Flash status samples:|VERIFY DIAGNOSTIC:)")
BYTE_PROGRESS = re.compile(r"\((\d+(?:\.\d+)?)/(\d+(?:\.\d+)?) KiB\)")
PERCENT = re.compile(r"(?<![\d.])(\d{1,3}(?:\.\d+)?)\s*%")


def windows_console_style(stream, foreground):
    """Change only the existing console foreground; caller restores it."""
    from ctypes import wintypes
    class Coord(ctypes.Structure):
        _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short)]
    class Rect(ctypes.Structure):
        _fields_ = [(key, ctypes.c_short) for key in ("left", "top", "right", "bottom")]
    class Info(ctypes.Structure):
        _fields_ = [("size", Coord), ("cursor", Coord), ("attributes", ctypes.c_ushort),
                    ("window", Rect), ("maximum", Coord)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetStdHandle.argtypes = [wintypes.DWORD]
    kernel.GetStdHandle.restype = wintypes.HANDLE
    kernel.GetConsoleScreenBufferInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Info)]
    kernel.SetConsoleTextAttribute.argtypes = [wintypes.HANDLE, wintypes.WORD]
    try:
        import msvcrt
        handle = msvcrt.get_osfhandle(stream.fileno())
    except (OSError, AttributeError, ValueError):
        handle = kernel.GetStdHandle(-12 if stream is sys.stderr else -11)
    info = Info()
    if not kernel.GetConsoleScreenBufferInfo(handle, ctypes.byref(info)):
        return None
    if foreground is not None and not kernel.SetConsoleTextAttribute(handle, (info.attributes & 0xFFF0) | foreground):
        return None
    return lambda: kernel.SetConsoleTextAttribute(handle, info.attributes)


def tool_is_error(line: str) -> bool:
    # One registered informational prompt, not a generic timeout suppression.
    return line.strip() != TOOL_EXPLANATION and bool(TOOL_ERROR.search(line))


def tlsr_read_failure(text: str):
    """Presentation-only classification of the maintained reader's final footer.

    Never turn a decoded mismatch into success, blame a capture, or use this
    classification to select a retry. Raw vendor text remains in raw logs.
    """
    match = re.search(r'Error: could not verify Flash data at (0x[0-9a-fA-F]{6}) after (\d+) reads! No/invalid frame=(\d+); decoded mismatch=(\d+)\.', ANSI.sub('', text))
    if match:
        offset, attempts, missing, mismatches = match.groups()
        attempts, missing, mismatches = int(attempts), int(missing), int(mismatches)
        if not 1 <= attempts <= 6 or missing + mismatches != attempts:
            return None
        if not missing:
            kind = 'readback-data-mismatch'
            summary = (f'VERIFY FAILED (DATA MISMATCH): {offset}; {mismatches} complete decoded reads differ from the selected first capture. '
                       'Data was received; this is not a no-response failure. Neither capture is declared correct.')
        elif not mismatches:
            kind = 'readback-no-valid-data'
            summary = f'READ FAILED (NO VALID DATA): {offset}; all {missing} attempts returned missing/invalid frames. No complete block was available to compare.'
        else:
            kind = 'readback-mixed-failure'
            summary = f'READ/VERIFY FAILED (MIXED): {offset}; {missing} missing/invalid frames and {mismatches} complete decoded mismatches. No verified block.'
        return dict(kind=kind, summary=summary, flash_offset=offset,
                    attempts=attempts, missing_invalid=missing, decoded_mismatches=mismatches)
    return None


def console_print(line, *, level="notice", stream=None, end="\n"):
    """Colour interactive consoles only; no ANSI in files or redirected output."""
    stream = stream or sys.stdout
    colours = {"error": (12, 31), "warning": (14, 33), "result": (10, 32)}
    foreground, ansi_colour = colours.get(level, (7, 39))
    if "NO_COLOR" in os.environ or not stream.isatty():
        print(line, file=stream, end=end, flush=True)
        return
    if sys.platform == "win32":
        restore = None
        try:
            restore = windows_console_style(stream, foreground)
        except (OSError, AttributeError, ValueError):
            pass  # A redirected/unsupported console stays readable plain text.
        try:
            print(line, file=stream, end=end, flush=True)
        finally:
            if restore:
                restore()
    else:
        try:
            print(f"\x1b[{ansi_colour}m{line}", file=stream, end="", flush=True)
        finally:
            # An interrupt during the coloured write must still reset SGR.
            print("\x1b[0m", file=stream, end=end, flush=True)


@dataclass(frozen=True)
class Board:
    name: str
    wire: str
    port: str
    repo: str = "main"


BOARDS = {
    b.name: b for b in (
        Board("gr5331_zkc21v", "gr5331_zkc21v", "goodix/gr5331"),
        Board("gr5513_zkc42v", "gr5513_zkc42v", "goodix/gr551x"),
        Board("gr5513_zkc42vm", "gr5513_zkc42vm", "goodix/gr551x"),
        Board("gr5515_zkc42vm", "gr5515_zkc42vm", "goodix/gr551x"),
        *(Board(f"om6626_{n}", f"om6626_{n}", "onmicro/om6626")
          for n in ("zkc21b", "zkc21v", "zkc42b")),
        *(Board(f"tlsr8359_{n}", old, "telink/tlsr8359") for n, old in (
            ("nowa154r", "hs_nowa154"), ("nowa213r", "hs_nowa213"),
            ("nebular213r", "hs_nebular213"), ("nebular420r", "hs_nebular420"))),
        Board("nrf52832_priceetag", "nrf52832_priceetag", "nordic/nrf52832"),
        Board("nrf52811_asel22", "aesl22_nrf", "nordic/nrf52811", "nrf52811"),
        Board("da14585_asel29", "aesl29_da", "dialog/da14585", "da14585"),
        Board("da14585_asel42", "aesl42_da", "dialog/da14585", "da14585"),
    )
}
ALIASES = {b.wire: b.name for b in BOARDS.values() if b.wire != b.name}
ALIASES["gr551x_zkc42v"] = "gr5513_zkc42v"
PCB_LABELS = {"tlsr8359_nebular420r": "HSEL4Q_04_20M_33", "tlsr8359_nebular213r": "HSEL4Q_02_13M_30",
              "tlsr8359_nowa154r": "HS_EL5101_5M_66_05", "tlsr8359_nowa213r": "HS_EL5102_1M_67_33"}
PCB_ALIASES = {marking.lower(): board for board, marking in PCB_LABELS.items()}
# gr551x_zkc42vm is intentionally not an alias: its exact MCU is ambiguous.


def resolve_board(value: str) -> Board:
    value = value.strip().lower()
    value = ALIASES.get(value, value)
    if value not in BOARDS:
        raise ValueError(f"Unknown/ambiguous board {value!r}; use --list-boards (MCU_model).")
    return BOARDS[value]


def backup_pcb_label(args: argparse.Namespace, board: Board) -> str:
    """Archive metadata only, never a new hardware/firmware selector."""
    if board.name in PCB_LABELS:
        return PCB_LABELS[board.name]
    if not args.board:
        return "UNIDENTIFIED-PCB"
    known = PCB_ALIASES.get(args.board.strip().lower(), args.board.strip().lower())
    known = ALIASES.get(known, known)
    if known in PCB_LABELS:
        return PCB_LABELS[known]
    label = args.board
    if label.upper() == 'HSELAQ_04_37MG_38':
        raise ValueError('HSELAQ_04_37MG_38 is human-confirmed non-TLSR; use the OM owning backup tools. No device accessed.')
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", label):
        raise ValueError("PCB marking must be 1-64 letters/digits/underscores/hyphens, not a path.")
    if label.upper() in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}:
        raise ValueError("PCB marking cannot be a reserved Windows filename.")
    return label.upper()


def resolve_target(args: argparse.Namespace) -> Board:
    if args.chip:
        if args.action not in ("probe", "backup") or (args.action == "probe" and args.board):
            raise ValueError("--chip is only for probe/backup; probe does not select a PCB.")
        if args.board:
            value = args.board.strip().lower()
            value = PCB_ALIASES.get(value, value)
            value = ALIASES.get(value, value)
            if value in BOARDS:
                board = BOARDS[value]
                if board.port != "telink/tlsr8359" or args.chip not in ("tlsr8359", "hs9118", "tlsr825x"):
                    raise ValueError("Requested chip does not match the registered PCB; no device accessed.")
                if args.action != "backup":
                    return board
                # Recognized markings also stay metadata in Chip backup:
                # actual JEDEC determines the capture size, not a PCB preset.
            # New silkscreens are allowed for read-only backup grouping. Do not
            # register a board, infer a screen/pin map, or reuse sibling geometry.
            target = Board("tlsr8359_transport_probe", "", "telink/tlsr8359")
            backup_pcb_label(args, target)
            return target
        # Chip-only backup is JEDEC-bounded; no exact PCB/write qualification.
        return Board("tlsr8359_transport_probe", "", "telink/tlsr8359")
    if not args.board:
        raise ValueError("--board is required for backup/Full; probe may use --chip.")
    return resolve_board(args.board)


def primary_root(root: Path) -> Path:
    """Read Git's worktree pointer, never scan another session's worktree."""
    pointer = root / ".git"
    if pointer.is_file():
        line = pointer.read_text(encoding="utf-8").strip()
        if line.startswith("gitdir:"):
            admin = Path(line.split(":", 1)[1].strip())
            if not admin.is_absolute():
                admin = root / admin
            common = admin / "commondir"
            if common.is_file():
                path = Path(common.read_text(encoding="utf-8").strip())
                if not path.is_absolute():
                    path = admin / path
                return path.resolve().parent
    return root




def entry(root: Path, board: Board, action: str) -> Path:
    # OM's maintained double-read backend supersedes the old manual-import
    # wrapper for live backup. --source still selects the read-only importer.
    name = "flash" if action == "full" else action
    if board.port == "telink/tlsr8359" and action == "backup":
        name = "internal/backup-operation"
    if board.port == "telink/tlsr8359" and action == "probe":
        name = "internal/probe-operation"
    return root / "firmware/ports" / board.port / "tools" / f"{name}.ps1"


def add_value(command: list[str], name: str, value: object) -> None:
    if value is not None and value != "":
        command.extend((f"-{name}", str(value)))


def build_command(args: argparse.Namespace, board: Board, action: str, unit: Path,
                  *, stock_backup: str | None = None, expected_mac: str | None = None) -> list[str]:
    # This internal engine serves only TLSR stock reads. Each MCU owns its
    # Full entry; stale cross-platform selectors must never resolve a writer.
    if board.port != "telink/tlsr8359" or action not in ("probe", "backup"):
        raise ValueError("Cross-platform/Full dispatch is retired; use the MCU-owned tools.")
    script = entry(ROOT, board, action)
    if args.verify_only and action == "backup":
        script = script.with_name("backup-verify-operation.ps1")
    command = [args.powershell or "pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)]
    add_value(command, "Chip", args.chip or "tlsr8359")
    if action == "backup" and args.chip and board.wire:
        add_value(command, "Board", board.wire)
    elif action == "backup" and not args.chip:
        add_value(command, "Board", board.name)
    add_value(command, "Port", args.port)
    add_value(command, "ToolPath", args.tool_path)
    add_value(command, "Python", sys.executable)
    if action == "probe":
        command.extend(("-NoWait", "-NoManualReset"))
        if args.probe_baud:
            add_value(command, "Baud", args.probe_baud)
        if args.probe_activation_ms:
            add_value(command, "ActivationMs", args.probe_activation_ms)
        if args.probe_clock_mhz >= 0:
            add_value(command, "ClockMHz", args.probe_clock_mhz)
        if args.allow_unqualified_probe_baud:
            command.append("-AllowUnqualifiedBaud")
    else:
        add_value(command, "OutputDir", str(unit / "backup"))
        add_value(command, "DeviceLabel", args.board_note)
        add_value(command, "PcbSilkscreen", backup_pcb_label(args, board))
        for name, value in (("Baud", args.backup_baud or 921600),
                            ("ActivationBaud", args.backup_activation_baud),
                            ("ActivationMs", args.backup_activation_ms),
                            ("ClockMHz", args.backup_clock_mhz)):
            add_value(command, name, value)
        if args.allow_unqualified_backup_baud:
            command.append("-AllowUnqualifiedBaud")
        if args.verify_only:
            add_value(command, "RequestFile", str(unit / "verification-request.json"))
        if not args.no_wait:
            command.append("-RetryDetect")
            add_value(command, "DetectTimeoutSeconds", args.timeout_seconds)
            add_value(command, "DetectIntervalSeconds", args.interval_seconds)
    return command


def script_parameters(path: Path) -> set[str]:
    source = path.read_text(encoding="utf-8-sig")
    # Permit legal comments/#requires/CmdletBinding, not executable code or
    # the parameter block of a nested function.
    block = re.match(r"(?ims)\A\s*(?:(?:\#[^\n]*(?:\n|$)|<\#.*?\#>)\s*)*(?:\[CmdletBinding\([^\]]*\)\]\s*)?param\((.*?)^\)", source)
    if not block:
        raise ValueError(f"No public parameter block: {path}")
    return set(re.findall(r"\[[\w\[\]]+\]\s*\$(\w+)", block.group(1)))


def check_command(command: list[str]) -> None:
    path = Path(command[command.index("-File") + 1])
    if not path.is_file():
        raise ValueError(f"Maintained source entry is missing: {path}; specify the independent repository path.")
    allowed = script_parameters(path)
    for value in command[command.index("-File") + 2:]:
        if value.startswith("-") and value[1:] not in allowed:
            raise ValueError(f"{path.name} does not yet support {value}. Update its reviewed host tool; no hardware started.")


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def terminal_prefix(level: str, timestamp: str | None = None) -> str:
    tags = {"error": "ERROR", "warning": "WARNING", "result": "SUCCESS",
            "diagnostic": "DEBUG", "notice": "INFO", "progress": "INFO"}
    moment = datetime.fromisoformat(timestamp) if timestamp else datetime.now(timezone.utc)
    local = moment.astimezone().isoformat(timespec="seconds").replace("T", " ")
    return f"[{local}] [{tags[level]}]"


def open_console_output(enabled: bool):
    # PowerShell may pipe native stdout even in ConsoleHost. The outer shell
    # explicitly attests non-redirected console output before opting in here.
    # Never open another window, and never bypass ordinary redirected output.
    if enabled and sys.platform == "win32":
        try:
            return open("CONOUT$", "w", encoding="utf-8", buffering=1)
        except OSError:
            pass  # No attached console: retain readable pipe/file output.
    return None


def progress_line(prefix, target, stage, percent, fields, columns=None):
    """Keep elapsed/counters ahead of a decorative bar when a row is narrow."""
    elapsed = fields.get('progress_elapsed_seconds', fields.get('elapsed_seconds'))
    elapsed_text = f"elapsed {elapsed:.1f}s" if elapsed is not None else ''
    bytes_text = (f"{fields['completed_kib']:g}/{fields['total_kib']:g} KiB"
                  if 'completed_kib' in fields else '')
    filled = max(0, min(20, int(float(percent) / 5)))
    full = f"{prefix} [{target}] {stage} [{'#' * filled + '.' * (20 - filled)}] {percent:g}%"
    if bytes_text:
        full += f"  {fields['completed_kib']:.2f}/{fields['total_kib']:.2f} KiB"
    if elapsed_text:
        full += f" | {elapsed_text}"
    if columns is None or len(full) < columns:
        return full
    compact_stage = re.sub(r'^Read ([12])/2$', r'R\1/2', stage)
    short_prefix = re.sub(r'([+-]\d{2}:\d{2}|Z)(?=\])', '', prefix)
    time_match = re.search(r'\d{2}:\d{2}:\d{2}', prefix)
    clock = f"[{time_match[0]}] [INFO]" if time_match else '[INFO]'
    candidates = [
        ' '.join(filter(None, (prefix, stage, f'{percent:g}%', bytes_text, elapsed_text))),
        ' '.join(filter(None, (short_prefix, compact_stage, f'{percent:g}%', bytes_text, elapsed_text))),
        ' '.join(filter(None, (clock, compact_stage, f'{percent:g}%', bytes_text, elapsed_text))),
        ' '.join(filter(None, (f'{percent:g}%', bytes_text, elapsed_text))),
        ' '.join(filter(None, (f'{percent:g}%', elapsed_text))),
        elapsed_text or f'{percent:g}%',
    ]
    for candidate in candidates:
        if len(candidate) < columns:
            return candidate
    return candidates[-1][:max(0, columns - 1)]


class Log:
    def __init__(self, path: Path, board: Board, action: str, unit: int, *, verbose: bool = False, target_label: str = "", console_output: bool = False):
        path.mkdir(parents=True, exist_ok=False)
        self.path = path
        self.board = board
        self.common = dict(schema=SCHEMA, board=board.name, action=action, unit=unit)
        self.events = (path / "events.jsonl").open("x", encoding="utf-8")
        self.console = (path / "console.log").open("x", encoding="utf-8")
        self.verbose = verbose
        self.target_label = target_label or ("TLSR SWS" if board.name.endswith("transport_probe") else board.name)
        self.owned_console = open_console_output(console_output)
        self.screen = self.owned_console or sys.stdout
        self.error_screen = self.owned_console or sys.stderr
        self.interactive = self.screen.isatty()
        self.restore_console_colour = None
        if self.interactive and sys.platform == 'win32':
            try:
                self.restore_console_colour = windows_console_style(self.screen, None)
            except (OSError, AttributeError, ValueError):
                pass
        self.progress_open = False
        self.progress_width = 0
        self.last_progress: dict[str, tuple[object, float]] = {}
        self.shown_errors: set[tuple[str, str]] = set()
        self.sws_response_captured = False
        self.last_tool_progress: dict[str, dict] = {}
        self.progress_started: dict[str, float] = {}
        self.read_pass: dict[str, str] = {}
        self.activity_frame = 0
        self.backup_stage = "Waiting for SWS/JEDEC detection; Flash reading has not started"

    def action_is_tlsr_backup(self) -> bool:
        return self.common["action"] == "backup" and self.common["board"].startswith("tlsr")

    def emit(self, phase: str, message: str, *, level: str = "diagnostic", **fields: object) -> None:
        message = ANSI.sub('', message)
        if level == 'progress' and fields.get('percent') is not None:
            # An address-start line is not completed data. For TLSR backup,
            # begin this display timer at the first observed confirmed bytes.
            now = time.monotonic()
            confirmed_progress = (fields.get('completed_kib', 0) > 0 or
                                  ('completed_kib' not in fields and fields['percent'] > 0))
            if phase in self.progress_started or not self.action_is_tlsr_backup() or confirmed_progress:
                origin = self.progress_started.setdefault(phase, now)
                fields['progress_elapsed_seconds'] = max(0.0, now - origin)
        record = dict(self.common, utc=utc(), phase=phase, message=message, level=level, **fields)
        self.events.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.events.flush()
        prefix = terminal_prefix(level, record["utc"])
        display_message = message
        if 'progress_elapsed_seconds' in fields and 'elapsed ' not in display_message:
            display_message += f" | elapsed {fields['progress_elapsed_seconds']:.1f}s"
        line = f"{prefix} [{self.target_label}] [{phase}] {display_message}"
        self.console.write(line + "\n")
        self.console.flush()
        if self.verbose:
            if level == "progress" and fields.get("percent") is None:
                self.activity_frame = self.activity_frame % 5 + 1
                display_message += " " + "." * self.activity_frame
            console_print(f"{prefix} [{self.target_label}] [{phase}] {display_message}",
                          level=level, stream=self.error_screen if level == "error" else self.screen)
        elif level == "progress":
            percent = fields.get("percent")
            key = "contact" if phase in ("probe", "wait") else phase
            now = time.monotonic()
            previous = self.last_progress.get(key)
            if previous and percent == previous[0] and now - previous[1] < 5:
                return
            self.last_progress[key] = (percent, now)
            if percent is None:
                self.activity_frame = self.activity_frame % 5 + 1
                text = f"{prefix} [{self.target_label}] [{phase}] {message} {'.' * self.activity_frame}"
            else:
                stage = self.read_pass.get(phase, phase)
                display_fields = fields
                if self.action_is_tlsr_backup() and 'progress_elapsed_seconds' not in fields:
                    display_fields = {key: value for key, value in fields.items() if key != 'elapsed_seconds'}
                text = progress_line(prefix, self.target_label, stage, percent, display_fields)
            if self.interactive:
                previous_width = self.progress_width
                try:
                    columns = os.get_terminal_size(self.screen.fileno()).columns
                    if columns > 1:
                        previous_width = min(previous_width, columns - 1)
                        if percent is not None:
                            text = progress_line(prefix, self.target_label, stage, percent, display_fields, columns)
                        text = text[:columns - 1]  # Avoid wrapping into a second line.
                except (OSError, AttributeError, ValueError):
                    pass
                console_print('\r' + text + ' ' * max(0, previous_width - len(text)), level='progress', end='', stream=self.screen)
                self.progress_width = len(text)
                self.progress_open = True
            else:
                print(text, file=self.screen, flush=True)
        elif level in ("error", "result", "notice", "warning"):
            self.end_progress()
            console_print(f"{prefix} [{self.target_label}] [{phase}] {message}", level=level,
                          stream=self.error_screen if level == "error" else self.screen)

    def end_progress(self) -> None:
        if self.progress_open:
            print(file=self.screen, flush=True)
            self.progress_open = False
            self.progress_width = 0

    def activity_message(self, phase: str, elapsed: float | None = None, *, has_progress: bool = False) -> str:
        if phase == 'probe' and self.board.port == 'telink/tlsr8359':
            state = ('SWS response captured; completing bounded probe' if self.sws_response_captured
                     else 'Waiting for SWS activation; Flash reading has not started')
            return state + (f'; elapsed {elapsed:.1f}s' if elapsed is not None else '')
        if phase == 'backup' and self.action_is_tlsr_backup():
            return self.backup_stage
        return (f'Tool running; elapsed {elapsed:.1f}s' + ('' if has_progress else '; percentage unavailable') if elapsed is not None
                else 'Running; percentage unavailable until the tool reports it')

    def tool_line(self, phase: str, line: str) -> None:
        # Raw bytes and full diagnostic text always reach files. Only actual
        # vendor percentages become default console progress, never estimates.
        clean = ANSI.sub('', line)
        decorated = re.match(r"^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:[+-]\d{2}:\d{2}|Z)\] \[(INFO|DEBUG|WARNING|ERROR|SUCCESS)\] (.*)$", clean)
        if decorated:
            # The raw log retains the original inner timestamp. Avoid repeated
            # timestamp/tag decoration on the public console and normalized log.
            tag, clean = decorated.groups()
            if clean.strip() == TOOL_EXPLANATION:
                self.emit(phase, clean, level="notice")
                return
            if tag in ("ERROR", "WARNING", "SUCCESS"):
                self.emit(phase, clean, level={"ERROR": "error", "WARNING": "warning", "SUCCESS": "result"}[tag])
                return
            line = clean
        if clean.strip() == TOOL_EXPLANATION:
            self.emit(phase, clean, level="notice")
            return
        if clean.strip().startswith(("FULL-SCOPE:", "CONFIRMATION:")):
            self.emit(phase, clean, level="notice")
            return
        read_failure = tlsr_read_failure(clean) if self.action_is_tlsr_backup() else None
        if read_failure:
            self.shown_errors.add((phase, clean.strip()))
            self.emit(phase, read_failure['summary'], level='error',
                      failure_details=read_failure, original_tool_line=clean)
            return
        if re.fullmatch(r"\s*-{3,} DETECTION RETRY \d+ -{3,}\s*", clean):
            self.emit(phase, clean.strip(), level="notice")
            return
        if clean.strip().startswith("BACKUP STAGE:"):
            self.backup_stage = "Waiting for SWS/JEDEC detection; Flash reading has not started"
        read_pass = re.match(r"^\s*BACKUP READ ([12])/2:", clean)
        if read_pass:
            self.read_pass[phase] = f"Read {read_pass[1]}/2"
            self.backup_stage = f"Read {read_pass[1]}/2: waiting for activation/first confirmed data block"
            self.last_tool_progress.pop(phase, None)
            self.last_progress.pop(phase, None)
            self.progress_started.pop(phase, None)
            self.emit(phase, clean, level="notice", read_number=int(read_pass[1]), reads_total=2)
            return
        if TOOL_WARNING.search(clean):
            self.emit(phase, clean, level="warning")
            return
        if re.match(r"^\s*Activation detected after \d+ ms and \d+ validation attempt\(s\)", clean):
            self.sws_response_captured = True
            self.emit(phase, "SWS response captured. Activation readback passed; complete probe/chip identity not yet verified. " + clean.strip(),
                      level="notice", sws_response_captured=True, chip_identity_verified=False)
            return
        if self.sws_response_captured and re.search(r"(?i)Error read PC!", clean):
            self.shown_errors.add((phase, clean.strip()))
            self.emit(phase, "SWS response captured, but CPU PC read failed. Complete probe did not pass. " + clean.strip(),
                      level="error", sws_response_captured=True, probe_completed=False)
            return
        if tool_is_error(clean) and not (phase == "probe" and TRANSIENT.search(clean)):
            self.shown_errors.add((phase, clean.strip()))
            self.emit(phase, line, level="error")
            return
        if TOOL_RESET.search(clean):
            self.emit(phase, line, level="notice", reset_request_not_success=True)
            return
        if TOOL_CONTACT.search(clean):
            self.emit(phase, line, level="notice")
            return
        if TOOL_IDENTITY.search(clean):
            self.emit(phase, line, level="notice")
            return
        match = PERCENT.search(clean)
        if match and TOOL_PROGRESS.search(clean) and not tool_is_error(clean):
            percent = float(match.group(1))
            if 0 <= percent <= 100:
                counters = BYTE_PROGRESS.search(clean)
                fields = {"percent": percent}
                if counters:
                    fields.update(completed_kib=float(counters[1]), total_kib=float(counters[2]))
                self.last_tool_progress[phase] = fields
                self.emit(phase, line, level="progress", **fields)
                return
        self.emit(phase, line, level="notice" if decorated and decorated[1] == "INFO" else "diagnostic")

    def tool_failure(self, phase: str, result: Result) -> None:
        if self.verbose:
            return  # The complete live output has already been shown.
        lines = [ANSI.sub('', line).strip() for line in result.text.splitlines() if line.strip()]
        errors = [line for line in lines if tool_is_error(line)]
        for line in (errors or lines[-6:])[-8:]:
            if (phase, line) not in self.shown_errors:
                self.emit(phase, line, level="error")

    def close(self) -> None:
        try:
            self.end_progress()
            self.events.close()
            self.console.close()
        finally:
            try:
                if self.restore_console_colour is not None:
                    self.restore_console_colour()
            finally:
                if self.owned_console is not None:
                    self.owned_console.close()


@dataclass
class Result:
    code: int
    text: str


def stop_process(process: subprocess.Popen) -> None:
    # Only our owned child tree. A canceled write remains unknown, never retried.
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, check=False)
    else:
        os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        if os.name != "nt":
            os.killpg(process.pid, signal.SIGKILL)
        process.kill()
        process.wait(timeout=5)


def run_command(command: list[str], log: Log, phase: str, number: int,
                *, heartbeat_seconds: float = 5, operation_timeout: float = 900) -> Result:
    raw_path = log.path / f"{phase}-{number:03d}.raw.log"
    log.progress_started.pop(phase, None)
    log.emit(phase, "Starting maintained tool", command=command, raw_log=raw_path.name)
    tlsr_backup = phase == "backup" and log.action_is_tlsr_backup()
    log.emit(phase, log.activity_message(phase), level="progress")
    env = dict(os.environ, PYTHONUNBUFFERED="1", PYTHONIOENCODING="utf-8")
    kwargs = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {"start_new_session": True}
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, **kwargs)
    chunks: queue.Queue[bytes | None] = queue.Queue()

    def reader() -> None:
        assert process.stdout is not None
        try:
            while data := process.stdout.read1(4096):
                chunks.put(data)
        finally:
            process.stdout.close()
            chunks.put(None)

    worker = threading.Thread(target=reader, daemon=True)
    worker.start()
    started = last_heartbeat = time.monotonic()
    output = bytearray()
    pending = bytearray()
    code = None
    try:
        with raw_path.open("xb") as raw:
            while True:
                try:
                    data = chunks.get(timeout=min(0.2, heartbeat_seconds))
                except queue.Empty:
                    data = b""
                if data is None:
                    break
                if data:
                    raw.write(data)
                    raw.flush()
                    output.extend(data)
                    pending.extend(data)
                    # Vendor percentage bars often use CR, not newline.
                    while match := re.search(b"[\r\n]", pending):
                        end = match.start()
                        line = pending[:end].decode("utf-8", errors="replace")
                        del pending[:end + 1]
                        if line:
                            log.tool_line(phase, line)
                now = time.monotonic()
                if now - last_heartbeat >= heartbeat_seconds:
                    counters = log.last_tool_progress.get(phase, {})
                    message = ((f"{log.read_pass.get(phase, phase)}: latest confirmed byte count" if counters else log.backup_stage)
                               if tlsr_backup else log.activity_message(phase, now - started, has_progress=bool(counters)))
                    log.emit(phase, message,
                             level="progress", elapsed_seconds=now - started, **counters)
                    last_heartbeat = now
                if now - started >= operation_timeout:
                    stop_process(process)
                    code = 124
                    log.emit(phase, "Operation timed out; stopped owned tool. Write outcome may be unknown; no replay.", level="error")
            if pending:
                log.tool_line(phase, pending.decode("utf-8", errors="replace"))
        if code is None:
            try:
                code = process.wait(timeout=max(0.1, operation_timeout - (time.monotonic() - started)))
            except subprocess.TimeoutExpired:
                stop_process(process)
                code = 124
    except BaseException:
        stop_process(process)
        log.emit(phase, "Canceled/interrupted; preserve logs. No automatic write retry.", level="error")
        raise
    finally:
        worker.join(timeout=1)
    log.emit(phase, f"Tool exited {code}", exit_code=code,
             raw_sha256=hashlib.sha256(raw_path.read_bytes()).hexdigest())
    return Result(code, output.decode("utf-8", errors="replace"))


# Retry only a recognized no-contact result of a probe; a wrong identity, tool,
# malformed image or other refusal must stop. Never run full/backup in this loop.
TRANSIENT = re.compile(r"(?i)cannot connect to target|could not connect to target|failed to connect to target|"
                       r"no serial ports found|serial port .* is not present|no ACK|no direct Cortex-M0 response|"
                       r"no readable .*target|SWD target is not readable|nRF52832 target is not readable")
REFUSAL = re.compile(r"(?i)wrong .*target|MCU mismatch|non-Cortex|identity .*failed|reset fallback|"
                     r"not found:|not supported|requires J-Link|requires.*7\.88|unknown device")


def probe_absent(board: Board, text: str) -> bool:
    if REFUSAL.search(text):
        return False
    if board.port == "telink/tlsr8359":
        # Exact reviewed activation failure, before any Flash access. A PC
        # read failure, decoded response or baud handover failure is not absence.
        if re.search(r"(?i)Error read PC|handover .*failed|Traceback \(most recent|serial\.serialutil\.SerialTimeoutException|SerialTimeoutException:", text):
            return False
        return bool(re.search(
            r"Activation timed out after \d+ validation attempt\(s\), 0 decoded readback\(s\).*\(no Flash access\)",
            text, re.DOTALL)) or bool(re.search(r"(?i)serial port .* is not present|no serial ports found", text))
    return bool(TRANSIENT.search(text))


def probe_preflash_short_pc(board: Board, text: str) -> bool:
    """Typed incomplete probe, NOT target absence or a Flash/read failure.

    Only the explicit read-only probe RetryDetect loop may retry this stage.
    Identity/protocol refusals, complete invalid frames and arbitrary PC errors
    remain terminal. Never used by Full/backup/write execution.
    """
    if board.port!='telink/tlsr8359' or REFUSAL.search(text): return False
    if re.search(r'(?i)Traceback \(most recent|SerialTimeoutException:|handover .*failed|Erase Flash|Write Flash|Read Flash',text):
        return False
    return all(re.search(pattern,text,re.I) for pattern in (
        r'PROBE: SWS transport only; no Flash read/write',
        r'Activation detected after \d+ ms',
        r'register 0x0006BC, byte \d/4, UART symbols [0-8]/9; invalid/short frame',
        r'Error read PC!',
        r'releasing CPU stop and requesting one whole-chip reset after failed probe'))


def wait_probe(args: argparse.Namespace, board: Board, unit: Path, log: Log,
               run=run_command) -> Result:
    command = build_command(args, board, "probe", unit)
    started = time.monotonic()
    attempt = 0
    while True:
        attempt += 1
        remaining = args.timeout_seconds - (time.monotonic() - started) if args.timeout_seconds else args.operation_timeout_seconds
        if remaining <= 0:
            raise ValueError("Wait deadline reached; no full/backup operation started.")
        result = run(command, log, "probe", attempt, operation_timeout=min(args.operation_timeout_seconds, remaining))
        if result.code == 0:
            return result
        incomplete=args.action=='probe' and probe_preflash_short_pc(board,result.text)
        if args.no_wait or not (probe_absent(board, result.text) or incomplete):
            log.tool_failure("probe", result)
            raise ValueError("Probe failed/refused; not a recognized no-contact wait condition. See raw log.")
        if args.timeout_seconds and time.monotonic() - started >= args.timeout_seconds:
            raise ValueError("Wait deadline reached; no full/backup operation started.")
        log.emit("wait", f"-------------------- DETECTION RETRY {attempt + 1} --------------------", level="notice", next_attempt=attempt + 1)
        if incomplete:
            log.emit('wait','Pre-Flash PC frame incomplete; retrying read-only detection only, not treating it as a passed probe or starting Flash.',level='warning')
        log.emit("wait", f"Target not ready; elapsed {time.monotonic() - started:.1f}s; polling every {args.interval_seconds}s", level="progress")
        delay = args.interval_seconds
        if args.timeout_seconds:
            delay = min(delay, max(0, args.timeout_seconds - (time.monotonic() - started)))
        time.sleep(delay)


def validate(args: argparse.Namespace, board: Board) -> None:
    if board.port != 'telink/tlsr8359' or args.action not in ('probe', 'backup') or args.config:
        raise ValueError('Cross-platform/Full dispatch is retired; use the MCU-owned tools.')
    if args.resume and (args.action != 'backup' or board.port != 'telink/tlsr8359' or not args.chip or args.log_dir):
        raise ValueError('--resume belongs to TLSR --chip backup only, without --log-dir.')
    if args.action == 'full' and board.port in ('telink/tlsr8359','onmicro/om6626') and not args.config:
        raise ValueError('Full now requires --config plus --firmware; legacy Board/bin mode is retired.')
    if args.factory_session_dir and (args.action != "backup" or board.port != "telink/tlsr8359" or not args.chip or args.log_dir):
        raise ValueError("Factory raw session belongs to TLSR Chip backup only; no --log-dir override.")
    if args.verify_only and (args.action != "backup" or board.port != "telink/tlsr8359" or not args.chip):
        raise ValueError("--verify-only belongs to TLSR --chip backup only.")
    if args.verify_only and args.log_dir:
        raise ValueError("VerifyOnly uses a fresh continuation under the original capture; do not pass --log-dir.")
    if args.board_note:
        if args.action != "backup" or board.port != "telink/tlsr8359":
            raise ValueError("--board-note currently belongs to TLSR backup only.")
        if len(args.board_note) > 64 or any(ord(c) < 32 or ord(c) == 127 for c in args.board_note):
            raise ValueError("Board note must be a single line, at most 64 characters, without controls.")
    if args.chip and (args.action not in ("probe", "backup") or (args.action == "probe" and args.board)):
        raise ValueError("--chip is only for probe/backup; probe has no PCB selection.")
    probe_options = (args.probe_baud or args.probe_activation_ms or
                     args.probe_clock_mhz != -1 or args.allow_unqualified_probe_baud)
    if probe_options and (args.action != "probe" or board.port != "telink/tlsr8359"):
        raise ValueError("Probe transport options are only for TLSR probe.")
    if (args.probe_baud < 0 or (args.probe_baud and args.probe_baud < 340000) or
            args.probe_activation_ms < 0 or args.probe_clock_mhz < -1):
        raise ValueError("Invalid probe baud/activation/clock option.")
    backup_options = (args.backup_baud or args.backup_activation_baud or args.backup_clock_mhz or
                      args.backup_activation_ms != 20000 or args.allow_unqualified_backup_baud)
    if backup_options and (args.action != "backup" or board.port != "telink/tlsr8359"):
        raise ValueError("Backup transport options are only for TLSR backup.")
    if any(value < 0 or (value and value < 340000) for value in (args.backup_baud, args.backup_activation_baud)) or args.backup_activation_ms <= 0 or not 0 <= args.backup_clock_mhz <= 192:
        raise ValueError("Invalid backup baud/activation/clock option.")
    if args.backup_baud not in (0, 460800, 921600) and not args.allow_unqualified_backup_baud:
        raise ValueError("Custom read baud requires --allow-unqualified-backup-baud; standard rates are 460800/921600, not exact-board qualification.")
    if args.production and args.action != "full":
        raise ValueError("--production is only for full; probe and backup remain single-device operations.")
    if args.action == "full" and not args.firmware:
        raise ValueError("Full requires --firmware <reviewed exact-board Full file>; no automatic latest-image selection.")
    if args.action == "full" and board.name.startswith("om") and not all((args.factory_state, args.factory_device_info)):
        raise ValueError("OM Full requires explicit --factory-state and --factory-device-info; all three reviewed files must be bound, not mutable build defaults.")
    if args.interval_seconds < 1 or args.timeout_seconds < 0 or args.operation_timeout_seconds < 10:
        raise ValueError("interval >= 1, wait timeout >= 0, operation timeout >= 10 are required.")
    if args.frequency_hz is not None and args.frequency_hz < 1000:
        raise ValueError("frequency-hz must be >= 1000.")
    if args.skip_stock_backup_check and args.action != "full":
        raise ValueError("--skip-stock-backup-check is only for full.")
    if args.production and ((args.stock_backup and not args.skip_stock_backup_check) or args.expected_identity):
        raise ValueError("Production cannot reuse one unit's backup/identity for every board; supply each unit when prompted.")
    families = {
        "source": (board.name.startswith("om"), "backup"),
        "expected_mac": (board.name.startswith("tlsr"), "full"),
        "expected_identity": (board.repo == "nrf52811", "full"),
        "font_assets": (board.repo == "nrf52811", "full"),
        "factory_state": (board.name.startswith("om"), "full"),
        "factory_device_info": (board.name.startswith("om"), "full"),
        "gprogrammer_dir": (board.name.startswith("gr"), "full"),
        "console_exe": (board.name.startswith("gr"), "full"),
        "project_template": (board.repo == "da14585", "full"),
        "tool_path": (board.name.startswith("tlsr"), args.action),
        "allow_unqualified_layout": (board.port == "goodix/gr551x", "full"),
        "acknowledge_experimental_layout": (board.name.startswith("tlsr"), "full"),
        "allow_destructive_repartition": (board.name == "nrf52832_priceetag", "full"),
    }
    for option, (family, action) in families.items():
        if getattr(args, option) and (not family or args.action != action):
            raise ValueError(f"--{option.replace('_', '-')} is not applicable to this board/action.")
    if args.port != "auto" and not board.name.startswith("tlsr"):
        raise ValueError("--port is only for TLSR UART-SWS.")
    if args.source_layout != "v5" and not board.name.startswith("tlsr"):
        raise ValueError("--source-layout is only for TLSR Full.")
    if args.source and any((args.snr, args.jlink_exe, args.jflash_exe, args.frequency_hz)):
        raise ValueError("OM --source imports a finished dump without a probe; hardware tool options are not applicable.")
    if args.frequency_hz and board.name.startswith("om") and args.frequency_hz % 1000:
        raise ValueError("OM frequency-hz must be divisible by 1000 (whole kHz).")
    if args.jflash_exe and not ((board.name.startswith("om") and args.action == "backup") or
                               (board.repo == "da14585" and args.action in ("backup", "full"))):
        raise ValueError("--jflash-exe is only for OM backup or DA backup/Full.")
    if args.frequency_hz and board.name.startswith("gr") and args.action == "full":
        raise ValueError("Goodix Full speed is controlled by GProgrammer, not --frequency-hz.")
    if args.action == "full" and board.name.startswith("tlsr"):
        if args.port == "auto":
            raise ValueError("Full requires the exact qualified UART --port, not auto selection.")
        if not args.production and not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", args.expected_mac):
            raise ValueError("TLSR full requires --expected-mac for this exact unit.")
    if args.action != "full" and any((args.firmware, args.stock_backup, args.expected_mac,
                                      args.allow_unqualified_layout, args.allow_destructive_repartition,
                                      args.acknowledge_experimental_layout, args.clear_user_settings,
                                      args.clear_all_user_data, args.yes)):
        raise ValueError("Full-only options cannot be used with probe/backup.")
    if args.verify_only and not args.resume:
        from tlsr_backup_verify import find_request
        args.verification_request = find_request(primary_root(ROOT), chip=args.chip,
            pcb=backup_pcb_label(args, board), note=args.board_note, port=args.port)
    if args.yes and not (args.clear_user_settings or args.clear_all_user_data):
        raise ValueError("--yes skips the explicit clear confirmation only; select a clear option.")
    # Port scripts still contain Windows-only tool names, COM/PnP checks and
    # Start-Process modes. Do not silently run an unqualified Linux recipe.
    if sys.platform != "win32" and not args.plan and not args.source:
        raise ValueError("Native Linux hardware execution is not yet qualified for these maintained recipes. "
                         "--dry-run works on Linux; run the Windows command or qualify a vendor adapter first.")


def next_unit(read=input) -> bool:
    reply = read("Replace the board. Enter = next unit; q = finish: ").strip().lower()
    if reply == "q":
        return False
    if reply:
        raise ValueError("Only Enter or q is accepted; no device operation started.")
    return True


def unit_inputs(args: argparse.Namespace, board: Board, number: int, read=input) -> tuple[str, str]:
    # Skip means no historical file input, including an explicitly supplied
    # stale path. Mandatory live-unit preservation belongs to the port recipe.
    mac, backup = args.expected_mac, "" if args.skip_stock_backup_check else args.stock_backup
    if args.production:
        if board.name.startswith("tlsr"):
            mac = read("Exact MAC of this board: ").strip()
            if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", mac):
                raise ValueError("Invalid exact MAC; no probe/write started.")
        if not args.skip_stock_backup_check and board.repo != "nrf52811":
            backup = read("Verified backup path for this board (not a sibling's): ").strip()
            if not backup:
                raise ValueError("Per-unit backup path is required; no probe/write started.")
    return mac, backup


def session_parent(args: argparse.Namespace, board: Board) -> Path:
    if args.factory_session_dir:
        return Path(args.factory_session_dir).resolve()
    if args.log_dir:
        return Path(args.log_dir).resolve()
    elif args.action == "backup" and board.port == "telink/tlsr8359":
        chip = args.chip or "tlsr8359"
        session = primary_root(ROOT) / ".local/backups" / chip / backup_pcb_label(args, board)
        if args.board_note:
            # Human metadata only. Never use raw text as a path or selector.
            note_leaf = re.sub(r"[^A-Za-z0-9_-]", "_", args.board_note).strip("_")[:64] or "note"
            if note_leaf.upper() in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}:
                note_leaf = "note-" + note_leaf
            session /= note_leaf
        return session
    return ROOT / ".local/flashing"


def execute(args: argparse.Namespace, board: Board, run=run_command, read=input) -> int:
    if board.port != 'telink/tlsr8359' or args.action not in ('probe', 'backup') or args.config:
        raise ValueError('Cross-platform/Full dispatch is retired; use the MCU-owned tools.')
    if args.verify_only:
        session = Path(args.factory_session_dir or args.verification_request["originalUnit"])
        session /= f"verify-only-{datetime.now(timezone.utc):%Y%m%dT%H%M%S}-{uuid.uuid4().hex[:8]}"
    else:
        session = session_parent(args, board)
        session /= f"{board.name}-{args.action}-{datetime.now(timezone.utc):%Y%m%dT%H%M%S}-{uuid.uuid4().hex[:8]}"
    number = 0
    frozen_images: dict[str, str] | None = None
    while True:
        number += 1
        mac, backup = unit_inputs(args, board, number, read)
        unit = session / f"unit-{number:04d}"
        target_label = (backup_pcb_label(args, board) if args.action == "backup" and board.port == "telink/tlsr8359"
                        else args.chip or PCB_LABELS.get(board.name, board.name))
        log = Log(unit, board, args.action, number, verbose=args.verbose, target_label=target_label,
                  console_output=args.console_output)
        if args.action == "backup":
            log.common["board_note"] = args.board_note
            if board.port == "telink/tlsr8359":
                log.common["pcb_silkscreen"] = backup_pcb_label(args, board)
        if args.verify_only:
            log.common["verification_of"] = args.verification_request["originalUnit"]
            (unit / "verification-request.json").write_text(json.dumps(args.verification_request, indent=2) + "\n", encoding="utf-8")
        status = "failed"
        failure_details = None
        try:
            log.emit("preflight", f"Unit {number}; RetryDetect={'off (single detection)' if args.no_wait else 'on (pre-Flash detection only)'}; log={unit}", level="notice",
                     retry_detect=not args.no_wait,
                     skip_stock_backup_check=args.skip_stock_backup_check,
                     expected_mac=mac, stock_backup=backup)
            images = {}
            inputs = []
            for label in ("firmware", "factory_state", "factory_device_info", "font_assets", "source"):
                value = getattr(args, label)
                if not value:
                    continue
                path = Path(value).resolve()
                if not path.is_file():
                    raise ValueError(f"{label} input file missing: {path}; no probe/write started.")
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                images[label] = digest
                inputs.append(dict(input=label, path=str(path), bytes=path.stat().st_size, sha256=digest))
            if frozen_images is not None and images != frozen_images:
                raise ValueError("Production image inputs changed between units; stop and start a newly reviewed batch.")
            frozen_images = images
            log.emit("inputs", "Exact provided image inputs; vendor preservation remains separate", inputs=inputs)
            if args.verify_only:
                selected = args.verification_request["primary"]
                log.emit("verify-only", f"Selected complete first capture: {selected['path']} ({selected['bytes']} bytes). Reading second pass only; original files are preserved.",
                         level="notice", primary_sha256=selected["sha256"])
            probe = [] if args.verify_only else build_command(args, board, "probe", unit)
            command = build_command(args, board, args.action, unit, stock_backup=backup, expected_mac=mac)
            direct_tlsr_backup = args.action == "backup" and board.port == "telink/tlsr8359"
            if direct_tlsr_backup:
                log.emit("contact", "Direct backup activation/JEDEC gate; no standalone probe or pre-backup App restart.", level="notice")
            if not direct_tlsr_backup:
                check_command(probe)
            check_command(command)
            if not direct_tlsr_backup:
                wait_probe(args, board, unit, log, run)
            if args.action != "probe":
                result = run(command, log, args.action, 1, operation_timeout=args.operation_timeout_seconds)
                if result.code:
                    log.tool_failure(args.action, result)
                    if direct_tlsr_backup:
                        failure_details = tlsr_read_failure(result.text)
                    if failure_details:
                        raise ValueError(f"{failure_details['summary']} Exit {result.code}; captures preserved; no automatic replay.")
                    raise ValueError(f"Tool failed (exit {result.code}); no automatic read/write replay or next-board transition.")
            status = "tool-success"
            log.emit("complete", f"{args.action} completed; logs: {unit}. Not BLE/panel qualification.", level="result")
        except (ValueError, OSError, KeyboardInterrupt, EOFError) as exc:
            log.emit("failed", (str(exc) or "Interrupted; outcome may be unknown. No replay.") + f" Logs: {unit}", level="error")
            return 130 if isinstance(exc, KeyboardInterrupt) else 1
        finally:
            report = dict(log.common, status=status, ended_utc=utc(),
                          device_attested=False, firmware_built=False)
            if failure_details is not None:
                report['failure_details'] = failure_details
            (unit / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            log.close()
        if not args.production:
            return 0
        try:
            if not next_unit(read):
                return 0
        except (KeyboardInterrupt, EOFError):
            return 0  # Previous unit completed; EOF never means "next board".


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", nargs="?", choices=("probe", "backup", "full"))
    p.add_argument("--board", default="", help="With --chip backup: PCB marking for archive grouping (new markings allowed); otherwise registered hardware selector.")
    p.add_argument("--board-note", default="", help="Backup metadata only, never a hardware selector.")
    p.add_argument("--factory-session-dir", default="", help=argparse.SUPPRESS)
    p.add_argument("--verify-only", action="store_true", help="TLSR backup: independently verify latest matching complete first read; never replay it.")
    p.add_argument('--resume', action='store_true', help='TLSR backup: continue exited partial reads in new segments; requires SAME-board confirmation; mismatches stop.')
    p.add_argument("--chip", choices=("tlsr825x", "tlsr8359", "hs9118"), default="",
                   help="SWS probe/JEDEC-bounded backup only; not exact board/write qualification.")
    p.add_argument("--probe-baud", type=int, default=0)
    p.add_argument("--probe-activation-ms", type=int, default=0)
    p.add_argument("--probe-clock-mhz", type=int, default=-1)
    p.add_argument("--allow-unqualified-probe-baud", action="store_true")
    p.add_argument("--backup-baud", type=int, default=0)
    p.add_argument("--backup-activation-baud", type=int, default=0)
    p.add_argument("--backup-activation-ms", type=int, default=20000)
    p.add_argument("--backup-clock-mhz", type=int, default=0)
    p.add_argument("--allow-unqualified-backup-baud", action="store_true")
    p.add_argument("--list-boards", action="store_true")
    p.add_argument("--dry-run", dest="plan", action="store_true", help="Print exact commands, no tool/device access (also on Linux).")
    p.add_argument("--plan", dest="plan", action="store_true", help=argparse.SUPPRESS)
    p.add_argument("--console-output", action="store_true", help=argparse.SUPPRESS)
    waits = p.add_mutually_exclusive_group()
    waits.add_argument("--wait", action="store_true", help="Default: poll read-only contact before starting an operation.")
    waits.add_argument("--no-wait", action="store_true", help="One contact attempt; no polling.")
    p.add_argument("--interval-seconds", type=int, default=1)
    p.add_argument("--timeout-seconds", type=int, default=0, help="Contact wait deadline; 0 = unlimited. Ctrl+C stops.")
    p.add_argument("--operation-timeout-seconds", type=int, default=900)
    p.add_argument("--production", action="store_true", help="After success, Enter starts next board; q/EOF ends.")
    p.add_argument("--verbose", action="store_true", help="Show full live diagnostic/tool output; complete logs are always saved. Default: progress, result and errors only.")
    p.add_argument("--skip-stock-backup-check", action="store_true", help="Do not request, discover, read or validate an original factory backup; live identity/preservation gates remain.")
    clears = p.add_mutually_exclusive_group()
    clears.add_argument("--clear-user-settings", action="store_true", help="Full only: clear ordinary settings/password, keep pictures, Pro and Find. Requires a supporting reviewed port recipe.")
    clears.add_argument("--clear-all-user-data", action="store_true", help="Full only: clear settings/password, pictures/slots, Pro activation and device Find binding, then install basic defaults. Requires a supporting reviewed port recipe.")
    p.add_argument("--yes", action="store_true", help="Skip per-unit clear consequence confirmation only; never skip identity, capture, voltage, range or readback gates.")
    for name in ("firmware", "config", "stock-backup", "expected-mac", "expected-identity", "font-assets",
                 "source", "factory-state", "factory-device-info", "jlink-exe", "jflash-exe",
                 "snr", "tool-path", "gprogrammer-dir", "console-exe", "project-template",
                 "log-dir", "powershell", "nrf52811-repo", "da14585-repo"):
        p.add_argument(f"--{name}", default="")
    p.add_argument("--port", default="auto")
    p.add_argument("--frequency-hz", type=int)
    # Keep the pre-existing neutral value for other vendors. main() selects
    # auto only for Config Full when this option was not explicitly supplied.
    p.add_argument("--source-layout", choices=("auto", "stock", "installed", "layout-v1", "v4", "v5", "v6", "v7"), default="v5")
    p.add_argument("--allow-unqualified-layout", action="store_true")
    p.add_argument("--acknowledge-experimental-layout", action="store_true")
    p.add_argument('--embedded-font-bench', action='store_true')
    p.add_argument("--allow-destructive-repartition", action="store_true")
    return p






def main(argv: list[str] | None = None) -> int:
    p = parser()
    args = p.parse_args(argv)
    raw_options = sys.argv[1:] if argv is None else argv
    args.operation_timeout_explicit = any(item == '--operation-timeout-seconds' or item.startswith('--operation-timeout-seconds=') for item in raw_options)
    if args.list_boards:
        for board in BOARDS.values():
            print(f"{board.name:26} repo={board.repo:8} MCU={board.name.split('_')[0]} legacy={board.wire}")
        return 0
    try:
        if not args.action:
            raise ValueError("action is required; use --help or --list-boards.")
        if args.resume and args.action!='backup':
            raise ValueError('--resume is read-only TLSR backup only; never probe/Full.')
        if args.embedded_font_bench and not args.config:
            raise ValueError('--embedded-font-bench requires the configured TLSR Full pilot.')
        if args.config or args.action not in ('probe', 'backup'):
            raise ValueError('Cross-platform/Full dispatch is retired; use the MCU-owned tools.')
        board = resolve_target(args)
        validate(args, board)
        if args.resume:
            from tlsr_backup_resume import run_resume
            args.powershell = args.powershell or shutil.which('pwsh') or shutil.which('powershell') or 'powershell'
            return run_resume(args, board, sys.modules[__name__])
        if args.plan:
            planned = ((Path(args.factory_session_dir or args.verification_request["originalUnit"]) / "verify-only-TIMESTAMP")
                       if args.verify_only else session_parent(args, board) / "SESSION-TIMESTAMP") / "unit-NNNN"
            direct_tlsr_backup = args.action == "backup" and board.port == "telink/tlsr8359"
            commands = [] if direct_tlsr_backup else [build_command(args, board, "probe", planned)]
            if args.action != "probe":
                backup = args.stock_backup
                commands.append(build_command(args, board, args.action, planned, stock_backup=backup))
            print(json.dumps(dict(schema=SCHEMA, board=board.name, wait=not args.no_wait,
                                  hardware_started=False, commands=commands,
                                  verify_only=args.verify_only,
                                  verification_request=args.verification_request if args.verify_only else None,
                                  native_linux_hardware_qualified=False), indent=2))
            return 0
        shell = args.powershell or shutil.which("pwsh") or shutil.which("powershell")
        if not shell:
            raise ValueError("PowerShell not found; install PowerShell or pass --powershell <absolute executable>.")
        args.powershell = shell
        return execute(args, board)
    except (ValueError, OSError, KeyboardInterrupt, EOFError) as exc:
        # Preflight errors occur before Log exists. In a Windows public entry,
        # use the same attached-console route as Log, rather than letting the
        # parent PowerShell render native stderr after our colour cleanup.
        owned_console = open_console_output(args.console_output)
        try:
            console_print(f"{terminal_prefix('error')} {exc}", level="error",
                          stream=owned_console if owned_console is not None else sys.stderr)
        finally:
            if owned_console is not None:
                owned_console.close()
        return 130 if isinstance(exc, KeyboardInterrupt) else 2


if __name__ == "__main__":
    raise SystemExit(main())
