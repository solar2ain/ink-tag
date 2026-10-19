"""Host-side template rendering and the existing guarded image-upload path."""

from __future__ import annotations

import hashlib
import json
import tempfile
from pathlib import Path
from typing import Annotated

import typer

from .image_commands import _parse_size, image_upload
from .template_renderer import TEMPLATE_ID, read_template_data, render_template, template_definition


template_app = typer.Typer(help="Render a built-in template with external JSON data.", no_args_is_help=True)


def _render(template_id: str, data_source: str, size: str, output: Path) -> tuple[int, int]:
    width, height = _parse_size(size)
    data = read_template_data(data_source)
    image = render_template(template_id, data, width, height)
    image.save(output, "PNG")
    return width, height


@template_app.command("list")
def template_list() -> None:
    """List installed host-rendered templates."""
    definition = template_definition(TEMPLATE_ID)
    typer.echo(f"{definition['id']}  {definition['description']}")


@template_app.command("sample")
def template_sample(template_id: Annotated[str, typer.Argument(help="Template ID from `template list`.")]) -> None:
    """Print a valid external-data example for agents or scripts."""
    try:
        typer.echo(json.dumps(template_definition(template_id)["sample"], ensure_ascii=False, indent=2))
    except ValueError as exc:
        raise typer.BadParameter(str(exc)) from exc


@template_app.command("render")
def template_render(
    context: typer.Context,
    template_id: Annotated[str, typer.Argument(help="Template ID from `template list`.")],
    data: Annotated[str, typer.Option("--data", help="UTF-8 JSON file, or - for stdin.")],
    output: Annotated[Path, typer.Option("--output", "-o", help="Rendered PNG destination.")],
    size: Annotated[str, typer.Option(help="Exact visible canvas size, for example 400x300.")] = "400x300",
) -> None:
    """Render locally without connecting to or changing a device."""
    try:
        width, height = _render(template_id, data, size, output)
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
    except (OSError, RuntimeError, ValueError) as exc:
        raise typer.BadParameter(str(exc)) from exc
    result = {"ok": True, "template": template_id, "size": f"{width}x{height}", "output": str(output.resolve()), "sha256": digest}
    typer.echo(json.dumps(result, ensure_ascii=False) if getattr(context.obj, "json_output", False) else result["output"])


@template_app.command("show")
def template_show(
    context: typer.Context,
    template_id: Annotated[str, typer.Argument(help="Template ID from `template list`.")],
    data: Annotated[str, typer.Option("--data", help="UTF-8 JSON file, or - for stdin.")],
    size: Annotated[str, typer.Option(help="Required exact visible device canvas, for example 400x300.")],
    preview: Annotated[Path | None, typer.Option(help="Save the exact processed device preview as PNG.")] = None,
    refresh_timeout: Annotated[float, typer.Option(min=10.0, max=300.0)] = 120.0,
) -> None:
    """Render and display via the shared CLI image upload, with a size preflight."""
    try:
        with tempfile.TemporaryDirectory(prefix="inktag-template-") as directory:
            source = Path(directory) / f"{template_id}.png"
            width, height = _render(template_id, data, size, source)
            image_upload(
                context, source,
                algorithm="none", preset="balanced", strength=1.0, contrast=1.0,
                heavy_ink=False, fit="stretch", rotate="0", scale=1.0, pan_x=0.0, pan_y=0.0,
                slot=None, force=False, store_only=False, name=template_id, content_type="page",
                preview=preview, refresh_timeout=refresh_timeout, expected_size=f"{width}x{height}",
            )
    except (OSError, RuntimeError, ValueError) as exc:
        raise typer.BadParameter(str(exc)) from exc
