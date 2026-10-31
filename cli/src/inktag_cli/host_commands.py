"""User-facing host weather jobs. No WebUI server or firmware modification."""
from __future__ import annotations

import asyncio
import json
import os
from pathlib import Path
import sqlite3
import time
from typing import Annotated

import typer
from .command_framework import user_app

from .host_pages import JobStore, job_key, load_config, publish_image, read_target, run_due
from .host_render import TEMPLATES, render_weather
from .host_weather import MAX_RESPONSE, demo_weather, fetch_weather, find_locations, validate_weather
from .host_weather import fetch_weather as fetch_snapshot, validate_weather as validate_snapshot
from .local_service import local_data_directory

host_app = user_app(help="Render host pages and run schedules. The host must remain on and in BLE range for delivery.", no_args_is_help=True)


def emit(value): typer.echo(json.dumps(value, ensure_ascii=False, indent=2))


def fail(error):
    message = str(error) if isinstance(error, ValueError) else f"Host page operation failed ({type(error).__name__})"
    typer.echo(message, err=True)
    raise typer.Exit(1)


def snapshot_file(path: Path) -> dict:
    if path.stat().st_size > MAX_RESPONSE: raise ValueError("Host data snapshot is too large")
    try: return validate_snapshot(json.loads(path.read_text(encoding="utf-8")))
    except (KeyError, TypeError, IndexError, ValueError) as error:
        raise ValueError("Invalid host data snapshot; use host fetch or --demo for weather") from error


def size_option(value):
    from .image_commands import _parse_size
    return _parse_size(value)


@host_app.command("templates")
def templates():
    """List built-in layouts. Exported editor scenes use host import / render --scene."""
    emit([{"id": key, "version": 1, **value} for key, value in TEMPLATES.items()])


@host_app.command("target")
def target(context: typer.Context):
    """Read a binding once using --ble-name NAME or --ble-address ADDRESS. No clock write.

    Copy the returned target fields into a job. Does not save/enable a job or
    push a page. Ambiguous names must be selected by address instead.
    """
    from .transport import InkTagError
    if len(context.obj.target_selections)>1:
        raise typer.BadParameter('host target binds one device at a time; use one name/address per job')
    try:
        result = asyncio.run(read_target(context.obj))
        emit({"target": result, "addressKind": "mac" if ":" in result["address"] else "corebluetooth-uuid",
              "saved": False, "deliveryEnabled": False})
    except (OSError, ValueError, InkTagError) as error:
        if isinstance(error, InkTagError):
            typer.echo("Target binding failed. Use --ble-address for ambiguous names and complete normal authentication before retrying.", err=True)
            raise typer.Exit(1)
        fail(error)


@host_app.command("locations")
def locations(name: str, language: str = "en"):
    """Look up a city and choose coordinates; never uses device or host geolocation."""
    try: emit(find_locations(name, language))
    except (OSError, ValueError, TypeError) as error: fail(error)


@host_app.command("init")
def initialize(
    output: Annotated[Path, typer.Argument(help="New human-editable TOML file; never overwrites an existing file.")],
    latitude: Annotated[float, typer.Option(min=-90, max=90)],
    longitude: Annotated[float, typer.Option(min=-180, max=180)],
    label: str = "Weather",
    size: str = "400x300",
    template: str = "weather-paper",
):
    """Create a disabled preview job. Bind a host BLE address/identity before sending."""
    try:
        width, height = size_option(size)
        if not 96 <= width <= 2048 or not 96 <= height <= 2048 or width*height > 2097152:
            raise ValueError("Invalid visible pixel dimensions for a weather job")
        if output.exists(): raise ValueError("Configuration already exists; edit it instead of overwriting")
        # JSON basic strings are valid TOML strings for this printable label.
        from .host_weather import source_options
        source_options({"provider": "open-meteo", "latitude": latitude, "longitude": longitude, "label": label})
        if template not in TEMPLATES: raise ValueError("Unknown host template")
        content = f'''schema = 1

[sources.home]
provider = "open-meteo"
latitude = {latitude}
longitude = {longitude}
label = {json.dumps(label, ensure_ascii=False)}
timezone = "auto"

[[jobs]]
id = "desk"
template = "{template}"
source = "home"
width = {width}
height = {height}
palette = "bw" # bw, bwr, six
locale = "en" # en, zh-CN; Chinese requires a suitable local font
interval_minutes = 60
enabled = false # set true after checking the rendered preview
# device = "IKT_TL_1234" # optional display name only, never used for routing
# address = "AA:BB:CC:DD:12:34" # use the real host-reported BLE address
# macOS uses its host-local CoreBluetooth UUID instead of a visible MAC
# expected_device_id = "0123456789ABCDEF" # replace with real immutable ID
# font_file = "C:/Windows/Fonts/msyh.ttc" # optional local system font
'''
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as handle: handle.write(content)
        try: load_config(output)
        except ValueError:
            # Preserve the invalid file for user correction; never delete it.
            raise ValueError("Configuration was created but needs valid weather canvas dimensions")
        emit({"ok": True, "config": str(output.resolve()), "enabled": False, "deviceOperations": 0})
    except (OSError, ValueError) as error: fail(error)


