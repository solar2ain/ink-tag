"""Small, host-rendered templates. No device-side layout or font cost."""

from __future__ import annotations

import json
import math
import re
import sys
from importlib import resources
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


TEMPLATE_ID = "agent-quota"
_ASCII = re.compile(r"^[\x20-\x7e]*$")


def template_definition(template_id: str) -> dict[str, Any]:
    if template_id != TEMPLATE_ID:
        raise ValueError(f"unknown template {template_id!r}; try `inktag-cli host template list`")
    asset = resources.files("inktag_cli._assets").joinpath("template-agent-quota.json")
    return json.loads(asset.read_text(encoding="utf-8"))


def read_template_data(source: Path | str) -> dict[str, Any]:
    if str(source) == "-":
        text = sys.stdin.read(65_537)
    else:
        path = Path(source)
        if path.stat().st_size > 65_536:
            raise ValueError("template data exceeds 64 KiB")
        text = path.read_text(encoding="utf-8")
    if len(text.encode("utf-8")) > 65_536:
        raise ValueError("template data exceeds 64 KiB")
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid template JSON: {exc.msg}") from exc
    if not isinstance(data, dict):
        raise ValueError("template data must be a JSON object")
    return data


def _label(value: Any, name: str, limit: int, *, required: bool = False) -> str:
    if not isinstance(value, str) or len(value) > limit or not _ASCII.fullmatch(value):
        raise ValueError(f"{name} must be printable ASCII, at most {limit} characters")
    result = value.strip()
    if required and not result:
        raise ValueError(f"{name} must not be empty")
    return result


def validate_agent_quota(data: dict[str, Any]) -> dict[str, Any]:
    definition = template_definition(TEMPLATE_ID)
    if set(data) - {"title", "updated", "items"}:
        raise ValueError("unexpected agent-quota field")
    title = _label(data.get("title", "AGENT QUOTA"), "title", 28, required=True)
    updated = _label(data.get("updated", ""), "updated", 32)
    items = data.get("items")
    if not isinstance(items, list) or not 1 <= len(items) <= definition["maxItems"]:
        raise ValueError(f"items must contain 1..{definition['maxItems']} entries")
    normalized = []
    for index, item in enumerate(items):
        field = f"items[{index}]"
        if not isinstance(item, dict) or set(item) - {"label", "remaining", "usedPercent", "reset"}:
            raise ValueError(f"{field} has unexpected fields")
        label = _label(item.get("label"), f"{field}.label", 32, required=True)
        reset = _label(item.get("reset", ""), f"{field}.reset", 28)
        if ("remaining" in item) == ("usedPercent" in item):
            raise ValueError(f"{field} needs exactly one of remaining or usedPercent")
        raw = item.get("remaining", item.get("usedPercent"))
        if isinstance(raw, bool) or not isinstance(raw, (int, float)) or not math.isfinite(raw) or not 0 <= raw <= 100:
            raise ValueError(f"{field} percentage must be a number in 0..100")
        remaining = raw if "remaining" in item else 100 - raw
        normalized.append({"label": label, "remaining": round(remaining), "reset": reset})
    return {"title": title, "updated": updated, "items": normalized}


def _font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    return ImageFont.load_default(size=max(8, size))


def _text_width(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont) -> int:
    return draw.textbbox((0, 0), text, font=font)[2]


def _fit(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont, width: int) -> str:
    if _text_width(draw, text, font) <= width:
        return text
    while text and _text_width(draw, text + "...", font) > width:
        text = text[:-1]
    return text.rstrip() + "..." if text else ""


def _text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, font: ImageFont.ImageFont, color: str) -> None:
    box = draw.textbbox((0, 0), text, font=font)
    draw.text((xy[0], xy[1] - box[1]), text, font=font, fill=color)


