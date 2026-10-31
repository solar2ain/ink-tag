"""Nordic physical Full authority adapter. Pure offline; never reads hardware.

The native journal reader is supplied by the existing planner. Cryptography
and immutable-ID derivation belong to the shared activation verifier only.
"""
from __future__ import annotations


def activation_authority(pair: bytes, ficr: bytes, decode) -> tuple[bytes | None, dict]:
    if len(pair) != 8192 or len(ficr) != 4096:
        raise ValueError('Incomplete physical activation authority capture')
    if pair == b'\xff' * 8192:
        return None, dict(state='free', authority='both-physical-pages-erased')
    decoded = decode(pair)
    generation, records = decoded[:2]
    warnings = list(decoded[2]) if len(decoded) > 2 else []
    def free(reason):
        return None, dict(state='free', authority=reason, generation=generation,
                          warnings=warnings)
    token = records.get(8)
    if token is None:
        if warnings:
            warnings.append('Old Pro not established; Full continues as Free with selected defaults.')
        return free('reset-to-free' if warnings else 'clean-current-journal')
    if len(token) != 96:
        warnings.append('Malformed old Pro record cleared; Full continues as Free.')
        return free('invalid-old-authorization-cleared')
    try:
        from activation_preservation import derive_device_id, verify_token
    except ImportError as error:
        raise ValueError('Missing shared activation preservation verifier; no erase permitted') from error
    device_id = derive_device_id(0x0301, 1, ficr[0x60:0x68])
    try:
        result = verify_token(token, product_id=1, feature_mask=1, security_epoch=0,
                              device_id=device_id, key_id=1)
    except ValueError:
        # A complete successful target read is not a transport failure. The
        # explicit Full clears invalid/foreign old entitlements rather than
        # importing them or blocking stock/older-layout reinitialization.
        warnings.append('Old Pro signature, format, policy or same-device binding invalid; cleared and continued as Free.')
        return free('invalid-old-authorization-cleared')
    if not isinstance(result, dict) or result.get('hostHardwareBindingVerified') is not True:
        raise ValueError('Shared verifier did not establish immutable same-device activation authority')
    # Never emit the signed token, challenge or full activation identity in logs.
    return token, dict(state='pro', authority='verified-current-journal', generation=generation,
                       sequence=int.from_bytes(token[4:8], 'big'), hostHardwareBindingVerified=True,
                       warnings=warnings)


def verifier_available() -> bool:
    try:
        from activation_preservation import derive_device_id, verify_token
        return callable(derive_device_id) and callable(verify_token)
    except ImportError:
        return False
