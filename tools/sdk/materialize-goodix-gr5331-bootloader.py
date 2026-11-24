#!/usr/bin/env python3
"""Materialize the size-bounded GR5331 InkTag BLE DFU bootloader."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
try:
    from inktag_source_profiles import source_path
except ModuleNotFoundError:
    from tools.sdk.inktag_source_profiles import source_path
try:
    import goodix_crc_recovery
    from goodix_common_recovery import patch_boot_admission, retain_identity
    from goodix_recovery_version import patch_recovery_version
    from goodix_diagnostic_literals import makefile as compact_diagnostic_makefile
except ModuleNotFoundError:
    from tools.sdk import goodix_crc_recovery
    from tools.sdk.goodix_common_recovery import patch_boot_admission, retain_identity
    from tools.sdk.goodix_recovery_version import patch_recovery_version
    from tools.sdk.goodix_diagnostic_literals import makefile as compact_diagnostic_makefile
try:
    from goodix_nvds_guard import patch_goodix_nvds_guard
    from goodix_pin_runtime import materialize_recovery_pins, patch_recovery_pin_leds
except ModuleNotFoundError:
    from tools.sdk.goodix_nvds_guard import patch_goodix_nvds_guard
    from tools.sdk.goodix_pin_runtime import materialize_recovery_pins, patch_recovery_pin_leds


def write_if_changed(path: Path, data: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == data:
        return False
    path.write_text(data, encoding="utf-8", newline="\n")
    return True


def replace_required(text: str, old: str, new: str, source: Path) -> str:
    if old not in text:
        raise SystemExit(f"cannot find {old!r} in {source}")
    return text.replace(old, new)


def refresh_config_dependent_outputs(target: Path, inputs: list[Path],
                                     target_name: str) -> bool:
    """Invalidate only objects whose command-line/header config changed."""
    digest = hashlib.sha256()
    for path in inputs:
        digest.update(path.name.encode("utf-8"))
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest() + "\n"
    out = target / "GCC/out"
    stamp = out / ".inktag-config-sha256"
    if stamp.exists() and stamp.read_text(encoding="ascii") == fingerprint:
        return False

    obj = out / "obj"
    if obj.exists():
        shutil.rmtree(obj)
    for path in [
        out / "gcc_linker.lds",
        out / f"{target_name}.bin",
        out / f"{target_name}.hex",
        out / "lst" / f"{target_name}.elf",
        out / "lst" / f"{target_name}.map",
        out / "lst" / f"{target_name}.s",
    ]:
        if path.exists():
            path.unlink()
    out.mkdir(parents=True, exist_ok=True)
    stamp.write_text(fingerprint, encoding="ascii", newline="\n")
    return True


def patch_no_stdio(text: str, source: Path) -> str:
    helper = """#if !APP_LOG_ENABLE
int printf(const char *format, ...)
{
    (void)format;
    return 0;
}

int puts(const char *text)
{
    (void)text;
    return 0;
}

void __assert_func(const char *file, int line, const char *function,
                   const char *expression)
{
    (void)file;
    (void)line;
    (void)function;
    (void)expression;
    hal_nvic_system_reset();
    while (1) { }
}
#endif

"""
    return replace_required(text, "void app_periph_init(void)\n{",
                            helper + "void app_periph_init(void)\n{", source)


def patch_ota_name(text: str, source: Path) -> str:
    text = '#include "inktag_gr_ble_address.h"\n' + text
    text = replace_required(text, "            bootloader_gap_params_init();",
        "            if (!inktag_gr_ble_address_init()) break;\n"
        "            bootloader_gap_params_init();", source)
    text = replace_required(text, '#define DEVICE_NAME                     "Bootloader_OTA"', '#define DEVICE_NAME_PREFIX              "IKT_GR"\n#define DEVICE_NAME_MAX_LEN             22', source)
    start = text.index("static const uint8_t s_adv_rsp_data_set[]")
    end = text.index("/*\n * LOCAL FUNCTION DEFINITIONS", start)
    text = text[:start] + """extern void inktag_recovery_led_cancel_download(void);
extern void inktag_recovery_led_role_ready(void);

static uint8_t s_adv_rsp_data_set[DEVICE_NAME_MAX_LEN + 2];
static char s_device_name[DEVICE_NAME_MAX_LEN];
static uint8_t s_device_name_len;

static char inktag_hex_digit(uint8_t value)
{
    value &= 0x0f;
    return (char)(value < 10 ? ('0' + value) : ('A' + value - 10));
}

static void inktag_build_ota_name(const ble_gap_bdaddr_t *bd_addr)
{
    static const char prefix[] = DEVICE_NAME_PREFIX "_";
    static const char suffix[] = "_OTA";
    uint8_t offset = (uint8_t)(sizeof(prefix) - 1);

    memcpy(s_device_name, prefix, offset);
    s_device_name[offset++] = inktag_hex_digit(bd_addr->gap_addr.addr[1] >> 4);
    s_device_name[offset++] = inktag_hex_digit(bd_addr->gap_addr.addr[1]);
    s_device_name[offset++] = inktag_hex_digit(bd_addr->gap_addr.addr[0] >> 4);
    s_device_name[offset++] = inktag_hex_digit(bd_addr->gap_addr.addr[0]);
    memcpy(&s_device_name[offset], suffix, sizeof(suffix));
    s_device_name_len = (uint8_t)(offset + sizeof(suffix) - 1);
}

