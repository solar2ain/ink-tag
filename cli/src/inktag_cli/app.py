from __future__ import annotations

import asyncio
import hashlib
import getpass
import json
import math
import re
import sys
import time
import zlib
from contextlib import nullcontext, redirect_stdout
from dataclasses import dataclass, replace
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Annotated, Any, Callable, Coroutine, TypeVar

import typer
from bleak.exc import BleakError
from rich.console import Console
from rich.table import Table

from . import __version__
from .generated_release_v1 import FIND_PUBLIC_VERSION
from .board_ids import board_ids_equivalent, canonical_board_id
from .clock_accuracy import ClockSample, summarize_clock_accuracy
from .clock_status import clock_status
from .image_commands import image_app
from .template_commands import template_app
from .host_commands import host_app
from .image_encoding import decode_stored_image
from .image_processing import save_png
from .led_patterns import (
    LED_COLORS,
    LedStep,
    build_led_effect,
    normalize_led_color,
    parse_led_pattern,
)
from .models import DeviceSnapshot, ImageStoreInfo, ImageStoreItem, ScreenConfig
from .local_service import (
    DEFAULT_LOCAL_ORIGIN,
    local_service_request,
    read_webui_log,
    restart_webui_background,
    serve_webui,
    start_webui_background,
    stop_webui_background,
    webui_background_status,
)
from .ota import driver_command, inspect_package, run_update
from .ota_password import ota_password_prompt_available
from .owner_auth import prove_with_pro_action_hash
from .protocol import (
    OTA_SERVICE_UUIDS,
    SCREEN_SCHEMA_VERSION,
    Notification,
    encode_bluetooth_name,
    encode_device_label,
    encode_v2_time,
    screen_config_from_profile,
    screen_profile_name,
    screen_profile_names,
)
from .generated_commands_v2 import CapabilityV2
from .gui_font_roles import MAX_PACKAGE_BYTES, validate_font_package, build_font_config
from .device_link import build_device_link
from .password_session import PasswordCredentialStore, set_credential_cache_enabled
from .device_password import PASSWORD_WARNING
from .local_service import local_data_directory
from .transport import InkTagClient, InkTagError, SettingsResetUnconfirmedError, scan_device_status, scan_devices


_NFC_ACTIONS = {
    "disabled": "disabled",
    "next": "nextPage",
    "slot": "showSlot",
    "device-info": "showDeviceInfo",
    "calendar": "showCalendar",
    "clock": "showClock",
    "slideshow": "toggleSlideshow",
    "stop": "stop",
}


app = typer.Typer(
    name="inktag",
    help="Control InkTag e-paper devices over Bluetooth Low Energy.",
    no_args_is_help=True,
    add_completion=True,
)
device_app = typer.Typer(help="Discover devices and inspect identity/capabilities.", no_args_is_help=True)
led_app = typer.Typer(help="Inspect status-light policy and control qualified LEDs.", no_args_is_help=True)
display_app = typer.Typer(help="Run built-in display actions.", no_args_is_help=True)
screen_app = typer.Typer(help="Inspect and update the persisted screen configuration.", no_args_is_help=True)
time_app = typer.Typer(help="Read or synchronize device time.", no_args_is_help=True)
time_compensation_app = typer.Typer(help="Read/set volatile UTC rate in ppm on supported devices; never synchronizes time.", no_args_is_help=True)
slot_app = typer.Typer(help="Manage images stored in device Flash.", no_args_is_help=True)
slideshow_app = typer.Typer(help="Start or stop the device slideshow.", no_args_is_help=True)
diagnostic_app = typer.Typer(help="Read runtime, temperature, health and clock information without time synchronization.", no_args_is_help=True)
nfc_app = typer.Typer(help="Read NFC status and configure supported page-switching actions.", no_args_is_help=True)
ota_app = typer.Typer(help="Inspect and install a qualified OTA package.", no_args_is_help=True)
recovery_app = typer.Typer(help="Enter, inspect or safely leave fixed Recovery.", no_args_is_help=True)
activation_app = typer.Typer(help="Inspect or permanently activate a provisioned device.", no_args_is_help=True)
find_app = typer.Typer(help="Inspect and configure qualified or explicit experimental Find support.", no_args_is_help=True)
find_identity_app = typer.Typer(help="Manage private Find identities in the local WebUI service.", no_args_is_help=True)
webui_app = typer.Typer(help="Run the InkTag WebUI and its local services.", no_args_is_help=True)
gui_assets_app = typer.Typer(help="Build or install fonts: numbers, Latin, Chinese and large clock/calendar digits.", no_args_is_help=True)
password_app = typer.Typer(help="Configure the optional Pro device password. Secrets are read from hidden prompts or stdin.", no_args_is_help=True)
pins_app = typer.Typer(help="Read or override complete display/RGB/TXD/RXD routing on qualified firmware.", no_args_is_help=True)
io_app = typer.Typer(help="Configure qualified button/buzzer GPIO routing; does not implement new hardware actions.", no_args_is_help=True)

app.add_typer(device_app, name="device")
device_app.add_typer(gui_assets_app, name="gui-assets")
device_app.add_typer(password_app, name="password")
app.add_typer(led_app, name="led")
app.add_typer(pins_app, name="pins")
app.add_typer(io_app, name="io")
app.add_typer(display_app, name="display")
app.add_typer(screen_app, name="screen")
app.add_typer(time_app, name="time")
time_app.add_typer(time_compensation_app, name="compensation")
app.add_typer(image_app, name="image")
app.add_typer(template_app, name="template")
app.add_typer(host_app, name="host")
app.add_typer(slot_app, name="slot")
app.add_typer(slideshow_app, name="slideshow")
app.add_typer(diagnostic_app, name="diagnostic")
app.add_typer(nfc_app, name="nfc")
app.add_typer(ota_app, name="ota")
app.add_typer(recovery_app, name="recovery")
app.add_typer(activation_app, name="activation")
app.add_typer(find_app, name="find")
app.add_typer(webui_app, name="webui")
find_app.add_typer(find_identity_app, name="identity")

console = Console()
error_console = Console(stderr=True)
T = TypeVar("T")


async def _owner_proof_provider(
    client: InkTagClient, *, server: str, pro_code_file: Path | None,
):
    """Keep owner secrets out of BLE logs and command-line arguments."""
    if not client.supports_capability(CapabilityV2.OWNER_AUTHORIZATION):
        return None
    activation = client.snapshot.activation
    if not activation.activation_id:
        activation = await client.activation_status()
    try:
        device_id = bytes.fromhex(activation.activation_id)
    except ValueError as error:
        raise InkTagError("Device did not report a valid immutable owner identity") from error
    if len(device_id) != 8 or not any(device_id):
        raise InkTagError("Device did not report a valid immutable owner identity")

    fingerprint = ""
    if (
        client.supports_capability(CapabilityV2.FIND)
        or client.supports_capability(CapabilityV2.EXPERIMENTAL_FIND)
    ):
        try:
            owner = await client.find_owner_status()
            if owner.get("version") == FIND_PUBLIC_VERSION and owner.get("identityPresent"):
                fingerprint = str(owner.get("fingerprintHex", "")).lower()
        except InkTagError as error:
            if pro_code_file is None:
                raise InkTagError(
                    "Could not read Find owner identity; reconnect or explicitly use --pro-code-file"
                ) from error
    if not fingerprint and pro_code_file is None:
        raise InkTagError(
            "Owner proof requires a readable local Find identity or --pro-code-file"
        )

    code: str | None = None

    async def build(nonce: bytes, digest: bytes) -> bytes:
        nonlocal code
        if fingerprint:
            try:
                reply = await asyncio.to_thread(
                    local_service_request, server,
                    "/api/v1/find/identity/owner-action/prove", {
                        "fingerprint": fingerprint,
                        "deviceIdHex": device_id.hex(),
                        "nonceHex": nonce.hex(),
                        "actionHashHex": digest.hex(),
                    },
                )
                proof = bytes.fromhex(str(reply["proofHex"]))
                if len(proof) == 50 and proof[:2] == b"\x01\x02" and proof[2:34] == digest:
                    return proof
                raise InkTagError("Local Find service returned an invalid owner proof")
            except (ValueError, KeyError) as error:
                if pro_code_file is None:
                    raise InkTagError(
                        "Local Find owner proof unavailable; start the canonical "
                        "WebUI service or explicitly use --pro-code-file"
                    ) from error
        if code is None:
            if pro_code_file is not None:
                code = pro_code_file.read_text(encoding="utf-8").strip()
            else:
                raise InkTagError(
                    "Owner proof requires local Find identity or --pro-code-file"
                )
        return prove_with_pro_action_hash(code, device_id, nonce, digest)

    return build


@dataclass(frozen=True)
class CliContext:
    device: str | None
    address: str | None
    scan_timeout: float
    connect_timeout: float
    json_output: bool
    verbose: bool
    preserve_device_time: bool = False
    password_stdin: bool = False
    password_data_dir: Path | None = None
    client_type: type[InkTagClient] = InkTagClient

    def client(self, *, sink=None, auto_sync_time: bool | None = None, authenticate: bool = True,
               allow_password_fault_diagnostics: bool = False,
               expected_device_id: str | None = None, allow_password_prompt: bool = True) -> InkTagClient:
        sync_on_connect = not self.preserve_device_time if auto_sync_time is None else auto_sync_time
        if self.preserve_device_time:
            sync_on_connect = False
        return self.client_type(
            name=self.device,
            address=self.address,
            scan_timeout=self.scan_timeout,
            connect_timeout=self.connect_timeout,
            notification_sink=sink or (self._verbose_notification if self.verbose else None),
            phase_sink=self._verbose_phase if self.verbose else None,
            crash_warning_sink=self._crash_warning,
            auto_sync_time=sync_on_connect,
            require_password_authentication=authenticate,
            allow_password_fault_diagnostics=allow_password_fault_diagnostics,
            password_fault_sink=lambda message: error_console.print(f"[yellow]Warning:[/yellow] {message}"),
            password_force_prompt=self.password_stdin and authenticate,
            password_store=PasswordCredentialStore(local_data_directory(self.password_data_dir)) if authenticate else None,
            password_prompt=(lambda: _read_password("Device password: ", self.password_stdin))
                if authenticate and allow_password_prompt and (self.password_stdin or sys.stdin.isatty()) else None,
            **({"expected_device_id": expected_device_id} if expected_device_id is not None else {}),
        )

    def _verbose_notification(self, notification: Notification) -> None:
        _print_notification(notification, json_lines=False, stderr=True)

    def _crash_warning(self, warning: dict[str, Any]) -> None:
        if self.json_output:
            typer.echo(json.dumps({"type": "warning", "warning": warning}, ensure_ascii=False), err=True)
            return
        detail = (
            f"latest={warning.get('reason', 'unknown')}, "
            f"operation={warning.get('operation', 'unknown')}, "
            f"stage={warning.get('stage') or '--'}"
        )
        error_console.print(
            f"[yellow]Warning:[/yellow] device recorded {warning['newCrashes']} new crash(es) "
            f"since this client last acknowledged it ({detail})."
        )

    @staticmethod
    def _verbose_phase(message: str) -> None:
        error_console.print(f"[dim]BLE[/dim] {message}")


@app.callback(invoke_without_command=True)
def root(
    context: typer.Context,
    device: Annotated[str | None, typer.Option("--device", "-d", help="BLE name or unique name prefix.")] = None,
    address: Annotated[str | None, typer.Option("--address", "-a", help="BLE address or macOS CoreBluetooth UUID.")] = None,
    scan_timeout: Annotated[float, typer.Option(min=1.0, max=60.0, help="BLE scan timeout in seconds.")] = 8.0,
    connect_timeout: Annotated[float, typer.Option(min=2.0, max=60.0, help="BLE connection timeout in seconds.")] = 15.0,
    json_output: Annotated[bool, typer.Option("--json", help="Emit machine-readable JSON.")] = False,
    verbose: Annotated[bool, typer.Option("--verbose", "-v", help="Print raw device notifications.")] = False,
    preserve_device_time: Annotated[
        bool,
        typer.Option(
            "--no-time-sync",
            help="Do not synchronize the device clock on connection; use for drift and soak evidence.",
        ),
    ] = False,
    legacy_no_sync: Annotated[
        bool,
        typer.Option("--no-sync", hidden=True),
    ] = False,
    version: Annotated[bool, typer.Option("--version", help="Show the CLI version and exit.", is_eager=True)] = False,
    password_stdin: Annotated[bool, typer.Option("--password-stdin", help="Read the device password from stdin; this does not disable credential caching. Never put secrets in arguments.")] = False,
    password_data_dir: Annotated[Path | None, typer.Option("--password-data-dir", help="Absolute persistent directory for local device credentials; defaults to the user InkTag data directory.")] = None,
    no_credential_cache: Annotated[bool, typer.Option("--no-credential-cache", help="Do not read or write saved device control credentials, including in OTA subprocesses. Existing cache files are left untouched.")] = False,
) -> None:
    set_credential_cache_enabled(not no_credential_cache)
    if version:
        typer.echo(f"inktag-cli {__version__}")
        raise typer.Exit()
    context.obj = CliContext(
        device,
        address,
        scan_timeout,
        connect_timeout,
        json_output,
        verbose,
        preserve_device_time or legacy_no_sync,
        password_stdin,
        password_data_dir,
    )


