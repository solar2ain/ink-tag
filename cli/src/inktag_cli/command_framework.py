"""User command ordering, scoped selection and sequential batch execution."""
from __future__ import annotations

import asyncio
from contextlib import redirect_stdout, redirect_stderr
from dataclasses import replace
from functools import wraps
from io import StringIO
import json
import re
import sys
from typing import get_type_hints

import typer
from typer.core import TyperCommand, TyperGroup

from .wait_status import waiting


class UserCommand(TyperCommand):
    def invoke(self, context):
        # Help and parameter prompts happen before invoke; foreground server
        # logs retain their normal presentation. Labels never include arguments.
        path = []
        node = context
        while node.parent is not None:
            path.append(node.info_name)
            node = node.parent
        path.reverse()
        background_stream = path[:2] in (['webui', 'serve'], ['webui', 'log'])
        enabled = not getattr(context.obj, 'json_output', False) and not background_stream
        with waiting(' '.join(path) or self.name, enabled=enabled):
            return super().invoke(context)


class UserTyper(typer.Typer):
    def command(self, *args, **kwargs):
        kwargs.setdefault('cls', UserCommand)
        return super().command(*args, **kwargs)


class UserGroup(TyperGroup):
    def list_commands(self, context):
        order = {
            'setting': ['show','ble-name','device-name','device-url','screen','pins','button','font','password','buzzer','reset-settings','clear-user-data'],
        }.get(self.name, [])
        return sorted(self.commands, key=lambda name: order.index(name) if name in order else len(order))


def user_app(**kwargs):
    kwargs.setdefault('no_args_is_help', True)
    kwargs.setdefault('context_settings', {'help_option_names': ['-h', '--help']})
    kwargs.setdefault('cls', UserGroup)
    return UserTyper(**kwargs)


def selections(names: str | None, addresses: str | None) -> tuple[tuple[str | None, str | None], ...]:
    parts = []
    for value, address in ((names, False), (addresses, True)):
        entries = []
        if value is None:
            parts.append(entries)
            continue
        for item in value.split(','):
            item = item.strip()
            if not item or any(ord(c) < 32 for c in item):
                raise typer.BadParameter('Provide nonempty comma-separated BLE names or addresses')
            if address and not (re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', item)
                                or re.fullmatch(r'[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}', item)):
                raise typer.BadParameter('BLE address must be a MAC address or macOS CoreBluetooth identifier')
            entries.append(item)
        parts.append(entries)
    names_list, addresses_list = parts
    if names_list and addresses_list:
        if len(names_list) != len(addresses_list):
            raise typer.BadParameter('When combining names and addresses, provide the same number; they pair by position')
        pairs = zip(names_list, addresses_list)
    else:
        pairs = ((name, None) for name in names_list) if names_list else ((None,address) for address in addresses_list)
    result = {}
    for name,address in pairs:
        key = (address or name).casefold()
        result.setdefault(key, (name,address))
    return tuple(result.values())


async def resolve_batch(selected, timeout):
    from .transport import InkTagError, scan_devices
    inventory = await scan_devices(timeout) if any(name and not address for name,address in selected) else []
    result = {}
    for name, address in selected:
        if name and not address:
            exact = [d for d in inventory if d.name.casefold() == name.casefold()]
            matches = exact or [d for d in inventory if name.casefold() in d.name.casefold()]
            if len(matches) != 1:
                choices = ', '.join(f'{d.name} [{d.address}]' for d in matches)
                raise InkTagError(f'BLE name {name!r} matched {len(matches)} devices; select an exact name/address. {choices}')
            name, address = matches[0].name, matches[0].address
        result.setdefault(address.casefold(), (name, address))
    return tuple(result.values())


def device_handler(callback, *, offline=False):
    """Reuse the actual guarded handler; never replay a failed device operation."""
    if getattr(callback, '_device_handler', False):
        return callback

    @wraps(callback)
    def invoke(*args, **kwargs):
        context = kwargs.get('context') or next((a for a in args if hasattr(a, 'obj')), None)
        options = context.obj if context else None
        selected = getattr(options, 'target_selections', ())
        if offline or len(selected) < 2:
            return callback(*args, **kwargs)
        from .transport import InkTagError
        try:
            targets = asyncio.run(resolve_batch(selected, options.scan_timeout))
        except (InkTagError, ValueError, OSError) as error:
            raise typer.BadParameter(str(error)) from None
        results = []
        try:
            for name, address in targets:
                context.obj = replace(options, device=name, address=address, target_selections=())
                captured = StringIO(); diagnostic = StringIO()
                try:
                    if options.json_output:
                        with redirect_stdout(captured), redirect_stderr(diagnostic):
                            callback(*args, **kwargs)
                    else:
                        typer.echo(f'[{name or address}]', err=True)
                        callback(*args, **kwargs)
                    row = {'bleName': name, 'bleAddress': address, 'status': 'success'}
                except typer.Abort:
                    raise
                except Exception as error:
                    if isinstance(error, typer.Exit) and error.exit_code == 130:
                        raise
                    row = {'bleName': name, 'bleAddress': address,
                           'status': 'success' if isinstance(error,typer.Exit) and error.exit_code==0 else 'failed-or-unconfirmed',
                           'error': str(error) or type(error).__name__}
                details = diagnostic.getvalue().strip()
                if details:
                    sys.stderr.write(f'[{name or address}] {details}\n')
                    if row['status'] != 'success':
                        try:
                            row['error'] = json.loads(details).get('error',details)
                        except ValueError:
                            row['error'] = details
                output = captured.getvalue().strip()
                if output:
                    try:
                        row['result'] = json.loads(output)
                    except ValueError:
                        row['output'] = output
                results.append(row)
        finally:
            context.obj = options
        if options.json_output:
            typer.echo(json.dumps({'devices': results}, ensure_ascii=False))
        else:
            for row in results:
                typer.echo(f"{row['bleAddress']}: {row['status']}", err=True)
        if any(row['status'] != 'success' for row in results):
            raise typer.Exit(1)
    invoke._device_handler = True
    invoke.__annotations__ = get_type_hints(callback, include_extras=True)
    return invoke