@host_app.command("validate")
def validate(config: Path):
    """Check references, sizes and schedule values without network or BLE."""
    try:
        data = load_config(config)
        emit({"ok": True, "jobs": len(data["jobs"]), "sources": len(data["sources"]), "deviceOperations": 0})
    except (OSError, ValueError, TypeError, sqlite3.Error) as error: fail(error)


@host_app.command("fetch")
def fetch(config: Path, source: str = "home", output: Path | None = None):
    """Read public weather, never device/account data."""
    try:
        data = load_config(config)
        if source not in data["sources"]: raise ValueError("Unknown weather source")
        weather = fetch_snapshot(data["sources"][source])
        if output:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(weather, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
            emit({"ok": True, "output": str(output.resolve())})
        else: emit(weather)
    except (OSError, ValueError, TypeError) as error: fail(error)


@host_app.command("render")
def render(
    output: Annotated[Path, typer.Option("--output", "-o")],
    template: Annotated[str | None, typer.Argument()] = None,
    scene: Annotated[Path | None, typer.Option(help="Exported WebUI host scene. Uses its exact dimensions and colors.")] = None,
    snapshot: Annotated[Path | None, typer.Option(help="Snapshot from host fetch; no network request.")] = None,
    demo: Annotated[bool, typer.Option(help="Use clearly labelled synthetic demo weather.")] = False,
    size: str | None = None,
    palette: str | None = None,
    locale: str | None = None,
    font_file: Path | None = None,
):
    """Render a native-pixel PNG offline. Does not connect or update any device."""
    try:
        if (template is None) == (scene is None): raise ValueError('Choose exactly one built-in template or --scene')
        from .host_scene import read_scene, render_scene
        document = read_scene(scene) if scene else None
        if demo == (snapshot is not None): raise ValueError("Choose exactly one of --demo or --snapshot")
        data = demo_weather() if demo else snapshot_file(snapshot)
        if scene:
            if size is not None or palette is not None: raise ValueError('Scene dimensions and colors come from the exported canvas; edit the canvas instead')
            image = render_scene(document, data, locale=locale or 'zh-CN', font_file=str(font_file) if font_file else None)
            width, height = image.size
        else:
            width, height = size_option(size or '400x300')
            image = render_weather(data, template, width, height, palette=palette or 'bw', locale=locale or 'en', font_file=str(font_file) if font_file else None)
        output.parent.mkdir(parents=True, exist_ok=True)
        image.save(output, format="PNG")
        emit({"ok": True, "output": str(output.resolve()), "width": width, "height": height, "demo": bool(data.get("demo")), "deviceOperations": 0})
    except (OSError, ValueError, TypeError) as error: fail(error)


@host_app.command('import')
def import_scene(scene: Path, config: Annotated[Path, typer.Option('--config', help='New TOML task file; existing files are never overwritten.')],
                 job: str = 'desk', interval_minutes: Annotated[int | None, typer.Option(min=1, max=10080)] = None,
                 font_file: Path | None = None):
    """Import an exported WebUI host scene into a disabled, editable task."""
    try:
        from .host_scene import read_scene
        document = read_scene(scene)
        from .host_weather import source_options
        source = source_options(document['weatherSource'])
        interval_minutes = interval_minutes or 60
        import re
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,47}', job): raise ValueError('Invalid job name')
        if config.exists(): raise ValueError('Configuration already exists; use a new --config path')
        try: reference = os.path.relpath(scene.resolve(), config.resolve().parent)
        except ValueError: reference = str(scene.resolve())
        quote = lambda value: json.dumps(str(value), ensure_ascii=False)
        lines = ['schema = 1', '', '[sources.home]']
        lines += [f'{key} = {quote(value) if isinstance(value, str) else value}' for key, value in source.items()]
        lines += ['', '[[jobs]]', f'id = {quote(job)}', f'scene = {quote(reference)}', 'source = "home"',
                  f'width = {document["width"]}', f'height = {document["height"]}', 'locale = "zh-CN"',
                  f'interval_minutes = {interval_minutes}', 'enabled = false',
                  '# Read address and expected_device_id with: inktag-cli host -n NAME target',
                  '# address = "AA:BB:CC:DD:12:34"', '# expected_device_id = "0123456789ABCDEF"']
        if font_file: lines += [f'font_file = {quote(font_file.resolve())}']
        config.parent.mkdir(parents=True, exist_ok=True)
        with config.open('x', encoding='utf-8') as stream: stream.write('\n'.join(lines)+'\n')
        load_config(config)
        emit({'ok': True, 'config': str(config.resolve()), 'enabled': False, 'deviceOperations': 0})
    except (OSError, ValueError, TypeError) as error: fail(error)


