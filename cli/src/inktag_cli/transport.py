from __future__ import annotations

import asyncio
import re
import struct
import sys
import time
import zlib
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, replace
from datetime import date, datetime
from types import SimpleNamespace, TracebackType
from typing import Any

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice
from bleak.exc import BleakError

from .generated_commands_v2 import (
    ActivationResultV2,
    CapabilityV2,
    CommandV2,
    DisplayActionV2,
    DisplayModeV2,
    EventV2,
    PROTOCOL_V2_VERSION,
    RequestFlagV2,
    NfcCapabilityV2,
    ResourceClearScopeV2,
    SettingsSectionV2,
    SleepWakeFlagV2,
    StatusV2,
    SystemActionV2,
    TestOperationV2,
    TransferObjectV2,
)
from .activation import decode_code, parse_request, parse_status
from .owner_auth import action_hash as owner_action_hash, parse_challenge as parse_owner_challenge
from .board_ids import canonical_board_id
from .crash_alerts import acknowledge_crashes, crash_warning
from .device_qr import encode_device_qr
from .device_link import build_device_link
from .password_session import DevicePasswordSession
from .input_simulation import structured_input_ack, decode_input_completion
from .find import decode_capabilities as decode_find_capabilities
from .find import decode_config as decode_find_config
from .find import (
    decode_owner_rotation_challenge,
    decode_owner_rotation_proof,
    decode_owner_rotation_status,
    decode_owner_status,
)
from .find import PUBLIC_ONLY_OWNER_VERSION
from .find import encode_config as encode_find_config
from .nfc_command import decode_capabilities as decode_nfc_capabilities
from .nfc_command import encode_ndef as encode_nfc_ndef
from .nfc_command import encode_uri_ndef
from .nfc_command import decode_ndef as decode_nfc_ndef
from .nfc_command import decode_status as decode_nfc_status
from .image_quota import constrain_image_store_for_client
from .indicator import decode_indicator_config, encode_indicator_config
from .temperature_sources import has_temperature_source_record, parse_temperature_sources
from .gui_assets import decode_gui_asset_info, validate_gui_asset_package
from .models import (
    DeviceSnapshot,
    DiscoveredDevice,
    ImageStoreInfo,
    ImageStoreItem,
    ScreenConfig,
    SettingsCapabilities,
)
from .protocol import (
    COMMAND_UUID,
    INKTAG_SERVICE_UUID,
    KNOWN_NAME_PREFIXES,
    OTA_SERVICE_UUIDS,
    VERSION_UUID,
    Notification,
    decode_screen_config,
    encode_bluetooth_name,
    encode_device_label,
    encode_input_action,
    encode_resource_alias,
    resource_alias_key,
    encode_screen_config,
    encode_v2_time_set_test,
    encode_v2_time_shift_test,
    input_action_mask,
    screen_profile_name,
)


class InkTagError(RuntimeError):
    """User-facing transport or protocol failure."""


class InkTagV2TimeoutError(InkTagError):
    """A V2 request may have reached the device but its ACK was not observed."""


