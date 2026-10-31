"""Bounded, pristine-Full NVDS codec. No SDK, ELF, BLE or device execution.

This is NOT a repair/migration/GC decoder. Read-side support deliberately
refuses ambiguous logs instead of treating an SDK error as address absence.
The ABI and OFF payload are bound to App/Recovery by the package signature.
"""
from __future__ import annotations

import struct
import zlib
from ble_factory_address import validate as valid_address, encode as typed_record, decode as decode_typed, derive

MAGIC = b'GDX_NVDS'
SDK = {
    'GR5331': 'c0d6655832fc8eef95dffb8ee90926ef4365f305c2dd3aed6137f05abed63575',
    'GR5513': 'f12c3f5229edc48db98e5fcb20d74da8f50a1d924f2d40350a6a31f615b4692d',
    'GR5515': 'f12c3f5229edc48db98e5fcb20d74da8f50a1d924f2d40350a6a31f615b4692d',
}
# Qualified by actual nvds_get -> ble_nvds_get -> ble_sdk_gap_init execution,
# including an independently captured untyped C001 log. Not a MAC-bit guess.
LEGACY_PUBLIC_SDK = {
    'GR5331': 'c0d6655832fc8eef95dffb8ee90926ef4365f305c2dd3aed6137f05abed63575',
}


def require(value, message):
    if not value:
        raise ValueError(message)


def extent(chip, family=None):
    require(chip in SDK, 'Unknown host NVDS MCU')
    if family == 'gr5513_zkel_internal':
        require(chip == 'GR5513', 'Internal NVDS requires GR5513')
        return 0x7e000, 0x2000
    require(family in (None, 'gr5331_zkc21v' if chip == 'GR5331' else 'gr551x_zkc42v'),
            'Unknown host NVDS layout family')
    return (0x7e000, 0x2000) if chip == 'GR5331' else (
        (0x7c000, 0x4000) if chip == 'GR5513' else (0xfc000, 0x4000))


def check_off(wire):
    require(9 < len(wire) <= 420 and wire[:2] == b'\x01\0' and
            wire[2] in (21, 25, 29, 33, 37, 41, 45, 49, 53, 57) and
            len(wire) == 9 + (wire[2]**2+7)//8 and
            struct.unpack_from('<H', wire, 3)[0] == len(wire)-9 and
            struct.unpack_from('<I', wire, 5)[0] == zlib.crc32(wire[9:]),
            'Invalid signed password-OFF/default QR payload')
    padding = (len(wire)-9)*8-wire[2]**2
    require(not padding or not wire[-1] & ((1 << padding)-1), 'Invalid QR padding')


def recipe(chip, off):
    check_off(off)
    return dict(codec='goodix-pristine-nvds-v1', sdkSha256=SDK[chip], offHex=off.hex())


def check_recipe(value, chip):
    require(type(value) is dict and set(value) == {'codec', 'sdkSha256', 'offHex'} and
            value['codec'] == 'goodix-pristine-nvds-v1' and value['sdkSha256'] == SDK[chip],
            'Full requires a signed compatible host-NVDS recipe; rebuild the Full package')
    require(type(value['offHex']) is str and len(value['offHex']) <= 840, 'Oversize OFF recipe')
    try:
        off = bytes.fromhex(value['offHex'])
    except ValueError as error:
        raise ValueError('Invalid signed OFF hex') from error
    require(off.hex() == value['offHex'], 'Noncanonical OFF hex')
    check_off(off)
    return off


def _item(tag, payload, maximum):
    # The official SDK header sums low tag/length bytes, then the payload;
    # two reserved bytes are zero. Sign-only/plaintext Full uses no padding.
    # GR551x ROM get_align_bytes adds padding only when m_encryption_enabled
    # is set; it is NOT an unconditional MCU-family property.
    require(0 < len(payload) <= maximum and 0 < tag <= 0xffff, 'Invalid pristine NVDS item')
    wire = struct.pack('<HHBBH', tag, len(payload), (tag+len(payload)) & 255,
                       sum(payload) & 255, 0) + payload
    return wire


def item(tag, payload, chip):
    return _item(tag, payload, 420)


def full_item(tag, payload, chip):
    # Both bound SDKs' nvds_put compare (length - 1) <= 0xFEB.
    # This Full-only bound is independent of the ordinary small-record codec.
    require(0 < len(payload) <= 4076, 'Full NVDS item exceeds native 4076-byte limit; no erase')
    return _item(tag, payload, 4076)


def full_data_limit(chip, family=None):
    # The final sector is the existing GC reservation, never seed storage.
    # GR5331's native writer also reserves 12 bytes in its single data sector.
    return extent(chip, family)[1] - 4096 - (12 if chip == 'GR5331' else 0)


def prepare(chip, address, kind, signed_recipe, *, extra_records=(), family=None):
    off = check_recipe(signed_recipe, chip)
    records = ((0xc001, address[::-1]), (0x5212, typed_record(address, kind)), (0x520d, off),*extra_records)
    require(len({tag for tag,payload in records})==len(records),'Duplicate prepared NVDS tag')
    prefix = MAGIC + b''.join(item(tag, payload, chip) for tag, payload in records)
    require(len(prefix) < 4096, 'Pristine NVDS exceeds first data sector')
    return prefix.ljust(extent(chip, family)[1], b'\xff')


