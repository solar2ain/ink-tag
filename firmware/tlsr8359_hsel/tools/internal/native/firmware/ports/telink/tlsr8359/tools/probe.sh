#!/bin/sh
# Linux currently supports offline command plans, not qualified SWS hardware.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../../../.." && pwd)
exec "${INKTAG_PYTHON:-python3}" "$repo_root/tools/hardware/tlsr-terminal-entry.py" probe "$@"