class InkTagV2StatusError(InkTagError):
    """A V2 request was explicitly rejected with a protocol status."""

    def __init__(self, status: int, opcode: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.opcode = opcode


@dataclass
class _ResourceReadBudget:
    deadline: float
    retries: int = 0
    session: Any = None
    cancelled: asyncio.Event | None = None


_RESOURCE_READ_BUDGET = 60.0
_RESOURCE_READ_RETRY_DELAY = 2.0
_RESOURCE_READ_MAX_BUSY_RETRIES = 30


_DISPLAY_SETTINGS_VERSION = 3
_DISPLAY_SETTINGS_BASE_SIZE = 9
_DISPLAY_SETTINGS_MAX_NAME_BYTES = 12
_DEFAULT_DISPLAY_TIMEOUT = 100.0
_SLOW_DISPLAY_TIMEOUT = 240.0
_GHOST_CLEAN_MAX_TIMEOUT = 1800.0
_GHOST_CLEAN_FIRST_PROGRESS_TIMEOUT = 180.0
_GHOST_CLEAN_PROGRESS_STALL_TIMEOUT = 120.0
_SLOW_DISPLAY_MIN_PIXELS = 400 * 300


def _ghost_clean_hard_timeout(cycles: int) -> float:
    # Each cycle contains three full-panel waveforms. Large four-colour panels
    # can need more than the old fixed 300 s even while progressing normally.
    return min(_GHOST_CLEAN_MAX_TIMEOUT, 60.0 + cycles * 3 * 60.0)


class _IncompleteServiceDiscovery(RuntimeError):
    pass


_SCAN_SETTLE_SECONDS = 3.25
_FACTORY_EXACT_NAME = re.compile(r"^(?:IKT|InkTag)_[A-Za-z0-9]+_([0-9A-Fa-f]{4})$")


_V2_STATUS_LABELS = {
    int(status): status.name.lower().replace("_", " ") for status in StatusV2
}


def _advertised_name(device: Any, advertisement: Any) -> str:
    return (getattr(advertisement, "local_name", None) or getattr(device, "name", None) or "").strip()


def _advertised_services(advertisement: Any) -> tuple[str, ...]:
    return tuple(sorted(str(value).lower() for value in (getattr(advertisement, "service_uuids", None) or ())))


def _is_inktag(name: str, services: tuple[str, ...]) -> bool:
    return name.startswith(KNOWN_NAME_PREFIXES) or INKTAG_SERVICE_UUID in services


def _factory_name_matches_address(name: str, address: str) -> bool:
    """Match a factory BLE name to its address suffix.

    Windows can deliver an InkTag scan response carrying the service UUID
    before (or without) the primary advertisement carrying the local name.
    Factory names deliberately end in the final four hexadecimal address
    digits, so an exact requested factory name can identify that candidate.
    The subsequent GATT/V2 handshake remains the authoritative identity check.
    """
    match = _FACTORY_EXACT_NAME.fullmatch(name)
    address_hex = re.sub(r"[^0-9A-Fa-f]", "", address)
    return bool(match and len(address_hex) >= 4 and
                address_hex[-4:].lower() == match.group(1).lower())


def _merge_scan_advertisement(
    previous: tuple[Any, Any, str] | None,
    device: Any,
    advertisement: Any,
    name: str,
) -> tuple[Any, Any, str]:
    if previous is None:
        return device, advertisement, name
    previous_device, previous_advertisement, previous_name = previous
    merged_name = name or previous_name
    merged_services = tuple(sorted(set(_advertised_services(previous_advertisement)) |
                                   set(_advertised_services(advertisement))))
    previous_rssi = getattr(previous_advertisement, "rssi", None)
    current_rssi = getattr(advertisement, "rssi", None)
    merged_advertisement = SimpleNamespace(
        local_name=merged_name or None,
        service_uuids=merged_services,
        rssi=current_rssi if current_rssi is not None else previous_rssi,
    )
    return device or previous_device, merged_advertisement, merged_name


async def _collect_scan_matches_once(
    timeout: float,
    *,
    name: str | None = None,
    address: str | None = None,
    wait_for_full_timeout: bool = False,
) -> list[tuple[Any, Any, str]]:
    """Collect matching advertisements without always consuming the full timeout.

    An exact address is unambiguous and can return on its first advertisement.
    An exact full name is also sufficient for immediate selection: factory
    InkTag names carry the device address suffix. Prefix/automatic selection
    keeps a short aggregation window after the first match so the ordinary
    one-second advertisement and the bounded one-second Find multiplex window
    can expose another matching InkTag before selection.
    """
    exact_factory_name = bool(name and _FACTORY_EXACT_NAME.fullmatch(name))
    if address:
        matched_advertisement: Any | None = None

        def exact_filter(device: Any, advertisement: Any) -> bool:
            nonlocal matched_advertisement
            advertised_name = _advertised_name(device, advertisement)
            matches = (
                str(device.address).lower() == address.lower()
                if address
                else advertised_name.lower() == name.lower()
            )
            if matches:
                matched_advertisement = advertisement
            return matches

        device = await BleakScanner.find_device_by_filter(
            exact_filter,
            timeout=timeout,
        )
        if device is None or matched_advertisement is None:
            return []
        return [(device, matched_advertisement,
                 _advertised_name(device, matched_advertisement))]

    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    settle_deadline: float | None = None
    observed: dict[str, tuple[Any, Any, str]] = {}
    candidates: dict[str, tuple[Any, Any, str]] = {}
    # Do not ask the OS to pre-filter by service UUID. Some qualified boards
    # put the complete InkTag name in primary ADV and the 128-bit UUID only in
    # scan response; WinRT can otherwise discard the device before this
    # application-level name check runs. GATT discovery below remains the
    # authoritative product/protocol validation before any command is sent.
    scanner = BleakScanner()
    async with scanner:
        stream = scanner.advertisement_data()
        iterator = stream.__aiter__()
        try:
            while True:
                wait_until = deadline if settle_deadline is None else min(deadline, settle_deadline)
                remaining = wait_until - loop.time()
                if remaining <= 0:
                    break
                try:
                    device, advertisement = await asyncio.wait_for(
                        iterator.__anext__(), remaining
                    )
                except (TimeoutError, StopAsyncIteration):
                    break
                device_address = str(device.address)
                key = device_address.lower()
                merged = _merge_scan_advertisement(
                    observed.get(key), device, advertisement,
                    _advertised_name(device, advertisement),
                )
                observed[key] = merged
                advertised_name = merged[2]
                services = _advertised_services(merged[1])
                if address and device_address.lower() != address.lower():
                    continue
                if name and not advertised_name.lower().startswith(name.lower()):
                    if not (
                        exact_factory_name
                        and INKTAG_SERVICE_UUID in services
                        and _factory_name_matches_address(name, device_address)
                    ):
                        continue
                if not address and not name and not _is_inktag(advertised_name, services):
                    continue
                candidates[key] = merged
                if address:
                    break
                if exact_factory_name and (
                    advertised_name.lower() == name.lower()
                    or _factory_name_matches_address(name, device_address)
                ):
                    break
                if not wait_for_full_timeout and settle_deadline is None:
                    settle_deadline = loop.time() + _SCAN_SETTLE_SECONDS
        finally:
            close = getattr(iterator, "aclose", None)
            if close is not None:
                await close()
    return list(candidates.values())


async def _collect_scan_matches(
    timeout: float,
    *,
    name: str | None = None,
    address: str | None = None,
    inventory: bool = False,
    phase_sink: Callable[[str], None] | None = None,
) -> list[tuple[Any, Any, str]]:
    # Restart one empty scanner session to recover from the observed Windows
    # backend state where a scan opens successfully but yields no reports. Keep
    # both attempts inside the caller's original timeout budget.
    if inventory:
        windows = (timeout,)
    elif sys.platform == "win32" and name and timeout >= 8.0:
        # WinRT occasionally opens an advertisement watcher that yields no
        # reports even though a preceding inventory scan saw the device. A
        # bounded restart recovers it without extending the caller's budget.
        first = min(timeout / 2.0, 4.0)
        windows = (first, timeout - first)
    elif sys.platform == "win32":
        windows = (timeout,)
    elif timeout < 4.0:
        windows = (timeout,)
    else:
        # A working InkTag scanner should see the 1-second connectable set in
        # at most one 2-second Find multiplex gap plus startup allowance. Do
        # not spend half of a deliberately long caller timeout on a WinRT
        # scanner session which is open but yields no reports.
        first = min(timeout / 2.0, 4.0)
        windows = (first, timeout - first)
    for pass_index, window in enumerate(windows, start=1):
        started = time.monotonic()
        if inventory:
            matches = await _collect_scan_matches_once(
                window,
                name=name,
                address=address,
                wait_for_full_timeout=True,
            )
        else:
            matches = await _collect_scan_matches_once(
                window, name=name, address=address
            )
        if phase_sink:
            phase_sink(
                f"scan pass {pass_index}/{len(windows)}: "
                f"budget={window:.2f}s elapsed={time.monotonic() - started:.3f}s "
                f"matches={len(matches)}"
            )
        if matches:
            return matches
    return []


async def scan_devices(timeout: float = 8.0, include_all: bool = False) -> list[DiscoveredDevice]:
    if include_all:
        discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
        merged: dict[str, tuple[Any, Any, str]] = {}
        for device, advertisement in discovered.values():
            key = str(device.address).lower()
            merged[key] = _merge_scan_advertisement(
                merged.get(key), device, advertisement,
                _advertised_name(device, advertisement),
            )
        matches = list(merged.values())
    else:
        matches = await _collect_scan_matches(timeout, inventory=True)
    output: list[DiscoveredDevice] = []
    for device, advertisement, name in matches:
        services = _advertised_services(advertisement)
        output.append(
            DiscoveredDevice(
                name=name or "(unnamed)",
                address=str(device.address),
                rssi=getattr(advertisement, "rssi", None),
                services=services,
            )
        )
    return sorted(output, key=lambda item: (not item.name.startswith(KNOWN_NAME_PREFIXES), item.name, item.address))


async def scan_device_status(
    timeout: float = 8.0,
    *,
    name: str | None = None,
    address: str | None = None,
) -> DiscoveredDevice:
    """Observe one InkTag advertisement without opening a GATT connection.

    A caller may pass the ordinary application name.  Recovery names append
    ``_OTA``, so the normal name remains a valid prefix while the device is in
    Recovery.  Unlike ``select_device()``, an explicit Windows address still
    scans here because the advertised name and services are the status being
    inspected.
    """
    candidates = await _collect_scan_matches(
        timeout, name=name, address=address
    )
    label = address or name or "InkTag"
    if not candidates:
        raise InkTagError(f"No matching BLE device was found: {label}")
    if len(candidates) > 1:
        exact = [
            item for item in candidates
            if name and item[2].lower() == name.lower()
        ]
        if len(exact) == 1:
            candidates = exact
        else:
            choices = ", ".join(
                f"{item[2] or '(unnamed)'} [{item[0].address}]"
                for item in candidates[:8]
            )
            raise InkTagError(
                f"Multiple devices matched {label}; use --device or --address: "
                f"{choices}"
            )
    device, advertisement, advertised_name = candidates[0]
    return DiscoveredDevice(
        name=advertised_name or "(unnamed)",
        address=str(device.address),
        rssi=getattr(advertisement, "rssi", None),
        services=_advertised_services(advertisement),
    )


async def select_device(
    name: str | None,
    address: str | None,
    timeout: float,
    phase_sink: Callable[[str], None] | None = None,
) -> tuple[Any, Any]:
    if sys.platform == "win32" and address:
        # A BLEDevice gives Bleak's WinRT backend the numeric Bluetooth
        # address directly. Passing the bare string makes Bleak launch another
        # implicit scan before connecting, defeating an explicit --address.
        # GATT connect/service discovery still proves reachability and identity.
        if phase_sink:
            phase_sink("scan skipped: using explicit Windows BLE address")
        return BLEDevice(address, name or address, None), None
    candidates = await _collect_scan_matches(
        timeout, name=name, address=address, phase_sink=phase_sink
    )

    label = address or name or "InkTag"
    if not candidates:
        raise InkTagError(f"No matching BLE device was found: {label}")
    if len(candidates) > 1:
        exact = [item for item in candidates if name and item[2].lower() == name.lower()]
        if len(exact) == 1:
            return exact[0][0], exact[0][1]
        choices = ", ".join(f"{item[2] or '(unnamed)'} [{item[0].address}]" for item in candidates[:8])
        raise InkTagError(f"Multiple devices matched {label}; use --device or --address: {choices}")
    return candidates[0][0], candidates[0][1]


async def release_bleak_client(client: Any) -> None:
    """Release a Bleak client and the WinRT physical link it owns.

    Bleak 3.0 closes its GATT session without first clearing
    ``GattSession.maintain_connection``.  On Windows that can leave the radio
    link alive after the Python client exits, suppressing advertisements and
    delaying the next OTA-attestation or time-sync connection.  Other
    backends do not expose this private session and simply take the normal
    public disconnect path.
    """
    backend = getattr(client, "_backend", None)
    session = getattr(backend, "_session", None)
    if session is not None and hasattr(session, "maintain_connection"):
        try:
            session.maintain_connection = False
        except (AttributeError, OSError, RuntimeError):
            pass
    await client.disconnect()


class InkTagClient(DevicePasswordSession):
    def __init__(
        self,
        *,
        name: str | None = None,
        address: str | None = None,
        scan_timeout: float = 8.0,
        connect_timeout: float = 15.0,
        notification_sink: Callable[[Notification], None] | None = None,
        phase_sink: Callable[[str], None] | None = None,
        crash_warning_sink: Callable[[dict[str, Any]], None] | None = None,
        auto_sync_time: bool = True,
        password_store=None,
        password_prompt=None,
        password_force_prompt: bool = False,
        require_password_authentication: bool = True,
        allow_password_fault_diagnostics: bool = False,
        password_fault_sink=None,
    ) -> None:
        self.name = name
        self.address = address
        self.scan_timeout = scan_timeout
        self.connect_timeout = connect_timeout
        self.notification_sink = notification_sink
        self.phase_sink = phase_sink
        self.crash_warning_sink = crash_warning_sink
        self.auto_sync_time = auto_sync_time
        self._password_init(password_store, password_prompt)
        self.password_force_prompt = password_force_prompt
        self.require_password_authentication = require_password_authentication
        self.allow_password_fault_diagnostics = allow_password_fault_diagnostics
        self.password_fault_sink = password_fault_sink
        self._password_fault = False
        self.snapshot = DeviceSnapshot()
        self.notifications: list[Notification] = []
        self._notification_event = asyncio.Event()
        self._resource_read_cancel = asyncio.Event()
        self._client: BleakClient | None = None
        self._notify_started = False
        self._sequence = 0
        self._v2_transaction = 0
        self._v2_write_payload = 20
        self._v2_capabilities = 0
        self._v2_mode_mask = 0
        self._v2_week_start = 1
        self._v2_tests: frozenset[int] | None = None
        self._v2_pending: dict[int, tuple[int, asyncio.Future[bytes]]] = {}
        self._v2_completions: dict[int, tuple[int, asyncio.Future[bytes]]] = {}

    def supports_capability(self, capability: CapabilityV2) -> bool:
        """Return whether the connected V2 device advertised a capability."""
        return bool(self._v2_capabilities & int(capability))

    def _require_indicator(self) -> None:
        if not self.supports_capability(CapabilityV2.INDICATOR):
            raise InkTagError("Firmware does not advertise indicator configuration")

    async def get_indicator_config(self) -> dict[str, Any]:
        self._require_indicator()
        return decode_indicator_config(
            await self.request_v2(CommandV2.GET_INDICATOR_CONFIG)
        )

    async def set_indicator_policy(self, policy: str) -> dict[str, Any]:
        self._require_indicator()
        if policy not in {"all", "critical", "off"}:
            raise InkTagError("Indicator preset must be all, critical or off")
        response = await self.request_v2(
            CommandV2.SET_INDICATOR_CONFIG,
            encode_indicator_config({"policy": policy}),
        )
        return decode_indicator_config(response)

    async def __aenter__(self) -> "InkTagClient":
        await self.connect()
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        await self.disconnect()

    async def connect(self) -> None:
        self._resource_read_cancel.set()
        self._resource_read_cancel = asyncio.Event()
        connection_started = time.monotonic()
        device, advertisement = await select_device(
            self.name, self.address, self.scan_timeout, self.phase_sink
        )
        self._phase("device selected", connection_started)
        self.snapshot.name = _advertised_name(device, advertisement) or getattr(device, "name", None) or "InkTag"
        self.snapshot.address = str(device.address)
        cache_modes = (False, True) if sys.platform == "win32" else (False, False)
        for attempt, use_uncached_services in enumerate(cache_modes):
            self._resource_read_cancel.set()
            self._resource_read_cancel = asyncio.Event()
            # A stale WinRT cached-service attempt otherwise consumes the
            # entire caller timeout before the known-good uncached retry even
            # begins. Healthy cached connects on the qualified boards finish
            # well inside eight seconds; keep the full configured budget for
            # the live-table retry.
            attempt_timeout = (
                min(8.0, self.connect_timeout)
                if sys.platform == "win32" and attempt == 0
                else self.connect_timeout
            )
            client_options: dict[str, Any] = {
                "timeout": attempt_timeout,
                "disconnected_callback": self._on_ble_disconnected,
            }
            client_device: Any = device
            if use_uncached_services:
                # InkTag is a mutable peripheral: first flash and protocol
                # development can change its GATT table while retaining a
                # stable address. Use the Windows cache for the normal frozen
                # schema, then force one live table refresh only when the
                # cached service/characteristic check or GATT access fails.
                client_options["winrt"] = {"use_cached_services": False}
                client_options["services"] = {
                    INKTAG_SERVICE_UUID,
                    *OTA_SERVICE_UUIDS.values(),
                }
                client_device = str(device.address)
            self._client = BleakClient(client_device, **client_options)
            try:
                attempt_started = time.monotonic()
                await self._client.connect()
                self._phase(
                    "GATT connected (uncached retry)" if use_uncached_services
                    else "GATT connected (cached services)",
                    attempt_started,
                )
                services = {
                    str(characteristic.uuid).lower()
                    for service in self._client.services
                    for characteristic in service.characteristics
                }
                service_uuids = {str(service.uuid).lower() for service in self._client.services}
                self.snapshot.services = sorted(service_uuids)
                if COMMAND_UUID not in services or VERSION_UUID not in services:
                    raise _IncompleteServiceDiscovery
                version = bytes(await self._client.read_gatt_char(VERSION_UUID))
                self._phase("firmware identity read", attempt_started)
                if not version:
                    raise InkTagError("Firmware version characteristic is empty")
                self.snapshot.protocol_version = version[0]
                if version[0] != PROTOCOL_V2_VERSION:
                    raise InkTagError(
                        f"Firmware protocol 0x{version[0]:02X} is unsupported; "
                        f"InkTag CLI requires protocol 0x{PROTOCOL_V2_VERSION:02X}"
                    )
                if len(version) < 4:
                    raise InkTagError("Firmware version characteristic is incomplete")
                self.snapshot.firmware_version = f"{version[1]}.{version[2]}.{version[3]}"
                await self._client.start_notify(COMMAND_UUID, self._on_notification)
                self._notify_started = True
                if sys.platform == "win32":
                    # WinRT may resolve start_notify before its CCCD write has
                    # reached the peripheral. Keep the first V2 handshake
                    # behind a bounded settling delay; later commands remain
                    # immediate.
                    await asyncio.sleep(0.5)
                await self._initialize_v2()
                self._phase("V2 handshake complete", attempt_started)
                health_supported = bool(self._v2_capabilities & int(CapabilityV2.HEALTH))
                health = None
                if health_supported:
                    try:
                        health = await self.health()
                        self._report_new_crashes(health)
                    except InkTagError:
                        # HEALTH is additive. Old or malformed firmware must
                        # still remain usable, but an unavailable power result
                        # suppresses the optional Flash-backed clock write.
                        pass
                password_ready = not self._password_fault and (self.password_status is None or not self.password_status["enabled"] or self.password_status["authorized"])
                if self.auto_sync_time and password_ready:
                    if not health_supported or (health is not None and health["flashAllowed"]):
                        try:
                            await self.sync_time()
                        except InkTagError as error:
                            # Connection-time synchronization is opportunistic.
                            # A low-power or temporarily unavailable write path
                            # must not discard an otherwise usable read-only link.
                            if self.phase_sink:
                                self.phase_sink(f"clock sync skipped: {error}")
                await self._refresh_v2_status()
                self._phase("connection ready", connection_started)
                return
            except (BleakError, OSError, EOFError, TimeoutError, _IncompleteServiceDiscovery) as error:
                if self.phase_sink:
                    mode = "uncached" if use_uncached_services else "cached"
                    self.phase_sink(f"GATT {mode} attempt failed: {error}")
                await self.disconnect()
                if attempt == 0:
                    await asyncio.sleep(0.4)
                    continue
                if isinstance(error, _IncompleteServiceDiscovery):
                    raise InkTagError("Selected device does not expose the InkTag application service") from None
                raise
            except BaseException:
                await self.disconnect()
                raise

    def _report_new_crashes(self, health: dict[str, Any]) -> None:
        if self.crash_warning_sink is None:
            return
        device_id = self.snapshot.ble_address or self.snapshot.address or self.snapshot.name
        try:
            warning = crash_warning(device_id, health)
        except OSError:
            return
        if warning is None:
            return
        self.crash_warning_sink(warning)
        try:
            acknowledge_crashes(device_id, int(health["totalCrashes"]))
        except OSError:
            # A read-only or temporarily unavailable client data directory
            # must not make a valid BLE connection fail. The warning will be
            # shown again next time because it was not durably acknowledged.
            pass

    def _phase(self, label: str, started: float) -> None:
        if self.phase_sink:
            self.phase_sink(f"{label}: elapsed={time.monotonic() - started:.3f}s")

    def _on_ble_disconnected(self, client: BleakClient) -> None:
        if client is self._client:
            self._resource_read_cancel.set()
            self._reject_v2_transactions("Device disconnected")

    async def disconnect(self) -> None:
        self._resource_read_cancel.set()
        self._reject_v2_transactions("Device disconnected")
        self._v2_tests = None
        client, self._client = self._client, None
        notify_started, self._notify_started = self._notify_started, False
        if client:
            if client.is_connected and notify_started:
                try:
                    await client.stop_notify(COMMAND_UUID)
                except (BleakError, OSError, EOFError, KeyError):
                    pass
            # WinRT can own a controller link while Bleak still reports
            # is_connected=False because service discovery failed. Always ask
            # the backend to tear it down so a failed attempt cannot suppress
            # the peripheral's advertisements until supervision timeout.
            #
            # Bleak 3.0's WinRT backend sets GattSession.maintain_connection
            # while connecting, but closes the session without first clearing
            # that flag.  Windows may consequently keep the physical link
            # alive after the Python client has gone away.  Clear it
            # defensively before the normal public disconnect call; the
            # attribute walk is intentionally optional so this stays a no-op
            # on Linux/macOS and with future Bleak backends.
            try:
                await release_bleak_client(client)
            except (BleakError, OSError, EOFError):
                pass

    def _on_notification(self, _sender: Any, data: bytearray) -> None:
        self._on_v2_notification(bytes(data))

    def _record_v2_notification(self, data: bytes, text: str | None = None) -> None:
        self._sequence += 1
        notification = Notification(self._sequence, time.monotonic(), data, text=text)
        self.notifications.append(notification)
        if len(self.notifications) > 1000:
            del self.notifications[:200]
        self._notification_event.set()
        if self.notification_sink:
            self.notification_sink(notification)

    def _on_v2_notification(self, data: bytes) -> None:
        if len(data) < 3:
            return
        opcode, transaction_id, status = data[:3]
        if opcode >= int(EventV2.READY):
            if transaction_id != 0 or len(data) < 5:
                return
            origin_opcode, origin_transaction = data[3:5]
            payload = data[5:]
            text = None
            if opcode == int(EventV2.LOG):
                text = payload.decode("utf-8", errors="replace")
            elif opcode == int(EventV2.COMPLETE):
                text = f"v2:complete command=0x{origin_opcode:02x} status={status}"
                completion = self._v2_completions.get(origin_transaction)
                if completion and completion[0] != origin_opcode:
                    text = (
                        f"v2:unmatched completion command=0x{origin_opcode:02x} "
                        f"expected=0x{completion[0]:02x}"
                    )
                elif completion:
                    self._v2_completions.pop(origin_transaction, None)
                    future = completion[1]
                    if future.done():
                        self._record_v2_notification(data, text)
                        return
                    if status == int(StatusV2.OK):
                        future.set_result(payload)
                    else:
                        future.set_exception(self._v2_error(status, origin_opcode))
            elif status != int(StatusV2.OK):
                text = f"v2:event=0x{opcode:02x} command=0x{origin_opcode:02x} status={status}"
            self._record_v2_notification(data, text)
            return

        pending = self._v2_pending.pop(transaction_id, None)
        if pending is None:
            self._record_v2_notification(data, f"v2:unmatched command=0x{opcode:02x}")
            return
        expected_opcode, future = pending
        if future.done():
            return
        if opcode != expected_opcode:
            future.set_exception(
                InkTagError(
                    f"V2 response mismatch: expected 0x{expected_opcode:02X}, received 0x{opcode:02X}"
                )
            )
        elif status != int(StatusV2.OK):
            future.set_exception(self._v2_error(status, opcode))
        else:
            future.set_result(data[3:])
        if self.supports_capability(CapabilityV2.DEVICE_PASSWORD):
            self._record_v2_notification(data[:3], f"v2:response command=0x{opcode:02x} (payload redacted)")
        else:
            self._record_v2_notification(data)

    @staticmethod
    def _v2_error(status: int, opcode: int) -> InkTagError:
        label = _V2_STATUS_LABELS.get(status, f"status 0x{status:02X}")
        if status == int(StatusV2.BUSY):
            label += "; request was not queued"
        return InkTagV2StatusError(
            status, opcode, f"V2 command 0x{opcode:02X} failed: {label}"
        )

    def _next_v2_transaction(self) -> int:
        for _ in range(255):
            self._v2_transaction = 1 if self._v2_transaction >= 255 else self._v2_transaction + 1
            if self._v2_transaction not in self._v2_pending and self._v2_transaction not in self._v2_completions:
                return self._v2_transaction
        raise InkTagError("No V2 transaction IDs are available")

    async def request_v2(
        self,
        opcode: CommandV2 | int,
        payload: bytes = b"",
        *,
        flags: RequestFlagV2 | int = RequestFlagV2.NONE,
        timeout: float = 10.0,
        transaction_id: int | None = None,
        response: bool = True,
    ) -> bytes:
        if self._password_fault and opcode not in (
            CommandV2.HELLO, CommandV2.GET_CAPABILITIES,
            CommandV2.GET_DEVICE_INFO, CommandV2.GET_SCREEN_PROFILE,
            CommandV2.GET_STATUS, CommandV2.GET_TIME, CommandV2.GET_HEALTH,
            CommandV2.GET_ACTIVATION_STATUS, CommandV2.GET_SECURITY_INFO,
            CommandV2.GET_DEVICE_PASSWORD_STATUS,
        ):
            raise InkTagError("Password storage state is unknown; only basic read-only diagnostics are allowed")
        if not self._client or not self._client.is_connected:
            raise InkTagError("Device is not connected")
        transaction = transaction_id or self._next_v2_transaction()
        if transaction in self._v2_pending:
            raise InkTagError(f"V2 transaction {transaction} is already waiting for a response")
        future: asyncio.Future[bytes] = asyncio.get_running_loop().create_future()
        self._v2_pending[transaction] = (int(opcode), future)
        packet = bytes((int(opcode), transaction, int(flags))) + payload
        command_started = time.monotonic()
        try:
            if self.phase_sink:
                self.phase_sink(
                    f"TX op=0x{int(opcode):02X} tx={transaction} "
                    f"flags=0x{int(flags):02X} payload={len(payload)}B"
                )
            await self._client.write_gatt_char(COMMAND_UUID, packet, response=response)
            reply = await asyncio.wait_for(asyncio.shield(future), timeout)
            if self.phase_sink:
                self.phase_sink(
                    f"RX op=0x{int(opcode):02X} tx={transaction} "
                    f"payload={len(reply)}B elapsed={time.monotonic() - command_started:.3f}s"
                )
            return reply
        except TimeoutError as exc:
            if self.phase_sink:
                self.phase_sink(
                    f"TIMEOUT op=0x{int(opcode):02X} tx={transaction} "
                    f"elapsed={time.monotonic() - command_started:.3f}s"
                )
            raise InkTagV2TimeoutError(
                f"Timed out waiting for V2 command 0x{int(opcode):02X}"
            ) from exc
        finally:
            pending = self._v2_pending.get(transaction)
            if pending and pending[1] is future:
                self._v2_pending.pop(transaction, None)

    def _prepare_v2_completion(
        self, transaction_id: int, opcode: CommandV2 | int
    ) -> asyncio.Future[bytes]:
        previous = self._v2_completions.pop(transaction_id, None)
        if previous and not previous[1].done():
            previous[1].set_exception(InkTagError("V2 completion waiter was replaced"))
        future: asyncio.Future[bytes] = asyncio.get_running_loop().create_future()
        self._v2_completions[transaction_id] = (int(opcode), future)
        return future

    async def _write_v2_transfer_chunk(
        self,
        *,
        transaction_id: int,
        object_type: int,
        total_size: int,
        offset: int,
        data: bytes,
        decoded_length: int | None = None,
        timeout: float = 12.0,
    ) -> None:
        """Write one sequential chunk and recover only an unobserved ACK."""
        logical_length = len(data) if decoded_length is None else decoded_length
        end = offset + logical_length
        payload = struct.pack("<I", offset)
        if decoded_length is not None:
            payload += struct.pack("<H", decoded_length)
        payload += data
        for attempt in range(3):
            try:
                await self.request_v2(
                    CommandV2.TRANSFER_WRITE,
                    payload,
                    transaction_id=transaction_id,
                    timeout=timeout,
                )
                return
            except InkTagV2TimeoutError as timeout_error:
                status = await self.request_v2(
                    CommandV2.GET_TRANSFER_STATUS,
                    transaction_id=transaction_id,
                    timeout=5.0,
                )
                if len(status) != 11:
                    raise InkTagError("Device returned an invalid transfer status") from timeout_error
                active, active_object, active_transaction = status[:3]
                written, reported_total = struct.unpack_from("<II", status, 3)
                if (active != 1 or active_object != object_type or
                        active_transaction != transaction_id or
                        reported_total != total_size):
                    raise InkTagError("Device transfer state changed after a lost ACK") from timeout_error
                if written == end:
                    return
                if written != offset or attempt == 2:
                    raise InkTagError(
                        f"Device transfer offset is {written}, expected {offset} or {end}"
                    ) from timeout_error
        raise InkTagError("Transfer chunk retry exhausted")

    async def _commit_v2_transfer(
        self,
        *,
        transaction_id: int,
        object_type: int,
        total_size: int,
        completion: asyncio.Future[bytes] | None,
        timeout: float,
    ) -> tuple[bytes, bool]:
        """Commit without treating a delayed refresh ACK as a failed transfer.

        Some ports execute a synchronous panel waveform after accepting COMMIT,
        so the response notification may not reach the host until the refresh
        has completed.  A timeout is therefore ambiguous: query the transfer
        state and, for display transfers, require the independent completion
        event instead of retransmitting COMMIT or aborting the accepted work.
        """
        try:
            reply = await self.request_v2(
                CommandV2.TRANSFER_COMMIT,
                transaction_id=transaction_id,
                timeout=max(15.0, timeout),
            )
            return reply, False
        except InkTagV2TimeoutError as timeout_error:
            status = await self.request_v2(
                CommandV2.GET_TRANSFER_STATUS,
                transaction_id=transaction_id,
                timeout=5.0,
            )
            if len(status) != 11:
                raise InkTagError("Device returned an invalid transfer status") from timeout_error
            active, active_object, active_transaction = status[:3]
            written, reported_total = struct.unpack_from("<II", status, 3)
            if active not in (0, 1):
                raise InkTagError("Device returned an invalid transfer activity state") from timeout_error
            if active == 1 and (
                active_object != object_type or
                active_transaction != transaction_id or
                written != total_size or
                reported_total != total_size
            ):
                raise InkTagError("Device transfer state changed after a lost commit ACK") from timeout_error
            if completion is None:
                raise InkTagError("Transfer commit ACK was lost before completion could be verified") from timeout_error
            await self._await_v2_completion(transaction_id, completion, max(15.0, timeout))
            return b"", True

    async def _await_v2_completion(
        self, transaction_id: int, future: asyncio.Future[bytes], timeout: float
    ) -> bytes:
        try:
            return await asyncio.wait_for(asyncio.shield(future), timeout)
        except TimeoutError as exc:
            raise InkTagError("Timed out waiting for the device to finish V2 work") from exc
        finally:
            current = self._v2_completions.get(transaction_id)
            if current and current[1] is future:
                self._v2_completions.pop(transaction_id, None)

    def _reject_v2_transactions(self, reason: str) -> None:
        for _opcode, future in self._v2_pending.values():
            if not future.done():
                future.set_exception(InkTagError(reason))
        self._v2_pending.clear()
        for _opcode, future in self._v2_completions.values():
            if not future.done():
                future.set_exception(InkTagError(reason))
        self._v2_completions.clear()

    async def _initialize_v2(self, *, authenticate_session: bool = True) -> None:
        # A GR target can remain connected while a full e-paper waveform blocks
        # its main loop. Wait through that bounded operation instead of reporting
        # a false protocol failure immediately after a profile or mode change.
        hello = await self.request_v2(CommandV2.HELLO, timeout=45.0)
        if len(hello) < 6 or hello[0] != 1 or hello[1] != PROTOCOL_V2_VERSION:
            raise InkTagError("Device returned an invalid V2 handshake")
        _frame_size, self._v2_write_payload = struct.unpack_from("<HH", hello, 2)
        self._v2_write_payload = max(20, self._v2_write_payload)

        capabilities = await self.request_v2(CommandV2.GET_CAPABILITIES)
        if len(capabilities) < 17 or capabilities[0] != 1:
            raise InkTagError("Device returned invalid V2 capabilities")
        self._v2_capabilities = struct.unpack_from("<I", capabilities, 1)[0]
        self._v2_mode_mask = struct.unpack_from("<H", capabilities, 5)[0]
        display_action_mask = struct.unpack_from("<H", capabilities, 7)[0]
        name_max = capabilities[13]
        label_max = capabilities[14]
        nfc_action_mask = 0
        button_action_mask = 0
        button_gesture_mask = 0
        features = 0
        if self._v2_capabilities & int(CapabilityV2.DEVICE_NAME):
            features |= 0x01
        if self._v2_mode_mask & (1 << int(DisplayModeV2.DEVICE_INFO)):
            features |= 0x04
        if self._v2_capabilities & int(CapabilityV2.DEVICE_LABEL):
            features |= 0x80
        if self._v2_capabilities & int(CapabilityV2.DEVICE_INFO_QR):
            features |= 0x08
        if self._v2_capabilities & int(CapabilityV2.IO):
            features |= 0x10 | 0x20
        if self._v2_capabilities & int(CapabilityV2.INDICATOR):
            features |= 0x40
        self.snapshot.settings_capabilities = SettingsCapabilities(
            protocol_version=2,
            features=features,
            bluetooth_name_max_bytes=name_max,
            device_label_max_bytes=label_max,
            nfc_action_mask=nfc_action_mask,
            button_action_mask=button_action_mask,
            button_gesture_mask=button_gesture_mask,
            display_action_mask=display_action_mask,
            device_info_layout_mask=0x07 if features & 0x04 else 0,
        )

        info = await self.request_v2(CommandV2.GET_DEVICE_INFO)
        if len(info) < 23 or info[0] != 1:
            raise InkTagError("Device returned invalid V2 identity information")
        board_length, name_length, label_length = info[1:4]
        strings_end = 23 + board_length + name_length + label_length
        if strings_end > len(info):
            raise InkTagError("Device returned invalid V2 identity lengths")
        offset = 23
        self.snapshot.board_id = canonical_board_id(
            info[offset : offset + board_length].decode("ascii", errors="replace")
        )
        offset += board_length
        self.snapshot.device_name = info[offset : offset + name_length].decode("utf-8", errors="replace")
        offset += name_length
        self.snapshot.device_label = info[offset : offset + label_length].decode("utf-8", errors="replace")
        if info[4]:
            self.snapshot.ble_address = ":".join(f"{byte:02X}" for byte in info[5:11])
        self.snapshot.application_version = struct.unpack_from("<I", info, 11)[0]
        self.snapshot.application_size = struct.unpack_from("<I", info, 15)[0]
        self.snapshot.application_checksum = struct.unpack_from("<I", info, 19)[0]

        screen = decode_screen_config(await self.request_v2(CommandV2.GET_SCREEN_PROFILE))
        if screen is None:
            raise InkTagError("Device returned an invalid V2 screen profile")
        self.snapshot.screen = screen
        self.snapshot.profile = screen_profile_name(screen)
        device_time = await self.request_v2(CommandV2.GET_TIME)
        if len(device_time) >= 7 and device_time[6] <= 6:
            self._v2_week_start = device_time[6]
        if self._v2_capabilities & int(CapabilityV2.ACTIVATION | CapabilityV2.DEVICE_PASSWORD):
            await self.activation_status()
        try:
            if self.require_password_authentication and authenticate_session:
                await self._password_connect()
            else:
                await self.read_password_status()
        except InkTagV2StatusError as error:
            if not (self.allow_password_fault_diagnostics and error.status == 0x0C
                    and error.opcode == int(CommandV2.GET_DEVICE_PASSWORD_STATUS)):
                raise
            # Unknown is never OFF. Retain only the nonsecret diagnostic link;
            # do not prompt, use a cached credential, or attempt a clock write.
            self.password_status = None
            self._password_fault = True
            self.snapshot.password_diagnostic = "PERSISTENCE_ERROR: password state unknown; read-only diagnostics only"
            if self.password_fault_sink:
                self.password_fault_sink(self.snapshot.password_diagnostic)
        password_ready = not self._password_fault and (self.password_status is None or
                          not self.password_status["enabled"] or
                          self.password_status["authorized"])
        if self.require_password_authentication and not password_ready and not self._password_fault:
            raise InkTagError("Device password required; disconnecting. Use a prompt or --password-stdin")
        if password_ready and self._v2_capabilities & int(CapabilityV2.INPUT):
            input_config = await self.request_v2(CommandV2.GET_INPUT_CONFIG)
            if len(input_config) not in (10, 14) or input_config[0] != 1:
                raise InkTagError("Device returned invalid V2 input configuration")
            self.snapshot.settings_capabilities = replace(
                self.snapshot.settings_capabilities,
                button_gesture_mask=input_config[1],
                button_action_mask=struct.unpack_from("<H", input_config, 2)[0],
                nfc_action_mask=(struct.unpack_from("<H", input_config, 10)[0]
                                 if len(input_config) == 14 else 0),
            )

    async def activation_status(self):
        # Restricted rescue can expose password identity without allowing
        # activation writes. AUTH still needs the live immutable device ID.
        if not self._v2_capabilities & int(CapabilityV2.ACTIVATION | CapabilityV2.DEVICE_PASSWORD):
            raise InkTagError("Device does not support activation")
        status = parse_status(await self.request_v2(CommandV2.GET_ACTIVATION_STATUS))
        self.snapshot.activation = status
        return status

    def _require_find(self) -> bool:
        production = self.supports_capability(CapabilityV2.FIND)
        experimental = self.supports_capability(CapabilityV2.EXPERIMENTAL_FIND)
        if not production and not experimental:
            raise InkTagError("Device does not support Find")
        return experimental and not production

    async def find_status(self) -> dict[str, Any]:
        experimental = self._require_find()
        capabilities = decode_find_capabilities(
            await self.request_v2(CommandV2.GET_FIND_CAPABILITIES)
        )
        config = decode_find_config(await self.request_v2(CommandV2.GET_FIND_CONFIG))
        result = {
            "experimental": experimental or capabilities["experimental"],
            "capabilities": capabilities,
            "config": config,
        }
        if capabilities.get("ownerRotation") or capabilities.get("publicOnlyP224"):
            result["owner"] = decode_owner_rotation_status(
                await self.request_v2(CommandV2.GET_FIND_OWNER_STATUS)
            )
        return result

    async def configure_find(
        self, enabled: bool, *, allow_experimental: bool = False,
        owner_proof: Callable[[bytes, bytes], Awaitable[bytes]] | None = None,
    ) -> dict[str, Any]:
        experimental = self._require_find()
        if enabled and experimental and not allow_experimental:
            raise InkTagError("Experimental Find requires --allow-experimental")
        current = decode_find_config(await self.request_v2(CommandV2.GET_FIND_CONFIG))
        if enabled:
            capabilities = decode_find_capabilities(
                await self.request_v2(CommandV2.GET_FIND_CAPABILITIES)
            )
            if not capabilities.get("publicOnlyP224"):
                raise InkTagError("Find v1 is legacy; update device firmware to Find v2")
            if not current["identityPresent"]:
                raise InkTagError("Find has no v2 identity; register it through the owner transaction before enabling")
        request = encode_find_config(enabled)
        flags = (RequestFlagV2.CONFIRM
                 if enabled and experimental else RequestFlagV2.NONE)
        if current["enabled"] and not enabled:
            await self._authorize_owner_action(
                CommandV2.SET_FIND_CONFIG, flags, request, owner_proof
            )
        return decode_find_config(
            await self.request_v2(
                CommandV2.SET_FIND_CONFIG,
                request, flags=flags,
                timeout=20.0,
            )
        )

    async def begin_find_owner_rotation(self) -> bytes:
        status = await self.find_status()
        capabilities = status["capabilities"]
        if not capabilities.get("publicOnlyP224"):
            raise InkTagError("Find v1 is legacy; update device firmware to Find v2")
        if (status["config"]["identityPresent"] and
                not capabilities.get("publicOnlyOwnerReplacement")):
            raise InkTagError("Device does not support authenticated Find v2 owner replacement")
        payload = await self.request_v2(
            CommandV2.BEGIN_FIND_OWNER_ROTATION,
            flags=RequestFlagV2.CONFIRM,
        )
        if payload[:1] != bytes((PUBLIC_ONLY_OWNER_VERSION,)):
            raise InkTagError("Device returned a non-v2 Find owner challenge")
        return decode_owner_rotation_challenge(payload)

    async def find_owner_status(self) -> dict[str, object]:
        status = await self.find_status()
        if not (status["capabilities"].get("ownerRotation") or
                status["capabilities"].get("publicOnlyP224")):
            raise InkTagError("Device does not support secure Find owner rotation")
        return decode_owner_status(await self.request_v2(CommandV2.GET_FIND_OWNER_STATUS))

    async def stage_public_only_find_owner(self, payload: bytes, nonce: bytes) -> None:
        if len(payload) != 81 or payload[0] != PUBLIC_ONLY_OWNER_VERSION or payload[1:9] != nonce:
            raise InkTagError("Public-only Find stage must match the 81-byte v2 owner challenge")
        await self.request_v2(CommandV2.STAGE_FIND_OWNER_ROTATION, payload)

    async def stage_public_only_find_owner_replacement(
        self, payload: bytes, nonce: bytes, current_fingerprint: bytes,
    ) -> None:
        if (len(payload) != 113 or payload[0] != PUBLIC_ONLY_OWNER_VERSION or
                payload[1:9] != nonce or payload[9:25] != current_fingerprint):
            raise InkTagError("Find v2 replacement stage does not match this device challenge and owner")
        await self.request_v2(CommandV2.STAGE_FIND_OWNER_ROTATION, payload)

    async def commit_find_owner_rotation(self) -> dict[str, object]:
        payload = await self.request_v2(
            CommandV2.COMMIT_FIND_OWNER_ROTATION,
            flags=RequestFlagV2.CONFIRM,
            timeout=20.0,
        )
        if payload[:1] != bytes((PUBLIC_ONLY_OWNER_VERSION,)):
            raise InkTagError("Device returned a non-v2 Find owner proof")
        return decode_owner_rotation_proof(payload)

    async def abort_find_owner_rotation(self) -> None:
        await self.request_v2(CommandV2.ABORT_FIND_OWNER_ROTATION)

    async def health(self, record_index: int = 0, *, timeout: float = 10.0) -> dict[str, Any]:
        if not self._v2_capabilities & int(CapabilityV2.HEALTH):
            raise InkTagError("Device does not support health diagnostics")
        if not 0 <= record_index <= 3:
            raise InkTagError("Crash record index must be between 0 and 3")
        payload = await self.request_v2(CommandV2.GET_HEALTH, bytes((record_index,)), timeout=timeout)
        if len(payload) < 56 or payload[0] != 1 or payload[1] > 3:
            raise InkTagError("Device returned invalid health diagnostics")
        flags = payload[2]
        stage_value = struct.unpack_from("<I", payload, 36)[0]
        stage_bytes = stage_value.to_bytes(4, "big")
        stage = stage_bytes.decode("ascii") if all(0x20 <= byte <= 0x7E for byte in stage_bytes) else f"0x{stage_value:08X}"
        record_count = payload[20]
        crash = None
        if record_count and payload[23]:
            crash = {
                "reason": payload[23], "resetReason": payload[24], "operation": payload[25],
                "voltageMv": struct.unpack_from("<H", payload, 28)[0],
                "uptimeMs": struct.unpack_from("<I", payload, 32)[0], "stage": stage,
                "pc": f"0x{struct.unpack_from('<I', payload, 40)[0]:08X}",
                "lr": f"0x{struct.unpack_from('<I', payload, 44)[0]:08X}",
                "cfsr": f"0x{struct.unpack_from('<I', payload, 48)[0]:08X}",
                "hfsr": f"0x{struct.unpack_from('<I', payload, 52)[0]:08X}",
            }
        soak = None
        if (len(payload) >= 108 and payload[56:59] == b"IS\x01" and
                payload[59] >= 52 and len(payload) >= 56 + payload[59]):
            soak_flags = struct.unpack_from("<H", payload, 60)[0]
            find_available = bool(soak_flags & 0x0001)
            read_if = lambda flag, offset: (struct.unpack_from("<I", payload, offset)[0]
                                             if soak_flags & flag else None)
            soak = {
                "version": payload[58], "saturated": bool(soak_flags & 0x8000),
                "findActive": bool(soak_flags & 0x0100),
                "uptimeMs": read_if(0x0010, 64),
                "connectionSessions": read_if(0x0002, 68),
                "findRequests": struct.unpack_from("<I", payload, 72)[0] if find_available else None,
                "findStarts": struct.unpack_from("<I", payload, 76)[0] if find_available else None,
                "findCompletions": struct.unpack_from("<I", payload, 80)[0] if find_available else None,
                "findStartFailures": struct.unpack_from("<I", payload, 84)[0] if find_available else None,
                "findStopFailures": struct.unpack_from("<I", payload, 88)[0] if find_available else None,
                "findConnectionPauses": struct.unpack_from("<I", payload, 92)[0] if find_available else None,
                "findActiveMs": struct.unpack_from("<I", payload, 96)[0] if find_available else None,
                "radioMaintenanceTurns": read_if(0x0004, 100),
                "sleepEntries": read_if(0x0008, 104),
            }
        return {
            "powerState": ("unknown", "normal", "warning", "critical")[payload[1]],
            "resetReason": payload[3],
            "voltageMv": struct.unpack_from("<H", payload, 4)[0],
            "voltageValid": bool(flags & 0x01), "displayAllowed": bool(flags & 0x02),
            "flashAllowed": bool(flags & 0x04), "otaAllowed": bool(flags & 0x08),
            "watchdogActive": bool(flags & 0x10), "warningPageShown": bool(flags & 0x40),
            "thresholdsQualified": not bool(flags & 0x80),
            "warningMv": struct.unpack_from("<H", payload, 6)[0],
            "displayMinMv": struct.unpack_from("<H", payload, 8)[0],
            "flashMinMv": struct.unpack_from("<H", payload, 10)[0],
            "otaMinMv": struct.unpack_from("<H", payload, 12)[0],
            "recoveryHysteresisMv": struct.unpack_from("<H", payload, 14)[0],
            "totalCrashes": struct.unpack_from("<I", payload, 16)[0],
            "crashRecordCount": record_count,
            "consecutiveCrashes": payload[22], "latestCrash": crash,
            "soakTelemetry": soak,
        }

    async def activation_request(self, *, force: bool = False):
        if not self._v2_capabilities & int(CapabilityV2.ACTIVATION):
            raise InkTagError("Device does not support activation")
        flags = RequestFlagV2.FORCE if force else RequestFlagV2.NONE
        return parse_request(await self.request_v2(
            CommandV2.GET_ACTIVATION_REQUEST,
            flags=flags,
        ))

    async def activate(self, code: str, *, restore: bool = False):
        if not self._v2_capabilities & int(CapabilityV2.ACTIVATION):
            raise InkTagError("Device does not support activation")
        token = decode_code(code)
        if restore:
            # Establish a fresh bounded device request, not a new signing step.
            # Restore is explicit; never retry a normal rejection in this mode.
            await self.activation_request()
            token = bytes((1,)) + token
        status = parse_status(await self.request_v2(CommandV2.SET_ACTIVATION_CODE, token))
        self.snapshot.activation = status
        if status.last_result != int(ActivationResultV2.OK):
            raise InkTagError(f"Activation code was rejected: {status.result_name.replace('_', ' ')}")
        if not status.active:
            raise InkTagError("Activation code was accepted but the device did not enable the entitlement")
        return status

    async def _refresh_v2_status(self) -> bytes:
        payload = await self.request_v2(CommandV2.GET_STATUS)
        if len(payload) < 16 or payload[0] != 1:
            raise InkTagError("Device returned an invalid V2 status")
        flags = payload[2]
        voltage_mv = struct.unpack_from("<H", payload, 8)[0]
        temperature = struct.unpack_from("b", payload, 7)[0]
        self.snapshot.voltage = voltage_mv / 1000 if flags & 0x04 and voltage_mv else None
        self.snapshot.temperature = temperature if flags & 0x08 else None
        (self.snapshot.mcu_temperature,
         self.snapshot.panel_temperature) = parse_temperature_sources(payload)
        return payload

    async def _resource_snapshot(
        self, opcode: CommandV2, payload: bytes = b"", *,
        budget: _ResourceReadBudget | None = None,
    ) -> bytes:
        """Retry only explicit rejected metadata snapshots, never mutations."""
        if opcode not in (CommandV2.GET_RESOURCE_INFO, CommandV2.LIST_RESOURCES):
            raise InkTagError("Command is not eligible for resource snapshot retry")
        budget = budget or _ResourceReadBudget(time.monotonic() + _RESOURCE_READ_BUDGET)
        if budget.cancelled is None:
            budget.session, budget.cancelled = self._client, self._resource_read_cancel
        session, cancelled = budget.session, budget.cancelled

        def check_session() -> None:
            if (cancelled.is_set() or self._client is not session or
                    (session is not None and not session.is_connected)):
                raise InkTagError("Resource read cancelled: device disconnected or changed")

        for _ in range(_RESOURCE_READ_MAX_BUSY_RETRIES + 1):
            check_session()
            remaining = budget.deadline - time.monotonic()
            if remaining <= 0:
                raise InkTagError("Resource read wait limit reached; retry later")
            timeout = min(10.0, remaining)
            try:
                # Include GATT write time in the same bound, without extending
                # request_v2's ordinary response timeout.
                result = await asyncio.wait_for(
                    self.request_v2(opcode, payload, timeout=timeout), timeout,
                )
                check_session()
                if time.monotonic() >= budget.deadline:
                    raise InkTagError("Resource read wait limit reached; retry later")
                return result
            except InkTagV2StatusError as error:
                check_session()
                if error.status != int(StatusV2.BUSY) or error.opcode != int(opcode):
                    raise
                if (budget.retries >= _RESOURCE_READ_MAX_BUSY_RETRIES or
                        budget.deadline - time.monotonic() <= _RESOURCE_READ_RETRY_DELAY):
                    raise InkTagError("Resources still busy; retry later") from error
                budget.retries += 1
                if self.phase_sink and budget.retries == 1:
                    self.phase_sink("Reading resource metadata: device busy; bounded retry")
                try:
                    await asyncio.wait_for(cancelled.wait(), _RESOURCE_READ_RETRY_DELAY)
                except TimeoutError:
                    pass
                check_session()
            except TimeoutError as error:
                raise InkTagV2TimeoutError(
                    f"Timed out waiting for V2 command 0x{int(opcode):02X}"
                ) from error
        raise InkTagError("Resource read retry limit reached")

    async def _v2_resource_info(self, budget: _ResourceReadBudget | None = None) -> ImageStoreInfo:
        payload = await self._resource_snapshot(CommandV2.GET_RESOURCE_INFO, budget=budget)
        if len(payload) < 30 or payload[0] != 1:
            raise InkTagError("Device returned invalid V2 image-store information")
        required = struct.unpack_from("<H", payload, 9)[0]
        free = struct.unpack_from("<H", payload, 13)[0]
        largest = struct.unpack_from("<H", payload, 15)[0]
        allocatable = (
            struct.unpack_from("<H", payload, 30)[0]
            if len(payload) >= 32
            else largest // required if required else 0
        )
        return ImageStoreInfo(
            width=struct.unpack_from("<H", payload, 1)[0],
            height=struct.unpack_from("<H", payload, 3)[0],
            bytes_per_image=struct.unpack_from("<I", payload, 5)[0],
            required_sectors=required,
            total_sectors=struct.unpack_from("<H", payload, 11)[0],
            free_sectors=free,
            largest_free_run=largest,
            fit=free // required if required else 0,
            contiguous_fit=allocatable,
            saved=struct.unpack_from("<H", payload, 17)[0],
            compatible=struct.unpack_from("<H", payload, 19)[0],
            playable=struct.unpack_from("<H", payload, 21)[0],
            incompatible=struct.unpack_from("<H", payload, 23)[0],
            slideshow=payload[25] != 0,
            interval_seconds=struct.unpack_from("<I", payload, 26)[0],
        )

    async def _run_v2_display(
        self, mode: DisplayModeV2 | int, payload: bytes = b"", *, timeout: float = 100.0,
        after_ack: Callable[[asyncio.Future[bytes]], Awaitable[None]] | None = None,
    ) -> bytes:
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.SET_DISPLAY_MODE)
        try:
            await self.request_v2(
                CommandV2.SET_DISPLAY_MODE,
                bytes((int(mode),)) + payload,
                transaction_id=transaction,
            )
            deadline = time.monotonic() + timeout
            if after_ack is not None:
                await after_ack(completion)
            return await self._await_v2_completion(
                transaction, completion, max(0.0, deadline - time.monotonic())
            )
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise

    async def wait_for(
        self,
        predicate: Callable[[Notification], bool],
        *,
        after: int,
        timeout: float,
        description: str,
    ) -> Notification:
        deadline = time.monotonic() + timeout
        while True:
            self._notification_event.clear()
            for notification in self.notifications:
                if notification.sequence > after and predicate(notification):
                    return notification
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise InkTagError(f"Timed out waiting for {description}")
            try:
                await asyncio.wait_for(self._notification_event.wait(), remaining)
            except TimeoutError as exc:
                raise InkTagError(f"Timed out waiting for {description}") from exc

    async def collect_until_idle(self, *, after: int, idle: float = 0.35, timeout: float = 2.5) -> list[Notification]:
        deadline = time.monotonic() + timeout
        last_count = len(self.notifications)
        while time.monotonic() < deadline:
            self._notification_event.clear()
            try:
                await asyncio.wait_for(self._notification_event.wait(), min(idle, deadline - time.monotonic()))
            except TimeoutError:
                if len(self.notifications) == last_count:
                    break
            last_count = len(self.notifications)
        return [item for item in self.notifications if item.sequence > after]

    async def refresh_snapshot(self) -> DeviceSnapshot:
        # Refresh the same logical client, not a new connection. New connects
        # must still prove their own credential even if Windows retains LL.
        await self._initialize_v2(authenticate_session=False)
        await self._refresh_v2_status()
        return self.snapshot

    async def sync_time(self, mode: int | None = None) -> None:
        current = datetime.now().astimezone()
        offset = current.utcoffset()
        timezone_minutes = int((offset.total_seconds() if offset else 0) // 60)
        if not -32768 <= timezone_minutes <= 32767:
            raise ValueError("timezone offset does not fit the V2 protocol")
        await self.request_v2(
            CommandV2.SET_TIME,
            struct.pack("<IhB", int(current.timestamp()), timezone_minutes, self._v2_week_start),
        )
        if mode is not None:
            display_modes = {1: DisplayModeV2.CALENDAR, 2: DisplayModeV2.CLOCK}
            if mode not in display_modes:
                raise ValueError("display time mode must be 1 (calendar) or 2 (clock)")
            await self._run_v2_display(display_modes[mode])

    async def read_time(self, *, timeout: float = 10.0) -> dict[str, int]:
        """Read the device clock without changing it or triggering a redraw."""
        response = await self.request_v2(CommandV2.GET_TIME, timeout=timeout)
        if len(response) != 7 or response[6] > 6:
            raise InkTagError("Device returned an invalid V2 time response")
        return {
            "utcTimestamp": struct.unpack_from("<I", response, 0)[0],
            "timezoneMinutes": struct.unpack_from("<h", response, 4)[0],
            "weekStart": response[6],
        }

    async def _run_v2_test(self, payload: bytes, *, timeout: float = 100.0) -> bytes:
        if not self._v2_capabilities & int(CapabilityV2.TEST):
            raise InkTagError("Device does not advertise V2 test operations")
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.RUN_TEST)
        try:
            response = await self.request_v2(
                CommandV2.RUN_TEST,
                payload,
                transaction_id=transaction,
            )
            await self._await_v2_completion(transaction, completion, timeout)
            return response
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise

    async def list_v2_tests(self) -> frozenset[int]:
        if not self._v2_capabilities & int(CapabilityV2.TEST):
            raise InkTagError("Device does not advertise V2 test operations")
        if self._v2_tests is not None:
            return self._v2_tests
        response = await self.request_v2(CommandV2.LIST_TESTS)
        if (len(response) < 2 or response[0] != 1 or
                response[1] != len(response) - 2):
            raise InkTagError("Device returned an invalid V2 test list")
        self._v2_tests = frozenset(response[2:])
        return self._v2_tests

    async def _require_v2_test(self, operation: TestOperationV2) -> None:
        if int(operation) not in await self.list_v2_tests():
            raise InkTagError(
                f"Unsupported: firmware did not list {operation.name}"
            )

    @staticmethod
    def _decode_test_time(response: bytes) -> dict[str, int]:
        if len(response) != 7 or response[6] > 6:
            raise InkTagError("Device returned an invalid test-time response")
        return {
            "utcTimestamp": struct.unpack_from("<I", response, 0)[0],
            "timezoneMinutes": struct.unpack_from("<h", response, 4)[0],
            "weekStart": response[6],
        }

    async def test_time_shift(
        self, delta_seconds: int, display_mode: int, *, timeout: float = 100.0
    ) -> dict[str, int]:
        await self._require_v2_test(TestOperationV2.TIME_SHIFT)
        response = await self._run_v2_test(
            encode_v2_time_shift_test(delta_seconds, display_mode), timeout=timeout
        )
        return self._decode_test_time(response)

    async def test_time_set(
        self, when: datetime, display_mode: int, *, timeout: float = 100.0
    ) -> dict[str, int]:
        await self._require_v2_test(TestOperationV2.TIME_SET)
        response = await self._run_v2_test(
            encode_v2_time_set_test(when, display_mode, self._v2_week_start),
            timeout=timeout,
        )
        return self._decode_test_time(response)

    async def get_screen(self) -> ScreenConfig:
        screen = decode_screen_config(await self.request_v2(CommandV2.GET_SCREEN_PROFILE))
        if screen is None:
            raise InkTagError("Device returned an invalid V2 screen profile")
        self.snapshot.screen = screen
        self.snapshot.profile = screen_profile_name(screen)
        return screen

    async def set_screen(self, config: ScreenConfig) -> ScreenConfig:
        screen = decode_screen_config(
            await self.request_v2(CommandV2.SET_SCREEN_PROFILE, encode_screen_config(config))
        )
        if screen is None or screen != config:
            raise InkTagError("Device did not echo the persisted V2 screen profile")
        self.snapshot.screen = screen
        self.snapshot.profile = screen_profile_name(screen)
        return screen

    async def run_display(
        self,
        action: str,
        *,
        sync_time: bool = True,
        timeout: float | None = None,
    ) -> str:
        modes = {
            "device-info": DisplayModeV2.DEVICE_INFO,
            "clear": DisplayModeV2.CLEAR,
            "calendar": DisplayModeV2.CALENDAR,
            "clock": DisplayModeV2.CLOCK,
        }
        if action not in modes:
            raise ValueError(f"unsupported display action: {action}")
        if action in {"calendar", "clock"} and sync_time:
            await self.sync_time()
        extra = b"\x01" if action == "clear" else b""
        effective_timeout = timeout
        if effective_timeout is None:
            screen = self.snapshot.screen
            visible_pixels = (screen.visible_width * screen.visible_height) if screen else 0
            effective_timeout = (
                _SLOW_DISPLAY_TIMEOUT
                if screen and screen.color != 1 and visible_pixels >= _SLOW_DISPLAY_MIN_PIXELS
                else _DEFAULT_DISPLAY_TIMEOUT
            )
        await self._run_v2_display(modes[action], extra, timeout=effective_timeout)
        return f"{action}:done"

    async def display_health(
        self, *, slot: int | str | None = None,
        timeout: float = 100.0, read_timeout: float = 3.0,
    ) -> dict[str, Any]:
        """One refresh, then concurrent read-only probes after its ACK.

        Uses the ordinary display/completion state machine, decoders and same
        authenticated connection. Does not retry a write, synchronize time or
        start a second client. Probe failures still wait for the terminal event.
        """
        if not (1.0 <= read_timeout <= 10.0 and read_timeout <= timeout <= 300.0):
            raise ValueError("Require 1 <= read timeout <= 10 and read timeout <= timeout <= 300")
        if not self.supports_capability(CapabilityV2.HEALTH):
            raise InkTagError("Device does not support health diagnostics")
        slot_id = await self._resolve_visible_slot(slot) if slot is not None else None
        mode = DisplayModeV2.RESOURCE if slot_id is not None else DisplayModeV2.CALENDAR
        started = time.monotonic()
        report: dict[str, Any] = {
            "test": "display-health", "singleConnection": True,
            "action": "slot" if slot_id is not None else "calendar",
            "slotId": slot_id, "readTimeoutSeconds": read_timeout,
            "completionTimeoutSeconds": timeout, "clockWrittenByTest": False,
        }

        async def probes(completion: asyncio.Future[bytes]) -> None:
            report["ackMs"] = round((time.monotonic() - started) * 1000, 3)
            report["completionBeforeAck"] = completion.done()

            def finished(_future: asyncio.Future[bytes]) -> None:
                report["completionObservedMs"] = round((time.monotonic() - started) * 1000, 3)

            if completion.done():
                finished(completion)
            else:
                completion.add_done_callback(finished)

            async def read(name: str, operation: Callable[[], Awaitable[dict[str, Any]]]) -> dict[str, Any]:
                read_started = time.monotonic()
                sample: dict[str, Any] = {"request": name, "pendingAtStart": not completion.done()}
                try:
                    # A second bound also covers unexpected backend stalls.
                    sample["value"] = await asyncio.wait_for(operation(), read_timeout)
                    sample["ok"] = True
                except (InkTagError, BleakError, TimeoutError) as error:
                    sample.update(ok=False, error=str(error) or "Read deadline exceeded")
                sample["elapsedMs"] = round((time.monotonic() - read_started) * 1000, 3)
                sample["pendingAtReply"] = not completion.done()
                return sample

            report["reads"] = await asyncio.gather(
                read("GET_HEALTH", lambda: self.health(timeout=read_timeout)),
                read("GET_TIME", lambda: self.read_time(timeout=read_timeout)),
            )
            report["overlapConfirmed"] = all(
                item["ok"] and item["pendingAtStart"] and item["pendingAtReply"]
                for item in report["reads"]
            )

        try:
            terminal = await self._run_v2_display(
                mode, bytes((slot_id,)) if slot_id is not None else b"",
                timeout=timeout, after_ack=probes,
            )
            report.update(completed=True, completionPayloadHex=terminal.hex())
        except (InkTagError, BleakError) as error:
            report.update(completed=False, error=str(error))
        report["elapsedMs"] = round((time.monotonic() - started) * 1000, 3)
        report["ok"] = bool(report["completed"] and report.get("reads") and
                            all(item["ok"] for item in report["reads"]))
        return report

    async def stop(self, timeout: float = 25.0) -> str:
        await self.request_v2(CommandV2.CANCEL)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = await self._refresh_v2_status()
            if (status[2] & 0x11) == 0:
                return "epd:stopped"
            await asyncio.sleep(0.1)
        raise InkTagError("Timed out waiting for V2 screen work to stop")

    @staticmethod
    def _decode_display_settings(payload: bytes) -> dict[str, int | bool | str]:
        if (
            len(payload) < _DISPLAY_SETTINGS_BASE_SIZE
            or payload[0] != int(SettingsSectionV2.DISPLAY)
            or payload[1] != _DISPLAY_SETTINGS_VERSION
            or payload[2] not in (0, 1)
            or len(payload) != _DISPLAY_SETTINGS_BASE_SIZE + payload[8]
        ):
            raise InkTagError(
                "Device does not expose the complete display-settings v3 record; update firmware"
            )
        target_day = int.from_bytes(payload[4:8], "little")
        try:
            name = payload[_DISPLAY_SETTINGS_BASE_SIZE:].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise InkTagError("Device returned invalid display-settings UTF-8") from exc
        if (
            len(name) > 4
            or len(name.encode("utf-8")) > _DISPLAY_SETTINGS_MAX_NAME_BYTES
            or (target_day == 0) != (name == "")
        ):
            raise InkTagError("Device returned invalid display settings")
        flags = payload[3]
        target_date = ""
        if target_day:
            try:
                target_date = date.fromordinal(date(1970, 1, 1).toordinal() + target_day).isoformat()
            except ValueError as exc:
                raise InkTagError("Device returned an invalid countdown date") from exc
        return {
            "version": payload[1],
            "weekStart": payload[2],
            "bootRefreshEnabled": bool(flags & 0x01),
            "brandVisibilitySupported": True,
            "brandVisible": not bool(flags & 0x10),
            "countdownTargetDay": target_day,
            "countdownName": name,
            "countdownDate": target_date,
        }

    @staticmethod
    def _encode_display_settings(settings: dict[str, int | bool | str]) -> bytes:
        week_start = int(settings["weekStart"])
        if week_start not in (0, 1):
            raise ValueError("week start must be 0 (Sunday) or 1 (Monday)")
        name = str(settings.get("countdownName", "")).strip()
        encoded_name = name.encode("utf-8")
        target_day = int(settings.get("countdownTargetDay", 0))
        if len(name) > 4 or len(encoded_name) > _DISPLAY_SETTINGS_MAX_NAME_BYTES:
            raise ValueError("countdown name must be at most 4 characters / 12 UTF-8 bytes")
        if not 0 <= target_day <= 0xFFFFFFFF or (target_day == 0) != (name == ""):
            raise ValueError("countdown name and date must both be set or both be empty")
        flags = (
            (0x01 if settings.get("bootRefreshEnabled") else 0)
            | (0 if settings.get("brandVisible", True) else 0x10)
        )
        return bytes((
            int(SettingsSectionV2.DISPLAY), _DISPLAY_SETTINGS_VERSION,
            week_start, flags,
        )) + target_day.to_bytes(4, "little") + bytes((len(encoded_name),)) + encoded_name

    async def get_display_settings(self) -> dict[str, int | bool | str]:
        payload = await self.request_v2(
            CommandV2.GET_SETTINGS, bytes((int(SettingsSectionV2.DISPLAY),))
        )
        settings = self._decode_display_settings(payload)
        self._v2_week_start = int(settings["weekStart"])
        return settings

    async def set_display_configuration(
        self, week_start: int, countdown_name: str, countdown_date: str
    ) -> dict[str, int | bool | str]:
        current = await self.get_display_settings()
        name = countdown_name.strip()
        if bool(name) != bool(countdown_date):
            raise ValueError("countdown name and date must both be set or both be empty")
        target_day = 0
        if countdown_date:
            try:
                target = date.fromisoformat(countdown_date)
            except ValueError as exc:
                raise ValueError("countdown date must use YYYY-MM-DD") from exc
            target_day = (target - date(1970, 1, 1)).days
            if target_day <= 0:
                raise ValueError("countdown date is outside the supported range")
        current.update({
            "weekStart": week_start,
            "countdownName": name,
            "countdownTargetDay": target_day,
        })
        response = await self.request_v2(
            CommandV2.SET_SETTINGS, self._encode_display_settings(current)
        )
        confirmed = self._decode_display_settings(response)
        self._v2_week_start = int(confirmed["weekStart"])
        return confirmed

    async def set_brand_visible(self, visible: bool) -> dict[str, int | bool | str]:
        current = await self.get_display_settings()
        if not current["brandVisibilitySupported"]:
            raise InkTagError("Firmware does not support Pro brand visibility")
        current["brandVisible"] = visible
        response = await self.request_v2(
            CommandV2.SET_SETTINGS, self._encode_display_settings(current)
        )
        return self._decode_display_settings(response)

    async def rename(self, name: str) -> str:
        await self.refresh_snapshot()
        capabilities = self.snapshot.settings_capabilities
        if not capabilities.bluetooth_name:
            raise InkTagError("Firmware does not advertise Bluetooth-name configuration")
        encoded_name = encode_bluetooth_name(
            name, capabilities.bluetooth_name_max_bytes or 19
        )
        encoded_label = self.snapshot.device_label.encode("utf-8")
        payload = bytes(
            (SettingsSectionV2.IDENTITY, 1, len(encoded_name), len(encoded_label))
        ) + encoded_name + encoded_label
        await self.request_v2(CommandV2.SET_SETTINGS, payload)
        self.snapshot.device_name = encoded_name.decode("utf-8")
        return "settings:name:done"

    async def set_device_label(self, label: str) -> str:
        await self.refresh_snapshot()
        capabilities = self.snapshot.settings_capabilities
        if not capabilities.device_label:
            raise InkTagError("Firmware does not advertise persistent device-label configuration")
        encoded_label = encode_device_label(
            label, capabilities.device_label_max_bytes or 24
        )
        encoded_name = self.snapshot.device_name.encode("utf-8")
        payload = bytes(
            (SettingsSectionV2.IDENTITY, 1, len(encoded_name), len(encoded_label))
        ) + encoded_name + encoded_label
        await self.request_v2(CommandV2.SET_SETTINGS, payload)
        self.snapshot.device_label = encoded_label.decode("utf-8")
        return "settings:device:done"

    async def write_device_info_qr(self, value: str) -> dict[str, int | str]:
        if not self.supports_capability(CapabilityV2.DEVICE_INFO_QR):
            raise InkTagError("Firmware does not advertise device-information QR transfer")
        qr = encode_device_qr(value)
        descriptor = bytearray(20)
        descriptor[0] = int(TransferObjectV2.DEVICE_INFO_QR)
        descriptor[1] = qr.modules
        descriptor[2] = 1
        descriptor[3] = 0
        crc = zlib.crc32(qr.data)
        struct.pack_into("<IIII", descriptor, 4, len(qr.data), crc,
                         len(qr.data), 0)

        transaction = self._next_v2_transaction()
        transfer_active = False
        try:
            reply = await self.request_v2(
                CommandV2.TRANSFER_BEGIN, bytes(descriptor),
                transaction_id=transaction, timeout=15.0,
            )
            transfer_active = True
            device_chunk = struct.unpack_from("<H", reply, 2)[0] if len(reply) >= 4 else 0
            chunk_size = max(20, min(self._v2_write_payload,
                                     device_chunk or self._v2_write_payload))
            for offset in range(0, len(qr.data), chunk_size):
                await self._write_v2_transfer_chunk(
                    transaction_id=transaction,
                    object_type=int(TransferObjectV2.DEVICE_INFO_QR),
                    total_size=len(qr.data), offset=offset,
                    data=qr.data[offset:offset + chunk_size],
                )
            await self.request_v2(
                CommandV2.TRANSFER_COMMIT,
                transaction_id=transaction, timeout=15.0,
            )
            transfer_active = False
        except BaseException:
            if transfer_active:
                try:
                    await self.request_v2(
                        CommandV2.TRANSFER_ABORT,
                        transaction_id=transaction, timeout=5.0,
                    )
                except BaseException:
                    pass
            raise
        return {
            "value": value.strip(),
            "modules": qr.modules,
            "bytes": len(qr.data),
            "crc32": f"{crc:08X}",
        }

    async def write_nfc_web_link(self, value: str) -> dict[str, int | str]:
        try:
            capabilities = decode_nfc_capabilities(
                await self.request_v2(CommandV2.GET_NFC_CAPABILITIES)
            )
        except InkTagV2StatusError as error:
            if error.status in (
                int(StatusV2.UNKNOWN_COMMAND),
                int(StatusV2.UNSUPPORTED),
            ):
                return {"status": "unsupported", "value": value.strip()}
            raise
        if not int(capabilities["flags"]) & int(NfcCapabilityV2.NDEF_WRITE):
            return {"status": "unsupported", "value": value.strip()}
        ndef = encode_uri_ndef(value)
        await self.request_v2(CommandV2.SET_NFC_NDEF, encode_nfc_ndef(ndef))
        return {"status": "written", "value": value.strip(), "bytes": len(ndef)}

    async def probe_nfc_web_link(self, *, allow_unqualified: bool = False) -> dict[str, Any]:
        """Explicit static-URI pilot only; no arbitrary memory/raw NDEF input."""
        if not allow_unqualified:
            raise InkTagError("NFC NDEF pilot requires explicit --allow-unqualified")
        value = build_device_link(self.snapshot.device_name or self.snapshot.name)
        expected = encode_uri_ndef(value)
        await self.request_v2(CommandV2.SET_NFC_NDEF, encode_nfc_ndef(expected))
        received = decode_nfc_ndef(await self.request_v2(CommandV2.GET_NFC_NDEF))
        if received != expected:
            raise InkTagError("NFC static URL readback differs from the intended URI")
        return {"status": "verified", "value": value, "bytes": len(expected), "qualified": False}

    async def get_gui_asset_info(self) -> dict[str, int | None]:
        if not self.supports_capability(CapabilityV2.GUI_ASSET_UPDATE):
            raise InkTagError("Firmware does not advertise GUI asset updates")
        return decode_gui_asset_info(
            await self.request_v2(CommandV2.GET_GUI_ASSET_INFO)
        )

    async def install_gui_asset_package(
        self, package: bytes,
        progress: Callable[[int, int], None] | None = None,
    ) -> dict[str, Any]:
        if not self.supports_capability(CapabilityV2.GUI_ASSET_UPDATE):
            raise InkTagError("Firmware does not advertise GUI asset updates")
        if not self.snapshot.activation.active:
            raise InkTagError(
                "Owner font replacement requires Pro activation on this device"
            )
        validate_gui_asset_package(package)
        descriptor = bytearray(20)
        descriptor[0] = int(TransferObjectV2.GUI_ASSET_PACKAGE)
        descriptor[1] = 1
        descriptor[2] = 1
        struct.pack_into("<IIII", descriptor, 4, len(package),
                         zlib.crc32(package) & 0xFFFFFFFF, len(package), 0)
        transaction = self._next_v2_transaction()
        transfer_active = False
        try:
            reply = await self.request_v2(
                CommandV2.TRANSFER_BEGIN, bytes(descriptor),
                transaction_id=transaction, timeout=15.0,
            )
            transfer_active = True
            device_chunk = struct.unpack_from("<H", reply, 2)[0] if len(reply) >= 4 else 0
            chunk_size = max(20, min(self._v2_write_payload,
                                     device_chunk or self._v2_write_payload))
            for offset in range(0, len(package), chunk_size):
                chunk = package[offset:offset + chunk_size]
                await self._write_v2_transfer_chunk(
                    transaction_id=transaction,
                    object_type=int(TransferObjectV2.GUI_ASSET_PACKAGE),
                    total_size=len(package), offset=offset, data=chunk,
                )
                if progress is not None:
                    progress(min(offset + len(chunk), len(package)), len(package))
            await self.request_v2(
                CommandV2.TRANSFER_COMMIT,
                transaction_id=transaction, timeout=30.0,
            )
            transfer_active = False
        except BaseException:
            if transfer_active:
                try:
                    await self.request_v2(
                        CommandV2.TRANSFER_ABORT,
                        transaction_id=transaction, timeout=5.0,
                    )
                except BaseException:
                    pass
            raise
        return await self.get_gui_asset_info()

    async def list_slots(self) -> tuple[ImageStoreInfo | None, list[ImageStoreItem]]:
        budget = _ResourceReadBudget(time.monotonic() + _RESOURCE_READ_BUDGET)
        info = await self._v2_resource_info(budget)
        content_types = ("image", "schedule", "wifi", "todo", "business", "page")
        items: list[ImageStoreItem] = []
        cursor = 0xFF
        for _ in range(256):
            payload = await self._resource_snapshot(CommandV2.LIST_RESOURCES, bytes((cursor,)), budget=budget)
            if len(payload) >= 2 and payload[0] == 1:
                return constrain_image_store_for_client(
                    info, items, self.snapshot.activation, self.snapshot.screen,
                    tiered_capacity=bool(
                        self._v2_capabilities & int(CapabilityV2.ACTIVATION)
                    ),
                )
            if len(payload) < 19 or payload[0] != 0 or len(payload) != 19 + payload[6]:
                raise InkTagError("Device returned an invalid V2 image-store item")
            try:
                name = payload[19:].decode("utf-8")
            except UnicodeDecodeError as exc:
                raise InkTagError("Device returned an invalid UTF-8 image-slot name") from exc
            item = ImageStoreItem(
                id=payload[1],
                content_type=content_types[payload[2]] if payload[2] < len(content_types) else "image",
                format=payload[3],
                segments=payload[4],
                compatible=payload[5] != 0,
                width=struct.unpack_from("<H", payload, 7)[0],
                height=struct.unpack_from("<H", payload, 9)[0],
                bytes=struct.unpack_from("<I", payload, 11)[0],
                created_at=struct.unpack_from("<I", payload, 15)[0],
                name=name,
            )
            items.append(item)
            cursor = item.id
        raise InkTagError("Device V2 image-store listing did not terminate")

    async def read_slot(
        self,
        slot_id: int,
        on_progress: Callable[[int, int], None] | None = None,
    ) -> tuple[ImageStoreItem, bytes]:
        """Read one complete device-ready image record with strict framing checks."""
        slot = _slot_id(slot_id)
        _info, items = await self.list_slots()
        item = next((candidate for candidate in items if candidate.id == slot), None)
        if item is None:
            raise InkTagError(f"Device image slot {slot} does not exist")
        if item.bytes <= 0:
            raise InkTagError(f"Device image slot {slot} has no readable payload")
        if item.bytes > 4 * 1024 * 1024:
            raise InkTagError(
                f"Device image slot {slot} reports an unsafe payload size: {item.bytes} bytes"
            )

        output = bytearray(item.bytes)
        offset = 0
        while offset < item.bytes:
            requested = min(232, item.bytes - offset)
            request = bytes((slot,)) + struct.pack("<I", offset) + bytes((requested,))
            response = await self.request_v2(
                CommandV2.READ_RESOURCE,
                request,
                timeout=12.0,
                response=True,
            )
            if len(response) < 10 or response[0] != slot:
                raise InkTagError("Device returned invalid image-slot data")
            response_offset = struct.unpack_from("<I", response, 1)[0]
            total_size = struct.unpack_from("<I", response, 5)[0]
            chunk = response[9:]
            if (
                response_offset != offset
                or total_size != item.bytes
                or not chunk
                or len(chunk) > requested
                or offset + len(chunk) > item.bytes
            ):
                raise InkTagError("Device image-slot offset or length changed during download")
            output[offset : offset + len(chunk)] = chunk
            offset += len(chunk)
            if on_progress is not None:
                on_progress(offset, item.bytes)
        return item, bytes(output)

    async def _resolve_visible_slot(self, selector: int | str) -> int:
        if isinstance(selector, int) or selector.strip().isdigit():
            slot = _slot_id(int(selector))
        else:
            key = resource_alias_key(selector)
            if not key:
                raise ValueError("image-slot selector cannot be empty")
            _info, items = await self.list_slots()
            matches = [item.id for item in items if resource_alias_key(item.name) == key]
            if not matches:
                raise InkTagError(f"Device image-slot alias '{selector}' does not exist")
            if len(matches) != 1:
                raise InkTagError(f"Device image-slot alias '{selector}' is ambiguous")
            return matches[0]
        _info, items = await self.list_slots()
        if not any(item.id == slot for item in items):
            raise InkTagError(
                f"Device image slot {slot} does not exist or is unavailable for the current edition"
            )
        return slot

    async def show_slot(self, slot_id: int | str, timeout: float = 100.0) -> str:
        slot = await self._resolve_visible_slot(slot_id)
        await self._run_v2_display(DisplayModeV2.RESOURCE, bytes((slot,)), timeout=timeout)
        return f"img:show:done id={slot}"

    async def set_slot_alias(self, slot_id: int, alias: str) -> str:
        if not self.supports_capability(CapabilityV2.RESOURCE_ALIAS):
            raise InkTagError("Device does not support image-slot aliases")
        slot = await self._resolve_visible_slot(slot_id)
        encoded = encode_resource_alias(alias)
        key = resource_alias_key(alias)
        _info, items = await self.list_slots()
        if any(item.id != slot and resource_alias_key(item.name) == key for item in items):
            raise InkTagError(f"Image-slot alias '{alias.strip()}' is already in use")
        await self.request_v2(
            CommandV2.SET_RESOURCE_ALIAS,
            bytes((slot, len(encoded))) + encoded,
        )
        _info, refreshed = await self.list_slots()
        item = next((candidate for candidate in refreshed if candidate.id == slot), None)
        if item is None or resource_alias_key(item.name) != key:
            raise InkTagError("Device accepted the alias but did not return it from the slot list")
        return f"img:alias:done id={slot} alias={item.name}"

    async def delete_slot(self, slot_id: int) -> str:
        slot = await self._resolve_visible_slot(slot_id)
        await self.request_v2(CommandV2.DELETE_RESOURCE, bytes((slot,)))
        return f"img:delete:done id={slot}"

    async def clear_slots(self, incompatible_only: bool) -> str:
        scope = (
            ResourceClearScopeV2.INCOMPATIBLE
            if incompatible_only
            else ResourceClearScopeV2.ALL
        )
        # A full logical store can include hundreds of sectors across a main
        # and an auxiliary Flash. The device deliberately exposes one clear
        # operation, so allow its bounded erase to complete transparently.
        payload = await self.request_v2(
            CommandV2.CLEAR_RESOURCES, bytes((scope,)), timeout=180.0
        )
        removed = struct.unpack_from("<H", payload, 0)[0] if len(payload) >= 2 else 0
        return f"img:clear:done removed={removed}"

    async def start_slideshow(self, interval_seconds: int, timeout: float = 100.0) -> str:
        seconds = max(30, min(86400, int(interval_seconds)))
        await self._run_v2_display(
            DisplayModeV2.SLIDESHOW, struct.pack("<I", seconds), timeout=timeout
        )
        return "img:show:done slideshow=1"

    async def stop_slideshow(self) -> str:
        await self.request_v2(
            CommandV2.DISPLAY_ACTION, bytes((DisplayActionV2.PAUSE,))
        )
        return "img:slideshow:stopped"

    async def ghost_clean(self, cycles: int, timeout: float | None = None) -> str:
        count = int(cycles)
        if count != cycles or not 1 <= count <= 10:
            raise ValueError("Ghost-clean cycles must be an integer from 1 to 10")
        if timeout is not None and timeout <= 0:
            raise ValueError("Ghost-clean timeout must be positive")
        action = int(DisplayActionV2.GHOST_CLEAN)
        if not self.snapshot.settings_capabilities.display_action_mask & (1 << action):
            raise InkTagError("Firmware does not advertise bounded ghost cleaning")
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.DISPLAY_ACTION)
        hard_deadline = time.monotonic() + (
            _ghost_clean_hard_timeout(count) if timeout is None else timeout
        )
        progress_deadline = min(
            hard_deadline, time.monotonic() + _GHOST_CLEAN_FIRST_PROGRESS_TIMEOUT
        )
        last_sequence = self._sequence
        last_percent = 0
        try:
            await self.request_v2(
                CommandV2.DISPLAY_ACTION,
                bytes((action, count)),
                flags=RequestFlagV2.CONFIRM,
                transaction_id=transaction,
            )
            while not completion.done():
                self._notification_event.clear()
                for notification in self.notifications:
                    if notification.sequence <= last_sequence:
                        continue
                    last_sequence = notification.sequence
                    data = notification.raw
                    if (
                        len(data) >= 7
                        and data[0] == int(EventV2.PROGRESS)
                        and data[1:5] == bytes((0, int(StatusV2.OK),
                                                 int(CommandV2.DISPLAY_ACTION), transaction))
                        and 0 < data[6] <= 100
                        and data[6] > last_percent
                    ):
                        last_percent = data[6]
                        progress_deadline = min(
                            hard_deadline,
                            notification.received_at + _GHOST_CLEAN_PROGRESS_STALL_TIMEOUT,
                        )
                if completion.done():
                    break
                remaining = min(hard_deadline, progress_deadline) - time.monotonic()
                if remaining <= 0:
                    # A late COMPLETE may arrive while CANCEL is in flight.
                    # Retire our waiter first so its error cannot become an
                    # unobserved future exception after this timeout is raised.
                    completion.cancel()
                    try:
                        await self.request_v2(CommandV2.CANCEL, timeout=5.0)
                    except (InkTagError, BleakError, OSError):
                        pass
                    raise InkTagError(
                        f"Ghost cleaning timed out after {last_percent}% progress; "
                        "the device was asked to cancel"
                    )
                try:
                    await asyncio.wait_for(self._notification_event.wait(), remaining)
                except TimeoutError:
                    pass
            result = completion.result()
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise
        if result and result != bytes((action, count)):
            raise InkTagError("Device returned an invalid ghost-clean completion")
        return f"display:ghost-clean:done cycles={count}"

    async def diagnostic(self, timeout: float = 2.5) -> list[str]:
        status = await self._refresh_v2_status()
        messages = [
            f"v2 status mode={status[1]} flags=0x{status[2]:02x} progress={status[3]} "
            f"voltage={self.snapshot.voltage or 0:.3f} temperature={self.snapshot.temperature}"
        ]
        if has_temperature_source_record(status):
            messages.append(
                f"temperature sources mcu={self.snapshot.mcu_temperature} "
                f"panel={self.snapshot.panel_temperature}"
            )
        if len(status) >= 27 and status[16] == 4:
            panel_temperature = None
            if status[20] & 0x01:
                panel_raw = (status[18] << 4) | (status[19] >> 4)
                if panel_raw & 0x800:
                    panel_raw -= 0x1000
                panel_temperature = f"{panel_raw / 16:g}"
            messages.append(
                f"epd result={status[17]} panel_temperature={panel_temperature} "
                f"panel_tsc2=0x{status[19]:02x}"
            )
            messages.append(
                f"ble notify last_status=0x{status[21]:02x} "
                f"retries={struct.unpack_from('<H', status, 22)[0]} "
                f"drops={struct.unpack_from('<H', status, 24)[0]} "
                f"pending={status[26]}"
            )
        elif len(status) >= 21 and status[16] == 3:
            panel_temperature = None
            if status[20] & 0x01:
                panel_raw = (status[18] << 4) | (status[19] >> 4)
                if panel_raw & 0x800:
                    panel_raw -= 0x1000
                panel_temperature = f"{panel_raw / 16:g}"
            messages.append(
                f"epd result={status[17]} panel_temperature={panel_temperature} "
                f"panel_tsc2=0x{status[19]:02x}"
            )
        elif len(status) >= 25 and status[16] == 2:
            panel_temperature = None
            if status[24] & 0x01:
                panel_raw = (status[22] << 4) | (status[23] >> 4)
                if panel_raw & 0x800:
                    panel_raw -= 0x1000
                panel_temperature = f"{panel_raw / 16:g}"
            messages.append(
                f"epd trace stages_high=0x{status[17]:02x} waits={status[18]} "
                f"entry_high=0x{status[19]:02x} exit_high=0x{status[20]:02x} "
                f"result={status[21]} panel_temperature={panel_temperature} "
                f"panel_tsc2=0x{status[23]:02x}"
            )
        elif len(status) >= 23 and status[16] == 1:
            messages.append(
                f"epd trace stages_high=0x{status[17]:02x} waits={status[18]} "
                f"entry_high=0x{status[19]:02x} exit_high=0x{status[20]:02x} "
                f"result={status[21]} panel_tsc2=0x{status[22]:02x}"
            )
        if len(status) >= 37 and status[16] == 0xF1:
            flash_flags = status[17]
            jedec = int.from_bytes(status[18:21], "big")
            capacity = struct.unpack_from("<I", status, 21)[0]
            sfdp = bytes(status[25:33]).hex().upper()
            messages.append(
                f"external flash candidate={int(bool(flash_flags & 0x01))} "
                f"matched={int(bool(flash_flags & 0x02))} jedec={jedec:06X} "
                f"capacity={capacity} sfdp={sfdp} "
                f"pins=miso:{status[33]},mosi:{status[34]},cs:{status[35]},sclk:{status[36]}"
            )
        return messages

    async def test_led(self, color_mask: int, timeout: float = 5.0) -> str:
        mask = int(color_mask)
        if not 0 <= mask <= 0x07:
            raise ValueError("LED color mask must be in 0..7")
        await self._require_v2_test(TestOperationV2.LED_CHANNEL)
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.RUN_TEST)
        try:
            await self.request_v2(
                CommandV2.RUN_TEST,
                bytes((int(TestOperationV2.LED_CHANNEL), mask)),
                transaction_id=transaction,
            )
            result = await self._await_v2_completion(transaction, completion, timeout)
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise
        if result != bytes((mask,)):
            raise InkTagError("Device returned an invalid LED test completion")
        return f"led:test mask=0x{mask:02x}"

    async def test_adc(self, channel: str, timeout: float = 5.0) -> str:
        channel_ids = {"temperature": 0, "supply": 1, "reference": 2}
        normalized = channel.strip().lower()
        if normalized not in channel_ids:
            raise ValueError("ADC channel must be temperature, supply or reference")
        channel_id = channel_ids[normalized]
        await self._require_v2_test(TestOperationV2.ADC_READ)
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.RUN_TEST)
        try:
            response = await self.request_v2(
                CommandV2.RUN_TEST,
                bytes((int(TestOperationV2.ADC_READ), channel_id)),
                transaction_id=transaction,
            )
            result = await self._await_v2_completion(transaction, completion, timeout)
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise
        if (result != bytes((channel_id,)) or len(response) != 6 or
                response[:2] != bytes((1, channel_id))):
            raise InkTagError("Device returned an invalid ADC test result")
        reading = struct.unpack_from("<H", response, 3)[0]
        valid = response[2] != 0
        if channel_id == 0:
            if valid:
                temperature = struct.unpack_from("b", response, 5)[0]
                return f"adc:temperature valid=1 raw_sample={reading} celsius={temperature}"
            return f"adc:temperature valid=0 raw_sample={reading} celsius=unavailable"
        if channel_id == 2:
            return f"adc:reference programmed={int(valid)} millivolts={reading}"
        return f"adc:supply valid={int(valid)} millivolts={reading}"

    async def display_busy_timing(self, timeout: float = 240.0) -> list[str]:
        await self._require_v2_test(TestOperationV2.DISPLAY_BUSY_TIMING)
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.RUN_TEST)
        try:
            await self.request_v2(
                CommandV2.RUN_TEST,
                bytes((int(TestOperationV2.DISPLAY_BUSY_TIMING),)),
                transaction_id=transaction,
            )
            await self._await_v2_completion(transaction, completion, timeout)
        except BaseException:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()
            raise
        return await self.display_busy_status()

    async def display_busy_status(self) -> list[str]:
        await self._require_v2_test(TestOperationV2.DISPLAY_BUSY_TIMING)
        payload = await self.request_v2(
            CommandV2.GET_TEST_STATUS,
            bytes((int(TestOperationV2.DISPLAY_BUSY_TIMING),)),
        )
        if len(payload) < 3 or payload[0] != 1 or payload[1] != int(
            TestOperationV2.DISPLAY_BUSY_TIMING
        ):
            raise InkTagError("Device returned invalid display BUSY test status")
        count = payload[2]
        if len(payload) != 3 + count * 9:
            raise InkTagError("Device returned a truncated display BUSY trace")
        names = (
            "parked-pullup", "pb7-enable", "bus-ready-floating",
            "hardware-reset", "software-reset-12", "otp-b1-first",
            "border-a1", "otp-b1-second", "black-ram-24", "red-ram-26",
            "refresh-c7", "final-park", "pb7-released-pa1-floating",
            "pb7-released-hardware-reset",
        )
        messages: list[str] = []
        for index in range(count):
            offset = 3 + index * 9
            stage, levels = payload[offset : offset + 2]
            first_high, high_ms, elapsed_ms = struct.unpack_from("<HHH", payload, offset + 2)
            read_value = payload[offset + 8]
            name = names[stage] if stage < len(names) else f"stage-{stage}"
            first = "none" if first_high == 0xFFFF else str(first_high)
            messages.append(
                f"{name}: PA1={(levels >> 0) & 1}->{(levels >> 3) & 1} "
                f"saw_high={(levels >> 1) & 1} saw_low={(levels >> 2) & 1} "
                f"first_high_ms={first} high_ms={high_ms} elapsed_ms={elapsed_ms} "
                f"PB7={(levels >> 4) & 1}->{(levels >> 5) & 1} read=0x{read_value:02x}"
            )
        return messages

    async def request_nfc_status(self, *, include_user_data: bool = False) -> list[str]:
        capabilities = decode_nfc_capabilities(
            await self.request_v2(CommandV2.GET_NFC_CAPABILITIES)
        )
        status = decode_nfc_status(await self.request_v2(CommandV2.GET_NFC_STATUS))
        present = bool(int(status["flags"]) & 0x01)
        messages = [
            "nfc:status "
            f"present={int(present)} uid={status['uid'] or '-'} "
            f"flags=0x{int(capabilities['flags']):04x} "
            f"result={int(status['lastResult'])} "
            f"opcode={int(status['lastOpcode'])} "
            f"sequence={int(status['lastSequence'])} "
            f"diag=0x{int(status['diagnosticFlags']):02x}"
        ]
        if include_user_data:
            if not int(capabilities["flags"]) & 0x10:
                raise InkTagError("Firmware does not advertise NFC NDEF reading")
            ndef = decode_nfc_ndef(
                await self.request_v2(CommandV2.GET_NFC_NDEF)
            )
            messages.append(f"nfc:ndef={ndef.hex().upper()}")
        return messages

    async def request_nfc_channel_snapshot(self) -> list[str]:
        await self._require_v2_test(TestOperationV2.NFC_CHANNEL_SNAPSHOT)
        payload = await self._run_v2_test(
            bytes((int(TestOperationV2.NFC_CHANNEL_SNAPSHOT),)), timeout=10.0
        )
        if payload[:1] == b"\x02":
            # Type-2 host block 0 is raw evidence, not an FM11 register map or
            # proof of an SRAM channel. No arbitrary block/address request.
            if len(payload) != 21 or payload[1:5] != b"\x02\x55\x00\x10":
                raise InkTagError("Device returned invalid Type-2 NFC snapshot")
            return [
                "nfc:channel transport=type2 map=host-block0-unqualified "
                f"i2c=0x55 block=0x00 raw={payload[5:].hex().upper()}"
            ]
        if len(payload) != 34 or payload[0] != 1:
            raise InkTagError("Device returned invalid NFC channel snapshot")
        services, errors, flushes, responses = struct.unpack_from("<HHHH", payload, 15)
        frame = payload[23:31][:min(payload[10], 8)].hex().upper() or "-"
        reply = payload[31:34][:min(payload[11], 3)].hex().upper() or "-"
        state = payload[12]
        bus = payload[13]
        return [
            "nfc:channel "
            f"diag=0x{payload[1]:02x} irq={int(bool(state & 0x04))} "
            f"init={int(bool(state & 0x01))} ok={int(bool(state & 0x02))} "
            f"stage={payload[14]} sda={int(bool(bus & 0x01))} "
            f"scl={int(bool(bus & 0x02))} cs={int(bool(bus & 0x04))} "
            f"irq_pin={int(bool(bus & 0x08))} "
            f"sda_drive={int(bool(bus & 0x10))} "
            f"scl_drive={int(bool(bus & 0x20))} "
            f"cs_drive={int(bool(bus & 0x40))} "
            f"main=0x{payload[2]:02x}/0x{payload[7]:02x} "
            f"fifo_irq=0x{payload[3]:02x}/0x{payload[8]:02x} "
            f"aux=0x{payload[4]:02x}/0x{payload[9]:02x} "
            f"count={payload[5]} rf=0x{payload[6]:02x} "
            f"services={services} errors={errors} flushes={flushes} responses={responses} "
            f"frame={frame} reply={reply}"
        ]

    async def configure_nfc(
        self,
        action: str,
        *,
        slot_id: int = 0,
    ) -> str:
        await self.refresh_snapshot()
        capabilities = self.snapshot.settings_capabilities
        if not capabilities.nfc_action_mask:
            raise InkTagError("Firmware does not advertise NFC behavior support")
        if (capabilities.nfc_action_mask & input_action_mask(action)) == 0:
            raise InkTagError(f"Firmware does not advertise the NFC action {action}")
        payload = encode_input_action(
            "nfc", action, slot_id=slot_id
        )
        await self.request_v2(CommandV2.SET_INPUT_CONFIG, payload)
        return "settings:input:done source=nfc"

    async def simulate_nfc(self, timeout: float = 100.0) -> str:
        await self.refresh_snapshot()
        capabilities = self.snapshot.settings_capabilities
        if not capabilities.nfc_action_mask:
            raise InkTagError("Firmware does not advertise NFC behavior support")
        await self._require_v2_test(TestOperationV2.INPUT_SIMULATE)
        after = self._sequence
        structured = await self._run_input_simulation(0, timeout=timeout)
        if structured is not None:
            return structured
        tap = await self.wait_for(
            lambda item: item.text == "nfc:tap",
            after=after,
            timeout=5.0,
            description="simulated NFC tap",
        )
        assert tap.text is not None
        terminal = await self.wait_for(
            lambda item: bool(item.text and _is_nfc_terminal(item.text)),
            after=after,
            timeout=timeout,
            description="simulated NFC action completion",
        )
        assert terminal.text is not None
        if _is_nfc_error(terminal.text):
            raise InkTagError(terminal.text)
        return terminal.text

    async def _run_input_simulation(self, selector: int, *, timeout: float) -> str | None:
        transaction = self._next_v2_transaction()
        completion = self._prepare_v2_completion(transaction, CommandV2.RUN_TEST)
        # An error COMPLETE can arrive before ACK. Observe its exception now;
        # awaiting the future below still propagates it, without losing it.
        completion.add_done_callback(lambda result: result.exception() if not result.cancelled() else None)
        structured = False
        try:
            reply = await self.request_v2(
                CommandV2.RUN_TEST,
                bytes((int(TestOperationV2.INPUT_SIMULATE), selector)),
                transaction_id=transaction,
            )
            structured = structured_input_ack(reply)
            if not structured:
                return None
            result = await self._await_v2_completion(transaction, completion, timeout)
            return decode_input_completion(result, selector)
        except BaseException as error:
            if structured and (isinstance(error, asyncio.CancelledError) or
                               isinstance(error.__cause__, TimeoutError)):
                # The structured ACK also qualifies targeted cancellation.
                # Never use empty CANCEL, replay the action, or cancel RF work.
                try:
                    await self.request_v2(CommandV2.CANCEL,
                        bytes((int(CommandV2.RUN_TEST), transaction)), timeout=2.0)
                except Exception:
                    pass
            raise
        finally:
            current = self._v2_completions.get(transaction)
            if current and current[1] is completion:
                self._v2_completions.pop(transaction, None)
            if not completion.done():
                completion.cancel()

    async def _prepare_system_action(self) -> None:
        await self.stop()

    async def _authorize_owner_action(
        self, opcode: CommandV2, flags: RequestFlagV2 | int, payload: bytes,
        proof_builder: Callable[[bytes, bytes], Awaitable[bytes]] | None,
    ) -> None:
        if not self.supports_capability(CapabilityV2.OWNER_AUTHORIZATION):
            return
        if proof_builder is None:
            raise InkTagError("This device requires owner authorization for this action")
        nonce = parse_owner_challenge(
            await self.request_v2(CommandV2.GET_OWNER_CHALLENGE)
        )
        proof = await proof_builder(
            nonce, owner_action_hash(int(opcode), int(flags), payload)
        )
        await self.request_v2(CommandV2.AUTHORIZE_OWNER_ACTION, proof)

    async def _system_action(
        self,
        action: SystemActionV2,
        *,
        confirmed: bool = False,
        payload: bytes | None = None,
        timeout: float = 12.0,
        owner_proof: Callable[[bytes, bytes], Awaitable[bytes]] | None = None,
    ) -> bytes:
        if action == SystemActionV2.OTA_RECOVERY:
            if not self.supports_capability(CapabilityV2.OTA_RECOVERY):
                raise InkTagError("Device does not advertise fixed-Recovery entry")
        elif action != SystemActionV2.RESET and not (
            self._v2_capabilities & int(CapabilityV2.LIFECYCLE)
        ):
            raise InkTagError("Device does not advertise complete lifecycle support")
        request = payload if payload is not None else bytes((action,))
        flags = RequestFlagV2.CONFIRM if confirmed else RequestFlagV2.NONE
        protected = action in (
            SystemActionV2.RESET_SETTINGS,
            SystemActionV2.CLEAR_USER_DATA,
            SystemActionV2.FACTORY_RESET,
        )
        if protected:
            await self._authorize_owner_action(
                CommandV2.SYSTEM_ACTION, flags, request, owner_proof
            )
        try:
            return await self.request_v2(
                CommandV2.SYSTEM_ACTION, request, flags=flags, timeout=timeout
            )
        except InkTagV2StatusError as error:
            if error.status != int(StatusV2.BUSY):
                raise
            await self._prepare_system_action()
            if protected:
                await self._authorize_owner_action(
                    CommandV2.SYSTEM_ACTION, flags, request, owner_proof
                )
            return await self.request_v2(
                CommandV2.SYSTEM_ACTION, request, flags=flags, timeout=timeout
            )

    async def reset(self) -> str:
        await self._system_action(SystemActionV2.RESET)
        return "reset:sent"

    async def enter_recovery(self) -> str:
        """Request the platform's advertised fixed-Recovery path.

        The firmware remains authoritative for voltage, journal and role-entry
        admission.  A successful response only means the request was accepted;
        the transport disconnect that follows lets the platform complete its
        controlled role handoff.
        """
        await self._system_action(SystemActionV2.OTA_RECOVERY)
        return "recovery:requested"

    async def deep_sleep(self, seconds: int) -> str:
        if not 5 <= seconds <= 3600:
            raise ValueError("deep sleep duration must be between 5 and 3600 seconds")
        if not (
            self._v2_capabilities & int(CapabilityV2.EXPERIMENTAL_TIMED_SLEEP)
        ):
            raise InkTagError(
                "Device does not advertise the experimental timed-sleep capability"
            )
        request = struct.pack(
            "<BBBI",
            int(SystemActionV2.SLEEP),
            1,
            int(SleepWakeFlagV2.TIMER),
            seconds,
        )
        await self._system_action(SystemActionV2.SLEEP, payload=request)
        return f"sleep:sent wake-after={seconds}"

    async def reset_settings(
        self, *, owner_proof: Callable[[bytes, bytes], Awaitable[bytes]] | None = None,
    ) -> str:
        await self._system_action(
            SystemActionV2.RESET_SETTINGS, confirmed=True, owner_proof=owner_proof
        )
        return "settings-reset:sent"

    async def factory_reset(
        self, *, owner_proof: Callable[[bytes, bytes], Awaitable[bytes]] | None = None,
    ) -> int | None:
        try:
            payload = await self._system_action(
                SystemActionV2.FACTORY_RESET,
                confirmed=True,
                timeout=60.0,
                owner_proof=owner_proof,
            )
        except InkTagV2StatusError as error:
            if error.status == int(StatusV2.RETRY_REQUIRED):
                # A reset marker was committed and the device must reboot to
                # finish erasing. Acknowledgement is not completion evidence.
                return None
            raise
        return struct.unpack_from("<H", payload, 0)[0] if len(payload) >= 2 else 0

    async def clear_user_data(
        self, *, owner_proof: Callable[[bytes, bytes], Awaitable[bytes]] | None = None,
    ) -> int:
        payload = await self._system_action(
            SystemActionV2.CLEAR_USER_DATA,
            confirmed=True,
            timeout=60.0,
            owner_proof=owner_proof,
        )
        return struct.unpack_from("<H", payload, 0)[0] if len(payload) >= 2 else 0

    async def simulate_button(self, gesture: str, timeout: float = 100.0) -> str:
        values = {"single": 1, "double": 2, "long": 3}
        if gesture not in values:
            raise ValueError("button gesture must be single, double or long")
        await self.refresh_snapshot()
        capabilities = self.snapshot.settings_capabilities
        if (capabilities.button_gesture_mask & (1 << (values[gesture] - 1))) == 0:
            raise InkTagError(f"Firmware does not advertise the {gesture} button gesture")
        await self._require_v2_test(TestOperationV2.INPUT_SIMULATE)
        after = self._sequence
        structured = await self._run_input_simulation(values[gesture], timeout=timeout)
        if structured is not None:
            return structured
        event = await self.wait_for(
            lambda item: bool(item.text and item.text.startswith("button:event ")),
            after=after,
            timeout=5.0,
            description="simulated button event",
        )
        assert event.text is not None
        terminal = await self.wait_for(
            lambda item: bool(item.text and _is_button_terminal(item.text)),
            after=event.sequence,
            timeout=timeout,
            description="simulated button action completion",
        )
        assert terminal.text is not None
        if gesture == "long" and terminal.text.startswith("img:slideshow:stopped"):
            terminal = await self.wait_for(
                lambda item: bool(
                    item.text
                    and _is_button_terminal(item.text)
                    and not item.text.startswith("img:slideshow:stopped")
                ),
                after=terminal.sequence,
                timeout=timeout,
                description="display restoration after stopping slideshow",
            )
            assert terminal.text is not None
        if _is_operation_error(terminal.text) or terminal.text.startswith("button:unsupported"):
            raise InkTagError(terminal.text)
        return terminal.text

    async def test_buzzer(self, duration_ms: int) -> str:
        duration = max(20, min(5000, int(duration_ms)))
        await self.refresh_snapshot()
        if not self.snapshot.settings_capabilities.custom_buzzer_gpio:
            raise InkTagError("Firmware does not advertise buzzer configuration")
        await self._require_v2_test(TestOperationV2.BUZZER)
        await self.request_v2(
            CommandV2.RUN_TEST,
            bytes((int(TestOperationV2.BUZZER),)) + struct.pack("<H", duration),
        )
        return "buzzer:started"

    async def locate_device(self, *, start: bool = True) -> str:
        """Run the bounded local LED/buzzer locator without changing Find state."""
        await self._require_v2_test(TestOperationV2.DEVICE_LOCATE)
        await self.request_v2(
            CommandV2.RUN_TEST,
            bytes((int(TestOperationV2.DEVICE_LOCATE), 1 if start else 0)),
        )
        return "locate:started" if start else "locate:stopped"

    async def next_notification(self, after: int, timeout: float | None = None) -> Notification:
        return await self.wait_for(
            lambda item: item.sequence > after,
            after=after,
            timeout=timeout if timeout is not None else 365 * 24 * 3600,
            description="device notification",
        )


