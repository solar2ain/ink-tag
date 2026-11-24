"""DA native CRC/role/vector/range checks. No publisher key or crypto SDK."""
import re
import struct
import zlib

BOARD = 0x42584144
LAYOUT = 0x31564144
VERIFIER = 0x314B4144  # Retained IKS1 context, not a verifier implementation.
UNSIGNED_VERIFIER = 0x31554144
POLICY = bytes.fromhex('0d494b010153500102')


def native_payload(data, maximum):
    if len(data) < 64 or data[:3] != b'pQ\xaa':
        raise ValueError('Unknown native pQ header')
    size, expected = struct.unpack_from('<II', data, 4)
    if not 8 <= size <= maximum or len(data) != size + 64:
        raise ValueError('Native pQ size/bounds')
    raw = data[64:]
    if zlib.crc32(raw) != expected:
        raise ValueError('Native pQ CRC mismatch')
    return raw


def native_vectors(raw, name, maximum):
    if not 8 <= len(raw) <= maximum:
        raise ValueError(name + ' native size')
    stack, reset = struct.unpack_from('<II', raw)
    if not (0x07FC0000 < stack <= 0x07FD8000 and stack % 4 == 0 and
            0x07FC0001 <= reset < 0x07FC0000 + len(raw) and reset & 1):
        raise ValueError(name + ' native DA14585 SRAM vectors')


def verify_full_native_executables(data):
    if (len(data) != 0x40000 or data[:5] != b'pP\0\0\0' or
            data[0x38000:0x38002] != b'pR' or
            struct.unpack_from('<II', data, 0x38004) != (0x4000, 0x1C000)):
        raise ValueError('Unknown native DAV1 pP/pR container/layout')
    size = int.from_bytes(data[5:8], 'big')
    if size % 4:
        raise ValueError('Bootloader native alignment')
    native_vectors(data[8:8+size], 'Bootloader', 0xA00)
    if struct.pack('<I', 0x35008) not in data[8:8+size]:
        raise ValueError('Bootloader Recovery-selector layout literal missing')
    if data[8+size:0x4000] != b'\xff'*(0x4000-8-size):
        raise ValueError('Bootloader native region padding')
    for name, offset, limit, maximum, role in (
            ('App', 0x4000, 0x1C000, 0x17600, 1),
            ('Recovery', 0x1C000, 0x24000, 32704, 0)):
        length = struct.unpack_from('<I', data, offset+4)[0]
        raw = native_payload(data[offset:offset+64+length], maximum)
        field = data[offset+12:offset+28]
        if (data[offset+3] != role or data[offset+32:offset+34] != b'\0\0' or
                b'\0' not in field or not re.fullmatch(rb'[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+', field.split(b'\0', 1)[0])):
            raise ValueError(name + ' unknown native role/version/encryption format')
        native_vectors(raw[:-96] if name == 'App' else raw, name, maximum)
        if data[offset+64+length:limit] != b'\xff'*(limit-offset-64-length):
            raise ValueError(name + ' native region padding')
        if name == 'Recovery':
            # The pQ header retains four fields. Older DIS used the same string.
            header_version = field.split(b'\0', 1)[0]
            if all(version + b'\0' not in raw
                   for version in (header_version, header_version.removesuffix(b'.0'))):
                raise ValueError('CRC-only Recovery native/software version mismatch')
        if name == 'Recovery' and (raw.count(POLICY) != 1 or
                any(POLICY[:-1]+bytes([p]) in raw for p in (0, 1)) or
                any(struct.pack('<I', n) not in raw for n in (BOARD, LAYOUT, VERIFIER, UNSIGNED_VERIFIER))):
            raise ValueError('CRC-only Recovery policy/target constants mismatch')
    return dict(format='DAV1', bootloaderBytes=size, bootloaderPublisherVerified=False,
                bootloaderNativeCrcAvailable=False, recoveryNativeCrcVerified=True,
                recoveryPublisherVerified=False, recoveryPolicyCode=2,
                recoveryPolicyConstantsPresent=True, vectorsAndRegionBoundsVerified=True,
                firmwareVersionPinned=False)