def _read_password(label: str, use_stdin: bool) -> str:
    if use_stdin:
        value = sys.stdin.readline()
        if not value:
            raise InkTagError("No password provided on stdin")
        return value.rstrip("\r\n")
    if not sys.stdin.isatty():
        raise InkTagError("Use a terminal prompt or --password-stdin; do not put passwords in arguments")
    return getpass.getpass(label)


@password_app.command("status", help="Read password support and enabled state without synchronizing time; never reveals the password.")
def password_status(context: typer.Context) -> None:
    options = _context(context)
    async def operation():
        async with options.client(auto_sync_time=False, authenticate=False) as client:
            return await client.read_password_status()
    _emit(options, _run(options, operation()))


@password_app.command("set")
def password_set(context: typer.Context) -> None:
    """Enable/change. If enabled: fresh old password, new password, confirmation."""
    options = _context(context)
    async def operation():
        async with options.client(auto_sync_time=False, authenticate=False) as client:
            status = await client.read_password_status()
            if status is None:
                raise InkTagError("Firmware does not advertise device password support")
            error_console.print(PASSWORD_WARNING)
            old = _read_password("Original password: ", options.password_stdin) if status["enabled"] else None
            new = _read_password("New password: ", options.password_stdin)
            confirmation = _read_password("Repeat new password: ", options.password_stdin)
            if new != confirmation:
                raise InkTagError("New passwords do not match")
            client.password_store = PasswordCredentialStore(local_data_directory(options.password_data_dir))
            return await client.configure_password(new, old_password=old)
    _emit(options, _run(options, operation()))


@password_app.command("disable", help="Disable the device password after fresh verification of the original password; preserves images and other settings.")
def password_disable(context: typer.Context) -> None:
    options = _context(context)
    async def operation():
        async with options.client(auto_sync_time=False, authenticate=False) as client:
            old = _read_password("Original password: ", options.password_stdin)
            client.password_store = PasswordCredentialStore(local_data_directory(options.password_data_dir))
            return await client.configure_password(None, old_password=old)
    _emit(options, _run(options, operation()))


@password_app.command("show-device-page")
def password_show(context: typer.Context) -> None:
    """Forgot password: show the QR locally, never read it back over BLE."""
    options = _context(context)
    async def operation():
        async with options.client(auto_sync_time=False, authenticate=False) as client:
            return await client.show_password_device_info()
    _emit(options, _run(options, operation()))


def _context(context: typer.Context) -> CliContext:
    assert isinstance(context.obj, CliContext)
    return context.obj


def _run(options: CliContext, operation: Coroutine[Any, Any, T]) -> T:
    try:
        return asyncio.run(operation)
    except KeyboardInterrupt:
        _emit_error(options, "Cancelled")
        raise typer.Exit(130) from None
    except (BleakError, InkTagError, ValueError, OSError) as exc:
        _emit_error(options, str(exc) or type(exc).__name__)
        raise typer.Exit(1) from None


def _emit_error(options: CliContext, message: str) -> None:
    if options.json_output:
        typer.echo(json.dumps({"ok": False, "error": message}, ensure_ascii=False), err=True)
    else:
        error_console.print(f"[red]Error:[/red] {message}")


def _console_safe(value: str) -> str:
    encoding = sys.stdout.encoding or "utf-8"
    return value.encode(encoding, errors="replace").decode(encoding)


def _emit(options: CliContext, value: Any, *, title: str | None = None) -> None:
    if options.json_output:
        typer.echo(json.dumps(value, ensure_ascii=False, indent=2))
        return
    if isinstance(value, dict):
        table = Table(title=title, show_header=False, box=None)
        table.add_column("Field", style="cyan", no_wrap=True)
        table.add_column("Value", overflow="fold")
        for key, item in value.items():
            if item in (None, "", [], {}):
                rendered = "--"
            elif isinstance(item, (dict, list)):
                rendered = json.dumps(item, ensure_ascii=False)
            else:
                rendered = str(item)
            table.add_row(_human_label(key), rendered)
        console.print(table)
    elif isinstance(value, list):
        for item in value:
            console.print(item)
    else:
        console.print(value)


