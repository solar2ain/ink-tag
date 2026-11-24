"""Public Goodix native image/container codec; no publisher or key imports."""
import hashlib
import json
import struct
import zipfile

BOARDS = {
    'gr5331_zkc21v': dict(chip='GR5331', active=0x00220000, slot_size=0x40000,
        layout_marker=int.from_bytes(b'G3V1', 'little'), comment=b'inktag5331',
        recovery_a=0x00268000, recovery_b=0x00269000,
        image_store_start=0x0026a000, image_store_size=0x14000),
    'gr551x_zkc42v': dict(chip='GR551x', active=0x01020000, slot_size=0x52000,
        layout_marker=int.from_bytes(b'G5V1', 'little'), comment=b'inktag551x',
        recovery_a=0x0107a000, recovery_b=0x0107b000,
        image_store_start=0x0107c000, image_store_size=0x80000),
    'gr5513_zkel_internal': dict(chip='GR551x', active=0x01020000, slot_size=0x44000,
        layout_marker=int.from_bytes(b'GIV1', 'little'), comment=b'inktag5513i',
        recovery_a=0x0106c000, recovery_b=0x0106d000,
        image_store_start=0x0106e000, image_store_size=0x10000),
}


def checksum(data):
    return sum(data) & 0xffffffff


def image_info(data, board, epoch):
    profile = BOARDS[board]
    return struct.pack('<HH6I12sII', 0x4744, epoch, len(data), checksum(data),
        profile['active'], profile['active'], 3, 0x744, profile['comment'],
        profile['layout_marker'], profile['layout_marker'] ^ 0xffffffff)


def write_zip(path, members):
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_STORED) as package:
        for name, raw in members.items():
            item = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            item.create_system = 3
            item.external_attr = 0o100644 << 16
            package.writestr(item, raw)


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True)+'\n').encode('utf-8')


def record(raw):
    return dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
