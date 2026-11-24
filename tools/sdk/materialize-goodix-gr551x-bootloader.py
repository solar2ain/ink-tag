#!/usr/bin/env python3
"""Materialize the board-qualified shared GR5513/GR5515 InkTag Recovery."""

from __future__ import annotations

import argparse
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
    start = text.index("#if OTAS_VERSION")
    end = text.index("/*\n * LOCAL FUNCTION DEFINITIONS", start)
    text = text[:start] + """extern void inktag_recovery_led_cancel_download(void);
extern void inktag_recovery_led_init(void);
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
    adv_if = "#if OTAS_VERSION == 0x03\n    if (bootloader_dfu_info_get()->dfu_mode_pattern == DFU_NON_COPY_UPGRADE_MODE_PATTERN )\n    {\n        memcpy(s_adv_rsp_data_set + sizeof(s_adv_rsp_data_set) - DFU_ADV_NAME_RANDOM_DATA_SIZE, bootloader_dfu_info_get()->dfu_adv_name_random_data, DFU_ADV_NAME_RANDOM_DATA_SIZE);\n    }\n#endif\n"
    text = text.replace(adv_if, "")
    text = replace_required(text, "            bootloader_address_change();\n", "", source)
    text = replace_required(text, "            bootloader_address_restore();\n", "", source)
    text = replace_required(text, "#define NVDS_TAG_BD_ADDRESS_BACKUP      0xC0F1\n", "", source)
    text = replace_required(
        text,
        "static ble_gap_bdaddr_t         s_bd_addr;\n"
        "static bool                     s_bd_addr_changed_flag = false;\n\n",
        "",
        source,
    )
    address_start = text.index("static void bootloader_address_backup_error_handler(void)\n{")
    event_start = text.index("static void ble_evt_handler(const ble_evt_t *p_evt)\n{", address_start)
    text = text[:address_start] + text[event_start:]
    text = replace_required(
        text,
        "            bootloader_gap_params_init();\n"
        "            ble_gap_adv_start(0, &s_gap_adv_time_param);",
        "            bootloader_gap_params_init();\n"
        "            ble_gap_adv_start(0, &s_gap_adv_time_param);",
        source,
    )
    text = replace_required(
        text,
        "        case BLE_GAPC_EVT_CONNECTED:",
        "        case BLE_GAPM_EVT_ADV_START:\n"
        "            if (BLE_SUCCESS == p_evt->evt_status)\n"
        "                inktag_recovery_led_role_ready();\n"
        "            break;\n\n"
        "        case BLE_GAPC_EVT_CONNECTED:",
        source,
    )
    text = replace_required(
        text,
        "        case BLE_GAPC_EVT_DISCONNECTED:\n"
        "            APP_LOG_DEBUG(\">>> Disconnected and restart adv.\");",
        "        case BLE_GAPC_EVT_DISCONNECTED:\n"
        "            inktag_recovery_led_cancel_download();\n"
        "            APP_LOG_DEBUG(\">>> Disconnected and restart adv.\");",
        source,
    )
    return replace_required(
        text,
        "void bootloader_ota_task(void)\n{\n"
        "    ble_stack_init(ble_evt_handler, &heaps_table);",
        "void bootloader_ota_task(void)\n{\n"
        "    /* Only a boot that enters fixed Recovery owns this RGB round. */\n"
        "    inktag_recovery_led_init();\n"
        "    ble_stack_init(ble_evt_handler, &heaps_table);",
        source,
    )


def patch_unique_address(text: str, source: Path) -> str:
    text = replace_required(
        text,
        "void app_periph_init(void)\n{",
        "void app_periph_init(void)\n{\n"
        + "    mem_pwr_mgmt_mode_set(MEM_POWER_FULL_MODE);\n"
        + "    sys_swd_enable();",
        source,
    )
    return text


def patch_recovery_ota_led(text: str, source: Path) -> str:
    text = replace_required(
        text,
        '#include "board_SK.h"',
        '#include "board_SK.h"\n'
        '#include "gr55xx_ll_gpio.h"\n'
        '#include "gr55xx_ll_msio.h"\n'
        '#include "inktag_board.h"\n'
        '#include "inktag_gr_factory_seed.h"',
        source,
    )
    led_state = r"""
