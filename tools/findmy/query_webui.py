#!/usr/bin/env python3
"""Serve the open-source, local-only InkTag Find report WebUI.

The helper persists a credential-scrubbed encrypted Apple session and a local
SQLite identity library. The account password is never serialized. Tag private
keys are never returned in plaintext; portable backups are passphrase-encrypted.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import ctypes
import hashlib
import ipaddress
import json
import logging
import math
import os
import re
import secrets
import socket
import sqlite3
import sys
import time
from datetime import UTC, datetime, timedelta
from pathlib import Path
import plistlib
from typing import Any
from urllib.parse import urlencode

LOGGER = logging.getLogger("inktag.find")

REPO_ROOT = Path(__file__).resolve().parents[2]
CLI_SOURCE = REPO_ROOT / "cli" / "src"
if str(CLI_SOURCE) not in sys.path:
    sys.path.insert(0, str(CLI_SOURCE))

from inktag_cli.find_query import (  # noqa: E402
    ROTATION_PERIOD_SECONDS,
    query_batch_delay_seconds,
    query_key_batches,
    slot_for_utc,
    slots_for_window,
)
from inktag_cli.find_history import (  # noqa: E402
    latest_report_timestamp,
    parse_report_timestamp,
)
from inktag_cli import find_public_only, owner_auth, find_backup  # noqa: E402

from aiohttp import (
    ClientError,
    ClientConnectorError,
    ClientSession,
    ClientTimeout,
    TCPConnector,
    web,
)
from aiohttp.abc import AbstractResolver
from aiohttp.resolver import DefaultResolver
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from findmy import (
    AsyncAppleAccount,
    AsyncSmsSecondFactor,
    AsyncTrustedDeviceSecondFactor,
    InvalidCredentialsError,
    KeyPair,
    LocalAnisetteProvider,
    LoginState,
    UnauthorizedError,
    UnhandledProtocolError,
)
from findmy.util.http import HttpSession
from findmy.errors import EmptyResponseError


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765
MAX_JSON_BYTES = 16 * 1024
MAX_SESSION_BYTES = 1024 * 1024
MAX_IDENTITIES = 256
DEFAULT_TRACKING_INTERVAL_MINUTES = 60
MIN_TRACKING_INTERVAL_MINUTES = 15
MAX_TRACKING_INTERVAL_MINUTES = 24 * 60
TRACKING_POLL_SECONDS = 30
FIND_LATEST_QUERY_LOOKBACK_SECONDS = 4 * 60 * 60
FIND_LATEST_QUERY_LOOKAHEAD_SECONDS = 60 * 60
FIND_RECENT_QUERY_LOOKBACK_SECONDS = 12 * 60 * 60
FIND_RECENT_QUERY_LOOKAHEAD_SECONDS = 2 * 60 * 60
ASSET_DIR = Path(__file__).resolve().parent
CSRF_HEADER = "X-InkTag-CSRF"
SESSION_MAGIC_DPAPI = b"INKTAG-FIND-SESSION-DPAPI\x01"
SESSION_MAGIC_AESGCM = b"INKTAG-FIND-SESSION-AESGCM\x01"
SESSION_ENTROPY = b"InkTag Find query session v1"
SAFE_ERROR_NAMES = {
    "EmptyResponseError": "Apple 返回了空响应，请稍后再试。",
    "InvalidCredentialsError": "Apple 拒绝了账号认证，具体原因尚未确定。请勿连续重复提交。",
    "UnauthorizedError": "查询会话已失效，请退出后重新登录。",
    "UnhandledProtocolError": "Apple 的非公开接口返回了不兼容响应。",
}
SESSION_EXPIRED_MESSAGE = "Apple 查询会话已失效，请重新登录查询账号。设备 Find 身份和本机历史记录无需重建。"
APPLE_REPORTS_HOST = "gateway.icloud.com"
SYNTHETIC_PROXY_NETWORK = ipaddress.ip_network("198.18.0.0/15")
APPLE_DOH_RESOLVERS = (
    "https://dns.google/resolve",
    "https://cloudflare-dns.com/dns-query",
)
TRACK_MAP_MAX_POINTS = 24
TRACK_MAP_MAX_BYTES = 5 * 1024 * 1024
TRACK_MAP_API_KEY_MAX_BYTES = 512
TRACK_HISTORY_WINDOW_HOURS = 24
TRACK_HISTORY_RETENTION_DAYS = 90


def _track_map_points(points: list[dict[str, Any]], limit: int = TRACK_MAP_MAX_POINTS) -> list[dict[str, Any]]:
    ordered = sorted(points, key=lambda point: str(point["timestamp"]))
    if len(ordered) <= limit:
        return ordered
    # Keep both endpoints and spread the remaining samples across the complete
    # time range. This bounds provider URL length without biasing the latest leg.
    indices = sorted({round(index * (len(ordered) - 1) / (limit - 1)) for index in range(limit)})
    return [ordered[index] for index in indices]


def _track_map_api_key(value: Any) -> str:
    api_key = str(value or "").strip()
    if not api_key:
        raise ValueError("请先在轨迹页填写所选地图服务的 API Key。")
    if len(api_key.encode("utf-8")) > TRACK_MAP_API_KEY_MAX_BYTES or any(
        ord(character) < 33 or ord(character) == 127 for character in api_key
    ):
        raise ValueError("地图 API Key 格式无效。")
    return api_key


def _outside_china(latitude: float, longitude: float) -> bool:
    return longitude < 72.004 or longitude > 137.8347 or latitude < 0.8293 or latitude > 55.8271


def _wgs84_to_gcj02(latitude: float, longitude: float) -> tuple[float, float]:
    """Convert WGS-84 reports for Amap's GCJ-02 static-map coordinates."""
    if _outside_china(latitude, longitude):
        return latitude, longitude
    x = longitude - 105.0
    y = latitude - 35.0
    latitude_delta = (
        -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y
        + 0.2 * math.sqrt(abs(x))
        + (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
        + (20.0 * math.sin(y * math.pi) + 40.0 * math.sin(y / 3.0 * math.pi)) * 2.0 / 3.0
        + (160.0 * math.sin(y / 12.0 * math.pi) + 320.0 * math.sin(y * math.pi / 30.0)) * 2.0 / 3.0
    )
    longitude_delta = (
        300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y
        + 0.1 * math.sqrt(abs(x))
        + (20.0 * math.sin(6.0 * x * math.pi) + 20.0 * math.sin(2.0 * x * math.pi)) * 2.0 / 3.0
        + (20.0 * math.sin(x * math.pi) + 40.0 * math.sin(x / 3.0 * math.pi)) * 2.0 / 3.0
        + (150.0 * math.sin(x / 12.0 * math.pi) + 300.0 * math.sin(x / 30.0 * math.pi)) * 2.0 / 3.0
    )
    rad_latitude = latitude / 180.0 * math.pi
    magic = 1.0 - 0.00669342162296594323 * math.sin(rad_latitude) ** 2
    sqrt_magic = math.sqrt(magic)
    latitude_delta = latitude_delta * 180.0 / (
        (6378245.0 * (1.0 - 0.00669342162296594323)) / (magic * sqrt_magic) * math.pi
    )
    longitude_delta = longitude_delta * 180.0 / (
        6378245.0 / sqrt_magic * math.cos(rad_latitude) * math.pi
    )
    return latitude + latitude_delta, longitude + longitude_delta


def _track_map_url(
    provider: str,
    points: list[dict[str, Any]],
    *,
    api_key: str,
) -> str:
    if not points:
        raise ValueError("还没有可绘制的轨迹点。")
    sampled = _track_map_points(points)
    if provider == "baidu":
        coordinates = [f'{float(point["longitude"]):.6f},{float(point["latitude"]):.6f}' for point in sampled]
        return "https://api.map.baidu.com/staticimage/v2?" + urlencode({
            "ak": api_key,
            "width": "900",
            "height": "500",
            "scale": "1",
            "coordtype": "wgs84ll",
            "paths": ";".join(coordinates),
            "pathStyles": "0x238b6f,5,0.9",
            "markers": f"{coordinates[0]}|{coordinates[-1]}",
            "markerStyles": "s,A|s,Z",
        })
    if provider == "amap":
        coordinates = []
        for point in sampled:
            latitude, longitude = _wgs84_to_gcj02(float(point["latitude"]), float(point["longitude"]))
            coordinates.append(f"{longitude:.6f},{latitude:.6f}")
        return "https://restapi.amap.com/v3/staticmap?" + urlencode({
            "key": api_key,
            "size": "900*500",
            "scale": "1",
            "paths": f"5,0x238b6f,0.9,,:{';'.join(coordinates)}",
            "markers": f"small,0x17201c,A:{coordinates[0]}|small,0xbd4138,Z:{coordinates[-1]}",
        })
    raise ValueError("不支持的轨迹地图服务。")


class _AppleGatewayResolver(AbstractResolver):
    """Bypass a stale proxy fake-IP only for Apple's report gateway.

    Some local TUN proxies synthesize RFC 2544 addresses for DNS interception.
    If that mapping stops forwarding TLS, the Apple session is still valid but
    report queries fail at connect time. Resolve only the affected hostname
    through DNS-over-HTTPS in that case; credentials and report data never
    leave the normal Apple HTTPS request.
    """

    def __init__(self) -> None:
        self._default = DefaultResolver()
        self._gateway_addresses: list[str] = []

    async def resolve(
        self, host: str, port: int = 0, family: int = socket.AF_INET
    ) -> list[dict[str, Any]]:
        records = await self._default.resolve(host, port, family)
        if host != APPLE_REPORTS_HOST:
            return records

        addresses = [str(record["host"]) for record in records]
        if addresses and not all(
            ipaddress.ip_address(address) in SYNTHETIC_PROXY_NETWORK
            for address in addresses
        ):
            return records

        if not self._gateway_addresses:
            self._gateway_addresses = await self._resolve_gateway_doh()
        if not self._gateway_addresses:
            return records
        return [
            {
                "hostname": host,
                "host": address,
                "port": port,
                "family": socket.AF_INET,
                "proto": socket.IPPROTO_TCP,
                "flags": socket.AI_NUMERICHOST,
            }
            for address in self._gateway_addresses
        ]

    async def _resolve_gateway_doh(self) -> list[str]:
        for endpoint in APPLE_DOH_RESOLVERS:
            try:
                async with ClientSession(timeout=ClientTimeout(total=4)) as session:
                    async with session.get(
                        endpoint,
                        params={"name": APPLE_REPORTS_HOST, "type": "A"},
                        headers={"Accept": "application/dns-json"},
                    ) as response:
                        payload = await response.json(content_type=None)
                addresses = []
                for answer in payload.get("Answer", []):
                    value = str(answer.get("data", ""))
                    try:
                        parsed = ipaddress.ip_address(value)
                    except ValueError:
                        continue
                    if parsed.version == 4 and parsed not in SYNTHETIC_PROXY_NETWORK:
                        addresses.append(value)
                if addresses:
                    return list(dict.fromkeys(addresses))
            except Exception:
                continue
        return []

    async def close(self) -> None:
        await self._default.close()


def _install_apple_gateway_resolver(account: AsyncAppleAccount) -> None:
    """Install the narrow resolver before FindMy.py creates its HTTP session."""

    http = getattr(account, "_http", None)
    if http is None or getattr(http, "_session", None) is not None:
        return
    http._session = ClientSession(  # type: ignore[attr-defined]  # noqa: SLF001
        timeout=ClientTimeout(total=5),
        connector=TCPConnector(resolver=_AppleGatewayResolver()),
    )


class _DataBlob(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_ulong),
        ("data", ctypes.POINTER(ctypes.c_ubyte)),
    ]


def _blob(value: bytes) -> tuple[_DataBlob, Any]:
    buffer = ctypes.create_string_buffer(value)
    return (
        _DataBlob(len(value), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte))),
        buffer,
    )


def _dpapi_transform(data: bytes, *, protect: bool) -> bytes:
    if os.name != "nt":
        raise RuntimeError("DPAPI 只可在 Windows 上使用。")
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    input_blob, input_buffer = _blob(data)
    entropy_blob, entropy_buffer = _blob(SESSION_ENTROPY)
    output_blob = _DataBlob()
    flags = 0x01  # CRYPTPROTECT_UI_FORBIDDEN
    if protect:
        ok = crypt32.CryptProtectData(
            ctypes.byref(input_blob),
            "InkTag Find query session",
            ctypes.byref(entropy_blob),
            None,
            None,
            flags,
            ctypes.byref(output_blob),
        )
    else:
        ok = crypt32.CryptUnprotectData(
            ctypes.byref(input_blob),
            None,
            ctypes.byref(entropy_blob),
            None,
            None,
            flags,
            ctypes.byref(output_blob),
        )
    _ = (input_buffer, entropy_buffer)
    if not ok:
        raise ctypes.WinError()
    try:
        return ctypes.string_at(output_blob.data, output_blob.size)
    finally:
        kernel32.LocalFree(output_blob.data)


def _load_or_create_file_key(path: Path) -> bytes:
    if path.is_file():
        key = path.read_bytes()
        if len(key) != 32:
            raise ValueError("本机会话主密钥格式无效。")
        return key
    key = AESGCM.generate_key(bit_length=256)
    _atomic_write(path, key)
    return key


def _aesgcm_protect(data: bytes, file_key_path: Path) -> bytes:
    key = _load_or_create_file_key(file_key_path)
    nonce = secrets.token_bytes(12)
    encrypted = AESGCM(key).encrypt(nonce, data, SESSION_ENTROPY)
    return SESSION_MAGIC_AESGCM + nonce + encrypted


def _aesgcm_unprotect(data: bytes, file_key_path: Path) -> bytes:
    payload = data[len(SESSION_MAGIC_AESGCM) :]
    if len(payload) < 12 + 16:
        raise ValueError("本机会话密文无效。")
    # A missing decryption key is an error, never a reason to create one.
    key = file_key_path.read_bytes()
    if len(key) != 32:
        raise ValueError("本机会话主密钥格式无效。")
    return AESGCM(key).decrypt(payload[:12], payload[12:], SESSION_ENTROPY)


def _protect_session(data: bytes, file_key_path: Path) -> bytes:
    if os.name == "nt":
        return SESSION_MAGIC_DPAPI + _dpapi_transform(data, protect=True)
    return _aesgcm_protect(data, file_key_path)


def _unprotect_session(data: bytes, file_key_path: Path) -> bytes:
    if data.startswith(SESSION_MAGIC_DPAPI):
        if os.name != "nt":
            raise ValueError("DPAPI 会话只能由原 Windows 用户恢复。")
        return _dpapi_transform(data[len(SESSION_MAGIC_DPAPI) :], protect=False)
    if data.startswith(SESSION_MAGIC_AESGCM):
        return _aesgcm_unprotect(data, file_key_path)
    raise ValueError("本机会话封装格式无效。")


def _key_fingerprint(key_pair: KeyPair) -> str:
    return hashlib.sha256(key_pair.adv_key_bytes).hexdigest()[:16]


def _utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="seconds")


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".new")
    temporary.write_bytes(data)
    try:
        temporary.chmod(0o600)
    except OSError:
        pass
    os.replace(temporary, path)


def _find_address(advertisement_key: bytes) -> str:
    address = bytes((advertisement_key[0] | 0xC0,)) + advertisement_key[1:6]
    return ":".join(f"{value:02X}" for value in address)


