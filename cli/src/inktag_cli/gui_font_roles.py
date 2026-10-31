"""Four user roles; one shared host compiler, with no device operation here."""
from __future__ import annotations

from importlib.resources import files
import json
from pathlib import Path
import struct
from typing import Any
from .generated_release_v1 import FONT_VERSION, FONT_INFO_VERSION, FONT_SCHEMA_MASK, FONT_ROLE_VERSION, FONT_FLAGS


MAX_PACKAGE_BYTES = 32 * 1024

def _compiler(name: str, *args: Any) -> Any:
    from py_mini_racer import MiniRacer
    with MiniRacer() as context:
        context.eval(files('inktag_cli._assets').joinpath('fontRoles.js').read_text(encoding='utf-8'))
        try:
            return context.call('InkTagFontRoles.' + name, *args)
        except Exception as error:
            raise ValueError(str(error)) from error


def validate_font_package(package: bytes, *, maximum_size: int = 32768) -> dict[str, Any]:
    if type(maximum_size) is not int or not 88 <= maximum_size <= 32768:
        raise ValueError('invalid native font capacity')
    if len(package) < 6 or package[:4] != b'IKFA' or struct.unpack_from('<H', package, 4)[0] != FONT_VERSION:
        raise ValueError('only IKFA v1 user fonts are supported')
    return _compiler('parsePackage', list(package), maximum_size)


def native_font_default_sizes(screen: dict[str, Any]) -> dict[str, Any]:
    """Query shipping C layout in the shared WASM, with no device connection."""
    from py_mini_racer import MiniRacer
    with MiniRacer() as context:
        for name in ('nativePreviewWasm.js', 'nativePreview.js'):
            context.eval(files('inktag_cli._assets').joinpath(name).read_text(encoding='utf-8'))
        context.eval('globalThis.nativeFontSizes=s=>InkTagNativePreview.create().defaultSizes(s)')
        try:
            return context.call('nativeFontSizes', screen)
        except Exception as error:
            raise ValueError(str(error)) from error


def decode_font_info(payload: bytes) -> dict[str, Any]:
    if len(payload) != 24 or payload[0] != FONT_INFO_VERSION:
        raise ValueError('native fonts require a 24-byte INFO v1 response')
    generation, size, maximum, crc = struct.unpack_from('<IIII', payload, 4)
    info = dict(version=FONT_INFO_VERSION, schema=payload[1], activeBank=None if payload[2] == 255 else payload[2],
                storageMode=payload[3], generation=generation, activeBytes=size,
                maximumBytes=maximum, crc32=crc, supportedSchemas=struct.unpack_from('<H', payload, 20)[0],
                roleVersion=payload[22], flags=payload[23])
    font_info_limit(info)
    return info


def font_info_limit(info: dict[str, Any]) -> int:
    contract = (info.get('supportedSchemas'), info.get('roleVersion'), info.get('flags'))
    if (info['version'] != FONT_INFO_VERSION or contract != (FONT_SCHEMA_MASK, FONT_ROLE_VERSION, FONT_FLAGS) or info['schema'] not in (0, FONT_VERSION) or info.get('storageMode') != 1 or
            type(info['maximumBytes']) is not int or not 88 <= info['maximumBytes'] <= 32768):
        raise ValueError('unsupported four-role font contract')
    if info['activeBank'] is None:
        if any(info[key] for key in ('schema', 'generation', 'activeBytes', 'crc32')):
            raise ValueError('inconsistent embedded font status')
    elif info['activeBank'] != 0 or info['schema'] == 0 or not 88 <= info['activeBytes'] <= info['maximumBytes']:
        raise ValueError('invalid four-role font status')
    return info['maximumBytes']


def admit_font_package(package: bytes, info: dict[str, Any]) -> dict[str, Any]:
    local = validate_font_package(package, maximum_size=font_info_limit(info))
    # The single user bank is erased by BEGIN. Match the device's serial
    # generation check before admitting any destructive transfer.
    if info['activeBank'] is not None:
        delta = (local['generation'] - info['generation']) & 0xffffffff
        if not 0 < delta < 0x80000000:
            raise ValueError('font generation must advance the active package; no transfer started')
    return local


def compose_font_roles(base: list[bytes], choices: dict[str, Any], generation: int,
                       large_digits: list[bytes] | None = None, maximum_size: int = 32768,
                       schema: int = FONT_VERSION) -> bytes:
    if schema != FONT_VERSION:
        raise ValueError('only IKFA v1 user fonts are supported')
    selected = {}
    for role, value in choices.items():
        selected[role] = {'characters': value.get('characters', '')}
        for size in ('small', 'large'):
            if size + 'Glyphs' in value:
                selected[role][size + 'Glyphs'] = value[size + 'Glyphs']
            else:
                selected[role][size] = list(value[size])
    faces = _compiler('compose', [list(font) for font in base], selected)
    if large_digits:
        if len(large_digits) != 2:
            raise ValueError('large clock/calendar digits require both small and large faces')
        faces.extend(list(font) for font in large_digits)
    return bytes(_compiler('buildPackage', faces, generation, maximum_size, schema))


