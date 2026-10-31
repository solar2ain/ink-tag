"""GR native durable authorization wrapper; shared host verifier owns policy.

No issuer, signing, BLE, UID algorithm or persistent-data repair lives here.
Only the same operation's native record and immutable capture may enter Full.
"""
from __future__ import annotations

import importlib
import struct
import zlib

TAG = 0x5206
RECORD_BYTES = 108
PLATFORM = {'GR5331': 0x0102, 'GR5513': 0x0101, 'GR5515': 0x0101}


class UnverifiableAuthorization(ValueError):
    """Readable old authorization is unusable; confirmed Full may initialize Free."""


def _require(ok, message):
    if not ok:
        raise ValueError(message)


def native_token(record):
    """Match the unchanged GR cold reader, not an active-byte shortcut."""
    if type(record) is not bytes or len(record) != RECORD_BYTES:
        raise UnverifiableAuthorization('Existing Pro record is incomplete')
    if not (struct.unpack_from('<I', record)[0] == 0x31434149 and
             record[4] == 1 and record[5] == 1 and
             struct.unpack_from('<I', record, 104)[0] == zlib.crc32(record[:104])):
        raise UnverifiableAuthorization('Existing Pro record format/CRC is invalid')
    return record[8:104]


def shared_verifier():
    try:
        shared = importlib.import_module('activation_preservation')
    except ImportError as error:
        raise ValueError('Missing shared activation_preservation verifier dependency; '
                         'original Pro/data retained, no erase') from error
    _require(callable(getattr(shared, 'verify_token', None)) and
             callable(getattr(shared, 'derive_device_id', None)),
             'Incomplete shared activation verifier dependency; no erase')
    return shared


def preserve(record, *, chip, raw_uid, signed_app):
    """Return exact native bytes only after shared immutable/token admission."""
    token = native_token(record)
    _require(chip in PLATFORM and type(raw_uid) is bytes and len(raw_uid) == 16 and
             raw_uid not in (bytes(16), b'\xff' * 16),
             'Pro retention needs this target\'s complete immutable UID; no erase')
    shared = shared_verifier()
    public = getattr(shared, 'PUBLIC_KEY', None)
    _require(type(public) is bytes and len(public) == 64 and
             type(signed_app) is bytes and len(signed_app) > 904 and
             signed_app[:-904].count(public) == 1,
             'Candidate App activation key is not bound to the host verifier; no erase')
    device_id = shared.derive_device_id(PLATFORM[chip], 1, raw_uid)
    try:
        verified = shared.verify_token(token, product_id=1, feature_mask=1,
                                       security_epoch=0, device_id=device_id, public_key=public)
    except ValueError as error:
        raise UnverifiableAuthorization('Existing Pro token failed shared verification: '+str(error)) from error
    if not (isinstance(verified, dict) and
             verified.get('signatureVerified') is True and
             verified.get('hostHardwareBindingVerified') is True):
        raise UnverifiableAuthorization('Same-device Pro verification did not establish immutable binding')
    return record