def _human_label(value: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", " ", value).replace("_", " ").title()


def _print_notification(notification: Notification, *, json_lines: bool, stderr: bool = False) -> None:
    target = error_console if stderr else console
    if notification.screen:
        value: Any = {"type": "screen", "screen": notification.screen.to_dict()}
        human = (
            f"screen {notification.screen.width}x{notification.screen.height} "
            f"driver=0x{notification.screen.driver:02X}"
        )
    else:
        value = {"type": "notification", "message": notification.text}
        human = notification.text or notification.raw.hex()
    if json_lines:
        typer.echo(json.dumps(value, ensure_ascii=False))
    else:
        target.print(f"[dim]RX[/dim] {human}")


@device_app.command("scan")
def device_scan(
    context: typer.Context,
    include_all: Annotated[bool, typer.Option("--all", help="Include non-InkTag BLE devices.")] = False,
) -> None:
    options = _context(context)
    devices = _run(options, scan_devices(options.scan_timeout, include_all))
    if options.json_output:
        _emit(options, [device.to_dict() for device in devices])
        return
    table = Table(title=f"BLE devices ({len(devices)})")
    table.add_column("Name")
    table.add_column("Address / UUID")
    table.add_column("RSSI", justify="right")
    for device in devices:
        table.add_row(_console_safe(device.name), device.address, "--" if device.rssi is None else str(device.rssi))
    console.print(table)


def _advertised_device_status(device) -> dict[str, Any]:
    services = {service.lower() for service in device.services}
    ota_services = sorted(
        platform
        for platform, service in OTA_SERVICE_UUIDS.items()
        if service.lower() in services
    )
    ota_name = device.name.upper().endswith("_OTA")
    recovery = ota_name or bool(ota_services)
    evidence: list[str] = []
    if ota_name:
        evidence.append("advertising name ends in _OTA")
    evidence.extend(f"{platform} OTA service" for platform in ota_services)
    if not evidence:
        evidence.append("ordinary InkTag advertising")
    return {
        "name": device.name,
        "address": device.address,
        "mode": "recovery" if recovery else "application",
        "recovery": recovery,
        "evidence": evidence,
        "rssi": device.rssi,
        "services": list(device.services),
    }


@device_app.command(
    "status",
    help="Quickly identify whether a reachable device advertises as application or Recovery.",
)
def device_status(context: typer.Context) -> None:
    """Quickly identify application or Recovery advertising."""
    options = _context(context)
    device = _run(
        options,
        scan_device_status(
            options.scan_timeout,
            name=options.device,
            address=options.address,
        ),
    )
    _emit(options, _advertised_device_status(device), title="InkTag BLE status")


@device_app.command("info")
def device_info(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> DeviceSnapshot:
        async with options.client(allow_password_fault_diagnostics=True) as client:
            return client.snapshot

    snapshot = _run(options, operation())
    _emit(options, snapshot.to_dict(), title="InkTag device")


@device_app.command("health")
def device_health(
    context: typer.Context,
    record: Annotated[int, typer.Option(min=0, max=3, help="Crash record index; zero is newest.")] = 0,
    duration: Annotated[
        int | None,
        typer.Option(min=1, max=3600, help="Sample health on one BLE connection for this many seconds (maximum 3600)."),
    ] = None,
    interval: Annotated[
        float,
        typer.Option(min=1.0, max=3600.0, help="Seconds to wait between health reads during --duration."),
    ] = 5.0,
) -> None:
    options = _context(context)
    if not math.isfinite(interval):
        raise typer.BadParameter("Health interval must be finite", param_hint="--interval")

    async def operation():
        async with options.client(allow_password_fault_diagnostics=True) as client:
            if duration is None:
                return await client.health(record)
            samples = await _sample_device_health(client, record, duration, interval)
            return {
                "ok": True,
                "singleConnection": True,
                "healthReadsOnly": True,
                "timeSyncRequested": not options.preserve_device_time,
                "requestedDurationSeconds": duration,
                "intervalSeconds": interval,
                "sampleCount": len(samples),
                "samples": samples,
            }

    _emit(options, _run(options, operation()), title="InkTag health")


async def _sample_device_health(
    client: InkTagClient, record: int, duration: int, interval: float,
) -> list[dict[str, Any]]:
    """Read immediately and at the end, without reconnecting or catch-up bursts.

    Polling is bounded to one hour and at most 3601 samples by the CLI options.
    Raw counters stay intact: a reset, wrap or saturation must not be silently
    turned into a positive sleep delta. Counters are not current measurements.
    """
    samples: list[dict[str, Any]] = []
    started = time.monotonic()
    deadline = started + duration
    while True:
        before = time.monotonic()
        host_utc = datetime.now(timezone.utc).isoformat()
        health = await client.health(record)
        after = time.monotonic()
        samples.append({
            "hostUtc": host_utc,
            "elapsedSeconds": round(before - started, 3),
            "roundTripMilliseconds": round((after - before) * 1000.0, 3),
            "health": health,
        })
        if after >= deadline:
            break
        await asyncio.sleep(min(interval, deadline - after))
    return samples


@activation_app.command("status")
def activation_status(context: typer.Context) -> None:
    options = _context(context)

    async def operation():
        async with options.client() as client:
            return await client.activation_status()

    status = _run(options, operation())
    _emit(options, status.to_dict(), title="InkTag activation")


@activation_app.command("request")
def activation_request(
    context: typer.Context,
    new_challenge: Annotated[
        bool,
        typer.Option("--new", help="Invalidate the pending request and generate a new challenge."),
    ] = False,
) -> None:
    options = _context(context)

    async def operation():
        async with options.client() as client:
            return await client.activation_request(force=new_challenge)

    request = _run(options, operation())
    _emit(options, request.to_dict(), title="InkTag activation request")


@activation_app.command("install")
def activation_install(
    context: typer.Context,
    code: Annotated[str | None, typer.Argument(help="128-character device-bound IKAC1 activation code.")] = None,
    code_file: Annotated[Path | None, typer.Option("--code-file", help="Read the IKAC1 code from a local file instead of exposing it in the command line.")] = None,
    restore: Annotated[bool, typer.Option("--restore", help="Restore a saved signed code on the same device only when Free authorization is empty; never replaces an existing code.")] = False,
) -> None:
    options = _context(context)
    if (code is None) == (code_file is None):
        raise typer.BadParameter("Provide exactly one activation code or --code-file")
    if code_file is not None:
        try:
            code = code_file.read_text(encoding="ascii").strip()
        except (OSError, UnicodeError) as error:
            raise typer.BadParameter("Could not read the activation code file") from error
    assert code is not None

    async def operation():
        async with options.client() as client:
            return await client.activate(code, restore=True) if restore else await client.activate(code)

    status = _run(options, operation())
    _emit(options, status.to_dict(), title="InkTag activation")


@device_app.command("watch")
def device_watch(
    context: typer.Context,
    seconds: Annotated[float | None, typer.Option(min=0.1, help="Stop after this many seconds.")] = None,
) -> None:
    options = _context(context)

    async def operation() -> None:
        sink = lambda item: _print_notification(item, json_lines=options.json_output)
        async with options.client(sink=sink):
            if seconds is None:
                while True:
                    await asyncio.sleep(3600)
            else:
                await asyncio.sleep(seconds)

    _run(options, operation())


async def _sync_managed_device_links(
    client: InkTagClient,
    bluetooth_name: str,
    *,
    sync_qr: bool,
    sync_nfc: bool,
) -> dict[str, Any]:
    value = build_device_link(bluetooth_name)
    results: dict[str, Any] = {
        "value": value,
        "qr": {"status": "not-requested"},
        "nfc": {"status": "not-requested"},
    }
    failures: list[str] = []
    if sync_qr:
        try:
            results["qr"] = await client.write_device_info_qr(value)
        except Exception as error:
            failures.append(f"device QR: {error}")
    if sync_nfc:
        try:
            results["nfc"] = await client.write_nfc_web_link(value)
            if results["nfc"].get("status") != "written":
                failures.append("NFC NDEF: this device does not support managed link writes")
        except Exception as error:
            failures.append(f"NFC NDEF: {error}")
    if failures:
        retry = "inktag device sync-links"
        if sync_qr:
            retry += " --sync-managed-qr"
        raise InkTagError(
            "Bluetooth name is active, but managed links did not fully sync ("
            + "; ".join(failures)
            + f"). Retry with '{retry}'."
        )
    return results


@device_app.command("rename")
def device_rename(
    context: typer.Context,
    name: Annotated[str, typer.Argument(help="New IKT_ Bluetooth broadcast name: ASCII letters, digits, underscore or hyphen (legacy InkTag_ is accepted).")],
    sync_links: Annotated[
        bool,
        typer.Option(
            "--sync-links/--no-link-sync",
            help="Synchronize the managed NFC WebUI link after renaming.",
        ),
    ] = True,
    sync_managed_qr: Annotated[
        bool,
        typer.Option(
            "--sync-managed-qr",
            help="Also replace the device-information QR with the official managed link.",
        ),
    ] = False,
) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        encode_bluetooth_name(name)
        async with options.client() as client:
            reply = await client.rename(name)
            result: dict[str, Any] = {
                "name": client.snapshot.device_name or name,
                "deviceReply": reply,
            }
            if sync_links or sync_managed_qr:
                result["links"] = await _sync_managed_device_links(
                    client,
                    client.snapshot.device_name or name,
                    sync_qr=sync_managed_qr,
                    sync_nfc=sync_links,
                )
            else:
                result["links"] = {"status": "not-requested"}
            return result

    result = _run(options, operation())
    _emit(options, {"ok": True, **result})


@device_app.command("sync-links")
def device_sync_links(
    context: typer.Context,
    sync_managed_qr: Annotated[
        bool,
        typer.Option(
            "--sync-managed-qr",
            help="Replace the device-information QR with the official managed link.",
        ),
    ] = False,
) -> None:
    """Synchronize managed links from the reported BLE name."""
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            await client.refresh_snapshot()
            name = client.snapshot.device_name or client.snapshot.name
            if not name:
                raise InkTagError("Device did not report a Bluetooth name")
            return {
                "name": name,
                "links": await _sync_managed_device_links(
                    client,
                    name,
                    sync_qr=sync_managed_qr,
                    sync_nfc=True,
                ),
            }

    _emit(options, {"ok": True, **_run(options, operation())}, title="Managed device links")


@device_app.command("label")
def device_label(
    context: typer.Context,
    label: Annotated[str, typer.Argument(help="Persistent printable ASCII device label (English), up to 24 bytes; pass an empty string to clear it.")],
) -> None:
    options = _context(context)

    async def operation() -> str:
        encode_device_label(label)
        async with options.client() as client:
            return await client.set_device_label(label)

    result = _run(options, operation())
    _emit(options, {"ok": True, "label": label.strip(), "deviceReply": result})


@device_app.command("qr")
def device_qr(
    context: typer.Context,
    value: Annotated[str, typer.Argument(help="URL or text for the single device-information QR.")],
    show: Annotated[
        bool,
        typer.Option("--show/--no-show", help="Refresh the device-information page after commit."),
    ] = True,
) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            result = await client.write_device_info_qr(value)
            result["deviceReply"] = (
                await client.run_display("device-info") if show else "not-requested"
            )
            return result

    _emit(options, {"ok": True, **_run(options, operation())}, title="Device information QR")


@gui_assets_app.command("info")
def gui_assets_info(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> dict[str, int | None]:
        async with options.client() as client:
            return await client.get_gui_asset_info()

    _emit(options, _run(options, operation()), title="GUI asset package")


@gui_assets_app.command("install")
def gui_assets_install(
    context: typer.Context,
    package: Annotated[
        Path,
        typer.Argument(exists=True, file_okay=True, dir_okay=False, readable=True,
                       help="Complete .itfa font package; device schema support is checked before writing."),
    ],
    yes: Annotated[bool, typer.Option('--yes', help='Confirm single-region replacement may discard previous custom fonts on failure.')] = False,
) -> None:
    options = _context(context)
    package_bytes = package.read_bytes()
    local = validate_font_package(package_bytes, maximum_size=MAX_PACKAGE_BYTES)

    async def operation() -> dict[str, Any]:
        def progress(done: int, total: int) -> None:
            if options.verbose:
                typer.echo(f"GUI assets: {done}/{total} bytes", err=True)

        async with options.client() as client:
            device = await client.install_gui_asset_package(package_bytes, progress, confirm_single_user=yes)
            return {"package": local, "device": device}

    _emit(options, {"ok": True, **_run(options, operation())}, title="GUI asset package installed")


@gui_assets_app.command('build')
def gui_assets_build(context: typer.Context,
    config: Annotated[Path, typer.Argument(exists=True, dir_okay=False, help='Four-role font JSON configuration.')],
    output: Annotated[Path, typer.Option('--output', '-o', help='New .itfa output; existing files are refused.')],
    generation: Annotated[int, typer.Option('--generation', min=0, max=0xffffffff)] = 1,
) -> None:
    options = _context(context)
    package = build_font_config(config, generation)
    metadata = validate_font_package(package)
    with output.open('xb') as destination:
        destination.write(package)
    _emit(options, {'ok': True, 'output': str(output), 'package': metadata}, title='Font package built offline')


@led_app.command("status")
def led_status(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            return await client.get_indicator_config()

    _emit(options, _run(options, operation()), title="Status-light configuration")


@pins_app.command("status")
def pins_status(context: typer.Context) -> None:
    options = _context(context)
    async def operation():
        from .pin_config import describe_pin_report
        async with options.client() as client:
            report = await client.get_pin_config()
            snapshot = client.snapshot
            return {**report, 'display': describe_pin_report(report, snapshot.board_id, snapshot.screen)}
    result = _run(options, operation())
    if options.json_output:
        _emit(options, result)
    else:
        view = result['display']
        level = lambda high: '--' if high is None else 'High active' if high else 'Low active'
        _emit(options, {'Source': view['source'], 'Default valid': result['defaultValid'],
            **view['pins'], 'BUSY actual': level(view['busyActiveHigh']),
            'BUSY controller recommendation': level(view['controllerBusyActiveHigh']),
            'Recommendation scope': view['recommendationScope'],
            'Polarity differs': view['busyMismatch']}, title="Current effective routing")


@pins_app.command("set")
def pins_set(context: typer.Context,
             config: Annotated[Path, typer.Option("--config", exists=True, dir_okay=False, help="JSON with ten display/LED pins and activeLow. No TXD/RXD fields.")],
             yes: Annotated[bool, typer.Option("--yes", help="Confirm checked wiring and protected-pin conflicts.")] = False) -> None:
    from .pin_config import user_pin_configuration
    try:
        values = user_pin_configuration(json.loads(config.read_text(encoding="utf-8")))
    except (ValueError, OSError) as error:
        raise typer.BadParameter(str(error), param_hint="--config") from error
    if not yes and not typer.confirm("Incorrect routing can prevent display operation. Apply the checked complete routing?"):
        raise typer.Exit(1)
    options = _context(context)
    async def operation():
        async with options.client() as client:
            return await client.set_pin_config(values)
    _emit(options, _run(options, operation()), title="Device-confirmed routing")


@pins_app.command("defaults")
def pins_defaults(context: typer.Context,
                  yes: Annotated[bool, typer.Option("--yes")] = False) -> None:
    if not yes and not typer.confirm("Remove the pin override and restore the device's validated default routing?"):
        raise typer.Exit(1)
    options = _context(context)
    async def operation():
        async with options.client() as client:
            return await client.set_pin_config(None)
    _emit(options, _run(options, operation()), title="Device-confirmed default routing")


@io_app.command("status")
def io_status(context: typer.Context) -> None:
    options = _context(context)
    async def operation():
        async with options.client() as client:
            return await client.get_aux_configuration()
    _emit(options, _run(options, operation()), title="Button and buzzer IO routing")


@io_app.command("set")
def io_set(context: typer.Context,
           config: Annotated[Path, typer.Option("--config", exists=True, dir_okay=False, help="IO JSON: version, three fixed buttons (gpio/activeLow/pull), and buzzer settings. GPIO 255 disables a button.")],
           yes: Annotated[bool, typer.Option("--yes")] = False) -> None:
    from .aux_io import encode_aux_configuration
    try:
        values = json.loads(config.read_text(encoding="utf-8"))
        encode_aux_configuration(values)
    except (ValueError, OSError) as error:
        raise typer.BadParameter(str(error), param_hint="--config") from error
    if not yes and not typer.confirm("Apply the checked button/buzzer routing?"):
        raise typer.Exit(1)
    options = _context(context)
    async def operation():
        async with options.client() as client:
            return await client.set_aux_configuration(values)
    _emit(options, _run(options, operation()), title="Device-confirmed button/buzzer IO routing")


@led_app.command("policy")
def led_policy(
    context: typer.Context,
    preset: Annotated[
        str,
        typer.Argument(help="Free preset: all, critical or off."),
    ],
) -> None:
    options = _context(context)
    normalized = preset.strip().lower()
    if normalized not in {"all", "critical", "off"}:
        raise typer.BadParameter("preset must be all, critical or off")

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            return await client.set_indicator_policy(normalized)

    _emit(options, _run(options, operation()), title="Status-light configuration")


@device_app.command("verify")
def device_verify(
    context: typer.Context,
    expect_board: Annotated[str | None, typer.Option(help="Require this exact board ID.")] = None,
    expect_profile: Annotated[str | None, typer.Option(help="Require this exact screen profile.")] = None,
    require_ota: Annotated[bool, typer.Option(help="Require fixed-Recovery capability or an OTA service.")] = False,
) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            snapshot = client.snapshot
            if expect_board and not board_ids_equivalent(snapshot.board_id, expect_board):
                raise InkTagError(
                    f"Board mismatch: expected {canonical_board_id(expect_board)}, "
                    f"reported {snapshot.board_id or 'none'}"
                )
            if expect_profile and snapshot.profile != expect_profile:
                raise InkTagError(
                    f"Profile mismatch: expected {expect_profile}, reported {snapshot.profile or 'none'}"
                )
            ota_services = [
                name for name, uuid in OTA_SERVICE_UUIDS.items()
                if uuid in snapshot.services
            ]
            recovery_capable = client.supports_capability(
                CapabilityV2.OTA_RECOVERY
            )
            if require_ota and not ota_services and not recovery_capable:
                raise InkTagError("Device does not expose an OTA service or fixed-Recovery capability")
            return {
                "ok": True,
                "boardId": snapshot.board_id,
                "profile": snapshot.profile,
                "otaServices": ota_services,
                "otaRecovery": recovery_capable,
                "firmwareVersion": snapshot.firmware_version,
            }

    _emit(options, _run(options, operation()), title="InkTag verification")


@device_app.command("reset")
def device_reset(
    context: typer.Context,
    yes: Annotated[
        bool,
        typer.Option("--yes", "-y", help="Confirm the device reboot without prompting."),
    ] = False,
) -> None:
    options = _context(context)
    _require_confirmation(options, yes, "Reboot the selected device?")

    async def operation() -> str:
        async with options.client() as client:
            return await client.reset()

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "reset", "deviceReply": reply})


@device_app.command("sleep", help="Enter timed deep sleep on an explicitly enabled experimental build.")
def device_sleep(
    context: typer.Context,
    seconds: Annotated[
        int,
        typer.Option(
            "--seconds",
            min=5,
            max=3600,
            help="Experimental timer wake-up delay in seconds (5-3600).",
        ),
    ],
    yes: Annotated[
        bool,
        typer.Option("--yes", "-y", help="Confirm timed deep sleep without prompting."),
    ] = False,
) -> None:
    options = _context(context)
    _require_confirmation(
        options,
        yes,
        f"Use experimental timed sleep, disconnect Bluetooth, and wake after {seconds} seconds?",
    )

    async def operation() -> str:
        async with options.client() as client:
            return await client.deep_sleep(seconds)

    reply = _run(options, operation())
    _emit(options, {
        "ok": True,
        "action": "sleep",
        "wakeAfterSeconds": seconds,
        "deviceReply": reply,
    })


@device_app.command("reset-settings")
def device_reset_settings(
    context: typer.Context,
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm settings and password clearing.")] = False,
) -> None:
    options = _context(context)
    _require_confirmation(options, yes,
        "Clear ordinary settings and the device password? Images, Pro/Find, "
        "board defaults and calibration are preserved. An enabled password requires the original password.")

    async def operation() -> str:
        # No opportunistic sync or stdin consumption for V before the W prompt.
        async with options.client(authenticate=False, auto_sync_time=False) as client:
            client.require_lifecycle_v2()
            status = await client.read_password_status()
            had_password_support = status is not None
            expected_id = client._password_id() if had_password_support else None
            old = _read_password("Original password: ", options.password_stdin) if status and status["enabled"] else None
            address = client.snapshot.address or client.snapshot.ble_address
            unconfirmed = False
            try:
                await client.reset_settings(old_password=old)
            except SettingsResetUnconfirmedError:
                unconfirmed = True
            # One read-only reconnect. A lost result never replays SYSTEM_ACTION.
            if not address:
                raise InkTagError("Settings reset result is unconfirmed; reconnect to verify, do not retry blindly")
            await client.disconnect()
            client.address, client.name = address, None
            await asyncio.sleep(1)
            try:
                await client.connect()
                client.require_lifecycle_v2()
                if had_password_support:
                    if not client.supports_capability(CapabilityV2.DEVICE_PASSWORD) or client._password_id() != expected_id:
                        raise InkTagError("Settings reset identity/password capability readback mismatch")
                    confirmed = await client.read_password_status()
                    if confirmed is None or confirmed["enabled"]:
                        raise InkTagError("Password clearing has not been verified")
            except Exception as error:
                raise InkTagError("Settings reset result is unconfirmed; reconnect read-only, do not retry blindly") from error
            if unconfirmed:
                raise InkTagError("Settings reset reply was not received; read-only state recovered, "
                                  "but the action result remains unconfirmed. No action was replayed; do not retry blindly")
            if had_password_support:
                # This connection deliberately has no password_store: attaching
                # it before fresh W would restore automatic V/stdin handling.
                # Open the canonical cache only after ACK + same-device OFF.
                PasswordCredentialStore(local_data_directory(options.password_data_dir)).put(expected_id, None)
            return "settings-reset:verified"

    _emit(options, {"ok": True, "action": "reset-settings", "deviceReply": _run(options, operation())})


@device_app.command("clear-user-data")
def device_clear_user_data(
    context: typer.Context,
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm stored-image removal.")] = False,
    expected_device_id: Annotated[str | None, typer.Option(
        "--expected-device-id", help="Require this immutable activation ID (16 hex digits) on the same connection before any device mutation."
    )] = None,
) -> None:
    options = _context(context)
    _require_confirmation(options, yes,
        "Delete images and their metadata only? Settings, password, QR/NFC, "
        "Pro/Find, board defaults and calibration are preserved.")

    async def operation() -> int:
        async with options.client(**({"expected_device_id": expected_device_id}
                                     if expected_device_id is not None else {})) as client:
            return await client.clear_user_data()

    _emit(options, {"ok": True, "action": "clear-user-data", "removed": _run(options, operation())})


async def _display_operation(
    options: CliContext,
    action: str,
    *,
    no_sync: bool = False,
) -> str:
    sync_time = not (no_sync or options.preserve_device_time) and action in {"calendar", "clock"}
    auto_sync_time = False if action in {"calendar", "clock"} else None
    async with options.client(auto_sync_time=auto_sync_time) as client:
        return await client.run_display(action, sync_time=sync_time)


@display_app.command("clear")
def display_clear(context: typer.Context) -> None:
    options = _context(context)
    reply = _run(options, _display_operation(options, "clear"))
    _emit(options, {"ok": True, "action": "clear", "deviceReply": reply})


@display_app.command("calendar")
def display_calendar(
    context: typer.Context,
    no_sync: Annotated[
        bool,
        typer.Option(
            "--no-time-sync",
            help="Do not synchronize the device clock before rendering.",
        ),
    ] = False,
    legacy_no_sync: Annotated[bool, typer.Option("--no-sync", hidden=True)] = False,
) -> None:
    options = _context(context)
    reply = _run(
        options,
        _display_operation(options, "calendar", no_sync=no_sync or legacy_no_sync),
    )
    _emit(options, {"ok": True, "action": "calendar", "deviceReply": reply})


@display_app.command("clock")
def display_clock(
    context: typer.Context,
    no_sync: Annotated[
        bool,
        typer.Option(
            "--no-time-sync",
            help="Do not synchronize the device clock before rendering.",
        ),
    ] = False,
    legacy_no_sync: Annotated[bool, typer.Option("--no-sync", hidden=True)] = False,
) -> None:
    options = _context(context)
    reply = _run(
        options,
        _display_operation(options, "clock", no_sync=no_sync or legacy_no_sync),
    )
    _emit(options, {"ok": True, "action": "clock", "deviceReply": reply})


@display_app.command("device-info")
def display_device_info(context: typer.Context) -> None:
    options = _context(context)
    reply = _run(options, _display_operation(options, "device-info"))
    _emit(options, {"ok": True, "action": "device-info", "deviceReply": reply})


@display_app.command("stop")
def display_stop(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.stop()

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "stop", "deviceReply": reply})


@screen_app.command("show", help="Read the current screen profile without changing settings.")
def screen_show(context: typer.Context) -> None:
    options = _context(context)

    async def operation():
        async with options.client() as client:
            return await client.get_screen()

    screen = _run(options, operation())
    _emit(options, screen.to_dict(), title="Screen configuration")


@screen_app.command("profiles", help="List the built-in screen profiles offline.")
def screen_profiles(context: typer.Context) -> None:
    options = _context(context)
    profiles = [
        {
            "name": name,
            **screen_config_from_profile(name).to_dict(),
        }
        for name in screen_profile_names()
    ]
    if options.json_output:
        _emit(options, profiles)
        return
    table = Table(title=f"Screen profiles ({len(profiles)})")
    table.add_column("Name", no_wrap=True)
    table.add_column("Driver")
    table.add_column("Color", justify="right")
    table.add_column("Size")
    table.add_column("Transform", justify="right")
    for profile in profiles:
        table.add_row(
            str(profile["name"]),
            str(profile["driverHex"]),
            str(profile["color"]),
            f'{profile["width"]}x{profile["height"]}',
            str(profile["transform"]),
        )
    console.print(table)


@screen_app.command("brand", help="Read or change the Pro-authorized InkTag mark visibility.")
def screen_brand(
    context: typer.Context,
    state: Annotated[
        str | None,
        typer.Argument(help="Pro brand state: show or hide; omit to read the device setting."),
    ] = None,
) -> None:
    options = _context(context)
    normalized = state.lower() if state else None
    if normalized not in (None, "show", "hide"):
        raise typer.BadParameter("state must be show or hide")

    async def operation():
        async with options.client() as client:
            if normalized is None:
                return await client.get_display_settings()
            return await client.set_brand_visible(normalized == "show")

    result = _run(options, operation())
    _emit(options, result, title="Device brand setting")


@screen_app.command("calendar", help="Read or update calendar and clock display preferences.")
def screen_calendar(
    context: typer.Context,
    week_start: Annotated[
        int | None,
        typer.Option(min=0, max=1, help="First weekday: 0=Sunday, 1=Monday."),
    ] = None,
    countdown_name: Annotated[
        str | None,
        typer.Option(help="Shared calendar/clock countdown label (up to 4 characters)."),
    ] = None,
    countdown_date: Annotated[
        str | None,
        typer.Option(help="Shared calendar/clock target date in YYYY-MM-DD format."),
    ] = None,
    clear_countdown: Annotated[
        bool,
        typer.Option("--clear-countdown", help="Clear the shared target label and date."),
    ] = False,
) -> None:
    """Read or save the shared calendar/clock configuration."""
    options = _context(context)
    changing = week_start is not None or countdown_name is not None or countdown_date is not None or clear_countdown
    if clear_countdown and (countdown_name is not None or countdown_date is not None):
        raise typer.BadParameter("--clear-countdown cannot be combined with countdown values")
    if (countdown_name is None) != (countdown_date is None):
        raise typer.BadParameter("--countdown-name and --countdown-date must be supplied together")

    async def operation():
        async with options.client(auto_sync_time=not changing) as client:
            current = await client.get_display_settings()
            if not changing:
                return current
            saved = await client.set_display_configuration(
                int(current["weekStart"]) if week_start is None else week_start,
                "" if clear_countdown else (countdown_name or str(current["countdownName"])),
                "" if clear_countdown else (countdown_date or str(current["countdownDate"])),
            )
            await client.run_display("calendar", sync_time=not options.preserve_device_time)
            return saved

    result = _run(options, operation())
    _emit(options, result, title="Calendar and clock configuration")


def _parse_integer(value: str | None, label: str) -> int | None:
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError as exc:
        raise ValueError(f"{label} must be a decimal or 0x-prefixed integer") from exc


def _screen_config_from_options(
    profile: str | None,
    driver: str | None,
    recipe: str | None,
    color: int | None,
    transform: int | None,
    native_ui_scale: int | None,
    width: int | None,
    height: int | None,
    visible_x: int | None,
    visible_y: int | None,
    visible_width: int | None,
    visible_height: int | None,
) -> ScreenConfig:
    base = screen_config_from_profile(profile) if profile else None
    parsed_driver = _parse_integer(driver, "driver")
    parsed_recipe = _parse_integer(recipe, "recipe")
    selected_driver = parsed_driver if parsed_driver is not None else (base.driver if base else None)
    selected_recipe = parsed_recipe if parsed_recipe is not None else (base.recipe if base else 0)
    selected_color = color if color is not None else (base.color if base else None)
    selected_width = width if width is not None else (base.width if base else None)
    selected_height = height if height is not None else (base.height if base else None)
    if selected_driver is None or selected_color is None or selected_width is None or selected_height is None:
        raise ValueError("provide a profile name or --driver, --color, --width and --height")
    return ScreenConfig(
        schema_version=SCREEN_SCHEMA_VERSION,
        driver=selected_driver,
        recipe=selected_recipe,
        color=selected_color,
        transform=transform if transform is not None else (base.transform if base else 0),
        native_ui_scale=(
            native_ui_scale if native_ui_scale is not None else (base.native_ui_scale if base else 0)
        ),
        width=selected_width,
        height=selected_height,
        visible_x=visible_x if visible_x is not None else (base.visible_x if base else 0),
        visible_y=visible_y if visible_y is not None else (base.visible_y if base else 0),
        visible_width=(
            visible_width
            if visible_width is not None
            else (base.visible_width if base and width is None else selected_width)
        ),
        visible_height=(
            visible_height
            if visible_height is not None
            else (base.visible_height if base and height is None else selected_height)
        ),
    )


@screen_app.command("set", help="Save the selected screen profile to the connected device.")
def screen_set(
    context: typer.Context,
    profile: Annotated[
        str | None,
        typer.Argument(help="Preset name from 'screen profiles'; omit for advanced settings."),
    ] = None,
    driver: Annotated[str | None, typer.Option(help="Controller ID, decimal or 0x-prefixed.")] = None,
    recipe: Annotated[str | None, typer.Option(help="Advanced panel recipe ID, decimal or 0x-prefixed.")] = None,
    color: Annotated[int | None, typer.Option(min=1, max=11, help="Color mode ID.")] = None,
    transform: Annotated[int | None, typer.Option(min=0, max=15, help="Rotation/mirror bit field.")] = None,
    native_ui_scale: Annotated[
        int | None,
        typer.Option(min=0, max=5, help="Native page text scale: 0 auto, 1 100%, 2 150%, 3 200%, 4 125%, 5 175%."),
    ] = None,
    width: Annotated[int | None, typer.Option(min=1, help="Controller width in pixels.")] = None,
    height: Annotated[int | None, typer.Option(min=1, help="Controller height in pixels.")] = None,
    visible_x: Annotated[int | None, typer.Option(min=0, help="Visible-area X offset.")] = None,
    visible_y: Annotated[int | None, typer.Option(min=0, help="Visible-area Y offset.")] = None,
    visible_width: Annotated[int | None, typer.Option(min=1, help="Visible-area width.")] = None,
    visible_height: Annotated[int | None, typer.Option(min=1, help="Visible-area height.")] = None,
) -> None:
    options = _context(context)

    async def operation() -> ScreenConfig:
        requested = _screen_config_from_options(
            profile,
            driver,
            recipe,
            color,
            transform,
            native_ui_scale,
            width,
            height,
            visible_x,
            visible_y,
            visible_width,
            visible_height,
        )
        async with options.client() as client:
            return await client.set_screen(requested)

    screen = _run(options, operation())
    result = screen.to_dict()
    result["profile"] = screen_profile_name(screen) or "custom"
    _emit(options, result, title="Saved screen configuration")


@time_app.command("sync")
def time_sync(
    context: typer.Context,
    network_time: Annotated[bool, typer.Option("--network-time", help="After connection/authentication, cross-check UTC using two HTTPS Date sources; does not change the computer clock.")] = False,
    offset_seconds: Annotated[float, typer.Option("--offset-seconds", help="Explicit reference-minus-host correction (-86400..86400 seconds), sampled immediately before SET_TIME.")] = 0.0,
    compensation_ppm: Annotated[int | None, typer.Option(
        "--compensation-ppm", "--ppm", min=-4000, max=4000,
        help="Set/read back volatile rate first, then synchronize UTC once; omit to preserve current rate.",
    )] = None,
) -> None:
    options = _context(context)
    from .time_reference import validate_offset
    try:
        validate_offset(offset_seconds)
        if network_time and offset_seconds:
            raise ValueError("--network-time and --offset-seconds cannot be combined")
    except ValueError as error:
        raise typer.BadParameter(str(error)) from error

    async def operation() -> dict[str, Any]:
        # This command owns the single requested clock mutation. Disabling the
        # connection-time convenience sync avoids two Flash-backed SET_TIME
        # transactions before the explicit operation is issued.
        async with options.client(auto_sync_time=False) as client:
            sync_options: dict[str, Any] = {}
            if compensation_ppm is not None:
                sync_options["compensation_ppm"] = compensation_ppm
            if network_time:
                sync_options["network"] = True
            if offset_seconds:
                sync_options["offset_seconds"] = offset_seconds
            rate = await client.sync_time(**sync_options)
            result: dict[str, Any] = {"ok": True, "action": "time-sync"}
            if network_time or offset_seconds:
                result["timeReference"] = client.time_sync_reference
            if rate is not None:
                result["compensation"] = rate
            return result

    _emit(options, _run(options, operation()))


@time_app.command("set", help="Set an explicit device date/time using ordinary SET_TIME, without RUN_TEST. Leaves that time set; use time sync to restore host time.")
def time_set(
    context: typer.Context,
    at: Annotated[str, typer.Option("--at", help="ISO date/time in 2000–2099. No offset means this computer's local timezone.")],
    view: Annotated[str, typer.Option(help="Optional completed redraw: none, calendar or clock.")] = "none",
    timeout: Annotated[float, typer.Option(min=5.0, max=180.0, help="Seconds to wait for the optional display refresh.")] = 100.0,
) -> None:
    options = _context(context)
    normalized_view = view.strip().lower()
    if normalized_view not in {"none", "calendar", "clock"}:
        raise typer.BadParameter("--view must be none, calendar or clock")
    if not math.isfinite(timeout):
        raise typer.BadParameter("--timeout must be finite")
    try:
        when = datetime.fromisoformat(at.strip().replace("Z", "+00:00"))
        if when.tzinfo is None:
            when = when.astimezone()
        encode_v2_time(when, 0)  # Validate before opening a device connection.
    except (ValueError, OverflowError, OSError) as error:
        raise typer.BadParameter(f"Invalid --at date/time: {error}") from error
    display_mode = {"calendar": 1, "clock": 2}.get(normalized_view)

    async def operation() -> dict[str, int]:
        # Own the single explicit write; keep ordinary identity and AUTH gates.
        async with options.client(auto_sync_time=False) as client:
            return await client.set_time(when, display_mode, timeout=timeout)

    result = _run(options, operation())
    _emit(options, {
        "ok": True, "action": "time-set", "path": "ordinary-set-time",
        "view": normalized_view, "readbackVerified": True,
        "displayCompleted": display_mode is not None, "restored": False,
        **result,
    })


@time_compensation_app.command("show", help="Read the volatile UTC rate without synchronizing time.")
def time_compensation_show(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> dict[str, int | str]:
        async with options.client(auto_sync_time=False) as client:
            return await client.get_clock_rate()

    _emit(options, _run(options, operation()), title="UTC rate compensation (ppm)")


@time_compensation_app.command("set", help="Set and read back the volatile UTC rate without synchronizing time. Reset/OTA restores the board default.")
def time_compensation_set(
    context: typer.Context,
    ppm: Annotated[int, typer.Option("--ppm", min=-4000, max=4000,
        help="Positive speeds UTC up; negative slows it down; 0 disables. Reset/OTA restores board default.")],
) -> None:
    options = _context(context)

    async def operation() -> dict[str, int | str]:
        async with options.client(auto_sync_time=False) as client:
            return await client.set_clock_rate(ppm)

    _emit(options, {"ok": True, "readbackVerified": True,
                    **_run(options, operation())}, title="UTC rate compensation (ppm)")


@time_app.command("show")
def time_show(
    context: typer.Context,
    no_sync: Annotated[
        bool,
        typer.Option(
            "--no-time-sync",
            help="Read without synchronizing first; required for drift or soak evidence.",
        ),
    ] = False,
    legacy_no_sync: Annotated[bool, typer.Option("--no-sync", hidden=True)] = False,
) -> None:
    options = _context(context)
    result, host_utc_before, host_utc_after, monotonic_before_ns, monotonic_after_ns = _run(
        options,
        _sample_device_clock(
            options, sync_on_connect=not (no_sync or legacy_no_sync)
        ),
    )
    midpoint_timestamp = (host_utc_before.timestamp() + host_utc_after.timestamp()) / 2
    device_zone = timezone(timedelta(minutes=result["timezoneMinutes"]))
    _emit(options, {
        "ok": True,
        "readOnly": no_sync or options.preserve_device_time,
        "preserveDeviceTimeRequested": no_sync or options.preserve_device_time,
        "noSyncRequested": no_sync or options.preserve_device_time,
        "deviceUtcTimestamp": result["utcTimestamp"],
        "deviceLocalTime": datetime.fromtimestamp(result["utcTimestamp"], device_zone).isoformat(),
        "timezoneMinutes": result["timezoneMinutes"],
        "weekStart": result["weekStart"],
        "hostUtcBefore": host_utc_before.isoformat(),
        "hostUtcAfter": host_utc_after.isoformat(),
        "roundTripMilliseconds": round((monotonic_after_ns - monotonic_before_ns) / 1_000_000, 3),
        "offsetFromHostSeconds": round(result["utcTimestamp"] - midpoint_timestamp, 3),
    }, title="InkTag clock sample")


@time_app.command("accuracy")
def time_accuracy(
    context: typer.Context,
    duration: Annotated[
        str,
        typer.Option(help="Read-only measurement duration such as 10m or 1h."),
    ] = "10m",
    interval: Annotated[
        float,
        typer.Option(
            min=0.1,
            max=0.5,
            help="Seconds between samples (0.1–0.5); brackets RTC second edges, not sparse clock snapshots.",
        ),
    ] = 0.23,
) -> None:
    options = _context(context)
    seconds = _parse_clock_measurement_duration(duration)
    if not math.isfinite(interval):
        raise typer.BadParameter("Clock accuracy interval must be finite.", param_hint="--interval")

    async def operation() -> dict[str, Any]:
        samples: list[ClockSample] = []
        deadline: float | None = None
        next_progress = 60.0
        async with options.client(auto_sync_time=False) as client:
            next_sample = time.monotonic()
            while True:
                before_utc = time.time()
                before_monotonic = time.monotonic()
                device = await client.read_time()
                after_monotonic = time.monotonic()
                after_utc = time.time()
                midpoint_monotonic = (before_monotonic + after_monotonic) / 2.0
                samples.append(
                    ClockSample(
                        monotonic_seconds=midpoint_monotonic,
                        host_utc_seconds=(before_utc + after_utc) / 2.0,
                        device_utc_seconds=device["utcTimestamp"],
                        round_trip_milliseconds=(
                            after_monotonic - before_monotonic
                        ) * 1000.0,
                    )
                )
                if deadline is None:
                    deadline = midpoint_monotonic + seconds
                elapsed = midpoint_monotonic - samples[0].monotonic_seconds
                if elapsed >= next_progress:
                    typer.echo(
                        f"Clock sampling: {int(elapsed)}/{seconds}s, "
                        f"{len(samples)} samples",
                        err=True,
                    )
                    next_progress += 60.0
                if midpoint_monotonic >= deadline:
                    break
                next_sample += interval
                delay = next_sample - time.monotonic()
                if delay > 0:
                    await asyncio.sleep(delay)
        return {
            "ok": True,
            "readOnly": True,
            "clockSourceUnderTest": "device configured low-frequency clock",
            **summarize_clock_accuracy(samples),
        }

    _emit(options, _run(options, operation()), title="InkTag clock accuracy")


@find_app.command("status")
def find_status(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            return await client.find_status()

    _emit(options, _run(options, operation()), title="Find status")


@find_app.command("enable")
def find_enable(
    context: typer.Context,
    allow_experimental: Annotated[
        bool,
        typer.Option("--allow-experimental", help="Acknowledge that this board exposes only EXPERIMENTAL_FIND."),
    ] = False,
    server: Annotated[
        str, typer.Option(help="Canonical local WebUI origin retaining the private Find identity.")
    ] = DEFAULT_LOCAL_ORIGIN,
) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            experimental = (
                client.supports_capability(CapabilityV2.EXPERIMENTAL_FIND)
                and not client.supports_capability(CapabilityV2.FIND)
            )
            if experimental and not allow_experimental:
                raise InkTagError("Experimental Find requires --allow-experimental")
            status = await client.find_status()
            if not status.get("capabilities", {}).get("publicOnlyP224"):
                raise InkTagError("Device does not expose current public-only Find; update its firmware before enabling")
            provisioned = False
            fingerprint = ""
            if not status["config"]["identityPresent"]:
                if not client.snapshot.activation.active:
                    raise InkTagError("Activate Pro before enabling Find")
                device_name = (
                    client.snapshot.device_label or client.snapshot.device_name or
                    client.snapshot.name or "InkTag"
                )
                activation_id = client.snapshot.activation.activation_id.strip().upper()
                if not re.fullmatch(r"(?!0{16}$)[0-9A-F]{16}", activation_id):
                    raise InkTagError("Find v2 registration requires a stable activation identity")
                device_id = f"activation:{activation_id}"
                bluetooth_name = client.snapshot.device_name or client.snapshot.name
                owner = await client.find_owner_status()
                if owner.get("version") != FIND_PUBLIC_VERSION or owner["identityPresent"] or not owner["timeValid"]:
                    raise InkTagError("Find v2 first registration requires empty identity and valid UTC")
                nonce = await client.begin_find_owner_rotation()
                transaction_id = ""
                commit_started = False
                try:
                    prepared = local_service_request(server, "/api/v1/find/identity/public-only/prepare", {
                        "nonceHex": nonce.hex(),
                        "deviceId": device_id,
                        "deviceName": device_name,
                        "bluetoothName": bluetooth_name,
                        "findNote": device_name,
                    })
                    transaction_id = str(prepared["transactionId"])
                    if prepared.get("deviceId") != device_id or prepared.get("bluetoothName") != bluetooth_name:
                        raise InkTagError("Local Find service returned a different device identity")
                    await client.stage_public_only_find_owner(
                        bytes.fromhex(str(prepared["payloadHex"])), nonce
                    )
                    commit_started = True
                    proof = await client.commit_find_owner_rotation()
                    if proof.get("version") != FIND_PUBLIC_VERSION or str(proof.get("fingerprintHex", "")).upper() != prepared["fingerprintHex"]:
                        raise InkTagError("Find v2 device proof fingerprint does not match local owner")
                    local_service_request(server, "/api/v1/find/identity/public-only/finalize", {
                        "transactionId": transaction_id,
                        "slot": int(proof["slot"]),
                        "publicKeyHex": str(proof["publicKeyHex"]),
                        "fingerprintHex": str(proof["fingerprintHex"]),
                    })
                    fingerprint = str(proof["fingerprintHex"]).lower()
                    provisioned = True
                except BaseException:
                    # Cancellation also needs pre-commit cleanup. Once COMMIT
                    # was sent, keep the local secret for status-based recovery.
                    if not commit_started:
                        try:
                            await client.abort_find_owner_rotation()
                        except Exception:
                            pass
                        if transaction_id:
                            try:
                                local_service_request(server, "/api/v1/find/identity/public-only/abort", {
                                    "transactionId": transaction_id,
                                })
                            except ValueError:
                                pass
                    raise
            config = await client.configure_find(
                True, allow_experimental=allow_experimental
            )
            return {
                "ok": True, "experimental": experimental,
                "provisioned": provisioned,
                "keyFingerprint": fingerprint or None,
                "config": config,
            }

    _emit(options, _run(options, operation()), title="Find enabled")


@find_app.command("disable")
def find_disable(
    context: typer.Context,
    server: Annotated[
        str, typer.Option(help="Canonical local WebUI origin for Find owner proof.")
    ] = DEFAULT_LOCAL_ORIGIN,
    pro_code_file: Annotated[
        Path | None,
        typer.Option(help="File containing the installed Pro code when local Find proof is unavailable."),
    ] = None,
) -> None:
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            current = await client.find_status()
            proof = None
            if current["config"]["enabled"]:
                proof = await _owner_proof_provider(
                    client, server=server, pro_code_file=pro_code_file
                )
            return {"ok": True, "config": await client.configure_find(
                False, owner_proof=proof
            )}

    _emit(options, _run(options, operation()), title="Find disabled")


@find_app.command("query")
def find_query(
    context: typer.Context,
    server: Annotated[
        str, typer.Option(help="Running local WebUI origin.")
    ] = DEFAULT_LOCAL_ORIGIN,
) -> None:
    """Derive the configured time window and query Apple reports locally."""
    options = _context(context)
    try:
        payload = local_service_request(server, "/api/v1/find/query", {})
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, payload, title="Find query")


@find_identity_app.command("list")
def find_identity_list(
    context: typer.Context,
    server: Annotated[str, typer.Option(help="Running local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
) -> None:
    options = _context(context)
    try:
        payload = local_service_request(server, "/api/v1/find/status")
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, payload.get("identities", []), title="Local Find identities")


@find_identity_app.command("recover")
def find_identity_recover(
    context: typer.Context,
    server: Annotated[str, typer.Option(help="Running canonical local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
) -> None:
    """Verify and finish a pending public-only Find owner transaction."""
    options = _context(context)

    async def operation() -> dict[str, Any]:
        async with options.client() as client:
            owner = await client.find_owner_status()
            if owner.get("version") != FIND_PUBLIC_VERSION or not owner["identityPresent"] or not owner["timeValid"]:
                raise InkTagError("Connected device has no valid public-only Find identity to recover")
            activation_id = client.snapshot.activation.activation_id.strip().upper()
            device_id = (
                f"activation:{activation_id}" if activation_id and activation_id != "0" * 16
                else f"ble:{client.snapshot.ble_address or client.snapshot.address}"
            )
            return local_service_request(server, "/api/v1/find/identity/public-only/recover", {
                "deviceId": device_id,
                "fingerprintHex": str(owner["fingerprintHex"]),
                "slot": int(owner["slot"]),
                "publicKeyHex": str(owner["publicKeyHex"]),
            })

    _emit(options, _run(options, operation()), title="Find owner recovered")


@find_identity_app.command("import-local-v2")
def find_identity_import_local_v2(
    context: typer.Context,
    source_directory: Annotated[Path, typer.Argument(help="Absolute source Find directory containing its protected identity database.")],
    fingerprint: Annotated[str, typer.Option(help="Expected 32-hex v2 fingerprint read from the exact device.")],
    device_id: Annotated[str, typer.Option(help="Expected stable activation: device ID.")],
    bluetooth_name: Annotated[str, typer.Option(help="Expected exact Bluetooth advertising name.")],
    server: Annotated[str, typer.Option(help="Explicit destination loopback WebUI origin.")],
) -> None:
    options = _context(context)
    try:
        if not source_directory.is_absolute() or not source_directory.is_dir():
            raise ValueError("source directory must be an existing absolute path")
        if not re.fullmatch(r"[0-9a-fA-F]{32}", fingerprint):
            raise ValueError("expected v2 fingerprint must contain exactly 32 hexadecimal characters")
        if not re.fullmatch(r"activation:[0-9a-fA-F]{16}", device_id):
            raise ValueError("expected device ID must be activation: followed by 16 hexadecimal characters")
        if not bluetooth_name.strip():
            raise ValueError("expected Bluetooth name must not be empty")
        payload = local_service_request(server, "/api/v1/find/identity/import-local-v2", {
            "sourceDirectory": str(source_directory.resolve()), "fingerprint": fingerprint.lower(),
            "deviceId": device_id, "bluetoothName": bluetooth_name,
        })
        if (payload.get("importedKeyFingerprint") != fingerprint.lower() or
                str(payload.get("deviceId", "")).lower() != device_id.lower() or
                payload.get("bluetoothName") != bluetooth_name):
            raise ValueError("import response does not match the expected device identity")
    except (OSError, ValueError) as error:
        _emit_error(options, str(error))
        raise typer.Exit(1) from None
    _emit(options, {"ok": True, **{key: payload.get(key) for key in (
        "importedKeyFingerprint", "deviceId", "deviceName", "bluetoothName",
        "alreadyPresent", "selectedKeyFingerprint", "identityCount",
    )}}, title="Find v2 identity imported without replacement")


@find_identity_app.command("select")
def find_identity_select(
    context: typer.Context,
    fingerprint: Annotated[str, typer.Argument(help="Find identity fingerprint (v2: 32 hexadecimal characters).")],
    server: Annotated[str, typer.Option(help="Running local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
) -> None:
    options = _context(context)
    try:
        payload = local_service_request(server, "/api/v1/find/identity/select", {
            "fingerprint": fingerprint.strip().lower(),
        })
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, {
        "ok": True,
        "selectedKeyFingerprint": payload.get("selectedKeyFingerprint"),
        "address": payload.get("address"),
    }, title="Find identity selected")


@find_identity_app.command("update")
def find_identity_update(
    context: typer.Context,
    fingerprint: Annotated[str, typer.Argument(help="Find identity fingerprint (v2: 32 hexadecimal characters).")],
    server: Annotated[str, typer.Option(help="Running local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
    device_id: Annotated[str | None, typer.Option(help="Stable activation:/ble:/browser: device identity.")] = None,
    device_name: Annotated[str | None, typer.Option(help="User-visible InkTag device name.")] = None,
    bluetooth_name: Annotated[str | None, typer.Option(help="Current Bluetooth advertising name.")] = None,
    note: Annotated[str | None, typer.Option(help="Local Find identity note.")] = None,
) -> None:
    options = _context(context)
    fields = {
        "deviceId": device_id.strip() if device_id is not None else None,
        "deviceName": device_name.strip() if device_name is not None else None,
        "bluetoothName": bluetooth_name.strip() if bluetooth_name is not None else None,
        "findNote": note.strip() if note is not None else None,
    }
    body = {"fingerprint": fingerprint.strip().lower(), **{
        name: value for name, value in fields.items() if value is not None
    }}
    if len(body) == 1:
        _emit_error(options, "provide at least one metadata option to update")
        raise typer.Exit(1)
    try:
        payload = local_service_request(server, "/api/v1/find/identity/update", body)
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, {
        "ok": True,
        "keyFingerprint": fingerprint.strip().lower(),
        "identityCount": len(payload.get("identities", [])),
    }, title="Find identity updated")


@find_identity_app.command("delete")
def find_identity_delete(
    context: typer.Context,
    fingerprint: Annotated[str, typer.Argument(help="Find identity fingerprint (v2: 32 hexadecimal characters).")],
    server: Annotated[str, typer.Option(help="Running local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm local identity and track deletion without prompting.")] = False,
) -> None:
    options = _context(context)
    _require_confirmation(options, yes, "Delete this local Find identity and its saved track?")
    try:
        payload = local_service_request(server, "/api/v1/find/identity/delete", {
            "fingerprint": fingerprint.strip().lower(),
            "confirm": True,
        })
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, {
        "ok": True,
        "deletedKeyFingerprint": fingerprint.strip().lower(),
        "selectedKeyFingerprint": payload.get("selectedKeyFingerprint"),
        "identityCount": len(payload.get("identities", [])),
    }, title="Find identity deleted")


@find_identity_app.command("rotate")
def find_identity_rotate(
    context: typer.Context,
    fingerprint: Annotated[
        str | None,
        typer.Option(
            "--fingerprint",
            help="Advanced override for the current local identity; normally matched from the connected device.",
        ),
    ] = None,
    server: Annotated[str, typer.Option(help="Running local WebUI origin.")] = DEFAULT_LOCAL_ORIGIN,
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm owner-authenticated identity replacement without prompting.")] = False,
) -> None:
    """Replace P/K on the connected device; the private scalar stays local."""
    options = _context(context)
    normalized = fingerprint.strip().lower() if fingerprint is not None else ""
    if normalized and not re.fullmatch(r"[0-9a-f]{32}", normalized):
        _emit_error(options, "Find v2 fingerprint must contain 32 hexadecimal characters")
        raise typer.Exit(1)
    _require_confirmation(
        options, yes,
        "Replace the connected device's Find identity? Old local history is retained.",
    )

    async def operation() -> dict[str, Any]:
        rotation_id = ""
        async with options.client() as client:
            owner_status = await client.find_owner_status()
            if owner_status.get("version") != FIND_PUBLIC_VERSION:
                raise InkTagError("Device does not expose current public-only Find; update its firmware before replacing identity")
            capabilities = (await client.find_status())["capabilities"]
            if not capabilities.get("publicOnlyOwnerReplacement"):
                raise InkTagError("Device firmware does not support Find v2 owner replacement")
            current = str(owner_status.get("fingerprintHex", "")).lower()
            if not owner_status["identityPresent"] or not re.fullmatch(r"[0-9a-f]{32}", current):
                raise InkTagError("Device has no Find v2 owner identity to replace")
            if normalized and normalized != current:
                raise InkTagError("Requested fingerprint does not match the connected device")
            if not owner_status["timeValid"]:
                raise InkTagError("Device UTC is invalid; synchronize time first")
            activation_id = client.snapshot.activation.activation_id.strip().upper()
            if not re.fullmatch(r"(?!0{16}$)[0-9A-F]{16}", activation_id):
                raise InkTagError("Find v2 owner replacement requires a stable activation identity")
            nonce = await client.begin_find_owner_rotation()
            commit_started = False
            try:
                prepared = local_service_request(server, "/api/v1/find/identity/public-only/replace/prepare", {
                    "fingerprintHex": current,
                    "nonceHex": nonce.hex(),
                    "deviceId": f"activation:{activation_id}",
                })
                rotation_id = str(prepared["transactionId"])
                if (str(prepared.get("currentFingerprintHex", "")).lower() != current or
                        str(prepared.get("deviceId", "")).lower() != f"activation:{activation_id}".lower()):
                    raise InkTagError("Local Find service returned a different device owner")
                await client.stage_public_only_find_owner_replacement(
                    bytes.fromhex(str(prepared["payloadHex"])), nonce, bytes.fromhex(current)
                )
                commit_started = True
                proof = await client.commit_find_owner_rotation()
                if (proof.get("version") != FIND_PUBLIC_VERSION or
                        str(proof.get("fingerprintHex", "")).upper() != prepared["fingerprintHex"]):
                    raise InkTagError("Find v2 device proof does not match the prepared owner")
                result = local_service_request(server, "/api/v1/find/identity/public-only/finalize", {
                    "transactionId": rotation_id,
                    "slot": int(proof["slot"]),
                    "publicKeyHex": str(proof["publicKeyHex"]),
                    "fingerprintHex": str(proof["fingerprintHex"]),
                })
                return {
                    "ok": True,
                    "rotatedFromKeyFingerprint": current,
                    "rotatedToKeyFingerprint": result["verifiedKeyFingerprint"],
                    "slot": proof["slot"],
                }
            except BaseException:
                # A missing/invalid reply cannot prove COMMIT did not persist.
                # Keep the pending local owner and recover against device status.
                if not commit_started:
                    try:
                        await client.abort_find_owner_rotation()
                    except Exception:
                        pass
                    if rotation_id:
                        try:
                            local_service_request(server, "/api/v1/find/identity/public-only/abort", {
                                "transactionId": rotation_id,
                            })
                        except Exception:
                            pass
                raise

    try:
        result = _run(options, operation())
    except (ValueError, InkTagError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, result, title="Find identity rotated")


@webui_app.command("start")
def webui_start(
    context: typer.Context,
    port: Annotated[int, typer.Option(min=1, max=65535, help="Local WebUI port.")] = 8787,
    repo: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="InkTag source tree; auto-detected by default.")] = None,
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
    tls_cert: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="HTTPS certificate in PEM format.")] = None,
    tls_key: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="HTTPS private key in PEM format.")] = None,
    enable_tls: Annotated[bool, typer.Option("--enable-tls", help="Use HTTPS; generate/reuse a loopback certificate when no certificate paths are given.")] = False,
) -> None:
    options = _context(context)
    try:
        result = start_webui_background(repo=repo, port=port, data_dir=data_dir, tls_cert=tls_cert, tls_key=tls_key, enable_tls=enable_tls)
    except (OSError, RuntimeError, ValueError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, result, title="InkTag WebUI background service")