def raster_role_source(path: Path, role: str, additional: str = '',
                       sizes: tuple[int, int] = (24, 32)) -> dict[str, Any]:
    """Rasterize one local typeface; the shared compiler fits both reference sizes."""
    from PIL import Image, ImageDraw, ImageFont
    characters = _compiler('requiredCharacters', role, additional)
    result: dict[str, Any] = {'characters': additional}
    for size, pixels in zip(('small', 'large'), sizes):
        if type(pixels) is not int or not 1 <= pixels <= 127:
            raise ValueError('native font size exceeds the U8g2 metric format')
        font = ImageFont.truetype(str(path), pixels)
        missing = font.getmask('\U0010ffff')
        glyphs = []
        for cp in characters:
            character = chr(cp);mask = font.getmask(character)
            if (mask.size == missing.size and bytes(mask) == bytes(missing)) or (cp != 32 and not mask.getbbox()):
                raise ValueError(f'{role}: source lacks U+{cp:04X}')
            left, top, right, bottom = font.getbbox(character, anchor='ls')
            width, height = right-left, bottom-top
            advance = max(0, round(font.getlength(character)))
            if width and height:
                image = Image.new('L', (width, height))
                ImageDraw.Draw(image).text((-left, -top), character, font=font, fill=255, anchor='ls')
                # Keep grayscale until the shared composer has fitted the
                # final native cell. Early thresholding loses thin strokes.
                data = list(image.tobytes())
            else:
                width = height = 0;data = []
            glyphs.append(dict(codePoint=cp, width=width, height=height,
                               x=left, y=-bottom, dx=advance, coverage=data))
        result[size + 'Glyphs'] = glyphs
    return result


def raster_large_digits(path: Path, height: int) -> bytes:
    """Local TTF/OTF rasterization into predictable cells, never tiny-font zoom."""
    from PIL import Image, ImageDraw, ImageFont
    if type(height) is not int or not 1 <= height <= 127:
        raise ValueError('native font height exceeds the U8g2 metric format')
    font = ImageFont.truetype(str(path), height * 2)
    characters = '0123456789:'
    missing = font.getmask('\U0010ffff')
    for character in characters:
        mask = font.getmask(character)
        if not mask.getbbox() or (mask.size == missing.size and bytes(mask) == bytes(missing)):
            raise ValueError(f'large digit source lacks {character!r}')
    boxes = [font.getbbox(c, anchor='ls') for c in characters]
    top, bottom = min(b[1] for b in boxes), max(b[3] for b in boxes)
    width = max(1, height * 5 // 8)
    padding = min(max(0, (width-1)//2), max(1, height // 32))
    advance, colon_advance = width + padding, max(1, width // 2)
    scale = min((width - 2 * padding) / max(b[2] - b[0] for b in boxes[:10]),
                (height - 2 * padding) / (bottom - top),
                max(1, colon_advance - 2) / max(1, boxes[10][2] - boxes[10][0]))
    glyphs = []
    for glyph, (character, box) in enumerate(zip(characters, boxes)):
        w, h = box[2] - box[0], box[3] - box[1]
        source = Image.new('L', (max(1, w), max(1, h)))
        ImageDraw.Draw(source).text((-box[0], -box[1]), character, font=font, fill=255, anchor='ls')
        bitmap = source.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.Resampling.LANCZOS)
        cell = Image.new('L', (width, height))
        allowed = colon_advance if glyph == 10 else width
        cell.paste(bitmap, ((allowed - bitmap.width) // 2, padding + round((box[1] - top) * scale)))
        pixels = cell.load()
        cell_width = colon_advance if glyph == 10 else advance
        glyphs.append(dict(codePoint=ord(character), width=cell_width, height=height,
            x=0, y=0, dx=cell_width,
            pixels=[int(x < width and pixels[x, y] >= 128)
                    for y in range(height) for x in range(cell_width)]))
    return bytes(_compiler('compressNativeDigits', glyphs))


def build_font_config(config_path: Path, generation: int = 1) -> bytes:
    config = json.loads(config_path.read_text(encoding='utf-8-sig'))
    if config.get('schema') != 1 or set(config) - {'schema', 'base', 'screen', 'numbers', 'latin', 'chinese', 'largeDigits'}:
        raise ValueError('native font configuration requires schema1 and known options')
    def resolve(name: str) -> Path:
        return config_path.parent / name
    package = resolve(config['base']).read_bytes()
    parsed = validate_font_package(package)
    base = [package[e['offset']:e['offset'] + e['bytes']] for e in parsed['faces'][:4]]
    choices = {}
    for role in ('numbers', 'latin', 'chinese'):
        if role in config:
            value = config[role]
            if set(value) - {'font', 'small', 'large', 'characters', 'smallSize', 'largeSize'}:
                raise ValueError(f'unknown {role} option')
            if 'font' in value:
                if 'small' in value or 'large' in value:
                    raise ValueError('choose a local typeface or a U8g2 pair, not both')
                sizes = (value.get('smallSize', 12), value.get('largeSize', 16))
                choices[role] = raster_role_source(resolve(value['font']), role, value.get('characters', ''), sizes)
            else:
                if 'smallSize' in value or 'largeSize' in value:
                    raise ValueError('U8g2 files already carry their native metrics')
                choices[role] = {size: resolve(value[size]).read_bytes() for size in ('small', 'large')}
                choices[role]['characters'] = value.get('characters', '')
    large = None
    if config.get('largeDigits'):
        value = config['largeDigits']
        if 'font' in value and not set(value) - {'font', 'smallSize', 'largeSize'}:
            sizes = None
            if 'smallSize' not in value or 'largeSize' not in value:
                if not isinstance(config.get('screen'), dict):
                    raise ValueError('automatic digit sizes require screen dimensions; or specify both sizes')
                sizes = native_font_default_sizes(config['screen'])
            heights = (value.get('smallSize', sizes['small'] if sizes else None),
                       value.get('largeSize', sizes['large'] if sizes else None))
            large = [raster_large_digits(resolve(value['font']), height) for height in heights]
        elif set(value) == {'small', 'large'}:
            large = [resolve(value[size]).read_bytes() for size in ('small', 'large')]
        else:
            raise ValueError('largeDigits requires a local font or both native U8g2 masters')
    return compose_font_roles(base, choices, generation, large)
