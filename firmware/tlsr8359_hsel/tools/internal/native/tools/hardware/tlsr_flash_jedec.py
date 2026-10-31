"""Bounded TLSR Full JEDEC consent; no device access or status-register recipe.

Only already-supported Flash parts and the lower 512-KiB Full window are
eligible. Consent never qualifies a PCB, identity, archive or upper extent.
"""
import re

FULL_WINDOW_BYTES = 524288
FLASH_BYTES = {'EB6013': 524288, '856013': 524288, 'EB6014': 1048576}


def checked_pair(observed: str, registered: str | None) -> tuple[str, str | None]:
    if not all(isinstance(value, str) and re.fullmatch('[0-9A-Fa-f]{6}', value)
               for value in (observed,) + (() if registered is None else (registered,))):
        raise ValueError('Invalid Flash JEDEC; no model or capacity guessed.')
    observed, registered = observed.upper(), registered.upper() if registered is not None else None
    if observed not in FLASH_BYTES or (registered is not None and registered not in FLASH_BYTES):
        raise ValueError('Unknown Flash JEDEC/status recipe; confirmation cannot authorize it.')
    if FLASH_BYTES[observed] < FULL_WINDOW_BYTES:
        raise ValueError('Flash is smaller than the required 512-KiB Full window.')
    return observed, registered


def consent_fields(observed: str, registered: str | None, *, confirmed: bool) -> dict:
    observed, registered = checked_pair(observed, registered)
    mismatch = observed != registered
    if mismatch and confirmed is not True:
        raise ValueError('Flash JEDEC mismatch was not confirmed; no write authorized.')
    fields = dict(registeredJedec=registered,
                jedecSamples=[observed] * 3,
                observedFlashBytes=FLASH_BYTES[observed],
                permittedFullWindowBytes=FULL_WINDOW_BYTES,
                jedecMismatchConfirmed=mismatch,
                jedecConfirmation='y' if mismatch else 'not-required')
    if registered is None:
        fields['registrationMissing'] = True
    return fields


def validate_uid_jedec(receipt: dict, registered: str | None) -> str:
    """Consume original observed ID, never relabel it as the config's ID.

    Older exact-match receipts remain readable. A mismatched ID requires the
    complete new consent fields and three consistent JEDEC observations.
    UID equality and identity/archive checks remain the caller's obligations.
    """
    observed, registered = checked_pair(receipt.get('jedec'), registered)
    if receipt.get('registeredJedec', registered) != registered:
        raise ValueError('Flash JEDEC consent belongs to a different Config registration.')
    if observed != registered:
        expected = consent_fields(observed, registered, confirmed=True)
        if (receipt.get('jedecMismatchConfirmed') is not True or
                any(receipt.get(key) != value for key, value in expected.items()) or
                type(receipt.get('permittedFullWindowBytes')) is not int or
                type(receipt.get('observedFlashBytes')) is not int):
            raise ValueError('Flash JEDEC mismatch lacks bound y/N consent or capacity evidence.')
    return observed
