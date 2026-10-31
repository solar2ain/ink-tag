"""Private pipe worker for local machine authentication, not a public service.

Provisioning/virtual-store reference: sacca97/os-findmy-e, MIT license retained
in third_party/os-findmy-e.LICENSE. Its unverified transport is not reused.
"""
from __future__ import annotations

import argparse
import asyncio
import base64
import ctypes
import hashlib
import json
import os
import platform
import secrets
import subprocess
import sys
import time
import uuid
from datetime import UTC, datetime
from pathlib import Path
from urllib.parse import urlsplit

from query_webui import _atomic_write, _protect_session, _unprotect_session
from prepare_mac_adi import COREADI_SHA256, digest, verify_engine

STORE = "/Users/Shared/adi/"


class WorkerError(Exception):
    def __init__(self, code: str):
        self.code = code


def native_identity(uid: str) -> dict:
    if sys.platform != "darwin":
        raise WorkerError("platform")
    import plistlib
    values = plistlib.loads(subprocess.run(
        ["/usr/sbin/ioreg", "-a", "-rd1", "-c", "IOPlatformExpertDevice"],
        check=True, capture_output=True, timeout=10,
    ).stdout)[0]
    model = subprocess.run(["/usr/sbin/sysctl", "-n", "hw.model"],
                           check=True, capture_output=True, timeout=5).stdout.decode().strip()
    build = subprocess.run(["/usr/bin/sw_vers", "-buildVersion"],
                           check=True, capture_output=True, timeout=5).stdout.decode().strip()
    hardware = json.loads(subprocess.run(
        ["/usr/sbin/system_profiler", "SPHardwareDataType", "-json"],
        check=True, capture_output=True, timeout=20,
    ).stdout)["SPHardwareDataType"][0]
    # Modern Apple Silicon reports a provisioning UDID distinct from the
    # IOPlatformUUID. Do not label the latter as that UDID when one is present.
    device_id = str(hardware.get("provisioning_UDID") or hardware.get("provisioning_udid")
                    or hardware["platform_UUID"]).upper()
    return {"uid": uid, "device_id": device_id,
            "serial": str(values["IOPlatformSerialNumber"]),
            "host_binding": hashlib.sha256(f"{values['IOPlatformUUID']}:{os.getuid()}".encode()).hexdigest(),
            "client_info": f"<{model}> <Mac OS X;{platform.mac_ver()[0]};{build}> <com.apple.AuthKit/1 (com.apple.akd/1.0)>"}


def native_headers(identity: dict) -> dict[str, str]:
    """Call the same AOSUtilities selector as the upstream native-Mac proof.

    Typed objc_msgSend trampolines work on Intel and Apple Silicon. No pyobjc,
    system-library copying, SIP changes or arbitrary Objective-C calls.
    """
    if sys.platform != "darwin":
        raise WorkerError("platform")
    ctypes.CDLL("/System/Library/Frameworks/Foundation.framework/Foundation")
    ctypes.CDLL("/System/Library/PrivateFrameworks/AOSKit.framework/AOSKit")
    objc = ctypes.CDLL("/usr/lib/libobjc.A.dylib")
    objc.objc_getClass.argtypes = [ctypes.c_char_p]
    objc.objc_getClass.restype = ctypes.c_void_p
    objc.sel_registerName.argtypes = [ctypes.c_char_p]
    objc.sel_registerName.restype = ctypes.c_void_p
    address = ctypes.cast(objc.objc_msgSend, ctypes.c_void_p).value

    def send(receiver, selector: bytes, *arguments):
        types = [ctypes.c_void_p, ctypes.c_void_p] + [ctypes.c_void_p]*len(arguments)
        call = ctypes.CFUNCTYPE(ctypes.c_void_p, *types)(address)
        return call(receiver, objc.sel_registerName(selector), *arguments)

    def string(value: str):
        data = ctypes.c_char_p(value.encode())
        return send(objc.objc_getClass(b"NSString"), b"stringWithUTF8String:", ctypes.cast(data, ctypes.c_void_p))

    pool = send(send(objc.objc_getClass(b"NSAutoreleasePool"), b"alloc"), b"init")
    try:
        utilities = objc.objc_getClass(b"AOSUtilities")
        if not utilities or not send(utilities, b"respondsToSelector:", objc.sel_registerName(b"retrieveOTPHeadersForDSID:")):
            raise WorkerError("native")
        dictionary = send(utilities, b"retrieveOTPHeadersForDSID:", string("-2"))
        if not dictionary:
            raise WorkerError("native")
        result = {}
        for key in ("X-Apple-MD", "X-Apple-MD-M", "X-Apple-I-MD", "X-Apple-I-MD-M", "X-Apple-I-MD-RINFO"):
            value = send(dictionary, b"objectForKey:", string(key))
            if value:
                description = send(value, b"description")
                pointer = send(description, b"UTF8String")
                if pointer:
                    result[key.replace("X-Apple-MD", "X-Apple-I-MD")] = ctypes.string_at(pointer).decode()
        if not result.get("X-Apple-I-MD") or not result.get("X-Apple-I-MD-M"):
            raise WorkerError("native")
        result.setdefault("X-Apple-I-MD-RINFO", "17106176")
        return complete_headers(identity, result)
    finally:
        if pool:
            send(pool, b"drain")


