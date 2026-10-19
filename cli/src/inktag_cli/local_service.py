from __future__ import annotations

import json
import ctypes
import os
import signal
import ssl
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
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
    if start is not None and not start.is_dir():
        raise ValueError("The explicit --repo directory does not exist")
    candidates = [start.resolve()] if start is not None else list(Path(__file__).resolve().parents)
    for candidate in candidates:
        for root in (candidate, *candidate.parents):
            has_service = (root / "cli" / "src" / "inktag_cli" / "local_server.py").is_file()
            has_web = any(
                (web_root / "index.html").is_file()
                for web_root in (root / "webui" / "dist" / "static", root / "webui", root / "public")
            )
            if has_service and has_web:
                return root
    raise ValueError("InkTag repository with a built WebUI was not found; pass --repo")


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


def _origin_is_ready(origin: str, *, timeout: float = 0.5, tls_cert: Path | None = None) -> bool:
    try:
        options = {"context": ssl.create_default_context(cafile=str(tls_cert))} if tls_cert else {}
        with urlopen(f"{normalize_local_origin(origin)}/api/v1/capabilities", timeout=timeout, **options) as response:
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
    tls_cert = Path(str(state["tlsCert"])) if state.get("tlsCert") else None
    ready = process_running and bool(origin) and _origin_is_ready(origin, tls_cert=tls_cert)
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


def _validate_tls_pair(tls_cert: Path | None, tls_key: Path | None) -> tuple[Path | None, Path | None]:
    if (tls_cert is None) != (tls_key is None):
        raise ValueError("--tls-cert and --tls-key must be provided together")
    if tls_cert is None:
        return None, None
    certificate, key = tls_cert.resolve(), tls_key.resolve()
    # Validate before stopping a working service or starting a detached child.
    ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).load_cert_chain(certificate, key)
    return certificate, key


