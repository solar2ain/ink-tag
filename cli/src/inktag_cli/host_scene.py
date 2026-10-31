"""Render exported editor scenes using the shared geometry/weather plan. No BLE or network."""
from __future__ import annotations

import base64
from importlib.resources import files
from io import BytesIO
import json
import hashlib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from .host_render import find_font
from .host_weather import validate_weather as validate_snapshot

MAX_SCENE_BYTES = 8 * 1024 * 1024


def _shared(function, *args):
    from py_mini_racer import MiniRacer
    try:
        with MiniRacer() as context:
            context.set_hard_memory_limit(128 * 1024 * 1024)
            context.eval(files('inktag_cli._assets').joinpath('hostScene.js').read_text(encoding='utf-8'))
            context.eval('globalThis.invokeHost=(name,args)=>{try{return {ok:true,value:InkTagHostScene[name](...args)}}catch(e){return {ok:false,error:e.message}}}')
            result = context.call('invokeHost', function, list(args), timeout=5000)
            if not result['ok']: raise ValueError(result['error'])
            return result['value']
    except FileNotFoundError as error:
        raise ValueError('Missing hostScene.js; install the complete current CLI package') from error
    except ValueError:
        raise
    except Exception as error:
        raise ValueError('Host scene processing failed (' + type(error).__name__ + ')') from error


def read_scene_with_hash(path: Path) -> tuple[dict, str]:
    with path.open('rb') as stream: payload = stream.read(MAX_SCENE_BYTES + 1)
    if len(payload) > MAX_SCENE_BYTES:
        raise ValueError('Host scene exceeds 8 MiB')
    return _shared('parseHostScene', payload.decode('utf-8')), hashlib.sha256(payload).hexdigest()


def read_scene(path: Path) -> dict:
    return read_scene_with_hash(path)[0]


def scene_plan(doc: dict, weather: dict) -> dict:
    return _shared('scenePlan', json.dumps(doc, ensure_ascii=False), validate_snapshot(weather))