def complete_headers(identity: dict, headers: dict[str, str]) -> dict[str, str]:
    return {"X-Apple-I-Client-Time": datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z"),
            "X-Apple-I-TimeZone": "UTC", "X-Apple-Locale": "en_US",
            "X-Apple-I-MD-LU": base64.b64encode(identity["uid"].encode()).decode(),
            "X-Mme-Device-Id": identity["device_id"],
            "X-Apple-I-SRL-NO": identity["serial"],
            "X-MMe-Client-Info": identity["client_info"], **headers}


def emulated_core(args, state: dict):
    if sys.platform != "win32":
        raise WorkerError("platform")
    if os.environ.get("ADI_FROZEN_TIME") or os.environ.get("COREADI_DISPATCH"):
        raise WorkerError("dependencies")
    if args.engine_dir is None or args.coreadi is None:
        raise WorkerError("dependencies")
    try:
        verify_engine(args.engine_dir)
        if digest(args.coreadi) != COREADI_SHA256:
            raise ValueError("binary hash")
    except (OSError, ValueError):
        raise WorkerError("dependencies") from None
    # Load only pinned emulator core, never upstream provisioning/auth scripts.
    sys.path.insert(0, str(args.engine_dir.resolve()))
    from adi_run import CoreADI
    adi = CoreADI(path=args.coreadi.resolve(), log=False)
    adi.stubs.hostuuid = bytes.fromhex(state["identity"]["host_uuid"])
    adi.harness.register("arc4random", lambda _h: secrets.randbits(32))
    def random_buffer(h):
        target, count = adi.stubs._a(0), adi.stubs._a(1)
        if count > 1024*1024:
            raise WorkerError("state")
        h.mem_write(target, secrets.token_bytes(count))
        return 0
    adi.harness.register("arc4random_buf", random_buffer)
    for name, value in state.get("files", {}).items():
        if name == "adi.pb":
            continue
        if "/" in name or "\\" in name or len(value) > 1024*1024:
            raise WorkerError("state")
        adi.stubs.files[STORE+name] = bytearray(base64.b64decode(value, validate=True))
    if not adi.initialize().ok:
        raise WorkerError("state")
    return adi


def apple_url(value: str) -> str:
    parsed = urlsplit(value)
    host = (parsed.hostname or "").lower()
    if (parsed.scheme != "https" or parsed.username or parsed.password or parsed.port not in (None, 443)
            or not (host == "apple.com" or host.endswith(".apple.com"))):
        raise WorkerError("http")
    return value


