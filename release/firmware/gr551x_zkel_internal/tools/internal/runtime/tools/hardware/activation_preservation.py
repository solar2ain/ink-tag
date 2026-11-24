"""Public-key-only validation of an existing native IKAC1 authorization.

No issuer, private-key, signing, device I/O or user token import entry point.
The caller owns fresh same-unit acquisition and native storage integrity.
"""
import hashlib
import struct

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

# Same public trust root as the currently selected, unchanged native Apps.
# This remains the existing development/bench root; it is not a new release key.
PUBLIC_KEY = bytes.fromhex(
    '6cef365a284c497a548f93eee2397fe43673a2499c2494a52241122dbd179442'
    '78341110bb317e5ba5d7f49e71d71bcfb14731327643156e5a23ec9261d24b3b')
KEY_ID = 1
ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def derive_device_id(platform_id, product_id, raw_uid):
    if (type(raw_uid) is not bytes or not 1 <= len(raw_uid) <= 32 or
            type(platform_id) is not int or not 1 <= platform_id <= 65535 or
            type(product_id) is not int or not 1 <= product_id <= 65535):
        raise ValueError('invalid immutable activation identity')
    return hashlib.sha256(b'INKTAG-DEVICE-ID-V1\0' + struct.pack(
        '>HHH', platform_id, product_id, len(raw_uid)) + raw_uid).digest()[:8]


def verify_token(token, *, product_id, feature_mask, security_epoch,
                 device_id, public_key=PUBLIC_KEY, key_id=KEY_ID):
    """Match native cold-start predicates; None ID explicitly defers hardware bind.

    A caller must never substitute a mutable BLE address for an unavailable UID.
    Fixture public keys may be supplied by host tests, never public programmer
    configuration. The production caller uses the fixed App's public key.
    """
    if type(token) is not bytes or len(token) != 96:
        raise ValueError('activation token is incomplete')
    magic, version, actual_key, sequence, product, features, minimum, identity, _ = (
        struct.unpack('>2sBBIHIH8s8s', token[:32]))
    if (magic != b'IA' or version != 1 or actual_key != key_id or sequence == 0 or
            features == 0 or identity == bytes(8)):
        raise ValueError('activation token format/key/sequence is invalid')
    r, s = int.from_bytes(token[32:64], 'big'), int.from_bytes(token[64:], 'big')
    if not 0 < r < ORDER or not 0 < s <= ORDER // 2:
        raise ValueError('activation signature is not canonical low-S')
    try:
        key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), b'\x04' + public_key)
        key.verify(utils.encode_dss_signature(r, s), b'INKTAG-ACTIVATE-V1\0' + token[:32],
                   ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError) as error:
        raise ValueError('activation publisher signature failed') from error
    if product != product_id or features & ~feature_mask or minimum > security_epoch:
        raise ValueError('activation product/features/security epoch incompatible with App')
    if device_id is not None:
        if type(device_id) is not bytes or len(device_id) != 8 or identity != device_id:
            raise ValueError('activation belongs to a different immutable device identity')
    return dict(sequence=sequence, keyId=actual_key, featureMask=features,
                productId=product, minActivationEpoch=minimum,
                signatureVerified=True, hostHardwareBindingVerified=device_id is not None)