def _slot_id(value: int) -> int:
    if not 0 <= value <= 255:
        raise ValueError("slot id must be in 0..255")
    return value


def _is_operation_error(message: str) -> bool:
    return bool(
        re.match(
            r"^(?:epd:(?:busy|no-profile|no-busy-edge|no-screen|spi-error|stopped|timeout)|"
            r"picture:(?:bad-packet|queue-full)|(?:device-info|clear|calendar|clock):error)",
            message,
        )
    )


def _is_nfc_terminal(message: str) -> bool:
    return message.startswith(
        (
            "device-info:done",
            "calendar:done",
            "clock:done",
            "clear:done",
            "picture:done",
            "img:show:done",
            "img:slideshow:stopped",
            "img:slideshow:error",
            "nfc:link",
            "nfc:disabled",
            "nfc:url:ready",
            "nfc:slot-missing",
            "nfc:slideshow-single",
            "nfc:return-missing",
            "nfc:unsupported",
            "nfc:busy",
            "epd:stopped",
        )
    ) or _is_operation_error(message)


def _is_button_terminal(message: str) -> bool:
    return _is_nfc_terminal(message) or message.startswith(
        (
            "button:disabled",
            "button:slot-missing",
            "button:return-missing",
            "button:unsupported",
        )
    )


def _is_nfc_error(message: str) -> bool:
    return _is_operation_error(message) or message.startswith(
        ("img:slideshow:error", "nfc:slot-missing", "nfc:return-missing", "nfc:unsupported", "nfc:busy")
    )