@webui_app.command("stop")
def webui_stop(
    context: typer.Context,
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
) -> None:
    options = _context(context)
    try:
        result = stop_webui_background(data_dir)
    except OSError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, result, title="InkTag WebUI background service")


@webui_app.command("restart")
def webui_restart(
    context: typer.Context,
    port: Annotated[int | None, typer.Option(min=1, max=65535, help="Override the previous local WebUI port.")] = None,
    repo: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="InkTag source tree; auto-detected by default.")] = None,
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
    tls_cert: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="Override the HTTPS certificate (also requires --tls-key).")] = None,
    tls_key: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="Override the HTTPS private key (also requires --tls-cert).")] = None,
    enable_tls: Annotated[bool | None, typer.Option("--enable-tls/--no-enable-tls", help="Enable HTTPS with an automatic loopback certificate, or explicitly return to HTTP. Omitted: keep the previous mode.")] = None,
) -> None:
    options = _context(context)
    try:
        result = restart_webui_background(repo=repo, port=port, data_dir=data_dir, tls_cert=tls_cert, tls_key=tls_key, enable_tls=enable_tls)
    except (OSError, RuntimeError, ValueError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, result, title="InkTag WebUI background service")


@webui_app.command("status")
def webui_status(
    context: typer.Context,
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
) -> None:
    _emit(_context(context), webui_background_status(data_dir), title="InkTag WebUI background service")


