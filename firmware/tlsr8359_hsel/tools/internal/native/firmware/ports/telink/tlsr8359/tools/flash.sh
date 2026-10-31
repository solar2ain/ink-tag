#!/bin/sh
# Only offline Config/base validation is currently qualified on Linux.
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../../../.." && pwd)
exec "${INKTAG_PYTHON:-python3}" "$repo_root/tools/hardware/tlsr-terminal-entry.py" flash "$@"
