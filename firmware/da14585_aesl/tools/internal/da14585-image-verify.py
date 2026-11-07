"""Offline DA native image authentication. Public key only; no signer or device API."""
import argparse
import hashlib
import json
import runpy
import re
from pathlib import Path
import struct
import zlib

_full_state = runpy.run_path(str(Path(__file__).with_name('da14585-full-state.py')))
verify_recovery_pages = _full_state['verify_recovery_pages']

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature

PUBLIC_KEY = bytes.fromhex('04' +
    '4b7e9a9e6d38e0caf507cb25dfbf859f3753b97e5b7cd21bd20d156f07a14672' +
    'd71b4e32a19307f91654a45d4ce165a4207bab50443aee65e6ea6cc02911bbfa')
PUBLIC_KEY_HASH = '20cc6bee49009c28a27c41910d8312497eef2ca9c48abf8569b6404b2178ee3f'
ORDER = int('ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551', 16)
BOARD = int.from_bytes(b'DAXB', 'little')
LAYOUT = int.from_bytes(b'DAV1', 'little')
VERIFIER = int.from_bytes(b'DAK1', 'little')


def native_payload(data, maximum, magic=b'pQ'):
    if len(data) < 64 or data[:2] != magic or data[2] != 0xAA:
        raise ValueError('Wrong native image header')
    size, crc = struct.unpack_from('<II', data, 4)
    if not 0 < size <= maximum or len(data) != 64 + size:
        raise ValueError('Native size/bound mismatch')
    payload = data[64:]
    if zlib.crc32(payload) != crc:
        raise ValueError('Native CRC mismatch (CRC is not authentication)')
    return payload


