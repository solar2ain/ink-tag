"""InkTag-only PN532 CLI. No firmware update, BLE, PIN or tag EEPROM writes.

The separately licensed PN532/ISO-DEP provider is pinned in tools/nfc/provider
and loaded at runtime. InkTag pixel/LZO codecs and the Type2 pipe are reused.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import io
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'cli/src'))
from inktag_cli.image_encoding import RgbaFrame, canvas_size_for_screen, encode_image_with_format
from inktag_cli.models import ScreenConfig
from inktag_cli.lzo1x import encode_lzo1x

D1 = bytes.fromhex('00d1000000')
D4 = bytes.fromhex('f0d4000000')
DE = bytes.fromhex('f0de000001')


@dataclass(frozen=True)
class Profile:
    width: int
    height: int
    x: int
    y: int
    visible_width: int
    visible_height: int
    color: int
    packing: int
    transform: int
    lzo: bool
    streamed: bool
    compact: bool = False
    grouped: bool = False

    @property
    def screen(self):
        # Driver0 is unknown, never a claimed controller. Packing is explicit.
        return ScreenConfig(1, 0, self.color, self.transform, self.width, self.height,
                            self.x, self.y, self.visible_width, self.visible_height)

    @property
    def planes(self):
        return 2 if self.packing in (2, 4, 6) else 1

    @property
    def plane_bytes(self):
        shift = (0, 3, 3, 2, 3, 1, 2)[self.packing]
        return ((self.width + (1 << shift) - 1) >> shift) * self.height



def parse_d1(reply: bytes, uid: bytes):
    if not reply.endswith(b'\x90\x00'):
        raise ValueError('价签未返回可用的屏幕信息。')
    fields = {}
    at = 0
    while at < len(reply) - 2:
        if at + 2 > len(reply) - 2:
            raise ValueError('D1 TLV header is truncated')
        tag, size = reply[at:at+2]
        at += 2
        if tag in fields or at + size > len(reply) - 2:
            raise ValueError('D1 TLV is duplicated or truncated')
        fields[tag] = reply[at:at+size]
        at += size
    if fields.get(0xc0) != b'IKT1' or fields.get(0xc1) != uid:
        raise ValueError('价签身份不匹配，未发送图片。')
    meta = fields.get(0xe0, b'')
    if len(meta) != 15:
        raise ValueError('D1 lacks the15-byte native E0 descriptor')
    w, h, x, y, vw, vh, color, packing, transform = struct.unpack('>6H3B', meta)
    if (not 8 <= w <= 2048 or not 8 <= h <= 2048 or
            not 8 <= vw <= w or not 8 <= vh <= h or x + vw > w or y + vh > h or
            packing not in range(1, 7) or transform > 15 or
            color not in (1, 2, 3, 4, 5, 6, 7, 8, 10)):
        raise ValueError('D1 native geometry, color, packing or transform is invalid')
    if packing == 6 and color != 10:
        raise ValueError('D1 two-area six-color packing has an incompatible color ID')
    stream = fields.get(0xe2, b'')
    compression = fields.get(0xd1, b'')
    profile = Profile(w, h, x, y, vw, vh, color, packing, transform,
                      len(compression) == 7 and compression[0] == 1,
                      len(stream) == 2 and stream[0] == 1 and bool(stream[1] & 1),
                      len(stream) == 2 and stream[0] == 1 and bool(stream[1] & 2),
                      len(stream) == 2 and stream[0] == 1 and stream[1]&6==6)
    if profile.plane_bytes * profile.planes > 2_097_152:
        raise ValueError('图片尺寸超出工具支持范围。')
    return profile, fields


def palette_for(profile: Profile):
    palette = [(0, 0, 0), (255, 255, 255)]
    if profile.color == 4:
        palette += [(85, 85, 85), (170, 170, 170)]
    elif profile.color in (3, 10):
        palette += [(220, 48, 38), (235, 194, 25)]
        if profile.color == 10:
            palette += [(0, 0, 255), (41, 204, 20)]
    elif profile.color != 1:
        palette += [{2: (220, 48, 38), 5: (235, 194, 25), 6: (44, 97, 165),
                     7: (19, 116, 99), 8: (225, 139, 35)}[profile.color]]
    return palette


def encode_pixels(profile, image):
    rgba = image.convert('RGBA')
    encoded = encode_image_with_format(RgbaFrame(image.width, image.height, rgba.tobytes()), profile.screen,
                                       profile.packing, six_color=profile.color == 10,
                                       native_rows=True)
    transfers = tuple(t.data for t in encoded.transfers)
    if len(transfers) != profile.planes or any(len(t) != profile.plane_bytes for t in transfers):
        raise ValueError('Shared codec byte lengths do not match D1; no DATA sent')
    return transfers


def make_image(profile, path, *, fit='contain', dither='floyd'):
    from PIL import Image, ImageOps
    data = path.read_bytes()
    with Image.open(io.BytesIO(data)) as source:
        oriented = ImageOps.exif_transpose(source).convert('RGBA')
        flattened = Image.new('RGBA', oriented.size, 'white')
        flattened.alpha_composite(oriented)
        source = flattened.convert('RGB')
    size = canvas_size_for_screen(profile.screen)
    if fit == 'cover':
        image = ImageOps.fit(source, size, method=Image.Resampling.LANCZOS)
    else:
        scaled = ImageOps.contain(source, size, method=Image.Resampling.LANCZOS)
        image = Image.new('RGB', size, 'white')
        image.paste(scaled, ((size[0]-scaled.width)//2, (size[1]-scaled.height)//2))
    colors = palette_for(profile)
    palette = Image.new('P', (1, 1))
    palette.putpalette([v for rgb in colors for v in rgb] + list(colors[0])*(256-len(colors)))
    image = image.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG
                           if dither == 'floyd' else Image.Dither.NONE).convert('RGB')
    return encode_pixels(profile, image)


def packets(transfers, mode):
    for plane, data in enumerate(transfers):
        if mode == 'd2':
            for index, at in enumerate(range(0, len(data), 250)):
                chunk = data[at:at+250]
                yield bytes((0xf0, 0xd2, plane, index & 255, len(chunk))) + chunk
        else:
            for block, at in enumerate(range(0, len(data), 2000)):
                compressed = encode_lzo1x(data[at:at+2000])
                if len(compressed) > 2192:
                    raise ValueError('LZO block exceeds device workspace')
                for sub, offset in enumerate(range(0, len(compressed), 250)):
                    chunk = compressed[offset:offset+250]
                    yield bytes((0xf0, 0xd3, plane, int(offset+len(chunk) == len(compressed)),
                                 len(chunk)+2, block & 255, sub)) + chunk


def display_profile(profile):
    colors = {1: '黑白', 2: '黑白红', 3: '黑白红黄', 4: '四级灰度',
              5: '黑白黄', 6: '黑白蓝', 7: '黑白绿', 8: '黑白橙', 10: '六色'}
    print(f'屏幕 {profile.width}×{profile.height}，{colors[profile.color]}', flush=True)


def send_image(link, args, profile):
    transfers = make_image(profile, args.image, fit=args.fit, dither=args.dither)
    compressed = profile.lzo
    prepared = list(packets(transfers, 'd3' if compressed else 'd2'))
    payload_size = sum(len(packet) - (7 if compressed else 5) for packet in prepared)
    link.set_streamed(profile.streamed)
    if profile.compact:
        link.enable_compact()
    if profile.grouped:
        link.enable_grouped()

    started = time.monotonic()
    step = 16 if link.grouped else 1
    for offset in range(0, len(prepared), step):
        group = prepared[offset:offset + step]
        if link.grouped:
            link.exchange_group(group)
        elif link.exchange(group[0]) != b'\x90\0':
            raise RuntimeError('设备未接受后续图片数据。')
        print(f'已确认 {offset + len(group)}/{len(prepared)}', flush=True)
    elapsed = time.monotonic() - started
    print(f'图片数据已确认，{payload_size:,} 字节，用时 {elapsed:.1f} 秒。', flush=True)

    print('请求刷新。', flush=True)
    try:
        acknowledged = link.exchange(D4)
    except (OSError, RuntimeError, ValueError) as error:
        raise RuntimeError('未收到刷新回执，请查看屏幕。') from error
    if acknowledged != b'\x90\0':
        raise RuntimeError('设备未接受刷新。')
    if not args.wait_refresh:
        print('设备已接受刷新，可以移开标签并等待屏幕更新。')
        return 0

    print('设备已接受刷新，保持贴近直到刷新完成。', flush=True)
    link.set_wait(3.0)
    deadline = time.monotonic() + 120
    try:
        while time.monotonic() < deadline:
            reply = link.exchange(DE)
            if reply == b'\0\x90\0':
                print('刷新完成，可以移开标签。')
                return 0
            if reply != b'\1\x90\0':
                break
            time.sleep(1)
    except (OSError, RuntimeError, ValueError):
        pass
    print('设备已接受刷新，尚未确认屏幕更新完成。请查看屏幕。', file=sys.stderr)
    return 6


def execute(args):
    from inktag_nfc_reader import Link
    from inktag_nfc_ndef import Type4Ndef, decode_records, text_record, type2_command, type2_read

    link = Link(args)
    try:
        link.connect()
        print(f'已连接 {"Type 4" if link.kind == "iso-dep" else "Type 2"} 标签。', flush=True)
        if args.command == 'read':
            raw = Type4Ndef(link).read() if link.kind == 'iso-dep' else type2_read(link.pipe)
            records = [record.get('uri') or record.get('text') for record in decode_records(raw)]
            for value in filter(None, records):
                print(value)
            if not any(records):
                print('标签没有可显示的链接或文本。')
        elif args.command in ('send-cmd', 'clear', 'sync-time'):
            record = text_record(f'time#{int(time.time())}' if args.command == 'sync-time' else args.text)
            if link.kind == 'iso-dep':
                Type4Ndef(link).command(record)
            else:
                type2_command(link, record)
            print('命令已下发，请在价签上确认执行结果。')
        else:
            profile, _ = parse_d1(link.exchange(D1), link.uid)
            display_profile(profile)
            if args.command == 'send-image':
                return send_image(link, args, profile)
        return 0
    finally:
        link.close()


def parse_uid(value):
    try:
        uid = bytes.fromhex(value.replace(':', ''))
    except ValueError as error:
        raise argparse.ArgumentTypeError('UID 须为十六进制。') from error
    if len(uid) not in (4, 7, 10):
        raise argparse.ArgumentTypeError('UID 长度须为 4、7 或 10 字节。')
    return uid


def parser():
    root = argparse.ArgumentParser(description='InkTag NFC 工具，支持 PN532/PCR532 及兼容的 Type 2、Type 4 价签。')
    commands = root.add_subparsers(dest='command', required=True)
    commands.add_parser('ports', help='列出串口')
    help_text = {'read': '读取标签链接或文本', 'probe': '读取价签屏幕信息',
                 'send-cmd': '发送 NFC 文本命令', 'clear': '清屏', 'sync-time': '同步电脑时间',
                 'send-image': '发送本地图片并刷新'}
    for name, description in help_text.items():
        command = commands.add_parser(name, help=description)
        command.add_argument('--port', help='读卡器串口，例如 COM18')
        command.add_argument('--uid', type=parse_uid, help='只操作指定的标签 UID')
        if name == 'send-cmd':
            command.add_argument('text', help='例如 calendar、device 或 slot-3')
        if name == 'clear':
            command.set_defaults(text='clear')
        if name == 'send-image':
            command.add_argument('image', type=Path, help='本地图片，例如 PNG 或 JPEG')
            command.add_argument('--fit', choices=('contain', 'cover'), default='contain')
            command.add_argument('--dither', choices=('floyd', 'none'), default='floyd')
            command.add_argument('--wait-refresh', action='store_true', help='等待价签确认刷屏完成')
    return root


def main(argv=None):
    root = parser()
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        root.print_help()
        return 0
    args = root.parse_args(argv)
    if args.command == 'send-image' and not args.image.is_file():
        root.error(f'图片文件不存在：{args.image}')
    if args.command in ('send-cmd', 'clear'):
        from inktag_nfc_ndef import text_record
        try:
            text_record(args.text)
        except ValueError as error:
            root.error(str(error))
    try:
        if args.command == 'ports' or args.port is None:
            from serial.tools.list_ports import comports
            ports = list(comports())
            if args.command == 'ports':
                for port in ports:
                    print(f'{port.device}: {port.description}')
                return 0
            if len(ports) != 1:
                root.error('请用 --port 指定读卡器串口，可用 ports 查看。')
            args.port = ports[0].device
        return execute(args)
    except KeyboardInterrupt:
        print('\n已停止。如已请求刷新，请查看屏幕。', file=sys.stderr)
        return 130
    except ModuleNotFoundError:
        print('请先运行 python -m pip install -r tools/nfc/requirements.txt 安装依赖。', file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError) as error:
        print(f'操作失败：{error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
