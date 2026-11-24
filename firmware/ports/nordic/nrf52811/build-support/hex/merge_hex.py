#!/usr/bin/env python3
import sys


def checksum(record_bytes):
    return (-sum(record_bytes)) & 0xFF


def parse_hex(path):
    data = {}
    base = 0

    with open(path, "r", encoding="ascii") as f:
        for line_no, raw_line in enumerate(f, 1):
            line = raw_line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError(f"{path}:{line_no}: missing ':'")

            payload = bytes.fromhex(line[1:])
            length = payload[0]
            offset = (payload[1] << 8) | payload[2]
            record_type = payload[3]
            body = payload[4 : 4 + length]
            got_checksum = payload[4 + length]

            if checksum(payload[: 4 + length]) != got_checksum:
                raise ValueError(f"{path}:{line_no}: checksum mismatch")

            if record_type == 0x00:
                address = base + offset
                for index, value in enumerate(body):
                    data[address + index] = value
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                base = int.from_bytes(body, "big") << 4
            elif record_type == 0x04:
                base = int.from_bytes(body, "big") << 16
            else:
                continue

    return data


def emit_record(record_type, address, body):
    head = bytes([len(body), (address >> 8) & 0xFF, address & 0xFF, record_type])
    record = head + body
    return ":" + record.hex().upper() + f"{checksum(record):02X}"


def write_hex(path, data):
    current_upper = None
    addresses = sorted(data)

    with open(path, "w", encoding="ascii", newline="\n") as f:
        index = 0
        while index < len(addresses):
            address = addresses[index]
            upper = address >> 16
            if upper != current_upper:
                current_upper = upper
                f.write(emit_record(0x04, 0, upper.to_bytes(2, "big")) + "\n")

            chunk = bytearray()
            start = address
            while index < len(addresses) and len(chunk) < 16:
                expected = start + len(chunk)
                if addresses[index] != expected or (addresses[index] >> 16) != current_upper:
                    break
                chunk.append(data[addresses[index]])
                index += 1

            f.write(emit_record(0x00, start & 0xFFFF, bytes(chunk)) + "\n")

        f.write(":00000001FF\n")


def main(argv):
    if len(argv) < 4:
        print(f"usage: {argv[0]} OUTPUT.hex INPUT1.hex INPUT2.hex [...]", file=sys.stderr)
        return 2

    merged = {}
    for input_path in argv[2:]:
        for address, value in parse_hex(input_path).items():
            old_value = merged.get(address)
            if old_value is not None and old_value != value:
                print(f"overlap at 0x{address:08X}: 0x{old_value:02X} != 0x{value:02X}", file=sys.stderr)
                return 1
            merged[address] = value

    write_hex(argv[1], merged)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
