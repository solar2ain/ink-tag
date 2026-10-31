#!/usr/bin/env python3
"""Install InkTag CLI and local Find query dependencies, then open the WebUI."""
from __future__ import annotations

import argparse
import shlex
import subprocess
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = next((parent for parent in (HERE, *HERE.parents)
             if (parent / "cli/src/inktag_cli/local_server.py").is_file()), HERE)
sys.path.insert(0, str(ROOT / "cli/src"))
from inktag_cli.local_server import default_data_directory


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--prepare-only", action="store_true", help="Install/check dependencies without starting the server")
    parser.add_argument("--no-install", action="store_true", help="Check existing dependencies only; never download")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--lan", action="store_true", help="Enable phone access over HTTPS")
    args = parser.parse_args(argv)
    if sys.version_info < (3, 11):
        parser.error("Python 3.11 or newer is required")
    if args.data_dir is not None and not args.data_dir.is_absolute():
        parser.error("--data-dir must be an absolute path")
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    for relative in ("cli/pyproject.toml", "tools/findmy/bootstrap.py", "tools/findmy/query_webui.py"):
        if not (ROOT / relative).is_file():
            parser.error("Incomplete InkTag checkout or ZIP: missing " + relative)
    data = (args.data_dir or default_data_directory()).resolve()
    cli_root = ROOT / "cli"
    cli_python = cli_root / ".venv" / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    if args.no_install:
        if not cli_python.is_file():
            parser.error("CLI environment is missing; run once without --no-install")
        subprocess.run([str(cli_python), "-c", "from importlib.metadata import distribution; distribution('inktag-cli')"], check=True)
    else:
        print("Installing CLI dependencies into cli/.venv (system Python is unchanged).", flush=True)
        if not cli_python.is_file():
            subprocess.run([sys.executable, "-m", "venv", str(cli_root / ".venv")], check=True)
        subprocess.run([str(cli_python), "-m", "pip", "install", str(cli_root)], check=True)
    find_data = data / "find"
    command = [str(cli_python), str(ROOT / "tools/findmy/bootstrap.py"),
               "--data-dir", str(find_data), "--runtime-dir", str(find_data / "runtime"),
               "--port", str(args.port), "--prepare-only"]
    if args.no_install:
        command.append("--no-install")
    print("Preparing local dependencies. No Apple account login, device connection or firmware write.", flush=True)
    print("Windows: first setup downloads a verified Apple image (~292 MB); full 7-Zip is required. macOS uses system AOSKit.", flush=True)
    subprocess.run(command, check=True)
    print(f"Ready. Persistent data: {data}", flush=True)
    if args.prepare_only:
        return 0
    # Updating an already-running foreground instance must not silently reuse
    # it; stop only this profile through the maintained lifecycle command.
    subprocess.run([str(cli_python), "-m", "inktag_cli", "webui", "stop", "--data-dir", str(data), "--require-process-identity"], check=True)
    result = subprocess.run([str(cli_python), "-m", "inktag_cli", "webui", "start", "--repo", str(ROOT), "--data-dir", str(data),
                             "--port", str(args.port), *(["--lan"] if args.lan else [])], check=False).returncode
    if result == 0:
        print("WebUI and Find run in the background; closing this terminal does not stop them.", flush=True)
        for action in ("stop", "start", "status", "log", "log --service find"):
            command = [str(cli_python), "-m", "inktag_cli", "webui", *action.split(), "--data-dir", str(data)]
            if action == "start":
                command.extend(["--repo", str(ROOT), "--port", str(args.port), *(["--lan"] if args.lan else [])])
            rendered = ("powershell -NoProfile -ExecutionPolicy Bypass -File " +
                        " ".join("'" + part.replace("'", "''") + "'" for part in [str(ROOT / "cli/inktag.ps1"), *command[3:]])
                        if sys.platform == "win32" else shlex.join(command))
            print(f"{action}: {rendered}", flush=True)
    return result


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"InkTag setup failed ({type(error).__name__}). Check the step above; existing data was not cleared.")
