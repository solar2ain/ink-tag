"""Terminal-only waiting feedback; never changes task execution or JSON output."""
from __future__ import annotations

from contextlib import contextmanager
from contextvars import ContextVar
import sys
import time

from rich.console import Console as RichConsole, Group
from rich.live import Live
from rich.progress_bar import ProgressBar
from rich.spinner import Spinner
from rich.text import Text


_current: ContextVar[WaitStatus | None] = ContextVar('inktag_wait_status', default=None)


class Console(RichConsole):
    @property
    def is_dumb_terminal(self):
        # Modern Windows console capability is detected by Rich/Win32, whereas
        # inherited TERM=dumb is common in launchers and describes no capability.
        return super().is_dumb_terminal and not (
            sys.platform == 'win32' and not self.legacy_windows)


class WaitStatus:
    def __init__(self, description: str, *, console: Console | None = None):
        self.console = console or Console(stderr=True)
        self.description = description
        self.started = time.monotonic()
        self.completed = self.total = None
        self.spinner = Spinner('dots', style='cyan')
        self.live = Live(self, console=self.console, refresh_per_second=8,
                         transient=True)
        self.paused = 0
        self.finished = False
        self.detail = None

    def __rich__(self):
        elapsed = max(0.0, time.monotonic() - self.started)
        line = Text(self.description)
        if self.total:
            percent = min(100.0, max(0.0, self.completed / self.total * 100))
            line.append(f'  {percent:.0f}%')
        line.append(f'  elapsed {elapsed:.1f}s', style='dim')
        self.spinner.update(text=line)
        rows = [self.spinner]
        if self.detail:
            rows.append(Text(self.detail, no_wrap=True, overflow='ellipsis'))
        if self.total:
            rows.append(ProgressBar(total=self.total, completed=self.completed))
        return Group(*rows)

    def update(self, description: str, completed: int | None = None,
               total: int | None = None, *, detail: str | None = None):
        self.description = description
        self.completed, self.total = completed, total
        self.detail = detail


@contextmanager
def waiting(description: str, *, enabled: bool = True, console: Console | None = None):
    """One display per command; pipe/CI/dumb terminals stay plain and silent."""
    active = _current.get()
    if active is not None and not active.finished:
        yield active
        return
    output = console or Console(stderr=True)
    if not enabled or not output.is_terminal or output.is_dumb_terminal:
        yield None
        return
    status = WaitStatus(description, console=output)
    token = _current.set(status)
    try:
        with status.live:
            yield status
    finally:
        status.finished = True
        _current.reset(token)


@contextmanager
def pause_waiting():
    """Keep confirmation/password input readable; elapsed retains wall time."""
    status = _current.get()
    if status is None:
        yield
        return
    status.paused += 1
    if status.paused == 1:
        status.live.stop()
    try:
        yield
    finally:
        status.paused -= 1
        if status.paused == 0 and not status.finished:
            status.live.start(refresh=True)


def finish_waiting():
    """Remove the complete live region before printing final output."""
    status = _current.get()
    if status is not None and not status.finished:
        status.finished = True
        status.live.stop()


def print_wait_message(message):
    """Write above the active display using its console and cursor ownership."""
    status = _current.get()
    output = status.console if status is not None and not status.finished else Console(stderr=True)
    output.print(message)