#if !INKTAG_BOARD_HAS_RUNTIME_RGB_LED || \
    (((INKTAG_BOARD_LED_RED_PIN) >> 6) != 2) || \
    (((INKTAG_BOARD_LED_GREEN_PIN) >> 6) != 2) || \
    (((INKTAG_BOARD_LED_BLUE_PIN) >> 6) != 0) || \
    ((INKTAG_BOARD_LED_BLUE_PIN) < 16) || \
    ((INKTAG_BOARD_LED_BLUE_PIN) > 31) || \
    !INKTAG_BOARD_LED_ACTIVE_LOW
#error "GR551x Recovery expects the descriptor-gated ZKC42V RGB LEDs"
#endif

#define INKTAG_RECOVERY_LED_DOWNLOAD_HALF_TICKS    6u
#define INKTAG_RECOVERY_LED_INSTALL_HALF_TICKS     3u
#define INKTAG_RECOVERY_LED_TRANSIENT_TICKS        18u
#define INKTAG_RECOVERY_LED_DOWNLOAD_IDLE_TICKS    125u
#define INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS       9u
#define INKTAG_RECOVERY_LED_BOOT_SEPARATOR_TICKS   3u
#define INKTAG_RECOVERY_LED_BOOT_READY_HALF_TICKS  3u
#define INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS       48u
#define INKTAG_RECOVERY_LED_RED_MASK \
    (1u << ((INKTAG_BOARD_LED_RED_PIN) & 0x3fu))
#define INKTAG_RECOVERY_LED_GREEN_MASK \
    (1u << ((INKTAG_BOARD_LED_GREEN_PIN) & 0x3fu))
#define INKTAG_RECOVERY_LED_BLUE_MASK \
    (1u << (((INKTAG_BOARD_LED_BLUE_PIN) & 0x3fu) - 16u))

typedef enum
{
    INKTAG_RECOVERY_LED_OFF = 0,
    INKTAG_RECOVERY_LED_BOOT,
    INKTAG_RECOVERY_LED_DOWNLOAD,
    INKTAG_RECOVERY_LED_INSTALL,
    INKTAG_RECOVERY_LED_ERROR,
} inktag_recovery_led_mode_t;

static bool s_inktag_recovery_led_available;
static volatile bool s_inktag_recovery_role_ready;
static volatile bool s_inktag_recovery_transfer_started;
static volatile inktag_recovery_led_mode_t s_inktag_recovery_led_mode;
static volatile uint8_t s_inktag_recovery_led_phase_ticks;
static volatile uint16_t s_inktag_recovery_led_download_idle_ticks;
static size_t s_inktag_recovery_led_last_tick;

static bool inktag_recovery_is_zkc42v(void)
{
    /* The release path always uses the whole validated configured LED tuple.
     * Bare/invalid bases fail dark; no legacy PCB-default branch is admitted. */
    return false;
}

static void inktag_recovery_led_output(bool red, bool green, bool blue)
{
    if (!s_inktag_recovery_led_available) return;
    if (red) ll_msio_reset_output_pin(INKTAG_RECOVERY_LED_RED_MASK);
    else ll_msio_set_output_pin(INKTAG_RECOVERY_LED_RED_MASK);
    if (green) ll_msio_reset_output_pin(INKTAG_RECOVERY_LED_GREEN_MASK);
    else ll_msio_set_output_pin(INKTAG_RECOVERY_LED_GREEN_MASK);
    if (blue) ll_gpio_reset_output_pin(GPIO1, INKTAG_RECOVERY_LED_BLUE_MASK);
    else ll_gpio_set_output_pin(GPIO1, INKTAG_RECOVERY_LED_BLUE_MASK);
}

