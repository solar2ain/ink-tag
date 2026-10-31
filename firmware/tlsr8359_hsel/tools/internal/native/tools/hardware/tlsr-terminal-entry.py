#!/usr/bin/env python3
"""TLSR Linux parameter mirror: offline review only; native SWS is not qualified."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "firmware/ports/telink/tlsr8359/tools"

# Mirrors public PowerShell declarations. Regression tests compare names,
# types, defaults and descriptions; this is not a device-operation dispatcher.
OPTIONS = {
    "probe": [
        ("Chip", "--chip", "String", "", "Required MCU selector: tlsr8359 or hs9118. Does not select a PCB; HS9118 marking alone is not silicon proof."),
        ("Port", "--port", "String", "auto", "Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter."),
        ("Baud", "--baud", "Int32", 0, "Probe UART baud; 0 selects the existing transport default. Does not select a Flash-write speed."),
        ("ActivationMs", "--activation-ms", "Int32", 0, "Probe activation window in milliseconds; 0 selects the existing transport default."),
        ("ClockMHz", "--clock-mhz", "Int32", -1, "SWS response-clock timing estimate in MHz; -1 uses the existing default, 0 selects automatic estimation. Does not change the MCU runtime clock."),
        ("RetryDetect", "--retry-detect", "SwitchParameter", False, "Retry detection only; never automatically repeat Flash programming or an unknown write."),
        ("IntervalSeconds", "--interval-seconds", "Int32", 1, "Seconds between outer detection attempts; not a data-read retry interval."),
        ("TimeoutSeconds", "--timeout-seconds", "Int32", 0, "Overall detection-wait limit in seconds; 0 means no overall wait limit."),
        ("OperationTimeoutSeconds", "--operation-timeout-seconds", "Int32", 60, "Maximum seconds for one tool operation; does not replay failed operations."),
        ("OutputDir", "--output-dir", "String", "", "Optional evidence/output directory; backup uses the maintained factory archive layout by default."),
        ("ToolPath", "--tool-path", "String", "", "Optional path to the maintained UART/SWS backend; normally discovered automatically."),
        ("Python", "--python", "String", "python", "Python executable for the maintained host tools."),
        ("DryRun", "--dry-run", "SwitchParameter", False, "Validate and print the offline plan only; no serial/reset/Flash/device access."),
    ],
    "backup": [
        ("Chip", "--chip", "String", "", "Required requested MCU marking: tlsr8359 or hs9118. This Telink tool archives under tlsr8359 and retains the requested marking as metadata."),
        ("Board", "--board", "String", "", "Required PCB silkscreen used for factory archive grouping; new markings are allowed for backup only."),
        ("BoardNote", "--board-note", "String", "", "Optional board remark, such as a production code; stored in metadata, not an extra path level."),
        ("Port", "--port", "String", "auto", "Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter."),
        ("RetryDetect", "--retry-detect", "SwitchParameter", False, "Retry detection only; never automatically repeat Flash programming or an unknown write."),
        ("VerifyOnly", "--verify-only", "SwitchParameter", False, "Independently read and compare against the latest complete first capture for this board; error if none exists."),
        ("Resume", "--resume", "SwitchParameter", False, "Resume an exited partial capture in new segments after y/N confirmation of the same board; never overwrite old fragments."),
        ("Baud", "--baud", "Int32", 921600, "Flash-read UART baud. Default 921600; 460800 is also standard; 0 selects the backend default."),
        ("ActivationBaud", "--activation-baud", "Int32", 0, "UART baud during initial activation; 0 follows the read baud."),
        ("ActivationMs", "--activation-ms", "Int32", 20000, "Maximum activation window in milliseconds; stop early when SWS validation succeeds."),
        ("ClockMHz", "--clock-mhz", "Int32", 0, "SWS CPU-clock estimate in MHz, 0..192; 0 keeps automatic selection."),
        ("AllowUnqualifiedBaud", "--allow-unqualified-baud", "SwitchParameter", False, "Allow a custom backup UART baud >=340000; standard 460800/921600 require no override. Not Full-write qualification."),
        ("IntervalSeconds", "--interval-seconds", "Int32", 1, "Seconds between outer detection attempts; not a data-read retry interval."),
        ("TimeoutSeconds", "--timeout-seconds", "Int32", 0, "Overall detection-wait limit in seconds; 0 means no overall wait limit."),
        ("OperationTimeoutSeconds", "--operation-timeout-seconds", "Int32", 7200, "Maximum seconds for one tool operation; does not replay failed operations."),
        ("OutputDir", "--output-dir", "String", "", "Optional evidence/output directory; backup uses the maintained factory archive layout by default."),
        ("ToolPath", "--tool-path", "String", "", "Optional path to the maintained UART/SWS backend; normally discovered automatically."),
        ("Python", "--python", "String", "python", "Python executable for the maintained host tools."),
        ("DryRun", "--dry-run", "SwitchParameter", False, "Validate and print the offline plan only; no serial/reset/Flash/device access."),
    ],
    "flash": [
        ("Config", "--config", "String", "", "Required compatible TLSR PCB TOML/YAML/JSON preset; applies screen and LED defaults. Missing historical Flash records warn; fresh supported Flash identification remains mandatory."),
        ("Firmware", "--firmware", "String", "", "Required app-full.zip package containing the common signed App and Recovery."),
        ("Port", "--port", "String", "auto", "Serial port, for example COM13. Full requires an exact port; probe/backup may auto-select a qualified adapter."),
        ("ExpectedMac", "--expected-mac", "String", "", "Optional exact target MAC guard; refuse a different unit. Never borrowed from another board."),
        ("StockBackup", "--stock-backup", "String", "", "Optional verified exact-unit stock rescue for archive-reference mode. Normal Full does not require a backup or compare stock firmware; it preserves freshly read identity/calibration. Run backup separately if original-firmware recovery is wanted."),
        ("Yes", "--yes", "SwitchParameter", False, "Confirm the displayed Full user-data wipe without prompting. Default asks y/N; never skips signature, identity, calibration, capacity or readback checks."),
        ("SkipReadbackVerify", "--skip-readback-verify", "SwitchParameter", False, "Skip complete post-write readback only for later flashes after wiring is stable. Keep full readback enabled for the first flash. New Recovery/Settings write verification remains. Result is NOT VERIFIED; default verifies the complete prepared Full image."),
        ("Baud", "--baud", "Int32", 0, "Write UART baud; 0 selects the registered qualified fixture value. Arbitrary Full-write overrides are refused."),
        ("ReadBaud", "--read-baud", "Int32", 0, "Read UART baud; 0 selects the registered qualified fixture value."),
        ("ActivationMs", "--activation-ms", "Int32", 0, "Activation window in milliseconds; 0 selects the registered fixture value."),
        ("ResetPulseMs", "--reset-pulse-ms", "Int32", 500, "RTS-to-board-RESET pulse duration, 50..2000 milliseconds; default 500. Requires physical RTS wiring."),
        ("ClockMHz", "--clock-mhz", "Int32", -1, "SWS CPU-clock estimate; -1 selects the registered qualified Full fixture value."),
        ("ToolPath", "--tool-path", "String", "", "Optional path to the maintained UART/SWS backend; normally discovered automatically."),
        ("Python", "--python", "String", "python", "Python executable for the maintained host tools."),
        ("RetryDetect", "--retry-detect", "SwitchParameter", False, "Retry detection only; never automatically repeat Flash programming or an unknown write."),
        ("DetectTimeoutSeconds", "--detect-timeout-seconds", "Int32", 120, "Detection window in seconds when RetryDetect is selected; 1..3600."),
        ("DryRun", "--dry-run", "SwitchParameter", False, "Validate and print the offline plan only; no serial/reset/Flash/device access."),
    ],
}


def log_helpers():
    name = "tlsr_terminal_workflow"
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name("flash-workflow.py"))
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    return sys.modules[name]


class TerminalParser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        module = log_helpers()
        module.console_print(f"{module.terminal_prefix('error')} {message}", level="error", stream=sys.stderr)
        raise SystemExit(2)


def parser():
    result = TerminalParser(description=__doc__, allow_abbrev=False)
    actions = result.add_subparsers(dest="action", required=True, parser_class=TerminalParser)
    for action, definitions in OPTIONS.items():
        command = actions.add_parser(action, description=__doc__, allow_abbrev=False,
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        if action=='flash':
            command.description='Complete flashing preserves verified original same-device Pro plus identity/calibration; clears other mutable user/diagnostic state after y/N and rebuilds Recovery state. Unrestorable old authorization is cleared and initializes Free, requiring normal activation for Pro. No version/admission/clear mode switches.'
        for _, flag, kind, default, description in definitions:
            options = dict(default=default, help=description)
            if kind == "SwitchParameter":
                options["action"] = "store_true"
            elif kind == "Int32":
                options["type"] = int
            command.add_argument(flag, **options)
        command.add_argument("--verbose", action="store_true",
                            help="Show detailed backend/diagnostic output; raw logs remain complete.")
    return result


def main(argv=None):
    command = parser()
    args = command.parse_args(argv)
    if args.action == "backup":
        if not args.board.strip():
            command.error("Backup requires --board <PCB silkscreen>; new markings are allowed. No device accessed.")
        args.board = args.board.strip()
    if not args.dry_run:
        command.error("Native Linux SWS hardware execution is not qualified. Use --dry-run for offline review, or the maintained Windows .ps1 entry. No device accessed.")
    values = vars(args)
    if args.action == "flash":
        if not args.config or not args.firmware:
            command.error("Flash requires --config and --firmware <app-full.zip>; no Chip/Board or source-layout selector.")
        if not 50 <= args.reset_pulse_ms <= 2000 or not 1 <= args.detect_timeout_seconds <= 3600:
            command.error("Reset pulse must be 50..2000 ms; detection timeout must be 1..3600 s. No device accessed.")
        from tlsr_full_flash_plan import plan
        _, review = plan(Path(args.firmware), Path(args.config), skip_readback=args.skip_readback_verify)
    else:
        if args.chip not in ("tlsr8359", "hs9118"):
            command.error("Probe/backup require --chip tlsr8359 or hs9118; this tool does not prove an HS9118 silicon variant.")
        if args.action == "probe":
            if (args.baud < 0 or (args.baud and args.baud < 340000) or
                    not 0 <= args.activation_ms <= 120000 or not -1 <= args.clock_mhz <= 192):
                command.error("Probe baud must be 0 or >=340000, activation 0..120000 ms and clock estimate -1..192 MHz; no device accessed.")
        if args.action == "backup":
            for baud in (args.baud, args.activation_baud):
                if baud < 0 or (baud != 0 and baud < 340000):
                    command.error("Backup UART baud must be 0 or >=340000. No device accessed.")
            if args.baud not in (0, 460800, 921600) and not args.allow_unqualified_baud:
                command.error("Custom backup baud requires --allow-unqualified-baud. No device accessed.")
            if args.activation_ms <= 0 or not 0 <= args.clock_mhz <= 192:
                command.error("Activation window must be positive; clock MHz must be 0..192. No device accessed.")
            if (args.verify_only or args.resume) and args.output_dir:
                command.error("VerifyOnly/Resume automatically selects the existing capture; omit --output-dir.")
        review = {"hardware_started": False}
    # Only mirror the maintained public Windows interface. Never execute it.
    windows = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
               str(PORT / (args.action + ".ps1"))]
    for name, flag, kind, _, _ in OPTIONS[args.action]:
        value = values[flag[2:].replace("-", "_")]
        if kind == "SwitchParameter":
            if value:
                windows.append("-" + name)
        elif value != "":
            windows.extend(("-" + name, str(value)))
    if args.verbose:
        windows.append("-Verbose")
    review.update(native_linux_hardware_qualified=False, hardware_started=False,
                  plan_kind="offline public Windows argument mirror; no tool/device access; paths require target-host translation",
                  commands=[windows], argument_values=values)
    print(json.dumps(review, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        module = log_helpers()
        module.console_print(f"{module.terminal_prefix('error')} {exc}", level="error", stream=sys.stderr)
        raise SystemExit(2) from None
