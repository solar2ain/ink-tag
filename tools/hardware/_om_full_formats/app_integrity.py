"""Offline IKCI codec for the OM native gap, with no CLI or signing path.

Wire source: protocol/app-integrity-descriptor-v1.json (2026-11-10).
"""
import struct
import zlib

SIZE = 40


def append(body, *, board, layout, epoch, release):
    if (type(body) is not bytes or len(body) < 0x10c or epoch != 1 or
            any(type(v) is not int or not 0 <= v <= 0xffffffff
                for v in (board, layout, epoch, release))):
        raise ValueError('IKCI requires an exact native body/context/epoch')
    prefix = struct.pack('<4sBBH7I', b'IKCI', 1, SIZE, 0, len(body),
                         zlib.crc32(body), board, layout, epoch, release, 0)
    result = body + prefix + struct.pack('<I', zlib.crc32(prefix))
    inspect(result, board=board, layout=layout, epoch=epoch, release=release)
    return result


def _at(data, offset, *, board, layout, epoch, release):
    if offset < 0x10c or offset > len(data) - SIZE:
        return None
    magic, schema, size, flags, body_bytes, body_crc, native_board, native_layout, native_epoch, native_release, reserved, crc = struct.unpack_from('<4sBBH8I', data, offset)
    if (magic != b'IKCI' or schema != 1 or size != SIZE or flags != 0 or
            reserved != 0 or body_bytes != offset or native_board != board or
            native_layout != layout or native_epoch != epoch or epoch != 1 or
            (release is not None and native_release != release) or zlib.crc32(data[offset:offset+36]) != crc or
            zlib.crc32(data[:offset]) != body_crc):
        return None
    return dict(bodyBytes=body_bytes, bodyCrc32=body_crc, descriptorBytes=SIZE,
                nativeBoardContext=board, nativeLayoutContext=layout,
                compatibilityEpoch=epoch, nativeReleaseVersion=release)


def inspect(data, *, board, layout, epoch, release):
    """Exact unsigned preimage: body+40B, no signature or transport tail."""
    if type(data) is not bytes or len(data) < 0x10c + SIZE:
        raise ValueError('IKCI image truncated')
    descriptor = _at(data, len(data)-SIZE, board=board, layout=layout,
                     epoch=epoch, release=release)
    if descriptor is None:
        raise ValueError('IKCI CRC, length or native context mismatch')
    offset = data.find(b'IKCI', 0x10c, len(data)-SIZE)
    while offset >= 0:
        if _at(data, offset, board=board, layout=layout, epoch=epoch,
               release=None) is not None:
            raise ValueError('Ambiguous IKCI descriptors')
        offset = data.find(b'IKCI', offset+1, len(data)-SIZE)
    return descriptor
