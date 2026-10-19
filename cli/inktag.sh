#!/usr/bin/env bash
set -euo pipefail
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$task_dir/.venv/bin/python" ]]; then
  printf '%s\n' 'Run bash install.sh first (Python 3.11+).' >&2
  exit 2
fi
exec "$task_dir/.venv/bin/python" -m inktag_cli "$@"
