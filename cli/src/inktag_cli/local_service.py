from __future__ import annotations

import json
import ctypes
import os
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


DEFAULT_LOCAL_ORIGIN = "http://127.0.0.1:8787"
SERVICE_STATE_FILENAME = "webui-service.json"
SERVICE_LOG_FILENAME = "webui.log"
FIND_LOG_FILENAME = "find.log"


def normalize_local_origin(value: str) -> str:
    parsed = urlparse(value.strip())
    if parsed.scheme not in {"http", "https"} or parsed.hostname not in {"127.0.0.1", "::1", "localhost"}:
        raise ValueError("local WebUI URL must use http(s) on 127.0.0.1, ::1 or localhost")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError("local WebUI URL must not contain credentials, query parameters or a fragment")
    return f"{parsed.scheme}://{parsed.netloc}"


def local_service_request(origin: str, path: str, body: dict[str, Any] | None = None) -> dict[str, Any]:
    normalized_origin = normalize_local_origin(origin)
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode("utf-8")
    request = Request(
        f"{normalized_origin}{path}",
        data=data,
        method="GET" if body is None else "POST",
        headers={
            "Accept": "application/json",
            "Origin": normalized_origin,
            **({"Content-Type": "application/json"} if body is not None else {}),
        },
    )
    try:
        with urlopen(request, timeout=35) as response:  # noqa: S310 - loopback is enforced above
            payload = json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        try:
            payload = json.loads(error.read().decode("utf-8"))
            message = str(payload.get("error") or error.reason)
        except (UnicodeDecodeError, json.JSONDecodeError):
            message = str(error.reason)
        raise ValueError(f"local WebUI request failed: {message}") from error
    except (URLError, TimeoutError, OSError) as error:
        raise ValueError(f"cannot reach InkTag local WebUI at {normalized_origin}: {error}") from error
    if not isinstance(payload, dict) or payload.get("ok") is False:
        raise ValueError(str(payload.get("error") if isinstance(payload, dict) else "invalid local WebUI response"))
    return payload


def find_repo_root(start: Path | None = None) -> Path:
    candidates = [start.resolve()] if start is not None else []
    candidates.extend(Path(__file__).resolve().parents)
    for candidate in candidates:
        for root in (candidate, *candidate.parents):
            has_service = (root / "cli" / "src" / "inktag_cli" / "local_server.py").is_file()
            has_web = any(
                (web_root / "index.html").is_file()
                for web_root in (root / "webui" / "dist" / "static", root / "public")
            )
            if has_service and has_web:
                return root
    raise ValueError("InkTag source tree was not found; pass --repo")


def local_data_directory(data_dir: Path | None = None) -> Path:
    if data_dir is not None:
        return data_dir.resolve()
    from .local_server import default_data_directory

    return default_data_directory().resolve()


def webui_service_paths(data_dir: Path | None = None) -> dict[str, Path]:
    root = local_data_directory(data_dir)
    return {
        "data": root,
        "state": root / SERVICE_STATE_FILENAME,
        "logDirectory": root / "log",
        "log": root / "log" / SERVICE_LOG_FILENAME,
        "findLog": root / "log" / FIND_LOG_FILENAME,
    }


def _read_service_state(data_dir: Path | None = None) -> dict[str, Any] | None:
    state_path = webui_service_paths(data_dir)["state"]
    if not state_path.is_file():
        return None
    try:
        state = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return state if isinstance(state, dict) else None


def _write_service_state(data_dir: Path | None, state: dict[str, Any]) -> None:
    state_path = webui_service_paths(data_dir)["state"]
    state_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = state_path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")
    temporary.replace(state_path)


