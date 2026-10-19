"""Original weather layout candidates; renderer boundary is independent of any editor.

Leafer scene rendering can later register another renderer without changing job
identity, weather snapshots, scheduling, credential handling or image transfer.
"""
from __future__ import annotations

import math
from datetime import datetime
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

from .host_weather import validate_weather

TEMPLATES = {
    "weather-paper": {"name": "报纸天气站", "description": "Large current temperature, 24-hour chart, five-day forecast", "renderer": "weather-v1"},
    "weather-cards": {"name": "彩色天气卡", "description": "Color accents, current metrics and temperature bars with an hourly humidity curve", "renderer": "weather-v1"},
    "weather-compact": {"name": "桌面天气条", "description": "Compact current conditions and a readable trend on small panels", "renderer": "weather-v1"},
}
PALETTES = {"bw": ["#ffffff", "#000000"], "bwr": ["#ffffff", "#000000", "#b02020"],
            "six": ["#ffffff", "#000000", "#d02020", "#e8d520", "#2040c0", "#207040"]}


def find_font(path: str | None, locale: str) -> str:
    if path:
        if not Path(path).is_file():
            raise ValueError("Font file does not exist")
        return path
    candidates = (["C:/Windows/Fonts/msyh.ttc", "/System/Library/Fonts/PingFang.ttc",
                   "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"] if locale == "zh-CN" else [])
    candidates += ["C:/Windows/Fonts/arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
                   "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"] if locale == "en" else []
    for item in candidates:
        if Path(item).is_file():
            return item
    raise ValueError("No suitable system font found; specify --font-file (fonts are not redistributed)")


class Canvas:
    def __init__(self, width, height, font, palette):
        self.image = Image.new("RGB", (width, height), "white")
        self.draw = ImageDraw.Draw(self.image)
        self.font_path = font
        self.fonts = {}
        self.black, self.accent = "#000000", PALETTES[palette][2] if palette != "bw" else "#000000"
        self.rain = "#2040c0" if palette == "six" else self.black
        self.w, self.h = width, height

    def font(self, size):
        size = max(7, round(size))
        if size not in self.fonts:
            self.fonts[size] = ImageFont.truetype(self.font_path, size)
        return self.fonts[size]

    def text(self, xy, value, size, color=None, max_width=None, anchor="lt"):
        value = str(value)
        size = max(7, round(size))
        while max_width and self.draw.textlength(value, font=self.font(size)) > max_width and size > 7:
            size -= 1
        if max_width:
            while self.draw.textlength(value, font=self.font(size)) > max_width and len(value) > 1:
                value = value[:-2] + "…"
        self.draw.text(tuple(round(v) for v in xy), value, font=self.font(size), fill=color or self.black, anchor=anchor)

    def line(self, points, fill=None, width=1):
        self.draw.line([(round(x), round(y)) for x, y in points], fill=fill or self.black, width=max(1, round(width)))


def weather_words(code, zh):
    if code == 0: return "晴" if zh else "Clear"
    if code <= 3: return "多云" if zh else "Cloudy"
    if code <= 48: return "雾" if zh else "Fog"
    if code >= 95: return "雷雨" if zh else "Storm"
    if 71 <= code <= 77 or code in (85, 86): return "雪" if zh else "Snow"
    return "雨" if zh else "Rain"


def icon(c, x, y, size, code):
    d, stroke = c.draw, max(1, round(size / 22))
    # Geometric original icons; no downloaded icon set/font.
    if code <= 2:
        cx, cy, r = x + size * .36, y + size * .35, size * .18
        d.ellipse((cx-r, cy-r, cx+r, cy+r), outline=c.accent, width=stroke)
        for angle in range(0, 360, 45):
            a = math.radians(angle)
            c.line([(cx+math.cos(a)*r*1.35, cy+math.sin(a)*r*1.35),
                    (cx+math.cos(a)*r*1.7, cy+math.sin(a)*r*1.7)], c.accent, stroke)
        if code == 0: return
    left, top, right, bottom = x+size*.15, y+size*.43, x+size*.91, y+size*.77
    d.rounded_rectangle((left, top, right, bottom), radius=round(size*.13), fill="white", outline=c.black, width=stroke)
    d.ellipse((x+size*.35, y+size*.27, x+size*.73, y+size*.67), fill="white", outline=c.black, width=stroke)
    d.rectangle((x+size*.35+stroke, y+size*.55, x+size*.73-stroke, y+size*.74), fill="white")
    if code >= 50:
        for i in range(3):
            c.line([(left+size*(.2+i*.2), bottom+size*.04), (left+size*(.14+i*.2), bottom+size*.19)], c.rain, stroke)


def chart(c, box, data, zh, compact=False):
    from .host_scene import _shared
    x, y, w, h = box
    widget = {"width": w, "height": h, "style": {"fontSize": max(12, round(min(c.w,c.h)*.055)), "color": c.accent},
              "hostWeather": {"field": "temperature-chart"}}
    for item in _shared('weatherPrimitives', widget, data):
        if item['kind'] == 'text':
            value = item['text']
            if not zh and '24h' in value: value = '24h °C / %' if all('humidity' in hour for hour in data['hours']) else '24h °C'
            align = item.get('align', 'left')
            tx = x + item['x'] + item['width'] * {'left':0, 'center':.5, 'right':1}[align]
            c.text((tx,y+item['y']),value,item['size'],item['color'],max_width=item['width'],anchor={'left':'lt','center':'mt','right':'rt'}[align])
        elif item['kind'] == 'line':
            c.line([(x+item['x'],y+item['y']),(x+item['x2'],y+item['y2'])],item['color'],item['thickness'])
        else:
            c.draw.rectangle((round(x+item['x']),round(y+item['y']),round(x+item['x']+item['width']-1),round(y+item['y']+item['height']-1)),fill=item['color'])