def ensure_local_tls_certificate(data_dir: Path | None = None, *, hosts: tuple[str, ...] = ()) -> tuple[Path, Path]:
    """Prepare a server certificate; never install or trust a certificate automatically."""
    import hashlib
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

    directory = local_data_directory(data_dir) / "tls"
    suffix = 'lan-' + hashlib.sha256('\n'.join(sorted(hosts)).encode()).hexdigest()[:12] if hosts else 'localhost'
    certificate_path, key_path = directory / f"{suffix}-cert.pem", directory / f"{suffix}-key.pem"
    if certificate_path.exists() or key_path.exists():
        _validate_tls_pair(certificate_path, key_path)
        return certificate_path, key_path
    directory.mkdir(parents=True, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "InkTag local service")])
    names = [x509.DNSName("localhost"), x509.IPAddress(ip_address("127.0.0.1")), x509.IPAddress(ip_address("::1"))]
    for host in hosts:
        try:
            names.append(x509.IPAddress(ip_address(host)))
        except ValueError:
            names.append(x509.DNSName(host))
    now = datetime.now(timezone.utc)
    issuer, signer = subject, key
    if hosts:
        # A LAN phone needs a trust anchor, not merely a self-signed leaf.
        # The CA key never leaves the local data directory.
        ca_cert_path, ca_key_path = directory / 'lan-ca-cert.pem', directory / 'lan-ca-key.pem'
        if ca_cert_path.exists() or ca_key_path.exists():
            _validate_tls_pair(ca_cert_path, ca_key_path)
            ca = x509.load_pem_x509_certificate(ca_cert_path.read_bytes())
            signer = serialization.load_pem_private_key(ca_key_path.read_bytes(), password=None)
            if ca.not_valid_after_utc <= now:
                raise ValueError('The LAN trust certificate has expired; renew it before using phone access')
        else:
            signer = ec.generate_private_key(ec.SECP256R1())
            ca_subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'InkTag local LAN CA')])
            ca = (x509.CertificateBuilder().subject_name(ca_subject).issuer_name(ca_subject)
                  .public_key(signer.public_key()).serial_number(x509.random_serial_number())
                  .not_valid_before(now - timedelta(minutes=5)).not_valid_after(now + timedelta(days=1825))
                  .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
                  .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                      key_encipherment=False, data_encipherment=False, key_agreement=False,
                      key_cert_sign=True, crl_sign=True, encipher_only=None, decipher_only=None), critical=True)
                  .sign(signer, hashes.SHA256()))
            with os.fdopen(os.open(ca_key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as output:
                output.write(signer.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
            ca_cert_path.write_bytes(ca.public_bytes(serialization.Encoding.PEM))
        issuer = ca.subject
    certificate = (
        x509.CertificateBuilder()
        .subject_name(subject).issuer_name(issuer).public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(minutes=5)).not_valid_after(now + timedelta(days=365))
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectAlternativeName(names), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .sign(signer, hashes.SHA256())
    )
    key_bytes = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    with os.fdopen(os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as output:
        output.write(key_bytes)
    with certificate_path.open("xb") as output:
        output.write(certificate.public_bytes(serialization.Encoding.PEM))
    return certificate_path, key_path


def prepare_lan_https(listen: str, data_dir: Path | None, cert: Path | None, key: Path | None,
                      allowed_hosts: tuple[str, ...]) -> tuple[str, Path, Path, tuple[str, ...]]:
    from .local_server import _host_name, local_lan_hosts, parse_listen

    host, port = parse_listen(listen)
    if host in {'127.0.0.1', 'localhost', '::1'}:
        host = '0.0.0.0'
    hosts = tuple(dict.fromkeys((local_lan_hosts() if host in {'0.0.0.0', '::'} else (host,)) + allowed_hosts))
    if not hosts:
        raise ValueError('No LAN address found; connect to a network or pass --allowed-host')
    hosts = tuple(_host_name(value) for value in hosts)
    if cert is None and key is None:
        cert, key = ensure_local_tls_certificate(data_dir, hosts=hosts)
        print(f'Phone trust certificate (public certificate only): {cert.parent / "lan-ca-cert.pem"}')
        print('Install/trust this certificate on the phone once. Never share the matching private key.')
    cert, key = _validate_tls_pair(cert, key)
    assert cert is not None and key is not None
    return f'{"[" + host + "]" if ":" in host else host}:{port}', cert, key, hosts


def start_webui_background(
    *,
    repo: Path | None = None,
    port: int = 8787,
    data_dir: Path | None = None,
    find_service: str | None = None,
    tls_cert: Path | None = None,
    tls_key: Path | None = None,
    enable_tls: bool = False,
    startup_timeout: float = 300.0,
) -> dict[str, Any]:
    if not 1 <= port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    if enable_tls and tls_cert is None and tls_key is None:
        tls_cert, tls_key = ensure_local_tls_certificate(data_dir)
    tls_cert, tls_key = _validate_tls_pair(tls_cert, tls_key)
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
        if existing.get("tlsCert") != (str(tls_cert) if tls_cert else None) or existing.get("tlsKey") != (str(tls_key) if tls_key else None):
            raise ValueError("InkTag WebUI is already running with different TLS settings; restart it first")
        return {**existing, "alreadyRunning": True}
    paths["state"].unlink(missing_ok=True)

    root = find_repo_root(repo)
    origin = f"{'https' if tls_cert else 'http'}://127.0.0.1:{port}"
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
    if tls_cert:
        command.extend(("--tls-cert", str(tls_cert), "--tls-key", str(tls_key)))
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
        **({"tlsCert": str(tls_cert), "tlsKey": str(tls_key)} if tls_cert else {}),
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
        if _origin_is_ready(origin, tls_cert=tls_cert):
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
    tls_cert: Path | None = None,
    tls_key: Path | None = None,
    enable_tls: bool | None = None,
) -> dict[str, Any]:
    state = _read_service_state(data_dir) or {}
    selected_port = port if port is not None else int(state.get("port", 8787))
    selected_repo = repo or (Path(str(state["repo"])) if state.get("repo") else None)
    selected_find_service = find_service or (
        str(state["findService"]) if state.get("findService") else None
    )
    if enable_tls is False:
        if tls_cert is not None or tls_key is not None:
            raise ValueError("--no-enable-tls cannot be combined with TLS certificate options")
    elif tls_cert is None and tls_key is None:
        tls_cert = Path(str(state["tlsCert"])) if state.get("tlsCert") else None
        tls_key = Path(str(state["tlsKey"])) if state.get("tlsKey") else None
        if enable_tls and tls_cert is None and tls_key is None:
            tls_cert, tls_key = ensure_local_tls_certificate(data_dir)
    tls_cert, tls_key = _validate_tls_pair(tls_cert, tls_key)
    if not 1 <= selected_port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    selected_repo = find_repo_root(selected_repo)
    stop_webui_background(data_dir)
    return start_webui_background(
        repo=selected_repo, port=selected_port, data_dir=data_dir,
        find_service=selected_find_service,
        tls_cert=tls_cert, tls_key=tls_key,
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
    enable_tls: bool = False,
    lan: bool = False,
    allowed_hosts: tuple[str, ...] = (),
    access_password: str | None = None,
) -> int:
    from .local_server import parse_listen, run_local_server

    root = find_repo_root(repo)
    if lan or (enable_tls and parse_listen(listen)[0] not in {"localhost", "127.0.0.1", "::1"}):
        listen, tls_cert, tls_key, allowed_hosts = prepare_lan_https(listen, data_dir, tls_cert, tls_key, allowed_hosts)
    elif enable_tls and tls_cert is None and tls_key is None:
        tls_cert, tls_key = ensure_local_tls_certificate(data_dir)
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
        allowed_hosts=allowed_hosts,
        access_password=access_password,
    )
