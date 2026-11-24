"""Strict unsigned InkTag native envelope; never a live OTA authorization.

Pro publisher inspection stays in its existing signature verifier. This module
has no signer, SDK, CLI, BLE, serial or source-tree fallback dependency.
"""
import hashlib
import io
import json
import stat
import struct
import zipfile

from tlsr_full_flash_format import validate_telink_image
from inktag_programmer_formats.tlsr_layout import tlsr_boot_binding, tlsr_layout_profile

SIGNATURE = {'required': False, 'algorithm': 'none', 'policy': 'crc-only'}


def _pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError('Duplicate unsigned InkTag JSON key')
        result[key] = value
    return result


def json_bytes(data):
    return json.loads(data.decode('utf-8'), object_pairs_hook=_pairs)


def validate_native(data, limit, role):
    try:
        declared, crc = validate_telink_image(data, limit, role)
    except SystemExit as exc:
        raise ValueError(str(exc)) from exc
    prefix = int.from_bytes(data[12:14], 'little')*16
    if data[14:16] != b'\x88\0' or not 32 <= prefix <= min(32768, declared):
        raise ValueError(role+' native SRAM-prefix bounds invalid')
    return declared, crc


def recovery_policy(data):
    declared, _ = validate_native(data, 0x11000, 'Recovery')
    body = data[:declared]
    at = body.find(b'IKRV')
    if (body.count(b'IKRV') != 1 or body.count(b'IKSP') != 1 or
            at+14 > declared or body[at+8:at+14] != b'IKSP\x01\x02' or
            body[at+4] != 1 or body[at+7] != 1):
        raise ValueError('InkTag Full requires one actual CRC-only Recovery policy record')
    return dict(version='.'.join(str(x) for x in body[at+4:at+7]),
                compatibilityEpoch=body[at+7], policy='crc-only')


def verify_ota_bytes(data):
    if len(data) > 1024*1024:
        raise ValueError('Unsigned InkTag OTA ZIP exceeds bound')
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            entries = archive.infolist()
            if len(entries) != 2 or {e.filename for e in entries} != {'manifest.json', 'application.bin'}:
                raise ValueError('Unsigned InkTag OTA requires exactly two unique members')
            for item in entries:
                if (item.flag_bits & 1 or item.is_dir() or stat.S_ISLNK(item.external_attr >> 16)
                        or item.file_size > (65536 if item.filename == 'manifest.json' else 0x3f000)):
                    raise ValueError('Invalid/encrypted/oversized unsigned InkTag OTA member')
            manifest = json_bytes(archive.read('manifest.json'))
            image = archive.read('application.bin')
    except (zipfile.BadZipFile, RuntimeError) as exc:
        raise ValueError('Invalid unsigned InkTag OTA ZIP') from exc
    if (type(manifest) is not dict or manifest.get('edition') != 'InkTag' or
            manifest.get('signature') != SIGNATURE or
            type(manifest['signature'].get('required')) is not bool or
            manifest.get('requiredBaseline') != 'inktag-release-v1' or
            manifest.get('format') != 'inktag-ota-v1' or
            manifest.get('transport') != 'telink-fixed-recovery-v2' or
            manifest.get('layout') != 'layout-v1' or
            manifest.get('board') != 'tlsr8359_hanshow' or manifest.get('chip') != 'TLSR8359' or
            type(manifest.get('compatibilityEpoch')) is not int or manifest['compatibilityEpoch'] != 1 or
            type(manifest.get('releaseVersion')) is not int or not 0 < manifest['releaseVersion'] <= 0xffffffff):
        raise ValueError('Unsigned InkTag OTA native context/edition mismatch')
    declared, crc = validate_native(image, 0x3f000, 'InkTag application')
    if image[0x140:0x154] != tlsr_boot_binding(0x1000):
        raise ValueError('Unsigned InkTag App physical address binding mismatch')
    app = manifest.get('application')
    expected = dict(size=len(image), transfer_size=len(image), declared_size=declared,
                    crc32=crc, active_address=0x1000, slot_size=0x3f000, region_size=0x40000,
                    recovery_address=0x40000, marker_offset=8)
    if (type(app) is not dict or any(type(app.get(k)) is not int or app[k] != v for k,v in expected.items())
            or app.get('file') != 'application.bin' or
            app.get('sha256') != hashlib.sha256(image).hexdigest()):
        raise ValueError('Unsigned InkTag OTA actual native size/hash/context mismatch')
    return manifest, image


def validate_full_pair(application, recovery, stage0):
    if stage0 is None:
        raise ValueError('InkTag Full requires its actual paired stage0')
    recovery_policy(recovery)
    from tlsr_trusted_entry_profile import check_pair
    return check_pair(application, recovery, 'layout-v1', stage0)


def write_zip(path, members):
    if path.exists():
        raise ValueError('Preserve existing package: '+str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_STORED) as archive:
        for name, data in members.items():
            info = zipfile.ZipInfo(name, date_time=(1980,1,1,0,0,0))
            info.create_system=3;info.external_attr=0o100644<<16
            archive.writestr(info,data)
