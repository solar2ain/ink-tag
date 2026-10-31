"""Explicit TLSR entry-profile pairing, offline only; not a hardware root."""
import struct
from inktag_programmer_formats.tlsr_layout import tlsr_layout_profile, tlsr_boot_binding, TLSR_BOOT_BINDING_OFFSET
from tlsr_full_flash_format import validate_telink_image


def check_pair(application: bytes, recovery: bytes, layout: str, stage0: bytes | None) -> dict:
    profile = tlsr_layout_profile(layout, application_start=0x1000 if stage0 is not None else 0)
    validate_telink_image(application, profile['limit'] - 96, 'application')
    validate_telink_image(recovery, 0x11000, 'Recovery')
    if profile['start']:
        if stage0 is None:
            raise ValueError('Trusted entry requires the paired fixed stage0; complete flashing is required')
        validate_telink_image(stage0, 0x1000, 'stage0')
        # Compiled read-only policy, not a mutable status version or cached ACK.
        proof = b'ITC1' + struct.pack('<IIII', 0x3f000, 1, profile['signature'], 0x53484c54)
        if recovery.count(proof) != 1:
            raise ValueError('Trusted-entry App requires exact compiled TLV1/epoch1 Recovery')
        for role,image in (('App',application),('Recovery',recovery)):
            at=TLSR_BOOT_BINDING_OFFSET
            if image[at:at+20] != tlsr_boot_binding(profile['start']):
                raise ValueError(role+' signed/native address binding differs')
        for role, image in (('App', application), ('Recovery', recovery), ('stage0', stage0)):
            prefix = int.from_bytes(image[12:14], 'little') * 16
            bound = len(image) if role == 'stage0' else int.from_bytes(image[24:28], 'little')
            if (image[14:16] != b'\x88\0' or not 32 <= prefix <= 0x8000 or
                    prefix > bound):
                raise ValueError(role + ' native SRAM-prefix bounds invalid')
    elif stage0 is not None:
        raise ValueError('TLV1 cannot carry a relocated stage0; do not reinterpret old signed layouts')
    return profile
