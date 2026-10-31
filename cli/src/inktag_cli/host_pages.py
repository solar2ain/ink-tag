"""Host-only jobs: template + source + exact target + schedule, never firmware scripts."""
from __future__ import annotations

import asyncio
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import time
import tomllib
import uuid
from typing import Callable

from .host_render import PALETTES, TEMPLATES, render_weather
from .host_weather import fetch_weather, source_options, validate_weather

MAX_CONFIG_BYTES = 128 * 1024
JOB_FIELDS = {"id", "template", "source", "device", "address", "expected_device_id", "width", "height", "palette",
              "locale", "font_file", "interval_minutes", "enabled", "scene"}


def target_address(value: str) -> str:
    """Persist a host BLE address, never a display name or Web Bluetooth ID.

    WinRT/BlueZ expose MAC addresses. CoreBluetooth exposes a host-local UUID
    instead; it is not a MAC and is not portable to another computer.
    """
    if re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
        return value.upper()
    if re.fullmatch(r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", value):
        return str(uuid.UUID(value)).upper()
    raise ValueError("address must be a host-reported Bluetooth MAC (or CoreBluetooth UUID on macOS), not a name or browser device ID")


def load_config(path: Path) -> dict:
    if path.stat().st_size > MAX_CONFIG_BYTES:
        raise ValueError("Host job configuration is too large")
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    if set(data) - {"schema", "sources", "jobs"} or type(data.get("schema")) is not int or data.get("schema") != 1:
        raise ValueError("Expected host job TOML schema = 1")
    sources, jobs = data.get("sources"), data.get("jobs")
    if not isinstance(sources, dict) or not 1 <= len(sources) <= 32 or not isinstance(jobs, list) or not 1 <= len(jobs) <= 32:
        raise ValueError("A configuration needs 1..32 sources and 1..32 jobs")
    sources = {key: source_options(value) for key, value in sources.items()}
    seen = set()
    for original in jobs:
        if not isinstance(original, dict) or set(original) - JOB_FIELDS:
            raise ValueError("Unknown host job option")
        job = original
        key = job.get("id")
        if not isinstance(key, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}", key) or key in seen:
            raise ValueError("Job IDs must be unique, 1..48 letters, digits, underscores or hyphens")
        seen.add(key)
        if not isinstance(job.get("source"), str) or job["source"] not in sources:
            raise ValueError(f"Job {key} references an unknown source")
        if "scene" in job:
            from .host_scene import read_scene_with_hash
            if "template" in job or not isinstance(job['scene'], str) or not job['scene'].strip():
                raise ValueError('Choose exactly one scene file or built-in template')
            scene_path = (path.resolve().parent / job['scene']).resolve()
            scene, scene_hash = read_scene_with_hash(scene_path)
            if (job.get('width'), job.get('height')) != (scene['width'], scene['height']):
                raise ValueError('Scene size and job size differ; re-import the updated scene')
            job['scene'] = str(scene_path)
            job['_scene_sha256'] = scene_hash
            job['_scene_color'] = scene.get('colorMode', 2)
        elif not isinstance(job.get("template"), str) or job['template'] not in TEMPLATES:
            raise ValueError(f"Job {key} references an unknown template")
        for field, default in (("palette", "bw"), ("locale", "en"), ("interval_minutes", 60), ("enabled", False)):
            job.setdefault(field, default)
        if not isinstance(job["palette"], str) or not isinstance(job["locale"], str) or job["palette"] not in PALETTES or job["locale"] not in {"en", "zh-CN"} or type(job["enabled"]) is not bool:
            raise ValueError(f"Invalid palette, locale or enabled flag for {key}")
        w, h = job.get("width"), job.get("height")
        if type(w) is not int or type(h) is not int or not 96 <= w <= 2048 or not 96 <= h <= 2048 or w*h > 2097152:
            raise ValueError(f"Invalid visible pixel dimensions for {key}")
        interval = job["interval_minutes"]
        if type(interval) is not int or not 1 <= interval <= 10080:
            raise ValueError("interval_minutes must be an integer in 1..10080 (60 recommended)")
        for field in ("device", "address", "expected_device_id", "font_file"):
            if field in job and (not isinstance(job[field], str) or not 1 <= len(job[field]) <= 512 or any(ord(c) < 32 for c in job[field])):
                raise ValueError(f"Invalid {field} for {key}")
        if "address" in job:
            job["address"] = target_address(job["address"])
        if "expected_device_id" in job and not re.fullmatch(r"[0-9a-fA-F]{16}", job["expected_device_id"]):
            raise ValueError("expected_device_id must be the 16-hex immutable identity reported by the device")
        if "font_file" in job:
            font = Path(job["font_file"])
            job["font_file"] = str((path.resolve().parent / font).resolve()) if not font.is_absolute() else str(font)
    return {"schema": 1, "sources": sources, "jobs": jobs}


def canonical(value) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


class JobStore:
    def __init__(self, directory: Path):
        directory = directory.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        self.directory = directory
        self.db = sqlite3.connect(directory / "host-pages.sqlite", timeout=5)
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS state (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS lease (key TEXT PRIMARY KEY, token TEXT, expires REAL);
            CREATE TABLE IF NOT EXISTS cache (key TEXT PRIMARY KEY, payload TEXT, expires REAL);
        """)

    def close(self): self.db.close()

    def state(self, key):
        row = self.db.execute("SELECT value FROM state WHERE key=?", (key,)).fetchone()
        return json.loads(row[0]) if row else {}

    def save(self, key, value):
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO state VALUES (?,?)", (key, canonical(value)))

    @contextmanager
    def lease(self, key, now):
        token = uuid.uuid4().hex
        with self.db:
            self.db.execute("DELETE FROM lease WHERE expires <= ?", (now,))
            acquired = self.db.execute("INSERT OR IGNORE INTO lease VALUES (?,?,?)", (key, token, now+600)).rowcount == 1
        try: yield acquired
        finally:
            if acquired:
                with self.db:
                    self.db.execute("DELETE FROM lease WHERE key=? AND token=?", (key, token))

    def weather(self, source, now, fetcher):
        key = hashlib.sha256(canonical(source).encode()).hexdigest()
        row = self.db.execute("SELECT payload,expires FROM cache WHERE key=?", (key,)).fetchone()
        if row and row[1] > now:
            return validate_weather(json.loads(row[0]))
        payload = validate_weather(fetcher(source))
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO cache VALUES (?,?,?)", (key, canonical(payload), now+600))
        return payload


def job_key(config: Path, job_id: str, send: bool) -> str:
    namespace = hashlib.sha256(str(config.resolve()).encode()).hexdigest()[:24]
    return f"{namespace}:{job_id}:{'send' if send else 'preview'}"


def require_target(job):
    if not job.get("address") or not job.get("expected_device_id") or int(job["expected_device_id"], 16) == 0:
        raise ValueError("Sending needs a bound Bluetooth address and nonzero expected_device_id; a device name alone is not a target")
    target_address(job["address"])


async def read_target(options) -> dict:
    """Resolve a user-selected name once using the shared client, not per tick.

    Returns data for an explicit local binding; does not save a job or enable
    delivery. The normal client rejects ambiguous names and performs AUTH.
    """
    if not (options.address or options.device):
        raise ValueError("Select a target with --ble-name or --ble-address before host target")
    async with options.client(auto_sync_time=False) as client:
        identity = await client.activation_status()
        result = {"device": client.snapshot.name, "address": target_address(client.snapshot.address),
                  "expected_device_id": identity.activation_id}
        require_target(result)
        return result


def failure_message(stage: str, error: Exception) -> str:
    """Actionable categories, without copying provider queries or BLE payloads."""
    prefix = {"weather": "Weather lookup", "render": "Page rendering", "delivery": "Device delivery"}[stage]
    detail = ""
    if stage == "delivery":
        text = str(error).lower()
        if "requires pro" in text: detail = " Pro activation is required."
        elif "dimensions differ" in text: detail = " Job size differs from the visible device screen."
        elif "palette" in text: detail = " Job colors do not match the device screen."
        elif "password" in text or "auth" in text or "credential" in text:
            detail = " Authentication unavailable; complete a normal authenticated CLI connection first."
        elif "identity" in text: detail = " Verify the exact target and expected immutable identity."
        elif isinstance(error, TimeoutError): detail = " Transfer result may be unknown; no immediate retry."
    elif stage == "weather" and type(getattr(error, "code", None)) is int:
        detail = f" Provider HTTP status {error.code}."
    elif stage == "render" and "font" in str(error).lower():
        detail = " Select an installed system font using font_file."
    return f"{prefix} failed ({type(error).__name__}).{detail} Last successful state is retained."


def run_due(config_path: Path, store: JobStore, *, send=False, fixture: dict | None = None,
            fetcher: Callable = fetch_weather, publisher: Callable | None = None, now: float | None = None) -> list[dict]:
    config = load_config(config_path)
    now = time.time() if now is None else now
    if send and fixture is not None:
        raise ValueError("Offline fixture/demo data cannot be sent by a scheduled job")
    if send:
        for job in config["jobs"]:
            if job["enabled"]: require_target(job)
        if publisher is None: raise ValueError("No shared CLI image publisher is configured")
    results = []
    for job in config["jobs"]:
        if not job["enabled"]: continue
        key = job_key(config_path, job["id"], send)
        source = config["sources"][job["source"]]
        revision = hashlib.sha256(canonical({"job": job, "source": source, "renderer": "weather-v1"}).encode()).hexdigest()
        with store.lease("job:"+key, now) as acquired:
            if not acquired:
                results.append({"job": job["id"], "status": "busy"}); continue
            state = store.state(key)
            if state.get("revision") == revision and state.get("next_due", 0) > now:
                results.append({"job": job["id"], "status": "not-due", "next_due": state["next_due"]}); continue
            # One lease per immutable target, not a computer-wide BLE queue.
            resource = "device:"+job["expected_device_id"].lower() if send else "preview:"+key
            with store.lease(resource, now) as target_acquired:
                if not target_acquired:
                    results.append({"job": job["id"], "status": "busy"}); continue
                try:
                    stage = "weather"
                    data = validate_weather(fixture) if fixture is not None else store.weather(source, now, fetcher)
                    if send and data.get('demo'):
                        raise ValueError('Synthetic weather cannot be sent by a scheduled job')
                    stage = "render"
                    if 'scene' in job:
                        from .host_scene import read_scene, render_scene
                        scene_bytes = Path(job['scene']).read_bytes()
                        if hashlib.sha256(scene_bytes).hexdigest() != job['_scene_sha256']:
                            raise ValueError('Scene changed during this run; retry at the next boundary')
                        image = render_scene(json.loads(scene_bytes), data, locale=job['locale'], font_file=job.get('font_file'))
                    else:
                        image = render_weather(data, job["template"], job["width"], job["height"], palette=job["palette"],
                                               locale=job["locale"], font_file=job.get("font_file"))
                    # Pixels, not PNG metadata/compression, decide whether to redraw.
                    digest = hashlib.sha256(f"{image.size}:RGB".encode()+image.tobytes()).hexdigest()
                    output = store.directory / f"{hashlib.sha256(key.encode()).hexdigest()[:24]}.png"
                    pending = output.with_suffix(".pending.png")
                    image.save(pending, format="PNG")
                    status = "unchanged"
                    target = canonical([job["address"], job["expected_device_id"].upper()]) if send else None
                    if digest != state.get("last_hash") or send and state.get("delivery_target") != target:
                        if send:
                            stage = "delivery"
                            asyncio.run(asyncio.wait_for(publisher(job, pending), timeout=240))
                            status = "sent"
                        else: status = "rendered"
                    pending.replace(output)
                    state.update(revision=revision, last_hash=digest, last_success=now, next_due=now+job["interval_minutes"]*60,
                                 failures=0, last_error=None, output=str(output), delivery_target=target)
                    store.save(key, state)
                    results.append({"job": job["id"], "status": status, "pixelSha256": digest,
                                    "fileSha256": hashlib.sha256(output.read_bytes()).hexdigest(), "output": str(output), "next_due": state["next_due"]})
                except Exception as error:
                    # Never log a provider URL/query, credentials or BLE response
                    # payload. Preserve the last successful hash on any failure.
                    failures = min(10, state.get("failures", 0)+1)
                    delay = min(job["interval_minutes"]*60, 3600, 60*2**(failures-1))
                    state.update(revision=revision, next_due=now+delay, failures=failures,
                                 last_error=failure_message(stage, error))
                    store.save(key, state)
                    results.append({"job": job["id"], "status": "error", "error": state["last_error"], "next_due": state["next_due"]})
    return results


async def publish_image(options, job, path):
    """Reuse the maintained BLE/auth/encoding/upload implementation, no second client."""
    from .image_encoding import canvas_size_for_screen, encode_image
    from .image_processing import ImageProcessOptions, compose_image, process_frame
    from .image_upload import upload_encoded_image
    from .transport import InkTagError
    from dataclasses import replace

    require_target(job)
    # `device` is a user-facing name only. Never re-resolve it on each tick or
    # fall back to another same-name device when the bound address is absent.
    selected = replace(options, device=None, address=target_address(job["address"]), password_stdin=False, verbose=False)
    async with selected.client(expected_device_id=job["expected_device_id"], allow_password_prompt=False) as client:
        # Existing policy classifies custom dynamic templates as Pro. This is
        # a client admission check, not proof that raster content is device-gated.
        if not (await client.activation_status()).active:
            raise InkTagError("Host dynamic template delivery requires Pro")
        screen = client.snapshot.screen or await client.get_screen()
        if canvas_size_for_screen(screen) != (job["width"], job["height"]):
            raise InkTagError("Visible screen dimensions differ from this job; update the job, not the device profile")
        if '_scene_color' in job and job['_scene_color'] != screen.color:
            raise InkTagError('Scene palette differs from the device screen; update the scene, not the device profile')
        if (job["palette"] == "bwr" and screen.color not in {2, 3, 10, 11}) or (job["palette"] == "six" and screen.color not in {10, 11}):
            raise InkTagError("Job palette is not compatible with the device screen")
        def encode():
            composed = compose_image(path, job["width"], job["height"], fit="stretch", rotation="0")
            frame = process_frame(composed.frame, screen.color, ImageProcessOptions(algorithm="none"), coverage=composed.coverage)
            return encode_image(frame, screen)
        encoded = await asyncio.to_thread(encode)
        result = await upload_encoded_image(client, encoded, screen, name="Host page", save_to_slot=False,
                                            content_type="page", refresh_timeout=120, wire_encoding="auto")
        return result.to_dict()
