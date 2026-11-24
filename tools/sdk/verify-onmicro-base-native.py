"""Read-only base ELF/BIN/HEX verifier. Never rewrites a build artifact."""
import argparse
import json
from pathlib import Path
from onmicro_base_elf import Elf
from onmicro_base_hex import verify_hex
from onmicro_base_native import verify


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=('app', 'recovery'), required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--hex', type=Path, required=True)
    args = parser.parse_args()
    raw = args.elf.read_bytes()
    report = verify(raw, args.role, args.bin.read_bytes())
    elf = Elf(raw)
    expected = {}
    for section in elf.sections:
        if not section[2] & 2 or section[1] == 8 or not section[5]:
            continue
        for segment in elf.loads:
            if segment[2] <= section[3] < section[3] + section[5] <= segment[2] + segment[4]:
                physical = segment[3] + section[3] - segment[2]
                expected.update({physical + i: value for i, value in enumerate(
                    elf.slice(section[4], section[5]))})
                break
        else:
            raise ValueError('Allocated ELF section lacks its native load segment')
    verify_hex(elf, args.hex.read_text(encoding='ascii'), expected)
    args.elf.with_suffix('.native-check.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('base native verified:', args.role, report['rawBytes'], 'bytes')


if __name__ == '__main__':
    main()