void inktag_recovery_led_init(void)
{
    s_inktag_recovery_led_available = inktag_recovery_is_zkc42v();
    if (!s_inktag_recovery_led_available) return;

    // Set inactive latches before enabling the active-low outputs.
    inktag_recovery_led_output(false, false, false);
    ll_msio_set_pin_mux(INKTAG_RECOVERY_LED_RED_MASK |
                            INKTAG_RECOVERY_LED_GREEN_MASK,
                        LL_MSIO_MUX_7);
    ll_msio_set_pin_mode(INKTAG_RECOVERY_LED_RED_MASK |
                             INKTAG_RECOVERY_LED_GREEN_MASK,
                         LL_MSIO_MODE_DIGITAL);
    ll_msio_set_pin_pull(INKTAG_RECOVERY_LED_RED_MASK |
                             INKTAG_RECOVERY_LED_GREEN_MASK,
                         LL_MSIO_PULL_NO);
    ll_msio_set_pin_direction(INKTAG_RECOVERY_LED_RED_MASK |
                                  INKTAG_RECOVERY_LED_GREEN_MASK,
                              LL_MSIO_DIRECTION_OUTPUT);
    ll_gpio_set_mux_pin_8_15(GPIO1, INKTAG_RECOVERY_LED_BLUE_MASK,
                             LL_GPIO_MUX_7);
    ll_gpio_set_pin_pull(GPIO1, INKTAG_RECOVERY_LED_BLUE_MASK,
                         LL_GPIO_PULL_NO);
    ll_gpio_set_pin_mode(GPIO1, INKTAG_RECOVERY_LED_BLUE_MASK,
                         LL_GPIO_MODE_OUTPUT);
    s_inktag_recovery_role_ready = false;
    s_inktag_recovery_transfer_started = false;
    s_inktag_recovery_led_mode = INKTAG_RECOVERY_LED_BOOT;
    s_inktag_recovery_led_phase_ticks = 0u;
    s_inktag_recovery_led_last_tick = s_systick_irq_count;
    inktag_recovery_led_output(true, false, false);
}

void inktag_recovery_led_role_ready(void)
{
    /* The boot round starts immediately, but the ready tail is released only
     * by the asynchronous ADV_START success event. */
    if (!s_inktag_recovery_led_available) return;
    s_inktag_recovery_role_ready = true;
}

static void inktag_recovery_led_set(inktag_recovery_led_mode_t mode)
{
    s_inktag_recovery_led_mode = mode;
    s_inktag_recovery_led_phase_ticks = 0u;
    s_inktag_recovery_led_download_idle_ticks = 0u;
    inktag_recovery_led_output(mode == INKTAG_RECOVERY_LED_ERROR, false,
                               mode == INKTAG_RECOVERY_LED_DOWNLOAD ||
                                   mode == INKTAG_RECOVERY_LED_INSTALL);
}

void inktag_recovery_led_cancel_download(void)
{
    /* Download feedback describes a live host transfer.  Do not leave
     * Recovery blinking indefinitely after cancellation or link loss.
     * INSTALL and ERROR own their bounded post-transfer lifecycles. */
    s_inktag_recovery_transfer_started = false;
    if (s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_DOWNLOAD)
        inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);
}

static void inktag_recovery_led_tick(void)
{
    uint8_t half_ticks;
    bool on;
    if (s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_OFF) return;
    ++s_inktag_recovery_led_phase_ticks;
    if (s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_BOOT)
    {
        uint8_t phase = s_inktag_recovery_led_phase_ticks;
        bool red = phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS;
        bool green = phase >= INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS &&
                     phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 2u;
        bool blue = phase >= INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 2u &&
                    phase < INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u;
        uint8_t ready_start = INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u +
                              INKTAG_RECOVERY_LED_BOOT_SEPARATOR_TICKS;
        if (phase >= INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u &&
            !s_inktag_recovery_role_ready)
        {
            /* Hold the required dark separator until advertising really
             * starts.  Do not mistake command acceptance for readiness. */
            s_inktag_recovery_led_phase_ticks =
                INKTAG_RECOVERY_LED_BOOT_COLOR_TICKS * 3u;
            inktag_recovery_led_output(false, false, false);
            return;
        }
        if (phase >= ready_start &&
            phase < INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS)
            blue = (((phase - ready_start) /
                     INKTAG_RECOVERY_LED_BOOT_READY_HALF_TICKS) & 1u) == 0u;
        if (phase >= INKTAG_RECOVERY_LED_BOOT_TOTAL_TICKS)
        {
            inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);
            return;
        }
        inktag_recovery_led_output(red, green, blue);
        return;
    }
    if (s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_DOWNLOAD &&
        ++s_inktag_recovery_led_download_idle_ticks >=
            INKTAG_RECOVERY_LED_DOWNLOAD_IDLE_TICKS)
    {
        /* PROGRAM_START without following bytes is not an active download.
         * A missed disconnect must not leave Recovery blinking forever. */
        inktag_recovery_led_set(INKTAG_RECOVERY_LED_OFF);
        return;
    }
    half_ticks = s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_DOWNLOAD
                     ? INKTAG_RECOVERY_LED_DOWNLOAD_HALF_TICKS
                     : INKTAG_RECOVERY_LED_INSTALL_HALF_TICKS;
    if (s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_ERROR &&
        s_inktag_recovery_led_phase_ticks >= INKTAG_RECOVERY_LED_TRANSIENT_TICKS)
    {
        s_inktag_recovery_led_mode = INKTAG_RECOVERY_LED_OFF;
        inktag_recovery_led_output(false, false, false);
        return;
    }
    on = ((s_inktag_recovery_led_phase_ticks / half_ticks) & 1u) == 0u;
    inktag_recovery_led_output(
        s_inktag_recovery_led_mode == INKTAG_RECOVERY_LED_ERROR && on,
        false,
        s_inktag_recovery_led_mode != INKTAG_RECOVERY_LED_ERROR && on);
}