def prepare_full(chip, address, kind, signed_recipe, *, extra_records=(), family=None):
    off = check_recipe(signed_recipe, chip)
    records = ((0xc001, address[::-1]), (0x5212, typed_record(address, kind)),
               (0x520d, off), *extra_records)
    require(len({tag for tag, payload in records}) == len(records), 'Duplicate prepared NVDS tag')
    prefix = MAGIC + b''.join(full_item(tag, payload, chip) for tag, payload in records)
    limit = full_data_limit(chip, family)
    require(len(prefix) <= limit,
            f'Full NVDS live records require {len(prefix)} bytes; native data capacity is {limit} bytes; no erase')
    return prefix.ljust(extent(chip, family)[1], b'\xff')


def _linear_records(data, alignment, limit=4096):
    cursor, found, deleted = 8, {}, False
    while cursor < limit:
        if data[cursor:limit] == b'\xff'*(limit-cursor):
            break
        require(cursor+8 <= limit, 'Truncated current NVDS header')
        tag, size, header_sum, value_sum, reserved = struct.unpack_from('<HHBBH', data, cursor)
        require(0 < size <= 4080 and cursor+8+size <= limit and reserved == 0,
                'Unsupported/damaged NVDS record; no erase')
        payload = data[cursor+8:cursor+8+size]
        require(sum(payload) & 255 == value_sum, 'Current NVDS checksum failure; no erase')
        if tag:
            require((tag+size) & 255 == header_sum, 'Current NVDS header checksum failure; no erase')
            require(tag not in found, 'Ambiguous live NVDS records; no erase')
            found[tag] = payload
        else:
            deleted = True
        end = cursor+8+size
        cursor = (end+alignment-1) & ~(alignment-1)
        require(data[end:cursor] == bytes(cursor-end), 'Invalid legacy NVDS padding; no erase')
    return found,deleted


def read_records(data, chip, family=None):
    """Read only a linear, unambiguous log with an erased GC reservation.

    No resync, skipped bad record, GC replay, type inference or damaged-record
    rollback. Deleted records are traversed but cannot establish an identity.
    Duplicate live address records or a nonempty GC page require SDK review.
    """
    require(len(data) == extent(chip, family)[1], 'Incomplete current identity reservation')
    if data == b'\xff'*len(data):
        return {},False
    require(data[:8] == MAGIC, 'Unknown current NVDS format; not absence')
    require(data[4096:] == b'\xff'*(len(data)-4096),
            'Current multi-sector/GC state needs qualified SDK extraction; no erase')
    if chip == 'GR5331':
        return _linear_records(data,1)
    # Admit complete plaintext SDK logs and the earlier host's zero-padded
    # logs, never a per-record resync or a partially valid prefix. Ambiguous
    # interpretations must agree on every protected value/deletion fact.
    candidates, errors = [], []
    for alignment in (1,4):
        try:
            candidates.append(_linear_records(data,alignment))
        except ValueError as error:
            errors.append(error)
    if not candidates:
        raise errors[0]
    require(all(value == candidates[0] for value in candidates),
            'Ambiguous current NVDS framing; no erase')
    return candidates[0]


def read_identity(data, chip, family=None, *, full_defaults=False):
    found,deleted=read_records(data,chip,family)
    return identity_from_records(found, chip, full_defaults=full_defaults, deleted=deleted)


def identity_from_records(found, chip, *, full_defaults=False, deleted=False):
    """Same identity policy for an already completely validated native log."""
    if 0x5212 in found:
        address, kind = decode_typed(found[0x5212])
        require(found.get(0xc001) == address[::-1], 'SDK/typed installed address mismatch; no erase')
        return address, kind
    if 0xc001 in found:
        # Explicit destructive Full may initialize the vendor public-address
        # path from the existing C001 bytes. This is a new native default,
        # not a claim to have recovered an undocumented old GAP type. The
        # SDK's public setter persists C001; its random setter is separate.
        # Normal readers/OTA never opt into this initialization policy.
        require(full_defaults or LEGACY_PUBLIC_SDK.get(chip) == SDK[chip],
                'Legacy six-byte address has no qualified SDK type; no erase')
        address = found[0xc001][::-1]
        if full_defaults and address in (bytes(6),b'\xff'*6,bytes.fromhex('eacb3ecf000f')):
            # Explicit Full reinitializes reserved/demo values through the
            # same immutable-factory/UID fallback as a fresh installation.
            return None
        valid_address(address, 0)
        require(address != bytes.fromhex('eacb3ecf000f'),
                'Legacy address is the SDK shared demo value; no erase')
        return address, 0
    require(full_defaults or not deleted, 'Deleted/unknown identity history cannot establish absence; no erase')
    return None


def select(installed, factory_address, factory_type, *, chip=None, uid=None, absent_qualified=False):
    if installed is not None:
        valid_address(*installed)
        return (*installed, 'installed-typed')
    require(factory_address is not None and factory_type is not None,
            'Factory address/type not independently qualified; no erase or UID fallback')
    if factory_address in (bytes(6), b'\xff'*6):
        require(absent_qualified, 'Factory absence representation is not qualified; no UID fallback')
        require(chip in SDK and type(uid) is bytes and len(uid) == 16 and
                uid not in (bytes(16), b'\xff'*16), 'Confirmed absence requires verified complete UID')
        namespace = 0x0102 if chip == 'GR5331' else 0x0101
        address,kind = derive(namespace,uid)
        return address,kind,'uid-hash-confirmed-absent'
    valid_address(factory_address, factory_type)
    return factory_address, factory_type, 'immutable-factory'