def _pid_is_running(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        process_query_limited_information = 0x1000
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)  # type: ignore[attr-defined]
        handle = kernel32.OpenProcess(
            process_query_limited_information, False, pid
        )
        if handle:
            kernel32.CloseHandle(handle)
            return True
        return ctypes.get_last_error() == 5  # Access denied still proves that the PID exists.
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _origin_is_ready(origin: str, *, timeout: float = 0.5) -> bool:
    try:
        with urlopen(f"{normalize_local_origin(origin)}/api/v1/capabilities", timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (HTTPError, URLError, TimeoutError, OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return isinstance(payload, dict) and payload.get("runtime") == "python"


def webui_background_status(data_dir: Path | None = None) -> dict[str, Any]:
    paths = webui_service_paths(data_dir)
    state = _read_service_state(data_dir)
    if state is None:
        return {
            "running": False,
            "state": str(paths["state"]),
            "log": str(paths["log"]),
        }
    try:
        pid = int(state.get("pid", 0))
    except (TypeError, ValueError):
        pid = 0
    origin = str(state.get("origin") or "")
    process_running = _pid_is_running(pid)
    ready = process_running and bool(origin) and _origin_is_ready(origin)
    return {
        **state,
        "running": process_running,
        "ready": ready,
        "state": str(paths["state"]),
        "log": str(paths["log"]),
    }


def _log_tail(path: Path, lines: int = 30) -> str:
    if not path.is_file():
        return ""
    content = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(content[-max(1, lines):])


def _background_python_executable() -> str:
    executable = Path(sys.executable)
    if os.name == "nt":
        pythonw = executable.with_name("pythonw.exe")
        if pythonw.is_file():
            return str(pythonw)
    return str(executable)


def start_webui_background(
    *,
    repo: Path | None = None,
    port: int = 8787,
    data_dir: Path | None = None,
    find_service: str | None = None,
    startup_timeout: float = 300.0,
) -> dict[str, Any]:
    if not 1 <= port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    paths = webui_service_paths(data_dir)
    paths["data"].mkdir(parents=True, exist_ok=True)
    paths["logDirectory"].mkdir(parents=True, exist_ok=True)
    existing = webui_background_status(data_dir)
    if existing.get("running"):
        if int(existing.get("port", 0)) != port:
            raise ValueError(
                f"InkTag WebUI is already running on port {existing.get('port')}; "
                "stop or restart it first"
            )
        return {**existing, "alreadyRunning": True}
    paths["state"].unlink(missing_ok=True)

    root = find_repo_root(repo)
    origin = f"http://127.0.0.1:{port}"
    command = [
        _background_python_executable(),
        "-u",
        "-m",
        "inktag_cli",
        "webui",
        "serve",
        "--port",
        str(port),
        "--repo",
        str(root),
        "--data-dir",
        str(paths["data"]),
        "--no-open",
    ]
    if find_service:
        command.extend(("--find-service", normalize_local_origin(find_service)))
    creationflags = 0
    popen_options: dict[str, Any] = {"start_new_session": os.name != "nt"}
    if os.name == "nt":
        startup_info = subprocess.STARTUPINFO()
        startup_info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup_info.wShowWindow = subprocess.SW_HIDE
        popen_options["startupinfo"] = startup_info
        creationflags = (
            subprocess.CREATE_NEW_PROCESS_GROUP
            | subprocess.DETACHED_PROCESS
            | subprocess.CREATE_NO_WINDOW
            | subprocess.CREATE_BREAKAWAY_FROM_JOB
        )
        popen_options["creationflags"] = creationflags
    child_environment = os.environ.copy()
    package_source = str(Path(__file__).resolve().parents[1])
    existing_python_path = child_environment.get("PYTHONPATH", "")
    child_environment["PYTHONPATH"] = os.pathsep.join(
        item for item in (package_source, existing_python_path) if item
    )
    with paths["log"].open("ab") as log_file:
        log_file.write(
            f"\n[{datetime.now(timezone.utc).isoformat()}] starting InkTag WebUI on {origin}\n".encode()
        )
        log_file.flush()
        try:
            process = subprocess.Popen(
                command,
                cwd=root,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                close_fds=True,
                env=child_environment,
                **popen_options,
            )
        except OSError as error:
            if os.name != "nt" or getattr(error, "winerror", None) != 5:
                raise
            # An ordinary terminal may run outside a Job Object or inside one
            # that forbids explicit breakaway. In that case detached process
            # creation is already sufficient.
            popen_options["creationflags"] = creationflags & ~subprocess.CREATE_BREAKAWAY_FROM_JOB
            process = subprocess.Popen(
                command,
                cwd=root,
                stdin=subprocess.DEVNULL,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                close_fds=True,
                env=child_environment,
                **popen_options,
            )
    state = {
        "pid": process.pid,
        "port": port,
        "origin": origin,
        "startedAt": datetime.now(timezone.utc).isoformat(),
        "repo": str(root),
        "dataDir": str(paths["data"]),
        **({"findService": normalize_local_origin(find_service)} if find_service else {}),
    }
    _write_service_state(data_dir, state)
    deadline = time.monotonic() + startup_timeout
    while time.monotonic() < deadline:
        return_code = process.poll()
        if return_code is not None:
            paths["state"].unlink(missing_ok=True)
            detail = _log_tail(paths["log"])
            raise RuntimeError(
                f"InkTag WebUI stopped during startup (exit {return_code}).\n{detail}"
            )
        if _origin_is_ready(origin):
            return {**state, "running": True, "ready": True, "log": str(paths["log"])}
        time.sleep(0.2)
    raise RuntimeError(
        f"InkTag WebUI did not become ready within {startup_timeout:g} seconds; "
        f"see {paths['log']}"
    )


def stop_webui_background(data_dir: Path | None = None, *, timeout: float = 10.0) -> dict[str, Any]:
    paths = webui_service_paths(data_dir)
    state = _read_service_state(data_dir)
    if state is None:
        return {"running": False, "stopped": False, "log": str(paths["log"])}
    try:
        pid = int(state.get("pid", 0))
    except (TypeError, ValueError):
        pid = 0
    if not _pid_is_running(pid):
        paths["state"].unlink(missing_ok=True)
        return {**state, "running": False, "stopped": False, "log": str(paths["log"])}
    terminated = False
    if os.name == "nt":
        result = subprocess.run(
            ["taskkill.exe", "/PID", str(pid), "/T", "/F"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0 and _pid_is_running(pid):
            detail = (result.stderr or result.stdout).strip()
            raise OSError(f"cannot stop InkTag WebUI process {pid}: {detail}")
        terminated = result.returncode == 0
    else:
        try:
            os.killpg(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        deadline = time.monotonic() + timeout
        while _pid_is_running(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        if _pid_is_running(pid):
            os.killpg(pid, signal.SIGKILL)
    if not terminated:
        deadline = time.monotonic() + timeout
        while _pid_is_running(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
    paths["state"].unlink(missing_ok=True)
    return {**state, "running": False, "stopped": True, "log": str(paths["log"])}


def restart_webui_background(
    *, repo: Path | None = None, port: int | None = None, data_dir: Path | None = None,
    find_service: str | None = None,
) -> dict[str, Any]:
    state = _read_service_state(data_dir) or {}
    selected_port = port if port is not None else int(state.get("port", 8787))
    selected_repo = repo or (Path(str(state["repo"])) if state.get("repo") else None)
    selected_find_service = find_service or (
        str(state["findService"]) if state.get("findService") else None
    )
    stop_webui_background(data_dir)
    return start_webui_background(
        repo=selected_repo, port=selected_port, data_dir=data_dir,
        find_service=selected_find_service,
    )


def read_webui_log(
    data_dir: Path | None = None, *, lines: int = 100, service: str = "webui"
) -> tuple[Path, str]:
    if lines < 1:
        raise ValueError("--lines must be at least 1")
    normalized_service = service.strip().lower()
    if normalized_service not in {"webui", "find"}:
        raise ValueError("--service must be webui or find")
    paths = webui_service_paths(data_dir)
    path = paths["findLog"] if normalized_service == "find" else paths["log"]
    return path, _log_tail(path, lines)


def serve_webui(
    *,
    repo: Path | None,
    listen: str,
    port: int | None,
    data_dir: Path | None,
    web_root: Path | None,
    find_data_dir: Path | None,
    tls_cert: Path | None,
    tls_key: Path | None,
    find_service: str | None,
    find_no_install: bool,
    no_open: bool,
) -> int:
    from .local_server import run_local_server

    root = find_repo_root(repo)
    return run_local_server(
        repo_root=root,
        listen=listen,
        port=port,
        data_dir=data_dir,
        web_root=web_root,
        cert_path=tls_cert,
        key_path=tls_key,
        find_service=find_service,
        find_data_dir=find_data_dir,
        find_no_install=find_no_install,
        no_open=no_open,
    )