def render_weather(data: dict, template: str, width: int, height: int, *, palette="bw", locale="en", font_file=None) -> Image.Image:
    validate_weather(data)
    if template not in TEMPLATES: raise ValueError("Unknown host template; Leafer scene import is not available yet")
    if palette not in PALETTES or locale not in {"en", "zh-CN"}: raise ValueError("Unsupported palette or locale")
    if isinstance(width, bool) or isinstance(height, bool) or not 128 <= width <= 2048 or not 96 <= height <= 2048 or width*height > 2097152:
        raise ValueError("Weather canvas must be 128..2048 by 96..2048, at most 2 Mi pixels")
    c = Canvas(width, height, find_font(font_file, locale), palette)
    zh, w, h = locale == "zh-CN", width, height
    compact = template == "weather-compact" or height < 220
    margin = max(5, round(min(w, h)*.04))
    title_size = max(10, min(w/23, h/14))
    stamp = data["observed_at"]
    label = "Hangzhou" if not zh and data.get("demo") else data["label"]
    c.text((margin, margin), label, title_size, max_width=w*.6)
    from .host_scene import _shared
    calendar = _shared('weatherCalendarLabels', data)
    c.text((w-margin, margin), calendar['weekday'] if zh else datetime.fromisoformat(stamp).strftime('%A'),
           title_size, anchor="rt")
    c.text((margin, margin+title_size*1.5), calendar['date']+'  '+calendar['lunar-date'] if zh else stamp[:10],
           title_size, max_width=w-margin*2)
    header = margin+title_size*3
    c.line([(margin, header), (w-margin, header)], width=2 if w>=600 else 1)
    if compact:
        split = w*.41
        size = min(h*.34, w*.17)
        c.text((margin, header+4), f"{data['temperature']:.0f}°", size, c.accent, max_width=split-margin)
        c.text((margin, header+size+5), weather_words(data["code"], zh), max(9, h/13))
        c.text((margin, h-24), f"{data['days'][0]['low']:.0f}–{data['days'][0]['high']:.0f}°  {data['humidity']:.0f}%", max(7, h/16))
        chart(c, (split, header+6, w-split-margin, h-header-18), data, zh, True)
    else:
        top_end = h*.42
        icon(c, margin, header+10, min(w*.16, h*.18), data["code"])
        c.text((w*.20, header+8), f"{data['temperature']:.0f}°", min(w*.14, h*.19), c.accent)
        c.text((w*.20, top_end-20), weather_words(data["code"], zh), max(12, min(w/28, h/22)))
        metrics = [("体感" if zh else "Feels", f"{data['feels_like']:.0f}°"),
                   ("湿度" if zh else "Humidity", f"{data['humidity']:.0f}%"),
                   ("风速" if zh else "Wind", f"{data['wind']:.0f} km/h")]
        for i, (key, value) in enumerate(metrics):
            yy = header+10+i*(top_end-header-5)/3
            if template == "weather-cards":
                c.draw.rounded_rectangle((round(w*.62), round(yy-2), w-margin, round(yy+h*.067)), radius=3, outline=c.accent)
            c.text((w*.64, yy), key, max(8, h/30), max_width=w*.14)
            c.text((w-margin-5, yy), value, max(9, h/26), anchor="rt", max_width=w*.19)
        chart(c, (margin, top_end+8, w-2*margin, h*.34), data, zh)
        y = h*.80
        c.line([(margin, y-5), (w-margin, y-5)])
        count = min(len(data["days"]), 5 if w >= 360 else 3)
        cell = (w-2*margin)/count
        for i, day in enumerate(data["days"][:count]):
            xx = margin+cell*i
            c.text((xx+cell/2, y), ("今天" if zh else "Today") if i == 0 else day["date"][5:], max(8, h/29), anchor="mt")
            c.text((xx+cell/2, y+h*.062), f"{day['low']:.0f} / {day['high']:.0f}°", max(9, h/25), c.accent, anchor="mt")
    # Device branding/authorization is not rasterized or removed here: it stays
    # in the ordinary device-side image pipeline, not in a host-side workaround.
    from PIL import ImageColor
    values = [channel for color in PALETTES[palette] for channel in ImageColor.getrgb(color)]
    palette_image = Image.new("P", (1, 1))
    palette_image.putpalette(values + values[:3] * (256-len(PALETTES[palette])))
    # Do not turn antialiased neutral text edges green/blue on a six-color
    # palette. Neutral pixels belong on the black/white axis only.
    pixels = c.image.load()
    for yy in range(height):
        for xx in range(width):
            r, g, b = pixels[xx, yy]
            if r == g == b and r not in (0, 255):
                pixels[xx, yy] = (0, 0, 0) if r < 160 else (255, 255, 255)
    return c.image.quantize(palette=palette_image, dither=Image.Dither.NONE).convert("RGB")
