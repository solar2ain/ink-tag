"""Pure offline IKCI codec. CRC integrity only; no signing or device access.

The caller supplies exact native contexts and the linked body boundary. Never
search for magic bytes or infer a board from a product name.
"""
import struct
import zlib

DESCRIPTOR_BYTES = 40
_PREFIX = struct.Struct('<4sBBH7I')


def _uint32(value: int) -> int:
    if type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('IKCI context must be uint32')
    return value


def make_descriptor(body: bytes, *, board: int, layout: int, epoch: int,
                    release: int) -> bytes:
    if not isinstance(body, bytes) or not 0 < len(body) <= 0xffffffff:
        raise ValueError('IKCI body length is invalid')
    if epoch != 1:
        raise ValueError('Unsupported IKCI epoch')
    values = [_uint32(v) for v in (board, layout, epoch, release)]
    prefix = _PREFIX.pack(b'IKCI', 1, DESCRIPTOR_BYTES, 0, len(body),
                          zlib.crc32(body), *values, 0)
    return prefix + struct.pack('<I', zlib.crc32(prefix))


def verify_descriptor(body: bytes, descriptor: bytes, *, board: int,
                      layout: int, epoch: int, release: int) -> dict:
    if not isinstance(descriptor, bytes) or len(descriptor) != DESCRIPTOR_BYTES:
        raise ValueError('Invalid IKCI descriptor length')
    expected = make_descriptor(body, board=board, layout=layout,
                               epoch=epoch, release=release)
    if descriptor != expected:
        raise ValueError('IKCI body CRC, length or native context mismatch')
    return dict(bodyBytes=len(body), bodyCrc32=zlib.crc32(body),
                nativeBoardContext=board, nativeLayoutContext=layout,
                compatibilityEpoch=epoch, nativeReleaseVersion=release,
                integrityPolicy='crc-only', publisherVerified=False)


def inspect_payload(payload: bytes, *, body_bytes: int, board: int,
                    layout: int, epoch: int, release: int) -> dict:
    """Inspect exactly body + IKCI, after native wrapper/signature processing."""
    if (not isinstance(payload, bytes) or type(body_bytes) is not int
            or body_bytes <= 0 or len(payload) != body_bytes + DESCRIPTOR_BYTES):
        raise ValueError('IKCI payload does not match its native body boundary')
    return verify_descriptor(payload[:body_bytes], payload[body_bytes:],
                             board=board, layout=layout, epoch=epoch,
                             release=release)
