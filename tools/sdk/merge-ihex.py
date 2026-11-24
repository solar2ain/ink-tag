#!/usr/bin/env python3
"""Merge non-overlapping Intel HEX images without third-party dependencies."""

import argparse
from pathlib import Path


def read_hex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    upper = 0
    segment = 0
    for line_number, raw in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_number}: invalid Intel HEX record")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise ValueError(f"{path}:{line_number}: invalid length/checksum")
        length = record[0]
        offset = (record[1] << 8) | record[2]
        kind = record[3]
        data = record[4:4 + length]
        if kind == 0x00:
            base = upper + segment + offset
            for index, value in enumerate(data):
                address = base + index
                previous = memory.get(address)
                if previous is not None and previous != value:
                    raise ValueError(f"{path}: conflicting byte at 0x{address:08X}")
                memory[address] = value
        elif kind == 0x02:
            if length != 2:
                raise ValueError(f"{path}:{line_number}: invalid segment record")
            segment = int.from_bytes(data, "big") << 4
            upper = 0
        elif kind == 0x04:
            if length != 2:
                raise ValueError(f"{path}:{line_number}: invalid linear address record")
            upper = int.from_bytes(data, "big") << 16
            segment = 0
        elif kind == 0x01:
            break
        elif kind not in (0x03, 0x05):
            raise ValueError(f"{path}:{line_number}: unsupported record type 0x{kind:02X}")
    return memory


def record(address: int, kind: int, data: bytes) -> str:
    body = bytes((len(data), (address >> 8) & 0xFF, address & 0xFF, kind)) + data
    checksum = (-sum(body)) & 0xFF
    return ":" + (body + bytes((checksum,))).hex().upper()


def write_hex(path: Path, memory: dict[int, int]) -> None:
    lines: list[str] = []
    current_upper = None
    addresses = sorted(memory)
    index = 0
    while index < len(addresses):
        start = addresses[index]
        upper = start >> 16
        if upper != current_upper:
            lines.append(record(0, 0x04, upper.to_bytes(2, "big")))
            current_upper = upper
        low = start & 0xFFFF
        chunk = bytearray((memory[start],))
        index += 1
        while index < len(addresses) and len(chunk) < 16:
            address = addresses[index]
            if address != start + len(chunk) or address >> 16 != upper:
                break
            chunk.append(memory[address])
            index += 1
        lines.append(record(low, 0x00, bytes(chunk)))
    lines.append(record(0, 0x01, b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("inputs", type=Path, nargs="+")
    args = parser.parse_args()
    merged: dict[int, int] = {}
    for source in args.inputs:
        for address, value in read_hex(source).items():
            previous = merged.get(address)
            if previous is not None and previous != value:
                raise SystemExit(f"Conflicting byte at 0x{address:08X} from {source}")
            merged[address] = value
    write_hex(args.output, merged)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
