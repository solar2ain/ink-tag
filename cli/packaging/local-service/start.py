#!/usr/bin/env python3
"""Source-transparent launcher for the InkTag local WebUI service."""

from __future__ import annotations

import sys
import argparse
import subprocess
from pathlib import Path


HERE = Path(__file__).resolve().parent
# Source checkout and the flat exported local-service ZIP share one launcher.
ROOT = next((parent for parent in (HERE, *HERE.parents)
             if (parent / "cli/src/inktag_cli/local_server.py").is_file()), HERE)
CLI_SOURCE = ROOT / "cli" / "src"
if str(CLI_SOURCE) not in sys.path:
    sys.path.insert(0, str(CLI_SOURCE))

from inktag_cli.local_server import main  # noqa: E402


if __name__ == "__main__":
    # Reuse the installed runtime rather than requiring global Python packages.
    from inktag_cli.local_server import default_data_directory
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--data-dir', type=Path)
    known, _ = parser.parse_known_args()
    data = (known.data_dir or default_data_directory()).resolve()
    runtime_python = data / 'find/runtime/.venv' / ('Scripts/python.exe' if sys.platform == 'win32' else 'bin/python')
    if runtime_python.is_file() and Path(sys.executable).resolve() != runtime_python.resolve():
        raise SystemExit(subprocess.run([str(runtime_python), __file__, *sys.argv[1:]], check=False).returncode)
    raise SystemExit(main(repo_root=ROOT))