class AppleSessionExpiredError(PermissionError):
    """Apple rejected the saved session; no password is retained for reauth."""


class AppleLoginRejectedError(PermissionError):
    """A sanitized GSA rejection, not a claim that the password is incorrect."""


FIND_QUERY_UNAVAILABLE_MESSAGE = (
    "Apple 账号验证已完成，但未签发 Find 位置查询令牌，当前会话无法查询位置。"
    "这是认证后端兼容性问题，不表示密码或验证码错误；请勿反复提交验证码。"
    "已登记的标签、密钥和本机历史均保留。"
)


class AppleFindUnavailableError(PermissionError):
    """Account authentication is not proof of Find service authorization."""


class AppleFindEmptyResponseError(RuntimeError):
    """An abnormal cloud response is not evidence that a tag has no reports."""


def _find_query_ready(account: Any) -> bool:
    if account is None or account.login_state != LoginState.LOGGED_IN:
        return False
    data = getattr(account, "_login_state_data", None)
    if not isinstance(data, dict) or not data.get("dsid"):
        return False
    mobileme = data.get("mobileme_data")
    tokens = mobileme.get("tokens") if isinstance(mobileme, dict) else None
    token = tokens.get("searchPartyToken") if isinstance(tokens, dict) else None
    return isinstance(token, str) and bool(token.strip())


class AppleAuthenticationTimeout(TimeoutError):
    """Only a fixed phase description leaves the backend, never request data."""


APPLE_AUTH_CLIENT = "<PC> <Windows;6.2(0,0);9200> <com.apple.AuthKit/1 (com.apple.akd/1.0)>"
APPLE_MOBILEME_CLIENT = "<PC> <Windows;6.2(0,0);9200> <com.apple.AOSKit/282 (com.apple.accountsd/113)>"


class _AppleAuthenticationHttp(HttpSession):
    """Endpoint-scoped compatibility; retain FindMy's verified TLS transport."""

    def __init__(self, *, auth_client: str = APPLE_AUTH_CLIENT,
                 mobileme_client: str = APPLE_MOBILEME_CLIENT) -> None:
        super().__init__()
        self.auth_client = auth_client
        self.mobileme_client = mobileme_client

    async def request(self, method: str, url: str, **kwargs: Any) -> Any:
        auth_urls = (
            AsyncAppleAccount._ENDPOINT_GSA,
            AsyncAppleAccount._ENDPOINT_2FA_METHODS,
            AsyncAppleAccount._ENDPOINT_2FA_SMS_REQUEST,
            AsyncAppleAccount._ENDPOINT_2FA_SMS_SUBMIT,
            AsyncAppleAccount._ENDPOINT_2FA_TD_REQUEST,
            AsyncAppleAccount._ENDPOINT_2FA_TD_SUBMIT,
        )
        phase = None
        if url in auth_urls or url == AsyncAppleAccount._ENDPOINT_LOGIN_MOBILEME:
            # Do not change report requests or mutate the caller's headers.
            headers = {key: value for key, value in kwargs.get("headers", {}).items()
                       if key.lower() not in ("x-mme-client-info", "connection")}
            headers["X-MMe-Client-Info"] = (
                self.mobileme_client if url == AsyncAppleAccount._ENDPOINT_LOGIN_MOBILEME
                else self.auth_client
            )
            headers["Connection"] = "close"
            kwargs.update(headers=headers, auto_retry=False, allow_redirects=False,
                          timeout=ClientTimeout(total=20, connect=8, sock_read=15))
            phase = {
                AsyncAppleAccount._ENDPOINT_2FA_METHODS: "获取短信验证方式",
                AsyncAppleAccount._ENDPOINT_2FA_SMS_REQUEST: "发送短信验证码",
                AsyncAppleAccount._ENDPOINT_2FA_SMS_SUBMIT: "验证短信验证码",
                AsyncAppleAccount._ENDPOINT_2FA_TD_REQUEST: "发送受信任设备验证码",
                AsyncAppleAccount._ENDPOINT_2FA_TD_SUBMIT: "验证受信任设备验证码",
                AsyncAppleAccount._ENDPOINT_LOGIN_MOBILEME: "建立 Find 查询会话",
            }.get(url, "Apple 账号认证")
            if url == AsyncAppleAccount._ENDPOINT_GSA:
                operation = plistlib.loads(kwargs["data"]).get("Request", {}).get("o")
                phase = {"init": "账号校验", "complete": "密码校验"}.get(operation, phase)
        started = time.monotonic()
        try:
            response = await super().request(method, url, **kwargs)
        except TimeoutError:
            if phase is None:
                raise
            LOGGER.warning("Apple auth timeout phase=%s elapsed_ms=%d", phase, int((time.monotonic()-started)*1000))
            raise AppleAuthenticationTimeout(f"{phase}超时，操作结果尚未确认。请重新登录后再试，不要连续重复提交验证码。") from None
        if phase:
            LOGGER.info("Apple auth response phase=%s http=%s elapsed_ms=%d", phase,
                        response.status_code, int((time.monotonic()-started)*1000))
        return response


class QueryAppleAccount(AsyncAppleAccount):
    def __init__(self, anisette: Any, *, state_info: Any = None) -> None:
        super().__init__(anisette, state_info=state_info)
        self._http = _AppleAuthenticationHttp()
        self.trusted_code_already_sent = False
        self.second_factor_warning = None
        self._machine_resync_attempts = 0
        self._srp_protocol = "unknown"

    async def fetch_raw_reports(self, *args: Any, **kwargs: Any) -> Any:
        if not _find_query_ready(self):
            raise AppleFindUnavailableError(FIND_QUERY_UNAVAILABLE_MESSAGE)
        try:
            return await super().fetch_raw_reports(*args, **kwargs)
        except EmptyResponseError:
            # The dependency's history fetcher otherwise turns this transport
            # failure into [], indistinguishable from a valid zero-report reply.
            raise AppleFindEmptyResponseError(
                "Apple 位置查询返回了异常空响应，不能判断为没有轨迹。"
                "请稍后重试；无需重新登记标签或删除现有会话。"
            ) from None

    async def get_2fa_methods(self) -> Any:
        self.second_factor_warning = None
        try:
            return await super().get_2fa_methods()
        except (UnhandledProtocolError, ClientError, TimeoutError, ValueError) as error:
            if not (self.trusted_code_already_sent and self.login_state == LoginState.REQUIRE_2FA):
                raise
            # Keep the independently established trusted-device challenge, but
            # do not invent a phone number when Apple's discovery fails.
            self.second_factor_warning = "短信验证方式获取失败，仍可使用受信任设备上的验证码。"
            if isinstance(error, UnhandledProtocolError) and str(error) == "SMS 2FA request failed: 403":
                self.second_factor_warning = "Apple 拒绝了短信验证方式请求（HTTP 403）。仍可使用受信任设备上的验证码；本工具无法强制开启短信验证。"
            LOGGER.warning("Apple SMS method discovery failed; retaining pending trusted-device challenge")
            return [AsyncTrustedDeviceSecondFactor(self)]

    async def td_2fa_request(self) -> None:
        if self.trusted_code_already_sent and self.login_state == LoginState.REQUIRE_2FA:
            self.trusted_code_already_sent = False
            return
        await super().td_2fa_request()

    async def _gsa_request(self, parameters: dict[str, Any]) -> dict[str, Any]:
        if parameters.get("o") == "init":
            self._srp_protocol = "unknown"
        response = await super()._gsa_request(parameters)
        status = response.get("Status", {})
        # A numeric -22421 alone is not a resync request. Only answer Apple's
        # explicit 434 + SIM challenge, on the existing Windows machine. Replay
        # at most this refused step once; SRP/M2/SPD validation remains upstream.
        if (type(status.get("ec")) is int and status["ec"] == -22421
                and type(status.get("hsc")) is int and status["hsc"] == 434):
            challenge = status.get("X-Apple-I-MD-DATA")
            provider = self._anisette
            if (getattr(provider, "backend", None) == "win-emulated"
                    and isinstance(challenge, str) and 0 < len(challenge) <= 16384
                    and parameters.get("o") in {"init", "complete"}
                    and self._machine_resync_attempts < 2):
                self._machine_resync_attempts += 1
                LOGGER.info("Apple machine resync requested phase=%s", parameters["o"])
                try:
                    await provider.resynchronize(challenge)
                except Exception as error:
                    if getattr(error, "code", None) == "throttled":
                        raise AppleLoginRejectedError(
                            "Apple 已限制本机认证请求（HTTP 429）。请停止尝试，至少等待45分钟；"
                            "不会自动重试或重建机器状态。") from None
                    raise AppleLoginRejectedError(
                        "Apple 要求重同步本机认证状态（-22421 / 434），但重同步未完成。"
                        "请至少等待一分钟再试；不要删除机器状态或连续提交密码。") from None
                response = await super()._gsa_request(parameters)
                status = response.get("Status", {})
                LOGGER.info("Apple machine resync completed; refused step retried once")
        code = status.get("ec")
        if code == 0:
            if parameters.get("o") == "init":
                protocol = response.get("sp")
                # Fixed public protocol names only: never log the challenge,
                # salt, proof, account, password or arbitrary server content.
                self._srp_protocol = protocol if isinstance(protocol, str) and protocol in {"s2k", "s2k_fo"} else "unknown"
                LOGGER.info("Apple auth challenge protocol=%s", self._srp_protocol)
            if parameters.get("o") == "complete":
                self.trusted_code_already_sent = status.get("hsc") == 409 and status.get("au") is None
                if self.trusted_code_already_sent:
                    # Normalize only the verified-success challenge signal.
                    # The upstream SRP/M2 and SPD validation still run before
                    # REQUIRE_2FA, and never turn it into LOGGED_IN.
                    return {**response, "Status": {**status, "au": "trustedDeviceSecondaryAuth"}}
            return response
        phase = {"init": "账号校验", "complete": "密码校验"}.get(parameters.get("o"), "账号认证")
        # Only a bounded numeric code and fixed classifications may leave the
        # backend. Never expose the raw Apple body, account, proof or password.
        numeric_code = code if type(code) is int and -(2**31) <= code < 2**31 else None
        detail = str(status.get("em", "")).lower()
        if "locked" in detail or "disabled" in detail:
            reason, guidance = "restricted", "Apple 报告账号受限，请通过 Apple 官方账号页面检查状态。"
        elif "temporar" in detail or "try again later" in detail:
            reason, guidance = "temporary", "Apple 要求稍后再试，请勿连续重复提交。"
        elif numeric_code == -22406:
            reason = "credentials"
            guidance = ("账号或密码校验未通过，请先核对 Apple 账号和正常登录密码（不要使用 App 专用密码）。"
                        "若同一账号可在 Apple 官方网站登录，仍需排查客户端认证兼容性。")
        elif "incorrect" in detail or "invalid password" in detail:
            reason, guidance = "credentials", "Apple 报告账号或密码不匹配；请核对正常登录密码，而不是 App 专用密码。"
        else:
            reason, guidance = "unspecified", "Apple 未提供可安全展示的明确原因，请勿直接认定密码错误。"
        hsc = status.get("hsc")
        safe_hsc = hsc if type(hsc) is int and 0 <= hsc <= 999 else None
        if numeric_code == -22421:
            reason = "machine-auth"
            guidance = ("Apple 未接受本机认证环境，不代表密码错误。"
                        "请勿连续重试或删除机器状态；需检查认证数据和重同步结果。")
        LOGGER.warning("Apple login rejected phase=%s code=%s hsc=%s reason=%s protocol=%s",
                       phase, numeric_code, safe_hsc, reason, self._srp_protocol)
        suffix = f"，Apple 错误码 {numeric_code}" if numeric_code is not None else ""
        raise AppleLoginRejectedError(f"Apple 在{phase}阶段拒绝认证{suffix}。{guidance}登录尚未完成。")


def _expired_apple_session(error: Exception) -> bool:
    if isinstance(error, UnauthorizedError):
        return True
    # FindMy.py attempts password reauthentication after HTTP 401.
    # Our password-free session cannot do that. Do not classify unrelated
    # ValueErrors (or expose their potentially confidential text) as expiry.
    if not isinstance(error, ValueError) or str(error) != "No username or password specified":
        return False
    frame = error.__traceback__
    while frame is not None:
        if (frame.tb_frame.f_globals.get("__name__") == "findmy.reports.account"
                and frame.tb_frame.f_code.co_name == "_gsa_authenticate"):
            return True
        frame = frame.tb_next
    return False


def _safe_error(error: Exception) -> str:
    if type(error).__name__ == "BackendVerificationError":
        if getattr(error, "code", None) == "throttled":
            return "Apple 已限制本机认证请求（HTTP 429）。请停止尝试，至少等待45分钟；不要删除机器状态。"
        if getattr(error, "code", None) == "resync-cooldown":
            return "本机认证状态刚尝试过重同步，请至少等待一分钟再登录；不要删除机器状态。"
        return "本机 Apple 认证组件不可用或机器状态不匹配。请检查本地认证环境；不要删除机器状态或连续提交密码。"
    if isinstance(error, AppleFindUnavailableError):
        return FIND_QUERY_UNAVAILABLE_MESSAGE
    if isinstance(error, AppleFindEmptyResponseError):
        return str(error)
    if isinstance(error, AppleAuthenticationTimeout):
        return str(error)
    if isinstance(error, TimeoutError):
        return "Apple 请求超时，操作结果尚未确认。请重新登录后再试，不要连续重复提交验证码。"
    if isinstance(error, AppleLoginRejectedError):
        return str(error)
    if isinstance(error, AppleSessionExpiredError):
        return SESSION_EXPIRED_MESSAGE
    if isinstance(error, KeyError):
        # Only fixed protocol field names may be reflected. An arbitrary key
        # can contain private account data, a report ID or even a whole body.
        field = error.args[0] if len(error.args) == 1 and isinstance(error.args[0], str) else None
        if field in {"searchPartyToken", "mobileme_data", "tokens"}:
            return FIND_QUERY_UNAVAILABLE_MESSAGE
        if field in {"dsid", "service-data", "delegates"}:
            return f"Apple 返回的 Find 会话不完整（缺少 {field}）。账号验证可能已通过，但查询会话尚未建立。"
        if field in {"adsid", "GsIdmsToken", "idms_token", "idms_pet", "Status", "Response", "M2", "spd", "s", "B", "i", "c", "sp"}:
            return f"Apple 认证响应缺少必要字段（{field}），当前认证流程未完成。这不表示账号密码或验证码错误。"
        return "返回数据缺少必要字段，服务未能完成此操作。当前无法确认具体缺失项，不应据此认定密码或验证码错误。"
    if isinstance(error, ClientConnectorError):
        return "无法连接 Apple 服务。请检查当前网络或代理后重试。"
    if isinstance(error, InvalidCredentialsError):
        return SAFE_ERROR_NAMES["InvalidCredentialsError"]
    if isinstance(error, UnauthorizedError):
        return SAFE_ERROR_NAMES["UnauthorizedError"]
    if isinstance(error, UnhandledProtocolError):
        detail = str(error)
        match = re.fullmatch(r"SMS 2FA request failed: ([0-9]{3})", detail)
        if match:
            return f"Apple 拒绝了双重认证请求（HTTP {match.group(1)}）。请重新获取验证码后再试。"
        match = re.fullmatch(r"Error response for GSA request: ([0-9]{3})", detail)
        if match:
            return f"Apple 账号认证请求失败（HTTP {match.group(1)}），尚未完成登录。这不表示验证码错误；请检查查询服务版本和网络后再试。"
        match = re.fullmatch(
            r"Unexpected state after submitting 2FA: LoginState\.([A-Z0-9_]+)",
            detail,
        )
        if match:
            return f"Apple 已处理验证码，但账号状态仍为 {match.group(1)}，未完成登录。"
        match = re.fullmatch(
            r"com\.apple\.mobileme login failed with status ([0-9]+): (.*)",
            detail,
        )
        if match:
            suffix = "；Apple 报告账号设备/会话数量已达限制" if match.group(2) == "Account limit reached." else ""
            return f"双重认证已通过，但 Apple Find 服务登录失败（状态 {match.group(1)}{suffix}）。"
        if re.fullmatch(r"Unknown auth value: [A-Za-z0-9_-]+", detail):
            return "验证码提交后的 Apple 账号状态不受当前查询工具支持。"
        return SAFE_ERROR_NAMES["UnhandledProtocolError"]
    return SAFE_ERROR_NAMES.get(type(error).__name__, "本地查询服务遇到未识别的处理错误，操作未完成。请保留失败步骤供排查；不要连续重复提交账号密码或验证码。")