@webui_app.command("log")
def webui_log(
    context: typer.Context,
    lines: Annotated[int, typer.Option(min=1, help="Number of recent log lines to show.")] = 100,
    follow: Annotated[bool, typer.Option("--follow", "-f", help="Keep following new log output.")] = False,
    service: Annotated[str, typer.Option(help="Log stream: webui or find.")] = "webui",
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
) -> None:
    options = _context(context)
    try:
        path, content = read_webui_log(data_dir, lines=lines, service=service)
    except (OSError, ValueError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    if options.json_output:
        _emit(options, {"path": str(path), "content": content})
        return
    if content:
        typer.echo(content)
    elif not follow:
        typer.echo(f"No WebUI log has been written yet: {path}")
    if not follow:
        return
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch(exist_ok=True)
        with path.open("r", encoding="utf-8", errors="replace") as stream:
            stream.seek(0, 2)
            while True:
                line = stream.readline()
                if line:
                    typer.echo(line, nl=False)
                else:
                    time.sleep(0.2)
    except KeyboardInterrupt:
        return


@webui_app.command("serve")
def webui_serve(
    context: typer.Context,
    listen: Annotated[str, typer.Option(help="Local or LAN host:port for the WebUI.")] = "127.0.0.1:8787",
    lan: Annotated[bool, typer.Option("--lan", help="Enable phone/LAN access; automatically prepare HTTPS and show an access password.")] = False,
    allowed_host: Annotated[list[str] | None, typer.Option("--allowed-host", help="Advanced: additional trusted hostname/IP, without a port.")] = None,
    access_password_stdin: Annotated[bool, typer.Option("--access-password-stdin", help="Advanced: read a server access password from stdin instead of generating one.")] = False,
    port: Annotated[int | None, typer.Option(min=0, max=65535, help="Local WebUI port; overrides the port in --listen.")] = None,
    repo: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="InkTag source tree; auto-detected by default.")] = None,
    data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Main local-service data directory.")] = None,
    web_root: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Built WebUI directory.")] = None,
    tls_cert: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="Trusted HTTPS certificate in PEM format.")] = None,
    tls_key: Annotated[Path | None, typer.Option(exists=True, dir_okay=False, readable=True, resolve_path=True, help="HTTPS private key in PEM format.")] = None,
    enable_tls: Annotated[bool, typer.Option("--enable-tls", help="Use HTTPS; generate/reuse a loopback certificate when no certificate paths are given.")] = False,
    find_service: Annotated[str | None, typer.Option(help="Existing loopback InkTag Find service URL.")] = None,
    find_data_dir: Annotated[Path | None, typer.Option(file_okay=False, resolve_path=True, help="Advanced: override the Find identity/session directory (default: DATA_DIR/find).")] = None,
    find_no_install: Annotated[bool, typer.Option("--find-no-install", help="Refuse to create or update the managed Find Python runtime.")] = False,
    no_open: Annotated[bool, typer.Option("--no-open", help="Do not open a browser after startup.")] = False,
) -> None:
    options = _context(context)
    try:
        from .local_server import read_access_password
        exit_code = serve_webui(
            repo=repo,
            listen=listen,
            port=port,
            data_dir=data_dir,
            web_root=web_root,
            tls_cert=tls_cert,
            tls_key=tls_key,
            enable_tls=enable_tls,
            find_service=find_service,
            find_data_dir=find_data_dir,
            find_no_install=find_no_install,
            no_open=no_open,
            lan=lan,
            allowed_hosts=tuple(allowed_host or ()),
            access_password=read_access_password() if access_password_stdin else None,
        )
    except (OSError, ValueError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    if exit_code:
        raise typer.Exit(exit_code)


async def _sample_device_clock(
    options: CliContext,
    *,
    sync_on_connect: bool = True,
) -> tuple[dict[str, int], datetime, datetime, int, int]:
    async with options.client(auto_sync_time=sync_on_connect, allow_password_fault_diagnostics=True) as client:
        host_utc_before = datetime.now(timezone.utc)
        monotonic_before_ns = time.monotonic_ns()
        result = await client.read_time()
        monotonic_after_ns = time.monotonic_ns()
        host_utc_after = datetime.now(timezone.utc)
        return result, host_utc_before, host_utc_after, monotonic_before_ns, monotonic_after_ns


@slot_app.command("list")
def slot_list(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> tuple[ImageStoreInfo | None, list[ImageStoreItem]]:
        async with options.client() as client:
            return await client.list_slots()

    info, items = _run(options, operation())
    if options.json_output:
        _emit(options, {"info": info.to_dict() if info else None, "items": [item.to_dict() for item in items]})
        return
    if info:
        _emit(options, info.to_dict(), title="Image store")
    table = Table(title=f"Stored pages ({len(items)})")
    table.add_column("ID", justify="right")
    table.add_column("Name")
    table.add_column("Type")
    table.add_column("Size")
    table.add_column("Bytes", justify="right")
    table.add_column("Compatible")
    for item in items:
        table.add_row(
            str(item.id),
            item.name,
            item.content_type,
            f"{item.width}x{item.height}",
            str(item.bytes),
            "yes" if item.compatible else "no",
        )
    console.print(table)


@slot_app.command("show")
def slot_show(
    context: typer.Context,
    slot: Annotated[str, typer.Argument(help="Device slot ID or current image name.")],
) -> None:
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.show_slot(slot)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "slot": slot, "deviceReply": reply})


