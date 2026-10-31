from __future__ import annotations

import json
import ctypes
import hashlib
import hmac
import os
import secrets
import signal
import ssl
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager
from contextvars import ContextVar
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen, build_opener, HTTPRedirectHandler, HTTPSHandler, ProxyHandler


DEFAULT_LOCAL_ORIGIN = "http://127.0.0.1:8787"
SERVICE_STATE_FILENAME = "webui-service.json"
SERVICE_LOG_FILENAME = "webui.log"
FIND_LOG_FILENAME = "find.log"
_request_options = ContextVar('inktag_local_service_options', default=(None, None, None))


def configure_service_client(origin=None, data_dir=None, ca_cert=None):
    _request_options.set((origin, data_dir, ca_cert))


class _NoLocalRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        raise ValueError('Local service redirect rejected; select its exact HTTP/HTTPS URL')


def normalize_local_origin(value: str) -> str:
    parsed = urlparse(value.strip())
    if parsed.scheme not in {"http", "https"} or parsed.hostname not in {"127.0.0.1", "::1", "localhost"}:
        raise ValueError("local WebUI URL must use http(s) on 127.0.0.1, ::1 or localhost")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError("local WebUI URL must not contain credentials, query parameters or a fragment")
    return f"{parsed.scheme}://{parsed.netloc}"


def local_service_request(origin: str | None, path: str, body: dict[str, Any] | None = None,
                          *, data_dir: Path | None = None, ca_cert: Path | None = None) -> dict[str, Any]:
    selected_origin, selected_data, selected_cert = _request_options.get()
    origin, data_dir, ca_cert = origin or selected_origin, data_dir or selected_data, ca_cert or selected_cert
    state = _read_service_state(data_dir) or {}
    normalized_origin = normalize_local_origin(origin or state.get('origin') or DEFAULT_LOCAL_ORIGIN)
    if ca_cert is None and state.get('origin') == normalized_origin and state.get('tlsCert'):
        ca_cert = Path(state['tlsCert'])
    context = ssl.create_default_context(cafile=str(ca_cert)) if ca_cert else ssl.create_default_context()
    opener = build_opener(ProxyHandler({}), _NoLocalRedirect(), HTTPSHandler(context=context))
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
        with opener.open(request, timeout=35) as response:  # noqa: S310 - loopback is enforced above
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


@contextmanager
def _service_state_guard(data_dir: Path | None):
    """Serialize short registry updates, never the service's request loop."""
    path = webui_service_paths(data_dir)['state'].with_suffix('.lock')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as handle:
        if handle.tell() == 0:
            handle.write(b'\0'); handle.flush()
        deadline = time.monotonic() + 3
        while True:
            handle.seek(0)
            try:
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise OSError('WebUI service registry is busy; retry the command') from None
                time.sleep(.02)
        try:
            yield
        finally:
            if os.name == 'nt':
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle, fcntl.LOCK_UN)


