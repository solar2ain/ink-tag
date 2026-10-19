from __future__ import annotations

import asyncio
import hashlib
import json
import re
from pathlib import Path
from typing import Annotated, Any, Protocol

import typer
from bleak.exc import BleakError
from rich.console import Console
from rich.progress import BarColumn, Progress, TaskProgressColumn, TextColumn
from rich.table import Table

from .image_encoding import CONTENT_TYPE_IDS, canvas_size_for_screen, encode_image
from .image_processing import (
    IMAGE_ALGORITHMS,
    ImageProcessOptions,
    compose_image,
    process_frame,
    resolve_algorithm,
    save_png,
)
from .image_upload import ImageUploadResult, normalize_wire_encoding, upload_encoded_image
from .models import ImageStoreInfo, ImageStoreItem, ScreenConfig
from .transport import InkTagError


class _CliOptions(Protocol):
    json_output: bool

    def client(self, *, sink=None): ...


image_app = typer.Typer(
    help="Process, preview and upload images with the same pipeline as WebUI.",
    no_args_is_help=True,
)
console = Console()
error_console = Console(stderr=True)


@image_app.command("algorithms")
def image_algorithms(context: typer.Context) -> None:
    options = _options(context)
    values = [
        {
            "index": algorithm.index,
            "name": algorithm.name,
            "title": algorithm.title,
            "group": algorithm.group,
            "strengthRange": "0..5" if algorithm.direct_strength else "0..1",
        }
        for algorithm in IMAGE_ALGORITHMS
    ]
    if options.json_output:
        typer.echo(json.dumps(values, ensure_ascii=False, indent=2))
        return
    table = Table(title="WebUI-compatible image algorithms")
    table.add_column("#", justify="right")
    table.add_column("Name", no_wrap=True)
    table.add_column("Description")
    table.add_column("Strength", no_wrap=True)
    for value in values:
        table.add_row(
            str(value["index"]),
            str(value["name"]),
            str(value["title"]),
            str(value["strengthRange"]),
        )
    console.print(table)


@image_app.command("process")
def image_process(
    context: typer.Context,
    source: Annotated[Path, typer.Argument(exists=True, dir_okay=False, readable=True)],
    output: Annotated[Path | None, typer.Option("--output", "-o", help="Processed PNG path.")] = None,
    size: Annotated[str, typer.Option(help="Visible canvas size, for example 400x300.")] = "400x300",
    color: Annotated[str, typer.Option(help="Color ID or BW/BWR/BWRY/GRAY4/SIX/SEVEN.")] = "BWR",
    algorithm: Annotated[str, typer.Option(help="Algorithm name or number from `image algorithms`.")] = "auto",
    preset: Annotated[str, typer.Option(help="balanced, dynamic, vivid, soft, grayscale, restore or posterScan.")] = "balanced",
    strength: Annotated[float, typer.Option(min=0.0, max=5.0)] = 1.0,
    contrast: Annotated[float, typer.Option(min=0.5, max=2.0)] = 1.0,
    heavy_ink: Annotated[bool, typer.Option("--heavy-ink", help="Strengthen dark and saturated regions.")] = False,
    fit: Annotated[str, typer.Option(help="crop or stretch.")] = "crop",
    rotate: Annotated[str, typer.Option(help="auto, 0, 90, 180 or 270.")] = "auto",
    scale: Annotated[float, typer.Option(min=0.25, max=6.0)] = 1.0,
    pan_x: Annotated[float, typer.Option(help="Horizontal image offset in device pixels.")] = 0.0,
    pan_y: Annotated[float, typer.Option(help="Vertical image offset in device pixels.")] = 0.0,
) -> None:
    options = _options(context)
    try:
        width, height = _parse_size(size)
        color_id = _parse_color(color)
        processing = ImageProcessOptions(algorithm, preset, contrast, strength, heavy_ink).validated()
        composed = compose_image(
            source,
            width,
            height,
            fit=fit,
            rotation=rotate,
            scale=scale,
            pan_x=pan_x,
            pan_y=pan_y,
        )
        result = process_frame(composed.frame, color_id, processing, coverage=composed.coverage)
        destination = output or source.with_name(f"{source.stem}-inktag.png")
        save_png(result, destination)
    except (OSError, RuntimeError, ValueError) as exc:
        _fail(options, str(exc))
    algorithm_spec = resolve_algorithm(processing.algorithm)
    _emit(
        options,
        {
            "ok": True,
            "output": str(destination.resolve()),
            "size": f"{width}x{height}",
            "color": color_id,
            "algorithm": algorithm_spec.name,
            "algorithmIndex": algorithm_spec.index,
            "preset": processing.preset,
            "rotation": composed.rotation,
            "rgbaSha256": hashlib.sha256(result.data).hexdigest(),
        },
    )