async def provision(adi, state: dict) -> None:
    """Bounded Apple-only provisioning, using FindMy's verified Apple trust store."""
    from aiohttp import ClientTimeout
    from findmy.util.http import HttpSession
    import plistlib
    identity = state["identity"]
    headers = {"User-Agent": "akd/1.0 CFNetwork/1404.0.5 Darwin/22.3.0",
               "Content-Type": "application/x-www-form-urlencoded", "Connection": "close",
               "X-Mme-Device-Id": identity["device_id"], "X-MMe-Client-Info": identity["client_info"],
               "X-Apple-I-MD-LU": identity["uid"], "X-Apple-I-SRL-NO": identity["serial"],
               "X-Apple-Client-App-Name": "Setup"}
    http = HttpSession()
    async def request(url: str, body=None):
        response = await http.request("GET" if body is None else "POST", apple_url(url),
            headers=headers, data=body, auto_retry=False, allow_redirects=False,
            timeout=ClientTimeout(total=20, connect=8, sock_read=15))
        if response.status_code == 429:
            state["cooldown_until"] = time.time() + 45*60
            raise WorkerError("throttled")
        if response.status_code != 200:
            raise WorkerError("http")
        return response.plist()
    def envelope(value):
        return plistlib.dumps({"Header": {}, "Request": value})
    try:
        urls = (await request("https://gsa.apple.com/grandslam/GsService2/lookup"))["urls"]
        started = (await request(urls["midStartProvisioning"], envelope({})))["Response"]
        operation = adi.provisioning_start(-2, base64.b64decode(started["spim"], validate=True))
        if not operation.ok:
            raise WorkerError("state")
        headers["X-Apple-I-Client-Time"] = datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")
        finished = (await request(urls["midFinishProvisioning"], envelope({
            "cpim": base64.b64encode(operation.outputs["cpim"]).decode(),
        })))["Response"]
        ended = adi.provisioning_end(operation.outputs["session_id"],
            base64.b64decode(finished["ptm"], validate=True), base64.b64decode(finished["tk"], validate=True))
        routing = int(finished["X-Apple-I-MD-RINFO"])
        if not ended.ok or not adi.set_routing(-2, routing).ok:
            raise WorkerError("state")
        state["provisioned"] = True
    finally:
        await http.close()


async def resynchronize(adi, state: dict, challenge: str) -> None:
    """Answer one explicit GSA 434 challenge using the existing machine only.

    Envelope/CoreADI reference: upstream tools/machoadi/resync.py (MIT).
    Reuse FindMy's verified TLS, never the upstream diagnostic transport.
    """
    from aiohttp import ClientTimeout
    from findmy.util.http import HttpSession
    import plistlib
    if not isinstance(challenge, str) or not 1 <= len(challenge) <= 16384:
        raise WorkerError("resync")
    try:
        sim = base64.b64decode(challenge, validate=True)
    except ValueError:
        raise WorkerError("resync") from None
    if not sim:
        raise WorkerError("resync")
    if state.get("resync_not_before", 0) > time.time():
        raise WorkerError("resync-cooldown")
    state["resync_not_before"] = time.time() + 60
    identity = state["identity"]
    headers = {"User-Agent": "akd/1.0 CFNetwork/1404.0.5 Darwin/22.3.0",
               "Content-Type": "text/x-xml-plist", "Connection": "close",
               "X-Mme-Device-Id": identity["device_id"],
               "X-MMe-Client-Info": identity["client_info"],
               "X-Apple-I-MD-LU": identity["uid"], "X-Apple-I-SRL-NO": identity["serial"],
               "X-Apple-I-Client-Time": datetime.now(UTC).isoformat(timespec="seconds").replace("+00:00", "Z")}
    http = HttpSession()
    async def request(url, body=None):
        # One lookup and one sync, no retries or redirects; bound the complete
        # operation below as well as individual network requests.
        response = await http.request("GET" if body is None else "POST", apple_url(url),
            headers=headers, data=body, auto_retry=False, allow_redirects=False,
            timeout=ClientTimeout(total=15, connect=5, sock_read=10))
        if response.status_code == 429:
            state["cooldown_until"] = time.time() + 45*60
            raise WorkerError("throttled")
        if response.status_code != 200:
            raise WorkerError("resync")
        return response.plist()
    try:
        async with asyncio.timeout(35):
            urls = (await request("https://gsa.apple.com/grandslam/GsService2/lookup"))["urls"]
            sync_url = apple_url(urls["midSyncMachine"])
            synced = adi.synchronize(-2, sim)
            routing = adi.get_routing(-2)
            if not synced.ok or not routing.ok:
                raise WorkerError("resync")
            mid, srm = synced.outputs["mid"], synced.outputs["srm"]
            if not mid or not srm or len(mid) > 16384 or len(srm) > 16384:
                raise WorkerError("resync")
            reply = (await request(sync_url, plistlib.dumps({"Header": {}, "Request": {
                "srm": base64.b64encode(srm).decode(),
                "X-Apple-I-MD-M": base64.b64encode(mid).decode(),
                "X-Apple-I-MD-RINFO": routing.outputs["routing"],
            }})))["Response"]
            status = reply.get("Status", {})
            if type(status.get("ec")) is not int or status["ec"] != 0:
                raise WorkerError("resync")
            if "X-Apple-I-MD-RINFO" in reply:
                updated = reply["X-Apple-I-MD-RINFO"]
                if type(updated) is not int or not 0 <= updated < 2**64 or not adi.set_routing(-2, updated).ok:
                    raise WorkerError("resync")
    except WorkerError:
        raise
    except Exception:
        # No raw Apple body, SIM, MID, SRM or dependency exception may escape.
        raise WorkerError("resync") from None
    finally:
        await http.close()


