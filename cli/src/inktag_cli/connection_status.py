"""Connection feedback only; never performs BLE reads or changes a session."""
from __future__ import annotations

import json
import os
import re

import typer
from rich.text import Text

from .wait_status import pause_waiting, print_wait_message
from .recovery_feedback import recovery_connection


def report_connection(name: str, address: str, *, ready: bool = False,
                      json_output: bool | None = None) -> None:
    if json_output is None:
        json_output = os.environ.get('INKTAG_JSON_PROGRESS') == '1'
    try:
        if recovery_connection(name, address):
            return
        if json_output:
            with pause_waiting():
                typer.echo(json.dumps(dict(type='connection', state='ready' if ready else 'ble-connected',
                    bleName=name, bleAddress=address), ensure_ascii=True), err=True)
        else:
            label = 'Connected' if ready else 'BLE connected'
            clean = lambda value: re.sub(r'[\x00-\x1f\x7f-\x9f]', ' ', str(value))
            shown_name, shown_address = clean(name), clean(address)
            peer = shown_address if shown_name.casefold() == shown_address.casefold() else f'{shown_name} [{shown_address}]'
            print_wait_message(Text(f'{label}: {peer}', style='green'))
    except Exception:
        # Broken output/formatting is not an authentication failure or authority
        # to reconnect/replay a command. Cancellation is intentionally not caught.
        pass


def report_ble_connection(device) -> None:
    report_connection(getattr(device, 'name', None) or '(unnamed)',
                      str(getattr(device, 'address', device)))
