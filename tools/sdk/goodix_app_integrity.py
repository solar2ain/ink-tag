"""Unsigned, standard-library-only IKCI codec for Goodix native contexts.

The board context is the existing SDK signing product_type (GR5331=2,
GR551x=0). Layout contexts are the already frozen native image-info markers.
Neither context is inferred from a user's product name or screen setting.
"""
import struct
import zlib

DESCRIPTOR_BYTES = 40
CONTEXTS = {
    'gr5331_zkc21v': (2, int.from_bytes(b'G3V1', 'little')),
    'gr551x_zkc42v': (0, int.from_bytes(b'G5V1', 'little')),
    'gr5513_zkel_internal': (0, int.from_bytes(b'GIV1', 'little')),
}


def append(body, *, board, epoch, release_version):
    board_context, layout_context = CONTEXTS[board]
    if not body or len(body) > 0xffffffff or epoch != 1 or not 0 < release_version <= 0xffffffff:
        raise ValueError('Invalid IKCI body, epoch or release version')
    record = struct.pack('<4sBBH7I', b'IKCI', 1, DESCRIPTOR_BYTES, 0,
        len(body), zlib.crc32(body), board_context, layout_context, epoch,
        release_version, 0)
    return body + record + struct.pack('<I', zlib.crc32(record))


def verify(native_body, *, board, epoch):
    if len(native_body) <= DESCRIPTOR_BYTES:
        raise ValueError('Missing IKCI or empty body')
    body, record = native_body[:-DESCRIPTOR_BYTES], native_body[-DESCRIPTOR_BYTES:]
    magic, schema, length, flags, size, crc, board_id, layout, actual_epoch, version, reserved, metadata_crc = struct.unpack('<4sBBH8I', record)
    if ((magic, schema, length, flags, size, board_id, layout, actual_epoch, reserved) !=
        (b'IKCI', 1, DESCRIPTOR_BYTES, 0, len(body), *CONTEXTS[board], epoch, 0)
        or epoch != 1 or version == 0 or zlib.crc32(record[:36]) != metadata_crc
        or zlib.crc32(body) != crc):
        raise ValueError('Invalid IKCI context, bounds or persisted CRC')
    return dict(bodyBytes=size, bodyCrc32=crc, descriptorBytes=DESCRIPTOR_BYTES,
        nativeBoardContext=board_id, nativeLayoutContext=layout,
        compatibilityEpoch=actual_epoch, nativeReleaseVersion=version)
