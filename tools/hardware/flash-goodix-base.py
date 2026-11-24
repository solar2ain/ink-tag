#!/usr/bin/env python3
"""Inspect or first-install an explicit unsigned GR base Full package.

Default operation is offline. --write --yes explicitly selects a destructive
first install and requires fresh physical identity/vendor capture. Existing
InkTag installations must use their maintained update path.
"""
import argparse
import json
from pathlib import Path

from goodix_base_programmer import configured_image, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--write', action='store_true')
    parser.add_argument('--yes', action='store_true')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--console', type=Path)
    parser.add_argument('--jlink', type=Path)
    parser.add_argument('--snr', default='')
    parser.add_argument('--address', default='', help='Compare exact expected immutable address.')
    parser.add_argument('--powershell', default='powershell.exe')
    parser.add_argument('--detect-timeout', type=int, default=60, choices=range(1, 121))
    parser.add_argument('--operation-timeout', type=int, default=300, choices=range(10, 601))
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    raw = args.package.read_bytes()
    report, _ = configured_image(raw, args.config)
    report.update(installable=False, hardwareQualified=False)
    if not args.write:
        require(not args.yes, '--yes requires --write')
        print(json.dumps(report, indent=2))
        return
    require(args.yes and args.console is not None and args.jlink is not None and
            args.evidence is not None and not args.evidence.exists(),
            'Write requires --yes, explicit console/JLink and a new evidence directory')
    args.chip = report['chip'].lower()
    args.retry_detect = False
    args.frequency_hz = 500000
    # Package/config admission is complete before probe enumeration. Neither
    # another firmware file nor the local SDK is an implicit fallback.
    args.evidence.mkdir(parents=True)
    from goodix_base_programmer import GProgrammerTransport, sequence
    transport = GProgrammerTransport(args)
    try:
        result = sequence(raw, args.config, transport)
    except BaseException as error:
        (args.evidence/'result.json').write_text(json.dumps(dict(status='failed',
            error=str(error), flashWriteStarted=transport.write_started,
            flashWriteAcknowledged=transport.write_acknowledged,
            noWriteReplay=True), indent=2), encoding='utf-8')
        raise
    (args.evidence/'result.json').write_text(json.dumps(dict(status='tool-success', **result),
        indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        raise SystemExit('ERROR: '+str(error)) from None