def _write_service_state(data_dir: Path | None, state: dict[str, Any]) -> None:
    state_path = webui_service_paths(data_dir)["state"]
    with _service_state_guard(data_dir):
        previous = _read_service_state(data_dir) or {}
        try:
            previous_pid = int(previous.get('pid', 0))
        except (TypeError, ValueError):
            previous_pid = 0
        if previous_pid != state.get('pid') and _pid_is_running(previous_pid):
            raise ValueError('Another InkTag WebUI owns this data directory; stop it before starting HTTP/HTTPS again')
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf8', dir=state_path.parent,
                                             prefix='webui-service-', suffix='.tmp', delete=False) as handle:
                temporary = Path(handle.name)
                json.dump(state, handle, ensure_ascii=False, indent=2)
            temporary.replace(state_path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


def _remove_service_state(data_dir: Path | None, pid: int) -> bool:
    with _service_state_guard(data_dir):
        if (_read_service_state(data_dir) or {}).get('pid') != pid:
            return False
        webui_service_paths(data_dir)['state'].unlink(missing_ok=True)
        return True


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


def _process_identity(pid: int) -> str | None:
    """Kernel creation identity; never infer ownership from a recycled PID."""
    if type(pid) is not int or pid <= 0:
        return None
    try:
        if os.name == 'nt':
            from ctypes import wintypes
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel.GetProcessTimes.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4)]
            handle = kernel.OpenProcess(0x1000, False, pid)
            if not handle:
                return None
            try:
                fields = [wintypes.FILETIME() for _ in range(4)]
                if not kernel.GetProcessTimes(handle, *(ctypes.byref(field) for field in fields)):
                    return None
                return 'win:' + str((fields[0].dwHighDateTime << 32) | fields[0].dwLowDateTime)
            finally:
                kernel.CloseHandle(handle)
        result = subprocess.run(['ps', '-ww', '-p', str(pid), '-o', 'lstart=', '-o', 'command='], capture_output=True,
            text=True, timeout=3, check=False, env={**os.environ, 'LC_ALL': 'C', 'TZ': 'UTC'})
        value = result.stdout.strip()
        return 'posix:' + hashlib.sha256(value.encode('utf8')).hexdigest() if result.returncode == 0 and value and len(value) <= 32768 else None
    except (OSError, ValueError, subprocess.SubprocessError):
        return None


