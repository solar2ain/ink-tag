"""Generate a configured 512-KiB TLSR Free image offline; never access a device."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from ble_factory_address import encode, validate
import tlsr_full_flash_address as factory_address
from tlsr_full_flash_plan import plan
from tlsr_full_flash_state import BASE_BYTES, FACTORY, defaults, initialize_full_state, sha

# Released0.2.1 codecs support explicit addresses but predate generic address
# provisioning. A missing capability stays false; no firmware trust fallback.
INIT_MARKER = getattr(factory_address, 'INIT_MARKER', 0x7F10D)
INIT_PENDING = getattr(factory_address, 'INIT_PENDING', 0xA5)


def generate(firmware: Path, config: Path, mac: str | None = None, address_type: str = 'Public') -> tuple[bytes, dict]:
    if mac is not None and not re.fullmatch(r'(?:[0-9a-fA-F]{12}|[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})', mac):
        raise ValueError('MAC must be six hexadecimal bytes, e.g. A4:C1:38:12:34:56')
    if address_type not in ('Public', 'StaticRandom'):
        raise ValueError('Address type must be Public or StaticRandom')
    kind = int(address_type == 'StaticRandom')
    address = validate(bytes.fromhex(mac.replace(':', '')), kind) if mac is not None else None
    template, selected = plan(firmware, config, require_write=True)
    if len(template) != BASE_BYTES or selected['application_start'] != 0x1000:
        raise ValueError('Offline export requires the current paired trusted-entry Full package')
    image = bytearray(template)
    if address is None and not selected.get('automatic_addresses', False):
        raise ValueError('Generic bin requires automatic-MAC-aware App AND Recovery; supplied firmware does not support it')
    legacy = address[::-1] if address is not None else None
    if address is None:
        image[INIT_MARKER] = INIT_PENDING
    else:
        image[FACTORY:FACTORY + 6] = legacy
        image[0x7F160:0x7F170] = encode(address, kind) + b'\xff' * 5
    records, links = defaults(legacy, bytes.fromhex(selected['screen_wire_hex']),
                             bytes.fromhex(selected['pin_wire_hex']),
                             bytes.fromhex(selected['aux_wire_hex']), selected['board_id'])
    state = initialize_full_state(records, selected['compatibility_epoch'], confirmed=True)
    for offset, data, _ in state['writes']:
        image[offset:offset + len(data)] = data
    result = bytes(image)
    report = dict(
        schema='inktag-tlsr-offline-bin-v1', device_access=False,
        image_bytes=len(result), image_sha256=sha(result), load_address=0,
        write_end_exclusive=BASE_BYTES, firmware_package_sha256=selected['base_package_sha256'],
        config_sha256=selected['configuration_sha256'], board=selected['board'],
        board_marking=selected['board_marking'], layout=selected['layout'],
        compatibility_epoch=selected['compatibility_epoch'],
        mac=address.hex(':').upper() if address is not None else None,
        address_type=address_type if address is not None else 'StaticRandom',
        automatic_mac=address is None,
        first_boot_identity='SDK RNG generates and persists an address before BLE' if address is None else 'supplied MAC',
        initial_edition='Free', pro_preserved=False,
        application_bytes_unchanged=True, recovery_bytes_unchanged=True, stage0_bytes_unchanged=True,
        defaults=links, first_boot_display=selected['first_boot_display'],
        original_factory_preserved=False, crystal_trim='0xFF: existing SDK default; not original calibration',
        base_image_store_only=selected['base_image_store_only'],
        upper_flash='Not included. On qualified 1-MiB boards, erase/write only 0x00000-0x7FFFF to retain upper image slots; old valid images may remain.',
        warnings=[
            'Burning replaces lower512-KiB settings, credentials, Pro, fonts, images and original Factory/calibration data. Back up separately if needed.',
            'Omit MAC for a reusable generic image; explicit MAC images are for one identity. StaticRandom requires high bits11.',
            'BDT must load at address0 and verify after writing. No chip erase if upper Flash must be preserved.',
            'No device capacity, calibration, RF, screen or physical BDT qualification is performed by this generator.',
            *selected['qualification_warnings'],
        ])
    return result, report


def write_output(output: Path, image: bytes, report: dict) -> None:
    if output.suffix.lower() != '.bin':
        raise ValueError('Output must have a .bin extension')
    companion = output.with_suffix('.json')
    if output.exists() or companion.exists() or output.is_symlink() or companion.is_symlink():
        raise ValueError('Output or companion report exists; no overwrite')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects existing files, including a racing writer.
    with output.open('xb') as stream:
        stream.write(image)
    try:
        with companion.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(report, indent=2, ensure_ascii=False) + '\n')
    except OSError:
        output.unlink()
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--mac', help='Optional explicit MAC; omitted creates a reusable generic image')
    parser.add_argument('--address-type', choices=('Public', 'StaticRandom'), default='Public')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    try:
        image, report = generate(args.firmware, args.config, args.mac, args.address_type)
        report.update(output=str(args.output.resolve()), dry_run=args.dry_run)
        if not args.dry_run:
            write_output(args.output, image, report)
        print(json.dumps(report, indent=2, ensure_ascii=False))
    except (ValueError, OSError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