def load_state(args) -> tuple[dict, Path, Path]:
    path = args.state_dir / "backend-machine.encrypted"
    key = args.state_dir / "backend-machine.key"
    if path.exists():
        if path.stat().st_size > 2*1024*1024:
            raise WorkerError("state")
        state = json.loads(_unprotect_session(path.read_bytes(), key))
        if state.get("v") != 1 or state.get("backend") != args.backend:
            raise WorkerError("state")
    else:
        uid = str(uuid.uuid4())
        if args.backend == "mac-native":
            identity = native_identity(uid)
        else:
            if sys.platform != "win32":
                raise WorkerError("platform")
            identity = {"uid": uid.replace("-", "").upper(),
                "device_id": "00006000-" + secrets.token_hex(8).upper(),
                "host_uuid": secrets.token_hex(16), "serial": "C02" + "".join(secrets.choice("0123456789ABCDEFGHJKLMNPQRSTUVWXYZ") for _ in range(9)),
                "client_info": "<MacBookPro13,2> <macOS;13.1;22C65> <com.apple.AuthKit/1 (com.apple.akd/1.0)>"}
        state = {"v": 1, "backend": args.backend, "identity": identity, "files": {}, "provisioned": False}
    if args.backend == "mac-native" and native_identity(state["identity"]["uid"]) != state["identity"]:
        raise WorkerError("state")
    return state, path, key