@host_app.command("jobs")
def jobs(config: Path):
    """List template/source/schedule associations without contacting devices."""
    try: emit([{key:value for key,value in job.items() if not key.startswith('_')} for job in load_config(config)["jobs"]])
    except (OSError, ValueError, TypeError) as error: fail(error)


@host_app.command("status")
def status(config: Path, data_dir: Path | None = None):
    """Show separate preview/delivery state; device identities remain local."""
    store = None
    try:
        data = load_config(config)
        store = JobStore(local_data_directory(data_dir)/"host-pages")
        emit([{"job": job["id"], "enabled": job["enabled"],
               "preview": store.state(job_key(config, job["id"], False)),
               "delivery": store.state(job_key(config, job["id"], True))} for job in data["jobs"]])
    except (OSError, ValueError, TypeError, sqlite3.Error) as error: fail(error)
    finally:
        if store: store.close()


@host_app.command("run")
def run(
    context: typer.Context,
    config: Path,
    once: Annotated[bool, typer.Option(help="Evaluate due jobs once; suitable for OS task schedulers.")] = False,
    send: Annotated[bool, typer.Option(help="Explicitly enable authenticated Pro device delivery. Default is local PNG preview only.")] = False,
    fixture: Annotated[Path | None, typer.Option(help="Offline snapshot for preview-only validation; incompatible with --send.")] = None,
    data_dir: Annotated[Path | None, typer.Option(help="Persistent host-job state directory; does not start/change the WebUI service.")] = None,
):
    """Run an opt-in host scheduler. Ctrl+C stops it; disabled jobs never run.

    Without --fixture this fetches public weather. Only --send permits BLE.
    Missed runs are coalesced, unchanged pixels skipped, failures backed off.
    """
    store = None
    try:
        if send and fixture: raise ValueError("--fixture cannot be combined with --send")
        load_config(config)
        data = snapshot_file(fixture) if fixture else None
        store = JobStore(local_data_directory(data_dir)/"host-pages")
        async def publisher(job, path): return await publish_image(context.obj, job, path)
        while True:
            results = run_due(config, store, send=send, fixture=data, publisher=publisher if send else None)
            if once or any(item["status"] not in {"busy", "not-due"} for item in results):
                emit({"mode": "send" if send else "preview", "results": results})
            if once:
                if any(item["status"] == "error" for item in results): raise typer.Exit(1)
                break
            time.sleep(15) # bounded config reload, no tight retry or catch-up loop
    except KeyboardInterrupt:
        typer.echo("Host scheduler stopped", err=True)
        raise typer.Exit(130)
    except (OSError, ValueError, TypeError, sqlite3.Error) as error: fail(error)
    finally:
        if store: store.close()