@image_app.command("upload")
def image_upload(
    context: typer.Context,
    source: Annotated[Path, typer.Argument(exists=True, dir_okay=False, readable=True)],
    algorithm: Annotated[str, typer.Option(help="Algorithm name or number from `image algorithms`.")] = "auto",
    preset: Annotated[str, typer.Option(help="balanced, dynamic, vivid, soft, grayscale, restore or posterScan.")] = "balanced",
    strength: Annotated[float, typer.Option(min=0.0, max=5.0)] = 1.0,
    contrast: Annotated[float, typer.Option(min=0.5, max=2.0)] = 1.0,
    heavy_ink: Annotated[bool, typer.Option("--heavy-ink", help="Strengthen dark and saturated regions.")] = False,
    fit: Annotated[str, typer.Option(help="crop or stretch.")] = "crop",
    rotate: Annotated[str, typer.Option(help="auto, 0, 90, 180 or 270.")] = "auto",
    scale: Annotated[float, typer.Option(min=0.25, max=6.0)] = 1.0,
    pan_x: Annotated[float, typer.Option(help="Horizontal image offset in device pixels.")] = 0.0,
    pan_y: Annotated[float, typer.Option(help="Vertical image offset in device pixels.")] = 0.0,
    slot: Annotated[str | None, typer.Option(help="Save to `next` or a zero-based device slot ID (0..254). Fixed-slot firmware also permits choosing an empty slot.")] = None,
    force: Annotated[bool, typer.Option("--force", help="When `--slot next` is full, replace the oldest compatible image.")] = False,
    store_only: Annotated[bool, typer.Option("--store-only", help="Save the image to a slot without refreshing the display; requires --slot.")] = False,
    name: Annotated[str | None, typer.Option(help="Name for this upload: up to 35 UTF-8 bytes on IMAGE_METADATA firmware; empty saves an unnamed image. Older firmware retains its 19-byte sanitized descriptor.")] = None,
    content_type: Annotated[str, typer.Option(help="image, schedule, wifi, todo, business or page.")] = "image",
    preview: Annotated[Path | None, typer.Option(help="Also save the exact processed device preview as PNG.")] = None,
    refresh_timeout: Annotated[float, typer.Option(min=10.0, max=300.0, help="Physical refresh timeout in seconds.")] = 120.0,
    expected_size: Annotated[str | None, typer.Option("--expect-size", help="Fail before upload if the device's visible canvas differs.")] = None,
    wire_encoding: Annotated[str, typer.Option(help="Transfer encoding: auto (capability and savings), none (raw), or packbits-rle (requires device support; no raw fallback).")] = "auto",
) -> None:
    options = _options(context)
    try:
        requested_wire_encoding = normalize_wire_encoding(wire_encoding)
        processing = ImageProcessOptions(algorithm, preset, contrast, strength, heavy_ink).validated()
        if content_type not in CONTENT_TYPE_IDS:
            raise ValueError(f"content type must be one of: {', '.join(CONTENT_TYPE_IDS)}")
        if force and (slot is None or slot.lower() != "next"):
            raise ValueError("--force is only valid together with --slot next")
        if store_only and slot is None:
            raise ValueError("--store-only requires --slot next or a numeric slot ID")
        normalized_slot = _parse_slot(slot)
        required_size = _parse_size(expected_size) if expected_size is not None else None
    except ValueError as exc:
        _fail(options, str(exc))

    progress = None if options.json_output else _UploadProgress()
    if progress:
        progress.start()

    async def operation() -> tuple[ImageUploadResult, ScreenConfig, str]:
        async with options.client() as client:
            screen = client.snapshot.screen or await client.get_screen()
            width, height = canvas_size_for_screen(screen)
            _require_visible_canvas((width, height), required_size)
            replacement: int | None = None
            save_to_slot = normalized_slot is not None
            if save_to_slot:
                info, items = await client.list_slots()
                replacement = _replacement_slot(normalized_slot, force, info, items)

            def render():
                composed = compose_image(
                    source,
                    width,
                    height,
                    fit=fit,
                    rotation=rotate,
                    scale=scale,
                    pan_x=pan_x,
                    pan_y=pan_y,
                )
                processed = process_frame(
                    composed.frame,
                    screen.color,
                    processing,
                    coverage=composed.coverage,
                )
                return processed, composed.rotation

            processed, resolved_rotation = await asyncio.to_thread(render)
            if preview:
                await asyncio.to_thread(save_png, processed, preview)
            encoded = encode_image(processed, screen)
            result = await upload_encoded_image(
                client,
                encoded,
                screen,
                name=source.name if name is None else name,
                save_to_slot=save_to_slot,
                content_type=content_type,
                replace_slot=replacement,
                display_after_store=not store_only,
                refresh_timeout=refresh_timeout,
                progress=progress.update if progress else None,
                wire_encoding=requested_wire_encoding,
            )
            if save_to_slot:
                _info, current_items = await client.list_slots()
                if result.stored_id is None or all(item.id != result.stored_id for item in current_items):
                    raise InkTagError("Device acknowledged the upload but the committed slot was not listed")
                if _info and _info.fixed_slot_count and replacement is not None and result.stored_id != replacement:
                    raise InkTagError("Device committed a different slot; inspect slots before retrying")
            return result, screen, str(resolved_rotation)

    try:
        result, screen, resolved_rotation = asyncio.run(operation())
    except KeyboardInterrupt:
        if progress:
            progress.stop()
        _fail(options, "Cancelled", exit_code=130)
    except (BleakError, InkTagError, OSError, RuntimeError, ValueError) as exc:
        if progress:
            progress.stop()
        _fail(options, str(exc))
    finally:
        if progress:
            progress.stop()

    algorithm_spec = resolve_algorithm(processing.algorithm)
    _emit(
        options,
        {
            "ok": True,
            "deviceScreen": f"{screen.width}x{screen.height}",
            "visibleCanvas": "x".join(map(str, canvas_size_for_screen(screen))),
            "algorithm": algorithm_spec.name,
            "algorithmIndex": algorithm_spec.index,
            "preset": processing.preset,
            "rotation": int(resolved_rotation),
            "preview": str(preview.resolve()) if preview else None,
            "requested_wire_encoding": requested_wire_encoding,
            **result.to_dict(),
        },
    )