def _origin_is_ready(origin: str, *, timeout: float = 0.5, tls_cert: Path | None = None,
                     allow_auth_challenge: bool = False) -> bool:
    try:
        options = {"context": ssl.create_default_context(cafile=str(tls_cert))} if tls_cert else {}
        with urlopen(f"{normalize_local_origin(origin)}/api/v1/capabilities", timeout=timeout, **options) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        # Only a registered LAN listener may use this probe. TLS verification
        # stays enabled and Basic protection remains in force on every route.
        return (allow_auth_challenge and error.code == 401
                and error.headers.get_all('WWW-Authenticate') == ['Basic realm="InkTag", charset="UTF-8"']
                and error.read(65) == b'Authentication required')
    except (URLError, TimeoutError, OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return isinstance(payload, dict) and payload.get("runtime") == "python"


def _find_health(origin: str, tls_cert: Path | None) -> dict[str, Any]:
    """Read component health only: never log in, query Apple or return identities."""
    try:
        options = {'context': ssl.create_default_context(cafile=str(tls_cert))} if tls_cert else {}
        with urlopen(normalize_local_origin(origin) + '/api/v1/find/health', timeout=1.5, **options) as response:
            payload = json.loads(response.read(4097).decode('utf-8'))
        if not isinstance(payload, dict) or payload.get('ok') is not True: raise ValueError('Invalid health')
        backend = payload.get('authBackend')
        return dict(configured=True, running=True,
            authBackend=backend if isinstance(backend, str) and backend in {'local', 'win-emulated', 'mac-native'} else 'unknown',
            authenticated=payload.get('loggedIn') is True, queryReady=payload.get('queryReady') is True,
            sessionRestored=payload.get('sessionRestored') is True,
            session='restore-failed' if payload.get('sessionError') else ('ready' if payload.get('loggedIn') is True else 'needs-login'))
    except HTTPError as error:
        if error.code == 401:
            return dict(configured=None, running=None, session='authentication-required')
        if error.code == 503:
            try: payload = json.loads(error.read(4097).decode('utf-8'))
            except (ValueError, UnicodeError): payload = {}
            if isinstance(payload, dict) and payload.get('error') == 'find_service_unavailable':
                return dict(configured=False, running=False, session='not-installed')
        if error.code == 404: return dict(configured=None, running=None, session='unknown')
    except (OSError, URLError, TimeoutError, ValueError, UnicodeError):
        pass
    return dict(configured=True, running=False, session='unavailable')


def webui_background_status(data_dir: Path | None = None, *, include_find: bool = False) -> dict[str, Any]:
    paths = webui_service_paths(data_dir)
    state = _read_service_state(data_dir)
    if state is None:
        # Older foreground servers may predate registration, or have lost their
        # record. A public loopback health check can show availability, but does
        # not grant stop/restart ownership or infer that server's data directory.
        if data_dir is None:
            cert = paths['data'] / 'tls/localhost-cert.pem'
            candidates = [(DEFAULT_LOCAL_ORIGIN, None)]
            if cert.is_file():
                candidates.insert(0, ('https://127.0.0.1:8787', cert))
            for origin, certificate in candidates:
                if _origin_is_ready(origin, tls_cert=certificate):
                    return dict(running=True, ready=True, managed=False, stateMissing=True,
                        dataDirectoryVerified=False, origin=origin, port=8787,
                        state=str(paths['state']), log=str(paths['log']),
                        **({'find': _find_health(origin, certificate)} if include_find else {}))
        return {
            "running": False,
            **({'find': dict(running=False, session='webui-not-running')} if include_find else {}),
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
    ready = (process_running and bool(origin) and state.get('starting') is not True
             and _origin_is_ready(origin, tls_cert=tls_cert,
                                  **({'allow_auth_challenge': True} if state.get('lan') is True else {})))
    result = {
        **{key: value for key, value in state.items() if key not in {'launchProof', 'processIdentity'}},
        "running": process_running,
        "ready": ready,
        "state": str(paths["state"]),
        "log": str(paths["log"]),
    }
    if include_find:
        result['find'] = _find_health(origin, tls_cert) if ready else dict(running=False, session='webui-not-ready')
    return result


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


def _matches_webui_launch(state: dict[str, Any], nonce: str) -> bool:
    pid, proof = state.get('pid'), state.get('launchProof')
    if (type(pid) is not int or pid <= 0 or not isinstance(proof, str)
            or len(proof) != 64 or any(char not in '0123456789abcdef' for char in proof)):
        return False
    expected = hmac.new(nonce.encode('ascii'), str(pid).encode('ascii'), hashlib.sha256).hexdigest()
    return hmac.compare_digest(proof, expected)


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
    lan: bool = False,
    startup_timeout: float = 300.0,
) -> dict[str, Any]:
    if not 1 <= port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    allowed_hosts: tuple[str, ...] = ()
    listen = f"127.0.0.1:{port}"
    if lan:
        listen, tls_cert, tls_key, allowed_hosts = prepare_lan_https(listen, data_dir, tls_cert, tls_key, ())
    if enable_tls and tls_cert is None and tls_key is None:
        tls_cert, tls_key = ensure_local_tls_certificate(data_dir)
    tls_cert, tls_key = _validate_tls_pair(tls_cert, tls_key)
    paths = webui_service_paths(data_dir)
    paths["data"].mkdir(parents=True, exist_ok=True)
    paths["logDirectory"].mkdir(parents=True, exist_ok=True)
    existing = webui_background_status(data_dir)
    if existing.get("running"):
        if bool(existing.get("lan", False)) != lan:
            raise ValueError("InkTag WebUI is already running with different LAN settings; stop it before changing --lan")
        if int(existing.get("port", 0)) != port:
            raise ValueError(
                f"InkTag WebUI is already running on port {existing.get('port')}; "
                "stop or restart it first"
            )
        if (
            existing.get("tlsCert") != (str(tls_cert) if tls_cert else None) or
            existing.get("tlsKey") != (str(tls_key) if tls_key else None)
        ):
            raise ValueError("InkTag WebUI is already running with different HTTP/HTTPS settings; use webui restart with the desired TLS options")
        return {**existing, "alreadyRunning": True}

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
    if lan:
        command.extend(("--listen", listen))
        for host in allowed_hosts:
            command.extend(("--allowed-host", host))
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
    # Windows venv launchers can create a second Python PID. Bind the actual
    # registering process to this exact launch, rather than trusting any PID.
    launch_nonce = secrets.token_hex(32)
    child_environment['INKTAG_WEBUI_LAUNCH_NONCE'] = launch_nonce
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
        "lan": lan,
        **({"findService": normalize_local_origin(find_service)} if find_service else {}),
        **({"tlsCert": str(tls_cert), "tlsKey": str(tls_key)} if tls_cert else {}),
    }
    deadline = time.monotonic() + startup_timeout
    while time.monotonic() < deadline:
        return_code = process.poll()
        if return_code is not None:
            _remove_service_state(data_dir, process.pid)
            detail = _log_tail(paths["log"])
            raise RuntimeError(
                f"InkTag WebUI stopped during startup (exit {return_code}).\n{detail}"
            )
        observed = _read_service_state(data_dir) or {}
        owned = _matches_webui_launch(observed, launch_nonce)
        if owned and observed.get('starting') is not True and _origin_is_ready(
                origin, tls_cert=tls_cert, **({'allow_auth_challenge': True} if lan else {})):
            public_state = {key: value for key, value in observed.items() if key not in {'launchProof', 'processIdentity'}}
            return {**state, **public_state, "running": True, "ready": True, "log": str(paths["log"])}
        time.sleep(0.2)
    raise RuntimeError(
        f"InkTag WebUI did not become ready within {startup_timeout:g} seconds; "
        f"see {paths['log']}"
    )


