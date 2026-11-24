"""Offline native unsigned InkTag TLSR package checks; no device/signing API."""
import hashlib
import struct
from .tlsr_layout import tlsr_package_profile, unsigned_tlsr_image


def verify_base_payload(manifest: dict, image: bytes) -> dict:
    if (manifest.get('format') != 'inktag-ota-v1' or
            manifest.get('transport') != 'telink-fixed-recovery-v2' or
            manifest.get('board') != 'tlsr8359_hanshow' or
            manifest.get('edition') != 'InkTag' or
            manifest.get('signature') != dict(required=False, algorithm='none', policy='crc-only') or
            manifest.get('compatibilityEpoch') != 1 or
            type(manifest.get('releaseVersion')) is not int or
            not 0 < manifest['releaseVersion'] <= 0xffffffff or not unsigned_tlsr_image(image)):
        raise ValueError('Invalid TLSR unsigned InkTag format/context')
    profile=tlsr_package_profile(manifest.get('layout'),image)
    body=len(image)-4
    crc=struct.unpack_from('>I',image,body)[0]
    app=manifest.get('application',{})
    if (app.get('size') != len(image) or app.get('transfer_size') != len(image) or
            app.get('declared_size') != body or app.get('crc32') != crc or
            app.get('active_address') != profile['start'] or
            hashlib.sha256(image).hexdigest() != app.get('sha256')):
        raise ValueError('TLSR unsigned payload does not match its manifest')
    return dict(declaredSize=body,crc32=crc,signatureStatus='unsigned',publisherVerified=False)
