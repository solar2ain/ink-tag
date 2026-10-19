"""Small host-side helpers for monotonic Find report summaries."""

from __future__ import annotations

from datetime import UTC, datetime
from typing import Iterable


def parse_report_timestamp(value: object) -> datetime | None:
    """Parse an ISO report time and normalize it to UTC for comparison."""
    if value is None:
        return None
    try:
        parsed = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=UTC)
    return parsed.astimezone(UTC)


def latest_report_timestamp(values: Iterable[object]) -> str | None:
    """Return the original representation of the newest valid report time."""
    latest_value: str | None = None
    latest_time: datetime | None = None
    for value in values:
        parsed = parse_report_timestamp(value)
        if parsed is not None and (latest_time is None or parsed > latest_time):
            latest_value = str(value)
            latest_time = parsed
    return latest_value