def render_agent_quota(data: dict[str, Any], width: int, height: int) -> Image.Image:
    """Render at the device's native pixels; the bottom-right Free mark is device-owned."""
    if not 96 <= width <= 4096 or not 80 <= height <= 4096:
        raise ValueError("template canvas must be between 96x80 and 4096x4096")
    values = validate_agent_quota(data)
    palette = template_definition(TEMPLATE_ID)["palette"]
    canvas = Image.new("RGB", (width, height), palette["background"])
    draw = ImageDraw.Draw(canvas)
    short = min(width, height)
    margin = max(4, round(short * .035))
    title_font = _font(max(9, min(22, round(short * .052))))
    meta_font = _font(max(8, min(14, round(short * .033))))
    footer = max(18, round(height * .038))
    title = _fit(draw, values["title"], title_font, width - margin * 2)
    meta = _fit(draw, values["updated"], meta_font, max(0, width - margin * 2 - _text_width(draw, title, title_font) - 10))
    _text(draw, (margin, margin), title, title_font, palette["ink"])
    if meta:
        _text(draw, (width - margin - _text_width(draw, meta, meta_font), margin + 2), meta, meta_font, palette["ink"])
    header_bottom = margin + max(16, title_font.size + 8)
    draw.rectangle((margin, header_bottom, width - margin - 1, header_bottom), fill=palette["ink"])

    top = header_bottom + max(4, margin // 2)
    bottom = height - footer - margin
    available = bottom - top
    if available < 22:
        raise ValueError("canvas is too short for this template")
    gap = max(3, min(10, round(available * .018)))
    min_row = 25 if height < 150 else 44 if height < 380 else 66
    shown = min(len(values["items"]), max(1, (available + gap) // (min_row + gap)))
    row_height = (available - (shown - 1) * gap) // shown
    hidden = len(values["items"]) - shown
    if hidden:
        count = f"{shown}/{len(values['items'])}"
        _text(draw, (max(margin, (width - _text_width(draw, count, meta_font)) // 2), margin + 2), count, meta_font, palette["accent"])

    for index, item in enumerate(values["items"][:shown]):
        y = top + index * (row_height + gap)
        label_size = max(8, min(24, round(row_height * .25), round(short * .07)))
        value_size = max(14, min(72, round(row_height * .48), round(short * .16)))
        detail_size = max(8, min(16, round(row_height * .16)))
        label_font, value_font, detail_font = _font(label_size), _font(value_size), _font(detail_size)
        percent = f"{item['remaining']}%"
        percent_width = _text_width(draw, percent, value_font)
        label_width = max(16, width - 2 * margin - percent_width - max(8, margin))
        label = _fit(draw, item["label"], label_font, label_width)
        _text(draw, (margin, y + max(1, round(row_height * .08))), label, label_font, palette["ink"])
        if item["reset"] and row_height >= 34:
            detail = _fit(draw, item["reset"], detail_font, label_width)
            _text(draw, (margin, y + max(13, round(row_height * .42))), detail, detail_font, palette["ink"])
        _text(draw, (width - margin - percent_width, y + max(1, round(row_height * .12))), percent, value_font, palette["accent"])
        bar_height = max(3, min(8, row_height // 11))
        bar_y = y + row_height - bar_height - 2
        bar_right = width - margin - 1
        draw.rectangle((margin, bar_y, bar_right, bar_y + bar_height - 1), outline=palette["ink"])
        fill_width = round((bar_right - margin - 1) * item["remaining"] / 100)
        if fill_width:
            draw.rectangle((margin + 1, bar_y + 1, margin + fill_width, bar_y + bar_height - 2), fill=palette["accent"])
        if index + 1 < shown:
            rule_y = y + row_height + gap // 2
            for x in range(margin, width - margin, 5):
                draw.rectangle((x, rule_y, min(width - margin - 1, x + 1), rule_y), fill=palette["rule"])
    return canvas


def render_template(template_id: str, data: dict[str, Any], width: int, height: int) -> Image.Image:
    template_definition(template_id)
    return render_agent_quota(data, width, height)
