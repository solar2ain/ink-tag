"""Password pilot session; secrets are never returned in command results."""
from __future__ import annotations

import asyncio
from contextvars import ContextVar
import os
import sqlite3
import struct
import time
import zlib
from pathlib import Path
from qrcode.constants import ERROR_CORRECT_L

from .device_password import (build_password_qr_link, decode_status, device_id_bytes,
    encode_password_bundle, password_change_key, password_credential, password_proof,
    RESET_SETTINGS_PROOF_DATA)
from .device_link import build_device_link
from .device_qr import encode_device_qr
from .generated_commands_v2 import CapabilityV2, CommandV2, TransferObjectV2


class PasswordAuthorizationRejected(ValueError):
    """An explicit AUTH rejection, not a transport failure or lost response."""


_cache_enabled = ContextVar('inktag_credential_cache_enabled', default=True)


def set_credential_cache_enabled(enabled: bool) -> None:
    """CLI-invocation policy; no secrets are carried in this setting."""
    _cache_enabled.set(bool(enabled))


def credential_cache_enabled() -> bool:
    return _cache_enabled.get() and os.environ.get('INKTAG_NO_CREDENTIAL_CACHE') != '1'


class PasswordCredentialStore:
    """Local bearer V only, isolated from Find/export collections. No plaintext/W."""
    def __init__(self, directory: Path):
        self.path = directory.resolve() / "device-passwords.sqlite3"
        self.persistent = credential_cache_enabled()
        self._memory: dict[str, bytes] = {}

    def get(self, device_id: str) -> bytes | None:
        device_id_bytes(device_id)
        if not self.persistent:
            return self._memory.get(device_id.upper())
        if not self.path.exists():
            return None
        try:
            with sqlite3.connect(self.path) as database:
                row = database.execute("SELECT credential FROM credentials WHERE device_id=?",
                                       (device_id.upper(),)).fetchone()
        except sqlite3.Error as error:
            raise ValueError("Local password credential cache is unreadable") from error
        return bytes(row[0]) if row and len(row[0]) == 32 else None

    def put(self, device_id: str, credential: bytes | None) -> None:
        device_id_bytes(device_id)
        if credential is not None and (len(credential) != 32 or not any(credential)):
            raise ValueError("Invalid device password credential")
        if not self.persistent:
            if credential is None:
                self._memory.pop(device_id.upper(), None)
            else:
                self._memory[device_id.upper()] = bytes(credential)
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.touch(mode=0o600, exist_ok=True)
        self.path.chmod(0o600)
        with sqlite3.connect(self.path) as database:
            database.execute("CREATE TABLE IF NOT EXISTS credentials "
                             "(device_id TEXT PRIMARY KEY, credential BLOB NOT NULL)")
            if credential is None:
                database.execute("DELETE FROM credentials WHERE device_id=?", (device_id.upper(),))
            else:
                database.execute("INSERT OR REPLACE INTO credentials VALUES (?, ?)",
                                 (device_id.upper(), credential))
        # Windows uses the containing user's ACL; POSIX adds owner-only access.
        self.path.chmod(0o600)