class MachineRuntime:
    """One locked machine; fresh OTPs, bounded reuse of the pinned emulator."""

    def __init__(self, args, state: dict, path: Path, key: Path):
        self.args, self.state, self.path, self.key = args, state, path, key
        self.adi = None
        self.header_count = 0

    def save(self) -> None:
        if self.adi is not None:
            self.state["files"] = {name[len(STORE):]: base64.b64encode(bytes(value)).decode()
                for name, value in self.adi.stubs.files.items() if name.startswith(STORE)}
        _atomic_write(self.path, _protect_session(json.dumps(self.state).encode(), self.key))

    def recycle_due(self) -> bool:
        if self.adi is None:
            return False
        # Pinned harness allocators are monotonic (free is a no-op). Never
        # rewind pointers into live CoreADI state; reload from committed files.
        from harness import HEAP_ADDRESS, HEAP_SIZE, SCRATCH_ADDRESS, SCRATCH_SIZE, OBJECT_ADDRESS, OBJECT_SIZE
        h = self.adi.harness
        return self.header_count >= 16 or any(current - base >= size // 2 for current, base, size in (
            (h._heap_cur, HEAP_ADDRESS, HEAP_SIZE),
            (h._scratch_cur, SCRATCH_ADDRESS, SCRATCH_SIZE),
            (h._obj_cur, OBJECT_ADDRESS, OBJECT_SIZE)))

    def perform(self, action: str, *, challenge: str | None = None) -> dict:
        if action not in {"identity", "headers", "provision", "resync"}:
            raise WorkerError("state")
        if action == "resync" and self.args.backend != "win-emulated":
            raise WorkerError("state")
        state = self.state
        if state.get("cooldown_until", 0) > time.time():
            raise WorkerError("throttled")
        if action == "resync" and state.get("resync_not_before", 0) > time.time():
            raise WorkerError("resync-cooldown")
        result = {"ok": True, "identity": state["identity"]}
        try:
            if self.args.backend == "mac-native":
                if action != "identity":
                    result["headers"] = native_headers(state["identity"])
            elif action != "identity":
                if self.recycle_due():
                    self.save()
                    self.adi = None
                    # Unicorn callback cycles otherwise retain discarded VMs.
                    import gc
                    gc.collect()
                if self.adi is None:
                    self.adi = emulated_core(self.args, state)
                    self.header_count = 0
                adi = self.adi
                if action == "provision" and not state["provisioned"]:
                    asyncio.run(provision(adi, state))
                if not state["provisioned"] or not adi.is_machine_provisioned(-2).ok:
                    raise WorkerError("not-provisioned")
                if action == "resync":
                    asyncio.run(resynchronize(adi, state, challenge))
                    self.header_count += 1
                    return result
                otp = adi.request_otp(-2)
                routing = adi.get_routing(-2)
                if not otp.ok or not routing.ok:
                    raise WorkerError("state")
                self.header_count += 1
                result["headers"] = complete_headers(state["identity"], {
                    "X-Apple-I-MD": base64.b64encode(otp.outputs["otp"]).decode(),
                    "X-Apple-I-MD-M": base64.b64encode(otp.outputs["mid"]).decode(),
                    "X-Apple-I-MD-RINFO": str(routing.outputs["routing"]),
                })
            return result
        finally:
            # Commit before replying, including provisioning/throttle failures.
            self.save()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["identity", "headers", "provision", "serve"])
    parser.add_argument("--backend", required=True, choices=["mac-native", "win-emulated"])
    parser.add_argument("--state-dir", required=True, type=Path)
    parser.add_argument("--engine-dir", type=Path)
    parser.add_argument("--coreadi", type=Path)
    args = parser.parse_args()
    try:
        # Machine state lives in its own directory, separate from the account
        # and identity library. Refuse accidental use of the library directory.
        args.state_dir = args.state_dir.resolve()
        if any((args.state_dir/name).exists() for name in ("find-identities.sqlite3", "account-session.dpapi", "account-session.encrypted")):
            raise WorkerError("state")
        args.state_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        with (args.state_dir / "backend-machine.lock").open("a+b") as lock:
            if os.name == "nt":
                import msvcrt
                lock.write(b"\0")
                lock.flush()
                lock.seek(0)
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                except OSError:
                    raise WorkerError('busy') from None
            else:
                import fcntl
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise WorkerError('busy') from None
            state, path, key = load_state(args)
            # Persist identity before network: a failed provision must never mint
            # a new identity on the next attempt to evade an Apple refusal.
            _atomic_write(path, _protect_session(json.dumps(state).encode(), key))
            runtime = MachineRuntime(args, state, path, key)
            if args.action == "serve":
                while line := sys.stdin.buffer.readline(17409):
                    if len(line) > 17408 or not line.endswith(b"\n"):
                        raise WorkerError("state")
                    message = line.decode("ascii").strip()
                    if message.startswith("{"):
                        request = json.loads(message)
                        if (not isinstance(request, dict) or set(request) != {"action", "challenge"}
                                or request["action"] != "resync"):
                            raise WorkerError("state")
                        result = runtime.perform("resync", challenge=request["challenge"])
                    else:
                        result = runtime.perform(message)
                    print(json.dumps(result), flush=True)
            else:
                print(json.dumps(runtime.perform(args.action)), flush=True)
        return 0
    except WorkerError as error:
        print(json.dumps({"ok": False, "errorCode": error.code}), flush=True)
    except Exception:
        print(json.dumps({"ok": False, "errorCode": "internal"}), flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
