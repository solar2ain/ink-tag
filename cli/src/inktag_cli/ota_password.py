"""Reuse the ordinary password session on an OTA driver's existing App link."""
from __future__ import annotations

import asyncio
import getpass
import struct
import sys
from contextlib import asynccontextmanager
from pathlib import Path
from types import SimpleNamespace

from .activation import parse_status
from .generated_commands_v2 import CapabilityV2, CommandV2, PROTOCOL_V2_VERSION
from .local_service import local_data_directory
from .password_session import DevicePasswordSession, PasswordCredentialStore


def ota_password_prompt_available() -> bool:
    """Only a visible interactive terminal may ask for an OTA password.

    JSON OTA captures the child's output, even when its inherited stdin still
    claims to be a TTY. A captured/redirected prompt must never block the run.
    """
    return all(stream is not None and stream.isatty()
               for stream in (sys.stdin, sys.stdout, sys.stderr))


@asynccontextmanager
async def ota_application_requests(client, *, disconnect_on_exit: bool = False):
    """Borrow an OTA driver's App link; retain its CCCD through admission.

    Reuse the canonical V2 transaction/error decoder without running ordinary
    connection bootstrap, diagnostics or opportunistic clock synchronization.
    The caller, not this adapter, owns connection and disconnection. A caller
    that immediately closes the link may leave CCCDs to that teardown: after
    accepted buttonless admission the peripheral may already have disappeared
    while Windows still reports is_connected=True. Do not write another CCCD
    into that expected disconnect. No request/authentication error is ignored.
    """
    from .protocol import COMMAND_UUID
    from .transport import InkTagClient

    session = InkTagClient(auto_sync_time=False)
    session._client = client
    # Redact all responses, including before capabilities/auth are known.
    session._v2_capabilities = int(CapabilityV2.DEVICE_PASSWORD)
    await client.start_notify(COMMAND_UUID, session._on_notification)
    try:
        if sys.platform == "win32":
            await asyncio.sleep(0.5)  # Same bounded CCCD settling as normal CLI.
        hello = await session.request_v2(CommandV2.HELLO)
        if len(hello) < 6 or hello[0] != 1 or hello[1] != PROTOCOL_V2_VERSION:
            raise ValueError("OTA application returned invalid V2 handshake")
        yield session.request_v2
    finally:
        session._client = None
        if not disconnect_on_exit and client.is_connected:
            await client.stop_notify(COMMAND_UUID)


async def authorize_ota_application(request, directory: Path | None = None) -> None:
    """No new BLE connection, clock write, QR read or Recovery authentication."""
    capabilities = await request(CommandV2.GET_CAPABILITIES)
    if len(capabilities) < 17 or capabilities[0] != 1:
        raise ValueError("OTA application returned invalid capabilities")
    flags = struct.unpack_from("<I", capabilities, 1)[0]
    if not flags & int(CapabilityV2.DEVICE_PASSWORD):
        return
    activation = parse_status(await request(CommandV2.GET_ACTIVATION_STATUS))

    class Session(DevicePasswordSession):
        def supports_capability(self, capability):
            return bool(flags & int(capability))

        async def request_v2(self, opcode, payload=b"", **kwargs):
            return await request(opcode, payload)

    session = Session()
    session._password_init(PasswordCredentialStore(local_data_directory(directory)),
        (lambda: getpass.getpass("Device password (saved credential unavailable): "))
        if ota_password_prompt_available() else None)
    session.snapshot = SimpleNamespace(activation=activation)
    await session._password_connect()
    if session.password_status["enabled"] and not session.password_status["authorized"]:
        raise ValueError("OTA requires device password authorization; authenticate with the shared CLI first")
