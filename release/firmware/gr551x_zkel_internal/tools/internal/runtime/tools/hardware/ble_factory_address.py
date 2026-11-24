"""Host equivalent of the frozen common BLE address codec; no device IO.

Canonical MSB-first bytes and explicit type. Shared across Full tool callers;
tests compare this codec/hash against the actual common C implementation.
"""
import hashlib
import zlib

def validate(address: bytes, kind: int) -> bytes:
    if len(address)!=6 or type(kind) is not int or kind not in (0,1):
        raise ValueError('Invalid ordinary address/type')
    if kind==1 and address[0]&0xc0!=0xc0:
        raise ValueError('Static-random address requires high bits11')
    random=bytes([address[0]&0x3f])+address[1:] if kind else address
    if random==bytes(6) or random==(b'\x3f'+b'\xff'*5 if kind else b'\xff'*6):
        raise ValueError('Reserved ordinary address')
    return address

def derive(namespace: int, uid: bytes) -> tuple[bytes,int]:
    if not 0<namespace<=65535 or not 1<=len(uid)<=32 or uid in (bytes(len(uid)),b'\xff'*len(uid)):
        raise ValueError('Verified complete UID required')
    raw=bytearray(hashlib.sha256(b'INKTAG-BLE-ADDR-V1\0'+namespace.to_bytes(2,'big')+bytes([len(uid)])+uid).digest()[:6])
    raw[0]=raw[0]&0x3f|0xc0
    return validate(bytes(raw),1),1

def encode(address: bytes, kind: int) -> bytes:
    payload=bytes([0x10|kind])+validate(address,kind)
    return payload+zlib.crc32(payload).to_bytes(4,'little')

def decode(record: bytes) -> tuple[bytes,int]:
    if len(record)!=11 or record[0] not in (0x10,0x11) or zlib.crc32(record[:7]).to_bytes(4,'little')!=record[7:]:
        raise ValueError('Invalid typed ordinary-address record')
    kind=record[0]&1
    return validate(record[1:7],kind),kind