def stop_webui_background(data_dir: Path | None = None, *, timeout: float = 10.0,
                           require_process_identity: bool = False) -> dict[str, Any]:
    paths = webui_service_paths(data_dir)
    state = _read_service_state(data_dir)
    if state is None:
        return {**webui_background_status(data_dir), "stopped": False}
    try:
        pid = int(state.get("pid", 0))
    except (TypeError, ValueError):
        pid = 0
    if not _pid_is_running(pid):
        _remove_service_state(data_dir, pid)
        return {**state, "running": False, "stopped": False, "log": str(paths["log"])}
    expected_identity = state.get('processIdentity')
    if expected_identity is not None or require_process_identity:
        if not isinstance(expected_identity, str) or _process_identity(pid) != expected_identity:
            raise OSError('Cannot verify the existing service process identity. Stop its original terminal/service once, then retry installation; no process was terminated.')
    state = {key: value for key, value in state.items() if key not in {'launchProof', 'processIdentity'}}
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
        def signal_owned_process(signum: int) -> None:
            try:
                # A foreground serve shares its terminal's group. Never kill
                # that group: only detached group leaders own their process tree.
                if os.getpgid(pid) == pid:
                    os.killpg(pid, signum)
                else:
                    os.kill(pid, signum)
            except ProcessLookupError:
                pass
        signal_owned_process(signal.SIGTERM)
        deadline = time.monotonic() + timeout
        while _pid_is_running(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
        if _pid_is_running(pid):
            signal_owned_process(signal.SIGKILL)
    if not terminated:
        deadline = time.monotonic() + timeout
        while _pid_is_running(pid) and time.monotonic() < deadline:
            time.sleep(0.1)
    _remove_service_state(data_dir, pid)
    return {**state, "running": False, "stopped": True, "log": str(paths["log"])}


def restart_webui_background(
    *, repo: Path | None = None, port: int | None = None, data_dir: Path | None = None,
    find_service: str | None = None,
    tls_cert: Path | None = None,
    tls_key: Path | None = None,
    enable_tls: bool = False,
) -> dict[str, Any]:
    state = _read_service_state(data_dir) or {}
    selected_port = port if port is not None else int(state.get("port", 8787))
    selected_repo = repo or (Path(str(state["repo"])) if state.get("repo") else None)
    selected_find_service = find_service or (
        str(state["findService"]) if state.get("findService") else None
    )
    if enable_tls and tls_cert is None and tls_key is None:
        tls_cert, tls_key = ensure_local_tls_certificate(data_dir)
    tls_cert, tls_key = _validate_tls_pair(tls_cert, tls_key)
    if not 1 <= selected_port <= 65535:
        raise ValueError("--port must be between 1 and 65535")
    selected_repo = find_repo_root(selected_repo)
    stopped = stop_webui_background(data_dir)
    if stopped.get('running'):
        raise ValueError('WebUI is reachable but its process is not registered; close that serve process before restarting')
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
