"""Read existing Recovery information without entering, resuming or updating it."""
from __future__ import annotations

import re

from .transport import scan_device_status


async def read_status(name: str | None, address: str | None,
                      scan_timeout: float, connect_timeout: float) -> dict:
    selected_name, selected_address = name, address
    grammar = r"(?:IKT|InkTag)_(GR|OM|TL|NRF|DA)_[0-9A-F]{4}_OTA"
    match = re.fullmatch(grammar, selected_name or "", re.IGNORECASE)
    if (name or address) and match is None:
        if name and not address:
            from .command_framework import resolve_batch
            ((selected_name, selected_address),) = await resolve_batch(
                ((name, None),), scan_timeout)
        else:
            observed = await scan_device_status(
                scan_timeout, address=address, **({"name": name} if name else {}))
            selected_name, selected_address = observed.name, observed.address
        match = re.fullmatch(grammar, selected_name or "", re.IGNORECASE)
        if match is None:
            raise ValueError("Select a supported InkTag Recovery device ending in _OTA")
    family = match.group(1).upper() if match else "TL"
    if family == "NRF":
        from ._drivers.ota_nrf52811 import read_recovery_status
    elif family == "DA":
        from ._drivers.ota_da14585 import read_recovery_status
    elif family == "GR":
        from ._drivers.ota_goodix import read_recovery_status
    elif family == "OM":
        from ._drivers.ota_om6626 import read_recovery_status
    else:
        from ._drivers.ota_tlsr8359 import read_recovery_status
    return await read_recovery_status(
        name=selected_name, address=selected_address,
        scan_timeout=scan_timeout, connect_timeout=connect_timeout)
