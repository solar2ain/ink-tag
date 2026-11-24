"""Offline configuration-based full-flash admission. No serial, SWS or BLE client."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from tlsr_full_flash_base import configure, sha
from tlsr_full_flash_config import ROOT, read_board_config
from tlsr_full_flash_contract import resolve_contract


def plan(base: Path, config: Path, *, require_write: bool = False,
         embedded_font_bench: bool = False, skip_readback: bool = False,
         ) -> tuple[bytes, dict]:
    image, review = configure(base, config)
    registry = json.loads((ROOT / 'firmware/boards/boards.json').read_text(encoding='utf-8'))['boards']
    entry = registry[review['board']]
    contract, warnings = resolve_contract(entry, review['board_id'])
    blockers = []
    if not review.get('typed_addresses'):
        blockers.append('Full requires typed-address-aware App and Recovery; rebuild app-full.zip.')
    if embedded_font_bench:
        raise ValueError('Embedded-font bench is retired; native ITFA3 uses the normal configured Full path.')
    if not review.get('package_verified'):
        blockers.append('Package integrity validation is required.')
    if review['board'] in ('hs_hsel3_266q', 'hs_hsel5101_200bwr'):
        warnings.append('Supply ADC and physical-board functional qualification remain open; Full installation does not qualify runtime features.')
    review.update(write_admitted=not blockers, write_blockers=blockers,qualification_warnings=warnings,
                  flash_contract=contract,
                  target_layout=review['layout'], source_layouts=['auto', 'stock', 'installed', 'layout-v1'],
                  readback='complete readback skipped; NOT VERIFIED; new Recovery/Settings checks remain' if skip_readback else 'one complete write then one linear readback; same halted session',
                  readback_skipped=bool(skip_readback),
                  user_data='Preserve valid original same-device Pro; clear other mutable user/diagnostic state after y/N or Yes',
                  pro_retention='fresh exact-unit native capture; signature/UID verified before erase; no BLE activation',
                  external_retained='Separate NFC EEPROM is not accessed/erased',
                  clear_all_user_data=False, clear_mutable_user_state=True,
                  preserve_valid_same_device_pro=True,
                  recovery_state='initialized from selected firmware; Full allows downgrade',
                  font_protocol_status=review['font_protocol_status'],
                  embedded_font_bench=embedded_font_bench,
                  release_qualified=False, hardware_started=False)
    if require_write and blockers:
        raise ValueError('Full refused before device access: ' + ' '.join(blockers))
    return image, review


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--require-write', action='store_true')
    parser.add_argument('--skip-readback-verify', action='store_true',
                        help='Explicitly skip Full readback; result is written but NOT VERIFIED.')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check-template', type=Path)
    args = parser.parse_args()
    try:
        image, review = plan(args.base, args.config, require_write=args.require_write,
                             skip_readback=args.skip_readback_verify)
        if args.check_template and args.check_template.read_bytes() != image:
            raise ValueError('Config/base/template bytes mismatch; refusing serial access.')
        if args.output:
            report = args.output.with_suffix('.json')
            if args.output.exists() or report.exists():
                raise ValueError('Configured template output exists; no overwrite.')
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open('xb') as stream:
                stream.write(image)
            with report.open('x', encoding='utf-8') as stream:
                stream.write(json.dumps(review, indent=2) + '\n')
            review['configured_template'] = str(args.output.resolve())
        print(json.dumps(review, indent=2))
    except (ValueError, OSError, KeyError) as exc:
        parser.error(str(exc))


if __name__ == '__main__':
    main()