void inktag_recovery_led_poll(void)
{
    /* SysTick is shared with the vendor BLE/timeout plumbing.  Keep its ISR
     * to time accounting only; touching GPIO from that interrupt can preempt
     * the BLE stack while it is bringing advertising up. */
    size_t now = s_systick_irq_count;
    while (s_inktag_recovery_led_last_tick != now)
    {
        ++s_inktag_recovery_led_last_tick;
        inktag_recovery_led_tick();
    }
}

"""
    text = replace_required(
        text,
        "#if BOOTLOADER_DFU_ENABLE\nstatic void dfu_program_start_callback(void);",
        led_state
        + "#if BOOTLOADER_DFU_ENABLE\nstatic void dfu_program_start_callback(void);",
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("Start DFU OTA.");',
        '    /* Goodix invokes this callback while restoring/initializing DFU\n'
        '     * state.  It is not proof that the host sent firmware bytes. */\n'
        '    APP_LOG_DEBUG("Start DFU OTA.");',
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("DFU OTA %d%%", pro);',
        '    /* This callback is reached by program_flash_replace only after a\n'
        '     * verified firmware-data write.  That is the download boundary. */\n'
        '    s_inktag_recovery_transfer_started = true;\n'
        '    if (s_inktag_recovery_led_mode != INKTAG_RECOVERY_LED_DOWNLOAD)\n'
        '        inktag_recovery_led_set(INKTAG_RECOVERY_LED_DOWNLOAD);\n'
        '    s_inktag_recovery_led_download_idle_ticks = 0u;\n'
        '    APP_LOG_DEBUG("DFU OTA %d%%", pro);',
        source,
    )
    text = replace_required(
        text,
        '    APP_LOG_DEBUG("DFU OTA complete.");',
        '    if (s_inktag_recovery_transfer_started)\n'
        '    {\n'
        '        inktag_recovery_led_set(status == 1u ?\n'
        '            INKTAG_RECOVERY_LED_INSTALL : INKTAG_RECOVERY_LED_ERROR);\n'
        '        s_inktag_recovery_transfer_started = false;\n'
        '    }\n'
        '    APP_LOG_DEBUG("DFU OTA complete.");',
        source,
    )
    text = replace_required(
        text,
        "#define SYSTICK_IRQ_PERIOD          100  //unit: ms",
        "// Exact common status cadence: 6 ticks = 240 ms, 3 ticks = 120 ms.\n"
        "#define SYSTICK_IRQ_PERIOD          40  //unit: ms",
        source,
    )
    return text


def enforce_permanent_recovery_debug_attach(text: str, source: Path) -> str:
    active_mode = "    pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);"
    marker = "    /* Fixed Recovery never inherits an application debug-grace window. */"
    if active_mode not in text:
        raise SystemExit(
            f"fixed GR551x Recovery must retain PMR_MGMT_ACTIVE_MODE: {source}"
        )
    for forbidden in ("PMR_MGMT_IDLE_MODE", "PMR_MGMT_SLEEP_MODE"):
        if forbidden in text:
            raise SystemExit(
                f"fixed GR551x Recovery must remain directly attachable; "
                f"unexpected {forbidden} in {source}"
            )
    if marker not in text:
        text = text.replace(active_mode, marker + "\n" + active_mode, 1)
    dfu_init_tail = (
        "#else\n"
        "    dfu_port_init(NULL, DFU_FW_SAVE_ADDR, &dfu_pro_call);\n"
        "#endif\n"
        "#endif\n"
        "}"
    )
    return replace_required(
        text,
        dfu_init_tail,
        dfu_init_tail[:-1]
        + "    /* DFU/peripheral setup may rewrite debug routing.  Reassert\n"
        + "     * SWD last so fixed Recovery remains directly attachable. */\n"
        + "    sys_swd_enable();\n}",
        source,
    )


def patch_swap_boot_vendor(text: str, source: Path) -> str:
    text = replace_required(
        text, '#include "hal_flash.h"',
        '#include "hal_flash.h"\n#include "inktag_gr_swap.h"\n#include "inktag_gr_flash_layout.h"\n#include "inktag_gr_ota_policy.h"', source,
    )
    legacy_verify_start = text.find("static bool bootloader_app_dfu_fw_verify(void)")
    dfu_info_verify_start = text.find(
        "static bool bootloader_dfu_info_verify(void)", legacy_verify_start
    )
    if legacy_verify_start < 0 or dfu_info_verify_start < 0:
        raise SystemExit(f"cannot remove GR551x copy-image verification in {source}")
    text = text[:legacy_verify_start] + text[dfu_info_verify_start:]

    verify_start = text.find("static bool bootloader_app_fw_verify()")
    verify_end = text.find("\nstatic void app_bootloader_jump", verify_start)
    if verify_start < 0 or verify_end < 0:
        raise SystemExit(f"cannot isolate GR551x APP_INFO verification in {source}")
    verify = r'''static bool bootloader_app_fw_verify()
{
    uint32_t layout_marker[2] = {0u, 0u};
    memset((uint8_t *)&s_app_img_info, 0, sizeof(s_app_img_info));
    (void)hal_flash_read_judge_security(APP_INFO_START_ADDR,
                                        (uint8_t *)&s_app_img_info,
                                        sizeof(s_app_img_info));
    (void)hal_flash_read_judge_security(
        APP_INFO_START_ADDR + INKTAG_GR_IMAGE_INFO_STRUCT_LEN,
        (uint8_t *)layout_marker, sizeof(layout_marker));
    if (s_app_img_info.pattern != DFU_IMG_INFO_PATTERN ||
        layout_marker[0] != INKTAG_GR_LAYOUT_MARKER_MAGIC ||
        layout_marker[1] != INKTAG_GR_LAYOUT_MARKER_INVERSE ||
        s_app_img_info.boot_info.load_addr != APP_FW_LOAD_ADDR ||
        s_app_img_info.boot_info.run_addr != APP_FW_LOAD_ADDR ||
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
        raise SystemExit(f"cannot remove GR551x copy/self-repair writes in {source}")
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
        raise SystemExit(f"cannot isolate GR551x legacy copy-mode path in {source}")
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
        '#include "dfu_port.h"\n#include "inktag_gr_swap.h"\n\n'
        'extern void inktag_recovery_led_poll(void);', source,
    )
    text = replace_required(
        text,
        "    app_periph_init();\n    bootloader_dfu_task();",
        "    app_periph_init();\n"
        "    inktag_swap_result_t swap_result = inktag_gr_swap_boot();\n"
        "    if (swap_result == INKTAG_SWAP_RESET_REQUIRED)\n"
        "    {\n"
        "        hal_nvic_system_reset();\n"
        "    }\n"
        "    bootloader_dfu_task();",
        source,
    )
    return replace_required(
        text,
        "        bootloader_timeout_task();",
        "        inktag_recovery_led_poll();\n"
        "        bootloader_timeout_task();",
        source,
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True)
    ap.add_argument("--repo", default=Path(__file__).resolve().parents[2])
    ap.add_argument("--sdk-root", type=Path, required=True,
                    help="absolute owned SDK workspace under the durable primary SDK root")
    ap.add_argument("--internal-images", action="store_true",
                    help="maintainer-only matching GR5513 internal-image Recovery")
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
    if board is None or board.get("port") != "goodix/gr551x":
        raise SystemExit(f"board {args.board!r} is not a GR551x board")
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
    custom = replace_required(custom, "#define CHIP_TYPE  4", "#define CHIP_TYPE  2", custom_src)
    custom = replace_required(custom, "#define CFG_LPCLK_INTERNAL_EN   0", "#define CFG_LPCLK_INTERNAL_EN   1", custom_src)
    custom = replace_required(custom, "#define NVDS_START_ADDR         0x010FF000", "#define NVDS_START_ADDR         0x010FC000", custom_src)
    custom = replace_required(custom, "#define NVDS_NUM_SECTOR          1", "#define NVDS_NUM_SECTOR          4", custom_src)
    if args.internal_images:
        custom = replace_required(custom, "#define CHIP_TYPE  2", "#define CHIP_TYPE  7", custom_src)
        custom = custom.replace("#define NVDS_START_ADDR         0x0107F000", "#define NVDS_START_ADDR         0x0107E000")
        custom = replace_required(custom, "#define NVDS_START_ADDR         0x010FC000", "#define NVDS_START_ADDR         0x0107E000", custom_src)
        custom = replace_required(custom, "#define NVDS_NUM_SECTOR          4", "#define NVDS_NUM_SECTOR          2", custom_src)
    # Production OTA diagnostics are reported by the application/Web client;
    # keeping the vendor UART/RTT formatter in the boot path costs more than
    # the RSA verifier. Disable only the bootloader log backend.
    custom = replace_required(custom, "#define APP_LOG_ENABLE            1", "#define APP_LOG_ENABLE            0", custom_src)
    # Recovery is one peripheral, one connection and one OTAS profile.  The
    # GR551x SDK template otherwise reserves ten profiles, four bonds, five
    # links and a scanner, which also keeps central/scan patch paths reachable
    # in the fixed boot image.  Retain one spare profile entry, matching the
    # already-qualified GR5331 Recovery policy, but do not advertise or build
    # resources that Recovery can never use.
    custom = replace_required(custom, "#define CFG_MAX_PRFS             10", "#define CFG_MAX_PRFS             2", custom_src)
    custom = replace_required(custom, "#define CFG_MAX_BOND_DEVS        4", "#define CFG_MAX_BOND_DEVS        1", custom_src)
    custom = replace_required(custom, "#define CFG_MAX_CONNECTIONS      5", "#define CFG_MAX_CONNECTIONS      1", custom_src)
    custom = replace_required(custom, "#define CFG_MAX_SCAN             1", "#define CFG_MAX_SCAN             0", custom_src)

    boot_cfg_src = template / "Src/config/bootloader_config.h"
    boot_cfg = boot_cfg_src.read_text(encoding="utf-8")
    boot_cfg = goodix_crc_recovery.boot_config(boot_cfg)
    boot_cfg = replace_required(boot_cfg, "#define BOOTLOADER_BY_NAME_ENABLE               1", "#define BOOTLOADER_BY_NAME_ENABLE               0", boot_cfg_src)
    boot_cfg = replace_required(boot_cfg, "#define BOOTLOADER_BY_ADDR_ENABLE               0", "#define BOOTLOADER_BY_ADDR_ENABLE               1", boot_cfg_src)
    boot_cfg = replace_required(boot_cfg, "#define APP_FW_LOAD_ADDR                        (FLASH_START_ADDR + 0x40000)", "#define APP_FW_LOAD_ADDR                        (FLASH_START_ADDR + 0x20000)", boot_cfg_src)
    boot_cfg = replace_required(boot_cfg, "#define DFU_FW_SAVE_ADDR                        (FLASH_START_ADDR + 0x40000)", "#define DFU_FW_SAVE_ADDR                        (FLASH_START_ADDR + 0x20000)", boot_cfg_src)

    linker_src = template / "GCC/gcc_linker_gr5515.lds"
    linker = linker_src.read_text(encoding="utf-8")
    linker = replace_required(
        linker,
        "FLASH (rx) : ORIGIN = 0x01004000, LENGTH = (0x00800000 -0x00002000)",
        "FLASH (rx) : ORIGIN = 0x01004000, LENGTH = 0x0001C000",
        linker_src,
    )
    # The qualified ZKC42VM reports SYS_DEV_SRAM_128K and aliases addresses
    # above the low 128 KiB window. Keep the fixed Boot and application on the
    # same physical SRAM contract instead of trusting the generic 256 KiB SDK
    # linker template for every GR551x package.
    linker = replace_required(
        linker,
        "RAM (rwx)  : ORIGIN = 0x30000000 +0x4000, LENGTH = (0x00040000 -0x4000)",
        "RAM (rwx)  : ORIGIN = 0x30000000 +0x4000, LENGTH = (0x00020000 -0x4000)",
        linker_src,
    )

    make_src = template / "GCC/Makefile"
    makefile = make_src.read_text(encoding="utf-8")
    makefile = replace_required(makefile, "MAKE_TARGET_NAME :=  app_bootloader", f"MAKE_TARGET_NAME :=  {artifact}-boot", make_src)
    makefile = replace_required(
        makefile,
        "../../../../../components/libraries/app_bootloader/bootloader_ota.c  \\",
        "../Src/user/inktag_bootloader_ota.c  \\",
        make_src,
    )
    makefile = replace_required(
        makefile,
        "../../../../../components/libraries/app_bootloader/bootloader_boot_vendor.c  \\",
        "../Src/user/inktag_bootloader_boot_vendor.c  \\\n"
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
        "INKTAG_GOODIX_GR5515  \\\n"
        "INKTAG_SWAP_BOOTLOADER  \\\n"
        "INKTAG_SWAP_EXECUTE_FROM_RAM  \\",
        make_src,
    )
    makefile = replace_required(makefile, "COMMON_COMPILE_FLAGS += -ggdb3", "COMMON_COMPILE_FLAGS += -ggdb3 -Oz", make_src)
    if args.internal_images:
        makefile = makefile.replace("INKTAG_GOODIX_GR5515  " + chr(92),
            "INKTAG_GOODIX_GR5515  " + chr(92) + "\nINKTAG_GR5513_INTERNAL_IMAGES=1  " + chr(92),1)
    makefile = replace_required(
        makefile,
        "LDFLAGS += -Wl,--gc-sections",
        "LDFLAGS += -Wl,--gc-sections\nLDFLAGS += --specs=nano.specs",
        make_src,
    )
    makefile = makefile.replace("IS_WIN_OS :=  true", "IS_WIN_OS :=  false")

    periph_src = template / "Src/platform/user_periph_setup.c"
    periph = periph_src.read_text(encoding="utf-8")
    periph = replace_required(
        periph,
        '#include "dfu_port.h"',
        '#include "dfu_port.h"\n#include "platform_sdk.h"',
        periph_src,
    )
    periph = replace_required(
        periph,
        "void cortex_backtrace_fault_handler(void)\n{\n    hal_nvic_system_reset();",
        "void cortex_backtrace_fault_handler(uint32_t fault_handler_lr, uint32_t fault_handler_sp)\n"
        "{\n"
        "    (void)fault_handler_lr;\n"
        "    (void)fault_handler_sp;\n"
        "    hal_nvic_system_reset();",
        periph_src,
    )
    periph = patch_unique_address(periph, periph_src)
    periph = patch_recovery_ota_led(periph, periph_src)
    periph = patch_recovery_pin_leds(periph, gr551x=True)
    periph = patch_no_stdio(periph, periph_src)
    periph = enforce_permanent_recovery_debug_attach(periph, periph_src)
    periph += r"""

