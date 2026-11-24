"""Public bounded TLSR Full envelope checks; no SDK, headers or signer import."""
import struct
import sys
from pathlib import Path
import zlib

from inktag_programmer_formats.tlsr_layout import (
    TLSR_APP_LIMIT, TLSR_LAYOUT, TLSR_RECOVERY_START,
)

APPLICATION_SIZE=TLSR_APP_LIMIT
RECOVERY_ADDRESS=TLSR_RECOVERY_START
RECOVERY_SIZE=68*1024
FONT_PROTOCOL_STATUS='ikfa1-native-single-user; mask2-role1-flags3'
POLICY=dict(board='tlsr8359_hanshow',layout=TLSR_LAYOUT,compatibility_epoch=1)


def validate_telink_image(image: bytes, limit: int, role: str) -> tuple[int,int]:
    if len(image)<32 or len(image)>limit:
        raise SystemExit(f'{role} is {len(image)} bytes; limit is {limit}')
    if image[8:12]!=b'KNLT':raise SystemExit(f'{role} has no TLNK marker')
    declared=struct.unpack_from('<I',image,24)[0]
    if declared+4!=len(image):raise SystemExit(f'{role} declared/container length mismatch')
    crc=zlib.crc32(image[:declared])&0xffffffff
    if crc!=struct.unpack_from('>I',image,declared)[0]:raise SystemExit(f'{role} CRC32 mismatch')
    return declared,crc


def validate_producer_budget(application: bytes, recovery: bytes, *, development_validation=False) -> int:
    """Offline producer/Full policy, never a wire or manifest-derived authority."""
    remaining=APPLICATION_SIZE-len(application)-96
    if len(application)<32 or remaining<0:
        raise ValueError('Signed application exceeds the actual safe payload limit')
    if development_validation:
        from tlsr_full_flash_trust import accepted_pair
        accepted_pair(application,recovery,target=POLICY['board'],layout=POLICY['layout'],
            epoch=POLICY['compatibility_epoch'],baseline='inktag-release-v1',development_validation=True)
    return remaining