class DevicePasswordSession:
    def _password_init(self, store=None, prompt=None):
        self.password_store = store
        self.password_prompt = prompt
        self.password_status = None
        self._password_lock = asyncio.Lock()
        self._password_next_attempt = 0.0

    async def read_password_status(self):
        if not self.supports_capability(CapabilityV2.DEVICE_PASSWORD):
            return None
        self.password_status = decode_status(await self.request_v2(CommandV2.GET_DEVICE_PASSWORD_STATUS))
        self._password_next_attempt = max(self._password_next_attempt,
            time.monotonic() + self.password_status["retryAfterMs"] / 1000)
        return self.password_status

    def _password_id(self):
        identifier = self.snapshot.activation.activation_id
        device_id_bytes(identifier)
        return identifier.upper()

    async def _password_prove(self, key, purpose=0, bundle=None):
        if time.monotonic() < self._password_next_attempt:
            raise ValueError("Device password retry is throttled; wait before trying again")
        self._password_next_attempt = time.monotonic() + 1.0
        if purpose == 0 and self.password_status:
            self.password_status = {**self.password_status, "authorized": False}
        challenge = await self.request_v2(CommandV2.GET_DEVICE_PASSWORD_CHALLENGE, bytes((purpose,)))
        if len(challenge) != 22 or challenge[1] != purpose:
            raise ValueError("Device returned a mismatched password challenge")
        try:
            self.password_status = decode_status(await self.request_v2(CommandV2.AUTHORIZE_DEVICE_PASSWORD,
                password_proof(self._password_id(), key, challenge, bundle)))
            if not self.password_status["authorized"]:
                raise PasswordAuthorizationRejected("Device password authorization was not confirmed")
        except Exception as error:
            if purpose == 0 and self.password_status:
                self.password_status = {**self.password_status, "authorized": False}
            if (getattr(error, "status", None) == 0x0D and
                    getattr(error, "opcode", None) == int(CommandV2.AUTHORIZE_DEVICE_PASSWORD)):
                # Only an explicit rejection permits the interactive retry.
                # Use the device's actual cooldown; do not invent 30 seconds
                # for one typo or retry an ambiguous transport failure.
                try:
                    await self.read_password_status()
                except Exception:
                    raise error
                if purpose == 0 and self.password_status:
                    self.password_status = {**self.password_status, "authorized": False}
                raise PasswordAuthorizationRejected("Device password was rejected") from None
            raise

    async def authenticate_password(self, password=None, *, credential=None):
        async with self._password_lock:
            status = await self.read_password_status()
            if status is None or not status["enabled"]:
                return status
            key = credential if credential is not None else password_credential(self._password_id(), password)
            await self._password_prove(key)
            if self.password_store:
                self.password_store.put(self._password_id(), key)
            return self.password_status

    async def _password_connect(self):
        status = await self.read_password_status()
        # A browser/OS may retain the physical LL session across logical CLI
        # connects. Authorization on that link is not this client's credential.
        if status is None or not status["enabled"]:
            return
        self.password_status = {**status, "authorized": False}
        key = self.password_store.get(self._password_id()) if self.password_store else None
        if status["retryAfterMs"] and status["retryAfterMs"] <= 5000:
            await asyncio.sleep(status["retryAfterMs"] / 1000 + 0.05)
        if getattr(self, "password_force_prompt", False) and self.password_prompt:
            await self.authenticate_password(self.password_prompt())
        elif key:
            try:
                await self.authenticate_password(credential=key)
            except PasswordAuthorizationRejected:
                if not self.password_prompt:
                    raise
                password = self.password_prompt()
                if not password:
                    raise
                # One explicit user retry only. Keep both client and device
                # throttles; transport failures never trigger another proof.
                await asyncio.sleep(max(0, self._password_next_attempt - time.monotonic()))
                status = await self.read_password_status()
                if status["retryAfterMs"]:
                    raise ValueError("Device password retry is throttled; wait before trying again")
                await self.authenticate_password(password)
        elif self.password_prompt:
            password = self.password_prompt()
            if password:
                await self.authenticate_password(password)

    async def show_password_device_info(self):
        return decode_status(await self.request_v2(CommandV2.SHOW_PASSWORD_DEVICE_INFO))

    async def _password_authorize_settings_reset(self, old_password):
        """Caller holds the password lock through SYSTEM_ACTION; no mutation here."""
        status = await self.read_password_status()
        if status is None or not status["enabled"]:
            return
        if not old_password:
            raise ValueError("Clearing settings requires the original password")
        identifier = self._password_id()
        if not status["authorized"]:
            await self._password_prove(password_credential(identifier, old_password))
        await asyncio.sleep(max(0, self._password_next_attempt - time.monotonic()))
        await self._password_prove(password_change_key(identifier, old_password),
                                   2, RESET_SETTINGS_PROOF_DATA)

    async def configure_password(self, new_password, *, old_password=None):
        async with self._password_lock:
            status = await self.read_password_status()
            if status is None:
                raise ValueError("Firmware does not advertise device password support")
            identifier = self._password_id()
            if status["enabled"] and not old_password:
                raise ValueError("Changing/disabling requires the original password")
            if status["enabled"] and not status["authorized"]:
                await self._password_prove(password_credential(identifier, old_password))
                await asyncio.sleep(1.05)
            key = password_credential(identifier, new_password) if new_password is not None else None
            change = password_change_key(identifier, new_password) if new_password is not None else None
            link = build_password_qr_link(self.snapshot.device_name or self.snapshot.name, identifier, key) if key else build_device_link(self.snapshot.device_name or self.snapshot.name)
            qr = encode_device_qr(link, error_correction=ERROR_CORRECT_L)
            revision = status["revision"] + 1 if status["enabled"] else 1
            bundle = encode_password_bundle(key, qr.modules, qr.data, change_key=change, revision=revision)
            # Fresh W proof is independent of any existing V authorization.
            if status["enabled"]:
                if time.monotonic() < self._password_next_attempt:
                    await asyncio.sleep(self._password_next_attempt - time.monotonic())
                await self._password_prove(password_change_key(identifier, old_password),
                    1 if key else 2, bundle)
            descriptor = bytes((int(TransferObjectV2.DEVICE_PASSWORD_BUNDLE), 0, 1, 0)) + struct.pack(
                "<IIII", len(bundle), zlib.crc32(bundle), len(bundle), 0)
            transaction = self._next_v2_transaction()
            active = False
            commit_sent = False
            try:
                reply = await self.request_v2(CommandV2.TRANSFER_BEGIN, descriptor, transaction_id=transaction)
                active = True
                chunk = min(self._v2_write_payload, struct.unpack_from("<H", reply, 2)[0] if len(reply) >= 4 else self._v2_write_payload)
                if chunk < 1:
                    raise ValueError("Invalid password transfer chunk size")
                for offset in range(0, len(bundle), chunk):
                    await self._write_v2_transfer_chunk(transaction_id=transaction, object_type=7,
                        total_size=len(bundle), offset=offset, data=bundle[offset:offset+chunk])
                commit_sent = True
                await self.request_v2(CommandV2.TRANSFER_COMMIT, transaction_id=transaction, timeout=15)
                active = False
                confirmed = await self.read_password_status()
                if confirmed["enabled"] != bool(key) or (key and confirmed["revision"] != revision):
                    raise ValueError("Password commit was not confirmed; reconnect with the intended password")
                # Even a valid status cannot attest V; prove new V before replacing local material.
                if key:
                    if time.monotonic() < self._password_next_attempt:
                        await asyncio.sleep(self._password_next_attempt - time.monotonic())
                    await self._password_prove(key)
                if self.password_store:
                    self.password_store.put(identifier, key)
                return self.password_status
            except Exception:
                if active and not commit_sent:
                    await self.request_v2(CommandV2.TRANSFER_ABORT, transaction_id=transaction)
                # Never retry an ambiguous commit or replace saved V without new-key proof.
                raise