typedef struct
{
    uint32_t magic;
    uint32_t exc_return;
    uint32_t stacked_r0;
    uint32_t stacked_r1;
    uint32_t stacked_r2;
    uint32_t stacked_r3;
    uint32_t stacked_r12;
    uint32_t stacked_lr;
    uint32_t stacked_pc;
    uint32_t stacked_psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t dfsr;
    uint32_t afsr;
    uint32_t mmfar;
    uint32_t bfar;
} inktag_boot_fault_record_t;

volatile inktag_boot_fault_record_t g_inktag_boot_fault_record;

SECTION_RAM_CODE __attribute__((used, noinline)) void inktag_boot_fault_capture(uint32_t sp, uint32_t exc_return)
{
    volatile const uint32_t *stacked = (volatile const uint32_t *)sp;

    g_inktag_boot_fault_record.magic = 0;
    g_inktag_boot_fault_record.exc_return = exc_return;
    g_inktag_boot_fault_record.stacked_r0 = stacked[0];
    g_inktag_boot_fault_record.stacked_r1 = stacked[1];
    g_inktag_boot_fault_record.stacked_r2 = stacked[2];
    g_inktag_boot_fault_record.stacked_r3 = stacked[3];
    g_inktag_boot_fault_record.stacked_r12 = stacked[4];
    g_inktag_boot_fault_record.stacked_lr = stacked[5];
    g_inktag_boot_fault_record.stacked_pc = stacked[6];
    g_inktag_boot_fault_record.stacked_psr = stacked[7];
    g_inktag_boot_fault_record.cfsr = *((volatile uint32_t *)0xE000ED28U);
    g_inktag_boot_fault_record.hfsr = *((volatile uint32_t *)0xE000ED2CU);
    g_inktag_boot_fault_record.dfsr = *((volatile uint32_t *)0xE000ED30U);
    g_inktag_boot_fault_record.afsr = *((volatile uint32_t *)0xE000ED3CU);
    g_inktag_boot_fault_record.mmfar = *((volatile uint32_t *)0xE000ED34U);
    g_inktag_boot_fault_record.bfar = *((volatile uint32_t *)0xE000ED38U);
    g_inktag_boot_fault_record.magic = 0x47524631U; /* GRF1 */

    for (;;)
    {
    }
}