""" + text[end:]
    text = replace_required(
        text,
        "static void bootloader_gap_params_init(void)\n{",
        "static void bootloader_gap_params_init(void)\n{\n"
        "    ble_gap_bdaddr_t bd_addr = {0};\n"
        "    ble_gap_addr_get(&bd_addr);\n"
        "    inktag_build_ota_name(&bd_addr);\n"
        "    s_adv_rsp_data_set[0] = s_device_name_len + 1;\n"
        "    s_adv_rsp_data_set[1] = BLE_GAP_AD_TYPE_COMPLETE_NAME;\n"
        "    memcpy(&s_adv_rsp_data_set[2], s_device_name, s_device_name_len);",
        source,
    )
    text = replace_required(text, "(const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME)", "(const uint8_t *)s_device_name, s_device_name_len", source)
    text = replace_required(text, "            bootloader_address_change();\n", "", source)
    text = replace_required(text, "            bootloader_address_restore();\n", "", source)
    text = replace_required(
        text,
        "static ble_gap_bdaddr_t         s_bd_addr;\n"
        "static bool                     s_bd_addr_changed_flag = false;\n\n",
        "",
        source,
    )
    address_start = text.index("static void bootloader_address_change(void)\n{")
    event_start = text.index("static void ble_evt_handler(const ble_evt_t *p_evt)\n{", address_start)
    text = text[:address_start] + text[event_start:]
    text = replace_required(
        text,
        "            bootloader_gap_params_init();\n"
        "            ble_gap_adv_start(0, &s_gap_adv_time_param);",
        "            bootloader_gap_params_init();\n"
        "            if (BLE_SUCCESS == ble_gap_adv_start(0, &s_gap_adv_time_param))\n"
        "                inktag_recovery_led_role_ready();",
        source,
    )
    return replace_required(
        text,
        "        case BLE_GAPC_EVT_DISCONNECTED:\n"
        "            APP_LOG_DEBUG(\">>> Disconnected and restart adv.\");",
        "        case BLE_GAPC_EVT_DISCONNECTED:\n"
        "            inktag_recovery_led_cancel_download();\n"
        "            APP_LOG_DEBUG(\">>> Disconnected and restart adv.\");",
        source,
    )


def patch_unique_address(text: str, source: Path) -> str:
    # Identity is read/applied in STACK_INIT, never written before stack init.
    return text


def patch_recovery_ota_led(text: str, source: Path) -> str:
    text = replace_required(
        text,
        '#include "board_SK.h"',
        '#include "board_SK.h"\n#include "gr533x_ll_msio.h"\n#include "inktag_board.h"',
        source,
    )
    led_state = r"""
#if !BOOTLOADER_WDT_ENABLE
#error "GR5331 Recovery OTA LED cadence requires the qualified watchdog SysTick"
#endif

#if !INKTAG_BOARD_HAS_LED_DIAG || \
    (((INKTAG_BOARD_LED_BLUE_PIN) >> 6) != 2) || \
    (((INKTAG_BOARD_LED_GREEN_PIN) >> 6) != 2) || \
    (((INKTAG_BOARD_LED_RED_PIN) >> 6) != 2)
#error "GR5331 Recovery expects the qualified active-low MSIO RGB LEDs"
#endif

#define INKTAG_RECOVERY_TICK_HZ                    25u
#define INKTAG_RECOVERY_LED_DOWNLOAD_HALF_TICKS     6u
#define INKTAG_RECOVERY_LED_INSTALL_HALF_TICKS      3u
#define INKTAG_RECOVERY_LED_TRANSIENT_TICKS         18u
#define INKTAG_RECOVERY_LED_DOWNLOAD_IDLE_TICKS    125u
#define INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS         9u
#define INKTAG_RECOVERY_LED_BOOT_SEPARATOR_TICKS     3u
#define INKTAG_RECOVERY_LED_BOOT_READY_HALF_TICKS    3u
#define INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS         48u
#define INKTAG_RECOVERY_LED_BLUE_MASK \
    (1u << ((INKTAG_BOARD_LED_BLUE_PIN) & 0x3fu))
#define INKTAG_RECOVERY_LED_GREEN_MASK \
    (1u << ((INKTAG_BOARD_LED_GREEN_PIN) & 0x3fu))
#define INKTAG_RECOVERY_LED_RED_MASK \
    (1u << ((INKTAG_BOARD_LED_RED_PIN) & 0x3fu))

typedef enum
{
    INKTAG_RECOVERY_LED_OFF = 0,
    INKTAG_RECOVERY_LED_BOOT,
    INKTAG_RECOVERY_LED_DOWNLOAD,
    INKTAG_RECOVERY_LED_INSTALL,
    INKTAG_RECOVERY_LED_ERROR,
} inktag_recovery_led_mode_t;

