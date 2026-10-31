#!/usr/bin/env python3
"""Prepare the local InkTag Find runtime, then start its source service."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import venv
from pathlib import Path


TOOL_ROOT = Path(__file__).resolve().parent
REPO_ROOT = TOOL_ROOT.parents[1]
REQUIREMENTS = TOOL_ROOT / "requirements.txt"
QUERY_SERVICE = TOOL_ROOT / "query_webui.py"


def runtime_python(venv_root: Path) -> Path:
    return venv_root / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def prepare_runtime(runtime_root: Path, *, no_install: bool) -> Path:
    venv_root = runtime_root / ".venv"
    python = runtime_python(venv_root)
    marker = runtime_root / "requirements.sha256"
    required_hash = hashlib.sha256(REQUIREMENTS.read_bytes()).hexdigest()
    installed_hash = marker.read_text(encoding="ascii").strip().lower() if marker.exists() else ""

    if not python.exists():
        if no_install:
            raise RuntimeError(
                "Find Python environment is missing; start once without --no-install."
            )
        runtime_root.mkdir(parents=True, exist_ok=True)
        venv.EnvBuilder(with_pip=True).create(venv_root)

    if installed_hash != required_hash:
        if no_install:
            raise RuntimeError(
                "Find dependencies changed; start once without --no-install."
            )
        subprocess.run(
            [
                str(python),
                "-m",
                "pip",
                "install",
                "--disable-pip-version-check",
                "-r",
                str(REQUIREMENTS),
            ],
            check=True,
        )
        marker.write_text(required_hash, encoding="ascii")
    return python


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=REPO_ROOT / ".local/findmy-webui")
    parser.add_argument(
        "--runtime-dir", type=Path, default=REPO_ROOT / ".local/findmy-webui-runtime"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--no-install", action="store_true")
    parser.add_argument(
        "--prepare-only",
        action="store_true",
        help="prepare the virtual environment without starting the service",
    )
    args = parser.parse_args()

    data_dir = args.data_dir.resolve()
    runtime_root = args.runtime_dir.resolve()
    data_dir.mkdir(parents=True, exist_ok=True)
    python = prepare_runtime(runtime_root, no_install=args.no_install)
    if sys.platform == "win32":
        prepare_windows_auth(runtime_root / "mac-adi", no_install=args.no_install)
    if args.prepare_only:
        return 0

    command = [
        str(python),
        str(QUERY_SERVICE),
        "--anisette-libs",
        str(data_dir / "ani_libs.bin"),
        "--host",
        args.host,
        "--port",
        str(args.port),
    ]
    if os.name == "nt":
        # Windows implements exec through a child process. Waiting here keeps
        # the PowerShell launcher attached for Ctrl-C and orderly shutdown.
        return subprocess.run(command, check=False).returncode
    os.execv(str(python), command)
    return 0  # pragma: no cover - os.execv does not return on success


def prepare_windows_auth(cache: Path, *, no_install: bool) -> None:
    from prepare_mac_adi import COREADI_SHA256, ENGINE_HASHES, digest, prepare
    inputs = {cache / "upstream" / name: expected for name, expected in ENGINE_HASHES.items()}
    inputs[cache / "apple/CoreADI.x86_64"] = COREADI_SHA256
    if all(path.is_file() and digest(path) == expected for path, expected in inputs.items()):
        return
    if no_install:
        raise RuntimeError("Windows Apple authentication inputs are missing or invalid; prepare the runtime first.")
    # Download/extract only. Provisioning starts with an explicit user login,
    # never merely because a service is started or a dependency is repaired.
    seven_zip = shutil.which("7z.exe")
    if not seven_zip:
        candidate = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "7-Zip/7z.exe"
        if candidate.is_file():
            seven_zip = str(candidate)
    if not seven_zip:
        raise RuntimeError("Install full 7-Zip to prepare Windows Apple authentication; account data was not accessed.")
    prepare(cache, seven_zip)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"InkTag Find startup failed: {error}") from error
