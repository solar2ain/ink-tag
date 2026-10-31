"""Bounded TLSR native Settings authority; public verification is shared.

No issuer, private key, BLE operation or activation algorithm lives here.
Snapshot bytes are private per-operation evidence, never generic firmware.
"""
from __future__ import annotations

import argparse
import importlib
import json
from pathlib import Path
import struct

from tlsr_full_flash_state import PAGE, SETTINGS, fnv, sha

SNAPSHOT_BYTES = 2 * PAGE
ACTIVATION = 8


def verifier():
    try:
        shared = importlib.import_module('activation_preservation')
        if not callable(shared.verify_token) or not callable(shared.derive_device_id):
            raise AttributeError('Incomplete shared verifier')
        return shared
    except (ImportError, AttributeError) as error:
        raise ValueError('Missing shared activation_preservation dependency; install the complete tools delivery. No erase permitted.') from error


def _generation(raw: bytes):
    magic, generation, inverse, reserved = struct.unpack_from('<4sIII', raw)
    if magic == b'ISP1' and inverse == generation ^ 0xffffffff and reserved == 0xffffffff:
        return generation
    return None


def _page(raw: bytes):
    """Read only the authoritative page, matching the native cold reader."""
    if raw == b'\xff' * PAGE:
        return None
    generation = _generation(raw)
    if generation is None:
        raise ValueError('Unknown/partial Settings page; absence of Pro is not proven')
    offset = 16
    token = None
    occurrences = 0
    while offset < PAGE:
        tail = raw[offset:]
        if tail == b'\xff' * len(tail):
            break
        if len(tail) < 16:
            raise ValueError('Dirty Settings tail; no erase')
        marker, record_id, length, checksum = struct.unpack_from('<4sHHI', raw, offset)
        padded = (length + 3) & ~3
        end = offset + 12 + padded + 4
        if marker != b'ISR1' or not 1 <= record_id <= 32 or length > 420 or end > PAGE:
            raise ValueError('Invalid native Settings record; no erase')
        data = raw[offset + 12:offset + 12 + length]
        # Native scan_page checks the commit and payload checksum, not padding.
        if (raw[end-4:end] != b'ISC1' or
                fnv(struct.pack('<HH', record_id, length) + data) != checksum):
            raise ValueError('Settings checksum/commit failure')
        if record_id == ACTIVATION:
            if length not in (0, 96):
                raise ValueError('Invalid native Pro record length; no erase')
            token = data if length else None
            occurrences += 1
        offset = end
    return dict(generation=generation, token=token, occurrences=occurrences)


def native_record(snapshot: bytes):
    """Select committed logical authority, never scan/revive an old token."""
    if len(snapshot) != SNAPSHOT_BYTES:
        raise ValueError('Complete same-unit 8-KiB Settings capture required')
    pages = [snapshot[:PAGE], snapshot[PAGE:]]
    generations = [_generation(raw) for raw in pages]
    present = [index for index, value in enumerate(generations) if value is not None]
    if not present:
        if snapshot == b'\xff' * SNAPSHOT_BYTES:
            return None, dict(nativeState='erased', activePage=None, generation=None)
        raise ValueError('No committed native Settings page; old authorization is unknown')
    if len(present) == 2:
        delta = (generations[1] - generations[0]) & 0xffffffff
        if delta in (0, 0x80000000):
            raise ValueError('Ambiguous Settings generations; no erase')
        active = 1 if delta < 0x80000000 else 0
    else:
        active = present[0]
    # Cold readonly_result ignores an invalid/inactive peer. Never parse that
    # peer's records or revive its authorization when the current page fails.
    page = _page(pages[active])
    return page['token'], dict(nativeState='committed', activePage=active,
        generation=page['generation'], activationOccurrences=page['occurrences'])


def authority(snapshot: bytes, uid: bytes, source: str):
    """Preserve verified native Pro; rebuild other old user state as Free.

    Unknown metadata is explicitly reported, never claimed to be proven empty.
    Physical capture/UID/dependency failures still refuse before erase.
    """
    shared = verifier()  # Missing dependency is never a success stub, even Free.
    from tlsr_full_flash_address import check_uid
    check_uid(uid)
    if len(snapshot) != SNAPSHOT_BYTES:
        raise ValueError('Complete same-unit 8-KiB Settings capture required')
    reason = None
    try:
        token, native = native_record(snapshot)
    except ValueError:
        token = None
        native = dict(nativeState='unrecognized-or-damaged', activePage=None, generation=None)
        reason = 'Old native authorization authority is unrecognized, ambiguous or damaged'
    proof = dict(schema='inktag-tlsr-native-pro-preservation-v1',
        sourceLayout=source, settingsSha256=sha(snapshot), flashUidSha256=sha(uid),
        **native, state='absent', tokenSha256=None, sequence=None,
        initializedEdition='Free', authorizationNotRestoredReason=None,
        userImpact='No current native authorization; initialize Free',
        hostHardwareBindingVerified=False, applicationProBootObserved=False)
    if token is not None:
        device = shared.derive_device_id(0x0103, 1, uid)
        try:
            result = shared.verify_token(token, product_id=1, feature_mask=1,
                security_epoch=0, device_id=device)
        except ValueError as error:
            # Shared verifier emits fixed public reasons, never token bytes.
            reason = str(error)
            token = None
        else:
            if not isinstance(result, dict) or result.get('hostHardwareBindingVerified') is not True:
                raise ValueError('Shared verifier did not establish exact immutable target binding; no erase')
            proof.update(state='preserved', tokenSha256=sha(token),
                         sequence=int.from_bytes(token[4:8], 'big'),
                         initializedEdition='Pro',
                         userImpact='Preserve exact verified original same-device Pro authorization',
                         hostHardwareBindingVerified=True)
    if reason:
        proof.update(state='reinitialized-free', authorizationNotRestoredReason=reason,
            userImpact='Old authorization cannot be restored and will be cleared; initialize Free. Normal activation is required for Pro')
    return token, proof


def read_capture(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file() or path.stat().st_size != SNAPSHOT_BYTES:
        raise ValueError('Missing/unsafe/short same-operation Settings capture; no erase')
    return path.read_bytes()


def bound_capture(plan: dict):
    """Re-read private inputs at actual writer admission; returns no raw token in logs."""
    from tlsr_full_flash_v1 import source_family, read_exact
    evidence = plan.get('retentionCapture')
    if not isinstance(evidence, dict) or set(evidence) != {'settings', 'settingsSha256', 'factory', 'factorySha256', 'proof'}:
        raise ValueError('Complete-flash retention capture is missing; use matching updated tools')
    settings = read_capture(Path(evidence['settings']))
    factory = read_exact(Path(evidence['factory']), PAGE)
    if sha(settings) != evidence['settingsSha256'] or sha(factory) != evidence['factorySha256']:
        raise ValueError('Same-operation retention evidence changed before erase')
    source = source_family(factory, plan['board_id'])
    if source != plan['source_layout']:
        raise ValueError('Source identity/layout changed before erase')
    token, proof = authority(settings, bytes.fromhex(plan['uid']), source)
    if proof != evidence['proof']:
        raise ValueError('Native Pro authority differs from bound capture; no erase')
    return token, proof, factory, settings


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-dependencies', action='store_true', required=True)
    parser.parse_args()
    try:
        verifier()
        print(json.dumps(dict(sharedVerifierAvailable=True, deviceAccess=False)))
    except ValueError as error:
        parser.error(str(error))
