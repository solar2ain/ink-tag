"""Materialize the sole new GR Recovery policy, without a signing dependency."""
import re


def boot_config(text):
    # These SDK definitions are no longer build controls or selectable code.
    text = re.sub(r'(?ms)^#if BOOTLOADER_SIGN_ENABLE\n.*?^#endif\n', '', text)
    text = re.sub(r'(?m)^#define BOOTLOADER_(?:SIGN_ENABLE|SIGN_VERIFY_AFTER_DFU|PUBLIC_KEY_HASH)\b[^\n]*\n', '', text)
    return text


def makefile(text):
    lines = text.splitlines(keepends=True)
    text = ''.join(line for line in lines
        if not any(part in line for part in ('/sign_verify/', '/Src/sign_verify/')))
    anchor = '../Src/user/inktag_gr_swap.c  \\\n'
    if text.count(anchor) != 1:
        raise ValueError('Unknown GR Recovery source list')
    return text.replace(anchor, anchor +
        '../Src/user/inktag_gr_app_integrity.c  \\\n'
        '../Src/user/inktag_gr_recovery_hash.c  \\\n'
        '../Src/user/inktag_sha256.c  \\\n', 1)


def patch_transport(sdk):
    source = sdk/'components/libraries/dfu_port/dfu_port.c'
    text = source.read_text(encoding='utf-8')
    old_check = 'if (firmware_type != SIGN_FIRMWARE)'
    crc_check = 'if (firmware_type != SIGN_FIRMWARE && firmware_type != NORMAL_FIRMWARE)'
    old_size = 's_file_size = bin_size + INKTAG_GR_IMAGE_INFO_LEN +\n                  INKTAG_GR_SIGNATURE_EXTENSION_LEN;'
    crc_size = ('s_file_size = bin_size + INKTAG_GR_IMAGE_INFO_LEN +\n'
        '                  (firmware_type == SIGN_FIRMWARE ? INKTAG_GR_SIGNATURE_EXTENSION_LEN : 0u);')
    for old, crc in ((old_check, crc_check), (old_size, crc_size),
        ('s_inktag_signed_firmware = true;', 's_inktag_signed_firmware = firmware_type == SIGN_FIRMWARE;')):
        role_bound = '#if defined(BOOTLOADER_ENABLE)\n    '+crc+'\n#else\n    '+old+'\n#endif'
        if role_bound in text:
            continue
        if crc in text:
            text = text.replace(crc, role_bound, 1)
        elif old in text:
            text = text.replace(old, role_bound, 1)
        else:
            raise ValueError('Unknown GR single-slot role/type admission')
    source.write_text(text, encoding='utf-8', newline='\n')


def materialize(repo, target):
    try:
        from inktag_source_profiles import source_path
    except ModuleNotFoundError:
        from tools.sdk.inktag_source_profiles import source_path
    selected = source_path(repo, 'firmware/common/security/inktag_code_guard.h', profile='base')
    (target/'Src/user/inktag_code_guard.h').write_bytes(selected.read_bytes())
    for relative in ('firmware/ports/goodix/common/src/inktag_gr_app_integrity.c',
                     'firmware/ports/goodix/common/src/inktag_gr_app_integrity.h',
                     'firmware/ports/goodix/common/src/inktag_gr_recovery_hash.c',
                     'firmware/common/security/inktag_sha256.c',
                     'firmware/common/security/inktag_sha256.h',
                     'firmware/common/ota/inktag_recovery_integrity_policy.h'):
        source = repo/relative
        destination = target/'Src/user'/source.name
        if not destination.exists() or destination.read_bytes() != source.read_bytes():
            destination.write_bytes(source.read_bytes())
