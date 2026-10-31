"""Short-lived host preferences only; the browser owns every GATT operation."""
from __future__ import annotations

import asyncio
import importlib.util
import math
import secrets
import sys
import threading
from concurrent.futures import TimeoutError as FutureTimeout
from typing import Any

from .ble_connection import open_connected_image_link

TTL_SECONDS = 15
ACQUIRE_SECONDS = 2


class ImageLinkLeases:
    """Serialize native handles on one loop, including late acquisition cleanup.

    No device identity is inferred here. The caller must already own a verified
    browser App connection; the shared borrower checks the exact connected OS
    address without GATT discovery, connect, authentication or product commands.
    """

    def __init__(self) -> None:
        self.csrf = secrets.token_urlsafe(24)
        self._lock = threading.Lock()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._closing = False
        self._owners: set[str] = set()
        self._blocked: set[str] = set()
        self._pending: set[asyncio.Task] = set()
        self._leases: dict[str, tuple[str, Any, asyncio.TimerHandle]] = {}

    @staticmethod
    def supported() -> bool:
        if sys.platform != 'win32':
            return False
        try:
            return importlib.util.find_spec('winrt.windows.devices.bluetooth') is not None
        except (ImportError, ValueError):
            return False

    def _ensure_loop(self) -> asyncio.AbstractEventLoop | None:
        with self._lock:
            if self._closing:
                return None
            if self._loop is None:
                self._loop = asyncio.new_event_loop()
                self._thread = threading.Thread(target=self._run, daemon=True,
                                                name='inktag-image-link')
                self._thread.start()
            return self._loop

    def _run(self) -> None:
        loop = self._loop
        assert loop is not None
        asyncio.set_event_loop(loop)
        try:
            loop.run_forever()
        finally:
            loop.close()

    def call(self, action: str, body: dict[str, Any]) -> dict[str, Any]:
        if not self.supported():
            return {'available': False, 'reason': 'unsupported_host'}
        loop = self._ensure_loop()
        if loop is None:
            return {'available': False, 'reason': 'shutting_down'}
        with self._lock:
            if self._closing:
                return {'available': False, 'reason': 'shutting_down'}
            future = asyncio.run_coroutine_threadsafe(self._dispatch(action, body), loop)
        try:
            return future.result(timeout=ACQUIRE_SECONDS + 2)
        except (FutureTimeout, RuntimeError):
            # Do not replay/cancel an unknown native outcome. A resulting lease
            # still expires, and a late borrower is closed on the owning loop.
            return {'available': False, 'reason': 'dispatch_timeout'}

    async def _dispatch(self, action: str, body: dict[str, Any]) -> dict[str, Any]:
        if self._closing:
            return {'available': False, 'reason': 'shutting_down'}
        if action == 'acquire':
            return await self._acquire(body['address'], body.get('boardId', ''))
        token = body['lease']
        if token not in self._leases:
            return {'ok': False, 'reason': 'lease_missing'}
        if action == 'release':
            return {'ok': self._release(token)}
        address, link, timer = self._leases[token]
        if action == 'inspect':
            # Read only the handle already owned by this exact opaque lease.
            # Inspection cannot renew its TTL or create another native scope.
            return {'ok': True, 'parameters': self._parameters(link)}
        timer.cancel()
        self._leases[token] = (address, link, asyncio.get_running_loop().call_later(
            TTL_SECONDS, self._release, token))
        # Renewal is only local lifetime maintenance. Optional synchronous
        # WinRT getters can block and must not delay renewal or compete with
        # the browser's in-flight DATA. Do not report cached values as actual.
        return {'ok': True, 'parameters': None}

    @staticmethod
    def _parameters(link: Any) -> dict[str, Any] | None:
        try:
            observed = link.parameters()
            if not isinstance(observed, dict):
                return None
            result: dict[str, Any] = {}
            for key, low, high in (('intervalUnits', 6, 3200), ('latency', 0, 499),
                                   ('timeoutUnits', 10, 3200)):
                value = observed.get(key)
                if type(value) in (int, float) and math.isfinite(value) and low <= value <= high:
                    result[key] = value
            if 'intervalUnits' in result:
                result['intervalMs'] = result['intervalUnits'] * 1.25
            for key in ('tx2M', 'rx2M'):
                if type(observed.get(key)) is bool:
                    result[key] = observed[key]
            return result or None
        except Exception:
            # Metadata is optional; never expose OS exceptions/identifiers or
            # turn a getter failure into another acquisition or transfer error.
            return None

    def _close_link(self, address: str, link: Any) -> bool:
        try:
            closed = link.close()
        except Exception:
            closed = False
        if not closed:
            # A failed native close is not proof of release or a reason to
            # accumulate another owned handle on this address.
            self._blocked.add(address)
        self._owners.discard(address)
        self._maybe_stop()
        return bool(closed)

    def _maybe_stop(self) -> None:
        if self._closing and not self._pending:
            loop = asyncio.get_running_loop()
            loop.call_later(0.01, loop.stop)

    async def _acquire(self, address: str, board: str) -> dict[str, Any]:
        address = address.upper()
        if address in self._blocked:
            return {'available': False, 'reason': 'native_close_unconfirmed'}
        if address in self._owners:
            return {'available': False, 'reason': 'peer_owned'}
        if len(self._owners) >= 4:
            return {'available': False, 'reason': 'lease_limit'}
        self._owners.add(address)
        task = asyncio.create_task(open_connected_image_link(address, board))
        self._pending.add(task)
        done, _ = await asyncio.wait({task}, timeout=ACQUIRE_SECONDS)
        if not done:
            task.add_done_callback(lambda result: self._late(address, result))
            return {'available': False, 'reason': 'acquire_timeout'}
        self._pending.discard(task)
        try:
            link = task.result()
        except (Exception, asyncio.CancelledError):
            self._owners.discard(address)
            self._maybe_stop()
            return {'available': False, 'reason': 'native_error'}
        if link is None:
            self._owners.discard(address)
            self._maybe_stop()
            return {'available': False, 'reason': 'connected_peer_unavailable'}
        try:
            accepted = not self._closing and link.request_throughput()
        except Exception:
            accepted = False
        if not accepted:
            parameters = self._parameters(link)
            self._close_link(address, link)
            return {'available': False, 'reason': 'shutting_down' if self._closing else 'preference_refused',
                    'parameters': parameters}
        token = secrets.token_urlsafe(24)
        timer = asyncio.get_running_loop().call_later(TTL_SECONDS, self._release, token)
        self._leases[token] = (address, link, timer)
        return {'available': True, 'lease': token, 'ttlMs': TTL_SECONDS * 1000,
                'parameters': self._parameters(link)}

    def _late(self, address: str, task: asyncio.Task) -> None:
        self._pending.discard(task)
        try:
            link = task.result()
            if link is not None:
                self._close_link(address, link)
        except (Exception, asyncio.CancelledError):
            pass
        self._owners.discard(address)
        self._maybe_stop()

    def _release(self, token: str) -> bool:
        owned = self._leases.pop(token, None)
        if owned is None:
            return True
        address, link, timer = owned
        timer.cancel()
        return self._close_link(address, link)

    async def _shutdown(self) -> None:
        for token in list(self._leases):
            self._release(token)
        self._maybe_stop()

    def close(self) -> None:
        with self._lock:
            self._closing = True
            loop = self._loop
        if loop is not None and not loop.is_closed():
            try:
                asyncio.run_coroutine_threadsafe(self._shutdown(), loop).result(timeout=2)
            except (FutureTimeout, RuntimeError):
                pass
            # A still-pending native call retains its loop until late cleanup;
            # stopping that loop early would orphan the returned native handle.
            if self._thread is not None:
                self._thread.join(timeout=0.1)
