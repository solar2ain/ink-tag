"""Local Mac-native / Windows Mac-CoreADI machine authentication providers.

Used by the service and isolated verifier. No account passwords enter the worker.
"""
from __future__ import annotations

import asyncio
import base64
import binascii
import hashlib
import json
import logging
import sys
import time
from pathlib import Path

from findmy.reports.anisette import BaseAnisetteProvider

LOGGER = logging.getLogger(__name__)

class BackendVerificationError(RuntimeError):
    """Only fixed, non-confidential worker errors are exposed."""

    def __init__(self, message: str, *, code: str | None = None):
        super().__init__(message)
        self.code = code


class VerificationAnisetteProvider(BaseAnisetteProvider):
    def __init__(self, backend: str, state_dir: Path, *, engine_dir: Path | None = None,
                 coreadi_path: Path | None = None) -> None:
        super().__init__()
        if backend not in {"mac-native", "win-emulated"}:
            raise ValueError("Unsupported verification backend")
        self.backend = backend
        self.state_dir = state_dir.resolve()
        self.engine_dir = engine_dir.resolve() if engine_dir else None
        self.coreadi_path = coreadi_path.resolve() if coreadi_path else None
        self._headers: dict[str, str] = {}
        self._lock = asyncio.Lock()
        self._worker_lock = asyncio.Lock()
        self._process: asyncio.subprocess.Process | None = None
        self._closed = False

    async def worker(self, action: str, *, challenge: str | None = None) -> dict:
        async with self._worker_lock:
            return await self._worker(action, challenge=challenge)

    async def _stop_worker(self) -> None:
        process, self._process = self._process, None
        if process is not None:
            if process.returncode is None:
                process.kill()
            await process.wait()

    async def _worker(self, action: str, *, challenge: str | None = None) -> dict:
        if self._closed:
            raise BackendVerificationError("认证工作进程已关闭，请重新建立会话。")
        if action not in {"identity", "headers", "provision", "resync"}:
            raise ValueError("Unknown authentication worker action")
        if action == "resync":
            if self.backend != "win-emulated" or not isinstance(challenge, str) or not 1 <= len(challenge) <= 16384:
                raise BackendVerificationError("本机认证重同步挑战不可用。")
            try:
                if not base64.b64decode(challenge, validate=True):
                    raise ValueError
            except (ValueError, binascii.Error):
                raise BackendVerificationError("本机认证重同步挑战无效。") from None
        elif challenge is not None:
            raise ValueError("Unexpected authentication challenge")
        persistent = self.backend == "win-emulated"
        reused = persistent and self._process is not None and self._process.returncode is None
        started = time.monotonic()
        command = [sys.executable, str(Path(__file__).with_name("mac_adi_worker.py")),
                   "serve" if persistent else action, "--backend", self.backend, "--state-dir", str(self.state_dir)]
        if self.engine_dir is not None:
            command.extend(["--engine-dir", str(self.engine_dir)])
        if self.coreadi_path is not None:
            command.extend(["--coreadi", str(self.coreadi_path)])
        process = None
        try:
            async with asyncio.timeout(85):
                if persistent:
                    if not reused:
                        await self._stop_worker()
                        self._process = await asyncio.create_subprocess_exec(
                            *command, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                            stderr=asyncio.subprocess.DEVNULL, limit=64 * 1024)
                    process = self._process
                    message = json.dumps({"action": action, "challenge": challenge}) if action == "resync" else action
                    process.stdin.write((message + "\n").encode("ascii"))
                    await process.stdin.drain()
                    stdout = await process.stdout.readline()
                else:
                    process = await asyncio.create_subprocess_exec(
                        *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
                    stdout, _stderr = await process.communicate()
                result = self._decode_result(stdout, process.returncode)
            LOGGER.info("auth worker action=%s reused=%s duration_ms=%d", action, bool(reused),
                        round((time.monotonic() - started) * 1000))
            return result
        except BaseException:
            if persistent:
                await self._stop_worker()
            elif process is not None:
                if process.returncode is None:
                    process.kill()
                await process.wait()
            # Never replay a request with an unknown outcome or reuse old OTPs.
            raise

    @staticmethod
    def _decode_result(stdout: bytes, returncode: int | None) -> dict:
        if len(stdout) > 64 * 1024:
            raise BackendVerificationError("认证工作进程响应过大。")
        try:
            result = json.loads(stdout)
        except (ValueError, UnicodeError):
            raise BackendVerificationError("认证工作进程未返回有效结果。") from None
        if returncode not in (None, 0) or not isinstance(result, dict) or not result.get("ok"):
            # Never forward raw stdout/stderr or arbitrary dependency exceptions.
            code = result.get("errorCode") if isinstance(result, dict) else None
            messages = {
                "busy": "另一组 Find 服务正在使用本机认证状态；请停止旧服务后重新启动，无需先重新登录。",
                "platform": "认证后端与当前操作系统不匹配。",
                "dependencies": "认证后端依赖未准备好或固定哈希不匹配。",
                "not-provisioned": "测试机器尚未注册；请显式执行 provision，不会自动重建机器。",
                "throttled": "Apple 返回 HTTP 429；停止尝试并等待至少 45 分钟。",
                "http": "Apple 机器注册请求失败；没有自动重试。",
                "native": "无法从 macOS 原生 AOSKit 获取认证数据。",
                "state": "测试机器状态无效或与原机器不匹配；不会覆盖或重建。",
                "resync": "Apple 本机认证状态重同步未完成；请稍后再试，不要删除机器状态。",
                "resync-cooldown": "本机认证状态刚尝试过重同步；请至少等待一分钟再试。",
            }
            raise BackendVerificationError(messages.get(code, "认证工作进程失败；未输出敏感响应。"),
                                           code=code if code in messages else None)
        return result

    async def prepare(self, *, provision: bool = False) -> None:
        result = await self.worker("provision" if provision else "identity")
        self.identity = result["identity"]
        self.uid = self.identity["uid"]
        self.device_id = self.identity["device_id"]
        self.binding = hashlib.sha256(json.dumps(self.identity, sort_keys=True).encode()).hexdigest()

    @property
    def client(self) -> str:
        return self.identity["client_info"]

    @property
    def otp(self) -> str:
        return self._headers["X-Apple-I-MD"]

    @property
    def machine(self) -> str:
        return self._headers["X-Apple-I-MD-M"]

    async def get_headers(self, user_id: str, device_id: str, serial: str = "0",
                          with_client_info: bool = False) -> dict[str, str]:
        if user_id != self.uid or device_id != self.device_id:
            raise BackendVerificationError("认证机器身份不匹配；拒绝混用会话。")
        async with self._lock:
            result = await self.worker("headers")
        headers = result["headers"]
        if not all(isinstance(headers.get(key), str) and headers[key]
                   for key in ("X-Apple-I-MD", "X-Apple-I-MD-M", "X-Apple-I-MD-LU",
                               "X-Apple-I-MD-RINFO", "X-Mme-Device-Id")):
            raise BackendVerificationError("认证数据不完整。")
        if headers["X-Mme-Device-Id"] != self.device_id:
            raise BackendVerificationError("认证机器身份已改变；拒绝使用旧令牌。")
        self._headers = headers
        # Each request gets one coherent OTP/MID snapshot from the same worker.
        headers = dict(headers)
        headers["loc"] = headers.get("X-Apple-Locale", "en_US")
        if with_client_info:
            headers["X-Apple-App-Info"] = "com.apple.gs.xcode.auth"
            headers["X-Xcode-Version"] = "11.2 (11B41)"
        return headers

    async def resynchronize(self, challenge: str) -> None:
        # Same serialized machine and encrypted store; no new provisioning or
        # account password is passed into the private worker.
        async with self._lock:
            await self.worker("resync", challenge=challenge)
            self._headers.clear()

    def to_json(self, path=None) -> dict:
        if path is not None:
            raise ValueError("Provider state must be saved inside the encrypted session")
        return {"type": "inktagVerificationAnisette", "backend": self.backend,
                "binding": self.binding}

    @classmethod
    def from_json(cls, *args, **kwargs):
        raise ValueError("Restore with the explicit verification backend and original machine")

    async def close(self) -> None:
        async with self._worker_lock:
            self._closed = True
            await self._stop_worker()
            self._headers.clear()
