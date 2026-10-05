"""Bounded UTC-slot and query-batch scheduling for InkTag Find v2."""

from __future__ import annotations

import secrets
from collections.abc import Callable, Sequence
from typing import TypeVar

ROTATION_PERIOD_SECONDS = 900
QUERY_BATCH_MIN_KEYS = 4
QUERY_BATCH_MAX_KEYS = 8
QUERY_BATCH_DELAY_MIN_MS = 600
QUERY_BATCH_DELAY_MAX_MS = 1200

_T = TypeVar("_T")


def slot_for_utc(unix_seconds: int) -> int:
    value = int(unix_seconds)
    if value < 0 or value > 0xFFFFFFFFFFFFFFFF:
        raise ValueError("UTC timestamp is outside the unsigned 64-bit range")
    return value // ROTATION_PERIOD_SECONDS


def slots_for_window(start_seconds: int, end_seconds: int) -> tuple[int, ...]:
    start = slot_for_utc(start_seconds)
    end = slot_for_utc(end_seconds)
    if end < start:
        raise ValueError("Find query window ends before it starts")
    return tuple(range(start, end + 1))


def query_key_batches(
    items: Sequence[_T],
    randbelow: Callable[[int], int] = secrets.randbelow,
) -> tuple[tuple[_T, ...], ...]:
    """Split one Apple query window into jittered, bounded key batches.

    Every non-empty batch contains 4–8 keys unless the complete request itself
    contains fewer than four.  The selection always leaves enough keys for a
    final full-sized batch, avoiding a trailing one-to-three-key request.
    """
    batches: list[tuple[_T, ...]] = []
    offset = 0
    total = len(items)
    while offset < total:
        remaining = total - offset
        if remaining <= QUERY_BATCH_MAX_KEYS:
            size = remaining
        else:
            maximum = min(QUERY_BATCH_MAX_KEYS, remaining - QUERY_BATCH_MIN_KEYS)
            size = QUERY_BATCH_MIN_KEYS + randbelow(
                maximum - QUERY_BATCH_MIN_KEYS + 1
            )
        batches.append(tuple(items[offset : offset + size]))
        offset += size
    return tuple(batches)


def query_batch_delay_seconds(
    randbelow: Callable[[int], int] = secrets.randbelow,
) -> float:
    span = QUERY_BATCH_DELAY_MAX_MS - QUERY_BATCH_DELAY_MIN_MS + 1
    return (QUERY_BATCH_DELAY_MIN_MS + randbelow(span)) / 1000.0
