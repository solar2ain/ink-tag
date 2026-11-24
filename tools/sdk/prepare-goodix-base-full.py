#!/usr/bin/env python3
"""Inspect/compose a base Full offline image; never access a probe or device."""
import argparse
import json
from pathlib import Path

from goodix_base_full import configured_image, inspect_base, json_bytes, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--offline-identity-fixture', type=Path,
                        help='Explicit typed-address JSON for offline byte comparison only.')
    args = parser.parse_args()
    raw = args.package.read_bytes(); reviewed = inspect_base(raw)
    if args.config is None:
        require(args.output is None and args.offline_identity_fixture is None,
                'An offline image requires an explicit board configuration')
        print(json.dumps(dict(schema=reviewed['metadata']['schema'],
            family=reviewed['metadata']['family'], release=reviewed['metadata']['release'],
            recovery=reviewed['metadata']['recovery'], hardwareAccessed=False), indent=2))
        return
    require(args.output is not None and not args.output.exists(), 'A new output directory is required')
    identity = {}
    if args.offline_identity_fixture is not None:
        value = json.loads(args.offline_identity_fixture.read_text(encoding='utf-8'))
        require(type(value) is dict and set(value) == {'address', 'type'},
                'Expected explicit address/type comparison fixture')
        require(type(value['address']) is str and len(value['address']) == 12,
                'Comparison address must be twelve hexadecimal characters')
        identity = dict(address=bytes.fromhex(value['address']), address_kind=value['type'])
    report, image = configured_image(raw, args.config, **identity)
    report.update(offlineComparisonOnly=True, installable=False,
                  note='Physical first-install preparation must use fresh identity/vendor capture.')
    args.output.mkdir(parents=True)
    (args.output/'image.bin').write_bytes(image)
    (args.output/'review.json').write_bytes(json_bytes(report))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit('ERROR: '+str(error)) from None