class _UploadProgress:
    def __init__(self) -> None:
        self.progress = Progress(
            TextColumn("{task.description}"),
            BarColumn(),
            TaskProgressColumn(),
            console=console,
            transient=True,
        )
        self.task: int | None = None
        self.stage = ""
        self.active = False

    def start(self) -> None:
        if not self.active:
            self.progress.start()
            self.active = True

    def stop(self) -> None:
        if self.active:
            self.progress.stop()
            self.active = False

    def update(self, stage: str, completed: int, total: int, message: str) -> None:
        if self.task is None or stage != self.stage:
            if self.task is not None:
                self.progress.update(self.task, completed=self.progress.tasks[self.task].total)
            self.task = self.progress.add_task(message, total=max(1, total), completed=completed)
            self.stage = stage
        else:
            self.progress.update(self.task, description=message, total=max(1, total), completed=completed)


def _options(context: typer.Context) -> _CliOptions:
    return context.obj


def _parse_size(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"\s*(\d+)\s*[xX]\s*(\d+)\s*", value)
    if not match:
        raise ValueError("size must look like 400x300")
    width, height = int(match.group(1)), int(match.group(2))
    if not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError("size dimensions must be in 1..4096")
    return width, height


def _require_visible_canvas(actual: tuple[int, int], expected: tuple[int, int] | None) -> None:
    if expected is not None and actual != expected:
        raise InkTagError(
            f"Device visible canvas is {actual[0]}x{actual[1]}, expected {expected[0]}x{expected[1]}; nothing was uploaded"
        )


