#!/usr/bin/env python3
"""Read one field from InkTag's canonical version.json."""

import argparse
import json
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument("--field", required=True, choices=("version", "protocolVersion", "otaVersion", "recoveryVersion", "recoveryEpoch"))
args = parser.parse_args()
data = json.loads((args.repo.resolve() / "version.json").read_text(encoding="utf-8"))
print(int(data['recoveryVersion'].split('.')[0]) if args.field == 'recoveryEpoch' else data[args.field])