@slot_app.command("download")
def slot_download(
    context: typer.Context,
    slot_id: Annotated[int, typer.Argument(min=0, max=255)],
    output: Annotated[
        Path | None,
        typer.Option("--output", "-o", help="Output file, or basename when --format both."),
    ] = None,
    output_format: Annotated[
        str,
        typer.Option("--format", help="Export format: png, raw, or both."),
    ] = "png",
    force: Annotated[
        bool,
        typer.Option("--force", help="Overwrite existing local output files."),
    ] = False,
) -> None:
    """Download one stored device-ready frame and optionally decode it to PNG."""
    options = _context(context)
    selected_format = output_format.strip().lower()
    if selected_format not in {"png", "raw", "both"}:
        raise typer.BadParameter("--format must be png, raw, or both")

    async def operation() -> tuple[ImageStoreItem, bytes, ScreenConfig | None]:
        async with options.client() as client:
            item, payload = await client.read_slot(slot_id)
            return item, payload, client.snapshot.screen

    item, payload, screen = _run(options, operation())
    stem = re.sub(r"[^a-zA-Z0-9._-]+", "_", item.name).strip("._") or "image"
    default_base = Path(f"slot-{item.id}-{stem}")
    requested = output or default_base
    if selected_format == "both":
        base = requested.with_suffix("") if requested.suffix else requested
        raw_path = base.with_suffix(".bin")
        png_path = base.with_suffix(".png")
    elif selected_format == "raw":
        raw_path = requested if output else requested.with_suffix(".bin")
        png_path = None
    else:
        raw_path = None
        png_path = requested if output else requested.with_suffix(".png")

    paths = [path for path in (raw_path, png_path) if path is not None]
    existing = [path for path in paths if path.exists()]
    if existing and not force:
        joined = ", ".join(str(path) for path in existing)
        _emit_error(options, f"Output already exists: {joined}; use --force to overwrite")
        raise typer.Exit(1)

    if png_path is not None and (
        screen is None
        or not item.compatible
        or item.width != screen.width
        or item.height != screen.height
    ):
        _emit_error(
            options,
            "The slot is not compatible with the current screen profile; "
            "download it with --format raw instead",
        )
        raise typer.Exit(1)

    if raw_path is not None:
        raw_path.parent.mkdir(parents=True, exist_ok=True)
        raw_path.write_bytes(payload)
    if png_path is not None:
        save_png(
            decode_stored_image(payload, screen, item.format, item.segments),
            png_path,
        )

    result = {
        "ok": True,
        "slot": item.to_dict(),
        "bytes": len(payload),
        "computedCrc32": f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}",
        "sha256": hashlib.sha256(payload).hexdigest(),
        "rawPath": str(raw_path.resolve()) if raw_path is not None else None,
        "pngPath": str(png_path.resolve()) if png_path is not None else None,
    }
    _emit(options, result, title="Image slot downloaded")