static volatile inktag_recovery_led_mode_t s_inktag_ota_led_mode;
static volatile uint8_t s_inktag_ota_led_phase_ticks;
static volatile uint16_t s_inktag_ota_led_download_idle_ticks;

static void inktag_recovery_led_pin_write(uint32_t mask, bool on)
{
#if INKTAG_BOARD_LED_ACTIVE_LOW
    if (on)
    {
        ll_msio_reset_output_pin(MSIOA, mask);
    }
    else
    {
        ll_msio_set_output_pin(MSIOA, mask);
    }
#else
    if (on)
    {
        ll_msio_set_output_pin(MSIOA, mask);
    }
    else
    {
        ll_msio_reset_output_pin(MSIOA, mask);
    }
#endif
}

static void inktag_recovery_led_output(bool red, bool green, bool blue)
{
    inktag_recovery_led_pin_write(INKTAG_RECOVERY_LED_RED_MASK, red);
    inktag_recovery_led_pin_write(INKTAG_RECOVERY_LED_GREEN_MASK, green);
    inktag_recovery_led_pin_write(INKTAG_RECOVERY_LED_BLUE_MASK, blue);
}

static void inktag_recovery_led_init(void)
{
    // Set the inactive latch before enabling output to avoid a power-on flash.
    inktag_recovery_led_output(false, false, false);
    ll_msio_set_pin_mux(MSIOA,
                        INKTAG_RECOVERY_LED_RED_MASK |
                            INKTAG_RECOVERY_LED_GREEN_MASK |
                            INKTAG_RECOVERY_LED_BLUE_MASK,
                        IO_MUX_GPIO);
    ll_msio_set_pin_mode(MSIOA,
                         INKTAG_RECOVERY_LED_RED_MASK |
                             INKTAG_RECOVERY_LED_GREEN_MASK |
                             INKTAG_RECOVERY_LED_BLUE_MASK,
                         LL_MSIO_MODE_DIGITAL);
    ll_msio_set_pin_pull(MSIOA,
                         INKTAG_RECOVERY_LED_RED_MASK |
                             INKTAG_RECOVERY_LED_GREEN_MASK |
                             INKTAG_RECOVERY_LED_BLUE_MASK,
                         LL_MSIO_PULL_NO);
    ll_msio_set_pin_direction(MSIOA,
                              INKTAG_RECOVERY_LED_RED_MASK |
                                  INKTAG_RECOVERY_LED_GREEN_MASK |
                                  INKTAG_RECOVERY_LED_BLUE_MASK,
                              LL_MSIO_DIRECTION_OUTPUT);
    s_inktag_ota_led_mode = INKTAG_RECOVERY_LED_OFF;
    s_inktag_ota_led_phase_ticks = 0u;
    inktag_recovery_led_output(false, false, false);
}

void inktag_recovery_led_role_ready(void)
{
    /* Do not let pre-stack initialization stretch only the red phase. */
    s_inktag_ota_led_mode = INKTAG_RECOVERY_LED_BOOT;
    s_inktag_ota_led_phase_ticks = 0u;
    inktag_recovery_led_output(true, false, false);
}

static void inktag_recovery_led_set(inktag_recovery_led_mode_t mode)
{
    s_inktag_ota_led_mode = mode;
    s_inktag_ota_led_phase_ticks = 0u;
    s_inktag_ota_led_download_idle_ticks = 0u;
    inktag_recovery_led_output(mode == INKTAG_RECOVERY_LED_ERROR, false,
                               mode == INKTAG_RECOVERY_LED_DOWNLOAD ||
                                   mode == INKTAG_RECOVERY_LED_INSTALL);
}

void inktag_recovery_led_cancel_download(void)
{
    /* Download feedback describes a live host transfer.  Do not leave
     * Recovery blinking indefinitely after cancellation or link loss.
     * INSTALL and ERROR own their bounded post-transfer lifecycles. */
    if (s_inktag_ota_led_mode == INKTAG_RECOVERY_LED_DOWNLOAD)
        inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);
}

