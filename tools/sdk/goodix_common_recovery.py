"""Common GR Recovery CRC/format/extent admission for both App editions.

Derived from the maintained native CRC admission patch. Private protection
attributes and dataflow helpers are deliberately absent. App-only Pro
protection stays in its separate producer; Recovery has one common input closure."""
import re


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Unknown GR boot admission shape: '+old[:70])
    return text.replace(old,new,1)


def patch_boot_admission(text, *, jump_address='APP_FW_LOAD_ADDR', cold_boot=False):
    text=replace_once(text,'static bool                 s_is_app_fw_valid;',
        'static uint32_t             s_inktag_verified_app_size;')
    start=text.index('static bool bootloader_firmware_verify(')
    end=text.index('\nstatic void bootloader_fw_boot_info_get',start)
    firmware='''static bool inktag_gr_integrity_read(uint32_t address, void *data, uint32_t length)
{
    return hal_flash_read_judge_security(address, data, length) == length;
}

static bool bootloader_firmware_verify(uint32_t bin_addr, uint32_t bin_size,
    uint32_t check_sum_store, bool is_in_load_addr, uint32_t *verified_size)
{
    *verified_size = 0u;
    if (!is_in_load_addr || bin_addr != APP_FW_LOAD_ADDR ||
        bin_size == 0u || bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT)
        return false;
    bool valid = inktag_gr_app_integrity_verify(inktag_gr_integrity_read,
        bin_addr, bin_size, INKTAG_GR_APP_PAYLOAD_LIMIT, check_sum_store,
        INKTAG_GR_NATIVE_BOARD_CONTEXT, INKTAG_GR_LAYOUT_MARKER_MAGIC,
        INKTAG_GR_OTA_COMPATIBILITY_EPOCH);
    *verified_size = valid ? bin_size : 0u;
    return *verified_size != 0u;
}
'''.replace('APP_FW_LOAD_ADDR',jump_address)
    text=text[:start]+firmware+text[end:]
    sum_start = text.index('static bool check_image_crc(')
    sum_end = text.index('static uint32_t hal_flash_read_judge_security', sum_start)
    text = text[:sum_start] + text[sum_end:]
    text=text.replace('#include "sign_verify.h"', '#include "inktag_gr_app_integrity.h"')
    # Delete the vendor's now unreachable one-time signature-bypass source,
    # rather than leaving a selectable signature policy in the new Recovery.
    text=re.sub(r'#if BOOTLOADER_SIGN_VERIFY_AFTER_DFU\n.*?\n#endif(?: /\* BOOTLOADER_SIGN_VERIFY_AFTER_DFU \*/)?', '', text, flags=re.S)
    start=text.index('static bool bootloader_app_fw_verify()')
    end=text.index('\nbool inktag_gr_bootloader_app_valid',start)
    app=text[start:end]
    app=replace_once(app,'    uint32_t layout_marker[2] = {0u, 0u};',
        '    uint32_t layout_marker[2] = {0u, 0u};\n    s_inktag_verified_app_size = 0u;')
    for size, pattern in [
        ('sizeof(s_app_img_info)',r'(?:\(void\))?hal_flash_read_judge_security\(APP_INFO_START_ADDR,\s*\(uint8_t \*\)&s_app_img_info,\s*sizeof\(s_app_img_info\)\);'),
        ('sizeof(layout_marker)',r'(?:\(void\))?hal_flash_read_judge_security\(\s*APP_INFO_START_ADDR \+ INKTAG_GR_IMAGE_INFO_STRUCT_LEN,\s*\(uint8_t \*\)layout_marker, sizeof\(layout_marker\)\);')]:
        matches=list(re.finditer(pattern,app))
        if len(matches)!=1:raise ValueError('Unknown GR metadata read shape')
        old=matches[0].group();call=old.removeprefix('(void)')[:-1]
        app=app.replace(old,f'if ({size} != {call}) return false;',1)
    app=replace_once(app,'s_app_img_info.boot_info.check_sum, true);',
        's_app_img_info.boot_info.check_sum, true, &s_inktag_verified_app_size);')
    text=text[:start]+app+text[end:]
    text=replace_once(text,'s_is_app_fw_valid = bootloader_app_fw_verify();',
        '(void)bootloader_app_fw_verify();')
    text=replace_once(text,'if (s_is_app_fw_valid)','if (s_inktag_verified_app_size != 0u)')
    start=text.index('static void app_bootloader_jump(dfu_boot_info_t *p_boot_info)')
    end=text.index('\n/*',start)
    jump='''static void app_bootloader_jump(dfu_boot_info_t *p_boot_info)
{
    uint32_t verified_size = s_inktag_verified_app_size;
    s_inktag_verified_app_size = 0u;
    if (verified_size == 0u || verified_size != p_boot_info->bin_size ||
        p_boot_info->load_addr != APP_FW_LOAD_ADDR ||
        p_boot_info->run_addr != APP_FW_LOAD_ADDR)
    {
        bootloader_ota_task();
        return;
    }
#if APP_LOG_ENABLE
    app_log_flush();
#endif
    app_periph_deinit();
    /* Fixed XIP only; use verified extent, not a mutable alternative entry. */
    sys_firmware_jump(APP_FW_LOAD_ADDR, verified_size);
}
'''
    jump=jump.replace('APP_FW_LOAD_ADDR',jump_address)
    if cold_boot:
        # GR5331's qualified native jump has no periph-deinit API; preserve
        # that path and its mandatory cold flag rather than using GR551x's.
        jump=jump.replace('    app_periph_deinit();\n','')
        jump=jump.replace('    sys_firmware_jump(',
            '    pwr_mgmt_set_wakeup_flag(COLD_BOOT);\n    sys_firmware_jump(',1)
    return text[:start]+jump+text[end:]


def retain_identity(linker):
    anchor = '        *( .text*)'
    if linker.count(anchor) != 1:
        raise ValueError('Unknown GR ordinary linker shape')
    return linker.replace(anchor,
        '        KEEP(*(.rodata.inktag_recovery_policy_info*))\n'
        '        KEEP(*(.rodata.inktag_recovery_version_info*))\n'
        '        KEEP(*(.rodata.inktag_gr_clean_identity_info*))\n'+anchor, 1)
