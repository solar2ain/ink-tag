"""Optional, operation-scoped native connection preferences for shared BLE."""
from __future__ import annotations

import sys
import re
from typing import Any

from .board_ids import canonical_board_id


def _throughput_parameters() -> Any:
    # Lazy: WinRT and this Windows 11 API are optional on other hosts/backends.
    from winrt.windows.devices.bluetooth import BluetoothLEPreferredConnectionParameters
    return BluetoothLEPreferredConnectionParameters.throughput_optimized


def _note(client: Any, message: str) -> None:
    phase = getattr(client, "phase_sink", None)
    if phase:
        try:
            phase(message)
        except Exception:
            # Diagnostics must not mask a transfer/cancellation/cleanup result.
            pass


def _actual_parameters(requester: Any) -> str:
    try:
        params = requester.get_connection_parameters()
        interval = params.connection_interval
        if not interval:
            return "actual=unavailable"
        return f"actual_interval={interval * 1.25:g}ms latency={params.connection_latency}"
    except (AttributeError, OSError, RuntimeError, NotImplementedError):
        return "actual=unavailable"


def report_image_ack_link(client: Any) -> None:
    """Verbose TLSR qualification metadata; no connection or preference change."""
    if (not getattr(client, 'phase_sink', None) or sys.platform != 'win32'
            or canonical_board_id(getattr(getattr(client, 'snapshot', None), 'board_id', ''))
            != 'tlsr8359_hanshow'):
        return
    native = getattr(client, '_client', None)
    requester = getattr(getattr(native, '_backend', None), '_requester', None)
    _note(client, 'image data ACK link: ' + _actual_parameters(requester))


class ImageConnectionPreference:
    def __init__(self, request: Any, requester: Any, client: Any) -> None:
        self._request = request
        self._requester = requester
        self._client = client

    def close(self) -> None:
        request, self._request = self._request, None
        if request is None:
            return
        try:
            request.close()
        except (AttributeError, OSError, RuntimeError):
            # The normal shared disconnect still disposes the owning native
            # device/session. Never replace an original transfer error here.
            _note(self._client, "image link preference release failed; disconnect releases native device")
        else:
            _note(self._client, "image link preference released; " + _actual_parameters(self._requester))


def request_image_throughput(client: Any) -> ImageConnectionPreference | None:
    """Hold one OS request during image bytes, never change product protocol.

    A successful request is only a preference, not proof of the actual interval.
    All image destinations and boards share this optional host capability.
    Unsupported hosts/backends and refused requests retain ordinary transport.
    """
    if sys.platform != "win32":
        return None
    native = getattr(client, "_client", None)
    if native is None or not getattr(native, "is_connected", False):
        return None
    requester = getattr(getattr(native, "_backend", None), "_requester", None)
    return _request_throughput(requester, client)


def _request_throughput(requester: Any, client: Any) -> ImageConnectionPreference | None:
    request = None
    try:
        request = requester.request_preferred_connection_parameters(_throughput_parameters())
        lease = ImageConnectionPreference(request, requester, client)
        if int(request.status) != 1:  # WinRT SUCCESS; no success claim for a refusal.
            _note(client, f"image link preference unavailable: status={int(request.status)}")
            lease.close()
            return None
    except (ImportError, AttributeError, OSError, RuntimeError, NotImplementedError):
        if request is not None:
            ImageConnectionPreference(request, requester, client).close()
        _note(client, "image link preference unavailable; retaining ordinary connection")
        return None
    _note(client, "image link throughput preference requested; " + _actual_parameters(requester))
    return lease


class ConnectedImageLink:
    """An owned OS metadata/preference handle, never a GATT connection/client.

    The normal browser remains responsible for device identity, AUTH and all
    protocol commands. This handle never scans, connects, subscribes, writes
    CCCD, or sends an InkTag command, and cannot wake an unavailable device.
    """
    def __init__(self, device: Any, phase_sink: Any = None) -> None:
        self._device = device
        self.phase_sink = phase_sink
        self._preference: ImageConnectionPreference | None = None

    def parameters(self) -> dict[str, Any]:
        result: dict[str, Any] = {}
        # These are OS metadata getters, not GATT operations. PHY support must
        # not hide a usable interval/latency observation (or vice versa).
        try:
            params = self._device.get_connection_parameters()
            result.update(intervalUnits=params.connection_interval,
                intervalMs=params.connection_interval * 1.25,
                latency=params.connection_latency, timeoutUnits=params.link_timeout)
        except (AttributeError, OSError, RuntimeError, NotImplementedError):
            pass
        try:
            phy = self._device.get_connection_phy()
            result.update(tx2M=phy.transmit_info.is_uncoded2_m_phy,
                          rx2M=phy.receive_info.is_uncoded2_m_phy)
        except (AttributeError, OSError, RuntimeError, NotImplementedError):
            pass
        return result

    def request_throughput(self) -> bool:
        if self._device is None or self._preference is not None:
            return False
        self._preference = _request_throughput(self._device, self)
        return self._preference is not None

    def close(self) -> bool:
        device, self._device = self._device, None
        if self._preference is not None:
            self._preference.close()
            self._preference = None
        if device is not None:
            try:
                device.close()  # Dispose this handle; do not disconnect the browser.
            except (AttributeError, OSError, RuntimeError):
                _note(self, 'image native handle release failed')
                return False
        return True


async def _connected_windows_device(address: int) -> Any:
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    return await BluetoothLEDevice.from_bluetooth_address_async(address)


async def open_connected_image_link(address: str, board_id: str, phase_sink: Any = None) -> ConnectedImageLink | None:
    """Borrow only a *currently connected* exact OS address for a native scope.

    This is not an identity/AUTH substitute. Callers must bind the address and
    board to the normal browser's live verified App before requesting a scope.
    The board argument carries caller context, not a throughput allowlist.
    No GattSession or GATT read is created to find/verify an unavailable device.
    """
    if sys.platform != 'win32':
        return None
    if not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', address):
        raise ValueError('A complete Bluetooth address is required')
    numeric = int(address.replace(':', ''), 16)
    if not numeric:
        raise ValueError('A nonzero Bluetooth address is required')
    device = None
    try:
        device = await _connected_windows_device(numeric)
        if device is None:
            return None
        if device.bluetooth_address != numeric or int(device.connection_status) != 1:
            ConnectedImageLink(device, phase_sink).close()
            return None
        return ConnectedImageLink(device, phase_sink)
    except (ImportError, AttributeError, OSError, RuntimeError, NotImplementedError):
        if device is not None:
            ConnectedImageLink(device, phase_sink).close()
        return None