@slot_app.command("delete")
def slot_delete(
    context: typer.Context,
    slot_id: Annotated[int, typer.Argument(min=0, max=255)],
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm deletion without prompting.")] = False,
) -> None:
    options = _context(context)
    _require_confirmation(options, yes, f"Delete device slot {slot_id}?")

    async def operation() -> str:
        async with options.client() as client:
            return await client.delete_slot(slot_id)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "deletedSlot": slot_id, "deviceReply": reply})


@slot_app.command("clear")
def slot_clear(
    context: typer.Context,
    all_images: Annotated[
        bool,
        typer.Option("--all", help="Delete every stored page, not only incompatible records."),
    ] = False,
    yes: Annotated[bool, typer.Option("--yes", "-y", help="Confirm deletion without prompting.")] = False,
) -> None:
    options = _context(context)
    scope = "all stored pages" if all_images else "incompatible stored pages"
    _require_confirmation(options, yes, f"Delete {scope}?")

    async def operation() -> str:
        async with options.client() as client:
            return await client.clear_slots(not all_images)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "cleared": "all" if all_images else "incompatible", "deviceReply": reply})


@slideshow_app.command("start")
def slideshow_start(
    context: typer.Context,
    interval: Annotated[str, typer.Option(help="Interval such as 1m for testing, 30m or 1h.")] = "60m",
) -> None:
    options = _context(context)
    seconds = _parse_duration(interval)

    async def operation() -> str:
        async with options.client() as client:
            return await client.start_slideshow(seconds)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "intervalSeconds": seconds, "deviceReply": reply})


@slideshow_app.command("stop")
def slideshow_stop(context: typer.Context) -> None:
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.stop_slideshow()

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "slideshow-stop", "deviceReply": reply})


@diagnostic_app.command('runtime')
def diagnostic_runtime(
    context: typer.Context,
    wait_seconds: Annotated[int, typer.Option(
        '--wait-seconds', min=0, max=30,
        help='Wait 0-30 seconds on the same authenticated connection before one STATUS/HEALTH read; no polling or time sync.',
    )] = 0,
) -> None:
    """Same STATUS temperatures/power gates/health/soak fields as WebUI Device > Logs."""
    options = _context(context)

    async def operation():
        async with options.client(auto_sync_time=False, allow_password_fault_diagnostics=True) as client:
            if wait_seconds:
                await asyncio.sleep(wait_seconds)
            report = await client.read_diagnostics()
            report.update(ok=True, readOnly=True, healthSupported=client.supports_capability(CapabilityV2.HEALTH), health=None)
            if report['healthSupported']:
                try:
                    report['health'] = await client.health()
                except InkTagError as error:
                    report.update(ok=False, healthError=str(error))
            return report

    report = _run(options, operation())
    _emit(options, report, title='Runtime diagnostics (read-only)')
    if not report['ok']:
        raise typer.Exit(1)


@diagnostic_app.command('clock')
def diagnostic_clock(context: typer.Context) -> None:
    """Device - host UTC offset; one GET_TIME, no sync/refresh. Positive means device fast."""
    options = _context(context)
    async def operation():
        sample = await _sample_device_clock(options, sync_on_connect=False)
        return clock_status(*sample)
    report = _run(options, operation())
    report['note'] = 'Integer-second sample with RTT uncertainty, not long-term drift/ppm. Ordinary earlier connections may have synced time.'
    _emit(options, dict(ok=True, **report), title='Clock diagnostics (read-only)')


@led_app.command("color")
def led_color(
    context: typer.Context,
    color: Annotated[
        str,
        typer.Argument(help="off, red, green, blue, yellow, magenta, cyan or white."),
    ] = "off",
) -> None:
    options = _context(context)
    try:
        normalized = normalize_led_color(color)
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(2)

    async def operation() -> str:
        async with options.client() as client:
            return await client.test_led(LED_COLORS[normalized])

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "led-color", "color": normalized, "deviceReply": reply})


async def _play_led_steps(client: InkTagClient, steps: list[LedStep], clear_at_end: bool) -> float:
    started = time.perf_counter()
    try:
        for step in steps:
            await client.test_led(step.color_mask)
            if step.duration_ms:
                await asyncio.sleep(step.duration_ms / 1000.0)
    finally:
        if clear_at_end:
            await client.test_led(0)
    return time.perf_counter() - started


@led_app.command("effect")
def led_effect(
    context: typer.Context,
    color: Annotated[
        str,
        typer.Argument(help="off, red, green, blue, yellow, magenta, cyan or white."),
    ],
    effect: Annotated[
        str,
        typer.Argument(
            help="off, solid, slow-flash, fast-flash, flash-once, flash-twice, "
            "flash-three or sequence."
        ),
    ],
    repeat: Annotated[
        int,
        typer.Option(min=1, max=100, help="Repeat the bounded effect this many times."),
    ] = 1,
    step_ms: Annotated[
        int | None,
        typer.Option(min=1, max=60_000, help="Override the effect step duration in milliseconds."),
    ] = None,
    duration_ms: Annotated[
        int,
        typer.Option(
            min=0,
            max=600_000,
            help="Solid hold time in milliseconds; zero uses the device's bounded diagnostic timeout.",
        ),
    ] = 0,
) -> None:
    """Play a bounded host-timed status-light effect without changing saved settings."""

    options = _context(context)
    try:
        steps, clear_at_end = build_led_effect(
            color,
            effect,
            repeat=repeat,
            step_ms=step_ms,
            duration_ms=duration_ms,
        )
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(2)

    async def operation() -> float:
        async with options.client() as client:
            return await _play_led_steps(client, steps, clear_at_end)

    elapsed = _run(options, operation())
    _emit(options, {
        "ok": True,
        "action": "led-effect",
        "color": color.strip().lower(),
        "effect": effect.strip().lower(),
        "repeat": repeat,
        "requestedDurationMs": sum(step.duration_ms for step in steps),
        "elapsedMs": round(elapsed * 1000),
        "timing": "host-best-effort",
    })


@led_app.command("pattern")
def led_pattern(
    context: typer.Context,
    pattern: Annotated[
        str,
        typer.Argument(help="Comma-separated color:milliseconds steps, for example red:120,off:120,blue:240."),
    ],
    repeat: Annotated[
        int,
        typer.Option(min=1, max=100, help="Repeat the complete pattern this many times."),
    ] = 1,
) -> None:
    """Play a bounded custom millisecond LED sequence without changing saved settings."""

    options = _context(context)
    try:
        steps = parse_led_pattern(pattern, repeat=repeat)
    except ValueError as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(2)

    async def operation() -> float:
        async with options.client() as client:
            return await _play_led_steps(client, steps, True)

    elapsed = _run(options, operation())
    _emit(options, {
        "ok": True,
        "action": "led-pattern",
        "repeat": repeat,
        "steps": len(steps),
        "requestedDurationMs": sum(step.duration_ms for step in steps),
        "elapsedMs": round(elapsed * 1000),
        "timing": "host-best-effort",
    })


@led_app.command("off")
def led_off(context: typer.Context) -> None:
    """Stop the active diagnostic LED override and restore the saved policy."""

    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.test_led(0)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "led-off", "deviceReply": reply})


@led_app.command("locate")
def led_locate(
    context: typer.Context,
    stop: Annotated[
        bool,
        typer.Option("--stop", help="Stop the active local LED/buzzer locator."),
    ] = False,
) -> None:
    """Start or stop the device's bounded local LED/buzzer locator."""
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.locate_device(start=not stop)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "device-locate-stop" if stop else "device-locate", "deviceReply": reply})


@display_app.command("ghost-clean")
def display_ghost_clean(
    context: typer.Context,
    cycles: Annotated[
        int,
        typer.Option(min=1, max=10, help="Complete white-black-white cleaning cycles."),
    ] = 5,
) -> None:
    """Run bounded full-screen ghost cleaning and finish on white."""
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.ghost_clean(cycles)

    reply = _run(options, operation())
    _emit(options, {
        "ok": True,
        "action": "ghost-clean",
        "cycles": cycles,
        "deviceReply": reply,
    })


@io_app.command("buzzer-play")
def buzzer_play(
    context: typer.Context,
    duration_ms: Annotated[
        int,
        typer.Option(min=20, max=5000, help="Bounded output duration in milliseconds."),
    ] = 500,
) -> None:
    options = _context(context)

    async def operation() -> str:
        async with options.client() as client:
            return await client.test_buzzer(duration_ms)

    reply = _run(options, operation())
    _emit(options, {"ok": True, "action": "buzzer-test", "durationMs": duration_ms, "deviceReply": reply})


