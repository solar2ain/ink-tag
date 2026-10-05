"""Reuse the ordinary password session on an OTA driver's existing App link."""
from __future__ import annotations

import getpass
import struct
import sys
from types import SimpleNamespace
from pathlib import Path

from .activation import parse_status
from .generated_commands_v2 import CapabilityV2, CommandV2
from .local_service import local_data_directory
from .password_session import DevicePasswordSession, PasswordCredentialStore


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
        if sys.stdin.isatty() else None)
    session.snapshot = SimpleNamespace(activation=activation)
    await session._password_connect()
    if session.password_status["enabled"] and not session.password_status["authorized"]:
        raise ValueError("OTA requires device password authorization; authenticate with the shared CLI first")
