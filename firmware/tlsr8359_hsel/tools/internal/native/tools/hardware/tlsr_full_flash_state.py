"""Encode current Full defaults and new Recovery state. No old-state migration."""
from __future__ import annotations
import hashlib
import struct
import zlib

from tlsr_factory_config import fnv

PAGE = 4096
SETTINGS = 0x59000
FLOOR = 0x5B000
FACTORY = 0x7F000
BASE_BYTES = 0x80000
CURRENT_IDS = frozenset((3, 4, 5, 6, 7, 8, 11, 13, 29))


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def erased(data: bytes) -> bool:
    return data == b'\xff' * len(data)


def validate_record(rid: int, data: bytes) -> None:
    """Structural admission, not a new activation/password authorization path."""
    from inktag_programmer_formats.protocol import decode_screen_config, encode_screen_config
    from inktag_programmer_formats.aux_io import decode_aux_configuration
    from inktag_programmer_formats.pin_config import decode_pin_block
    if rid not in CURRENT_IDS or not 0 < len(data) <= 420:
        raise ValueError('Unknown/empty current Settings record')
    valid = True
    if rid == 3:
        valid = (len(data) == 28 and data[:4] == b'SP\1\x11' and
                 data[21:24] == bytes(3) and fnv(data[:24]) == int.from_bytes(data[24:], 'little'))
        if valid:
            screen = decode_screen_config(data[4:21])
            valid = screen is not None and encode_screen_config(screen) == data[4:21]
    elif rid == 4:
        valid = (len(data) == 5 and data[0] == 1 and data[1] <= 6 and
                 int.from_bytes(data[3:], 'little') & ~0x8000 == 0 and
                 (data[1] != 6 or data[2] == 255))
    elif rid == 5:
        valid = 9 <= len(data) <= 420 and data[0] == 1 and data[1] in (0, 1)
        if valid:
            modules = data[2]; length = int.from_bytes(data[3:5], 'little')
            offset = 73 if data[1] else 9
            matrix = data[offset:]
            valid = (21 <= modules <= 57 and (modules - 21) % 4 == 0 and
                     len(data) == offset + length and length == (modules * modules + 7) // 8)
            if valid and data[1]:
                valid = (int.from_bytes(data[5:9], 'little') != 0 and
                         any(data[9:41]) and any(data[41:73]) and data[9:41] != data[41:73])
            elif valid:
                valid = zlib.crc32(matrix) & 0xffffffff == int.from_bytes(data[5:9], 'little')
            padding = len(matrix) * 8 - modules * modules
            if valid and padding:
                valid = not matrix[-1] & ((1 << padding) - 1)
    elif rid == 6:
        valid = len(data) == 46 and data[0] == 1 and data[1] <= 19 and data[2] <= 24
        if valid:
            for start, size, length in ((3, 19, data[1]), (22, 24, data[2])):
                field = data[start:start + length]
                field.decode('utf-8')
                valid &= not any(b < 32 or b == 127 for b in field)
            if data[1]:
                from inktag_programmer_formats.protocol import encode_bluetooth_name
                valid &= encode_bluetooth_name(data[3:3+data[1]].decode('utf-8'))==data[3:3+data[1]]
    elif rid == 7:
        valid = (len(data) == 64 and data[0] == 1 and data[39] in (0, 1) and
                 data[60:] == bytes(4) and data[18:20] == b'\2\1' and
                 data[20] <= 1 and not data[21] & ~0x11 and data[26] <= 12 and
                 (int.from_bytes(data[14:18], 'little') == 0 or
                  30 <= int.from_bytes(data[14:18], 'little') <= 86400))
        if valid:
            aux = bytes((1,)) + data[7:9] + data[56:60] + data[9:14]
            config = decode_aux_configuration(aux)
            valid &= config['buzzerMode'] in ('off', 'passiveSingle')
            valid &= all(data[i] <= 10 and data[i] != 8 for i in (1,3,5))
            name=data[27:27+data[26]].decode('utf-8')
            valid &= len(name)<=4 and not any(ord(c)<32 or ord(c)==127 for c in name)
            valid &= (int.from_bytes(data[22:26],'little')==0)==(not name)
            aux_pins={aux[1],aux[3],aux[5],aux[8],aux[9]}-{255}
            valid &= all(p==255 or p<32 for p in aux_pins)
            if data[39]:
                decode_pin_block(data[40:56])
                valid &= data[54:56]==b'\xff\xff' and all(
                    p==255 or (p<32 and p not in aux_pins) for p in data[44:56])
            else:
                valid &= data[40:56] == bytes(16)
    elif rid == 8:
        # Native envelope only. Immutable binding/signature admission belongs
        # to the shared verifier and is repeated by the actual writer.
        valid = len(data) == 96
    elif rid == 11:
        valid = len(data) == 2 and data[0] == 1 and data[1] <= 2
    elif rid == 13:
        valid = len(data)>=3 and data[0] == 1 and data[1] <= 1 and data[2] == 0
        valid &= len(data) == 3 or (5 <= len(data) <= 126 and data[3] == len(data) - 4)
    elif rid == 29:
        valid = len(data) == 3 and data[0] == 1 and data[1] <= 1 and data[2] <= 1
    if not valid:
        raise ValueError(f'Invalid current default Settings payload ID{rid}')


def encode_settings(generation: int, records: dict[int, bytes]) -> bytes:
    page = bytearray(b'\xff' * PAGE)
    page[:16] = struct.pack('<4sIII', b'ISP1', generation & 0xffffffff,
                            ~generation & 0xffffffff, 0xffffffff)
    offset = 16
    for rid, data in sorted(records.items()):
        validate_record(rid, data)
        key = struct.pack('<HH', rid, len(data))
        record = b'ISR1' + key + struct.pack('<I', fnv(key + data))
        record += data + b'\xff' * (-len(data) % 4) + b'ISC1'
        if offset + len(record) > PAGE:
            raise ValueError('Settings live set exceeds one generation; no erase')
        page[offset:offset + len(record)] = record; offset += len(record)
    return bytes(page)


def managed_records(name: str) -> tuple[dict[int, bytes],dict]:
    from inktag_programmer_formats.device_link import build_device_link
    from inktag_programmer_formats.device_qr import encode_device_qr
    from inktag_programmer_formats.nfc_command import encode_uri_ndef
    link=build_device_link(name);qr=encode_device_qr(link);ndef=encode_uri_ndef(link)
    return {5:struct.pack('<BBBHI',1,0,qr.modules,len(qr.data),zlib.crc32(qr.data)&0xffffffff)+qr.data,
            13:bytes((1,0,0,len(ndef)))+ndef},dict(applicationName=name,managedLink=link)


def check_dependencies() -> dict:
    """Exercise lazy default/QR/NDEF imports before any port or target access."""
    try:
        managed_records('IKT_TL_0000')
        validate_record(11, b'\1\1')
    except ImportError as error:
        raise ValueError('Missing bundled programmer formats or QR dependency; reinstall the complete tools and run setup.ps1. No CLI installation is required; no device accessed.') from error
    return dict(defaultFormatsAvailable=True, deviceAccess=False)


def defaults(mac: bytes, screen: bytes, pins: bytes, aux: bytes, board_id: int) -> tuple[dict[int, bytes], dict]:
    from tlsr_full_flash_config import validate_aux_wire
    if len(mac) != 6:
        raise ValueError('Exact selected address required')
    validate_aux_wire(aux, pins, board_id)
    name = f'IKT_TL_{mac[1]:02X}{mac[0]:02X}'
    label = f'InkTag {mac[1]:02X}{mac[0]:02X}'
    managed,links=managed_records(name)
    body = b'SP\1\x11' + screen + bytes(3)
    device = bytearray(64)
    device[:7] = bytes((1, 1, 0, 10, 0, 9, 0))
    device[7:9] = aux[1:3]; device[9:14] = aux[7:12]; device[56:60] = aux[3:7]
    device[14:18] = (3600).to_bytes(4, 'little')
    device[18:27] = bytes((2, 1, 1, 0, 0, 0, 0, 0, 0))
    records = {3: body + struct.pack('<I', fnv(body)), 4: b'\1\1\xff\0\0',
        6: bytes((1, len(name), len(label))) + name.encode().ljust(19, b'\0') + label.encode().ljust(24, b'\0'),
        7: bytes(device), 11: b'\1\1',29:b'\1\0\1',**managed}
    for rid, data in records.items():
        validate_record(rid, data)
    return records, dict(deviceLabel=label,**links)


def floor_seed(epoch: int, generation: int = 1) -> bytes:
    if type(epoch) is not int or not 0 <= epoch <= 65535:
        raise ValueError('Actual uint16 security epoch required')
    body = struct.pack('<4s4sIIBBHII', b'IRJ1', b'1RLT', generation & 0xffffffff, 0,
                       3, 1, 65535, epoch, 0xffffffff)
    return body + struct.pack('<I', zlib.crc32(body) & 0xffffffff) + b'\xff' * (PAGE - 32)


def initialize_full_state(seed: dict[int,bytes], epoch: int, *, confirmed: bool,
                          activation: bytes | None = None) -> dict:
    """New generation, with only an independently admitted native Pro token."""
    if confirmed is not True:
        raise ValueError('Full user-data wipe needs y/N or explicit Yes confirmation')
    records=dict(seed)
    if activation is not None:
        validate_record(8, activation)
        records[8] = activation
    writes=[(FLOOR,floor_seed(epoch),'security-floor'),
            (FLOOR+PAGE,b'\xff'*PAGE,'recovery-state-clear'),
            (SETTINGS,encode_settings(1,records),'settings-generation'),
            (SETTINGS+PAGE,b'\xff'*PAGE,'settings-clear')]
    identity=records[6];name=identity[3:3+identity[1]].decode('utf-8')
    return dict(writes=writes,epoch=epoch,
        application_name_hint=name,
        semantic_signature_authority='unchanged device validators, not host restoration')


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser(description='Offline programmer default-data dependency check')
    parser.add_argument('--check-dependencies', action='store_true', required=True)
    parser.parse_args()
    try:
        print(json.dumps(check_dependencies()))
    except ValueError as error:
        parser.error(str(error))