"""
    text = replace_required(
        text,
        "#if BOOTLOADER_DFU_ENABLE\nstatic void dfu_program_start_callback(void);",
        led_state + "#if BOOTLOADER_DFU_ENABLE\nstatic void dfu_program_start_callback(void);",
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("    Start DFU OTA.");',
        '    /* Wait for the first successfully written data chunk before\n'
        '     * reporting host-to-Recovery download activity. */\n'
        '    s_inktag_ota_led_download_idle_ticks = 0u;\n'
        '    APP_LOG_DEBUG("    Start DFU OTA.");',
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("    DFU OTA.... %d%%", pro);',
        '    if (s_inktag_ota_led_mode != INKTAG_RECOVERY_LED_DOWNLOAD)\n'
        '        inktag_recovery_led_set(INKTAG_RECOVERY_LED_DOWNLOAD);\n'
        '    s_inktag_ota_led_download_idle_ticks = 0u;\n'
        '    APP_LOG_DEBUG("    DFU OTA.... %d%%", pro);',
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("    DFU OTA complete.");',
        '    inktag_recovery_led_set(status == 1u ?\n'
        '        INKTAG_RECOVERY_LED_INSTALL : INKTAG_RECOVERY_LED_ERROR);\n'
        '    APP_LOG_DEBUG("    DFU OTA complete.");',
        source,
    )
    text = replace_required(
        text,
        "    SysTick_Config(SystemCoreClock/10);",
        "    // A 40 ms tick represents exact 240 ms download, 120 ms install/error phases.\n"
        "    SysTick_Config(SystemCoreClock / INKTAG_RECOVERY_TICK_HZ);",
        source,
    )
    text = replace_required(
        text,
        "    bootloader_wdt_init();",
        "    bootloader_wdt_init();\n    inktag_recovery_led_init();",
        source,
    )
    text = replace_required(
        text,
        "    bootloader_wdt_refresh();",
        "    bootloader_wdt_refresh();\n"
        "    if (s_inktag_ota_led_mode != INKTAG_RECOVERY_LED_OFF)\n"
        "    {\n"
        "        ++s_inktag_ota_led_phase_ticks;\n"
        "        if (s_inktag_ota_led_mode == INKTAG_RECOVERY_LED_BOOT)\n"
        "        {\n"
        "            uint8_t phase = s_inktag_ota_led_phase_ticks;\n"
        "            bool red = phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS;\n"
        "            bool green = phase >= INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS &&\n"
        "                         phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 2u;\n"
        "            bool blue = phase >= INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 2u &&\n"
        "                        phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u;\n"
        "            uint8_t ready_start = INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u +\n"
        "                                  INKTAG_RECOVERY_LED_BOOT_SEPARATOR_TICKS;\n"
        "            if (phase >= ready_start &&\n"
        "                phase < INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS)\n"
        "                blue = (((phase - ready_start) /\n"
        "                         INKTAG_RECOVERY_LED_BOOT_READY_HALF_TICKS) & 1u) == 0u;\n"
        "            if (phase >= INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS)\n"
        "                inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);\n"
        "            else\n"
        "                inktag_recovery_led_output(red, green, blue);\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "        if (s_inktag_ota_led_mode == INKTAG_RECOVERY_LED_DOWNLOAD &&\n"
        "            ++s_inktag_ota_led_download_idle_ticks >=\n"
        "                INKTAG_RECOVERY_LED_DOWNLOAD_IDLE_TICKS)\n"
        "        {\n"
        "            inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "        uint8_t half_ticks = s_inktag_ota_led_mode ==\n"
        "                                 INKTAG_RECOVERY_LED_DOWNLOAD\n"
        "                                 ? INKTAG_RECOVERY_LED_DOWNLOAD_HALF_TICKS\n"
        "                                 : INKTAG_RECOVERY_LED_INSTALL_HALF_TICKS;\n"
        "        if (s_inktag_ota_led_mode == INKTAG_RECOVERY_LED_ERROR &&\n"
        "            s_inktag_ota_led_phase_ticks >= INKTAG_RECOVERY_LED_TRANSIENT_TICKS)\n"
        "        {\n"
        "            s_inktag_ota_led_mode = INKTAG_RECOVERY_LED_OFF;\n"
        "            inktag_recovery_led_output(false, false, false);\n"
        "        }\n"
        "        else\n"
        "        {\n"
        "            bool on = ((s_inktag_ota_led_phase_ticks / half_ticks) & 1u) == 0u;\n"
        "            inktag_recovery_led_output(\n"
        "                s_inktag_ota_led_mode == INKTAG_RECOVERY_LED_ERROR && on,\n"
        "                false,\n"
        "                s_inktag_ota_led_mode != INKTAG_RECOVERY_LED_ERROR && on);\n"
        "        }\n"
        "        }\n"
        "        }\n"
        "    }",
        source,
    )
    return text


def enforce_permanent_recovery_debug_attach(text: str, source: Path) -> str:
    active_mode = "    pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);"
    marker = "    /* Fixed Recovery never inherits an application debug-grace window. */"
    if active_mode not in text:
        raise SystemExit(
            f"fixed GR5331 Recovery must retain PMR_MGMT_ACTIVE_MODE: {source}"
        )
    for forbidden in ("PMR_MGMT_IDLE_MODE", "PMR_MGMT_SLEEP_MODE"):
        if forbidden in text:
            raise SystemExit(
                f"fixed GR5331 Recovery must remain directly attachable; "
                f"unexpected {forbidden} in {source}"
            )
    if marker not in text:
        text = text.replace(active_mode, marker + "\n" + active_mode, 1)
    return text


def patch_swap_boot_vendor(text: str, source: Path) -> str:
    text = replace_required(
        text, '#include "hal_flash.h"',
        '#include "hal_flash.h"\n#include "gr533x_pwr.h"\n#include "inktag_gr_swap.h"\n#include "inktag_gr_flash_layout.h"\n#include "inktag_gr_ota_policy.h"', source,
    )
    text = replace_required(
        text,
        "    sys_firmware_jump(p_boot_info->run_addr, p_boot_info->bin_size);",
        "    // A global soft reset always reasserts the GR5331 warm-boot flag.\n"
        "    // Recovery instead sets cold immediately before its direct jump,\n"
        "    // so the application rebuilds data, BSS and BLE callback state.\n"
        "    pwr_mgmt_set_wakeup_flag(COLD_BOOT);\n"
        "    sys_firmware_jump(p_boot_info->run_addr, p_boot_info->bin_size);",
        source,
    )
    legacy_verify_start = text.find("static bool bootloader_app_dfu_fw_verify(void)")
    dfu_info_verify_start = text.find(
        "static bool bootloader_dfu_info_verify(void)", legacy_verify_start
    )
    if legacy_verify_start < 0 or dfu_info_verify_start < 0:
        raise SystemExit(f"cannot remove GR5331 copy-image verification in {source}")
    text = text[:legacy_verify_start] + text[dfu_info_verify_start:]

    verify_start = text.find("static bool bootloader_app_fw_verify()")
    verify_end = text.find("\nstatic void app_bootloader_jump", verify_start)
    if verify_start < 0 or verify_end < 0:
        raise SystemExit(f"cannot isolate GR5331 APP_INFO verification in {source}")
    verify = r'''static bool bootloader_app_fw_verify()
{
    uint32_t layout_marker[2] = {0u, 0u};
    memset((uint8_t *)&s_app_img_info, 0, sizeof(s_app_img_info));
    hal_flash_read_judge_security(APP_INFO_START_ADDR,
                                  (uint8_t *)&s_app_img_info,
                                  sizeof(s_app_img_info));
    hal_flash_read_judge_security(
        APP_INFO_START_ADDR + INKTAG_GR_IMAGE_INFO_STRUCT_LEN,
        (uint8_t *)layout_marker, sizeof(layout_marker));
    if (s_app_img_info.pattern != DFU_IMG_INFO_PATTERN ||
        layout_marker[0] != INKTAG_GR_LAYOUT_MARKER_MAGIC ||
        layout_marker[1] != INKTAG_GR_LAYOUT_MARKER_INVERSE ||
        s_app_img_info.boot_info.load_addr != DFU_FW_SAVE_ADDR ||
        s_app_img_info.boot_info.run_addr != DFU_FW_SAVE_ADDR ||
        s_app_img_info.version != INKTAG_GR_OTA_COMPATIBILITY_EPOCH ||
        s_app_img_info.boot_info.bin_size == 0u ||
        s_app_img_info.boot_info.bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT)
    {
        return false;
    }
    return bootloader_firmware_verify(
        s_app_img_info.boot_info.load_addr,
        s_app_img_info.boot_info.bin_size,
        s_app_img_info.boot_info.check_sum, true);
}
'''
    app_valid_export = r'''

bool inktag_gr_bootloader_app_valid(void)
{
    return bootloader_app_fw_verify();
}
'''
    text = (text[:verify_start] + verify.rstrip() + app_valid_export.rstrip() +
            text[verify_end:])

    legacy_mutation_start = text.find("static void bootloader_dfu_info_clear(void)")
    verify_start = text.find("static bool bootloader_app_fw_verify()")
    if legacy_mutation_start < 0 or verify_start < 0:
        raise SystemExit(f"cannot remove GR5331 copy/self-repair writes in {source}")
    text = text[:legacy_mutation_start] + text[verify_start:]

    task_start = text.find("void bootloader_dfu_task(void)")
    copy_start = text.find(
        "    if (s_dfu_info.dfu_mode_pattern == DFU_COPY_UPGRADE_MODE_PATTERN)",
        task_start,
    )
    copy_end = text.find(
        "\n\n    if (s_dfu_info.dfu_mode_pattern == DFU_NON_COPY_UPGRADE_MODE_PATTERN)",
        copy_start,
    )
    if task_start < 0 or copy_start < 0 or copy_end < 0:
        raise SystemExit(f"cannot isolate GR5331 legacy copy-mode path in {source}")
    copy_reject = r'''    if (s_dfu_info.dfu_mode_pattern == DFU_COPY_UPGRADE_MODE_PATTERN)
    {
        /* The product publishes only fixed Recovery plus one App region.
         * Leave legacy metadata intact for diagnosis and stay in Recovery;
         * a successful signed single-slot OTA replaces it transactionally. */
        s_dfu_info.dfu_mode_pattern = DFU_NON_COPY_UPGRADE_MODE_PATTERN;
        bootloader_ota_task();
        return;
    }'''
    text = text[:copy_start] + copy_reject + text[copy_end:]
    return replace_required(
        text,
        '    APP_LOG_DEBUG(\">>> Verify DFU info\");',
        '    if (inktag_gr_recovery_required())\n'
        '    {\n'
        '        s_dfu_info.dfu_mode_pattern = DFU_NON_COPY_UPGRADE_MODE_PATTERN;\n'
        '        bootloader_ota_task();\n'
        '        return;\n'
        '    }\n\n'
        '    APP_LOG_DEBUG(\">>> Verify DFU info\");',
        source,
    )


def patch_swap_main(text: str, source: Path) -> str:
    text = replace_required(
        text, '#include "dfu_port.h"',
        '#include "dfu_port.h"\n#include "inktag_gr_swap.h"', source,
    )
    text = replace_required(
        text,
        "int main (void)\n{",
        "extern void __main(void);\n\n"
        "// GR5331 global soft reset retains RAM and always reports warm boot.\n"
        "// Fixed Recovery crosses image boundaries, so it must never resume\n"
        "// the previous application's retained BLE callback table.\n"
        "void main_init(void)\n"
        "{\n"
        "    __main();\n"
        "}\n\n"
        "int main (void)\n{",
        source,
    )
    return replace_required(
        text,
        "    app_periph_init();\n\n    bootloader_dfu_task();",
        "    app_periph_init();\n\n"
        "    inktag_swap_result_t swap_result = inktag_gr_swap_boot();\n"
        "    if (swap_result == INKTAG_SWAP_RESET_REQUIRED)\n"
        "    {\n"
        "        hal_nvic_system_reset();\n"
        "    }\n\n"
        "    bootloader_dfu_task();",
        source,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True)
    ap.add_argument("--repo", default=Path(__file__).resolve().parents[2])
    ap.add_argument("--sdk-root", type=Path, required=True,
                    help="absolute owned SDK workspace under the durable primary SDK root")
    ap.add_argument("--source-profile", choices=("common",), default="common")
    args = ap.parse_args()
    if not args.sdk_root.is_absolute():
        ap.error("--sdk-root must be an absolute owned SDK workspace")
    sdk = args.sdk_root.resolve()
    if not sdk.is_dir():
        ap.error(f"--sdk-root directory is missing: {sdk}")

    repo = Path(args.repo).resolve()
    subprocess.run([sys.executable,str(repo/'tools/version/sync-version.py'),'--repo',str(repo)],check=True)
    patch_recovery_version(sdk)
    registry = json.loads((repo / "firmware/boards/boards.json").read_text(encoding="utf-8"))
    board = registry.get("boards", {}).get(args.board)
    if board is None or board.get("port") != "goodix/gr5331":
        raise SystemExit(f"board {args.board!r} is not a GR5331 board")
    artifact = board["artifact"]
    patch_goodix_nvds_guard(sdk)
    template = sdk / "projects/ble/dfu/app_bootloader"
    target = sdk / "projects/ble/dfu/app_bootloader_inktag"
    if not template.exists():
        raise SystemExit(f"missing Goodix app_bootloader project: {template}")
    if not target.exists():
        shutil.copytree(template, target)

    custom_src = template / "Src/config/custom_config.h"
    custom = custom_src.read_text(encoding="utf-8")
    # Keep the SDK patch-table selector aligned with the hardware-qualified
    # GR5331 application. Runtime package detection still reports GR5331CENI;
    # this project enum controls a separate set of vendor ROM BLE patches.
    # The bootloader exposes one OTAS GATT server over one peripheral link.
    # Generic sample defaults (central/scanner, five links, pairing, CoC and a
    # GATT client) pull tens of KiB of GR533x BLE patch code that DFU cannot use.
    replacements = {
        "#define APP_LOG_ENABLE                   1": "#define APP_LOG_ENABLE                   0",
        # Recovery exposes one OTAS service over one peripheral connection.
        # Keep one spare profile entry, matching the hardware-qualified app.
        "#define CFG_MAX_PRFS                     10": "#define CFG_MAX_PRFS                     2",
        "#define CFG_MAX_BOND_DEVS                4": "#define CFG_MAX_BOND_DEVS                1",
        "#define CFG_MAX_CONNECTIONS              5": "#define CFG_MAX_CONNECTIONS              1",
        "#define CFG_MAX_SCAN                     1": "#define CFG_MAX_SCAN                     0",
        "#define CFG_MASTER_SUPPORT               1": "#define CFG_MASTER_SUPPORT               0",
        "#define CFG_LEGACY_PAIR_SUPPORT          1": "#define CFG_LEGACY_PAIR_SUPPORT          0",
        "#define CFG_SC_PAIR_SUPPORT              1": "#define CFG_SC_PAIR_SUPPORT              0",
        "#define CFG_COC_SUPPORT                  1": "#define CFG_COC_SUPPORT                  0",
        "#define CFG_GATTC_SUPPORT                1": "#define CFG_GATTC_SUPPORT                0",
    }
    for old, new in replacements.items():
        custom = replace_required(custom, old, new, custom_src)

    boot_cfg_src = template / "Src/config/bootloader_config.h"
    boot_cfg = boot_cfg_src.read_text(encoding="utf-8")
    boot_cfg = goodix_crc_recovery.boot_config(boot_cfg)
    boot_cfg = replace_required(boot_cfg, "#define DFU_FW_SAVE_ADDR                        (FLASH_START_ADDR + 0x40000)", "#define DFU_FW_SAVE_ADDR                        (FLASH_START_ADDR + 0x20000)", boot_cfg_src)
    # The factory image and OTA package use this board-locked image marker.
    # If it differs from APP_FW_COMMENTS, the bootloader rejects a valid Slot
    # A and enters recovery OTA on every power-up.
    boot_cfg = replace_required(
        boot_cfg,
        '#define APP_FW_COMMENTS                         "ble_app_temp"',
        '#define APP_FW_COMMENTS                         "inktag5331"',
        boot_cfg_src,
    )

    ota_src = template / "Src/user/bootloader_ota.c"
    ota = patch_ota_name(ota_src.read_text(encoding="utf-8"), ota_src)
    boot_vendor_src = template / "Src/user/bootloader_boot_vendor.c"
    boot_vendor = patch_swap_boot_vendor(boot_vendor_src.read_text(encoding="utf-8"), boot_vendor_src)
    boot_vendor = patch_boot_admission(boot_vendor,jump_address='DFU_FW_SAVE_ADDR',cold_boot=True)
    main_src = template / "Src/user/main.c"
    main_source = patch_swap_main(main_src.read_text(encoding="utf-8"), main_src)
    periph_src = template / "Src/platform/user_periph_setup.c"
    periph = patch_unique_address(periph_src.read_text(encoding="utf-8"), periph_src)
    periph = patch_recovery_ota_led(periph, periph_src)
    periph = patch_recovery_pin_leds(periph, gr551x=False)
    periph = patch_no_stdio(periph, periph_src)
    periph = enforce_permanent_recovery_debug_attach(periph, periph_src)

    linker_src = sdk / "platform/soc/linker/gcc/gcc_linker.lds"
    linker = linker_src.read_text(encoding="utf-8")
    linker = replace_required(
        linker,
        '#include "flash_linker_config.h"',
        '#include "../../../../../platform/soc/linker/gcc/flash_linker_config.h"',
        linker_src,
    )
    linker = replace_required(
        linker,
        "FLASH (rx) : ORIGIN = (APP_CODE_RUN_ADDR), LENGTH = (APP_FLASH_END_ADDR - APP_CODE_RUN_ADDR)",
        # 0x00204000-0x0021FFFF: 112 KiB after the 16 KiB SCA/info area.
        "FLASH (rx) : ORIGIN = (APP_CODE_RUN_ADDR), LENGTH = 0x0001C000",
        linker_src,
    )

    make_src = template / "GCC/Makefile"
    makefile = make_src.read_text(encoding="utf-8")
    makefile = replace_required(
        makefile,
        "MAKE_TARGET_NAME :=  app_bootloader",
        f"MAKE_TARGET_NAME :=  {artifact}-boot",
        make_src,
    )
    makefile = replace_required(makefile, "CFLAGS += -O2", "CFLAGS += -Os", make_src)
    makefile = replace_required(
        makefile,
        "../Src/user/bootloader_boot_vendor.c  \\",
        "../Src/user/bootloader_boot_vendor.c  \\\n"
        "../Src/user/inktag_power.c  \\\n"
        "../Src/user/inktag_gr_swap.c  \\\n"
        "../Src/user/inktag_gr_recovery_power.c  \\\n"
        "../Src/user/inktag_recovery.c  \\",
        make_src,
    )
    makefile = makefile.replace(
        "../Src/user/inktag_gr_recovery_power.c",
        "../Src/user/inktag_gr_ble_address.c  " + chr(92) + "\n"
        "../Src/user/inktag_ble_address.c  " + chr(92) + "\n"
        "../Src/user/inktag_ble_address_record.c  " + chr(92) + "\n"
        "../Src/user/inktag_ble_address_uid_compact.c  " + chr(92) + "\n"
        "../Src/user/inktag_protocol_core.c  " + chr(92) + "\n"
        "../Src/user/inktag_gr_nvds_guard.c  " + chr(92) + "\n"
        "../../../../../components/libraries/hal_flash/hal_flash.c  " + chr(92) + "\n"
        "../Src/user/inktag_gr_dfu_diagnostics.c  " + chr(92) +
        "\n../Src/user/inktag_gr_recovery_power.c",
        1,
    )
    makefile = replace_required(
        makefile,
        "PRJ_C_MICRO_DEFINES :=   \\",
        "PRJ_C_MICRO_DEFINES :=   \\\n"
        "INKTAG_GOODIX_GR5331  \\\n"
        "INKTAG_SWAP_BOOTLOADER  \\\n"
        "INKTAG_SWAP_EXECUTE_FROM_RAM  \\",
        make_src,
    )
    makefile = replace_required(
        makefile,
        "LINKER_SCRIPT_TMPL = ../../../../../platform/soc/linker/gcc/gcc_linker.lds",
        "LINKER_SCRIPT_TMPL = gcc_linker_gr5331_inktag.lds",
        make_src,
    )
    makefile = makefile.replace("IS_WIN_OS :=  true", "IS_WIN_OS :=  false")
    makefile += '\n.DEFAULT_GOAL := all\nCFLAGS += -MMD -MP\n$(OBJ_ADJUST): Makefile\n-include $(wildcard $(BUILD_OBJ)/*.d)\n'
    linker = retain_identity(linker)
    makefile = goodix_crc_recovery.makefile(makefile)
    goodix_crc_recovery.materialize(repo, target)
    goodix_crc_recovery.patch_transport(sdk)
    makefile = compact_diagnostic_makefile(makefile,repo)
    changed = []
    makefile = materialize_recovery_pins(repo, target, makefile, changed)
    for relative, content in [
        ("Src/config/custom_config.h", custom),
        ("Src/config/bootloader_config.h", boot_cfg),
        ("Src/user/bootloader_ota.c", ota),
        ("Src/user/bootloader_boot_vendor.c", boot_vendor),
        ("Src/user/main.c", main_source),
        ("Src/platform/user_periph_setup.c", periph),
        ("GCC/gcc_linker_gr5331_inktag.lds", linker),
        ("GCC/Makefile", makefile),
    ]:
        path = target / relative
        if write_if_changed(path, content):
            changed.append(relative)

    for source, relative in [
        (repo / "firmware/ports/goodix/common/src/inktag_gr_ble_address.c", "Src/user/inktag_gr_ble_address.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_ble_address.h", "Src/user/inktag_gr_ble_address.h"),
        (repo / "firmware/common/ble/inktag_ble_address.c", "Src/user/inktag_ble_address.c"),
        (repo / "firmware/common/ble/inktag_ble_address.h", "Src/user/inktag_ble_address.h"),
        (repo / "firmware/common/ble/inktag_ble_address_record.c", "Src/user/inktag_ble_address_record.c"),
        (repo / "firmware/common/ble/inktag_ble_address_uid_compact.c", "Src/user/inktag_ble_address_uid_compact.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_security_random.inc", "Src/user/inktag_gr_security_random.inc"),
        (repo / "firmware/common/protocol/inktag_protocol_core.c", "Src/user/inktag_protocol_core.c"),
        (repo / "firmware/common/protocol/inktag_protocol_core.h", "Src/user/inktag_protocol_core.h"),
        (repo / "firmware/common/epd/inktag_protocol_v2.h", "Src/user/inktag_protocol_v2.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_nvds_guard.c", "Src/user/inktag_gr_nvds_guard.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_nvds_guard.h", "Src/user/inktag_gr_nvds_guard.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_swap.c", "Src/user/inktag_gr_swap.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_swap.h", "Src/user/inktag_gr_swap.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_flash_layout.h", "Src/user/inktag_gr_flash_layout.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_ota_policy.h", "Src/user/inktag_gr_ota_policy.h"),
        (repo / "firmware/common/ota/inktag_recovery_version.h", "Src/user/inktag_recovery_version.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_dfu_diagnostics.c", "Src/user/inktag_gr_dfu_diagnostics.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_dfu_diagnostics.h", "Src/user/inktag_gr_dfu_diagnostics.h"),
        (repo / "firmware/common/ota/inktag_ota_reject.h", "Src/user/inktag_ota_reject.h"),
        (repo / "firmware/common/ota/inktag_swap.h", "Src/user/inktag_swap.h"),
        (repo / "firmware/common/ota/inktag_recovery.c", "Src/user/inktag_recovery.c"),
        (repo / "firmware/common/ota/inktag_recovery.h", "Src/user/inktag_recovery.h"),
        (repo / "firmware/common/power/inktag_power.h", "Src/user/inktag_power.h"),
        (repo / "firmware/common/power/inktag_power.c", "Src/user/inktag_power.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_recovery_power.c", "Src/user/inktag_gr_recovery_power.c"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_adc.h", "Src/user/inktag_gr_adc.h"),
        (repo / "firmware/ports/goodix/common/src/inktag_gr_adc_poll.inc", "Src/user/inktag_gr_adc_poll.inc"),
        (repo / "firmware/boards/gr5331_zkel.h", "Src/user/inktag_board.h"),
    ]:
        destination = target / relative
        if not destination.exists() or destination.read_bytes() != source.read_bytes():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            changed.append(relative)

    if refresh_config_dependent_outputs(
        target,
        [
            target / "Src/config/custom_config.h",
            target / "Src/config/bootloader_config.h",
            target / "GCC/gcc_linker_gr5331_inktag.lds",
            target / "GCC/Makefile",
            target / "Src/user/inktag_power.h",
            target / "Src/user/inktag_power.c",
            target / "Src/user/inktag_ota_reject.h",
            target / "Src/user/inktag_gr_dfu_diagnostics.c",
            target / "Src/user/inktag_gr_flash_layout.h",
            target / "Src/user/inktag_gr_ota_policy.h",
            target / "Src/user/inktag_gr_recovery_power.c",
            target / "Src/user/inktag_board.h",
        ],
        f"{artifact}-boot",
    ):
        changed.append("GCC/out config cache")

    suffix = "unchanged" if not changed else "updated: " + ", ".join(changed)
    print(f"{target} ({suffix})")


if __name__ == "__main__":
    main()
