from __future__ import annotations

import argparse
import base64
import hashlib
import json
import mimetypes
import os
import re
import secrets
import shutil
import socket
import sqlite3
import ssl
import subprocess
import sys
import threading
import tempfile
import time
import webbrowser
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import ip_address
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import quote, unquote, urlsplit
from urllib.request import Request, urlopen

from .image_link_leases import ImageLinkLeases

SERVER_VERSION = "1.0.0"
WEBUI_CSP = (
    "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; script-src-attr 'none'; "
    "style-src 'self' 'unsafe-inline'; img-src 'self' data: blob: https://tile.openstreetmap.org; "
    "font-src 'self' data:; connect-src 'self' data: blob: https://api.open-meteo.com "
    "https://solar2ain.github.io/ink-tag/updates/; object-src 'none'; frame-src 'none'; "
    "worker-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"
)
JSON_LIMIT = 2 * 1024 * 1024
ASSET_LIMIT = 32 * 1024 * 1024
FIND_JSON_LIMIT = 16 * 1024
RECORD_COLLECTIONS = ("pages", "screen-profiles", "device-preferences")
_COLLECTION_SET = frozenset(RECORD_COLLECTIONS)
_LOOPBACK_HOSTS = frozenset(("127.0.0.1", "::1", "localhost"))
_INLINE_ASSET_TYPES = frozenset(('image/png', 'image/jpeg', 'image/gif', 'image/webp', 'image/avif', 'image/bmp'))
# Only immutable build outputs, never HTML, account/API data or user uploads.
_BUILD_ASSET = re.compile(r'^[A-Za-z0-9_.-]+-[A-Za-z0-9_-]{8}\.(?:js|css|png|jpg|jpeg|gif|webp|svg|woff2?|ttf|wasm)$')


def _host_name(value: str) -> str:
    """Validate a configured hostname, never a request-derived authority."""
    if not value or any(ord(c) <= 32 or ord(c) == 127 for c in value) or any(c in value for c in '/@?#\\'):
        raise ValueError('Invalid trusted host')
    parsed = urlsplit('//' + (f'[{value}]' if ':' in value and not value.startswith('[') else value))
    if parsed.port is not None or not parsed.hostname or parsed.hostname in {'0.0.0.0', '::'}:
        raise ValueError('Trusted hosts must be concrete hostnames or IP addresses without ports')
    return parsed.hostname.lower()


def local_lan_hosts() -> tuple[str, ...]:
    """Snapshot local interface addresses, never trust a request's DNS name."""
    addresses = set()
    for result in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
        address = ip_address(result[4][0])
        if not address.is_loopback and not address.is_unspecified:
            addresses.add(str(address))
    return tuple(sorted(addresses))


def read_access_password() -> str:
    value = sys.stdin.readline().rstrip('\r\n')
    if not value or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError('The server access password must be a nonempty line without control characters')
    return value
_IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$")
_FIND_ROUTES = {
    "/api/v1/find/backup/export": ("POST", "/api/backup/export"),
    "/api/v1/find/backup/import": ("POST", "/api/backup/import"),
    "/api/v1/find/health": ("GET", "/api/health"),
    "/api/v1/find/status": ("GET", "/api/status"),
    "/api/v1/find/login": ("POST", "/api/login"),
    "/api/v1/find/2fa/request": ("POST", "/api/2fa/request"),
    "/api/v1/find/2fa/submit": ("POST", "/api/2fa/submit"),
    "/api/v1/find/query": ("POST", "/api/query"),
    "/api/v1/find/logout": ("POST", "/api/logout"),
    "/api/v1/find/identity/import-local-v2": ("POST", "/api/identity/import-local-v2"),
    "/api/v1/find/identity/select": ("POST", "/api/identity/select"),
    "/api/v1/find/identity/proof/verify": ("POST", "/api/identity/proof/verify"),
    "/api/v1/find/identity/owner-action/prove": ("POST", "/api/identity/owner-action/prove"),
    "/api/v1/find/identity/update": ("POST", "/api/identity/update"),
    "/api/v1/find/identity/public-only/prepare": ("POST", "/api/identity/public-only/prepare"),
    "/api/v1/find/identity/public-only/replace/prepare": ("POST", "/api/identity/public-only/replace/prepare"),
    "/api/v1/find/identity/public-only/finalize": ("POST", "/api/identity/public-only/finalize"),
    "/api/v1/find/identity/public-only/abort": ("POST", "/api/identity/public-only/abort"),
    "/api/v1/find/identity/public-only/recover": ("POST", "/api/identity/public-only/recover"),
    "/api/v1/find/identity/delete": ("POST", "/api/identity/delete"),
    "/api/v1/find/tracking/configure": ("POST", "/api/tracking/configure"),
    "/api/v1/find/tracking/configure-all": ("POST", "/api/tracking/configure-all"),
    "/api/v1/find/tracking/history": ("POST", "/api/tracking/history"),
    "/api/v1/find/tracking/map": ("POST", "/api/tracking/map"),
    "/api/v1/find/tracking/clear": ("POST", "/api/tracking/clear"),
}


class LocalServerError(RuntimeError):
    def __init__(self, message: str, status: int = HTTPStatus.INTERNAL_SERVER_ERROR):
        super().__init__(message)
        self.status = int(status)


def default_data_directory() -> Path:
    if sys.platform == "darwin":
        preferred = Path.home() / ".inktag"
        legacy = Path.home() / "Library" / "Application Support" / "InkTag"
        # Keep existing identities, sessions and a running service discoverable.
        # A directory default must never move/delete a live database or venv.
        return legacy if not preferred.exists() and legacy.exists() else preferred
    if os.name == "nt":
        return Path(os.environ.get("LOCALAPPDATA", Path.home())) / "InkTag"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "inktag"


def parse_listen(value: str) -> tuple[str, int]:
    value = value.strip()
    if value.startswith("["):
        closing = value.find("]")
        if closing < 0 or closing + 1 >= len(value) or value[closing + 1] != ":":
            raise ValueError("--listen must use host:port")
        host, port_text = value[1:closing], value[closing + 2 :]
    else:
        host, separator, port_text = value.rpartition(":")
        if not separator or not host:
            raise ValueError("--listen must use host:port")
    try:
        port = int(port_text)
    except ValueError as error:
        raise ValueError("listen port must be between 0 and 65535") from error
    if not 0 <= port <= 65535:
        raise ValueError("listen port must be between 0 and 65535")
    return host, port


