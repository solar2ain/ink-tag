#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v python3 >/dev/null 2>&1; then
  exec python3 "$SCRIPT_DIR/install.py" "$@"
fi
echo "Install Python 3.11 or newer, then run this installer again." >&2
exit 1
