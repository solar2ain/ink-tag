"""Presentation for a CLI Recovery return; never controls a device operation."""
from __future__ import annotations

import builtins
from contextlib import contextmanager
from contextvars import ContextVar
import json
import re
import time

import typer
from rich.text import Text

from .wait_status import print_wait_message


_current = ContextVar('inktag_recovery_feedback', default=None)


class RecoveryFeedback:
    def __init__(self, name, *, verbose, json_output):
        self.name = name
        self.verbose, self.json_output = verbose, json_output
        self.started = time.monotonic()
        self.connected = False

    def emit(self, stage, message):
        try:
            elapsed = round(time.monotonic() - self.started, 1)
            message = re.sub(r'[\x00-\x1f\x7f-\x9f]', ' ', message)
            if self.json_output:
                typer.echo(json.dumps(dict(type='recovery-progress', stage=stage,
                    message=message, elapsedSeconds=elapsed), ensure_ascii=True), err=True)
            else:
                print_wait_message(Text(f'{message}  ({elapsed:.1f}s)', style='dim' if stage == 'detail' else 'cyan'))
        except Exception:
            pass  # Output failure cannot authorize a retry or abort a cleanup.

    def connection(self, name, address):
        if not self.connected:
            self.connected = True
            if not name or name.casefold() == address.casefold() or name == '(unnamed)':
                name = self.name
            peer = f'{name} [{address}]' if name and name.casefold() != address.casefold() else address
            self.emit('connected', f'Recovery connected: {peer}')
        elif self.verbose:
            self.emit('detail', f'BLE link connected: {address}')


@contextmanager
def recovery_feedback(name, *, verbose=False, json_output=False):
    status = RecoveryFeedback(name, verbose=verbose, json_output=json_output)
    token = _current.set(status)
    try:
        status.emit('connecting', 'Connecting to Recovery')
        yield status
    finally:
        _current.reset(token)


def recovery_phase(stage):
    status = _current.get()
    if status is not None:
        messages = {'request': 'Requesting return to application',
                    'waiting-app': 'Waiting for application', 'app-ready': 'Application ready'}
        try: status.emit(stage, messages[stage])
        except Exception: pass  # A display failure must never replay control.


def recovery_selected(name):
    status = _current.get()
    if status is not None:
        status.name = name


def recovery_connection(name, address):
    status = _current.get()
    if status is None:
        return False
    status.connection(name, address)
    return True


def driver_print(*values, **kwargs):
    """Native scripts keep their original output outside this explicit scope."""
    status = _current.get()
    if status is None:
        return builtins.print(*values, **kwargs)
    if status.verbose:
        try:
            text = (kwargs.get('sep') or ' ').join(str(value) for value in values)
            text = re.sub(r'[\x00-\x1f\x7f-\x9f]', ' ', text).strip()
            if text and text != '.':
                status.emit('detail', text)
        except Exception:
            pass


def recovery_summary(result, elapsed):
    """A common human summary, without relabeling native attestation evidence."""
    from .board_ids import display_build_target
    image = result.get('applicationImage') or {}
    name = result.get('applicationName') or image.get('applicationName')
    if name and name.casefold() == str(result.get('address', '')).casefold():
        name = None
    summary = dict(ok=result.get('ok'), platform=result.get('platform'), address=result.get('address'),
                   applicationName=name, elapsedSeconds=round(elapsed, 1))
    if result.get('board'):
        summary['buildTarget'] = display_build_target(result['board'])
    if 'applicationVerified' in result:
        summary['applicationVerified'] = result['applicationVerified']
    if result.get('notificationCleanupError'):
        summary['notificationCleanupError'] = result['notificationCleanupError']
    return summary