def override_listen_port(value: str, port: int | None) -> str:
    """Replace only the port of a validated listen address."""
    host, current_port = parse_listen(value)
    selected_port = current_port if port is None else port
    if not 0 <= selected_port <= 65535:
        raise ValueError("--port must be between 0 and 65535")
    formatted_host = f"[{host}]" if ":" in host else host
    return f"{formatted_host}:{selected_port}"


def normalize_find_service(value: str) -> str:
    parsed = urlsplit(value.strip())
    if (
        parsed.scheme != "http"
        or parsed.hostname not in _LOOPBACK_HOSTS
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or parsed.path not in ("", "/")
    ):
        raise ValueError("--find-service must use an uncredentialed loopback HTTP URL")
    try:
        port = parsed.port
    except ValueError as error:
        raise ValueError("--find-service must include a valid port") from error
    if port is None:
        raise ValueError("--find-service must include a port")
    host = f"[{parsed.hostname}]" if ":" in parsed.hostname else parsed.hostname
    return f"http://{host}:{port}"


@dataclass(frozen=True)
class LocalServerConfig:
    host: str
    port: int
    data_dir: Path
    web_root: Path
    cert_path: Path | None = None
    key_path: Path | None = None
    find_service: str | None = None
    allowed_hosts: tuple[str, ...] = ()
    access_password: str | None = field(default=None, repr=False)

    def __post_init__(self) -> None:
        if not 0 <= self.port <= 65535:
            raise ValueError("listen port must be between 0 and 65535")
        if bool(self.cert_path) != bool(self.key_path):
            raise ValueError("--tls-cert and --tls-key must be provided together")
        if self.host not in _LOOPBACK_HOSTS and not self.cert_path:
            raise ValueError(
                "LAN listening requires --tls-cert and --tls-key so Web Bluetooth has a secure context"
            )
        if self.host not in _LOOPBACK_HOSTS and not self.access_password:
            raise ValueError('LAN listening requires an access password')
        if self.host in {'0.0.0.0', '::'} and not self.allowed_hosts:
            raise ValueError('Wildcard LAN listening requires --allowed-host with the hostname or IP used by browsers')
        for host in self.allowed_hosts:
            _host_name(host)
        if not (self.web_root / "index.html").is_file():
            raise ValueError("WebUI build directory was not found; pass --web-root")


