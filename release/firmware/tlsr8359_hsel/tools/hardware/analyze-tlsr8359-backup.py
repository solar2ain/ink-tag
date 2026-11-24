#!/usr/bin/env python3
"""Produce a read-only first-pass report for a TLSR8359 Flash backup."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


SECTOR_SIZE = 0x1000
WINDOW_SIZE = 0x10000
MIRROR_PERIODS = (0x10000, 0x20000, 0x40000)
STRING_TOKENS = (
    "HANS",
    "NOWA",
    "NEBULAR",
    "TLSR",
    "TELINK",
    "EPD",
    "INK",
    "DISPLAY",
    "NFC",
    "BLE",
    "OTA",
    "VERSION",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def used_ranges(data: bytes) -> list[tuple[int, int, int]]:
    ranges: list[tuple[int, int, int]] = []
    range_start: int | None = None
    range_end = 0
    non_ff_bytes = 0
    for offset in range(0, len(data), SECTOR_SIZE):
        sector = data[offset : offset + SECTOR_SIZE]
        count = sum(byte != 0xFF for byte in sector)
        if count:
            if range_start is None:
                range_start = offset
                non_ff_bytes = 0
            range_end = offset + len(sector)
            non_ff_bytes += count
        elif range_start is not None:
            ranges.append((range_start, range_end, non_ff_bytes))
            range_start = None
    if range_start is not None:
        ranges.append((range_start, range_end, non_ff_bytes))
    return ranges


def printable_runs(data: bytes, min_length: int = 5) -> list[tuple[int, str]]:
    runs: list[tuple[int, str]] = []
    start: int | None = None
    chars: list[str] = []
    for offset, byte in enumerate(data):
        if 32 <= byte < 127:
            if start is None:
                start = offset
            chars.append(chr(byte))
            continue
        if start is not None and len(chars) >= min_length:
            text = "".join(chars)
            if any(token in text.upper() for token in STRING_TOKENS):
                runs.append((start, text))
        start = None
        chars = []
    if start is not None and len(chars) >= min_length:
        text = "".join(chars)
        if any(token in text.upper() for token in STRING_TOKENS):
            runs.append((start, text))
    return runs


def periodic_match(data: bytes, period: int) -> bool:
    if period >= len(data) or len(data) % period:
        return False
    return all(
        data[offset : offset + period] == data[:period]
        for offset in range(period, len(data), period)
    )


def build_report(path: Path, base_address: int = 0) -> str:
    data = path.read_bytes()
    lines = [
        f"file: {path}",
        f"base address: 0x{base_address:06X}",
        f"length: {len(data)} bytes (0x{len(data):X})",
        f"sha256: {sha256(data)}",
        "",
    ]
    if not data:
        lines.append("status: empty file")
        return "\n".join(lines) + "\n"

    ff_count = data.count(0xFF)
    zero_count = data.count(0x00)
    non_ff_offsets = [offset for offset, byte in enumerate(data) if byte != 0xFF]
    lines.extend(
        (
            f"0xFF bytes: {ff_count} ({ff_count / len(data):.2%})",
            f"0x00 bytes: {zero_count} ({zero_count / len(data):.2%})",
            f"first non-0xFF byte: 0x{base_address + non_ff_offsets[0]:06X}" if non_ff_offsets else "first non-0xFF byte: none",
            f"last non-0xFF byte: 0x{base_address + non_ff_offsets[-1]:06X}" if non_ff_offsets else "last non-0xFF byte: none",
            "",
            "mirror/window findings:",
        )
    )
    for period in MIRROR_PERIODS:
        if period < len(data):
            lines.append(f"  - period 0x{period:X}: {'repeats' if periodic_match(data, period) else 'not repeated'}")
    if len(data) >= 0x80000:
        lines.append(
            "  - 256 KiB halves: "
            + ("identical" if data[:0x40000] == data[0x40000:0x80000] else "different")
        )
    lines.append("")

    lines.append("64 KiB window hashes and occupancy:")
    for offset in range(0, len(data), WINDOW_SIZE):
        window = data[offset : offset + WINDOW_SIZE]
        non_ff = sum(byte != 0xFF for byte in window)
        lines.append(
            f"  - 0x{base_address + offset:06X}-0x{base_address + offset + len(window) - 1:06X}: "
            f"non_ff={non_ff:6d} sha256={sha256(window)}"
        )
    lines.append("")

    lines.append("non-empty 4 KiB sector ranges:")
    ranges = used_ranges(data)
    if ranges:
        for start, end, count in ranges:
            lines.append(
                f"  - 0x{base_address + start:06X}-0x{base_address + end - 1:06X}: "
                f"non_ff={count}"
            )
    else:
        lines.append("  - none")
    lines.append("")

    lines.append("relevant ASCII strings:")
    strings = printable_runs(data)
    if strings:
        for offset, value in strings[:80]:
            lines.append(f"  - 0x{base_address + offset:06X}: {value[:160]}")
        if len(strings) > 80:
            lines.append(f"  - ... {len(strings) - 80} more")
    else:
        lines.append("  - none")
    lines.extend(
        (
            "",
            "interpretation limits:",
            "  - Two identical reads prove transport consistency, not physical capacity by themselves.",
            "  - Distinct programmed data above 0x40000 is strong evidence against a 256 KiB mirror.",
            "  - A package marking, Flash ID, or non-mirrored full-device backup is still preferred for capacity qualification.",
        )
    )
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backup", type=Path)
    parser.add_argument("--base-address", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.backup.is_file():
        parser.error(f"backup not found: {args.backup}")
    if args.base_address < 0 or args.base_address + args.backup.stat().st_size > 0x1000000:
        parser.error("base address and backup length exceed the 24-bit Flash address space")
    report = build_report(args.backup, args.base_address)
    if args.output:
        args.output.write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
