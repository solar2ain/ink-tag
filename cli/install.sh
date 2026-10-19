#!/usr/bin/env bash
set -euo pipefail
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
task_python="${PYTHON:-python3}"
if [[ ! -x "$task_dir/.venv/bin/python" ]]; then
  "$task_python" -m venv "$task_dir/.venv"
fi
"$task_dir/.venv/bin/python" -m pip install "$task_dir"
printf '%s\n' 'Installed. Run bash inktag.sh --help from this directory.'