@ota_app.command("inspect")
def ota_inspect(
    context: typer.Context,
    package: Annotated[Path, typer.Argument(exists=True, dir_okay=False)],
) -> None:
    options = _context(context)
    try:
        info = inspect_package(package)
    except (ValueError, OSError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None
    _emit(options, info.to_dict(), title="OTA package")


@recovery_app.command("enter")
def recovery_enter(context: typer.Context) -> None:
    """Request the selected application to enter its fixed Recovery role."""
    options = _context(context)

    _emit(
        options,
        _run(options, _recovery_enter_operation(options)),
        title="Recovery entry completed",
    )


async def _recovery_enter_operation(options: CliContext) -> dict[str, Any]:
    """Request Recovery and finish vendor-specific role handoff when needed."""
    async with options.client(auto_sync_time=False) as client:
        reply = await client.enter_recovery()
        snapshot = client.snapshot
        board_id = snapshot.board_id
        application_name = snapshot.device_name or snapshot.name
        transport_address = snapshot.address
        result: dict[str, Any] = {
            "ok": True,
            "action": "recovery-enter",
            "deviceName": application_name,
            "address": snapshot.ble_address or transport_address,
            "deviceReply": reply,
        }

    if board_id.casefold().startswith("gr"):
        from ._drivers.ota_goodix import enter_verified_recovery

        try:
            verification = await enter_verified_recovery(
                name=application_name,
                address=transport_address,
                scan_timeout=options.scan_timeout,
                connect_timeout=options.connect_timeout,
                reconnect_timeout=30.0,
                password_data_dir=local_data_directory(getattr(options, "password_data_dir", None)),
            )
        except (OSError, RuntimeError, TimeoutError) as error:
            raise InkTagError(str(error) or type(error).__name__) from None
        result["verifiedRole"] = verification["role"]
        result["recoveryName"] = verification["recoveryName"]
        result["modeSetSent"] = verification["modeSetSent"]
    return result


@recovery_app.command("status")
def recovery_status(
    context: typer.Context,
    device: Annotated[str | None, typer.Option("--device", "-d", help="Exact TLSR Recovery BLE name; no prefix matching.")] = None,
    address: Annotated[str | None, typer.Option("--address", "-a", help="Exact BLE address or macOS CoreBluetooth UUID.")] = None,
) -> None:
    """Read existing TLSR Recovery status without resume, transfer or time sync."""
    from ._drivers.ota_tlsr8359 import read_recovery_status

    options = _context(context)
    try:
        with redirect_stdout(sys.stderr) if options.json_output else nullcontext():
            result = asyncio.run(read_recovery_status(
                name=device if device is not None else options.device,
                address=address if address is not None else options.address,
                scan_timeout=options.scan_timeout, connect_timeout=options.connect_timeout,
            ))
    except (BleakError, InkTagError, ValueError, OSError, RuntimeError) as exc:
        _emit_error(options, str(exc) or type(exc).__name__)
        raise typer.Exit(1) from None
    _emit(options, result, title="Recovery read-only status")


@recovery_app.command("exit")
@recovery_app.command("resume-app")
def recovery_resume_app(
    context: typer.Context,
    device: Annotated[
        str | None,
        typer.Option("--device", "-d", help="Recovery BLE name or unique name prefix."),
    ] = None,
    address: Annotated[
        str | None,
        typer.Option("--address", "-a", help="Exact BLE address or macOS CoreBluetooth UUID."),
    ] = None,
    reconnect_timeout: Annotated[
        float,
        typer.Option(
            "--reconnect-wait",
            min=5.0,
            max=120.0,
            help="Seconds to wait for the unchanged application to advertise again.",
        ),
    ] = 30.0,
) -> None:
    """Ask an idle Recovery to resume an untouched, locally valid application.

    This command never enters Recovery, confirms a trial image, advances an OTA
    epoch, or bypasses Recovery's voltage, journal, mutation and image checks.
    """
    options = _context(context)
    selected_device = device if device is not None else options.device
    selected_address = address if address is not None else options.address
    if not selected_device and not selected_address:
        _emit_error(options, "provide --device or --address for the Recovery device")
        raise typer.Exit(1)

    async def resume_for_platform() -> dict:
        platform_name = selected_device or ""
        resolved_address = selected_address
        if not platform_name and resolved_address:
            # An address by itself does not identify a vendor. Observe the
            # fixed Recovery advertisement before choosing its protocol; a
            # sequential Goodix/OM fallback can never reach TLSR safely.
            observed = await scan_device_status(
                options.scan_timeout, address=resolved_address
            )
            platform_name = observed.name
            if not platform_name.casefold().endswith("_ota"):
                raise RuntimeError(
                    "safe resume requires an advertised fixed Recovery _OTA name"
                )
        normalized_name = platform_name.casefold()
        if normalized_name.startswith(("ikt_gr_", "inktag_gr_")):
            from ._drivers.ota_goodix import resume_safe_application
        elif normalized_name.startswith(("ikt_om_", "inktag_om_")):
            from ._drivers.ota_om6626 import resume_safe_application
        elif normalized_name.startswith(("ikt_tl_", "inktag_tl_")):
            from ._drivers.ota_tlsr8359 import resume_safe_application
        elif normalized_name.startswith(("ikt_da_", "inktag_da_")):
            from ._drivers.ota_da14585 import resume_safe_application
        elif normalized_name.startswith(("ikt_nrf_", "inktag_nrf_")):
            from ._drivers.ota_nrf52811 import resume_safe_application
        else:
            raise RuntimeError(
                "Recovery safe resume requires a qualified fixed GR, OM, TLSR, DA or Nordic role"
            )
        return await resume_safe_application(
            name=platform_name,
            address=resolved_address,
            scan_timeout=options.scan_timeout,
            connect_timeout=options.connect_timeout,
            reconnect_timeout=reconnect_timeout,
        )

    try:
        # Driver discovery text must not contaminate the JSON result on stdout.
        with redirect_stdout(sys.stderr) if options.json_output else nullcontext():
            result = asyncio.run(resume_for_platform())
    except (BleakError, InkTagError, ValueError, OSError, RuntimeError) as exc:
        _emit_error(options, str(exc) or type(exc).__name__)
        raise typer.Exit(1) from None
    _emit(options, result, title="Recovery safe resume")


@ota_app.command("update")
def ota_update(
    context: typer.Context,
    package: Annotated[Path, typer.Argument(exists=True, dir_okay=False)],
    yes: Annotated[
        bool,
        typer.Option("--yes", "-y", help="Confirm the validated update without prompting."),
    ] = False,
    dry_run: Annotated[
        bool,
        typer.Option("--dry-run", help="Validate and show the driver command without connecting."),
    ] = False,
    single_packet_window: Annotated[
        bool, typer.Option("--single-packet-window",
            help="TLSR only: acknowledge each data packet before sending another; retains normal verification and clock sync."),
    ] = False,
    reconnect_timeout: Annotated[
        float,
        typer.Option(
            "--reconnect-wait",
            min=5.0,
            max=300.0,
            help="Seconds to wait for the updated device to return.",
        ),
    ] = 75.0,
    no_sync: Annotated[
        bool,
        typer.Option(
            "--no-time-sync",
            help="Do not synchronize the device clock after a verified update.",
        ),
    ] = False,
    legacy_no_sync: Annotated[bool, typer.Option("--no-sync", hidden=True)] = False,
    legacy_recovery_name_fallback: Annotated[
        bool,
        typer.Option(
            "--legacy-om-recovery",
            help=("Rescue unprovisioned old OM firmware with a generic Recovery "
                  "name; never use for current stable-identity devices."),
        ),
    ] = False,
) -> None:
    options = _context(context)
    no_sync = no_sync or legacy_no_sync or options.preserve_device_time
    try:
        info = inspect_package(package)
        command = driver_command(
            info,
            device=options.device,
            address=options.address,
            scan_timeout=options.scan_timeout,
            connect_timeout=options.connect_timeout,
            reconnect_timeout=reconnect_timeout,
            skip_reconnect_check=False,
            no_sync=no_sync,
            legacy_recovery_name_fallback=legacy_recovery_name_fallback,
            password_data_dir=local_data_directory(options.password_data_dir),
            single_packet_window=single_packet_window,
        )
    except (ValueError, OSError, RuntimeError) as exc:
        _emit_error(options, str(exc))
        raise typer.Exit(1) from None

    if dry_run:
        _emit(options, {"package": info.to_dict(), "driverCommand": command})
        return
    _require_confirmation(
        options,
        yes,
        f"Install {info.transport} OTA package for {info.board or 'the selected device'}?",
    )
    result = run_update(command, capture_output=options.json_output)
    clock_synced = False
    clock_sync_error: str | None = None
    if result.returncode == 0 and not no_sync:
        sync_options = options
        if (info.transport == "nordic" and options.device and
                re.fullmatch(r"(?:IKT|InkTag)_NRF_[0-9A-F]{4}_OTA", options.device, re.IGNORECASE)):
            # Recovery has a different BLE address. Reconnect by its stable App
            # name, never subtract from/guess the Recovery's address.
            sync_options = replace(options, device=options.device[:-4], address=None)
        clock_sync_error = _run(options, _sync_time_after_ota(sync_options))
        clock_synced = clock_sync_error is None
        if clock_sync_error and not options.json_output:
            error_console.print(
                "[yellow]部分成功:[/yellow] 新应用已安装并通过验身，"
                "但设备时间校准失败，完整更新流程未完成："
                f"{clock_sync_error}。请排查连接/鉴权并单独重试 time sync，"
                "不要因此重复 OTA。"
            )
    exit_code = result.returncode or (1 if clock_sync_error is not None else 0)
    if options.json_output:
        _emit(
            options,
            {
                "ok": exit_code == 0,
                "status": ("partial-success" if clock_sync_error is not None else
                           "failed" if exit_code else "success"),
                "transport": info.transport,
                "exitCode": exit_code,
                "driverExitCode": result.returncode,
                "applicationVerified": result.returncode == 0,
                "clockSynced": clock_synced,
                "clockSyncError": clock_sync_error,
                "stdout": result.stdout,
                "stderr": result.stderr,
            },
        )
    if exit_code:
        raise typer.Exit(exit_code)


async def _sync_time_after_ota(options: CliContext) -> str | None:
    """Restore wall time once, after the transport has attested the new app.

    OTA drivers own only transport and application attestation. Keeping this
    explicit mutation here gives every MCU the same behavior without reviving
    the normal connection-time auto-sync or duplicating SET_TIME in each driver.
    """
    try:
        client = options.client(auto_sync_time=False)
        if getattr(options, "json_output", False) or not ota_password_prompt_available():
            # Post-attestation uses another link too: a JSON run must not turn
            # a refused credential into an interactive wait at this stage.
            client.password_prompt = None
        async with client:
            await client.sync_time()
    except Exception as error:
        return str(error).strip() or type(error).__name__
    return None


def _require_confirmation(options: CliContext, confirmed: bool, prompt: str) -> None:
    if confirmed:
        return
    if options.json_output or not sys.stdin.isatty():
        raise typer.BadParameter("This operation requires --yes in non-interactive mode")
    if not typer.confirm(prompt):
        raise typer.Abort()


def _parse_duration(value: str) -> int:
    match = re.fullmatch(r"\s*(\d+)\s*([smh]?)\s*", value, re.IGNORECASE)
    if not match:
        raise typer.BadParameter("Duration must look like 30s, 5m or 1h")
    amount = int(match.group(1))
    multiplier = {"": 1, "s": 1, "m": 60, "h": 3600}[match.group(2).lower()]
    seconds = amount * multiplier
    if not 30 <= seconds <= 86400:
        raise typer.BadParameter("Slideshow interval must be between 30 seconds and 24 hours")
    return seconds


def _parse_clock_measurement_duration(value: str) -> int:
    match = re.fullmatch(r"\s*(\d+)\s*([smh]?)\s*", value, re.IGNORECASE)
    if not match:
        raise typer.BadParameter("Duration must look like 60s, 10m or 1h")
    amount = int(match.group(1))
    multiplier = {"": 1, "s": 1, "m": 60, "h": 3600}[match.group(2).lower()]
    seconds = amount * multiplier
    if not 60 <= seconds <= 86400:
        raise typer.BadParameter("Clock measurement must be between 60 seconds and 24 hours")
    return seconds


@diagnostic_app.command('health', help='Read power, health and a recent crash record without synchronizing time.')
def diagnostic_health(context: typer.Context,
    record: Annotated[int, typer.Option(min=0, max=3, help='Crash record index; zero is newest.')] = 0,
) -> None:
    options = _context(context)
    async def operation():
        async with options.client(auto_sync_time=False, allow_password_fault_diagnostics=True) as client:
            return await client.health(record)
    _emit(options, {'ok': True, 'readOnly': True, 'health': _run(options, operation())}, title='Health diagnostics (read-only)')


# Internal extensions are optional; missing extension dependencies are not hidden.
try:
    from ._diagnostics import register as _register_diagnostics
except ModuleNotFoundError as error:
    if error.name != __package__ + '._diagnostics':
        raise
else:
    _register_diagnostics(app, diagnostic_app)


@nfc_app.command("status")
def nfc_status(context: typer.Context) -> None:
    """Read NFC capability/status without dumping tag user content."""
    options = _context(context)
    async def operation():
        async with options.client() as client:
            return await client.request_nfc_status()
    _emit(options, {"ok": True, "messages": _run(options, operation())})


@nfc_app.command("action")
def nfc_action(context: typer.Context,
    action: Annotated[str, typer.Argument(help="disabled, next, slot, device-info, calendar, clock, slideshow or stop")],
    slot: Annotated[int, typer.Option(min=0, max=65535)] = 0,
) -> None:
    options = _context(context)
    if action not in _NFC_ACTIONS:
        raise typer.BadParameter("Unsupported NFC action")
    async def operation():
        async with options.client() as client:
            return await client.configure_nfc(_NFC_ACTIONS[action], slot_id=slot)
    _emit(options, {"ok": True, "deviceReply": _run(options, operation())})


def main() -> None:
    app()