def verify_application(data, public_key=None):
    payload = native_payload(data, 0x17600)
    if len(payload) < 96:
        raise ValueError('Missing publisher trailer')
    raw, trailer = payload[:-96], payload[-96:]
    fields = struct.unpack('<IBBHIIIIII', trailer[:32])
    magic, version, algorithm, trailer_size, raw_size, epoch, board, layout, verifier, release = fields
    if (magic, version, algorithm, trailer_size) != (0x31534B49, 1, 1, 96):
        raise ValueError('Wrong publisher envelope/version')
    if raw_size != len(raw) or not raw or len(raw) > 0x17600-96:
        raise ValueError('Wrong signed payload bound')
    if (board, layout, verifier) != (BOARD, LAYOUT, VERIFIER) or epoch > 0xFFFF:
        raise ValueError('Foreign/old DA layout or epoch outside existing Recovery policy')
    if struct.unpack_from('<IIIIII', data, 40) != (0x31504B49, raw_size, epoch, board, layout, verifier):
        raise ValueError('Native preflight/trailer mismatch')
    key = PUBLIC_KEY if public_key is None else public_key
    if public_key is None and hashlib.sha256(key).hexdigest() != PUBLIC_KEY_HASH:
        raise ValueError('Compiled public trust root mismatch')
    r, s = int.from_bytes(trailer[32:64], 'big'), int.from_bytes(trailer[64:96], 'big')
    if not 0 < r < ORDER or not 0 < s <= ORDER//2:
        raise ValueError('Noncanonical publisher signature')
    point = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), key)
    point.verify(encode_dss_signature(r, s), raw + trailer[:32], ec.ECDSA(hashes.SHA256()))
    return {'appPublisherVerified': True, 'layout': 'DAV1', 'compatibilityEpoch': epoch,
            'release': release, 'rawBytes': len(raw), 'signedBytes': len(payload),
            'nativeBytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def native_vectors(raw, name, maximum):
    if not 8 <= len(raw) <= maximum:
        raise ValueError(name + ' native executable bound')
    sp, reset = struct.unpack_from('<II', raw)
    if not (0x07FC0000 < sp <= 0x07FD8000 and sp % 4 == 0 and
            0x07FC0001 <= reset < 0x07FC0000 + len(raw) and reset & 1):
        raise ValueError(name + ' native DA14585 SRAM vectors')


def verify_full_native_executables(data):
    """Version-independent DAV1 structure, NOT non-App authentication.

    The caller separately verifies the signed App and seeded defaults. Native
    Recovery CRC, policy constants and vectors establish format compatibility;
    they cannot prove who supplied its code. pP has no native CRC/signature.
    """
    if len(data) != 0x40000 or data[:5] != b'pP\0\0\0' or \
            data[0x38000:0x38002] != b'pR' or \
            struct.unpack_from('<II', data, 0x38004) != (0x4000, 0x1C000):
        raise ValueError('Unknown native DAV1 pP/pR container/layout')
    boot_size = int.from_bytes(data[5:8], 'big')
    if boot_size % 4:
        raise ValueError('Bootloader native alignment')
    native_vectors(data[8:8 + boot_size], 'Bootloader', 0xA00)
    if struct.pack('<I', 0x35008) not in data[8:8 + boot_size]:
        raise ValueError('Bootloader Recovery-selector layout literal missing')
    if data[8 + boot_size:0x4000] != b'\xff' * (0x4000 - 8 - boot_size):
        raise ValueError('Bootloader native region padding')
    for name, offset, limit, maximum, image_id in (
            ('App', 0x4000, 0x1C000, 0x17600, 1),
            ('Recovery', 0x1C000, 0x24000, 32704, 0)):
        size = struct.unpack_from('<I', data, offset + 4)[0]
        raw = native_payload(data[offset:offset + 64 + size], maximum)
        version_field = data[offset + 12:offset + 28]
        version = version_field.split(b'\0', 1)[0]
        if data[offset + 3] != image_id or data[offset + 32:offset + 34] != b'\0\0' or \
                b'\0' not in version_field or \
                not re.fullmatch(rb'[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+', version):
            raise ValueError(name + ' unknown native role/version/encryption format')
        native_vectors(raw[:-96] if name == 'App' else raw, name, maximum)
        end = offset + 64 + size
        if data[end:limit] != b'\xff' * (limit - end):
            raise ValueError(name + ' native region padding')
        if name == 'Recovery' and (struct.pack('<III', BOARD, LAYOUT, VERIFIER) not in raw or
                                   PUBLIC_KEY[1:] not in raw):
            raise ValueError('Recovery native family/layout/public-key policy constants missing')
    return {'format': 'DAV1', 'bootloaderBytes': boot_size,
            'bootloaderPublisherVerified': False, 'bootloaderNativeCrcAvailable': False,
            'recoveryNativeCrcVerified': True, 'recoveryPublisherVerified': False,
            'recoveryPolicyConstantsPresent': True, 'vectorsAndRegionBoundsVerified': True,
            'firmwareVersionPinned': False}


def verify_full(data, *, require_label=True):
    if len(data) != 0x40000 or data[:2] != b'pP' or data[0x38000:0x38002] != b'pR':
        raise ValueError('Not a DA fixed Full container')
    # pR is the vendor product directory, not a Recovery image header.
    # Both executable images use pQ; preserve the actual fixed Flash map.
    if struct.unpack_from('<II', data, 0x38004) != (0x4000, 0x1C000):
        raise ValueError('Wrong native product App/Recovery offsets')
    size = struct.unpack_from('<I', data, 0x4004)[0]
    if not 96 < size <= 0x17600:
        raise ValueError('Full App bounds')
    result = verify_application(data[0x4000:0x4040+size])
    recovery_size = struct.unpack_from('<I', data, 0x1C004)[0]
    if not 0 < recovery_size <= 32704:
        raise ValueError('Full Recovery bounds')
    native_payload(data[0x1C000:0x1C040+recovery_size], 32704)
    executables = verify_full_native_executables(data)
    initialized = verify_recovery_pages(data, result['compatibilityEpoch'])
    defaults = _full_state['verify_settings'](data)
    if require_label and not defaults['defaultLabelSeeded']:
        raise ValueError('Configured Full requires an OTP-derived default label; template is not a complete configuration')
    assets = _full_state['verify_assets_and_first_display'](data)
    if data[0x24000:0x35000] != b'\xff' * 0x11000:
        raise ValueError('Full image-store cleanup is incomplete')
    result.update({'fullBytes': len(data), 'fullSha256': hashlib.sha256(data).hexdigest(),
                   'recoveryNativeCrcVerified': True, 'recoveryPublisherVerified': False,
                   'nativeExecutables': executables,
                   'diagnosticCleanupProfile': _full_state['diagnostic_cleanup_profile'](data[0x4000:0x4040+size]),
                   'cleanupScope': _full_state['cleanup_scope'](),
                   'recoveryInitialization': initialized,
                   'defaultInitialization': defaults,
                   'assetInitialization': assets,
                   'imageStoreClearedBytes': 0x11000,
                   'configuredWriterQualified': False})
    return result


def verify_full_template(data):
    # Only the input package may lack the target-derived label. The physical
    # transaction captures OTP and regenerates defaults before any erase;
    # prepared bytes and the independent readback still use strict verify_full.
    return verify_full(data, require_label=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--kind', choices=['application', 'full', 'full-template'], default='application')
    args = parser.parse_args()
    try:
        data = args.image.read_bytes()
        result = {'full': verify_full, 'full-template': verify_full_template,
                  'application': verify_application}[args.kind](data)
        print(json.dumps(result))
    except Exception as error:
        print(json.dumps({'ok': False, 'error': type(error).__name__ + ': ' + str(error)}))
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