SECTION_RAM_CODE __attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b inktag_boot_fault_capture\n"
    );
}
"""
    # The application materializer adds early-boot diagnostics to the shared
    # GR551x SDK startup code.  The bootloader links that same startup unit but
    # intentionally has no retained diagnostic block, so satisfy the hook with
    # a local no-op instead of pulling application state into the boot image.
    periph += "\nvoid inktag_gr_boot_stage_set(uint32_t stage)\n{\n    (void)stage;\n}\n"

    ota_src = sdk / "components/libraries/app_bootloader/bootloader_ota.c"
    ota = patch_ota_name(ota_src.read_text(encoding="utf-8"), ota_src)
    boot_vendor_src = sdk / "components/libraries/app_bootloader/bootloader_boot_vendor.c"
    boot_vendor = patch_swap_boot_vendor(boot_vendor_src.read_text(encoding="utf-8"), boot_vendor_src)
    boot_vendor = patch_boot_admission(boot_vendor)
    linker = retain_identity(linker)
    makefile = goodix_crc_recovery.makefile(makefile)
    goodix_crc_recovery.materialize(repo, target)
    goodix_crc_recovery.patch_transport(sdk)
    makefile = compact_diagnostic_makefile(makefile,repo)
    main_src = template / "Src/user/main.c"
    main_source = patch_swap_main(main_src.read_text(encoding="utf-8"), main_src)

    changed = []
    makefile = materialize_recovery_pins(repo, target, makefile, changed)
    # Makefile changes rebuild old dependency-less objects once. Subsequent
    # source/header/linker changes remain incremental; never delete SDK output.
    makefile += "\nCFLAGS += -MMD -MP\n"
    makefile += "$(OBJ_ADJUST): Makefile\n"
    makefile += "-include $(wildcard $(BUILD_OBJ)/*.d)\n"
    makefile = makefile.replace(
        "$(BUILD_LST)/$(MAKE_TARGET_NAME).elf: $(OBJ_ADJUST)",
        "$(BUILD_LST)/$(MAKE_TARGET_NAME).elf: $(OBJ_ADJUST) $(LINK_SCRIPT) $(PATCH_FILE)",
    )
    for relative, content in [
        ("Src/config/custom_config.h", custom),
        ("Src/config/bootloader_config.h", boot_cfg),
        ("Src/platform/user_periph_setup.c", periph),
        ("Src/user/inktag_bootloader_ota.c", ota),
        ("Src/user/inktag_bootloader_boot_vendor.c", boot_vendor),
        ("Src/user/main.c", main_source),
        ("GCC/gcc_linker_gr5515.lds", linker),
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
        (repo / "firmware/ports/goodix/common/src/inktag_gr_factory_seed.h", "Src/user/inktag_gr_factory_seed.h"),
        (repo / "firmware/common/storage/inktag_board_record.h", "Src/user/inktag_board_record.h"),
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
        (repo / "firmware/boards/gr551x_zkel.h", "Src/user/inktag_board.h"),
    ]:
        destination = target / relative
        if not destination.exists() or destination.read_bytes() != source.read_bytes():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            changed.append(relative)

    suffix = "unchanged" if not changed else "updated: " + ", ".join(changed)
    print(f"{target} ({suffix})")


if __name__ == "__main__":
    main()