class LocalStore:
    def __init__(self, database_path: Path):
        database_path.parent.mkdir(parents=True, exist_ok=True)
        self.path = database_path
        self._lock = threading.RLock()
        self._database = sqlite3.connect(database_path, check_same_thread=False)
        with self._lock:
            self._database.executescript(
                """
                PRAGMA journal_mode = WAL;
                PRAGMA foreign_keys = ON;
                CREATE TABLE IF NOT EXISTS records (
                  collection TEXT NOT NULL,
                  id TEXT NOT NULL,
                  payload TEXT NOT NULL,
                  created_at INTEGER NOT NULL,
                  updated_at INTEGER NOT NULL,
                  PRIMARY KEY (collection, id)
                );
                CREATE INDEX IF NOT EXISTS records_updated_at
                  ON records(collection, updated_at DESC);
                CREATE TABLE IF NOT EXISTS assets (
                  id TEXT PRIMARY KEY,
                  name TEXT NOT NULL,
                  content_type TEXT NOT NULL,
                  content BLOB NOT NULL,
                  created_at INTEGER NOT NULL,
                  updated_at INTEGER NOT NULL
                );
                """
            )
            self._database.commit()

    @staticmethod
    def _record(row: tuple[Any, ...]) -> dict[str, Any]:
        return {
            "id": row[0],
            "data": json.loads(row[1]),
            "createdAt": row[2],
            "updatedAt": row[3],
        }

    @staticmethod
    def _collection(value: str) -> str:
        if value not in _COLLECTION_SET:
            raise LocalServerError("Unsupported collection", HTTPStatus.BAD_REQUEST)
        return value

    def list_records(self, collection: str) -> list[dict[str, Any]]:
        collection = self._collection(collection)
        with self._lock:
            rows = self._database.execute(
                "SELECT id, payload, created_at, updated_at FROM records "
                "WHERE collection=? ORDER BY updated_at DESC",
                (collection,),
            ).fetchall()
        return [self._record(row) for row in rows]

    def get_record(self, collection: str, identifier: str) -> dict[str, Any] | None:
        collection = self._collection(collection)
        with self._lock:
            row = self._database.execute(
                "SELECT id, payload, created_at, updated_at FROM records "
                "WHERE collection=? AND id=?",
                (collection, identifier),
            ).fetchone()
        return self._record(row) if row else None

    def put_record(self, collection: str, identifier: str, payload: dict[str, Any]) -> dict[str, Any]:
        collection = self._collection(collection)
        now = int(time.time() * 1000)
        encoded = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        with self._lock, self._database:
            self._database.execute(
                "INSERT INTO records(collection,id,payload,created_at,updated_at) VALUES(?,?,?,?,?) "
                "ON CONFLICT(collection,id) DO UPDATE SET payload=excluded.payload, updated_at=excluded.updated_at",
                (collection, identifier, encoded, now, now),
            )
        result = self.get_record(collection, identifier)
        assert result is not None
        return result

    def delete_record(self, collection: str, identifier: str) -> bool:
        collection = self._collection(collection)
        with self._lock, self._database:
            cursor = self._database.execute(
                "DELETE FROM records WHERE collection=? AND id=?", (collection, identifier)
            )
        return cursor.rowcount > 0

    def list_assets(self) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._database.execute(
                "SELECT id,name,content_type,length(content),created_at,updated_at "
                "FROM assets ORDER BY updated_at DESC"
            ).fetchall()
        return [
            {
                "id": row[0], "name": row[1], "contentType": row[2], "bytes": row[3],
                "createdAt": row[4], "updatedAt": row[5],
            }
            for row in rows
        ]

    def get_asset(self, identifier: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._database.execute(
                "SELECT id,name,content_type,content,created_at,updated_at FROM assets WHERE id=?",
                (identifier,),
            ).fetchone()
        if not row:
            return None
        return {
            "id": row[0], "name": row[1], "contentType": row[2], "content": bytes(row[3]),
            "createdAt": row[4], "updatedAt": row[5],
        }

    def put_asset(self, identifier: str, name: str, content_type: str, content: bytes) -> dict[str, Any]:
        now = int(time.time() * 1000)
        with self._lock, self._database:
            self._database.execute(
                "INSERT INTO assets(id,name,content_type,content,created_at,updated_at) VALUES(?,?,?,?,?,?) "
                "ON CONFLICT(id) DO UPDATE SET name=excluded.name, content_type=excluded.content_type, "
                "content=excluded.content, updated_at=excluded.updated_at",
                (identifier, name, content_type, content, now, now),
            )
        result = self.get_asset(identifier)
        assert result is not None
        return result

    def delete_asset(self, identifier: str) -> bool:
        with self._lock, self._database:
            cursor = self._database.execute("DELETE FROM assets WHERE id=?", (identifier,))
        return cursor.rowcount > 0

    def export_all(self) -> dict[str, Any]:
        assets = []
        for metadata in self.list_assets():
            asset = self.get_asset(str(metadata["id"]))
            assert asset is not None
            assets.append({
                **metadata,
                "contentBase64": base64.b64encode(asset["content"]).decode("ascii"),
            })
        return {
            "schemaVersion": 1,
            "exportedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "records": {collection: self.list_records(collection) for collection in RECORD_COLLECTIONS},
            "assets": assets,
        }

    def close(self) -> None:
        with self._lock:
            self._database.close()


class _LocalHttpServer(ThreadingHTTPServer):
    daemon_threads = True
    tls_handshake_timeout = 10.0

    def __init__(self, config: LocalServerConfig, store: LocalStore):
        if ":" in config.host:
            self.address_family = socket.AF_INET6
        self.config = config
        self.store = store
        self.tls_context: ssl.SSLContext | None = None
        if config.cert_path and config.key_path:
            self.tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            self.tls_context.load_cert_chain(config.cert_path, config.key_path)
        self.image_links = ImageLinkLeases()
        namespace = hashlib.sha256(str(config.web_root.resolve()).encode()).hexdigest()[:16]
        self.asset_cache = config.data_dir / 'webui-build-cache' / namespace
        self._asset_lock = threading.Lock()
        self._cached_index: bytes | None = None
        self.retain_build_assets()
        super().__init__((config.host, config.port), _LocalRequestHandler)

    def process_request_thread(self, request, client_address) -> None:
        if self.tls_context is not None:
            # Handshake only after dispatch: a speculative TCP connection must
            # not block the accept loop or another client's lease renewal.
            original_timeout = request.gettimeout()
            try:
                request = self.tls_context.wrap_socket(
                    request, server_side=True, do_handshake_on_connect=False)
                request.settimeout(self.tls_handshake_timeout)
                request.do_handshake()
                request.settimeout(original_timeout)
            except OSError:
                request.close()
                return
        super().process_request_thread(request, client_address)

    def retain_build_assets(self, index: bytes | None = None) -> None:
        """Keep hashed chunks for already-open pages across local updates/restarts."""
        if index is None:
            index = (self.config.web_root / 'index.html').read_bytes()
        with self._asset_lock:
            if index == self._cached_index:
                return
            if not self.asset_cache.resolve().is_relative_to(self.config.data_dir.resolve()):
                raise ValueError('Invalid WebUI build cache directory')
            self.asset_cache.mkdir(parents=True, exist_ok=True)
            root = (self.config.web_root / 'assets').resolve()
            if not root.is_relative_to(self.config.web_root.resolve()):
                raise ValueError('Invalid WebUI assets directory')
            for source in root.glob('*'):
                if not _BUILD_ASSET.fullmatch(source.name) or not source.is_file() or source.is_symlink():
                    continue
                target = self.asset_cache / source.name
                if target.is_symlink():
                    raise ValueError('Invalid WebUI build cache link')
                if not target.exists():
                    with tempfile.NamedTemporaryFile(dir=self.asset_cache, delete=False) as stream:
                        temporary = Path(stream.name)
                    try:
                        shutil.copyfile(source, temporary)
                        os.replace(temporary, target)
                    finally:
                        temporary.unlink(missing_ok=True)
                # Retain at least thirty days since a build was last served.
                target.touch()
            cutoff = time.time() - 30 * 86400
            for old in self.asset_cache.iterdir():
                if _BUILD_ASSET.fullmatch(old.name) and not old.is_symlink() and old.is_file() and old.stat().st_mtime < cutoff:
                    old.unlink()
            self._cached_index = index


class _LocalRequestHandler(BaseHTTPRequestHandler):
    server: _LocalHttpServer

    def handle_one_request(self) -> None:
        self._request_started = time.monotonic()
        if self.server.tls_context is None:
            try:
                # ClientHello is binary, not an HTTP request line. Detect it
                # in this connection's worker; never block the accept loop or
                # print handshake bytes as terminal text.
                if self.rfile.peek(3).startswith(b'\x16\x03'):
                    self.close_connection = True
                    self.log_message('HTTPS request received on an HTTP port. Open http://127.0.0.1:%s/ or start with --enable-tls.', self.server.server_port)
                    return
            except OSError:
                self.close_connection = True
                return
        super().handle_one_request()

    def log_message(self, format_string: str, *args: object) -> None:
        message = format_string % args
        # Retain readable ordinary logs without permitting request bytes to
        # inject terminal control sequences (stdlib's logger does this too).
        message = ''.join(f'\\x{ord(c):02x}' if ord(c) < 32 or 127 <= ord(c) <= 159 else c for c in message)
        sys.stderr.write(f"{time.strftime('%H:%M:%S')} InkTag local server: {message}\n")
        sys.stderr.flush()

    def log_request(self, code='-', size='-') -> None:
        # Query strings can contain user data. Access logs describe requests,
        # never credentials, bodies or the contents of a Find identity.
        path = urlsplit(getattr(self, 'path', '')).path
        elapsed = (time.monotonic() - getattr(self, '_request_started', time.monotonic())) * 1000
        self.log_message('%s %s %s %.0fms', getattr(self, 'command', '?'), path, code, elapsed)

    def _common_headers(self) -> None:
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Permissions-Policy", "bluetooth=(self)")
        self.send_header('X-Frame-Options', 'DENY')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')

    def _send(self, status: int, content: bytes = b"", *, content_type: str | None = None,
              cache_control: str = "no-store", extra_headers: dict[str, str] | None = None) -> None:
        for key, value in {'Content-Type': content_type or '', 'Cache-Control': cache_control,
                           **(extra_headers or {})}.items():
            if (not re.fullmatch(r'[A-Za-z0-9-]+', key) or
                    any(ord(c) < 32 or ord(c) == 127 for c in value)):
                raise LocalServerError('Invalid response header', HTTPStatus.BAD_REQUEST)
        self.send_response(status)
        self._common_headers()
        if content_type:
            self.send_header("Content-Type", content_type)
        if content_type and content_type.split(';', 1)[0] in {'text/html', 'image/svg+xml'} and not (extra_headers or {}).get('Content-Security-Policy'):
            self.send_header('Content-Security-Policy', WEBUI_CSP)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", cache_control)
        for key, value in (extra_headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD" and content:
            self.wfile.write(content)

    def _json(self, status: int, payload: dict[str, Any]) -> None:
        self._send(
            status,
            json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
            content_type="application/json; charset=utf-8",
        )

    def _body(self, limit: int) -> bytes:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise LocalServerError("Invalid Content-Length", HTTPStatus.BAD_REQUEST) from error
        if length < 0 or length > limit:
            raise LocalServerError(f"Request body exceeds {limit} bytes", HTTPStatus.REQUEST_ENTITY_TOO_LARGE)
        data = self.rfile.read(length)
        if len(data) != length:
            raise LocalServerError("Request body is incomplete", HTTPStatus.BAD_REQUEST)
        return data

    def _json_body(self) -> dict[str, Any]:
        try:
            value = json.loads(self._body(JSON_LIMIT).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise LocalServerError("Request body must be valid JSON", HTTPStatus.BAD_REQUEST) from error
        if not isinstance(value, dict):
            raise LocalServerError("Request body must be a JSON object", HTTPStatus.BAD_REQUEST)
        return value

    @staticmethod
    def _identifier(value: str) -> str:
        if not _IDENTIFIER.fullmatch(value):
            raise LocalServerError(
                "Identifier must be 1-128 URL-safe characters", HTTPStatus.BAD_REQUEST
            )
        return value

    def _trusted_origins(self) -> set[str]:
        scheme = "https" if self.server.config.cert_path else "http"
        port = self.server.server_port
        config = self.server.config
        hosts = set(_LOOPBACK_HOSTS) | {_host_name(h) for h in config.allowed_hosts}
        if config.host not in {'0.0.0.0', '::'}:
            hosts.add(_host_name(config.host))
        authorities = {f'[{host}]' if ':' in host else host for host in hosts}
        return {f'{scheme}://{host}:{port}' for host in authorities} | (
            {f'{scheme}://{host}' for host in authorities} if port == (443 if scheme == 'https' else 80) else set())

    def _request_boundary(self, path: str) -> bool:
        hosts = self.headers.get_all('Host', [])
        scheme = 'https' if self.server.config.cert_path else 'http'
        origins = self._trusted_origins()
        if len(hosts) != 1 or f'{scheme}://{hosts[0].lower()}' not in origins:
            self._json(HTTPStatus.MISDIRECTED_REQUEST, {'error': 'untrusted_host'})
            return False
        supplied_origins = self.headers.get_all('Origin', [])
        if len(supplied_origins) > 1 or (supplied_origins and supplied_origins[0] not in origins):
            self._json(HTTPStatus.FORBIDDEN, {'error': 'untrusted_origin'})
            return False
        if self.command not in {'GET', 'HEAD', 'OPTIONS'} and not supplied_origins:
            self._json(HTTPStatus.FORBIDDEN, {'error': 'origin_required'})
            return False
        if path.startswith('/api/') and self.headers.get('Sec-Fetch-Site') == 'cross-site':
            self._json(HTTPStatus.FORBIDDEN, {'error': 'cross_site_request'})
            return False
        # Authenticate the listener, not the supplied Host or client address.
        # A LAN client must not bypass authentication by sending Host: localhost.
        password = self.server.config.access_password
        if password:
            supplied = self.headers.get_all('Authorization', [])
            expected = base64.b64encode(('inktag:' + password).encode()).decode('ascii')
            if len(supplied) != 1 or not secrets.compare_digest(supplied[0].encode(), ('Basic ' + expected).encode()):
                self._send(HTTPStatus.UNAUTHORIZED, b'Authentication required', content_type='text/plain; charset=utf-8',
                           extra_headers={'WWW-Authenticate': 'Basic realm="InkTag", charset="UTF-8"'})
                return False
        return True

    @staticmethod
    def _find_request(origin: str, path: str, *, method: str = "GET", body: bytes | None = None,
                      headers: dict[str, str] | None = None) -> tuple[int, bytes]:
        request = Request(origin + path, data=body, method=method, headers=headers or {})
        try:
            timeout = 100 if path in ("/api/login", "/api/2fa/submit", "/api/query") else 30
            with urlopen(request, timeout=timeout) as response:  # noqa: S310 - origin is loopback-validated
                return response.status, response.read()
        except HTTPError as error:
            return error.code, error.read()
        except TimeoutError as error:
            raise LocalServerError("Find 请求等待超时；服务不一定已停止，请稍后查看状态，不要重复提交验证码。",
                                   HTTPStatus.GATEWAY_TIMEOUT) from error
        except (URLError, OSError) as error:
            if isinstance(getattr(error, "reason", None), TimeoutError):
                raise LocalServerError("Find 请求等待超时；服务不一定已停止，请稍后查看状态，不要重复提交验证码。",
                                       HTTPStatus.GATEWAY_TIMEOUT) from error
            raise LocalServerError(
                "InkTag Find 本机服务未运行或无法访问", HTTPStatus.SERVICE_UNAVAILABLE
            ) from error

    def _proxy_find(self, path: str) -> bool:
        route = _FIND_ROUTES.get(path)
        if route is None:
            return False
        portable_backup = path in {"/api/v1/find/backup/export", "/api/v1/find/backup/import"}
        if portable_backup and not self.server.config.cert_path:
            try:
                peer = ip_address(self.client_address[0])
                mapped = getattr(peer, "ipv4_mapped", None)
                loopback = peer.is_loopback or bool(mapped and mapped.is_loopback)
            except ValueError:
                loopback = False
            if not loopback:
                self._json(HTTPStatus.FORBIDDEN, {"ok": False, "error": "迁移Find资料请使用HTTPS或本机地址，以保护迁移口令。"})
                return True
        find_service = self.server.config.find_service
        if find_service is None:
            self._json(HTTPStatus.SERVICE_UNAVAILABLE, {"ok": False, "error": "find_service_unavailable"})
            return True
        method, upstream_path = route
        if self.command != method:
            self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"ok": False, "error": "method_not_allowed"})
            return True
        body = None
        headers = {"Accept": "application/json"}
        if method == "POST":
            status_code, status_body = self._find_request(find_service, "/api/status")
            try:
                status = json.loads(status_body.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                status = {}
            if status_code >= 400 or not isinstance(status, dict) or not status.get("csrfToken"):
                self._json(status_code or HTTPStatus.BAD_GATEWAY, {
                    "ok": False,
                    "error": status.get("error", "Find 服务状态不可用") if isinstance(status, dict) else "Find 服务状态不可用",
                })
                return True
            body = self._body(16 * 1024 * 1024 if portable_backup else FIND_JSON_LIMIT)
            headers.update({
                "Content-Type": "application/json",
                "Origin": find_service,
                "X-InkTag-CSRF": str(status["csrfToken"]),
            })
        status_code, response_body = self._find_request(
            find_service, upstream_path, method=method, body=body, headers=headers
        )
        try:
            payload = json.loads(response_body.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            payload = {"ok": False, "error": "Find 本机服务返回了无效响应"}
        if isinstance(payload, dict):
            payload.pop("csrfToken", None)
        else:
            payload = {"ok": False, "error": "Find 本机服务返回了无效响应"}
        self._json(status_code, payload)
        return True

    def _records(self, path: str) -> bool:
        match = re.fullmatch(
            r"/api/v1/(pages|screen-profiles|device-preferences)(?:/([^/]+))?", path
        )
        if not match:
            return False
        collection, encoded_identifier = match.groups()
        identifier = self._identifier(unquote(encoded_identifier)) if encoded_identifier else None
        store = self.server.store
        if self.command == "GET" and identifier is None:
            self._json(HTTPStatus.OK, {"items": store.list_records(collection)})
        elif self.command == "GET":
            item = store.get_record(collection, identifier)
            self._json(HTTPStatus.OK, item) if item else self._json(HTTPStatus.NOT_FOUND, {"error": "not_found"})
        elif self.command == "PUT" and identifier is not None:
            self._json(HTTPStatus.OK, store.put_record(collection, identifier, self._json_body()))
        elif self.command == "DELETE" and identifier is not None:
            self._send(HTTPStatus.NO_CONTENT if store.delete_record(collection, identifier) else HTTPStatus.NOT_FOUND)
        else:
            self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method_not_allowed"})
        return True

    def _assets(self, path: str) -> bool:
        store = self.server.store
        if path == "/api/v1/assets":
            if self.command == "GET":
                self._json(HTTPStatus.OK, {"items": store.list_assets()})
            else:
                self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method_not_allowed"})
            return True
        match = re.fullmatch(r"/api/v1/assets/([^/]+)(?:/(content))?", path)
        if not match:
            return False
        identifier = self._identifier(unquote(match.group(1)))
        content_request = match.group(2) == "content"
        if self.command == "GET":
            asset = store.get_asset(identifier)
            if not asset:
                self._json(HTTPStatus.NOT_FOUND, {"error": "not_found"})
            elif content_request:
                safe_name = ''.join(c for c in str(asset['name']) if ord(c) >= 32 and ord(c) != 127)[:240] or 'asset'
                media_type = str(asset['contentType']).split(';', 1)[0].strip().lower()
                inline = media_type in _INLINE_ASSET_TYPES
                disposition = 'inline' if inline else 'attachment'
                self._send(HTTPStatus.OK, asset["content"], content_type=media_type if inline else 'application/octet-stream',
                           cache_control="private, max-age=3600",
                           extra_headers={
                               'Content-Disposition': f'{disposition}; filename="asset"; filename*=UTF-8\'\'{quote(safe_name, safe="")}',
                               'Content-Security-Policy': "sandbox; default-src 'none'; base-uri 'none'; frame-ancestors 'none'",
                           })
            else:
                self._json(HTTPStatus.OK, {
                    key: value for key, value in asset.items() if key != "content"
                } | {"bytes": len(asset["content"])})
        elif self.command == "PUT" and not content_request:
            content = self._body(ASSET_LIMIT)
            raw_name = self.headers.get("X-InkTag-Filename", identifier)
            try:
                name = unquote(raw_name)[:240] or identifier
            except Exception:
                name = identifier
            content_type = self.headers.get("Content-Type", "application/octet-stream")[:120]
            if any(ord(c) < 32 or ord(c) == 127 for c in name + content_type):
                raise LocalServerError('Asset metadata contains control characters', HTTPStatus.BAD_REQUEST)
            asset = store.put_asset(identifier, name, content_type, content)
            self._json(HTTPStatus.OK, {
                key: value for key, value in asset.items() if key != "content"
            } | {"bytes": len(asset["content"])})
        elif self.command == "DELETE" and not content_request:
            self._send(HTTPStatus.NO_CONTENT if store.delete_asset(identifier) else HTTPStatus.NOT_FOUND)
        else:
            self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method_not_allowed"})
        return True

    def _image_link(self, path: str) -> bool:
        if not path.startswith('/api/v1/image-link/'):
            return False
        # LAN clients must never borrow a Bluetooth connection on this host.
        host = urlsplit('//' + self.headers.get('Host', '')).hostname
        peer = ip_address(self.client_address[0])
        if host not in _LOOPBACK_HOSTS or not peer.is_loopback:
            self._json(HTTPStatus.FORBIDDEN, {'error': 'local_only'})
            return True
        if urlsplit(self.path).query:
            raise LocalServerError('Image link parameters belong in JSON', HTTPStatus.BAD_REQUEST)
        links = self.server.image_links
        if path == '/api/v1/image-link/capabilities' and self.command == 'GET':
            self._json(HTTPStatus.OK, {'available': links.supported(), 'csrfToken': links.csrf})
            return True
        action = path.rsplit('/', 1)[-1]
        if action not in {'acquire', 'renew', 'release', 'inspect'}:
            self._json(HTTPStatus.NOT_FOUND, {'error': 'api_not_found'})
            return True
        if self.command != 'POST':
            self._json(HTTPStatus.METHOD_NOT_ALLOWED, {'error': 'method_not_allowed'})
            return True
        origin = f"{'https' if self.server.config.cert_path else 'http'}://{self.headers['Host']}"
        tokens = self.headers.get_all('X-InkTag-Image-Link-CSRF', [])
        if (self.headers.get('Origin') != origin or len(tokens) != 1 or
                not secrets.compare_digest(tokens[0].encode('utf-8'), links.csrf.encode('utf-8'))):
            self._json(HTTPStatus.FORBIDDEN, {'error': 'image_link_csrf'})
            return True
        if self.headers.get('Content-Type', '').split(';')[0].strip() != 'application/json':
            raise LocalServerError('JSON is required', HTTPStatus.UNSUPPORTED_MEDIA_TYPE)
        try:
            body = json.loads(self._body(1024))
        except (ValueError, UnicodeDecodeError) as error:
            raise LocalServerError('Invalid image link JSON', HTTPStatus.BAD_REQUEST) from error
        if not isinstance(body, dict):
            raise LocalServerError('Invalid image link JSON', HTTPStatus.BAD_REQUEST)
        if action == 'acquire':
            address, board = body.get('address'), body.get('boardId', '')
            if (not isinstance(address, str) or
                    not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', address) or
                    int(address.replace(':', ''), 16) == 0 or
                    not isinstance(board, str) or len(board) > 80 or
                    set(body) - {'address', 'boardId'}):
                raise LocalServerError('Invalid connected peer', HTTPStatus.BAD_REQUEST)
        elif (set(body) != {'lease'} or not isinstance(body.get('lease'), str) or
                not re.fullmatch(r'[A-Za-z0-9_-]{32}', body['lease'])):
            raise LocalServerError('Invalid image link lease', HTTPStatus.BAD_REQUEST)
        self._json(HTTPStatus.OK, links.call(action, body))
        return True

    def _api(self, path: str) -> bool:
        if self._image_link(path):
            return True
        if path == "/api/v1/health" and self.command == "GET":
            self._json(HTTPStatus.OK, {"ok": True, "serverVersion": SERVER_VERSION})
            return True
        if path == "/api/v1/capabilities" and self.command == "GET":
            features = ["records", "assets", "backup-export"]
            if self.server.config.find_service:
                features.append("find-query")
            self._json(HTTPStatus.OK, {
                "apiVersion": 1,
                "serverVersion": SERVER_VERSION,
                "runtime": "python",
                "features": features,
                "collections": list(RECORD_COLLECTIONS),
                "limits": {"jsonBytes": JSON_LIMIT, "assetBytes": ASSET_LIMIT},
            })
            return True
        if path == "/api/v1/export" and self.command == "GET":
            self._json(HTTPStatus.OK, self.server.store.export_all())
            return True
        if self._proxy_find(path) or self._records(path) or self._assets(path):
            return True
        if path.startswith("/api/"):
            self._json(HTTPStatus.NOT_FOUND, {"error": "api_not_found"})
            return True
        return False

    def _static(self, path: str) -> None:
        decoded = unquote(path)
        root = self.server.config.web_root.resolve()
        requested = (root / decoded.lstrip("/")).resolve()
        try:
            requested.relative_to(root)
        except ValueError:
            self._json(HTTPStatus.NOT_FOUND, {"error": "not_found"})
            return
        if not requested.is_file():
            relative = requested.relative_to(root)
            if len(relative.parts) == 2 and relative.parts[0] == 'assets' and _BUILD_ASSET.fullmatch(relative.name):
                cached = self.server.asset_cache / relative.name
                if cached.is_file() and not cached.is_symlink():
                    requested = cached
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "asset_not_found"})
                    return
            elif relative.parts and (relative.parts[0] == 'assets' or requested.suffix):
                self._json(HTTPStatus.NOT_FOUND, {"error": "not_found"})
                return
            else:
                requested = root / "index.html"
        content = requested.read_bytes()
        if requested == root / 'index.html':
            self.server.retain_build_assets(content)
        content_type = mimetypes.guess_type(requested.name)[0] or "application/octet-stream"
        if content_type.startswith("text/") or content_type in ("application/javascript", "application/json"):
            content_type += "; charset=utf-8"
        cache = "public, max-age=31536000, immutable" if _BUILD_ASSET.fullmatch(requested.name) else "no-cache"
        self._send(HTTPStatus.OK, content, content_type=content_type, cache_control=cache)

    def _handle(self) -> None:
        try:
            path = urlsplit(self.path).path
            if not self._request_boundary(path):
                return
            if not self._api(path):
                if self.command not in ("GET", "HEAD"):
                    self._json(HTTPStatus.METHOD_NOT_ALLOWED, {"error": "method_not_allowed"})
                else:
                    self._static(path)
        except LocalServerError as error:
            self._json(error.status, {"error": str(error)})
        except Exception as error:  # pragma: no cover - defensive server boundary
            self._json(HTTPStatus.INTERNAL_SERVER_ERROR, {"error": str(error) or "internal_error"})

    do_GET = _handle
    do_HEAD = _handle
    do_POST = _handle
    do_PUT = _handle
    do_DELETE = _handle


class InkTagLocalServer:
    def __init__(self, config: LocalServerConfig):
        self.config = config
        self.store = LocalStore(config.data_dir / "inktag.sqlite3")
        try:
            self.httpd = _LocalHttpServer(config, self.store)
        except Exception:
            self.store.close()
            raise
        address = self.httpd.server_address
        self.port = int(address[1])
        browser_host = config.allowed_hosts[0] if config.host in {'0.0.0.0', '::'} else config.host
        host = f"[{browser_host}]" if ":" in browser_host else browser_host
        self.origin = f"{'https' if config.cert_path else 'http'}://{host}:{self.port}"
        self._thread: threading.Thread | None = None
        self._closed = False

    def start_in_thread(self) -> None:
        if self._thread is not None:
            raise RuntimeError("local server is already running")
        self._thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self._thread.start()

    def serve_forever(self) -> None:
        self.httpd.serve_forever()

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if self._thread is not None:
            self.httpd.shutdown()
        self.httpd.server_close()
        self.httpd.image_links.close()
        if self._thread is not None:
            self._thread.join(timeout=5)
            self._thread = None
        self.store.close()


class ManagedFindService:
    def __init__(
        self, repo_root: Path, data_dir: Path, *, no_install: bool,
        log_path: Path | None = None,
    ):
        self.repo_root = repo_root
        self.data_dir = data_dir
        self.no_install = no_install
        self.log_path = log_path
        self.process: subprocess.Popen[bytes] | None = None
        self.origin = ""
        self._command: list[str] = []
        self._popen_options: dict[str, Any] = {}
        self._closing = threading.Event()
        self._process_lock = threading.Lock()
        self._watchdog: threading.Thread | None = None

    @staticmethod
    def _free_port() -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            return int(listener.getsockname()[1])

    def _append_log(self, message: str) -> None:
        if self.log_path is None:
            print(message, flush=True)
            return
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        with self.log_path.open("ab") as log_file:
            log_file.write(
                f"[{datetime.now(timezone.utc).isoformat()}] {message}\n".encode()
            )

    def _spawn(self) -> subprocess.Popen[bytes]:
        if self.log_path is None:
            process = subprocess.Popen(
                self._command, cwd=self.repo_root, **self._popen_options
            )
        else:
            self.log_path.parent.mkdir(parents=True, exist_ok=True)
            with self.log_path.open("ab") as log_file:
                self._append_log("starting InkTag Find")
                try:
                    process = subprocess.Popen(
                        self._command,
                        cwd=self.repo_root,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        **self._popen_options,
                    )
                except OSError as error:
                    if os.name != "nt" or getattr(error, "winerror", None) != 5:
                        raise
                    retry_options = dict(self._popen_options)
                    retry_options["creationflags"] &= ~subprocess.CREATE_BREAKAWAY_FROM_JOB
                    process = subprocess.Popen(
                        self._command,
                        cwd=self.repo_root,
                        stdout=log_file,
                        stderr=subprocess.STDOUT,
                        **retry_options,
                    )
        with self._process_lock:
            self.process = process
        return process

    def _wait_ready(self, process: subprocess.Popen[bytes], timeout: float) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not self._closing.is_set():
            if process.poll() is not None:
                raise RuntimeError(
                    f"InkTag Find stopped during startup (exit {process.returncode})"
                )
            try:
                with urlopen(f"{self.origin}/api/status", timeout=1):  # noqa: S310 - fixed loopback
                    return
            except (URLError, TimeoutError, OSError):
                time.sleep(0.15)
        raise RuntimeError("InkTag Find did not become ready before the startup deadline")

    def _watch(self) -> None:
        while not self._closing.wait(1):
            with self._process_lock:
                process = self.process
            if process is None or process.poll() is None:
                continue
            self._append_log(
                f"Find process exited unexpectedly (exit {process.returncode}); restarting"
            )
            for attempt in range(1, 6):
                if self._closing.is_set():
                    return
                try:
                    replacement = self._spawn()
                    self._wait_ready(replacement, 30)
                    self._append_log(f"Find process restart succeeded (attempt {attempt})")
                    break
                except (OSError, RuntimeError) as error:
                    self._append_log(
                        f"Find process restart failed (attempt {attempt}): {error}"
                    )
                    if self._closing.wait(min(attempt * 2, 10)):
                        return
            else:
                self._append_log("Find process restart abandoned after five attempts")
                return

    def start(self) -> str:
        bootstrap = self.repo_root / "tools" / "findmy" / "bootstrap.py"
        query_service = self.repo_root / "tools" / "findmy" / "query_webui.py"
        runtime_root = self.data_dir / "runtime"
        if not bootstrap.is_file() or not query_service.is_file():
            raise ValueError("InkTag Find sources were not found below the selected repository")
        port = self._free_port()
        self.origin = f"http://127.0.0.1:{port}"
        prepare = [
            sys.executable, str(bootstrap),
            "--data-dir", str(self.data_dir), "--runtime-dir", str(runtime_root),
            "--host", "127.0.0.1", "--port", str(port), "--prepare-only",
        ]
        if self.no_install:
            prepare.append("--no-install")
        prepare_options: dict[str, Any] = {}
        if os.name == "nt":
            startup_info = subprocess.STARTUPINFO()
            startup_info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup_info.wShowWindow = subprocess.SW_HIDE
            prepare_options["startupinfo"] = startup_info
            prepare_options["creationflags"] = subprocess.CREATE_NO_WINDOW
        subprocess.run(prepare, cwd=self.repo_root, check=True, **prepare_options)
        runtime_python = runtime_root / (
            ".venv/Scripts/pythonw.exe" if os.name == "nt" else ".venv/bin/python"
        )
        if os.name == "nt" and not runtime_python.is_file():
            runtime_python = runtime_root / ".venv/Scripts/python.exe"
        self._command = [
            str(runtime_python), "-u", str(query_service),
            "--anisette-libs", str(self.data_dir / "ani_libs.bin"),
            "--host", "127.0.0.1", "--port", str(port),
            "--parent-pid", str(os.getpid()),
        ]
        self._popen_options = {}
        if os.name == "nt":
            startup_info = subprocess.STARTUPINFO()
            startup_info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup_info.wShowWindow = subprocess.SW_HIDE
            self._popen_options["startupinfo"] = startup_info
            self._popen_options["creationflags"] = (
                subprocess.CREATE_NEW_PROCESS_GROUP
                | subprocess.DETACHED_PROCESS
                | subprocess.CREATE_NO_WINDOW
                | subprocess.CREATE_BREAKAWAY_FROM_JOB
            )
        self._closing.clear()
        process = self._spawn()
        try:
            self._wait_ready(process, 300)
        except RuntimeError:
            self.close()
            raise
        self._watchdog = threading.Thread(
            target=self._watch, name="inktag-find-watchdog", daemon=True
        )
        self._watchdog.start()
        return self.origin

    def close(self) -> None:
        self._closing.set()
        with self._process_lock:
            process, self.process = self.process, None
        if process is None or process.poll() is not None:
            return
        if os.name == "nt":
            subprocess.run(
                ["taskkill.exe", "/pid", str(process.pid), "/t", "/f"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
            )
        else:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
        watchdog, self._watchdog = self._watchdog, None
        if watchdog is not None and watchdog is not threading.current_thread():
            watchdog.join(timeout=2)


def run_local_server(
    *, repo_root: Path, listen: str = "127.0.0.1:8787", port: int | None = None,
    data_dir: Path | None = None,
    web_root: Path | None = None, cert_path: Path | None = None, key_path: Path | None = None,
    find_service: str | None = None,
    find_data_dir: Path | None = None, find_no_install: bool = False,
    no_open: bool = False,
    allowed_hosts: tuple[str, ...] = (), access_password: str | None = None,
) -> int:
    host, selected_port = parse_listen(override_listen_port(listen, port))
    resolved_data = (data_dir or default_data_directory()).resolve()
    resolved_find_data = (find_data_dir or resolved_data / "find").resolve()
    candidates = (repo_root / "webui/dist/static", repo_root / "webui", repo_root / "public")
    resolved_web = (web_root or next((path for path in candidates if (path / "index.html").is_file()), candidates[0])).resolve()
    # Pair assets before opening data, Find, TLS or sockets. Restarting Python
    # does not compile JS; an old frontend must not tell users to reflash a
    # perfectly current device because it expects a different protocol byte.
    from .webui_build import inspect_webui_build
    source_repo = repo_root if resolved_web == (repo_root / 'webui/dist/static').resolve() else None
    web_build = inspect_webui_build(resolved_web, repo_root=source_repo)
    print(f"WebUI: {resolved_web} ({web_build['buildId'] or 'v1'})")
    managed: ManagedFindService | None = None
    resolved_find = normalize_find_service(find_service) if find_service else None
    generated_password = access_password is None and host not in _LOOPBACK_HOSTS
    lan_password = secrets.token_urlsafe(18) if generated_password else access_password
    if host in {'0.0.0.0', '::'} and not allowed_hosts:
        allowed_hosts = local_lan_hosts()
    # Reject bad configuration before starting a backend or opening a database.
    config = LocalServerConfig(
        host=host, port=selected_port, data_dir=resolved_data, web_root=resolved_web,
        cert_path=cert_path.resolve() if cert_path else None,
        key_path=key_path.resolve() if key_path else None,
        find_service=resolved_find, allowed_hosts=allowed_hosts, access_password=lan_password,
    )
    if config.cert_path:
        ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER).load_cert_chain(config.cert_path, config.key_path)
    # Claim the profile before Find/database/socket work. Only the child owns
    # this registration; an old parent must not overwrite a newer foreground
    # or HTTPS process after spawn or delete it when its own startup fails.
    from .local_service import _write_service_state, _remove_service_state, _process_identity
    launch_nonce = os.environ.pop('INKTAG_WEBUI_LAUNCH_NONCE', None)
    launch_binding = {}
    process_identity = _process_identity(os.getpid())
    if process_identity:
        launch_binding['processIdentity'] = process_identity
    if launch_nonce:
        import hmac
        launch_binding['launchProof'] = hmac.new(launch_nonce.encode('ascii'), str(os.getpid()).encode('ascii'), hashlib.sha256).hexdigest()
    service_state = dict(pid=os.getpid(), port=selected_port,
        origin=f'{"https" if cert_path else "http"}://127.0.0.1:{selected_port}',
        repo=str(repo_root.resolve()), dataDir=str(resolved_data), starting=True, lan=host not in _LOOPBACK_HOSTS,
        **launch_binding,
        **({'tlsCert': str(config.cert_path), 'tlsKey': str(config.key_path)} if config.cert_path else {}))
    _write_service_state(resolved_data, service_state)
    server = None
    try:
        find_runtime = (repo_root / 'tools/findmy/bootstrap.py', repo_root / 'tools/findmy/query_webui.py')
        # A clients-only checkout intentionally omits the separate Find runtime.
        # A partially installed runtime must still fail its normal startup check.
        if not find_service and any(path.is_file() for path in find_runtime):
            managed = ManagedFindService(
                repo_root, resolved_find_data, no_install=find_no_install,
                log_path=resolved_data / "log" / "find.log",
            )
            print("Starting the managed InkTag Find service...")
            resolved_find = managed.start()
            print("InkTag Find is ready behind the main WebUI address.")
        elif not find_service:
            print("Find query runtime is missing; WebUI remains available. Use the complete InkTag CLI ZIP or repository and run install_inktag.sh / install_inktag.cmd at its root.")
        config = LocalServerConfig(
            host=host, port=selected_port, data_dir=resolved_data, web_root=resolved_web,
            cert_path=cert_path.resolve() if cert_path else None,
            key_path=key_path.resolve() if key_path else None,
            find_service=resolved_find,
            allowed_hosts=allowed_hosts, access_password=lan_password,
        )
        server = InkTagLocalServer(config)
        # Foreground serve and detached start share the same lifecycle record.
        # A stopped old process must never unlink a replacement owner's state.
        _write_service_state(resolved_data, dict(pid=os.getpid(), port=server.port,
            origin=f'{"https" if config.cert_path else "http"}://127.0.0.1:{server.port}',
            browserOrigin=server.origin, repo=str(repo_root.resolve()), dataDir=str(resolved_data), lan=host not in _LOOPBACK_HOSTS,
            **launch_binding,
            **({'findService': resolved_find} if find_service else {}),
            **({'tlsCert': str(config.cert_path), 'tlsKey': str(config.key_path)} if config.cert_path else {})))
        print(f"InkTag local server is ready: {server.origin}/")
        if host in {'0.0.0.0', '::'}:
            for trusted_host in allowed_hosts[1:]:
                authority = f'[{trusted_host}]' if ':' in trusted_host else trusted_host
                print(f'Also available: https://{authority}:{server.port}/')
        print(f"Data: {server.store.path}")
        if generated_password:
            print('LAN access username: inktag')
            print(f'LAN access password (this run only): {lan_password}')
        if not no_open:
            webbrowser.open(f"{server.origin}/")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.close()
        return 0
    finally:
        if server is not None:
            server.close()
        if managed:
            managed.close()
        _remove_service_state(resolved_data, os.getpid())


def main(argv: list[str] | None = None, *, repo_root: Path | None = None) -> int:
    parser = argparse.ArgumentParser(description="Serve the InkTag WebUI and local data API.")
    parser.add_argument("--listen", default="127.0.0.1:8787")
    parser.add_argument("--port", type=int, help="Override only the local WebUI port.")
    parser.add_argument("--data-dir", type=Path)
    parser.add_argument("--web-root", type=Path)
    parser.add_argument("--tls-cert", type=Path)
    parser.add_argument("--tls-key", type=Path)
    parser.add_argument("--find-service")
    parser.add_argument("--find-data-dir", type=Path)
    parser.add_argument("--find-no-install", action="store_true")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument('--lan', action='store_true', help='Enable phone/LAN access with automatically prepared HTTPS.')
    parser.add_argument('--allowed-host', action='append', default=[], help='Trusted browser hostname/IP for wildcard LAN listening; no port.')
    parser.add_argument('--access-password-stdin', action='store_true', help='Read the HTTPS LAN access password from stdin instead of generating one for this run.')
    args = parser.parse_args(argv)
    root = (repo_root or Path(__file__).resolve().parents[3]).resolve()
    if args.lan:
        from .local_service import prepare_lan_https
        args.listen, args.tls_cert, args.tls_key, args.allowed_host = prepare_lan_https(
            args.listen, args.data_dir, args.tls_cert, args.tls_key, tuple(args.allowed_host))
    return run_local_server(
        repo_root=root, listen=args.listen, port=args.port, data_dir=args.data_dir, web_root=args.web_root,
        cert_path=args.tls_cert, key_path=args.tls_key, find_service=args.find_service,
        find_data_dir=args.find_data_dir,
        find_no_install=args.find_no_install, no_open=args.no_open,
        allowed_hosts=tuple(args.allowed_host),
        access_password=read_access_password() if args.access_password_stdin else None,
    )


if __name__ == "__main__":
    raise SystemExit(main())
