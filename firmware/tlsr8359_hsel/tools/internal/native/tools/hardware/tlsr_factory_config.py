"""Pure bounded vendor Factory codecs. No identity/Flash/GPIO operations."""
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]

def layout():
    # Public factory contract; host parity tests bind these to the owning C
    # header. Ordinary portable Full must not require private source files.
    result=dict(PIN_FACTORY_ADDRESS=0x7F110,PIN_RECORD_BYTES=32,
                AUX_FACTORY_ADDRESS=0x7F130,AUX_FACTORY_BYTES=16,
                SCREEN_FACTORY_ADDRESS=0x7F140,SCREEN_FACTORY_BYTES=32)
    if (result['PIN_RECORD_BYTES']!=32 or result['SCREEN_FACTORY_BYTES']!=32 or
        result['AUX_FACTORY_BYTES']!=16 or result['AUX_FACTORY_ADDRESS']!=0x7F130 or
        result['PIN_FACTORY_ADDRESS']+32!=result['AUX_FACTORY_ADDRESS'] or
        result['AUX_FACTORY_ADDRESS']+16!=result['SCREEN_FACTORY_ADDRESS'] or
        not 0x7F10D<=result['PIN_FACTORY_ADDRESS']<result['SCREEN_FACTORY_ADDRESS']<=0x7FFE0):
        raise ValueError('Unreviewed Factory envelope layout')
    return result

AUX_CAPABILITY=b'INKTAG-TL-AUX3\0'
PIN_CONFIG_CAPABILITY=b'INKTAG-TL-PINS2\0'

def aux_record(wire, pins, backend):
    from tlsr_full_flash_config import validate_aux_wire
    wire=validate_aux_wire(wire,pins,backend)
    return wire+struct.pack('<I',fnv(wire))

def validate_aux_record(data,pins,backend):
    if len(data)!=16:raise ValueError('Invalid aux Factory length')
    if data==b'\xff'*16:return None
    if data!=aux_record(data[:12],pins,backend):raise ValueError('Damaged aux Factory checksum')
    return data[:12]

def fnv(data):
    value=2166136261
    for byte in data: value=((value^byte)*16777619)&0xffffffff
    return value

def pin_record(backend,wire):
    if type(backend) is not int or not 1<=backend<=255 or len(wire)!=16:
        raise ValueError('Invalid backend/pin record length')
    body=struct.pack('<4sBBBB',b'IPN1',1,16,backend,0)+wire
    return body+struct.pack('<II',fnv(body),0x314D4350)

def screen_record(backend,wire):
    if type(backend) is not int or not 1<=backend<=255 or len(wire)!=17:
        raise ValueError('Invalid backend/screen record length')
    body=struct.pack('<4sBB',b'ISC1',backend,17)+wire+b'\0'
    return body+struct.pack('<II',fnv(body),0x314D4353)

def validate_records(backend,pins,screen):
    if pins!=pin_record(backend,pins[8:24]) or screen!=screen_record(backend,screen[6:23]):
        raise ValueError('Damaged/mismatched Factory envelope')
    # Semantic screen validation uses the same shared client wire contract.
    import sys
    from inktag_programmer_formats.protocol import decode_screen_config
    decode_screen_config(screen[6:23])
    return pins[8:24],screen[6:23]