_COLOR_ALIASES = {
    "bw": 1,
    "bwr": 2,
    "bwry": 3,
    "gray4": 4,
    "4gray": 4,
    "bwy": 5,
    "bwb": 6,
    "bwg": 7,
    "bwo": 8,
    "gray16": 9,
    "16gray": 9,
    "six": 10,
    "seven": 11,
}


def _parse_color(value: str) -> int:
    normalized = re.sub(r"[^a-zA-Z0-9]", "", value).lower()
    if normalized.isdigit():
        color = int(normalized)
    else:
        color = _COLOR_ALIASES.get(normalized, 0)
    if color not in range(1, 12):
        raise ValueError("color must be an ID in 1..11 or a supported color code")
    return color


def _parse_slot(value: str | None) -> str | int | None:
    if value is None:
        return None
    if value.lower() == "next":
        return "next"
    try:
        slot_id = int(value, 0)
    except ValueError as exc:
        raise ValueError("slot must be `next` or a numeric device slot ID") from exc
    if not 0 <= slot_id <= 254:
        raise ValueError("slot ID must be in 0..254; 255 is reserved for automatic selection")
    return slot_id


def _replacement_slot(
    requested: str | int,
    force: bool,
    info: ImageStoreInfo | None,
    items: list[ImageStoreItem],
) -> int | None:
    if isinstance(requested, int):
        if info and info.fixed_slot_count and not 0 <= requested < info.fixed_slot_count:
            raise InkTagError(f"Device slot {requested} is outside its fixed capacity")
        if all(item.id != requested for item in items):
            if info and info.fixed_slot_count and info.contiguous_fit > 0 and info.occupied_ids is not None:
                if requested in info.occupied_ids:
                    raise InkTagError(f"Device slot {requested} is outside the current edition's visible records")
                return requested
            raise InkTagError(f"Device slot {requested} does not exist; use --slot next to allocate one")
        return requested
    if info and info.contiguous_fit > 0:
        return None
    if not force:
        raise InkTagError("Image store is full; use --slot next --force or choose a numeric slot to replace")
    candidates = [item for item in items if item.compatible and item.content_type == "image"]
    if not candidates:
        raise InkTagError("Image store is full and has no compatible ordinary image that --force may replace")
    return candidates[0].id


def _emit(options: _CliOptions, value: dict[str, Any]) -> None:
    if options.json_output:
        typer.echo(json.dumps(value, ensure_ascii=False, indent=2))
        return
    table = Table(show_header=False, box=None)
    table.add_column("Field", style="cyan", no_wrap=True)
    table.add_column("Value", overflow="fold")
    for key, item in value.items():
        if item is None:
            continue
        table.add_row(re.sub(r"(?<!^)(?=[A-Z])", " ", key).title(), str(item))
    console.print(table)


def _fail(options: _CliOptions, message: str, *, exit_code: int = 1) -> None:
    if options.json_output:
        typer.echo(json.dumps({"ok": False, "error": message}, ensure_ascii=False), err=True)
    else:
        error_console.print(f"[red]Error:[/red] {message}")
    raise typer.Exit(exit_code)
