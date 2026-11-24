"""Recovery identity and explicit policy after native 20-byte GET_INFO prefix.

Historical replies remain valid; never gate OTA on a minor/patch version.
"""
from pathlib import Path
import re

MARKER='/* InkTag optional Recovery release identity, after native 20-byte info. */'


def recovery_binary_identity(raw: bytes):
    """Read this build's retained identity, never relabel an older binary."""
    records=[]
    for offset in range(max(0,len(raw)-7)):
        if raw[offset:offset+4] != b'IKRV':continue
        major,minor,patch,epoch=raw[offset+4:offset+8]
        if major and major==epoch:
            records.append(dict(version=f'{major}.{minor}.{patch}',epoch=epoch))
    unique={(r['version'],r['epoch']) for r in records}
    if len(unique)>1:raise ValueError('Ambiguous Recovery identity')
    return records[0] if records else None


def patch_recovery_version(sdk: Path):
    source=sdk/'components/libraries/dfu_port/dfu_port.c'
    text=source.read_text(encoding='utf-8')
    if MARKER in text:
        # Durable SDK workspaces may still carry the historically named header.
        # Canonicalize the exact owned declaration block as one unit: adding a
        # declaration independently of its include is not idempotent there.
        pattern = (r'#if defined\(BOOTLOADER_ENABLE\)[^\n]*\n'
                   r'#include "(?:InkTag|inktag)_recovery_version.h"\n'
                   r'.*?#endif\n\n(?=static void get_info_replace)')
        declarations = '#if defined(BOOTLOADER_ENABLE) && (defined(INKTAG_GOODIX_GR5515) || defined(INKTAG_GOODIX_GR5331))\n' + \
            '#include "inktag_recovery_version.h"\n#include "inktag_recovery_integrity_policy.h"\n' + \
            'static const uint8_t inktag_recovery_policy_info[6] __attribute__((used, retain)) = INKTAG_RECOVERY_INTEGRITY_INFO_BYTES;\n' + \
            'static const uint8_t inktag_recovery_version_info[8] __attribute__((used, retain)) = INKTAG_RECOVERY_INFO_BYTES;\n#endif\n\n'
        updated, count = re.subn(pattern, lambda _: declarations, text, count=1, flags=re.S)
        if count != 1:
            raise ValueError('Unknown patched GR Recovery declaration block')
        updated = updated.replace('#if defined(BOOTLOADER_ENABLE) && defined(INKTAG_GOODIX_GR5515)',
            '#if defined(BOOTLOADER_ENABLE) && (defined(INKTAG_GOODIX_GR5515) || defined(INKTAG_GOODIX_GR5331))')
        if '    dfu_send_frame(p_frame->data, 28u, p_frame->cmd_type);' in updated:
            updated = updated.replace('    dfu_send_frame(p_frame->data, 28u, p_frame->cmd_type);',
                '    memcpy(p_frame->data + 28u, inktag_recovery_policy_info, 6u);\n'
                '    dfu_send_frame(p_frame->data, 34u, p_frame->cmd_type);', 1)
        if updated.count('memcpy(p_frame->data + 28u, inktag_recovery_policy_info, 6u);') != 1 or \
            updated.count('dfu_send_frame(p_frame->data, 34u, p_frame->cmd_type);') != 1:
            raise ValueError('Unknown patched GR Recovery policy reply')
        if updated == text:
            return False
        source.write_text(updated, encoding='utf-8', newline='\n')
        return True
    start=text.index('static void get_info_replace(dfu_receive_frame_t *p_frame)')
    end=text.index('\nstatic void ',start+15)
    original=text[start:end]
    anchor='    dfu_send_frame(p_frame->data, 20, p_frame->cmd_type);'
    if original.count(anchor)!=1:raise ValueError('Unknown native GR GET_INFO reply')
    declarations='''#if defined(BOOTLOADER_ENABLE) && (defined(INKTAG_GOODIX_GR5515) || defined(INKTAG_GOODIX_GR5331))
#include "inktag_recovery_version.h"
#include "inktag_recovery_integrity_policy.h"
static const uint8_t inktag_recovery_policy_info[6] __attribute__((used, retain)) = INKTAG_RECOVERY_INTEGRITY_INFO_BYTES;
static const uint8_t inktag_recovery_version_info[8] __attribute__((used, retain)) = INKTAG_RECOVERY_INFO_BYTES;
#endif

'''
    replacement=MARKER+'''
#if defined(BOOTLOADER_ENABLE) && (defined(INKTAG_GOODIX_GR5515) || defined(INKTAG_GOODIX_GR5331))
    p_frame->data[18] = 0u;
    p_frame->data[19] = 0u;
    memcpy(p_frame->data + 20u, inktag_recovery_version_info, 8u);
    memcpy(p_frame->data + 28u, inktag_recovery_policy_info, 6u);
    dfu_send_frame(p_frame->data, 34u, p_frame->cmd_type);
#else
    dfu_send_frame(p_frame->data, 20, p_frame->cmd_type);
#endif'''
    text=text[:start]+declarations+original.replace(anchor,replacement,1)+text[end:]
    source.write_text(text,encoding='utf-8',newline='\n')
    return True