class QuerySession:
    """Own one encrypted Apple session and a persistent local Find identity library."""

    def __init__(
        self,
        anisette_libs_path: Path,
        session_store_path: Path | None = None,
        *, auth_backend: str = "local", auth_state_dir: Path | None = None,
        auth_cache_dir: Path | None = None,
    ) -> None:
        if auth_backend not in {"local", "win-emulated", "mac-native"}:
            raise ValueError("Unsupported Apple authentication backend")
        self.auth_backend = auth_backend
        self.auth_state_dir = auth_state_dir or anisette_libs_path.parent / "authentication" / auth_backend
        self.auth_cache_dir = auth_cache_dir or anisette_libs_path.parent / "runtime/mac-adi"
        self.anisette_libs_path = anisette_libs_path
        self.anisette_libs_path.parent.mkdir(parents=True, exist_ok=True)
        self.session_store_path = session_store_path
        self.session_file_key_path = (
            session_store_path.with_suffix(session_store_path.suffix + ".key")
            if session_store_path is not None
            else self.anisette_libs_path.parent / "account-session.key"
        )
        self.identity_database_path = self.anisette_libs_path.parent / "find-identities.sqlite3"
        self.identity_database = sqlite3.connect(self.identity_database_path)
        try:
            self.identity_database_path.chmod(0o600)
        except OSError:
            pass
        self.identities: dict[str, dict[str, Any]] = {}
        self.identity_seeds: dict[str, bytes] = {}
        self.selected_key_fingerprint = ""
        self.tracking_task: asyncio.Task[None] | None = None
        self._initialize_identity_database()
        self.next_track_history_prune_at = datetime.now(UTC) + timedelta(hours=1)
        self._load_identity_registry()
        self._migrate_history_tracking()
        self.tracking_interval_minutes = self._load_tracking_interval()
        self.apple_account_username = self._load_apple_account_username()
        selected_row = self.identity_database.execute(
            "SELECT value FROM find_settings WHERE name='selectedKeyFingerprint'"
        ).fetchone()
        requested = str(selected_row[0]).lower() if selected_row else ""
        if requested in self.identities:
            self._select_identity(requested)
        elif self.identities:
            self._select_identity(next(iter(self.identities)))
        self._save_identity_registry()
        self.session_restored = False
        self.session_error: str | None = None
        self.account: AsyncAppleAccount | None = None
        self.second_factors: list[Any] = []
        self.requested_second_factor_index: int | None = None
        self.lock = asyncio.Lock()

    @property
    def public_metadata(self) -> dict[str, str]:
        if (not self.selected_key_fingerprint or
                self.identities[self.selected_key_fingerprint].get("historyOnly") or
                self.identities[self.selected_key_fingerprint]["derivationVersion"] != find_public_only.VERSION):
            return {"address": "", "keyFingerprint": "", "advertisementKey": ""}
        pair = self._pair_for_slot(self.selected_key_fingerprint, slot_for_utc(int(time.time())))
        adv_key = pair.adv_key_bytes
        return {
            "address": _find_address(adv_key),
            "keyFingerprint": hashlib.sha256(adv_key).hexdigest()[:16],
            # Public by protocol design: the tag broadcasts this material in
            # its address/manufacturer frame. The private P-224 key remains
            # confined to the local service and is never returned here.
            "advertisementKey": adv_key.hex().upper(),
        }

    def _protect_seed(self, seed: bytes) -> bytes:
        return _protect_session(seed, self.session_file_key_path)

    def _unprotect_seed(self, stored: bytes) -> bytes:
        if stored.startswith((SESSION_MAGIC_DPAPI, SESSION_MAGIC_AESGCM)):
            return _unprotect_session(stored, self.session_file_key_path)
        raise ValueError("Find 身份库包含未加密的派生种子。")

    def _pair_for_slot(self, fingerprint: str, slot: int) -> KeyPair:
        if self.identities[fingerprint].get("historyOnly"):
            raise ValueError("此设备仅有历史轨迹，请导入设备资料和查询密钥后再查询。")
        if self.identities[fingerprint]["derivationVersion"] != find_public_only.VERSION:
            raise ValueError("旧版 Find 身份仅保留本机历史；请使用 Find v2 身份。")
        material = self.identity_seeds[fingerprint]
        scalar = find_public_only.derive_private_scalar(
            int.from_bytes(material[:28], "big"), material[28:], slot
        )
        return KeyPair(scalar.to_bytes(28, "big"))

    @staticmethod
    def _clean_identity_name(value: Any, field: str, *, allow_empty: bool = True) -> str:
        name = str(value or "").strip()
        if not name and allow_empty:
            return ""
        if not name or len(name) > 64 or any(ord(character) < 32 for character in name):
            raise ValueError(f"{field}必须是 1–64 个可见字符。")
        return name

    def _initialize_identity_database(self) -> None:
        existing_columns = {
            str(row[1])
            for row in self.identity_database.execute("PRAGMA table_info(find_identities)")
        }
        if "private_key_pem" in existing_columns:
            # No public release used the obsolete single-key/keyring schema.
            # Rebuild the identity/track tables instead of carrying a secret
            # format migration path. Account sessions and general settings are
            # stored separately and remain intact.
            with self.identity_database:
                self.identity_database.execute("DROP TABLE IF EXISTS find_track_points")
                self.identity_database.execute("DROP TABLE IF EXISTS find_identity_keys")
                self.identity_database.execute("DROP TABLE IF EXISTS find_identities")
        self.identity_database.executescript(
            """
            PRAGMA journal_mode=WAL;
            PRAGMA synchronous=FULL;
            PRAGMA secure_delete=ON;
            CREATE TABLE IF NOT EXISTS find_identities (
                key_fingerprint TEXT PRIMARY KEY,
                label TEXT NOT NULL,
                device_id TEXT NOT NULL DEFAULT '',
                device_name TEXT NOT NULL DEFAULT '',
                bluetooth_name TEXT NOT NULL DEFAULT '',
                find_note TEXT NOT NULL DEFAULT '',
                tracking_enabled INTEGER NOT NULL DEFAULT 1,
                last_auto_query_at TEXT,
                last_auto_error TEXT,
                derivation_version INTEGER NOT NULL,
                derivation_seed BLOB NOT NULL,
                created_at TEXT NOT NULL,
                last_queried_at TEXT,
                last_report_at TEXT
            );
            CREATE TABLE IF NOT EXISTS find_settings (
                name TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS find_track_points (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                key_fingerprint TEXT NOT NULL,
                report_timestamp TEXT NOT NULL,
                received_at TEXT NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                horizontal_accuracy REAL NOT NULL,
                confidence INTEGER NOT NULL,
                status INTEGER NOT NULL,
                source_slot INTEGER NOT NULL DEFAULT -1,
                UNIQUE(key_fingerprint, report_timestamp, latitude, longitude)
            );
            CREATE INDEX IF NOT EXISTS find_track_points_identity_time
                ON find_track_points(key_fingerprint, report_timestamp DESC);
            CREATE INDEX IF NOT EXISTS find_track_points_received_time
                ON find_track_points(received_at);
            CREATE TABLE IF NOT EXISTS find_owner_rotations (
                rotation_id TEXT PRIMARY KEY,
                current_fingerprint TEXT NOT NULL,
                replacement_fingerprint TEXT NOT NULL,
                protected_replacement_seed BLOB NOT NULL,
                nonce BLOB NOT NULL,
                created_at TEXT NOT NULL,
                prepared_metadata TEXT NOT NULL DEFAULT '{}'
            );
            """
        )
        columns = {
            str(row[1])
            for row in self.identity_database.execute("PRAGMA table_info(find_identities)")
        }
        missing_columns = {
            "device_id": "TEXT NOT NULL DEFAULT ''",
            "device_name": "TEXT NOT NULL DEFAULT ''",
            "bluetooth_name": "TEXT NOT NULL DEFAULT ''",
            "find_note": "TEXT NOT NULL DEFAULT ''",
            "tracking_enabled": "INTEGER NOT NULL DEFAULT 1",
            "last_auto_query_at": "TEXT",
            "last_auto_error": "TEXT",
            "derivation_version": "INTEGER NOT NULL DEFAULT 0",
            "derivation_seed": "BLOB",
            "period_seconds": f"INTEGER NOT NULL DEFAULT {ROTATION_PERIOD_SECONDS}",
            "history_only": "INTEGER NOT NULL DEFAULT 0",
        }
        for name, declaration in missing_columns.items():
            if name not in columns:
                self.identity_database.execute(
                    f"ALTER TABLE find_identities ADD COLUMN {name} {declaration}"
                )
        rotation_columns = {
            str(row[1]) for row in self.identity_database.execute("PRAGMA table_info(find_owner_rotations)")
        }
        if "prepared_metadata" not in rotation_columns:
            self.identity_database.execute(
                "ALTER TABLE find_owner_rotations ADD COLUMN prepared_metadata TEXT NOT NULL DEFAULT '{}'"
            )
        track_columns = {
            str(row[1])
            for row in self.identity_database.execute("PRAGMA table_info(find_track_points)")
        }
        if "source_slot" not in track_columns:
            self.identity_database.execute(
                "ALTER TABLE find_track_points ADD COLUMN "
                "source_slot INTEGER NOT NULL DEFAULT -1"
            )
        self.identity_database.execute(
            "UPDATE find_identities SET find_note=label WHERE find_note=''"
        )
        self.identity_database.execute("DROP TABLE IF EXISTS find_identity_keys")
        self.identity_database.execute(
            "DELETE FROM find_settings WHERE name='selectedKeyFingerprint' "
            "AND value NOT IN (SELECT key_fingerprint FROM find_identities)"
        )
        self.identity_database.commit()
        # Pending owner rotations contain the only encrypted-at-rest copy of a
        # replacement seed until the device proof is finalized.  Never discard
        # them merely because the service restarted: the BLE commit reply can
        # be lost after the device has already switched identities.
        self._prune_track_history()
        self._secure_identity_database_files()

    def _load_tracking_interval(self) -> int:
        row = self.identity_database.execute(
            "SELECT value FROM find_settings WHERE name='trackingIntervalMinutes'"
        ).fetchone()
        try:
            interval = int(row[0]) if row is not None else DEFAULT_TRACKING_INTERVAL_MINUTES
        except (TypeError, ValueError):
            interval = DEFAULT_TRACKING_INTERVAL_MINUTES
        return min(MAX_TRACKING_INTERVAL_MINUTES, max(MIN_TRACKING_INTERVAL_MINUTES, interval))

    def _load_apple_account_username(self) -> str:
        row = self.identity_database.execute(
            "SELECT value FROM find_settings WHERE name='appleAccountUsername'"
        ).fetchone()
        value = str(row[0]).strip() if row is not None else ""
        return value if len(value) <= 254 and "@" in value else ""

    def _save_apple_account_username(self, username: str) -> None:
        normalized = username.strip()
        if not normalized or len(normalized) > 254 or "@" not in normalized:
            return
        with self.identity_database:
            self.identity_database.execute(
                """
                INSERT INTO find_settings(name, value) VALUES ('appleAccountUsername', ?)
                ON CONFLICT(name) DO UPDATE SET value=excluded.value
                """,
                (normalized,),
            )
        self.apple_account_username = normalized
        self._secure_identity_database_files()

    def _secure_identity_database_files(self) -> None:
        for suffix in ("", "-wal", "-shm"):
            path = Path(str(self.identity_database_path) + suffix)
            if not path.exists():
                continue
            try:
                path.chmod(0o600)
            except OSError:
                pass

    def _prune_track_history(self) -> int:
        cutoff = (datetime.now(UTC) - timedelta(days=TRACK_HISTORY_RETENTION_DAYS)).isoformat()
        changes_before = self.identity_database.total_changes
        with self.identity_database:
            self.identity_database.execute(
                "DELETE FROM find_track_points WHERE received_at < ?",
                (cutoff,),
            )
        return self.identity_database.total_changes - changes_before

    def _load_identity_registry(self) -> None:
        rows = self.identity_database.execute(
            """
            SELECT key_fingerprint, label, device_id, device_name, bluetooth_name, find_note,
                   tracking_enabled, last_auto_query_at, last_auto_error,
                   derivation_seed, created_at, last_queried_at, last_report_at,
                   derivation_version, period_seconds, history_only
              FROM find_identities
             ORDER BY created_at
            """
        ).fetchall()
        if len(rows) > MAX_IDENTITIES:
            raise ValueError("Find 身份库超过 256 条上限。")
        for (
            fingerprint_value,
            legacy_label,
            device_id,
            device_name,
            bluetooth_name,
            find_note,
            tracking_enabled,
            last_auto_query_at,
            last_auto_error,
            derivation_seed,
            created_at,
            last_queried_at,
            last_report_at,
            derivation_version,
            period_seconds,
            history_only,
        ) in rows:
            fingerprint = str(fingerprint_value).lower()
            if not re.fullmatch(r"(?:[0-9a-f]{16}|[0-9a-f]{32})", fingerprint):
                raise ValueError("Find 身份库包含无效指纹。")
            if history_only and bytes(derivation_seed):
                raise ValueError("仅轨迹资料包含意外的身份密钥。")
            seed = b'' if history_only else self._unprotect_seed(bytes(derivation_seed))
            version = int(derivation_version)
            if history_only:
                if version != find_public_only.VERSION or len(fingerprint) != 32:
                    raise ValueError("仅轨迹资料的身份格式无效。")
            elif version == find_public_only.VERSION:
                if len(seed) != 44 or find_public_only.identity_fingerprint(
                    find_public_only.master_public_bytes(int.from_bytes(seed[:28], "big")),
                    seed[28:],
                ) != fingerprint:
                    raise ValueError("Find 身份库中的新版所有者材料与指纹不匹配。")
            elif version == 1:  # Historical rows only; no v1 query/provisioning path.
                if len(seed) != 16 or hashlib.sha256(seed).hexdigest()[:16] != fingerprint:
                    raise ValueError("Find 身份库中的派生种子与指纹不匹配。")
            else:
                raise ValueError("Find 身份库包含不支持的派生版本。")
            self.identities[fingerprint] = {
                "keyFingerprint": fingerprint,
                "deviceId": self._clean_device_id(device_id),
                "deviceName": self._clean_identity_name(device_name, "设备名称"),
                "bluetoothName": self._clean_identity_name(bluetooth_name, "蓝牙名称"),
                "findNote": self._clean_identity_name(
                    find_note or legacy_label, "Find 备注", allow_empty=False
                ),
                "trackingEnabled": bool(tracking_enabled and not history_only),
                "historyOnly": bool(history_only),
                "lastAutoQueryAt": last_auto_query_at,
                "lastAutoError": last_auto_error,
                "createdAt": str(created_at or _utc_now()),
                "lastQueriedAt": last_queried_at,
                "lastReportAt": last_report_at,
                "derivationVersion": version,
                "periodSeconds": int(period_seconds or ROTATION_PERIOD_SECONDS),
            }
            if not history_only:
                self.identity_seeds[fingerprint] = seed

    def _save_identity_registry(self) -> None:
        with self.identity_database:
            for fingerprint, entry in self.identities.items():
                self.identity_database.execute(
                    """
                    INSERT INTO find_identities (
                        key_fingerprint, label, device_id, device_name, bluetooth_name, find_note,
                        tracking_enabled, last_auto_query_at, last_auto_error,
                        created_at, last_queried_at, last_report_at,
                        derivation_version, derivation_seed, period_seconds, history_only
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    ON CONFLICT(key_fingerprint) DO UPDATE SET
                        label=excluded.label,
                        device_id=excluded.device_id,
                        device_name=excluded.device_name,
                        bluetooth_name=excluded.bluetooth_name,
                        find_note=excluded.find_note,
                        tracking_enabled=excluded.tracking_enabled,
                        last_auto_query_at=excluded.last_auto_query_at,
                        last_auto_error=excluded.last_auto_error,
                        created_at=excluded.created_at,
                        last_queried_at=excluded.last_queried_at,
                        last_report_at=excluded.last_report_at,
                        derivation_version=excluded.derivation_version,
                        derivation_seed=excluded.derivation_seed,
                        period_seconds=excluded.period_seconds,
                        history_only=excluded.history_only
                    """,
                    (
                        fingerprint,
                        entry["findNote"],
                        entry["deviceId"],
                        entry["deviceName"],
                        entry["bluetoothName"],
                        entry["findNote"],
                        1 if entry["trackingEnabled"] else 0,
                        entry["lastAutoQueryAt"],
                        entry["lastAutoError"],
                        entry["createdAt"],
                        entry["lastQueriedAt"],
                        entry["lastReportAt"],
                        entry["derivationVersion"],
                        b'' if entry.get("historyOnly") else self._protect_seed(self.identity_seeds[fingerprint]),
                        entry["periodSeconds"],
                        int(entry.get("historyOnly", False)),
                    ),
                )
            self.identity_database.execute(
                """
                INSERT INTO find_settings(name, value) VALUES ('selectedKeyFingerprint', ?)
                ON CONFLICT(name) DO UPDATE SET value=excluded.value
                """,
                (self.selected_key_fingerprint,),
            )
        self._secure_identity_database_files()

    def _device_fingerprints(self, fingerprint: str, device_scope: bool = True) -> list[str]:
        if fingerprint not in self.identities:
            raise ValueError("Find 身份不存在。")
        device_id = self.identities[fingerprint]["deviceId"].casefold()
        return [fp for fp, e in self.identities.items() if fp == fingerprint or
                (device_scope and device_id and e["deviceId"].casefold() == device_id)]

    def _current_fingerprints(self) -> set[str]:
        groups = {}
        for fp, e in self.identities.items():
            group = e["deviceId"].casefold() or f"key:{fp}"
            def order(key):
                try:
                    date = datetime.fromisoformat(self.identities[key]["createdAt"].replace('Z', '+00:00'))
                    stamp = date.replace(tzinfo=date.tzinfo or UTC).timestamp()
                except (TypeError, ValueError, OverflowError):
                    stamp = 0
                return (stamp, key)
            if group not in groups or order(fp) > order(groups[group]):
                groups[group] = fp
        return set(groups.values())

    def _migrate_history_tracking(self) -> None:
        # A one-time policy migration. A user's later manual enable survives restart.
        if self.identity_database.execute("SELECT 1 FROM find_settings WHERE name='historyTrackingPolicyV1'").fetchone():
            return
        current = self._current_fingerprints()
        with self.identity_database:
            for fp, entry in self.identities.items():
                if fp not in current:
                    self.identity_database.execute("UPDATE find_identities SET tracking_enabled=0 WHERE key_fingerprint=?", (fp,))
                    entry["trackingEnabled"] = False
            self.identity_database.execute("INSERT INTO find_settings(name,value) VALUES ('historyTrackingPolicyV1','1')")

    def _remember_public_only_identity(
        self,
        master_scalar: int,
        key: bytes,
        find_note: str,
        device_name: str = "",
        bluetooth_name: str = "",
        device_id: str = "",
    ) -> str:
        public_xy = find_public_only.master_public_bytes(master_scalar)
        fingerprint = find_public_only.identity_fingerprint(public_xy, key)
        material = master_scalar.to_bytes(28, "big") + key
        if fingerprint in self.identities:
            if self.identities[fingerprint].get("historyOnly"):
                if self.identities[fingerprint]["deviceId"].lower() != device_id.lower():
                    raise ValueError("Find 身份指纹冲突。")
                self.identities[fingerprint].update(historyOnly=False, trackingEnabled=False)
                self.identity_seeds[fingerprint] = material
            elif self.identity_seeds[fingerprint] != material:
                raise ValueError("Find 身份指纹冲突。")
            return fingerprint
        if len(self.identities) >= MAX_IDENTITIES:
            raise ValueError("Find 身份库已达到 256 条上限。")
        self.identities[fingerprint] = {
            "keyFingerprint": fingerprint,
            "deviceId": self._clean_device_id(device_id),
            "deviceName": self._clean_identity_name(device_name, "设备名称"),
            "bluetoothName": self._clean_identity_name(bluetooth_name, "蓝牙名称"),
            "findNote": self._clean_identity_name(find_note, "Find 备注", allow_empty=False),
            "trackingEnabled": True,
            "lastAutoQueryAt": None,
            "lastAutoError": None,
            "createdAt": _utc_now(),
            "lastQueriedAt": None,
            "lastReportAt": None,
            "derivationVersion": find_public_only.VERSION,
            "periodSeconds": find_public_only.PERIOD_SECONDS,
        }
        self.identity_seeds[fingerprint] = material
        return fingerprint

    @staticmethod
    def _clean_device_id(value: Any) -> str:
        device_id = str(value or "").strip()
        if not device_id:
            return ""
        if len(device_id) > 128 or any(ord(character) < 32 for character in device_id):
            raise ValueError("设备身份必须是不超过 128 个字符的可见文本。")
        return device_id

    def _select_identity(self, fingerprint: str) -> None:
        normalized = str(fingerprint).lower()
        if normalized not in self.identities:
            raise ValueError("所选 Find 身份不存在。")
        self.selected_key_fingerprint = normalized

    def identity_payload(self) -> dict[str, Any]:
        identities = []
        for fingerprint, entry in self.identities.items():
            current_slot = slot_for_utc(int(time.time()))
            pair = (self._pair_for_slot(fingerprint, current_slot)
                    if entry["derivationVersion"] == find_public_only.VERSION and not entry.get("historyOnly") else None)
            point_count = int(self.identity_database.execute(
                """
                SELECT COUNT(*)
                  FROM find_track_points
                 WHERE key_fingerprint=?
                """,
                (fingerprint,),
            ).fetchone()[0])
            latest_track_row = self.identity_database.execute(
                """
                SELECT report_timestamp
                  FROM find_track_points
                 WHERE key_fingerprint=?
                 ORDER BY julianday(report_timestamp) DESC
                 LIMIT 1
                """,
                (fingerprint,),
            ).fetchone()
            latest_track_at = latest_track_row[0] if latest_track_row else None
            identities.append(
                {
                    **entry,
                    "lastReportAt": latest_report_timestamp(
                        (entry["lastReportAt"], latest_track_at)
                    ),
                    "address": _find_address(pair.adv_key_bytes) if pair else "",
                    "advertisementKey": pair.adv_key_bytes.hex().upper() if pair else "",
                    "trackingEnabled": bool(pair and entry["trackingEnabled"]),
                    "currentSlot": current_slot,
                    "trackPointCount": point_count,
                    "selected": fingerprint == self.selected_key_fingerprint,
                }
            )
        identities.sort(
            key=lambda item: (
                not item["selected"],
                (item["findNote"] or item["deviceName"] or item["bluetoothName"]).casefold(),
            )
        )
        return {
            "selectedKeyFingerprint": self.selected_key_fingerprint,
            "trackingIntervalMinutes": self.tracking_interval_minutes,
            "identities": identities,
        }

    @staticmethod
    def _report_payload(report: Any) -> dict[str, Any]:
        return {
            "found": True,
            "timestamp": report.timestamp.isoformat(),
            "latitude": report.latitude,
            "longitude": report.longitude,
            "horizontalAccuracy": report.horizontal_accuracy,
            "confidence": report.confidence,
            "status": report.status,
        }

    def _save_track_points(
        self,
        fingerprint: str,
        reports: list[Any],
        source_slots: dict[bytes, int] | None = None,
    ) -> int:
        if not reports:
            return 0
        self._prune_track_history()
        received_at = _utc_now()
        rows = [
            (
                fingerprint,
                report.timestamp.isoformat(),
                received_at,
                float(report.latitude),
                float(report.longitude),
                float(report.horizontal_accuracy),
                int(report.confidence),
                int(report.status),
                int((source_slots or {}).get(report.hashed_adv_key_bytes, -1)),
            )
            for report in reports
        ]
        with self.identity_database:
            # Rows captured before source attribution was introduced are not
            # new track points. Backfill them without inflating savedPointCount.
            self.identity_database.executemany(
                """
                UPDATE find_track_points
                   SET source_slot=?
                 WHERE source_slot=-1
                   AND key_fingerprint=?
                   AND report_timestamp=?
                   AND latitude=? AND longitude=?
                """,
                [
                    (row[8], row[0], row[1], row[3], row[4])
                    for row in rows
                    if row[8] >= 0
                ],
            )
            changes_before = self.identity_database.total_changes
            self.identity_database.executemany(
                """
                INSERT OR IGNORE INTO find_track_points (
                    key_fingerprint, report_timestamp, received_at, latitude, longitude,
                    horizontal_accuracy, confidence, status, source_slot
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                rows,
            )
        inserted = self.identity_database.total_changes - changes_before
        self._secure_identity_database_files()
        return inserted

    def _save_track_point(self, fingerprint: str, report: Any) -> None:
        self._save_track_points(fingerprint, [report])

    def _latest_track_point_payload(self, fingerprint: str) -> dict[str, Any] | None:
        row = self.identity_database.execute(
            """
            SELECT report_timestamp, latitude, longitude, horizontal_accuracy,
                   confidence, status, source_slot
              FROM find_track_points
             WHERE key_fingerprint=?
             ORDER BY julianday(report_timestamp) DESC
             LIMIT 1
            """,
            (fingerprint,),
        ).fetchone()
        if row is None:
            return None
        return {
            "found": True,
            "timestamp": row[0],
            "latitude": row[1],
            "longitude": row[2],
            "horizontalAccuracy": row[3],
            "confidence": row[4],
            "status": row[5],
            "sourceSlot": row[6],
        }

    @staticmethod
    def _tracking_due(entry: dict[str, Any], interval_minutes: int, now: datetime) -> bool:
        if (entry.get("historyOnly") or entry.get("derivationVersion", find_public_only.VERSION) != find_public_only.VERSION
                or not entry["trackingEnabled"]):
            return False
        last_value = entry["lastAutoQueryAt"]
        if not last_value:
            return True
        try:
            last = datetime.fromisoformat(str(last_value))
            if last.tzinfo is None:
                last = last.replace(tzinfo=UTC)
        except ValueError:
            return True
        return now - last >= timedelta(minutes=interval_minutes)

    def state_payload(self) -> dict[str, Any]:
        logged_in = self.account is not None and self.account.login_state == LoginState.LOGGED_IN
        needs_2fa = (
            self.account is not None
            and self.account.login_state == LoginState.REQUIRE_2FA
        )
        return {
            "loggedIn": logged_in,
            "authBackend": self.auth_backend,
            "queryReady": _find_query_ready(self.account),
            "queryError": FIND_QUERY_UNAVAILABLE_MESSAGE if logged_in and not _find_query_ready(self.account) else None,
            "needs2fa": needs_2fa,
            "secondFactorWarning": getattr(self.account, "second_factor_warning", None) if needs_2fa else None,
            "appleAccountUsername": self.apple_account_username,
            "sessionStored": bool(
                logged_in
                and self.session_store_path is not None
                and self.session_store_path.is_file()
            ),
            "sessionRestored": bool(logged_in and self.session_restored),
            "sessionError": self.session_error,
            "sessionProtection": "windows-dpapi" if os.name == "nt" else "aes256-local-key",
            "methods": self._method_payloads() if needs_2fa else [],
            "activeMethodIndex": (
                self.requested_second_factor_index if needs_2fa else None
            ),
            **self.public_metadata,
            **self.identity_payload(),
        }

    def _remove_saved_session(self) -> None:
        if self.session_store_path is not None:
            self.session_store_path.unlink(missing_ok=True)

    def _save_session(self) -> None:
        if (
            self.session_store_path is None
            or self.account is None
            or self.account.login_state != LoginState.LOGGED_IN
        ):
            return
        self._drop_password(self.account)
        state = self.account.to_json()
        if state["account"].get("password") is not None:
            raise RuntimeError("拒绝保存包含密码的 Apple 会话。")
        state["account"] = {"username": None, "password": None, "info": None}
        cleartext = json.dumps(state, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        protected = _protect_session(cleartext, self.session_file_key_path)
        self.session_store_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.session_store_path.with_suffix(self.session_store_path.suffix + ".tmp")
        temporary.write_bytes(protected)
        os.replace(temporary, self.session_store_path)

    async def restore_saved_session(self) -> None:
        if self.session_store_path is None or not self.session_store_path.is_file():
            return
        try:
            protected = self.session_store_path.read_bytes()
            if len(protected) > MAX_SESSION_BYTES:
                raise ValueError("invalid session envelope")
            cleartext = _unprotect_session(protected, self.session_file_key_path)
            state = json.loads(cleartext.decode("utf-8"))
            account_fields = state.get("account", {})
            if account_fields.get("username") is not None or account_fields.get("password") is not None:
                raise ValueError("persisted credentials are forbidden")
            if self.auth_backend == "local":
                account = QueryAppleAccount.from_json(state, anisette_libs_path=self.anisette_libs_path)
            else:
                account = await create_backend_account(self.auth_backend, self.auth_state_dir,
                                                        self.auth_cache_dir, state=state)
            _install_apple_gateway_resolver(account)
            if account.login_state != LoginState.LOGGED_IN:
                await account.close()
                raise ValueError("persisted session is not logged in")
            self.account = account
            if not self.apple_account_username:
                restored_name = str(
                    account.account_name or getattr(account, "_username", "") or ""
                ).strip()
                if restored_name:
                    self._save_apple_account_username(restored_name)
            self.session_restored = True
        except Exception as error:
            # Wrong machine, unavailable native API or damaged state is not
            # permission to erase the saved account or rebuild its identity.
            self.account = None
            self.session_restored = False
            busy = type(error).__name__ == 'BackendVerificationError' and getattr(error, 'code', None) == 'busy'
            self.session_error = ("另一组 Find 服务正在使用本机认证状态。请停止旧服务后重新启动；无需先重新登录，现有会话和身份数据已保留。" if busy else
                                  "本机认证会话恢复失败；会话和身份数据已保留。请先检查并重启认证后端，仍无法恢复时再重新登录。")
            LOGGER.warning('Find session restore failed category=%s', 'backend-busy' if busy else type(error).__name__)

    def _method_payloads(self) -> list[dict[str, Any]]:
        payloads: list[dict[str, Any]] = []
        for index, method in enumerate(self.second_factors):
            if isinstance(method, AsyncTrustedDeviceSecondFactor):
                label = "受信任的 Apple 设备"
                kind = "trusted_device"
            elif isinstance(method, AsyncSmsSecondFactor):
                label = f"短信 {method.phone_number}"
                kind = "sms"
            else:
                label = "其他验证方式"
                kind = "unknown"
            payloads.append({"index": index, "kind": kind, "label": label})
        return payloads

    @staticmethod
    def _drop_password(account: AsyncAppleAccount | None) -> None:
        # FindMy.py retains this value for automatic reauthentication. Prefer a
        # fresh login over keeping it after authentication succeeds.
        if account is not None:
            account._password = None  # type: ignore[attr-defined]  # noqa: SLF001

    async def _close_account(self) -> None:
        account = self.account
        self.account = None
        self.second_factors = []
        self.requested_second_factor_index = None
        self._drop_password(account)
        if account is not None:
            await account.close()
        self.session_restored = False

    async def login(self, email: str, password: str, consent: bool) -> dict[str, Any]:
        if not consent:
            raise ValueError("请先阅读并确认隐私与账号说明。")
        if not email or len(email) > 254 or "@" not in email:
            raise ValueError("请输入有效的 Apple 账号。")
        if not password or len(password) > 512:
            raise ValueError("请输入 Apple 账号密码。")

        async with self.lock:
            await self._close_account()
            self.session_error = None
            self._remove_saved_session()
            if self.auth_backend == "local":
                provider = LocalAnisetteProvider(libs_path=self.anisette_libs_path)
                account = QueryAppleAccount(provider)
            else:
                account = await create_backend_account(self.auth_backend, self.auth_state_dir,
                                                        self.auth_cache_dir, provision=True)
            _install_apple_gateway_resolver(account)
            self.account = account
            try:
                async with asyncio.timeout(90):
                    state = await account.login(email, password)
                    if state == LoginState.REQUIRE_2FA:
                        self._save_apple_account_username(email)
                        self.second_factors = list(await account.get_2fa_methods())
                        if not self.second_factors:
                            raise RuntimeError("Apple 要求双重认证，但没有返回可用方式。")
                    elif state == LoginState.LOGGED_IN:
                        self._save_apple_account_username(email)
                        self._drop_password(account)
                        self._save_session()
                    else:
                        raise RuntimeError(f"Unexpected login state: {state.name}")
                return self.state_payload()
            except Exception:
                await self._close_account()
                raise

    def _second_factor(self, index: int) -> Any:
        if not 0 <= index < len(self.second_factors):
            raise ValueError("双重认证方式已失效，请重新登录。")
        return self.second_factors[index]

    async def request_second_factor(self, index: int) -> dict[str, Any]:
        async with self.lock:
            method = self._second_factor(index)
            await method.request()
            self.requested_second_factor_index = index
            return self.state_payload()

    async def submit_second_factor(self, index: int, code: str) -> dict[str, Any]:
        if not re.fullmatch(r"[0-9]{4,8}", code):
            raise ValueError("请输入 4–8 位数字验证码。")
        async with self.lock:
            method = self._second_factor(index)
            if (
                self.requested_second_factor_index is not None
                and index != self.requested_second_factor_index
            ):
                requested = self._method_payloads()[self.requested_second_factor_index][
                    "label"
                ]
                raise ValueError(
                    f"验证码是按“{requested}”请求的。请保持该方式，或重新选择并发送验证码。"
                )
            try:
                async with asyncio.timeout(90):
                    state = await method.submit(code)
            except TimeoutError:
                await self._close_account()
                raise
            if state != LoginState.LOGGED_IN:
                raise RuntimeError(f"Unexpected 2FA state: {state.name}")
            self._drop_password(self.account)
            self.second_factors = []
            self.requested_second_factor_index = None
            self._save_session()
            return self.state_payload()

    async def _query_identity_locked(
        self,
        fingerprint: str,
        *,
        automatic: bool,
        save_history: bool = False,
        query_window: str = "latest",
    ) -> dict[str, Any]:
        if self.account is None or self.account.login_state != LoginState.LOGGED_IN:
            if self.session_error:
                raise AppleSessionExpiredError(SESSION_EXPIRED_MESSAGE)
            raise PermissionError("请先登录。")
        if not _find_query_ready(self.account):
            raise AppleFindUnavailableError(FIND_QUERY_UNAVAILABLE_MESSAGE)
        entry = self.identities[fingerprint]
        attempted_at = _utc_now()
        entry["lastQueriedAt"] = attempted_at
        if automatic:
            entry["lastAutoQueryAt"] = attempted_at
            entry["lastAutoError"] = None
        self._save_identity_registry()
        reports: list[Any] = []
        try:
            now_seconds = int(datetime.now(UTC).timestamp())
            if query_window == "recent":
                lookback_seconds = FIND_RECENT_QUERY_LOOKBACK_SECONDS
                lookahead_seconds = FIND_RECENT_QUERY_LOOKAHEAD_SECONDS
            else:
                lookback_seconds = FIND_LATEST_QUERY_LOOKBACK_SECONDS
                lookahead_seconds = FIND_LATEST_QUERY_LOOKAHEAD_SECONDS
            selected_slots = slots_for_window(
                max(0, now_seconds - lookback_seconds),
                now_seconds + lookahead_seconds,
            )
            source_slots: dict[bytes, int] = {}
            pairs = []
            for slot in selected_slots:
                pair = self._pair_for_slot(fingerprint, slot)
                pairs.append(pair)
                source_slots[pair.hashed_adv_key_bytes] = slot
            queried_key_count = len(pairs)
            responses = []
            batches = query_key_batches(pairs)
            for batch_index, batch in enumerate(batches):
                responses.append(
                    await self.account.fetch_location_history(list(batch))
                )
                if batch_index + 1 < len(batches):
                    await asyncio.sleep(query_batch_delay_seconds())
            candidates = []
            for response in responses:
                if isinstance(response, dict):
                    for pair, history in response.items():
                        for candidate in history or []:
                            candidates.append(
                                (candidate, source_slots.get(pair.hashed_adv_key_bytes, -1))
                            )
                elif response is not None:
                    response_items = (
                        response if isinstance(response, (list, tuple)) else [response]
                    )
                    for candidate in response_items:
                        candidates.append((candidate, -1))
            report, matched_slot = max(
                candidates,
                key=lambda item: item[0].timestamp,
                default=(None, -1),
            )
            reports = [candidate for candidate, _slot in candidates]
            self._save_session()
        except Exception as error:
            if _expired_apple_session(error):
                # Close only the rejected account in RAM. Preserve encrypted
                # owner material, history, tracking preferences and session file.
                await self._close_account()
                self.session_error = SESSION_EXPIRED_MESSAGE
                error = AppleSessionExpiredError(SESSION_EXPIRED_MESSAGE)
            if automatic:
                entry["lastAutoError"] = _safe_error(error)
                self._save_identity_registry()
            raise error from None
        # A successful manual query proves that the Apple path recovered too.
        # Do not keep presenting an older background-query transport failure.
        entry["lastAutoError"] = None
        if report is None:
            self._save_identity_registry()
            return {
                "found": False,
                "queryScope": "window",
                "queryWindow": query_window,
                "derivedSlotCount": queried_key_count,
                "queriedKeyCount": queried_key_count,
            }
        fetched_payload = self._report_payload(report)
        saved_point_count = 0
        if save_history or entry["trackingEnabled"]:
            saved_point_count = self._save_track_points(
                fingerprint, reports, source_slots
            )
        latest_payload = self._latest_track_point_payload(fingerprint)
        latest_time = parse_report_timestamp(
            latest_payload["timestamp"] if latest_payload else None
        )
        fetched_time = parse_report_timestamp(fetched_payload["timestamp"])
        from_history = bool(
            latest_payload and latest_time and fetched_time and latest_time > fetched_time
        )
        result_payload = latest_payload if from_history else fetched_payload
        entry["lastReportAt"] = latest_report_timestamp(
            (entry["lastReportAt"], result_payload["timestamp"])
        )
        self._save_identity_registry()
        return {
            **result_payload,
            **({} if from_history else {"matchedSlot": matched_slot}),
            "queryScope": "window",
            "queryWindow": query_window,
            "derivedSlotCount": queried_key_count,
            "queriedKeyCount": queried_key_count,
            "savedPointCount": saved_point_count,
            "fromHistory": from_history,
            "queryReportTimestamp": fetched_payload["timestamp"],
        }

    async def query(
        self,
        *,
        fingerprint: str | None = None,
        save_history: bool = False,
        query_window: str = "latest",
    ) -> dict[str, Any]:
        async with asyncio.timeout(90), self.lock:
            normalized = str(fingerprint or self.selected_key_fingerprint).lower()
            if normalized not in self.identities:
                raise ValueError("需要查询的 Find 身份不存在。")
            if self.identities[normalized].get("historyOnly"):
                raise ValueError("此设备仅有历史轨迹，请导入设备资料和查询密钥后再查询。")
            if self.identities[normalized]["derivationVersion"] != find_public_only.VERSION:
                raise ValueError("旧版 Find 身份仅保留本机历史；请使用 Find v2 身份。")
            normalized_window = str(query_window).strip().lower()
            if normalized_window not in {"latest", "recent"}:
                raise ValueError("查询时间范围无效。")
            return await self._query_identity_locked(
                normalized,
                automatic=False,
                save_history=save_history,
                query_window=normalized_window,
            )

    async def run_due_tracking_once(self) -> None:
        async with self.lock:
            now = datetime.now(UTC)
            if now >= self.next_track_history_prune_at:
                self._prune_track_history()
                self.next_track_history_prune_at = now + timedelta(hours=1)
            if not _find_query_ready(self.account):
                return
            due = [
                fingerprint
                for fingerprint, entry in self.identities.items()
                if self._tracking_due(entry, self.tracking_interval_minutes, now)
            ]
        for fingerprint in due:
            # Let manual requests acquire the lock between tags, rather than
            # queueing behind an entire multi-minute background library pass.
            async with self.lock:
                if not _find_query_ready(self.account):
                    return
                entry = self.identities.get(fingerprint)
                if entry is None or not self._tracking_due(entry, self.tracking_interval_minutes, datetime.now(UTC)):
                    continue
                device_name = (
                    entry.get("deviceName")
                    or entry.get("bluetoothName")
                    or entry.get("findNote")
                    or "unnamed"
                )
                started_at = time.monotonic()
                try:
                    result = await self._query_identity_locked(fingerprint, automatic=True)
                    LOGGER.info(
                        "poll success device=%r fingerprint=%s found=%s queried_keys=%s "
                        "saved_points=%s report_time=%s duration_ms=%d",
                        device_name,
                        fingerprint,
                        result.get("found", True),
                        result.get("queriedKeyCount", 0),
                        result.get("savedPointCount", 0),
                        result.get("queryReportTimestamp", "none"),
                        round((time.monotonic() - started_at) * 1000),
                    )
                except Exception as error:
                    LOGGER.error(
                        "poll failed device=%r fingerprint=%s error=%s duration_ms=%d",
                        device_name,
                        fingerprint,
                        _safe_error(error),
                        round((time.monotonic() - started_at) * 1000),
                    )
                    # The sanitized error is stored on the identity. Other tags
                    # must still get their turn during the same scheduler pass.
                    if isinstance(error, AppleSessionExpiredError):
                        return  # One rejected account affects every identity.
                    continue

    async def _tracking_loop(self) -> None:
        try:
            while True:
                await self.run_due_tracking_once()
                await asyncio.sleep(TRACKING_POLL_SECONDS)
        except asyncio.CancelledError:
            pass

    def start_background_tracking(self) -> None:
        if self.tracking_task is None or self.tracking_task.done():
            self.tracking_task = asyncio.create_task(self._tracking_loop())

    async def stop_background_tracking(self) -> None:
        task = self.tracking_task
        self.tracking_task = None
        if task is None:
            return
        task.cancel()
        await task

    async def export_portable_backup(self, password: str, include_history: bool = True,
                                     fingerprints=None, include_keys: bool = True, encrypt: bool = True,
                                     plaintext_confirmed: bool = False) -> dict[str, Any]:
        if type(encrypt) is not bool or (not encrypt and plaintext_confirmed is not True):
            raise ValueError("请确认未加密资料的隐私泄露风险。")
        async with self.lock:
            payload = find_backup.library_payload(self, include_history, fingerprints, include_keys)
        text = (await asyncio.to_thread(find_backup.encrypt_backup, payload, password) if encrypt else find_backup.plain_backup(payload))
        return {("encryptedBackup" if encrypt else "backupText"): text, "encrypted": encrypt,
                "identityCount": len(payload["identities"]), "historyIncluded": include_history}

    async def import_portable_backup(self, encrypted: str, password: str, allow_plaintext: bool = False) -> dict[str, Any]:
        payload = await asyncio.to_thread(find_backup.read_backup, encrypted, password, allow_plaintext)
        async with self.lock:
            counts = find_backup.import_library(self, payload)
            return {**self.state_payload(), **counts}

    async def import_local_v2_identity(
        self, source_directory: str, fingerprint: str,
        device_id: str, bluetooth_name: str,
    ) -> dict[str, Any]:
        """Copy one protected owner from a read-only local library, never rotate it."""
        normalized = fingerprint.strip().lower()
        expected_id = device_id.strip()
        expected_ble = self._clean_identity_name(bluetooth_name, "蓝牙名称", allow_empty=False)
        if not re.fullmatch(r"[0-9a-f]{32}", normalized):
            raise ValueError("必须指定 32 位 Find v2 身份指纹。")
        if not re.fullmatch(r"activation:[0-9A-Fa-f]{16}", expected_id):
            raise ValueError("必须指定准确的 activation: 设备身份。")
        source = Path(source_directory)
        if not source.is_absolute() or not source.is_dir():
            raise ValueError("源 Find 数据目录必须是已存在的绝对路径。")
        source = source.resolve()
        database = source / "find-identities.sqlite3"
        if not database.is_file() or database.samefile(self.identity_database_path):
            raise ValueError("源必须是另一个已存在的 Find 身份库。")
        try:
            # Do not instantiate QuerySession: its startup can migrate/save/prune
            # the source. SQLite's read-only connection also sees committed WAL.
            connection = sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)
            try:
                row = connection.execute(
                    """SELECT device_id, device_name, bluetooth_name, find_note,
                              derivation_version, period_seconds, derivation_seed
                         FROM find_identities WHERE key_fingerprint=?""",
                    (normalized,),
                ).fetchone()
            finally:
                connection.close()
            if row is None:
                raise ValueError("源库没有指定的 Find v2 身份。")
            stored_id, name, ble, note, version, period, protected = row
            if (str(stored_id).lower() != expected_id.lower() or ble != expected_ble or
                    int(version) != find_public_only.VERSION or
                    int(period) != find_public_only.PERIOD_SECONDS):
                raise ValueError("源身份的设备 ID、蓝牙名称或 v2 参数不匹配。")
            name = self._clean_identity_name(name, "设备名称", allow_empty=False)
            note = self._clean_identity_name(note or name, "Find 备注", allow_empty=False)
            # Windows uses the current user's DPAPI, not the destination's key.
            # The non-Windows path is the maintained service's default file key.
            material = _unprotect_session(bytes(protected), source / "account-session.encrypted.key")
            if len(material) != 44 or material[28:] in (bytes(16), b"\xff" * 16):
                raise ValueError("源库的 Find v2 所有者材料无效。")
            scalar = int.from_bytes(material[:28], "big")
            public_xy = find_public_only.master_public_bytes(scalar)
            if not secrets.compare_digest(
                find_public_only.identity_fingerprint(public_xy, material[28:]), normalized
            ):
                raise ValueError("源库的 Find v2 所有者材料与指纹不匹配。")
        except (OSError, sqlite3.Error) as error:
            raise ValueError("无法只读打开或解密源 Find 身份库。") from error

        async with self.lock:
            for existing, entry in self.identities.items():
                if existing != normalized and entry["deviceId"].lower() == expected_id.lower():
                    raise ValueError("目标库已有该设备的另一身份；不会覆盖或换主。")
            entry = self.identities.get(normalized)
            if entry is not None:
                if (entry["deviceId"].lower() != expected_id.lower() or
                        entry["bluetoothName"] != expected_ble or
                        entry["derivationVersion"] != find_public_only.VERSION or
                        self.identity_seeds.get(normalized) != material):
                    raise ValueError("目标库已有冲突身份；不会覆盖。")
                already_present = True
            else:
                if len(self.identities) >= MAX_IDENTITIES:
                    raise ValueError("Find 身份库已达到 256 条上限。")
                created_at = _utc_now()
                # One INSERT, not _save_identity_registry's all-row upsert.
                # Existing identities, tracks, selection and account are untouched.
                protected_copy = self._protect_seed(material)
                with self.identity_database:
                    self.identity_database.execute(
                        """INSERT INTO find_identities
                           (key_fingerprint, label, device_id, device_name, bluetooth_name,
                            find_note, tracking_enabled, derivation_version, derivation_seed,
                            created_at, period_seconds)
                           VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?)""",
                        (normalized, note, stored_id, name, ble, note,
                         find_public_only.VERSION, protected_copy, created_at,
                         find_public_only.PERIOD_SECONDS),
                    )
                self.identities[normalized] = {
                    "keyFingerprint": normalized, "deviceId": stored_id,
                    "deviceName": name, "bluetoothName": ble, "findNote": note,
                    "trackingEnabled": False, "lastAutoQueryAt": None, "lastAutoError": None,
                    "createdAt": created_at, "lastQueriedAt": None, "lastReportAt": None,
                    "derivationVersion": find_public_only.VERSION,
                    "periodSeconds": find_public_only.PERIOD_SECONDS,
                }
                self.identity_seeds[normalized] = material
                self._secure_identity_database_files()
                already_present = False
            return {
                "importedKeyFingerprint": normalized, "deviceId": stored_id,
                "deviceName": name, "bluetoothName": ble,
                "alreadyPresent": already_present,
                "selectedKeyFingerprint": self.selected_key_fingerprint,
                "identityCount": len(self.identities),
            }

    async def select_identity(self, fingerprint: str) -> dict[str, Any]:
        async with self.lock:
            self._select_identity(fingerprint)
            self._save_identity_registry()
            return self.state_payload()

    async def verify_identity_proof(
        self, fingerprint: str, slot: int, public_key_hex: str,
    ) -> dict[str, Any]:
        """Verify that a device committed the same seed held by this service."""
        normalized = str(fingerprint).strip().lower()
        public_text = str(public_key_hex).strip()
        if not re.fullmatch(r"[0-9A-Fa-f]{56}", public_text):
            raise ValueError("设备提交证明必须包含 28 字节派生公钥。")
        if slot < 0 or slot > 0xFFFFFFFFFFFFFFFF:
            raise ValueError("设备提交证明包含无效 UTC 时间片。")
        async with self.lock:
            if normalized not in self.identities or normalized not in self.identity_seeds:
                raise ValueError("本机没有待验证的 Find 派生种子。")
            expected_public = self._pair_for_slot(normalized, int(slot)).adv_key_bytes
            if not secrets.compare_digest(expected_public, bytes.fromhex(public_text)):
                raise ValueError("设备提交的派生公钥与本机身份不匹配。")
            self._select_identity(normalized)
            self._save_identity_registry()
            return {**self.state_payload(), "verifiedKeyFingerprint": normalized}

    async def prove_owner_action(
        self, fingerprint: str, device_id_hex: str,
        nonce_hex: str, action_hash_hex: str,
    ) -> dict[str, str]:
        """Authorize one BLE action using the canonical encrypted Find owner K."""
        normalized = str(fingerprint).strip().lower()
        if not re.fullmatch(r"[0-9A-Fa-f]{16}", device_id_hex):
            raise ValueError("设备身份必须是 8 字节十六进制数据。")
        if not re.fullmatch(r"[0-9A-Fa-f]{32}", nonce_hex):
            raise ValueError("设备挑战必须是 16 字节十六进制数据。")
        if not re.fullmatch(r"[0-9A-Fa-f]{64}", action_hash_hex):
            raise ValueError("操作摘要必须是 32 字节十六进制数据。")
        device_id = bytes.fromhex(device_id_hex)
        nonce = bytes.fromhex(nonce_hex)
        digest = bytes.fromhex(action_hash_hex)
        expected_device_id = f"activation:{device_id.hex()}"
        async with self.lock:
            entry = self.identities.get(normalized)
            material = self.identity_seeds.get(normalized)
            if (entry is None or material is None or
                    entry.get("derivationVersion") != find_public_only.VERSION or
                    str(entry.get("deviceId", "")).lower() != expected_device_id or
                    len(material) != 44):
                raise ValueError("本机没有与该设备匹配的当前 Find 身份。")
            proof = owner_auth.prove_with_find_action_hash(
                material[28:], device_id, nonce, digest
            )
            return {"proofHex": proof.hex().upper(), "fingerprintHex": normalized.upper()}

    async def prepare_public_only_owner(
        self, nonce_hex: str, *, find_note: str, device_name: str,
        bluetooth_name: str, device_id: str,
    ) -> dict[str, Any]:
        """Prepare a first-owner v2 stage; the local master scalar never enters JSON."""
        if not re.fullmatch(r"[0-9A-Fa-f]{16}", nonce_hex):
            raise ValueError("设备所有者挑战必须是 8 字节十六进制数据。")
        nonce = bytes.fromhex(nonce_hex)
        metadata = {
            "findNote": self._clean_identity_name(find_note or device_name or bluetooth_name or "已登记标签", "Find 备注", allow_empty=False),
            "deviceName": self._clean_identity_name(device_name, "设备名称"),
            "bluetoothName": self._clean_identity_name(bluetooth_name, "蓝牙名称"),
            "deviceId": self._clean_device_id(device_id),
        }
        async with self.lock:
            master_scalar, key = find_public_only.new_owner_material()
            public_xy = find_public_only.master_public_bytes(master_scalar)
            fingerprint = find_public_only.identity_fingerprint(public_xy, key)
            if fingerprint in self.identities:
                raise ValueError("Find 身份指纹冲突，请重新生成。")
            transaction_id = secrets.token_hex(16)
            material = master_scalar.to_bytes(28, "big") + key
            with self.identity_database:
                self.identity_database.execute(
                    """
                    INSERT INTO find_owner_rotations(
                        rotation_id, current_fingerprint, replacement_fingerprint,
                        protected_replacement_seed, nonce, created_at, prepared_metadata
                    ) VALUES (?, '', ?, ?, ?, ?, ?)
                    """,
                    (transaction_id, fingerprint, self._protect_seed(material), nonce,
                     _utc_now(), json.dumps(metadata, ensure_ascii=False)),
                )
            self._secure_identity_database_files()
            return {
                "transactionId": transaction_id,
                "fingerprintHex": fingerprint.upper(),
                "payloadHex": (bytes((find_public_only.WIRE_VERSION,)) + nonce + public_xy + key).hex().upper(),
                "deviceId": metadata["deviceId"],
                "bluetoothName": metadata["bluetoothName"],
            }

    async def prepare_public_only_replacement(
        self, fingerprint_hex: str, nonce_hex: str, device_id: str,
    ) -> dict[str, Any]:
        """Persist a new local owner before handing an authenticated stage to BLE."""
        current = str(fingerprint_hex).strip().lower()
        if not re.fullmatch(r"[0-9a-f]{32}", current):
            raise ValueError("当前新版 Find 身份指纹无效。")
        if not re.fullmatch(r"[0-9a-fA-F]{16}", nonce_hex):
            raise ValueError("设备所有者挑战必须是 8 字节十六进制数据。")
        stable_id = self._clean_device_id(device_id)
        match = re.fullmatch(r"activation:([0-9A-Fa-f]{16})", stable_id)
        if not match or int(match.group(1), 16) == 0:
            raise ValueError("更新新版 Find 身份需要设备的固定激活标识。")
        async with self.lock:
            previous = self.identities.get(current)
            if previous is None or previous["derivationVersion"] != find_public_only.VERSION:
                raise ValueError("本机没有当前设备的新版 Find 身份。")
            if previous["deviceId"].lower() != stable_id.lower():
                raise ValueError("Find 身份与当前设备不匹配。")
            if self.identity_database.execute(
                "SELECT 1 FROM find_owner_rotations WHERE current_fingerprint=?", (current,)
            ).fetchone():
                raise ValueError("此设备已有待确认的身份更新，请先恢复事务。")
            old_material = self.identity_seeds[current]
            if len(old_material) != 44:
                raise ValueError("当前 Find 身份材料无效。")
            scalar, key = find_public_only.new_owner_material()
            public_xy = find_public_only.master_public_bytes(scalar)
            replacement = find_public_only.identity_fingerprint(public_xy, key)
            if replacement in self.identities:
                raise ValueError("Find 身份指纹冲突，请重新生成。")
            nonce = bytes.fromhex(nonce_hex)
            stage = find_public_only.replacement_stage(
                old_material[28:], bytes.fromhex(match.group(1)), nonce,
                bytes.fromhex(current), public_xy, key,
            )
            transaction_id = secrets.token_hex(16)
            metadata = {name: previous[name] for name in (
                "findNote", "deviceName", "bluetoothName", "deviceId"
            )}
            with self.identity_database:
                self.identity_database.execute(
                    """INSERT INTO find_owner_rotations(
                         rotation_id, current_fingerprint, replacement_fingerprint,
                         protected_replacement_seed, nonce, created_at, prepared_metadata
                       ) VALUES (?, ?, ?, ?, ?, ?, ?)""",
                    (transaction_id, current, replacement,
                     self._protect_seed(scalar.to_bytes(28, "big") + key), nonce,
                     _utc_now(), json.dumps(metadata, ensure_ascii=False)),
                )
            self._secure_identity_database_files()
            return {
                "transactionId": transaction_id,
                "currentFingerprintHex": current.upper(),
                "fingerprintHex": replacement.upper(),
                "payloadHex": stage.hex().upper(),
                "deviceId": previous["deviceId"],
            }

    async def finalize_public_only_owner(
        self, transaction_id: str, slot: int, public_key_hex: str,
        fingerprint_hex: str,
    ) -> dict[str, Any]:
        if not re.fullmatch(r"[0-9a-fA-F]{32}", transaction_id):
            raise ValueError("Find 身份事务编号无效。")
        if not 0 <= slot < 1 << 64:
            raise ValueError("设备提交证明包含无效 UTC 时间片。")
        if not re.fullmatch(r"[0-9a-fA-F]{56}", public_key_hex) or not re.fullmatch(r"[0-9a-fA-F]{32}", fingerprint_hex):
            raise ValueError("设备提交的新版 Find 证明格式无效。")
        async with self.lock:
            row = self.identity_database.execute(
                """
                SELECT current_fingerprint, replacement_fingerprint,
                       protected_replacement_seed, prepared_metadata
                  FROM find_owner_rotations WHERE rotation_id=?
                """,
                (transaction_id.lower(),),
            ).fetchone()
            if row is None:
                raise ValueError("新版 Find 身份事务不存在或已过期。")
            current = str(row[0])
            if current and (current not in self.identities or
                            self.identities[current]["derivationVersion"] != find_public_only.VERSION):
                raise ValueError("当前设备的旧版 Find 身份已不存在。")
            material = self._unprotect_seed(bytes(row[2]))
            if len(material) != 44:
                raise ValueError("新版 Find 身份事务材料无效。")
            master_scalar = int.from_bytes(material[:28], "big")
            key = material[28:]
            expected_fingerprint = find_public_only.identity_fingerprint(
                find_public_only.master_public_bytes(master_scalar), key
            )
            if not secrets.compare_digest(expected_fingerprint, str(row[1])) or not secrets.compare_digest(
                expected_fingerprint, fingerprint_hex.lower()
            ):
                raise ValueError("设备提交的新版 Find 身份指纹不匹配。")
            expected_public = KeyPair(
                find_public_only.derive_private_scalar(master_scalar, key, slot).to_bytes(28, "big")
            ).adv_key_bytes
            if not secrets.compare_digest(expected_public, bytes.fromhex(public_key_hex)):
                raise ValueError("设备提交的派生公钥与本机身份不匹配。")
            metadata = json.loads(str(row[3]))
            if current and self.identities[current]["deviceId"] != metadata["deviceId"]:
                raise ValueError("待确认的 Find 身份与当前设备不匹配。")
            fingerprint = self._remember_public_only_identity(
                master_scalar, key, metadata["findNote"], metadata["deviceName"],
                metadata["bluetoothName"], metadata["deviceId"],
            )
            if current:
                previous = self.identities[current]
                self.identities[fingerprint]["trackingEnabled"] = previous["trackingEnabled"]
                previous["trackingEnabled"] = False
            for old_fp in self._device_fingerprints(fingerprint):
                if old_fp != fingerprint:
                    self.identities[old_fp]["trackingEnabled"] = False
            self._select_identity(fingerprint)
            self._save_identity_registry()
            with self.identity_database:
                self.identity_database.execute(
                    "DELETE FROM find_owner_rotations WHERE rotation_id=?", (transaction_id.lower(),)
                )
            return {**self.state_payload(), "verifiedKeyFingerprint": fingerprint}

    async def abort_public_only_owner(self, transaction_id: str) -> dict[str, Any]:
        if not re.fullmatch(r"[0-9a-fA-F]{32}", transaction_id):
            raise ValueError("Find 身份事务编号无效。")
        async with self.lock:
            with self.identity_database:
                self.identity_database.execute(
                    "DELETE FROM find_owner_rotations WHERE rotation_id=?",
                    (transaction_id.lower(),),
                )
            return self.state_payload()

    async def recover_public_only_owner(
        self, fingerprint_hex: str, slot: int, public_key_hex: str,
        device_id: str,
    ) -> dict[str, Any]:
        """Resolve a lost COMMIT reply without writing to the device again."""
        fingerprint = str(fingerprint_hex).strip().lower()
        if not re.fullmatch(r"[0-9a-f]{32}", fingerprint):
            raise ValueError("新版 Find 身份指纹无效。")
        expected_device_id = self._clean_device_id(device_id)
        if not expected_device_id:
            raise ValueError("恢复身份需要稳定设备标识。")
        async with self.lock:
            if fingerprint in self.identities:
                if self.identities[fingerprint]["deviceId"] != expected_device_id:
                    raise ValueError("Find 身份与当前设备不匹配。")
                transaction_id = ""
            else:
                row = self.identity_database.execute(
                    """
                    SELECT rotation_id, prepared_metadata FROM find_owner_rotations
                     WHERE replacement_fingerprint=?
                    """,
                    (fingerprint,),
                ).fetchone()
                if row is None or json.loads(str(row[1])).get("deviceId") != expected_device_id:
                    raise ValueError("本机没有此设备待确认的新版 Find 身份。")
                transaction_id = str(row[0])
        if transaction_id:
            return await self.finalize_public_only_owner(
                transaction_id, slot, public_key_hex, fingerprint_hex
            )
        return await self.verify_identity_proof(fingerprint, slot, public_key_hex)

    async def update_identity(
        self,
        fingerprint: str,
        *,
        find_note: Any = None,
        device_name: Any = None,
        bluetooth_name: Any = None,
        device_id: Any = None,
        device_scope: bool = False,
    ) -> dict[str, Any]:
        normalized = str(fingerprint).lower()
        async with self.lock:
            if normalized not in self.identities:
                raise ValueError("需要更新的 Find 身份不存在。")
            updates: dict[str, str] = {}
            if device_id is not None:
                updates["deviceId"] = self._clean_device_id(device_id)
            if find_note is not None:
                updates["findNote"] = self._clean_identity_name(
                    find_note, "Find 备注", allow_empty=False
                )
            if device_name is not None:
                updates["deviceName"] = self._clean_identity_name(device_name, "设备名称")
            if bluetooth_name is not None:
                updates["bluetoothName"] = self._clean_identity_name(
                    bluetooth_name, "蓝牙名称"
                )
            if device_scope and set(updates) - {"findNote"}:
                raise ValueError("设备备注操作不能更改身份绑定。")
            targets = self._device_fingerprints(normalized, device_scope)
            previous = {fp: dict(self.identities[fp]) for fp in targets}
            try:
                for fp in targets:
                    self.identities[fp].update(updates)
                self._save_identity_registry()
            except Exception:
                self.identities.update(previous)
                raise
            return self.state_payload()

    async def delete_identity(self, fingerprint: str, confirmed: bool) -> dict[str, Any]:
        if not confirmed:
            raise ValueError("请先确认删除本机 Find 身份。")
        normalized = str(fingerprint).lower()
        async with self.lock:
            if normalized not in self.identities:
                raise ValueError("需要删除的 Find 身份不存在。")
            deleted = self.identities[normalized]
            selected_changed = normalized == self.selected_key_fingerprint
            if selected_changed:
                remaining = [
                    (candidate_fingerprint, entry)
                    for candidate_fingerprint, entry in self.identities.items()
                    if candidate_fingerprint != normalized
                ]
                same_device = [
                    item for item in remaining
                    if deleted["deviceId"] and item[1]["deviceId"] == deleted["deviceId"]
                ]
                candidates = same_device or remaining
                if candidates:
                    replacement_fingerprint = max(
                        candidates,
                        key=lambda item: str(item[1]["createdAt"] or ""),
                    )[0]
                    self._select_identity(replacement_fingerprint)
                else:
                    self.selected_key_fingerprint = ""

            del self.identities[normalized]
            self.identity_seeds.pop(normalized, None)
            with self.identity_database:
                self.identity_database.execute(
                    "DELETE FROM find_track_points WHERE key_fingerprint=?",
                    (normalized,),
                )
                self.identity_database.execute(
                    "DELETE FROM find_identities WHERE key_fingerprint=?",
                    (normalized,),
                )
                self.identity_database.execute(
                    """
                    INSERT INTO find_settings(name, value) VALUES ('selectedKeyFingerprint', ?)
                    ON CONFLICT(name) DO UPDATE SET value=excluded.value
                    """,
                    (self.selected_key_fingerprint,),
                )
            self.identity_database.execute("PRAGMA wal_checkpoint(TRUNCATE)")
            self._secure_identity_database_files()
            return {
                **self.state_payload(),
                "deletedKeyFingerprint": normalized,
                "selectedIdentityChanged": selected_changed,
            }

    async def configure_tracking(
        self,
        fingerprint: str,
        enabled: bool,
        interval_minutes: Any = None,
    ) -> dict[str, Any]:
        normalized = str(fingerprint).lower()
        if interval_minutes is not None:
            try:
                interval = int(interval_minutes)
            except (TypeError, ValueError) as error:
                raise ValueError("轨迹查询间隔无效。") from error
            if not MIN_TRACKING_INTERVAL_MINUTES <= interval <= MAX_TRACKING_INTERVAL_MINUTES:
                raise ValueError(
                    f"轨迹查询间隔必须在 {MIN_TRACKING_INTERVAL_MINUTES}–{MAX_TRACKING_INTERVAL_MINUTES} 分钟之间。"
                )
        else:
            interval = self.tracking_interval_minutes
        async with self.lock:
            if normalized not in self.identities:
                raise ValueError("需要配置的 Find 身份不存在。")
            if enabled and self.identities[normalized].get("historyOnly"):
                raise ValueError("仅轨迹资料没有查询密钥，无法开启记录。")
            if enabled and self.identities[normalized]["derivationVersion"] != find_public_only.VERSION:
                raise ValueError("旧版 Find 身份仅保留本机历史；请使用 Find v2 身份。")
            was_enabled = self.identities[normalized]["trackingEnabled"]
            self.identities[normalized]["trackingEnabled"] = bool(enabled)
            if enabled and not was_enabled:
                self.identities[normalized]["lastAutoQueryAt"] = None
                self.identities[normalized]["lastAutoError"] = None
            self.tracking_interval_minutes = interval
            with self.identity_database:
                self.identity_database.execute(
                    """
                    INSERT INTO find_settings(name, value) VALUES ('trackingIntervalMinutes', ?)
                    ON CONFLICT(name) DO UPDATE SET value=excluded.value
                    """,
                    (str(interval),),
                )
            self._save_identity_registry()
            return self.state_payload()

    async def configure_all_tracking(self, enabled: bool) -> dict[str, Any]:
        if type(enabled) is not bool:
            raise ValueError("记录开关无效。")
        async with self.lock:
            current = self._current_fingerprints()
            targets = [fp for fp, e in self.identities.items() if not enabled or
                       (fp in current and not e.get("historyOnly") and e["derivationVersion"] == find_public_only.VERSION)]
            with self.identity_database:
                for fp in targets:
                    self.identity_database.execute("""UPDATE find_identities SET tracking_enabled=?,
                        last_auto_query_at=CASE WHEN tracking_enabled=0 AND ?=1 THEN NULL ELSE last_auto_query_at END,
                        last_auto_error=CASE WHEN tracking_enabled=0 AND ?=1 THEN NULL ELSE last_auto_error END
                        WHERE key_fingerprint=?""", (int(enabled), int(enabled), int(enabled), fp))
            for fp in targets:
                e = self.identities[fp]
                if enabled and not e["trackingEnabled"]:
                    e.update(lastAutoQueryAt=None, lastAutoError=None)
                e["trackingEnabled"] = enabled
            self._secure_identity_database_files()
            return self.state_payload()

    async def tracking_history(
        self,
        fingerprint: str,
        limit: int = 250,
        hours: int = TRACK_HISTORY_WINDOW_HOURS,
        start_at: str | None = None,
        end_at: str | None = None,
        device_scope: bool = False,
    ) -> dict[str, Any]:
        normalized = str(fingerprint).lower()
        bounded_limit = min(30000, max(1, int(limit)))
        requested_hours = int(hours)
        bounded_hours = (
            TRACK_HISTORY_RETENTION_DAYS * 24
            if requested_hours <= 0
            else min(TRACK_HISTORY_RETENTION_DAYS * 24, requested_hours)
        )
        now = datetime.now(UTC)
        retention_start = now - timedelta(days=TRACK_HISTORY_RETENTION_DAYS)

        def parse_boundary(value: str | None, label: str) -> datetime | None:
            if not value:
                return None
            try:
                parsed = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
            except ValueError as error:
                raise ValueError(f"{label}不是有效时间。") from error
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=UTC)
            return parsed.astimezone(UTC)

        requested_start = parse_boundary(start_at, "开始时间")
        requested_end = parse_boundary(end_at, "结束时间")
        if requested_start or requested_end:
            window_start = max(retention_start, requested_start or retention_start)
            window_end = min(now, requested_end or now)
            if window_start > window_end:
                raise ValueError("开始时间必须早于结束时间。")
        else:
            window_start = now - timedelta(hours=bounded_hours)
            window_end = now
        cutoff = window_start.isoformat()
        ceiling = window_end.isoformat()
        async with self.lock:
            if normalized not in self.identities:
                raise ValueError("需要查看的 Find 身份不存在。")
            fingerprints = self._device_fingerprints(normalized, device_scope)
            predicate = 'key_fingerprint IN (' + ','.join('?' for _ in fingerprints) + ')'
            total = int(
                self.identity_database.execute(
                    f"SELECT COUNT(*) FROM find_track_points WHERE {predicate} AND julianday(report_timestamp)>=julianday(?) AND julianday(report_timestamp)<=julianday(?)",
                    (*fingerprints, cutoff, ceiling),
                ).fetchone()[0]
            )
            rows = self.identity_database.execute(
                f"""
                SELECT report_timestamp, received_at, latitude, longitude,
                       horizontal_accuracy, confidence, status, source_slot
                  FROM find_track_points
                 WHERE {predicate}
                   AND julianday(report_timestamp)>=julianday(?)
                   AND julianday(report_timestamp)<=julianday(?)
                 ORDER BY julianday(report_timestamp) DESC, id DESC
                 LIMIT ?
                """,
                (*fingerprints, cutoff, ceiling, bounded_limit),
            ).fetchall()
            return {
                "keyFingerprint": normalized,
                "total": total,
                "windowHours": bounded_hours,
                "windowStart": cutoff,
                "windowEnd": ceiling,
                "retentionDays": TRACK_HISTORY_RETENTION_DAYS,
                "points": [
                    {
                        "timestamp": row[0],
                        "receivedAt": row[1],
                        "latitude": row[2],
                        "longitude": row[3],
                        "horizontalAccuracy": row[4],
                        "confidence": row[5],
                        "status": row[6],
                        "sourceSlot": row[7],
                    }
                    for row in rows
                ],
            }

    async def tracking_map(
        self,
        fingerprint: str,
        provider: str,
        api_key: str,
        hours: int = TRACK_HISTORY_WINDOW_HOURS,
        start_at: str | None = None,
        end_at: str | None = None,
        device_scope: bool = False,
    ) -> dict[str, Any]:
        history = await self.tracking_history(
            fingerprint, 30000, hours, start_at, end_at, device_scope
        )
        points = history["points"]
        sampled = _track_map_points(points)
        provided_key = _track_map_api_key(api_key)
        request_url = _track_map_url(
            provider,
            points,
            api_key=provided_key,
        )
        try:
            async with ClientSession(timeout=ClientTimeout(total=15)) as client:
                async with client.get(request_url, headers={"Accept": "image/*"}) as response:
                    content_type = response.headers.get("Content-Type", "").split(";", 1)[0]
                    image = await response.read()
                    if response.status >= 400 or not content_type.startswith("image/"):
                        raise ValueError(f"{provider} 轨迹地图返回了无效响应（HTTP {response.status}）。")
                    if not image or len(image) > TRACK_MAP_MAX_BYTES:
                        raise ValueError(f"{provider} 轨迹地图图片大小无效。")
        except (ClientError, asyncio.TimeoutError) as error:
            provider_name = {"baidu": "百度", "amap": "高德", "osm": "OpenStreetMap"}.get(provider, provider)
            raise ValueError(f"无法连接{provider_name}轨迹地图服务。") from error
        return {
            "provider": provider,
            "pointCount": len(points),
            "sampledPointCount": len(sampled),
            "imageDataUrl": f"data:{content_type};base64,{base64.b64encode(image).decode('ascii')}",
        }

    async def clear_tracking_history(self, fingerprint: str, confirmed: bool, device_scope: bool = False) -> dict[str, Any]:
        if not confirmed:
            raise ValueError("请先确认清除本机轨迹记录。")
        normalized = str(fingerprint).lower()
        async with self.lock:
            if normalized not in self.identities:
                raise ValueError("需要清除轨迹的 Find 身份不存在。")
            with self.identity_database:
                for fp in self._device_fingerprints(normalized, device_scope):
                    self.identity_database.execute("DELETE FROM find_track_points WHERE key_fingerprint=?", (fp,))
            self._secure_identity_database_files()
            return self.state_payload()

    async def logout(self) -> dict[str, Any]:
        async with self.lock:
            await self._close_account()
            self.session_error = None
            self._remove_saved_session()
            return self.state_payload()

    async def close(self) -> None:
        await self.stop_background_tracking()
        async with self.lock:
            await self._close_account()
            self.identity_database.close()


async def _json_body(request: web.Request, limit: int = MAX_JSON_BYTES) -> dict[str, Any]:
    if request.content_length is not None and request.content_length > limit:
        raise web.HTTPRequestEntityTooLarge(max_size=limit, actual_size=request.content_length)
    raw = await request.read()
    if len(raw) > limit:
        raise web.HTTPRequestEntityTooLarge(max_size=limit, actual_size=len(raw))
    try:
        body = json.loads(raw)
    except Exception as error:
        raise web.HTTPBadRequest(text="请求格式无效。") from error
    if not isinstance(body, dict):
        raise web.HTTPBadRequest(text="请求必须是 JSON 对象。")
    return body


class ParentLifetime:
    """Bind managed Find to the exact live WebUI parent, including abrupt exits."""
    def __init__(self, pid: int):
        if pid <= 0: raise ValueError('Invalid parent PID')
        self.pid, self.handle = pid, None
        if os.name == 'nt':
            from ctypes import wintypes
            self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.kernel.OpenProcess.restype = wintypes.HANDLE
            self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.handle = self.kernel.OpenProcess(0x00100000, False, pid)
            if not self.handle: raise OSError('Managed Find parent is unavailable')

    def alive(self) -> bool:
        return self.kernel.WaitForSingleObject(self.handle, 0) == 258 if self.handle else os.getppid() == self.pid

    def close(self) -> None:
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def create_app(session: QuerySession, port: int = DEFAULT_PORT, *, parent: ParentLifetime | None = None) -> web.Application:
    csrf_token = secrets.token_urlsafe(32)
    allowed_origins = {
        f"http://127.0.0.1:{port}",
        f"http://[::1]:{port}",
        f"http://localhost:{port}",
    }

    @web.middleware
    async def security_headers(request: web.Request, handler: Any) -> web.StreamResponse:
        host_header = request.host.lower()
        host = (
            host_header[1 : host_header.index("]")]
            if host_header.startswith("[") and "]" in host_header
            else host_header.split(":", 1)[0]
        )
        if host not in {"127.0.0.1", "::1", "localhost"}:
            raise web.HTTPMisdirectedRequest(text="仅允许本机访问。")
        if request.method not in {"GET", "HEAD", "OPTIONS"}:
            if request.headers.get("Origin") not in allowed_origins:
                raise web.HTTPForbidden(text="来源校验失败。")
            if not secrets.compare_digest(request.headers.get(CSRF_HEADER, ""), csrf_token):
                raise web.HTTPForbidden(text="请求令牌无效。")
        response = await handler(request)
        response.headers["Cache-Control"] = "no-store, max-age=0"
        response.headers["Pragma"] = "no-cache"
        response.headers["Referrer-Policy"] = "strict-origin-when-cross-origin"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Cross-Origin-Opener-Policy"] = "same-origin"
        response.headers["Content-Security-Policy"] = (
            "default-src 'self'; base-uri 'none'; frame-ancestors 'none'; "
            "form-action 'self'; img-src 'self' data: https://tile.openstreetmap.org; object-src 'none'; "
            "script-src 'self'; style-src 'self'; connect-src 'self'; "
            "frame-src https://api.map.baidu.com https://map.baidu.com "
            "https://uri.amap.com https://ditu.amap.com "
            "https://www.openstreetmap.org"
        )
        return response

    app = web.Application(middlewares=[security_headers], client_max_size=find_backup.HTTP_LIMIT)
    app["session"] = session

    async def startup(_app: web.Application) -> None:
        if parent is not None:
            async def watch_parent():
                while parent.alive():
                    await asyncio.sleep(0.5)
                LOGGER.info('Managed Find parent exited; closing service and authentication worker')
                raise web.GracefulExit()
            _app['parent_watcher'] = asyncio.create_task(watch_parent())
        await session.restore_saved_session()
        session.start_background_tracking()

    async def asset(_request: web.Request, filename: str, content_type: str) -> web.Response:
        return web.Response(
            text=(ASSET_DIR / filename).read_text(encoding="utf-8"),
            content_type=content_type,
        )

    async def index(request: web.Request) -> web.Response:
        return await asset(request, "query_webui.html", "text/html")

    async def script(request: web.Request) -> web.Response:
        return await asset(request, "query_webui.js", "application/javascript")

    async def stylesheet(request: web.Request) -> web.Response:
        return await asset(request, "query_webui.css", "text/css")

    async def status(_request: web.Request) -> web.Response:
        return web.json_response({"ok": True, "csrfToken": csrf_token, **session.state_payload()})

    async def health(_request: web.Request) -> web.Response:
        logged_in = session.account is not None and session.account.login_state == LoginState.LOGGED_IN
        return web.json_response(dict(ok=True, authBackend=session.auth_backend,
            loggedIn=logged_in, queryReady=bool(logged_in and _find_query_ready(session.account)),
            sessionRestored=session.session_restored, sessionError=session.session_error))

    async def run_action(request: web.Request, action: Any) -> web.Response:
        try:
            limit = find_backup.HTTP_LIMIT if request.path in {"/api/backup/export", "/api/backup/import"} else MAX_JSON_BYTES
            payload = await action(await _json_body(request, limit))
            return web.json_response({"ok": True, **payload})
        except web.HTTPException:
            raise
        except ValueError as error:
            return web.json_response({"ok": False, "error": str(error)}, status=400)
        except PermissionError as error:
            return web.json_response({"ok": False, "error": str(error)}, status=401)
        except Exception as error:
            message = _safe_error(error)
            phase = {"/api/login": "login", "/api/2fa/request": "request-code",
                     "/api/2fa/submit": "submit-code", "/api/query": "query"}.get(request.path, "local-operation")
            LOGGER.warning("Find request failed phase=%s reason=%s", phase, message)
            return web.json_response({"ok": False, "error": message}, status=502)

    async def login(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.login(
                str(body.get("email", "")).strip(),
                str(body.get("password", "")),
                body.get("consent") is True,
            )

        return await run_action(request, action)

    async def request_2fa(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.request_second_factor(int(body.get("index", -1)))

        return await run_action(request, action)

    async def submit_2fa(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.submit_second_factor(
                int(body.get("index", -1)), str(body.get("code", "")).strip()
            )

        return await run_action(request, action)

    async def query(request: web.Request) -> web.Response:
        return await run_action(
            request,
            lambda body: session.query(
                fingerprint=str(body.get("fingerprint", "")).strip() or None,
                save_history=body.get("saveHistory") is True,
                query_window=str(body.get("queryWindow", "latest")),
            ),
        )

    async def logout(request: web.Request) -> web.Response:
        return await run_action(request, lambda _body: session.logout())

    async def export_portable_backup(request: web.Request) -> web.Response:
        return await run_action(request, lambda body: session.export_portable_backup(body.get("password", ""),
            body.get("includeHistory", True), body.get("fingerprints"), body.get("includeKeys", True),
            body.get("encrypt", True), body.get("plaintextConfirmed", False)))

    async def import_portable_backup(request: web.Request) -> web.Response:
        return await run_action(request, lambda body: session.import_portable_backup(
            body.get("encryptedBackup", ""), body.get("password", ""), body.get("allowPlaintext") is True))

    async def import_local_v2_identity(request: web.Request) -> web.Response:
        return await run_action(request, lambda body: session.import_local_v2_identity(
            str(body.get("sourceDirectory", "")), str(body.get("fingerprint", "")),
            str(body.get("deviceId", "")), str(body.get("bluetoothName", "")),
        ))

    async def select_identity(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.select_identity(str(body.get("fingerprint", "")))

        return await run_action(request, action)

    async def verify_identity_proof(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.verify_identity_proof(
                str(body.get("fingerprint", "")), int(body.get("slot", -1)),
                str(body.get("publicKeyHex", "")),
            )

        return await run_action(request, action)

    async def prove_owner_action(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, str]:
            return await session.prove_owner_action(
                str(body.get("fingerprint", "")), str(body.get("deviceIdHex", "")),
                str(body.get("nonceHex", "")), str(body.get("actionHashHex", "")),
            )

        return await run_action(request, action)

    async def update_identity(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.update_identity(
                str(body.get("fingerprint", "")),
                find_note=body.get("findNote"),
                device_name=body.get("deviceName"),
                bluetooth_name=body.get("bluetoothName"),
                device_id=body.get("deviceId"),
                device_scope=body.get("deviceScope") is True,
            )

        return await run_action(request, action)

    async def prepare_public_only_owner(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.prepare_public_only_owner(
                str(body.get("nonceHex", "")),
                find_note=str(body.get("findNote", "")),
                device_name=str(body.get("deviceName", "")),
                bluetooth_name=str(body.get("bluetoothName", "")),
                device_id=str(body.get("deviceId", "")),
            )

        return await run_action(request, action)

    async def prepare_public_only_replacement(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.prepare_public_only_replacement(
                str(body.get("fingerprintHex", "")),
                str(body.get("nonceHex", "")), str(body.get("deviceId", "")),
            )

        return await run_action(request, action)

    async def finalize_public_only_owner(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.finalize_public_only_owner(
                str(body.get("transactionId", "")), int(body.get("slot", -1)),
                str(body.get("publicKeyHex", "")), str(body.get("fingerprintHex", "")),
            )

        return await run_action(request, action)

    async def abort_public_only_owner(request: web.Request) -> web.Response:
        return await run_action(
            request, lambda body: session.abort_public_only_owner(str(body.get("transactionId", "")))
        )

    async def recover_public_only_owner(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.recover_public_only_owner(
                str(body.get("fingerprintHex", "")), int(body.get("slot", -1)),
                str(body.get("publicKeyHex", "")), str(body.get("deviceId", "")),
            )

        return await run_action(request, action)

    async def delete_identity(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.delete_identity(
                str(body.get("fingerprint", "")), body.get("confirm") is True
            )

        return await run_action(request, action)

    async def configure_tracking(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.configure_tracking(
                str(body.get("fingerprint", "")),
                body.get("enabled") is True,
                body.get("intervalMinutes"),
            )

        return await run_action(request, action)

    async def tracking_history(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.tracking_history(
                str(body.get("fingerprint", "")),
                int(body.get("limit", 250)),
                int(body.get("hours", TRACK_HISTORY_WINDOW_HOURS)),
                body.get("startAt"),
                body.get("endAt"),
                body.get("deviceScope") is True,
            )

        return await run_action(request, action)

    async def tracking_map(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.tracking_map(
                str(body.get("fingerprint", "")),
                str(body.get("provider", "")),
                str(body.get("apiKey", "")),
                int(body.get("hours", TRACK_HISTORY_WINDOW_HOURS)),
                body.get("startAt"),
                body.get("endAt"),
                body.get("deviceScope") is True,
            )

        return await run_action(request, action)

    async def clear_tracking_history(request: web.Request) -> web.Response:
        async def action(body: dict[str, Any]) -> dict[str, Any]:
            return await session.clear_tracking_history(
                str(body.get("fingerprint", "")), body.get("confirm") is True,
                body.get("deviceScope") is True,
            )

        return await run_action(request, action)

    async def cleanup(_app: web.Application) -> None:
        watcher = _app.get('parent_watcher')
        if watcher is not None:
            watcher.cancel()
            await asyncio.gather(watcher, return_exceptions=True)
        try:
            await session.close()
        finally:
            if parent is not None: parent.close()

    async def configure_all_tracking(request: web.Request) -> web.Response:
        return await run_action(request, lambda body: session.configure_all_tracking(body.get("enabled")))

    app.router.add_get("/", index)
    app.router.add_get("/query_webui.js", script)
    app.router.add_get("/query_webui.css", stylesheet)
    app.router.add_get("/api/status", status)
    app.router.add_get("/api/health", health)
    app.router.add_post("/api/login", login)
    app.router.add_post("/api/2fa/request", request_2fa)
    app.router.add_post("/api/2fa/submit", submit_2fa)
    app.router.add_post("/api/query", query)
    app.router.add_post("/api/logout", logout)
    app.router.add_post("/api/backup/export", export_portable_backup)
    app.router.add_post("/api/backup/import", import_portable_backup)
    app.router.add_post("/api/identity/import-local-v2", import_local_v2_identity)
    app.router.add_post("/api/identity/select", select_identity)
    app.router.add_post("/api/identity/proof/verify", verify_identity_proof)
    app.router.add_post("/api/identity/owner-action/prove", prove_owner_action)
    app.router.add_post("/api/identity/update", update_identity)
    app.router.add_post("/api/identity/public-only/prepare", prepare_public_only_owner)
    app.router.add_post("/api/identity/public-only/replace/prepare", prepare_public_only_replacement)
    app.router.add_post("/api/identity/public-only/finalize", finalize_public_only_owner)
    app.router.add_post("/api/identity/public-only/abort", abort_public_only_owner)
    app.router.add_post("/api/identity/public-only/recover", recover_public_only_owner)
    app.router.add_post("/api/identity/delete", delete_identity)
    app.router.add_post("/api/tracking/configure", configure_tracking)
    app.router.add_post("/api/tracking/configure-all", configure_all_tracking)
    app.router.add_post("/api/tracking/history", tracking_history)
    app.router.add_post("/api/tracking/map", tracking_map)
    app.router.add_post("/api/tracking/clear", clear_tracking_history)
    app.on_startup.append(startup)
    app.on_cleanup.append(cleanup)
    return app


def _loopback_host(value: str) -> str:
    if value == "localhost":
        return value
    try:
        if ipaddress.ip_address(value).is_loopback:
            return value
    except ValueError:
        pass
    raise argparse.ArgumentTypeError("query WebUI must bind to a loopback address")


def default_auth_backend() -> str:
    return {"win32": "win-emulated", "darwin": "mac-native"}.get(sys.platform, "local")


async def create_backend_account(backend: str, state_dir: Path, cache_dir: Path,
                                 *, state: dict | None = None, provision: bool = False):
    from anisette_backends import VerificationAnisetteProvider
    provider = VerificationAnisetteProvider(backend, state_dir,
        engine_dir=cache_dir / "upstream", coreadi_path=cache_dir / "apple/CoreADI.x86_64")
    try:
        await provider.prepare(provision=provision)
        if state is not None:
            if (state.get("anisette") != provider.to_json()
                    or state.get("ids") != {"uid": provider.uid, "devid": provider.device_id}
                    or state.get("account", {}).get("password") is not None):
                raise ValueError("认证会话与本机或后端不匹配。")
        account = QueryAppleAccount(provider, state_info=state)
        account._uid, account._devid = provider.uid, provider.device_id
        await account._http.close()
        account._http = _AppleAuthenticationHttp(auth_client=provider.client,
            mobileme_client=provider.client.rsplit(" <", 1)[0] + " <com.apple.AOSKit/282 (com.apple.accountsd/113)>")
        _install_apple_gateway_resolver(account)
        return account
    except BaseException:
        await provider.close()
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--anisette-libs", required=True, type=Path)
    parser.add_argument("--auth-backend", choices=["local", "win-emulated", "mac-native"], default=default_auth_backend())
    parser.add_argument("--auth-state-dir", type=Path)
    parser.add_argument("--parent-pid", type=int, help="Managed service owner; exit when it exits.")
    parser.add_argument("--auth-cache-dir", type=Path)
    parser.add_argument(
        "--session-store",
        type=Path,
        help="encrypted session path (default: beside --anisette-libs)",
    )
    parser.add_argument("--host", default=DEFAULT_HOST, type=_loopback_host)
    parser.add_argument("--port", default=DEFAULT_PORT, type=int)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
    )
    logging.getLogger("findmy").setLevel(logging.ERROR)
    logging.getLogger("anisette").setLevel(logging.ERROR)
    anisette_libs = args.anisette_libs.resolve()
    session_store = args.session_store.resolve() if args.session_store is not None else (
        anisette_libs.parent
        / (("account-session.dpapi" if os.name == "nt" else "account-session.encrypted")
           if args.auth_backend == "local" else f"account-session.{args.auth_backend}.encrypted")
    )
    parent = ParentLifetime(args.parent_pid) if args.parent_pid is not None else None
    session = QuerySession(
        anisette_libs,
        session_store,
        auth_backend=args.auth_backend,
        auth_state_dir=args.auth_state_dir.resolve() if args.auth_state_dir else None,
        auth_cache_dir=args.auth_cache_dir.resolve() if args.auth_cache_dir else None,
    )
    print(f"InkTag Find local WebUI: http://{args.host}:{args.port}")
    protection = "Windows DPAPI" if os.name == "nt" else "AES-256 local file key"
    print(f"Local-only service; {protection} session store: {session_store}")
    print("The Apple account password and Find derivation seeds never leave this host.")
    web.run_app(
        create_app(session, args.port, parent=parent),
        host=args.host,
        port=args.port,
        access_log=None,
        print=None,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