def render_scene(doc: dict, weather: dict, *, locale='zh-CN', font_file=None) -> Image.Image:
    plan = scene_plan(doc, weather)
    font_path = find_font(font_file, locale)
    fonts = {}
    def font(size):
        size = max(6, min(240, round(size)))
        if size not in fonts: fonts[size] = ImageFont.truetype(font_path, size)
        return fonts[size]
    def text(layer, value, box, size, color, align='left', bold=False, vertical='middle'):
        x, y, w, h = box
        # Clip per control, never shrink text silently to fit.
        tile = Image.new('RGBA', (max(1, round(w)), max(1, round(h))))
        draw = ImageDraw.Draw(tile)
        face = font(size)
        bounds = draw.multiline_textbbox((0, 0), str(value), font=face, spacing=4)
        tw, th = bounds[2] - bounds[0], bounds[3] - bounds[1]
        tx = max(0, tile.width - tw) * {'left': 0, 'center': .5, 'right': 1}.get(align, 0) - bounds[0]
        ty = max(0, tile.height - th) * {'top': 0, 'middle': .5, 'bottom': 1}.get(vertical, .5) - bounds[1]
        draw.multiline_text((tx, ty), str(value), font=face, fill=color, spacing=4, align=align or 'left', stroke_width=1 if bold else 0)
        layer.alpha_composite(tile, (round(x), round(y)))
    image = Image.new('RGBA', (plan['width'], plan['height']), 'white')
    for widget in plan['widgets']:
        w, h = max(1, round(widget['width'])), max(1, round(widget['height']))
        layer = Image.new('RGBA', (w, h))
        draw = ImageDraw.Draw(layer)
        if widget.get('primitives') is not None:
            background = widget['style'].get('background', 'transparent')
            if background != 'transparent': draw.rectangle((0,0,w,h),fill=background)
            for p in widget['primitives']:
                if p['kind'] == 'text': text(layer, p['text'], (p['x'], p['y'], p['width'], p['height']), p['size'], p['color'], p.get('align'), p.get('bold', False))
                elif p['kind'] == 'line': draw.line((p['x'], p['y'], p['x2'], p['y2']), fill=p['color'], width=max(1, round(p['thickness'])))
                else: draw.rectangle((p['x'], p['y'], p['x'] + p['width'] - 1, p['y'] + p['height'] - 1), fill=p['color'])
        elif widget['kind'] == 'stroke':
            for run in widget['runs']: draw.line((run['x'], run['y'], run['x'] + run['width'] - 1, run['y']), fill=widget['style']['color'])
        else:
            for cell in widget['cells']:
                x, y = round(cell['x'] - widget['x']), round(cell['y'] - widget['y'])
                cw, ch = max(1, round(cell['width'])), max(1, round(cell['height']))
                tile = Image.new('RGBA', (cw, ch), (0,0,0,0) if cell['background']=='transparent' else cell['background'])
                edge = ImageDraw.Draw(tile)
                if cell['kind'] == 'line': edge.rectangle((0, 0, cw, ch), fill=cell['color'])
                elif cell['kind'] == 'image' and cell.get('image'):
                    payload = base64.b64decode(cell['image'].split(',', 1)[1], validate=True)
                    with Image.open(BytesIO(payload)) as source:
                        if source.width * source.height > 40_000_000: raise ValueError('Scene image exceeds 40 million pixels')
                        source = source.convert('RGBA')
                        scale = (max if cell['imageFit'] == 'cover' else min)(cw/source.width, ch/source.height)
                        source = source.resize((max(1, round(source.width*scale)), max(1, round(source.height*scale))), Image.Resampling.LANCZOS)
                        tile.alpha_composite(source, ((cw-source.width)//2, (ch-source.height)//2))
                    algorithm = widget.get('imageDither', 'source')
                    if algorithm != 'source':
                        from .image_encoding import RgbaFrame
                        from .image_processing import ImageProcessOptions, process_frame
                        options = widget.get('imageAdjustments', {})
                        strength = options.get('strength', 1)
                        processed = process_frame(RgbaFrame(cw, ch, tile.tobytes()), plan['color'], ImageProcessOptions(algorithm=algorithm,
                            contrast=options.get('contrast', 1), strength=min(1,strength) if algorithm=='none' else strength, heavy_ink=options.get('heavyInk',False)))
                        tile = Image.frombytes('RGBA', (cw, ch), bytes(processed.data))
                elif cell['kind'] == 'qr':
                    import qrcode
                    qr = qrcode.QRCode(border=4); qr.add_data(cell['text']); qr.make(fit=True)
                    matrix = qr.get_matrix(); count = len(matrix); size = min(cw, ch)
                    if size < count: raise ValueError('QR frame is too small')
                    code = Image.new('RGBA', (size,size), 'white'); modules = ImageDraw.Draw(code)
                    def edge(index): return (index*size+count//2)//count
                    for r, row in enumerate(matrix):
                        for c, dark in enumerate(row):
                            if dark: modules.rectangle((edge(c),edge(r),edge(c+1)-1,edge(r+1)-1), fill='black')
                    tile.alpha_composite(code, ((cw-code.width)//2, (ch-code.height)//2))
                else:
                    inset = cell['fontSize'] + 12 if cell['kind'] in {'todo', 'list'} else 0 if cell['kind'] == 'text' else 4
                    content = cell.get('richText')
                    if content:
                        metrics = []
                        for run in content['runs']:
                            style = {**cell, **run.get('style', {})}
                            for value in run['text'].split('\n'):
                                face = font(style['fontSize']); left, top, right, bottom = face.getbbox(value, anchor='ls')
                                metrics.append(dict(advance=face.getlength(value), left=left, top=top, right=right, bottom=bottom))
                        layout = _shared('layoutHostText', content, cell, max(1, cw-inset), ch, metrics)
                        for placed in layout['runs']:
                            style = placed['style']
                            edge.text((placed['x'] + inset, placed['y']), placed['run']['text'], font=font(style['fontSize']), fill=style['color'], anchor='ls', stroke_width=1 if style.get('bold') else 0)
                    else:
                        text(tile, cell['text'], (inset, 0, max(1, cw-inset), ch), cell['fontSize'], cell['color'], cell.get('align'), cell.get('bold', False))
                    if cell['kind'] == 'todo':
                        size = round(cell['fontSize']); cy = (ch-size)//2
                        edge.rectangle((4, cy, 4+size, cy+size), outline=cell['color'], fill=cell['color'] if cell.get('checked') else None)
                    elif cell['kind'] == 'list': edge.ellipse((5, ch//2-2, 9, ch//2+2), fill=cell['color'])
                if cell['border']:
                    edge = ImageDraw.Draw(tile); color = cell.get('borderColor', '#000000') or '#000000'
                    edge.line((0, 0, cw-1, 0), fill=color); edge.line((0, 0, 0, ch-1), fill=color)
                    if cell['lastCol']: edge.line((cw-1, 0, cw-1, ch-1), fill=color)
                    if cell['lastRow']: edge.line((0, ch-1, cw-1, ch-1), fill=color)
                layer.alpha_composite(tile, (x, y))
        image.alpha_composite(layer, (round(widget['x']), round(widget['y'])))
    # Render only authored controls. Data-source credit remains in snapshot
    # metadata and the client data-source panel, not an uneditable screen label.
    from .image_encoding import RgbaFrame
    from .image_processing import ImageProcessOptions, process_frame
    frame = process_frame(RgbaFrame(image.width, image.height, image.tobytes()), plan['color'], ImageProcessOptions(algorithm='none'))
    return Image.frombytes('RGBA', image.size, bytes(frame.data)).convert('RGB')
