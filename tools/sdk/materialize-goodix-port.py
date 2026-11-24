#!/usr/bin/env python3
import argparse
import json
import re
import shutil
import subprocess
import sys
import hashlib
from pathlib import Path
try:
    from inktag_source_profiles import source_path, profile_defines
except ModuleNotFoundError:
    from tools.sdk.inktag_source_profiles import source_path, profile_defines
try:
    from goodix_diagnostic_literals import makefile as compact_diagnostic_makefile
except ModuleNotFoundError:
    from tools.sdk.goodix_diagnostic_literals import makefile as compact_diagnostic_makefile
try:  # Direct script and repository-root importlib/runpy test entrypoints.
    from goodix_nvds_guard import patch_goodix_nvds_guard
    from goodix_resume_transport import patch_resume_transport
except ModuleNotFoundError:
    from tools.sdk.goodix_nvds_guard import patch_goodix_nvds_guard
    from tools.sdk.goodix_resume_transport import patch_resume_transport

COMMON_SOURCES = [
    "inktag_gr_app.c", "inktag_gr_app.h", "inktag_gr_protocol_v2.inc",
    "inktag_gr_advertising_pro.h",
    "inktag_gr_cooperative.h",
    "inktag_gr_health.c", "inktag_gr_health.h",
    "inktag_gr_adc.h", "inktag_gr_adc_poll.inc",
    "inktag_gr_aux_io.c", "inktag_gr_aux_io.h",
    "inktag_gr_nfc.c", "inktag_gr_nfc_pro.h",
    "inktag_gr_activation.c", "inktag_gr_activation.h",
    "inktag_gr_security_random.inc",
    "inktag_gr_findmy.c", "inktag_gr_findmy_pro.h",
    "inktag_gr_find_public_store.c", "inktag_gr_find_public_store_pro.h",
    "inktag_gr_p224_public.c", "inktag_gr_p224_public_pro.h",
    "inktag_gr_gui_assets.c", "inktag_gr_gui_assets.h",
    "inktag_gr_flash_layout.h",
    "inktag_gr_features.h",
    "inktag_gr_service.c", "inktag_gr_service.h",
    "inktag_gr_external_flash.c", "inktag_gr_external_flash.h",
    "inktag_gr_image_store.c", "inktag_gr_image_store.h",
    "inktag_gr_image_io.c", "inktag_gr_image_io.h",
    "inktag_gr_dfu_diagnostics.c", "inktag_gr_dfu_diagnostics.h",
    "inktag_gr_settings_map.c", "inktag_gr_settings_map.h",
    "inktag_gr_settings_store.c", "inktag_gr_settings_store.h",
    "inktag_gr_nvds_guard.c", "inktag_gr_nvds_guard.h",
    "inktag_gr_ble_address.c", "inktag_gr_ble_address.h",
    "inktag_gr_board_identity.c", "inktag_gr_board_identity.h",
    "inktag_gr_factory_seed.h",
    "inktag_gr_pin_config.c", "inktag_gr_pin_config.h",
    "inktag_gr_pin_storage.c", "inktag_gr_pin_storage.h",
    "inktag_gr_pin_routes.c", "inktag_gr_pin_routes.h", "inktag_gr_pin_gpio.h",
    "inktag_gr_screen.c", "inktag_gr_screen.h",
    "inktag_gr_device_info_qr.c", "inktag_gr_device_info_qr.h",
    "inktag_gr_ota_policy.h",
    "inktag_gr_swap.c", "inktag_gr_swap.h",
    "epd_driver_goodix.c", "sdk_config.h", "user_app.h",
]
OTA_SOURCES = [
    "inktag_recovery_version.h",
    "inktag_swap.h", "inktag_recovery.c", "inktag_recovery.h",
    "inktag_ota_reject.h",
]
EPD_SOURCES = ["inktag_epd_config.c", "inktag_epd_config.h", "inktag_epd_driver.h", "inktag_epd_screen.c", "inktag_epd_screen.h", "inktag_screen_busy.h", "inktag_busy_defaults.inc", "inktag_epd_runtime.c", "inktag_epd_commands.c", "inktag_epd_temperature.h", "inktag_epd_wait_policy.h", "inktag_page_cycle.c", "inktag_page_cycle.h", "inktag_protocol_v2.h", "inktag_version.h", "inktag_epd_uc.c", "inktag_epd_e6.inc", "inktag_epd_jd.c", "inktag_epd_jd_shared.inc", "inktag_epd_ssd.c"]
GUI_SOURCES = [
    "inktag_solar_terms.inc",
    "inktag_gui.c", "inktag_gui.h", "inktag_gui_default.inc", "inktag_gui_edition_badge.inc", "inktag_gui_edition_name.inc", "inktag_gui_calendar.inc", "inktag_gui_assets.h", "inktag_gui_font_cache.h", "inktag_gui_asset_package.c", "inktag_gui_asset_package.h", "inktag_gui_font_roles.h", "inktag_gui_fixed_layout.h", "inktag_gui_large_digits.h", "inktag_gui_device_qr.h", "inktag_gui_device_qr_asset.c", "inktag_calendar.c", "inktag_calendar.h", "inktag_gui_fonts.c", "inktag_gui_fonts.h",
    "u8g2_font.c", "u8g2_font.h", "Adafruit_GFX.c", "Adafruit_GFX.h",
]
STATUS_SOURCES = ["inktag_status.c", "inktag_status.h", "inktag_temperature_wire.h"]
POWER_SOURCES = ["inktag_power.c", "inktag_power.h"]
HEALTH_SOURCES = ["inktag_soak_telemetry.c", "inktag_soak_telemetry.h",
                  "inktag_health_store_retry.h"]
INPUT_SOURCES = [
    "inktag_input.c", "inktag_input.h",
    "inktag_input_simulation.c", "inktag_input_simulation.h",
    "inktag_nfc_command.c", "inktag_nfc_command.h",
    "inktag_ndef.c", "inktag_ndef.h",
    "inktag_type2_uri_store.c", "inktag_type2_uri_store.h",
    "inktag_nfc_link.c", "inktag_nfc_link.h",
]
BLE_SOURCES = [
    "inktag_findmy.c", "inktag_findmy.h",
    "inktag_findmy_public.c", "inktag_findmy_public.h",
    "inktag_ble_identity.h", "inktag_ble_power.h",
    "inktag_ble_address.c", "inktag_ble_address.h", "inktag_ble_address_record.c", "inktag_ble_address_uid_compact.c",
    "inktag_device_display_name.h",
]
PROTOCOL_SOURCES = [
    "inktag_pin_config.c", "inktag_pin_config.h", "inktag_aux_io.c", "inktag_aux_io.h", "inktag_release_v1.h",
    "inktag_protocol_core.c", "inktag_protocol_core.h",
    "inktag_image_upload_v5.c", "inktag_image_upload_v5.h",
    "inktag_cooperative_snapshot.h",
    "inktag_display_settings.c", "inktag_display_settings.h",
    "inktag_packbits.c", "inktag_packbits.h",
    "inktag_lzo1x.c", "inktag_lzo1x.h",
]
LIFECYCLE_SOURCES = ["inktag_lifecycle.c", "inktag_lifecycle.h"]
STORAGE_SOURCES = [
    "inktag_resource_name.h", "inktag_resource_name.c",
    "inktag_image_fixed_store.h", "inktag_image_fixed_store.inc",
    "inktag_spi_nor_sfdp.h",
    "inktag_board_record.h",
    "inktag_settings_records.c", "inktag_settings_records.h",
    "inktag_settings_journal.c", "inktag_settings_journal.h",
]
SECURITY_SOURCES = [
    "inktag_device_identity.inc",
    "inktag_code_guard.c", "inktag_code_guard.h",
    "inktag_access.c", "inktag_access.h",
    "inktag_activation.c", "inktag_activation.h",
    "inktag_brand_overlay.c", "inktag_brand_overlay.h",
    "inktag_image_quota.c", "inktag_image_quota.h",
    "inktag_activation_product_key.h",
    "inktag_sha256.c", "inktag_sha256.h",
    "inktag_device_password.c", "inktag_device_password.h",
    "inktag_device_password_store.c", "inktag_device_password_store.h",
]
PLATFORM_SOURCES = ["inktag_elapsed_clock.h"]
TIME_SOURCES = ["inktag_clock_rate.c", "inktag_clock_rate.h",
                "inktag_time_checkpoint.h"]

# Both Goodix application targets are full-capability products. Keep their
# controller and exact-recipe catalog explicit here instead of relying on the
# common defaults, so a size-oriented platform change cannot silently remove a
# profile from only one Goodix OTA artifact. Recovery uses separate
# materializers and intentionally does not consume this application feature set.
GOODIX_APPLICATION_EPD_DEFINES = [
    "INKTAG_GR_PIN_CONFIG_PORT=1",
    "INKTAG_EPD_ENABLE_UC8XXX=1",
    "INKTAG_EPD_ENABLE_UC8159=1",
    "INKTAG_EPD_ENABLE_UC8179=1",
    "INKTAG_EPD_ENABLE_SSD1XXX=1",
    "INKTAG_EPD_ENABLE_SSD1675=1",
    "INKTAG_EPD_ENABLE_SSD1677=1",
    "INKTAG_EPD_ENABLE_SSD1683=1",
    "INKTAG_EPD_ENABLE_JD79XXX=1",
    "INKTAG_EPD_ENABLE_JD79665CA=1",
    # Accepted common software recipes, not exact-panel qualification.
    "INKTAG_EPD_ENABLE_OSPREY_E6=1",
    "INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ=1",
    "INKTAG_EPD_OSPREY_E6_PON_NO_WAIT=1",
    "INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q=1",
    "INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154=1",
    "INKTAG_EPD_ENABLE_HANSHOW_OLD_200=1",
    "INKTAG_EPD_ENABLE_STELLAR154=1",
]

def write_if_changed(path: Path, data: str):
    if path.exists() and path.read_text(encoding="utf-8") == data:
        return False
    path.write_text(data, encoding="utf-8", newline="\n")
    return True


def patch_goodix_dfu_service_status(sdk: Path):
    """Make the vendor's otherwise-void OTAS registration result observable."""
    header = sdk / "components/libraries/dfu_port/dfu_port.h"
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    header_text = header.read_text(encoding="utf-8")
    source_text = source.read_text(encoding="utf-8")
    old_decl = "void dfu_service_init(dfu_enter_callback dfu_enter);"
    new_decl = "sdk_err_t dfu_service_init(dfu_enter_callback dfu_enter);"
    old_body = "void dfu_service_init(dfu_enter_callback dfu_enter)\n{"
    new_body = "sdk_err_t dfu_service_init(dfu_enter_callback dfu_enter)\n{"
    if old_decl in header_text:
        header_text = header_text.replace(old_decl, new_decl, 1)
    elif new_decl not in header_text:
        raise SystemExit(f"cannot expose Goodix DFU service status in {header}")
    if old_body in source_text:
        source_text = source_text.replace(old_body, new_body, 1)
    elif new_body not in source_text:
        raise SystemExit(f"cannot change Goodix DFU service signature in {source}")
    guarded_old_end = "    otas_service_init(&otas_init);\n#endif\n}"
    guarded_new_end = (
        "    return otas_service_init(&otas_init);\n"
        "#else\n"
        "    return SDK_ERR_REQ_NOT_SUPPORTED;\n"
        "#endif\n}"
    )
    plain_old_end = "    otas_service_init(&otas_init);\n}"
    plain_new_end = "    return otas_service_init(&otas_init);\n}"
    if guarded_old_end in source_text:
        source_text = source_text.replace(guarded_old_end, guarded_new_end, 1)
    elif plain_old_end in source_text:
        source_text = source_text.replace(plain_old_end, plain_new_end, 1)
    elif guarded_new_end not in source_text and plain_new_end not in source_text:
        raise SystemExit(f"cannot return Goodix OTAS registration status from {source}")
    return write_if_changed(header, header_text) | write_if_changed(source, source_text)

def patch_goodix_literal_fault_log(sdk: Path):
    """Default-off size candidate: typed fault text, unchanged SDK log envelope.

    Preserve the original varargs API and all diagnostics. The new literal
    entry avoids a second general formatter for the already encoded message.
    App-only build selector; Recovery does not consume the common GUI formatter.
    """
    log = sdk / "components/libraries/app_log/app_log.c"
    text = log.read_text(encoding="utf-8")
    marker = "/* InkTag default-off literal fault log experiment. */"
    changed = False
    if marker not in text:
        start = text.index("void app_log_output(")
        end = text.index("\nvoid app_log_raw_info(", start)
        original = text[start:end]
        signature = "const char *format, ...)"
        if original.count(signature) != 1 or original.count("va_start(ap, format);") != 1:
            raise SystemExit(f"unknown Goodix log envelope: {log}")
        encoded = original.replace("void app_log_output(", "static void app_log_output_encode(", 1)
        encoded = encoded.replace(signature, "const char *format, va_list *ap)", 1)
        encoded = encoded.replace("    va_list  ap;\n", "", 1)
        encoded = encoded.replace("    va_start(ap, format);\n", "", 1)
        encoded = encoded.replace("    va_end(ap);\n", "", 1)
        formatting = "    fmt_result = vsnprintf((char *)s_log_encode_buf + log_length, APP_LOG_LINE_BUF_SIZE - log_length, format, ap);"
        if encoded.count(formatting) != 1:
            raise SystemExit(f"unknown Goodix payload formatting: {log}")
        encoded = encoded.replace(formatting, """    if (ap != NULL)
    {
        fmt_result = vsnprintf((char *)s_log_encode_buf + log_length, APP_LOG_LINE_BUF_SIZE - log_length, format, *ap);
    }
    else
    {
        size_t length = strlen(format);
        size_t capacity = APP_LOG_LINE_BUF_SIZE - log_length;
        if (capacity != 0u)
        {
            size_t stored = length < capacity ? length : capacity - 1u;
            memcpy(s_log_encode_buf + log_length, format, stored);
            s_log_encode_buf[log_length + stored] = '\\0';
        }
        fmt_result = (int)length;
    }""", 1)
        wrappers = """
void app_log_output(uint8_t level, const char *tag, const char *file, const char *func, const long line, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    app_log_output_encode(level, tag, file, func, line, format, &ap);
    va_end(ap);
}

void inktag_app_log_output_string(uint8_t level, const char *tag, const char *file, const char *func, const long line, const char *message)
{
    app_log_output_encode(level, tag, file, func, line, message, NULL);
}
"""
        text = text[:start] + marker + "\n#if INKTAG_GR_SDK_LITERAL_FAULT_LOG\n" + encoded + wrappers + "\n#else\n" + original + "\n#endif\n" + text[end:]
        changed |= write_if_changed(log, text)
    error = sdk / "components/libraries/app_error/app_error.c"
    text = error.read_text(encoding="utf-8")
    if marker not in text:
        anchor = "#include <stdarg.h>"
        if anchor not in text or text.count("sprintf(s_error_print_info") != 3:
            raise SystemExit(f"unknown Goodix fault format inventory: {error}")
        text = text.replace(anchor, anchor + "\n" + marker + """
#if INKTAG_GR_SDK_LITERAL_FAULT_LOG
#include "Adafruit_GFX.h"
void inktag_app_log_output_string(uint8_t, const char *, const char *, const char *, long, const char *);
#define INKTAG_FAULT_FORMAT(dst, ...) GFX_format(dst, sizeof(dst), __VA_ARGS__)
#define INKTAG_FAULT_OUTPUT(level, tag, file, func, line, format, message) \\
    inktag_app_log_output_string(level, tag, file, func, line, message)
#else
#define INKTAG_FAULT_FORMAT sprintf
#define INKTAG_FAULT_OUTPUT app_log_output
#endif
""", 1)
        text = text.replace("sprintf(s_error_print_info", "INKTAG_FAULT_FORMAT(s_error_print_info")
        text = text.replace("    app_log_output(APP_LOG_LVL_ERROR,", "    INKTAG_FAULT_OUTPUT(APP_LOG_LVL_ERROR,", 1)
        changed |= write_if_changed(error, text)
    # Fault-trace's strong RAM callbacks supersede the weak app_assert ones.
    # Both fixed diagnostic inventories must retain their full output; otherwise
    # the same full-newlib engine remains live through the strong callbacks.
    trace_count = 5 if sdk.name == "GR533x.SDK" else 4
    for component, destination, count in (("app_assert", "assert_info", 3),
                                           ("fault_trace", "s_fault_info", trace_count)):
        source = sdk / f"components/libraries/{component}/{component}.c"
        text = source.read_text(encoding="utf-8")
        if marker in text:
            continue
        anchor = "#include <stdarg.h>"
        if anchor not in text or text.count(f"sprintf({destination}") != count:
            raise SystemExit(f"unknown Goodix {component} format inventory: {source}")
        text = text.replace(anchor, anchor + "\n" + marker + """
#if INKTAG_GR_SDK_LITERAL_FAULT_LOG
#include "Adafruit_GFX.h"
void inktag_app_log_output_string(uint8_t, const char *, const char *, const char *, long, const char *);
#define INKTAG_FAULT_FORMAT(dst, ...) GFX_format(dst, sizeof(dst), __VA_ARGS__)
#define INKTAG_FAULT_OUTPUT(level, tag, file, func, line, format, message) \\
    inktag_app_log_output_string(level, tag, file, func, line, message)
#else
#define INKTAG_FAULT_FORMAT sprintf
#define INKTAG_FAULT_OUTPUT app_log_output
#endif
""", 1)
        text = text.replace(f"sprintf({destination}", f"INKTAG_FAULT_FORMAT({destination}")
        if component == "app_assert":
            text = text.replace("    app_log_output(APP_LOG_LVL_ERROR,", "    INKTAG_FAULT_OUTPUT(APP_LOG_LVL_ERROR,", 1)
        changed |= write_if_changed(source, text)
    return changed


def patch_goodix_fault_capture(sdk: Path, platform: str):
    """Route HardFault through InkTag's retained mailbox before SDK logging."""
    source = sdk / "components/libraries/fault_trace/fault_trace.c"
    text = source.read_text(encoding="utf-8")
    weak_hook = """
__WEAK void inktag_gr_health_fault_capture(uint32_t stack_pointer)
{
    (void)stack_pointer;
}
"""
    if "__WEAK void inktag_gr_health_fault_capture" not in text:
        marker = "#if SYS_FAULT_TRACE_ENABLE\n"
        if marker not in text:
            raise SystemExit(f"cannot add retained HardFault hook to {source}")
        text = text.replace(marker, marker + weak_hook + "\n", 1)
    if platform == "gr5515":
        function = "void hardfault_trace_handler(uint32_t sp)\n{\n"
        guarded = function + "    inktag_gr_health_fault_capture(sp);\n"
    else:
        function = "void hardfault_trace_handler(unsigned int *args)\n{\n"
        guarded = function + "    inktag_gr_health_fault_capture((uint32_t)args);\n"
    if guarded not in text:
        if function not in text:
            raise SystemExit(f"cannot guard Goodix HardFault handler in {source}")
        text = text.replace(function, guarded, 1)
    return write_if_changed(source, text)


def patch_gr551x_dwt_delay_resume(sdk: Path, platform: str):
    """Keep a halted GR551x from hanging if probe detach disables DWT."""
    if platform != "gr5515":
        return False

    header = sdk / "drivers/inc/hal/gr55xx_delay.h"
    text = header.read_text(encoding="utf-8")
    old_wait = """    // Wait time end
    while ((DWT->CYCCNT - cyccnt_initial) < cycles)
    {}
"""
    guarded_wait = """    // Wait time end. A debugger may disable the DWT while detaching after
    // a halt. Re-enable the counter in the loop so execution can resume instead
    // of remaining trapped here until the next hardware reset.
    while ((DWT->CYCCNT - cyccnt_initial) < cycles)
    {
        if (((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0u) ||
            ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u))
        {
            CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
            DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        }
    }
"""
    if old_wait in text:
        text = text.replace(old_wait, guarded_wait, 1)
    elif guarded_wait not in text:
        raise SystemExit(f"cannot guard GR551x DWT delay loop in {header}")
    return write_if_changed(header, text)


def patch_gr551x_app_timer_snapshot(sdk: Path, platform: str):
    """Expose the SDK's existing AON timeline without stopping/rearming it.

    Use exactly the same remaining-time algebra/lock as app_timer_start/stop.
    This accessor must never mutate the queue, total, timer, or radio state.
    """
    if platform != "gr5515":
        return False
    source = sdk / "components/libraries/app_timer/app_timer.c"
    text = source.read_text(encoding="utf-8")
    marker = "/* InkTag UTC-only snapshot. No extra timer, wake, or SDK clock mutation. */"
    required = ("low_level_timer_rest_get_in_interrupt", "APP_TIMER_LOCK()",
                "apptimer_total_us", "p_curr_timer_node", "next_shot_time")
    if not all(token in text for token in required):
        raise SystemExit(f"unknown Goodix app_timer timeline in {source}")
    snapshot = """

/* InkTag UTC-only snapshot. No extra timer, wake, or SDK clock mutation. */
#if defined(INKTAG_GOODIX_GR5515)
uint64_t inktag_gr_app_timer_now_us(void)
{
    uint64_t now_us;
    APP_TIMER_LOCK();
    now_us = s_app_timer_info.apptimer_total_us;
    if (s_app_timer_info.p_curr_timer_node != NULL)
    {
        uint64_t rest_us = APP_TIMER_IS_IN_INTERRUPT()
            ? low_level_timer_rest_get_in_interrupt()
            : low_level_timer_rest_get();
        now_us = s_app_timer_info.p_curr_timer_node->next_shot_time - rest_us;
    }
    APP_TIMER_UNLOCK();
    return now_us;
}
#endif
"""
    if "uint64_t inktag_gr_app_timer_now_us(void)" in text:
        if marker not in text:
            raise SystemExit(f"unknown Goodix UTC snapshot in {source}")
        old = snapshot.replace("#if defined(INKTAG_GOODIX_GR5515)\n", "").replace("#endif\n", "")
        tail = text[text.index(marker):].strip()
        if tail not in (snapshot.strip(), old.strip()):
            raise SystemExit(f"unrecognized content after Goodix UTC snapshot in {source}")
        text = text[:text.index(marker)].rstrip() + "\n"
    text += snapshot
    return write_if_changed(source, text)


def patch_gr551x_app_timer_epoch(sdk: Path, platform: str):
    """Keep the raw SDK epoch when the minute timer is the last queue node.

    The read-only snapshot remains read-only. Only the start API's empty-queue
    reset changes, for the standard Pro App's minute feature. The existing
    stop API already captures the last timer's elapsed time under the SDK lock.
    Reusing that base leaves relative start/IRQ/queue arithmetic unchanged.
    """
    if platform != 'gr5515':
        return False
    source = sdk / 'components/libraries/app_timer/app_timer.c'
    text = source.read_text(encoding='utf8')
    prefix = '''/* InkTag minute timer: preserve the existing empty-queue epoch. */
#if defined(INKTAG_GOODIX_OTA) && defined(INKTAG_GOODIX_GR5515) && INKTAG_BUILD_PRO && !defined(INKTAG_GR5513_INTERNAL_IMAGES)
#include "inktag_gr_advertising_pro.h"
#endif
#ifndef INKTAG_GR_MINUTE_IDLE_ENABLED
#define INKTAG_GR_MINUTE_IDLE_ENABLED 0
#endif
#line 1
'''
    marker = '/* InkTag minute timer: preserve the existing empty-queue epoch. */'
    if text.startswith(prefix):
        text = text[len(prefix):]
    elif marker in text:
        raise SystemExit(f'unknown Goodix minute epoch prefix in {source}')
    old = '''            // the node that will be started is the first timer node
            s_app_timer_info.apptimer_total_us = 0;'''
    new = '''            // the node that will be started is the first timer node
            if (!INKTAG_GR_MINUTE_IDLE_ENABLED) s_app_timer_info.apptimer_total_us = 0;'''
    if old in text:
        if text.count(old) != 1:
            raise SystemExit(f'ambiguous Goodix empty timer queue in {source}')
        text = text.replace(old, new, 1)
    elif text.count(new) != 1:
        raise SystemExit(f'unknown Goodix empty timer queue in {source}')
    return write_if_changed(source, prefix + text)


def patch_gr5331_checkpoint_aon_snapshot(sdk: Path, platform: str):
    """Read the existing sleep-timer timeline; never wake BLE or rearm a timer.

    GR533x rebases its queue after 36 hours. A shadow epoch preserves that
    rebase without changing any SDK deadline. App-only guards keep Recovery
    bytes unchanged. The ordinary clock timer keeps the queue alive.
    """
    if platform != "gr5331":
        return False
    source = sdk / "components/libraries/app_timer/app_timer.c"
    text = source.read_text(encoding="utf-8")
    guard = "#if defined(INKTAG_GOODIX_OTA) && defined(INKTAG_GOODIX_GR5331)"
    declaration_anchor = "static void            sleep_timer_handler_register(void);"
    rebase_anchor = "        s_app_timer_info.apptimer_total_us = 0;\n    }\n\n    if (p_curr_node->timer_node_mode"
    declaration = guard + "\nstatic uint64_t s_inktag_checkpoint_epoch_us;\n#endif\n\n"
    rebase = (guard + "\n"
              "        s_inktag_checkpoint_epoch_us += s_app_timer_info.apptimer_total_us;\n"
              "#endif\n")
    marker = "/* InkTag GR5331 AON checkpoint snapshot. App-only, no BLE wake. */"
    snapshot = "\n\n" + marker + "\n" + guard + "\n" + """uint32_t inktag_gr_checkpoint_aon_now_ms(void)
{
    uint64_t now_us;
    APP_TIMER_LOCK();
    now_us = s_app_timer_info.apptimer_total_us;
    if (s_app_timer_info.p_curr_timer_node != NULL)
        now_us = s_app_timer_info.p_curr_timer_node->next_shot_time - low_level_timer_rest_get();
    now_us += s_inktag_checkpoint_epoch_us;
    APP_TIMER_UNLOCK();
    return (uint32_t)(now_us / 1000u);
}
#endif
"""
    if marker in text:
        if not text.endswith(snapshot) or declaration not in text or rebase not in text:
            raise SystemExit(f"unrecognized Goodix GR5331 checkpoint snapshot in {source}")
        return False
    required = (declaration_anchor, rebase_anchor, "APP_TIMER_LOCK()",
                "low_level_timer_rest_get(void)", "APP_TIMER_DELAY_US_MAX")
    if not all(token in text for token in required):
        raise SystemExit(f"unknown Goodix GR5331 app_timer timeline in {source}")
    text = text.replace(declaration_anchor, declaration + declaration_anchor, 1)
    text = text.replace(rebase_anchor, rebase + rebase_anchor, 1)
    return write_if_changed(source, text.rstrip() + snapshot)


def patch_goodix_flash_mutation_guard(sdk: Path, platform: str):
    """Put the product voltage gate at the SDK's real erase/write boundary."""
    source = sdk / "components/libraries/hal_flash/hal_flash.c"
    text = source.read_text(encoding="utf-8")
    weak_hook = """
__WEAK bool inktag_gr_flash_mutation_allowed(uint32_t address, uint32_t size)
{
    (void)address;
    (void)size;
    return true;
}
"""
    include_anchor = "#include <string.h>\n"
    if "__WEAK bool inktag_gr_flash_mutation_allowed" not in text:
        if include_anchor not in text:
            raise SystemExit(f"cannot add Goodix Flash mutation hook to {source}")
        text = text.replace(include_anchor, include_anchor + weak_hook, 1)
    if platform == "gr5515" and "extern hal_status_t hal_exflash_init_ext" not in text:
        marker = "extern uint32_t sys_security_enable_status_check(void);\n"
        declaration = (
            "extern hal_status_t hal_exflash_init_ext("
            "exflash_handle_t *p_exflash);\n"
        )
        if marker not in text:
            raise SystemExit(f"cannot declare GR551x extended Flash init in {source}")
        text = text.replace(marker, marker + declaration, 1)

    write_body = (
        "uint32_t hal_flash_write(const uint32_t addr, const uint8_t *buf, const uint32_t size)\n"
        "{\n"
    )
    guarded_write_body = write_body + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return 0;\n"
    nvds_write_body = write_body + "    if (!inktag_gr_nvds_mutation_allowed(addr, size, false)) return 0;\n" + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return 0;\n"
    if guarded_write_body not in text and nvds_write_body not in text:
        if write_body not in text:
            raise SystemExit(f"cannot guard Goodix hal_flash_write in {source}")
        text = text.replace(write_body, guarded_write_body, 1)

    write_r_body = (
        "uint32_t hal_flash_write_r(const uint32_t addr, const uint8_t *buf, const uint32_t size)\n"
        "{\n"
    )
    guarded_write_r_body = write_r_body + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return 0;\n"
    nvds_write_r_body = write_r_body + "    if (!inktag_gr_nvds_mutation_allowed(addr, size, false)) return 0;\n" + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return 0;\n"
    if guarded_write_r_body not in text and nvds_write_r_body not in text:
        if write_r_body not in text:
            raise SystemExit(f"cannot guard Goodix hal_flash_write_r in {source}")
        text = text.replace(write_r_body, guarded_write_r_body, 1)

    erase_body = (
        "bool hal_flash_erase(const uint32_t addr, const uint32_t size)\n"
        "{\n"
    )
    guarded_erase_body = erase_body + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return false;\n"
    nvds_erase_body = erase_body + "    if (!inktag_gr_nvds_mutation_allowed(addr, size, true)) return false;\n" + "    if (!inktag_gr_flash_mutation_allowed(addr, size)) return false;\n"
    if guarded_erase_body not in text and nvds_erase_body not in text:
        if erase_body not in text:
            raise SystemExit(f"cannot guard Goodix hal_flash_erase in {source}")
        text = text.replace(erase_body, guarded_erase_body, 1)

    return write_if_changed(source, text)

def patch_goodix_dfu_reset_checkpoint(sdk: Path, platform: str):
    """Add InkTag checkpoints around the vendor DFU commit and reset."""
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    cold_boot_include = """#include "dfu_port.h"
#if defined(INKTAG_GOODIX_GR5331)
#include "gr533x_pwr.h"
#endif
"""
    if '#include "gr533x_pwr.h"' not in text:
        marker = '#include "dfu_port.h"\n'
        if marker not in text:
            raise SystemExit(f"cannot include GR5331 cold-boot API in {source}")
        text = text.replace(marker, cold_boot_include, 1)
    weak_hook = """\n__WEAK void inktag_dfu_before_reset(void)\n{\n}\n"""
    if "__WEAK void inktag_dfu_before_reset(void)" not in text:
        marker = '#include "dfu_port.h"\n'
        if marker not in text:
            raise SystemExit(f"cannot add Goodix DFU reset checkpoint hook to {source}")
        text = text.replace(marker, marker + weak_hook, 1)
    old_commit_hook = """\n__WEAK bool inktag_dfu_before_commit(void)\n{\n    return true;\n}\n"""
    commit_hook = """\n__WEAK bool inktag_dfu_before_commit(const dfu_image_info_t *candidate, bool signed_firmware)\n{\n    (void)candidate;\n    (void)signed_firmware;\n    return true;\n}\n"""
    if old_commit_hook in text:
        text = text.replace(old_commit_hook, commit_hook, 1)
    elif "__WEAK bool inktag_dfu_before_commit(const dfu_image_info_t *candidate, bool signed_firmware)" not in text:
        reset_hook = "__WEAK void inktag_dfu_before_reset(void)\n{\n}\n"
        if reset_hook not in text:
            raise SystemExit(f"cannot add Goodix DFU commit checkpoint hook to {source}")
        text = text.replace(reset_hook, reset_hook + commit_hook, 1)

    program_start_hook = """
__WEAK bool inktag_dfu_before_program_start(const dfu_image_info_t *candidate)
{
    (void)candidate;
    return true;
}
"""
    if "__WEAK bool inktag_dfu_before_program_start(" not in text:
        if commit_hook.strip() not in text:
            raise SystemExit(
                f"cannot add Goodix pre-erase compatibility hook to {source}"
            )
        text = text.replace(commit_hook, commit_hook + program_start_hook, 1)

    programming_hook = """\n__WEAK bool inktag_dfu_programming_allowed(void)\n{\n    return true;\n}\n"""
    if "__WEAK bool inktag_dfu_programming_allowed(void)" not in text:
        commit_hook_body = "__WEAK bool inktag_dfu_before_commit(const dfu_image_info_t *candidate, bool signed_firmware)\n{\n    (void)candidate;\n    (void)signed_firmware;\n    return true;\n}\n"
        if commit_hook_body not in text:
            raise SystemExit(f"cannot add Goodix DFU programming-role hook to {source}")
        text = text.replace(commit_hook_body, commit_hook_body + programming_hook, 1)

    mode_switch_hook = """\n__WEAK bool inktag_dfu_mode_switch_allowed(void)\n{\n    return true;\n}\n"""
    if "__WEAK bool inktag_dfu_mode_switch_allowed(void)" not in text:
        if programming_hook.strip() not in text:
            raise SystemExit(f"cannot add Goodix DFU mode-switch hook to {source}")
        text = text.replace(programming_hook, programming_hook + mode_switch_hook, 1)

    recovery_hooks = """
__WEAK bool inktag_dfu_ota_admission_allowed(void)
{
    return inktag_dfu_programming_allowed();
}

__WEAK bool inktag_dfu_recovery_request_mark(void)
{
    return true;
}

__WEAK bool inktag_dfu_application_mutation_mark(void)
{
    return true;
}

__WEAK bool inktag_dfu_safe_resume_allowed(void)
{
    return false;
}
"""
    if "__WEAK bool inktag_dfu_ota_admission_allowed(void)" not in text:
        if mode_switch_hook.strip() not in text:
            raise SystemExit(f"cannot add Goodix Recovery safety hooks to {source}")
        text = text.replace(mode_switch_hook, mode_switch_hook + recovery_hooks, 1)

    flash_guard_helpers = """
static bool s_inktag_application_mutated_this_transfer;

static bool inktag_dfu_application_mutation_allowed(void)
{
    if (!s_inktag_application_mutated_this_transfer)
    {
        if (!inktag_dfu_ota_admission_allowed() ||
            !inktag_dfu_application_mutation_mark()) return false;
        s_inktag_application_mutated_this_transfer = true;
    }
    return inktag_dfu_programming_allowed();
}

#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
static uint32_t inktag_dfu_flash_write(uint32_t addr, uint8_t *buf,
                                       uint32_t size)
{
    if (!inktag_dfu_programming_allowed()) return UINT32_MAX;
    return hal_flash_write(addr, buf, size) == size ? HAL_OK : UINT32_MAX;
}

static uint32_t inktag_dfu_flash_erase(uint32_t erase_type, uint32_t addr,
                                       uint32_t size)
{
    if (erase_type != EXFLASH_ERASE_SECTOR ||
        !inktag_dfu_programming_allowed()) return UINT32_MAX;
    return hal_flash_erase(addr, size) ? HAL_OK : UINT32_MAX;
}
#else
static uint32_t inktag_dfu_flash_write(const uint32_t addr,
                                       const uint8_t *buf,
                                       const uint32_t size)
{
    if (!inktag_dfu_programming_allowed()) return 0;
    return hal_flash_write(addr, buf, size);
}

static bool inktag_dfu_flash_erase(const uint32_t addr, const uint32_t size)
{
    if (!inktag_dfu_programming_allowed()) return false;
    return hal_flash_erase(addr, size);
}

static bool inktag_dfu_flash_erase_chip(void)
{
    return inktag_dfu_programming_allowed() && hal_flash_erase_chip();
}
#endif

static uint32_t inktag_dfu_metadata_write(const uint32_t addr,
                                          const uint8_t *buf,
                                          const uint32_t size)
{
    if (!inktag_dfu_programming_allowed() &&
        !inktag_dfu_mode_switch_allowed()) return 0;
    return hal_flash_write(addr, buf, size);
}

static bool inktag_dfu_metadata_erase(const uint32_t addr,
                                      const uint32_t size)
{
    return (inktag_dfu_programming_allowed() ||
            inktag_dfu_mode_switch_allowed()) && hal_flash_erase(addr, size);
}

static bool inktag_dfu_app_write(const uint32_t addr, const uint8_t *buf,
                                 const uint32_t size)
{
    if (!inktag_dfu_application_mutation_allowed()) return false;
#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    return inktag_dfu_flash_write(addr, (uint8_t *)buf, size) == HAL_OK;
#else
    return inktag_dfu_flash_write(addr, buf, size) == size;
#endif
}

static bool inktag_dfu_app_erase(const uint32_t addr, const uint32_t size)
{
    if (!inktag_dfu_application_mutation_allowed()) return false;
#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    return inktag_dfu_flash_erase(EXFLASH_ERASE_SECTOR, addr, size) == HAL_OK;
#else
    return inktag_dfu_flash_erase(addr, size);
#endif
}
"""
    marker = "extern uint16_t get_conn_hdl_by_idx(uint16_t conn_idx);\n"
    helpers_start = text.find(marker)
    helpers_end = text.find("#ifdef SOC_GR5526", helpers_start)
    if helpers_start < 0 or helpers_end < 0:
        raise SystemExit(f"cannot add Goodix DFU Flash backend guards to {source}")
    helpers_start += len(marker)
    text = (text[:helpers_start] + flash_guard_helpers +
            text[helpers_end:])

    for vendor, guarded in (
        (".dfu_flash_write        = hal_exflash_write,",
         ".dfu_flash_write        = inktag_dfu_flash_write,"),
        (".dfu_flash_write        = dfu_exflash_write,",
         ".dfu_flash_write        = inktag_dfu_flash_write,"),
        (".dfu_flash_write        = hal_flash_write,",
         ".dfu_flash_write        = inktag_dfu_flash_write,"),
        (".dfu_flash_erase        = hal_exflash_erase,",
         ".dfu_flash_erase        = inktag_dfu_flash_erase,"),
        (".dfu_flash_erase        = dfu_exflash_erase,",
         ".dfu_flash_erase        = inktag_dfu_flash_erase,"),
        (".dfu_flash_erase        = hal_flash_erase,",
         ".dfu_flash_erase        = inktag_dfu_flash_erase,"),
        (".dfu_flash_erase_chip   = hal_flash_erase_chip,",
         ".dfu_flash_erase_chip   = inktag_dfu_flash_erase_chip,"),
    ):
        text = text.replace(vendor, guarded)

    single_slot_marker = "InkTag single-slot DFU omits legacy FAST_DFU"
    if single_slot_marker in text:
        epoch_gate = """    if (!inktag_dfu_before_program_start(&s_now_img_info))
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u,
            s_now_img_info.version);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

"""
        signed_ready = "    s_inktag_signed_firmware = true;\n"
        start_at = text.find(
            "static void program_start_replace(dfu_receive_frame_t *p_frame)\n{"
        )
        start_end = text.find("\n\nstatic void program_flash_replace", start_at)
        if start_at < 0 or start_end < 0:
            raise SystemExit(f"cannot locate Goodix single-slot PROGRAM_START in {source}")
        start_handler = text[start_at:start_end]
        epoch_call = "inktag_dfu_before_program_start(&s_now_img_info)"
        if start_handler.count(epoch_call) > 1 and epoch_gate in start_handler:
            # Safe-resume decorates existing rejection exits.  A later
            # materialization must recognize that decorated gate instead of
            # inserting an undecorated duplicate before the signed state.
            start_handler = start_handler.replace(epoch_gate, "", 1)
            text = text[:start_at] + start_handler + text[start_end:]
        elif epoch_call not in start_handler:
            if signed_ready not in text:
                raise SystemExit(
                    f"cannot add Goodix pre-erase epoch gate to {source}"
                )
            text = text.replace(signed_ready, epoch_gate + signed_ready, 1)
        return write_if_changed(source, text)

    programming_guard = """    if (!inktag_dfu_programming_allowed())
    {
        p_frame->data[0] = DFU_ACK_ERROR;
        dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
        return;
    }

"""
    start_marker = "static void program_start_replace(dfu_receive_frame_t *p_frame)\n{\n"
    if "INKTAG_OTA_REJECT_STAGE_PROGRAM_START" in text:
        text = text.replace(programming_guard, "", 1)
        legacy_validation_reject = """        {
            p_frame->data[0] = DFU_ACK_ERROR;
            dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
            return;
        }
"""
        diagnostic_validation_reject = """        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data,
                INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                s_now_img_info.boot_info.load_addr);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
"""
        text = text.replace(legacy_validation_reject,
                            diagnostic_validation_reject, 1)
    elif programming_guard not in text:
        if start_marker not in text:
            raise SystemExit(f"cannot guard Goodix DFU programming role in {source}")
        text = text.replace(start_marker, start_marker + programming_guard, 1)

    fast_guard = """    if (!inktag_dfu_programming_allowed())
    {
        ring_buffer_clean(&s_ble_rx_ring_buffer);
        fast_dfu_state_machine_reset();
        s_p_cmd_buffer[0] = DFU_ACK_ERROR;
        dfu_send_frame(s_p_cmd_buffer, 1, 0xFF);
        return;
    }

"""
    fast_marker = "static void fast_dfu_program_schedule(void)\n{\n"
    if single_slot_marker in text:
        pass
    elif fast_guard not in text:
        if fast_marker not in text:
            raise SystemExit(f"cannot guard buffered Goodix DFU writes in {source}")
        text = text.replace(fast_marker, fast_marker + fast_guard, 1)

    erase_loop = """        for (int i = 0; i < 20; i++)
        {
"""
    erase_guard = erase_loop + """            if (!inktag_dfu_programming_allowed())
            {
                has_error = true;
                break;
            }
"""
    if single_slot_marker in text:
        pass
    elif erase_guard not in text:
        erase_function = "static void fast_dfu_erase_flash(void)\n{\n"
        erase_start = text.find(erase_function)
        erase_end = text.find("\n}\n", erase_start + len(erase_function))
        if erase_start < 0 or erase_end < 0:
            raise SystemExit(f"cannot find buffered Goodix DFU erase function in {source}")
        erase_body = text[erase_start:erase_end]
        if erase_loop not in erase_body:
            raise SystemExit(f"cannot guard buffered Goodix DFU erases in {source}")
        guarded_body = erase_body.replace(erase_loop, erase_guard, 1)
        text = text[:erase_start] + guarded_body + text[erase_end:]

    signed_state = "static bool s_inktag_signed_firmware = false;\n"
    if signed_state not in text:
        start_marker = "static void program_start_replace(dfu_receive_frame_t *p_frame)\n"
        if start_marker not in text:
            raise SystemExit(f"cannot track Goodix signed firmware type in {source}")
        text = text.replace(start_marker, signed_state + "\n" + start_marker, 1)

    firmware_type_marker = "    uint8_t firmware_type = (p_frame->data[0] & 0xF0) >> 4;\n"
    assignment_line = "    s_inktag_signed_firmware = (firmware_type == SIGN_FIRMWARE);\n"
    signed_assignment = firmware_type_marker + assignment_line
    text = text.replace(assignment_line, "")
    if firmware_type_marker not in text:
        raise SystemExit(f"cannot capture Goodix firmware type in {source}")
    # GR533x and GR551x place firmware_type in opposite preprocessor branches.
    # Patch every declaration so the commit hook receives the signed bit on
    # either family instead of silently reporting false on GR533x.
    text = text.replace(firmware_type_marker, signed_assignment)

    gr551x_unsigned_size = """        if (dfu_security_check_enable() == true)//security mode
        {
            s_file_size = s_now_img_info.boot_info.bin_size + DFU_IMAGE_INFO_LEN + DFU_SIGN_LEN;
        }
        else
        {
            s_file_size = s_now_img_info.boot_info.bin_size + DFU_IMAGE_INFO_LEN;
        }
"""
    gr551x_signed_size = """        if (dfu_security_check_enable() == true || firmware_type == SIGN_FIRMWARE)
        {
            s_file_size = s_now_img_info.boot_info.bin_size + DFU_IMAGE_INFO_LEN + DFU_SIGN_LEN;
        }
        else
        {
            s_file_size = s_now_img_info.boot_info.bin_size + DFU_IMAGE_INFO_LEN;
        }
"""
    if platform == "gr5515":
        if gr551x_unsigned_size in text:
            text = text.replace(gr551x_unsigned_size, gr551x_signed_size, 1)
        elif gr551x_signed_size not in text:
            raise SystemExit(f"cannot enable normal-mode GR551x signed transfer in {source}")

    old_commit_guard = """    if (check_result && !inktag_dfu_before_commit())
    {
        check_result = false;
    }

"""
    commit_guard = """    if (check_result && !inktag_dfu_before_commit(&s_now_img_info, s_inktag_signed_firmware))
    {
        check_result = false;
    }

"""
    if old_commit_guard in text:
        text = text.replace(old_commit_guard, commit_guard, 1)
    response_marker = "    if (check_result)\n    {\n        p_frame->data[0] = 0x01;\n"
    if commit_guard not in text:
        if response_marker not in text:
            raise SystemExit(f"cannot guard Goodix DFU commit in {source}")
        text = text.replace(response_marker, commit_guard + response_marker, 1)

    final_power_guard = """    if (check_result && !inktag_dfu_programming_allowed())
    {
        check_result = false;
    }

"""
    if final_power_guard not in text:
        if commit_guard not in text:
            raise SystemExit(f"cannot add final Goodix DFU power checkpoint in {source}")
        text = text.replace(commit_guard, commit_guard + final_power_guard, 1)

    program_end_start = text.find(
        "static void program_end_replace(dfu_receive_frame_t *p_frame)\n"
    )
    program_end_end = text.find("\n#ifdef ENABLE_DFU_CUSTOM_BUFFER", program_end_start)
    if program_end_start < 0 or program_end_end < 0:
        raise SystemExit(f"cannot guard Goodix DFU metadata writes in {source}")
    program_end = text[program_end_start:program_end_end]
    guarded_program_end = program_end.replace(
        "inktag_dfu_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
        "inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
    ).replace(
        "inktag_dfu_flash_erase(APP_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
        "inktag_dfu_metadata_erase(APP_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
    ).replace(
        "inktag_dfu_flash_write(APP_INFO_START_ADDR,",
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR,",
    ).replace(
        "inktag_dfu_flash_write(DFU_INFO_START_ADDR,",
        "inktag_dfu_metadata_write(DFU_INFO_START_ADDR,",
    ).replace(
        "hal_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
        "inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
    ).replace(
        "hal_flash_erase(APP_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
        "inktag_dfu_metadata_erase(APP_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE)",
    ).replace(
        "hal_flash_write(APP_INFO_START_ADDR,",
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR,",
    ).replace(
        "hal_flash_write(DFU_INFO_START_ADDR,",
        "inktag_dfu_metadata_write(DFU_INFO_START_ADDR,",
    )
    if "inktag_dfu_metadata_erase(DFU_INFO_START_ADDR" not in guarded_program_end:
        raise SystemExit(f"cannot route Goodix DFU metadata erase through guard in {source}")
    if "inktag_dfu_metadata_write(" not in guarded_program_end:
        raise SystemExit(f"cannot route Goodix DFU metadata write through guard in {source}")
    text = text[:program_end_start] + guarded_program_end + text[program_end_end:]

    # GR5331 retains the BLE SDK callback table across a warm software reset.
    # Crossing from the application image to fixed Recovery (or back) must use
    # the SDK's cold-boot path so the new image rebuilds BSS and callback state.
    mode_reset_vendor = """        security_disable();
        hal_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        hal_flash_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();

        hal_nvic_system_reset();
"""
    mode_reset_old = """        security_disable();
        hal_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        hal_flash_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();

        inktag_dfu_before_reset();
        hal_nvic_system_reset();
"""
    mode_reset_irq_only = """        inktag_dfu_before_reset();

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        hal_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        hal_flash_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    mode_reset_legacy = """        inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    mode_reset_guarded_preflag = """        inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        bool inktag_request_ok =
            inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE) &&
            inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info)) == sizeof(s_dfu_info) &&
            inktag_dfu_recovery_request_mark();
        security_state_recovery();
        if (!inktag_request_ok)
        {
            __set_PRIMASK(inktag_reset_primask);
            return;
        }
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    mode_reset_new = """        inktag_dfu_before_reset();

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        bool inktag_request_ok =
            inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE) &&
            inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info)) == sizeof(s_dfu_info) &&
            inktag_dfu_recovery_request_mark();
        security_state_recovery();
        if (!inktag_request_ok)
        {
            __set_PRIMASK(inktag_reset_primask);
            return;
        }
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    mode_reset_direct_new = """        inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        hal_flash_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        hal_flash_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    mode_switch_guard = """        if (!inktag_dfu_mode_switch_allowed())
        {
            p_frame->data[0] = DFU_ACK_ERROR;
            dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
            return;
        }

"""
    guarded_mode_reset_new = mode_switch_guard + mode_reset_new
    if "INKTAG_OTA_REJECT_STAGE_MODE_SWITCH" in text:
        text = text.replace(mode_switch_guard, "", 1)
    elif mode_reset_vendor in text:
        text = text.replace(mode_reset_vendor, guarded_mode_reset_new, 1)
    elif mode_reset_old in text:
        text = text.replace(mode_reset_old, guarded_mode_reset_new, 1)
    elif mode_reset_irq_only in text:
        text = text.replace(mode_reset_irq_only, guarded_mode_reset_new, 1)
    elif guarded_mode_reset_new in text:
        pass
    elif mode_switch_guard + mode_reset_legacy in text:
        text = text.replace(mode_switch_guard + mode_reset_legacy,
                            guarded_mode_reset_new, 1)
    elif mode_reset_legacy in text:
        text = text.replace(mode_reset_legacy, guarded_mode_reset_new, 1)
    elif mode_reset_new in text:
        text = text.replace(mode_reset_new, guarded_mode_reset_new, 1)
    elif mode_switch_guard + mode_reset_guarded_preflag in text:
        text = text.replace(mode_switch_guard + mode_reset_guarded_preflag,
                            guarded_mode_reset_new, 1)
    elif mode_reset_guarded_preflag in text:
        text = text.replace(mode_reset_guarded_preflag,
                            guarded_mode_reset_new, 1)
    elif mode_switch_guard + mode_reset_direct_new in text:
        text = text.replace(mode_switch_guard + mode_reset_direct_new,
                            guarded_mode_reset_new, 1)
    elif mode_reset_direct_new in text:
        text = text.replace(mode_reset_direct_new, guarded_mode_reset_new, 1)
    else:
        raise SystemExit(f"cannot guard Goodix DFU mode-switch Flash mutation in {source}")

    lines = text.splitlines(keepends=True)
    reset_count = 0
    changed = False
    for index, line in enumerate(lines):
        if line.strip() != "hal_nvic_system_reset();":
            continue
        reset_count += 1
        reset_context = "".join(lines[max(0, index - 12):index])
        if (
            "uint32_t inktag_reset_primask = __get_PRIMASK();" in reset_context
            or "pwr_mgmt_set_wakeup_flag(COLD_BOOT);" in reset_context
        ):
            continue
        indent = line[:len(line) - len(line.lstrip())]
        checkpoint = ""
        if index == 0 or lines[index - 1].strip() != "inktag_dfu_before_reset();":
            checkpoint = f"{indent}inktag_dfu_before_reset();\n"
        lines[index] = (
            checkpoint
            + "#if defined(INKTAG_GOODIX_GR5331)\n"
            + f"{indent}pwr_mgmt_set_wakeup_flag(COLD_BOOT);\n"
            + "#endif\n"
            + line
        )
        changed = True
    if reset_count == 0:
        raise SystemExit(f"cannot checkpoint Goodix DFU resets in {source}")
    if changed:
        text = "".join(lines)

    return write_if_changed(source, text)

def patch_goodix_dfu_rejection_diagnostics(sdk: Path):
    """Return actionable InkTag rejection tails from the vendor DFU handlers."""
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    if ("static uint8_t inktag_dfu_build_rejection(" in text and
            "static void program_flash_replace(dfu_receive_frame_t *p_frame)" in text and
            "#ifdef SOC_GR5526\nstatic void program_flash_replace" not in text and
            text.count("dfu_set_cmd_handler(0x07, 0X24, program_flash_replace);") >= 2 and
            text.count("dfu_set_cmd_handler(0x09, 0X24, program_flash_replace);") >= 2):
        return False
    if ("static uint8_t inktag_dfu_build_rejection(" in text and
            "#ifdef SOC_GR5526\nstatic void program_flash_replace" not in text):
        text = text.replace(
            "dfu_set_cmd_handler(0x06, 0X23, program_start_replace);\n"
            "    dfu_set_cmd_handler(0x08, 0x25, program_end_replace);",
            "dfu_set_cmd_handler(0x06, 0X23, program_start_replace);\n"
            "    dfu_set_cmd_handler(0x07, 0X24, program_flash_replace);\n"
            "    dfu_set_cmd_handler(0x08, 0x25, program_end_replace);",
        )
        text = text.replace(
            "dfu_set_cmd_handler(0x08, 0X23, program_start_replace);\n"
            "    dfu_set_cmd_handler(0x0A, 0x25, program_end_replace);",
            "dfu_set_cmd_handler(0x08, 0X23, program_start_replace);\n"
            "    dfu_set_cmd_handler(0x09, 0X24, program_flash_replace);\n"
            "    dfu_set_cmd_handler(0x0A, 0x25, program_end_replace);",
        )
        return write_if_changed(source, text)

    include_marker = '#include "dfu_port.h"\n'
    include_new = include_marker + '#include "inktag_ota_reject.h"\n'
    if include_new in text:
        pass
    elif include_marker not in text:
        raise SystemExit(f"cannot include InkTag OTA rejection ABI in {source}")
    else:
        text = text.replace(include_marker, include_new, 1)

    mode_hook = """__WEAK bool inktag_dfu_mode_switch_allowed(void)
{
    return true;
}
"""
    diagnostic_helpers = mode_hook + """

__WEAK bool inktag_dfu_rejection_info(inktag_ota_reject_stage_t stage,
                                      uint8_t detail_flags,
                                      uint32_t detail,
                                      inktag_ota_reject_info_t *info)
{
    (void)stage;
    (void)detail_flags;
    (void)detail;
    (void)info;
    return false;
}
"""
    if "__WEAK bool inktag_dfu_rejection_info(" in text:
        pass
    elif mode_hook not in text:
        raise SystemExit(f"cannot add Goodix DFU rejection hook to {source}")
    else:
        text = text.replace(mode_hook, diagnostic_helpers, 1)

    ack_enum = """enum
{
    DFU_ACK_SUCCESS = 0x01,
    DFU_ACK_ERROR,
};
"""
    rejection_builder = ack_enum + """

static uint8_t inktag_dfu_build_rejection(
    uint8_t *response, inktag_ota_reject_reason_t fallback_reason,
    inktag_ota_reject_stage_t stage, uint8_t detail_flags, uint32_t detail)
{
    inktag_ota_reject_info_t info;
    memset(&info, 0, sizeof(info));
    info.reason = fallback_reason;
    info.stage = stage;
    info.flags = detail_flags;
    info.detail = detail;
    (void)inktag_dfu_rejection_info(stage, detail_flags, detail, &info);
    uint8_t length = inktag_ota_reject_encode(
        response, INKTAG_OTA_REJECT_EXTENSION_WIRE_SIZE,
        DFU_ACK_ERROR, &info);
    if (length == 0u)
    {
        response[0] = DFU_ACK_ERROR;
        return 1u;
    }
    return length;
}
"""
    if "static uint8_t inktag_dfu_build_rejection(" in text:
        pass
    elif ack_enum not in text:
        raise SystemExit(f"cannot add Goodix DFU rejection encoder in {source}")
    else:
        text = text.replace(ack_enum, rejection_builder, 1)

    state_marker = "static dfu_image_info_t     s_now_img_info;\n"
    state_new = state_marker + """static bool                 s_inktag_normal_transfer_active = false;
static uint32_t             s_inktag_transfer_base = 0u;
static uint32_t             s_inktag_transfer_end = 0u;
static uint32_t             s_inktag_transfer_next = 0u;
static uint32_t             s_inktag_erased_through = 0u;
"""
    if "s_inktag_normal_transfer_active" in text:
        pass
    elif state_marker not in text:
        raise SystemExit(f"cannot add Goodix DFU diagnostic transfer state to {source}")
    else:
        text = text.replace(state_marker, state_new, 1)

    old_guard = """    if (!inktag_dfu_programming_allowed())
    {
        p_frame->data[0] = DFU_ACK_ERROR;
        dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
        return;
    }

"""
    new_guard = """    s_inktag_normal_transfer_active = false;
    if (!inktag_dfu_programming_allowed())
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }
    if (p_frame->data_len < 1u)
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

"""
    start_at = text.find("static void program_start_replace(dfu_receive_frame_t *p_frame)\n{")
    end_at = text.find("\n\n#ifdef SOC_GR5526\nstatic void program_flash_replace", start_at)
    if start_at < 0 or end_at < 0:
        raise SystemExit(f"cannot find Goodix normal DFU start handler in {source}")
    start_body = text[start_at:end_at]
    if new_guard in start_body:
        pass
    elif old_guard not in start_body:
        raise SystemExit(f"cannot extend Goodix PROGRAM_START power rejection in {source}")
    else:
        start_body = start_body.replace(old_guard, new_guard, 1)

    validation_marker = """#endif

    bool    erase_state = false;
"""
    validation_new = """#endif

    if (dfu_type == DFU_FLASH_INNER)
    {
        if (p_frame->data_len != (INKTAG_GR_IMAGE_INFO_LEN + 1u))
        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data, INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
        dfu_image_info_t candidate;
        memcpy(&candidate, &p_frame->data[1], sizeof(candidate));
        uint32_t layout_marker;
        uint32_t layout_inverse;
        memcpy(&layout_marker,
               &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN],
               sizeof(layout_marker));
        memcpy(&layout_inverse,
               &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN + 4u],
               sizeof(layout_inverse));
        if (candidate.pattern != DFU_IMG_INFO_PATTERN ||
            candidate.boot_info.bin_size == 0u ||
            layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC ||
            layout_inverse != INKTAG_GR_LAYOUT_MARKER_INVERSE)
        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data, INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
        if (candidate.boot_info.load_addr != s_dfu_info.dfu_fw_save_addr ||
            (candidate.boot_info.load_addr % DFU_FLASH_SECTOR_SIZE) != 0u ||
            check_system_info_address(candidate.boot_info.load_addr))
        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data,
                INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                candidate.boot_info.load_addr);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
    }

    bool    erase_state = false;
"""
    if "        dfu_image_info_t candidate;" in start_body:
        pass
    elif validation_marker not in start_body:
        raise SystemExit(f"cannot validate Goodix PROGRAM_START image in {source}")
    else:
        start_body = start_body.replace(validation_marker, validation_new, 1)

    legacy_validation_reject = """        {
            p_frame->data[0] = DFU_ACK_ERROR;
            dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
            return;
        }
"""
    diagnostic_validation_reject = """        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data,
                INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS,
                s_now_img_info.boot_info.load_addr);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
"""
    start_body = start_body.replace(legacy_validation_reject,
                                    diagnostic_validation_reject, 1)

    configure_marker = """    if (dfu_type == DFU_FLASH_INNER)
    {
        dfu_flash_type_set(DFU_FLASH_INNER);
"""
    configure_new = """    if (dfu_type == DFU_FLASH_INNER)
    {
        uint32_t load_addr = s_now_img_info.boot_info.load_addr;
        if (s_file_size < s_now_img_info.boot_info.bin_size ||
            load_addr > (UINT32_MAX - s_file_size) ||
            check_system_info_address(load_addr + s_file_size - 1u))
        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data,
                INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, load_addr);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
        s_inktag_transfer_base = load_addr;
        s_inktag_transfer_end = load_addr + s_file_size;
        s_inktag_transfer_next = load_addr;
        s_inktag_erased_through = load_addr + DFU_FLASH_SECTOR_SIZE;
        dfu_flash_type_set(DFU_FLASH_INNER);
"""
    if "        s_inktag_transfer_base = load_addr;" in start_body:
        pass
    elif configure_marker not in start_body:
        raise SystemExit(f"cannot capture Goodix normal DFU range in {source}")
    else:
        start_body = start_body.replace(configure_marker, configure_new, 1)

    response_old = """#if defined(SOC_GR533X) || defined(SOC_GR5405)
    erase_state = dfu_flash_erase((*p_page_start_addr), DFU_FLASH_SECTOR_SIZE);
    if (!erase_state)
#else
    erase_state = dfu_flash_erase(page_start_addr, DFU_FLASH_SECTOR_SIZE);
    if (erase_state)
#endif
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
    }
    else
    {
        p_frame->data[0] = DFU_ACK_ERROR;
    }

#if defined(SOC_GR533X) || defined(SOC_GR5405)
    *p_all_check_sum = 0;
#else
    all_check_sum = 0;
#endif

    dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
"""
    response_new = """#if defined(SOC_GR533X) || defined(SOC_GR5405)
    erase_state = dfu_flash_erase((*p_page_start_addr), DFU_FLASH_SECTOR_SIZE);
    bool erase_ok = !erase_state;
#else
    erase_state = dfu_flash_erase(page_start_addr, DFU_FLASH_SECTOR_SIZE);
    bool erase_ok = erase_state;
#endif
    uint8_t response_len = 1u;
    if (erase_ok)
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
        s_inktag_normal_transfer_active = (dfu_type == DFU_FLASH_INNER);
    }
    else
    {
        uint32_t erase_addr = s_now_img_info.boot_info.load_addr;
        response_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, erase_addr);
    }

#if defined(SOC_GR533X) || defined(SOC_GR5405)
    *p_all_check_sum = 0;
#else
    all_check_sum = 0;
#endif

    dfu_send_frame(p_frame->data, response_len, p_frame->cmd_type);
"""
    response_old_alt = """#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    erase_state = dfu_flash_erase((*p_page_start_addr), DFU_FLASH_SECTOR_SIZE);
    if (!erase_state)
#else
    erase_state = dfu_flash_erase(page_start_addr, DFU_FLASH_SECTOR_SIZE);
    if (erase_state)
#endif
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
    }
    else
    {
        p_frame->data[0] = DFU_ACK_ERROR;
    }

#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    *p_all_check_sum = 0;
#else
    all_check_sum = 0;
#endif

    dfu_program_start(s_file_size);
    dfu_flash_type_set(DFU_FLASH_INNER);
    dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
#if defined(SOC_GR5515) || defined(SOC_GR5X25) || defined(SOC_GR5526)
    cmd_receive_flag = 0;
#endif
"""
    response_new_alt = """#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    erase_state = dfu_flash_erase((*p_page_start_addr), DFU_FLASH_SECTOR_SIZE);
    bool erase_ok = !erase_state;
#else
    erase_state = dfu_flash_erase(page_start_addr, DFU_FLASH_SECTOR_SIZE);
    bool erase_ok = erase_state;
#endif
    uint8_t response_len = 1u;
    if (erase_ok)
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
        s_inktag_normal_transfer_active = (dfu_type == DFU_FLASH_INNER);
    }
    else
    {
        uint32_t erase_addr = s_now_img_info.boot_info.load_addr;
        response_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, erase_addr);
    }

#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    *p_all_check_sum = 0;
#else
    all_check_sum = 0;
#endif

    dfu_program_start(s_file_size);
    dfu_flash_type_set(DFU_FLASH_INNER);
    dfu_send_frame(p_frame->data, response_len, p_frame->cmd_type);
#if defined(SOC_GR5515) || defined(SOC_GR5X25) || defined(SOC_GR5526)
    cmd_receive_flag = 0;
#endif
"""
    if response_old in start_body:
        start_body = start_body.replace(response_old, response_new, 1)
    elif response_old_alt in start_body:
        start_body = start_body.replace(response_old_alt, response_new_alt, 1)
    elif "response_len = inktag_dfu_build_rejection(" in start_body:
        pass
    else:
        raise SystemExit(f"cannot extend Goodix PROGRAM_START erase rejection in {source}")
    text = text[:start_at] + start_body + text[end_at:]

    flash_start = text.find("#ifdef SOC_GR5526\nstatic void program_flash_replace")
    flash_end = text.find("\n#endif\n\nstatic void program_end_replace", flash_start)
    if flash_start < 0 or flash_end < 0:
        raise SystemExit(f"cannot find Goodix PROGRAM_FLASH handler in {source}")
    flash_end += len("\n#endif")
    flash_handler = r'''static void program_flash_replace(dfu_receive_frame_t *p_frame)
{
    uint8_t reject_len = 1u;
    if (p_frame->data_len < 7u)
    {
        reject_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE, 0u, 0u);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    uint8_t program_type = p_frame->data[0] & 0x0fu;
    uint8_t flash_type = (p_frame->data[0] & 0xf0u) >> 4;
    uint32_t addr = ((uint32_t)p_frame->data[4] << 24) |
                    ((uint32_t)p_frame->data[3] << 16) |
                    ((uint32_t)p_frame->data[2] << 8) |
                    (uint32_t)p_frame->data[1];
    uint16_t len = ((uint16_t)p_frame->data[6] << 8) |
                   (uint16_t)p_frame->data[5];
    if (program_type != 0x01u || flash_type != DFU_FLASH_INNER || len == 0u ||
        len > ONCE_WRITE_DATA_LEN || p_frame->data_len != (uint16_t)(len + 7u) ||
        !s_inktag_normal_transfer_active || addr != s_inktag_transfer_next ||
        addr < s_inktag_transfer_base || addr > s_inktag_transfer_end ||
        len > (s_inktag_transfer_end - addr))
    {
        reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, addr);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    dfu_flash_type_set(DFU_FLASH_INNER);
    uint32_t write_addr = addr;
    uint16_t remaining = len;
    uint16_t input_offset = 0u;
    while (remaining != 0u)
    {
        if (write_addr == s_inktag_erased_through)
        {
            security_disable();
            bool erase_state = dfu_flash_erase(write_addr, DFU_FLASH_SECTOR_SIZE);
            security_state_recovery();
#if defined(SOC_GR533X) || defined(SOC_GR5405)
            bool erase_ok = !erase_state;
#else
            bool erase_ok = erase_state;
#endif
            if (!erase_ok)
            {
                reject_len = inktag_dfu_build_rejection(
                    p_frame->data, INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
                    INKTAG_OTA_REJECT_STAGE_ERASE,
                    INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, write_addr);
                s_inktag_normal_transfer_active = false;
                dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
                return;
            }
            s_inktag_erased_through += DFU_FLASH_SECTOR_SIZE;
        }

        uint32_t sector_remaining = s_inktag_erased_through - write_addr;
        uint16_t write_len = remaining;
        if ((uint32_t)write_len > sector_remaining)
        {
            write_len = (uint16_t)sector_remaining;
        }
        security_disable();
        uint32_t write_state = dfu_flash_write(
            write_addr, &p_frame->data[7u + input_offset], write_len);
        (void)dfu_flash_read(write_addr, s_p_fast_cache_buffer, write_len);
        security_state_recovery();
#if defined(SOC_GR533X) || defined(SOC_GR5405)
        bool write_ok = (write_state == HAL_OK);
#else
        bool write_ok = (write_state == write_len);
#endif
        if (!write_ok ||
            memcmp(s_p_fast_cache_buffer, &p_frame->data[7u + input_offset],
                   write_len) != 0)
        {
            reject_len = inktag_dfu_build_rejection(
                p_frame->data, INKTAG_OTA_REJECT_REASON_WRITE_FAILED,
                INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
                INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, write_addr);
            s_inktag_normal_transfer_active = false;
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }

        uint32_t chunk_sum = 0u;
        for (uint16_t index = 0u; index < write_len; ++index)
        {
            chunk_sum += s_p_fast_cache_buffer[index];
        }
#if defined(SOC_GR533X) || defined(SOC_GR5405)
        *p_all_check_sum += chunk_sum;
#else
        all_check_sum += chunk_sum;
#endif
        write_addr += write_len;
        input_offset += write_len;
        remaining -= write_len;
    }

    s_inktag_transfer_next += len;
    dfu_programing(len);
    p_frame->data[0] = DFU_ACK_SUCCESS;
    dfu_send_frame(p_frame->data, 1u, p_frame->cmd_type);
    cmd_receive_flag = 0;
    dfu_flash_type_set(DFU_FLASH_INNER);
}
'''
    text = text[:flash_start] + flash_handler.rstrip() + text[flash_end:]

    end_state_marker = """#endif

    if (check_result && !inktag_dfu_before_commit(&s_now_img_info, s_inktag_signed_firmware))
"""
    end_state_guard = """#endif

    if (!s_fast_dfu_mode &&
        (!s_inktag_normal_transfer_active ||
         s_inktag_transfer_next != s_inktag_transfer_end))
    {
        check_result = false;
    }

    if (check_result && !inktag_dfu_before_commit(&s_now_img_info, s_inktag_signed_firmware))
"""
    if end_state_marker not in text:
        raise SystemExit(f"cannot validate Goodix PROGRAM_END transfer extent in {source}")
    text = text.replace(end_state_marker, end_state_guard, 1)

    response_old = """    if (check_result)
    {
        p_frame->data[0] = 0x01;
    }
    else
    {
        p_frame->data[0] = 0x02;
    }

    p_frame->data[1] = end_flag;

    if (s_fast_dfu_mode)
"""
    response_new = """    uint8_t response_len = 1u;
    if (check_result)
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
        s_inktag_normal_transfer_active = false;
    }
    else if (!s_fast_dfu_mode)
    {
        inktag_ota_reject_reason_t reason =
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED;
        uint8_t detail_flags = 0u;
        uint32_t detail = 0u;
        if (!s_inktag_normal_transfer_active ||
            s_inktag_transfer_next != s_inktag_transfer_end)
        {
            reason = INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH;
            detail_flags = INKTAG_OTA_REJECT_FLAG_DETAIL_IS_OFFSET;
            detail = s_inktag_transfer_next - s_inktag_transfer_base;
        }
        response_len = inktag_dfu_build_rejection(
            p_frame->data, reason, INKTAG_OTA_REJECT_STAGE_PROGRAM_END,
            detail_flags, detail);
        s_inktag_normal_transfer_active = false;
    }
    else
    {
        p_frame->data[0] = DFU_ACK_ERROR;
    }

    if (s_fast_dfu_mode)
"""
    response_old_alt = """    if (check_result)
    {
        p_frame->data[0] = 0x01;
    }
    else
    {
        p_frame->data[0] = 0x02;
    }

    p_frame->data[1] = reset_flag;

    if (s_fast_dfu_mode)
"""
    response_new_alt = """    uint8_t response_len = 1u;
    if (check_result)
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
        s_inktag_normal_transfer_active = false;
    }
    else if (!s_fast_dfu_mode)
    {
        inktag_ota_reject_reason_t reason =
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED;
        uint8_t detail_flags = 0u;
        uint32_t detail = 0u;
        if (!s_inktag_normal_transfer_active ||
            s_inktag_transfer_next != s_inktag_transfer_end)
        {
            reason = INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH;
            detail_flags = INKTAG_OTA_REJECT_FLAG_DETAIL_IS_OFFSET;
            detail = s_inktag_transfer_next - s_inktag_transfer_base;
        }
        response_len = inktag_dfu_build_rejection(
            p_frame->data, reason, INKTAG_OTA_REJECT_STAGE_PROGRAM_END,
            detail_flags, detail);
        s_inktag_normal_transfer_active = false;
    }
    else
    {
        p_frame->data[0] = DFU_ACK_ERROR;
    }

    if (s_fast_dfu_mode)
"""
    if response_old in text:
        text = text.replace(response_old, response_new, 1)
    elif response_old_alt in text:
        text = text.replace(response_old_alt, response_new_alt, 1)
    else:
        raise SystemExit(f"cannot extend Goodix PROGRAM_END rejection in {source}")
    normal_send = """    else
    {
        dfu_send_frame(p_frame->data,1,p_frame->cmd_type);
    }
"""
    normal_send_new = """    else
    {
        dfu_send_frame(p_frame->data, response_len, p_frame->cmd_type);
    }
"""
    program_end_at = text.find("static void program_end_replace")
    program_end_end = text.find("\n#ifdef ENABLE_DFU_CUSTOM_BUFFER", program_end_at)
    program_end = text[program_end_at:program_end_end]
    if normal_send not in program_end:
        raise SystemExit(f"cannot resize Goodix PROGRAM_END rejection in {source}")
    program_end = program_end.replace(normal_send, normal_send_new, 1)
    text = text[:program_end_at] + program_end + text[program_end_end:]

    for old, new in (
        ("dfu_set_cmd_handler(0x06, 0X23, program_start_replace);\n    dfu_set_cmd_handler(0x08, 0x25, program_end_replace);",
         "dfu_set_cmd_handler(0x06, 0X23, program_start_replace);\n    dfu_set_cmd_handler(0x07, 0X24, program_flash_replace);\n    dfu_set_cmd_handler(0x08, 0x25, program_end_replace);"),
        ("dfu_set_cmd_handler(0x08, 0X23, program_start_replace);\n    dfu_set_cmd_handler(0x0A, 0x25, program_end_replace);",
         "dfu_set_cmd_handler(0x08, 0X23, program_start_replace);\n    dfu_set_cmd_handler(0x09, 0X24, program_flash_replace);\n    dfu_set_cmd_handler(0x0A, 0x25, program_end_replace);"),
    ):
        if old not in text:
            raise SystemExit(f"cannot register Goodix PROGRAM_FLASH diagnostics in {source}")
        text = text.replace(old, new)

    old_mode_reject = """        if (!inktag_dfu_mode_switch_allowed())
        {
            p_frame->data[0] = DFU_ACK_ERROR;
            dfu_send_frame(p_frame->data, 1, p_frame->cmd_type);
            return;
        }
"""
    new_mode_reject = """        if (!inktag_dfu_mode_switch_allowed())
        {
            uint8_t reject_len = inktag_dfu_build_rejection(
                p_frame->data, INKTAG_OTA_REJECT_REASON_INTERNAL,
                INKTAG_OTA_REJECT_STAGE_MODE_SWITCH, 0u, 0u);
            dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
            return;
        }
"""
    if old_mode_reject not in text:
        raise SystemExit(f"cannot extend Goodix mode-switch rejection in {source}")
    text = text.replace(old_mode_reject, new_mode_reject, 1)

    return write_if_changed(source, text)


def patch_goodix_dfu_link_auth(sdk: Path):
    """Bind the raw OTAS parser to the App's authenticated physical link.

    Recovery uses the weak open hook and retains its signed rescue flow.
    The strong application hook never changes the SDK connection capacity.
    """
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    raw_hook = """__WEAK bool inktag_dfu_link_allowed(uint8_t conn_idx)
{
    (void)conn_idx;
    return true;
}

"""
    hook = "#if !defined(BOOTLOADER_ENABLE)\n" + raw_hook + "#endif\n\n"
    if hook not in text:
        if raw_hook in text:
            text = text.replace(raw_hook, hook, 1)
        else:
            anchor = "__WEAK bool inktag_dfu_mode_switch_allowed(void)"
            if anchor not in text:
                raise SystemExit(f"cannot add Goodix authenticated-link hook to {source}")
            text = text.replace(anchor, hook + anchor, 1)
    raw_guard = """    /* App authentication precedes every OTAS parser/state entry. */
    if ((p_evt->evt_type == OTAS_EVT_TX_NOTIFICATION_ENABLED ||
         p_evt->evt_type == OTAS_EVT_RX_RECEIVE_DATA ||
         p_evt->evt_type == OTAS_EVT_DFU_TASK_ENTER) &&
        !inktag_dfu_link_allowed(p_evt->conn_idx)) return;

"""
    guard = "#if !defined(BOOTLOADER_ENABLE)\n" + raw_guard + "#endif\n\n"
    if guard not in text:
        if raw_guard in text:
            text = text.replace(raw_guard, guard, 1)
        else:
            anchor = "    if (s_ota_conn_index != BLE_GAP_INVALID_CONN_INDEX && s_ota_conn_index != p_evt->conn_idx)"
            if anchor not in text:
                raise SystemExit(f"cannot bind Goodix OTAS receive link in {source}")
            text = text.replace(anchor, guard + anchor, 1)
    return write_if_changed(source, text)


AUTHENTICATED_MODE_HANDOFF_MARKER = "/* InkTag: commit on the authenticated link before disconnect. */"
AUTHENTICATED_MODE_HANDOFF_RESET = (
    "        " + AUTHENTICATED_MODE_HANDOFF_MARKER + "\n"
    "        __set_PRIMASK(inktag_reset_primask);\n"
    "#if BLE_SUPPORT\n        wait_for_disconnection();\n#endif\n"
    "        hal_nvic_system_reset();"
)
AUTHENTICATED_MODE_HANDOFF_GR5331_RESET = (
    "#if !defined(BOOTLOADER_ENABLE)\n" + AUTHENTICATED_MODE_HANDOFF_RESET +
    "\n#else\n        hal_nvic_system_reset();\n"
    "        __set_PRIMASK(inktag_reset_primask);\n#endif"
)


def patch_goodix_authenticated_mode_handoff(sdk: Path, platform="gr5515"):
    """Commit the authorized App request before disconnect clears its session.

    No authorization survives disconnect: only the already validated Recovery
    request is durable. Restore interrupts before the bounded vendor wait.
    GR551x has an enclosing !BOOTLOADER_ENABLE branch. GR5331 needs an
    explicit role split because the same function also serves Recovery.
    """
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    start = text.index("static void dfu_mode_set(")
    end = text.index("\n}", start) + 2
    mode = text[start:end]
    if AUTHENTICATED_MODE_HANDOFF_MARKER in mode:
        return False
    wait = ("        wait_for_disconnection();\n" if platform == "gr5331" else
            "#if BLE_SUPPORT\n        wait_for_disconnection();\n#endif\n")
    reset = "        hal_nvic_system_reset();\n        __set_PRIMASK(inktag_reset_primask);"
    if (mode.count(wait) != 1 or mode.count(reset) != 1 or
            "!inktag_dfu_mode_switch_allowed()" not in mode or
            "if (!inktag_request_ok)" not in mode):
        raise SystemExit(f"cannot bind authenticated Goodix mode handoff in {source}")
    if platform == "gr5331":
        # Preserve the vendor Recovery order and bytes. Only an App has the
        # password session which is invalidated by ordinary disconnect.
        mode = mode.replace(wait, "#if defined(BOOTLOADER_ENABLE)\n" + wait + "#endif\n", 1)
        replacement = AUTHENTICATED_MODE_HANDOFF_GR5331_RESET
    else:
        mode = mode.replace(wait, "", 1)
        replacement = AUTHENTICATED_MODE_HANDOFF_RESET
    mode = mode.replace(reset, replacement, 1)
    return write_if_changed(source, text[:start] + mode + text[end:])


def patch_goodix_single_slot_dfu(sdk: Path):
    """Remove vendor multi-target/FAST_DFU writes from InkTag's sole-App OTA."""
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    if '#include "inktag_gr_flash_layout.h"' not in text:
        text = text.replace(
            '#include "inktag_ota_reject.h"',
            '#include "inktag_ota_reject.h"\n#include "inktag_gr_flash_layout.h"',
            1,
        )
    marker = "/* InkTag single-slot DFU omits legacy FAST_DFU paths. */"
    if marker in text:
        pointer_write = """#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    *p_page_start_addr = load_addr;
    *p_all_check_sum = 0u;
#else
    page_start_addr = load_addr;
    all_check_sum = 0u;
#endif
"""
        initialized_pointer_write = """#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
    p_all_check_sum = (uint32_t *)(dfu_env_all + ENV_CHECK_SUM_OFFSET);
    p_page_start_addr = (uint32_t *)(dfu_env_all + ENV_PAGE_START_ADDR_OFFSET);
    *p_page_start_addr = load_addr;
    *p_all_check_sum = 0u;
#else
    page_start_addr = load_addr;
    all_check_sum = 0u;
#endif
"""
        aligned_state = """    page_start_addr = load_addr;
    all_check_sum = 0u;
"""
        if initialized_pointer_write in text:
            text = text.replace(initialized_pointer_write, aligned_state, 1)
        elif pointer_write in text:
            text = text.replace(pointer_write, aligned_state, 1)

        start_at = text.find("static void program_start_replace(dfu_receive_frame_t *p_frame)\n{")
        start_end = text.find("\n\nstatic void program_flash_replace", start_at)
        if start_at < 0 or start_end < 0:
            raise SystemExit(f"cannot verify Goodix single-slot PROGRAM_START in {source}")
        start_body = text[start_at:start_end]
        if "*p_page_start_addr" in start_body or "*p_all_check_sum" in start_body:
            raise SystemExit(f"unaligned Goodix single-slot DFU state remains in {source}")

        # GR533x's ROM DFU environment begins at byte alignment on actual
        # hardware.  Casting its fields to uint32_t pointers lets newer GCC
        # combine adjacent stores into an alignment-requiring STRD.  InkTag's
        # single-slot path owns its checksum state, so keep it in the existing
        # naturally aligned statics instead of type-punning the ROM buffer.
        for pointer_accumulator in (
            """#if defined(SOC_GR533X) || defined(SOC_GR5405)
        *p_all_check_sum += chunk_sum;
#else
        all_check_sum += chunk_sum;
#endif
""",
            """#if defined(SOC_GR533X) || defined(SOC_GR5405) || defined(SOC_SGP)
        *p_all_check_sum += chunk_sum;
#else
        all_check_sum += chunk_sum;
#endif
""",
        ):
            text = text.replace(
                pointer_accumulator, "        all_check_sum += chunk_sum;\n", 1
            )
        pointer_end_check = """#if defined(SOC_GR533X) || defined(SOC_GR5405)
    if (bin_check_sum == (*p_all_check_sum))
    {
        check_result = true;
    }
#endif

"""
        text = text.replace(pointer_end_check, "", 1)
        return write_if_changed(source, text)

    text = text.replace(
        "#define FAST_DFU_INIT_STATE             0x00\n"
        "#define FAST_DFU_ERASE_FLASH_STATE      0x01\n"
        "#define FAST_DFU_PROGRAM_FLASH_STATE    0x02\n",
        marker + "\n",
        1,
    )
    text = text.replace(
        "    DFU_FLASH_SPI = 0x01,\n\n"
        "    FAST_DFU_INNER = 0x02,\n"
        "    FAST_DFU_SPI = 0X03,\n",
        "    DFU_FLASH_SPI = 0x01,\n",
        1,
    )

    for obsolete in (
        "static ring_buffer_t        s_ble_rx_ring_buffer;\n",
        "static bool                 s_ring_buffer_over_flag = false;\n",
        "static uint8_t              s_fast_dfu_state  = FAST_DFU_INIT_STATE;\n",
        "static bool                 s_program_end_flag = false;\n",
        "static uint32_t             s_program_address = 0x00;\n",
        "static uint16_t             s_erase_all_count = 0;\n",
        "static uint32_t             s_all_write_size  = 0x00;\n",
        "static uint16_t             s_erase_count = 0;\n",
        "static uint8_t              s_fast_dfu_mode    = 0x00;\n",
        "static uint8_t * s_p_ring_buffer        = NULL;\n",
        "static uint16_t  s_ring_buffer_size     = 0;\n",
        "#define s_p_ring_buffer        __s_buffer_pool\n",
        "#define s_ring_buffer_size     MIN_RING_BUFFER_SIZE\n",
        "        s_p_ring_buffer        = NULL;\n",
        "    s_p_ring_buffer        = p_buffer;\n",
        "    s_ring_buffer_size     = (uint16_t)(buffer_size - ONCE_WRITE_DATA_LEN);\n",
    ):
        text = text.replace(obsolete, "", 1)
    text = text.replace(
        "#define s_p_fast_cache_buffer  (__s_buffer_pool + s_ring_buffer_size)",
        "#define s_p_fast_cache_buffer  (__s_buffer_pool + MIN_RING_BUFFER_SIZE)",
        1,
    )

    fast_rx_start = text.find(
        "static void fast_dfu_write_data_to_buffer(uint8_t const *p_data, uint16_t length)"
    )
    ota_event_start = text.find("static void otas_evt_process(otas_evt_t *p_evt)", fast_rx_start)
    if fast_rx_start < 0 or ota_event_start < 0:
        raise SystemExit(f"cannot remove Goodix FAST_DFU RX state from {source}")
    text = text[:fast_rx_start] + text[ota_event_start:]
    text = text.replace(
        "            dfu_cmd_parse_state_reset();\n"
        "            fast_dfu_state_machine_reset();\n",
        "            dfu_cmd_parse_state_reset();\n",
        1,
    )
    fast_rx = """            if (!s_fast_dfu_mode || s_program_end_flag)
            {
                dfu_ble_receive_data_process(p_evt->p_data, p_evt->length);
            }
            else
            {
                s_fast_dfu_state = FAST_DFU_PROGRAM_FLASH_STATE;
                fast_dfu_write_data_to_buffer(p_evt->p_data, p_evt->length);
            }
"""
    if fast_rx not in text:
        raise SystemExit(f"cannot isolate Goodix FAST_DFU BLE receive path in {source}")
    text = text.replace(
        fast_rx,
        "            dfu_ble_receive_data_process(p_evt->p_data, p_evt->length);\n",
        1,
    )

    fast_erase_start = text.find("static void fast_dfu_erase_flash(void)")
    signed_state = text.find("static bool s_inktag_signed_firmware = false;", fast_erase_start)
    if fast_erase_start < 0 or signed_state < 0:
        raise SystemExit(f"cannot remove Goodix FAST_DFU erase state from {source}")
    text = text[:fast_erase_start] + text[signed_state:]

    start_at = text.find("static void program_start_replace(dfu_receive_frame_t *p_frame)\n{")
    start_end = text.find("\n\nstatic void program_flash_replace", start_at)
    if start_at < 0 or start_end < 0:
        raise SystemExit(f"cannot replace Goodix single-slot PROGRAM_START in {source}")
    start_handler = r'''static void program_start_replace(dfu_receive_frame_t *p_frame)
{
    s_inktag_normal_transfer_active = false;
    if (p_frame->data_len != (INKTAG_GR_IMAGE_INFO_LEN + 1u))
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    uint8_t dfu_type = p_frame->data[0] & 0x0fu;
    uint8_t firmware_type = (p_frame->data[0] & 0xf0u) >> 4;
    if (dfu_type != DFU_FLASH_INNER)
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, dfu_type);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }
    if (firmware_type != SIGN_FIRMWARE)
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
            INKTAG_OTA_REJECT_STAGE_SIGNATURE_VERIFY, 0u, firmware_type);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    memcpy(&s_now_img_info, &p_frame->data[1], sizeof(s_now_img_info));
    uint32_t layout_marker;
    uint32_t layout_inverse;
    memcpy(&layout_marker,
           &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN],
           sizeof(layout_marker));
    memcpy(&layout_inverse,
           &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN + 4u],
           sizeof(layout_inverse));
    uint32_t load_addr = s_now_img_info.boot_info.load_addr;
    uint32_t bin_size = s_now_img_info.boot_info.bin_size;
    if (s_now_img_info.pattern != DFU_IMG_INFO_PATTERN || bin_size == 0u ||
        load_addr != s_dfu_info.dfu_fw_save_addr ||
        s_now_img_info.boot_info.run_addr != load_addr ||
        layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC ||
        layout_inverse != INKTAG_GR_LAYOUT_MARKER_INVERSE ||
        bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT)
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, load_addr);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    s_file_size = bin_size + INKTAG_GR_IMAGE_INFO_LEN +
                  INKTAG_GR_SIGNATURE_EXTENSION_LEN;
    if (load_addr > (UINT32_MAX - s_file_size) ||
        check_system_info_address(load_addr) ||
        check_system_info_address(load_addr + s_file_size - 1u))
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, load_addr);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    if (!inktag_dfu_before_program_start(&s_now_img_info))
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data,
            INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u,
            s_now_img_info.version);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    s_inktag_signed_firmware = true;
    s_inktag_transfer_base = load_addr;
    s_inktag_transfer_end = load_addr + s_file_size;
    s_inktag_transfer_next = load_addr;
    s_inktag_erased_through = load_addr + DFU_FLASH_SECTOR_SIZE;
    page_start_addr = load_addr;
    all_check_sum = 0u;
    dfu_flash_type_set(DFU_FLASH_INNER);

    security_disable();
    bool erase_ok = inktag_dfu_app_erase(load_addr, DFU_FLASH_SECTOR_SIZE);
    security_state_recovery();
    if (!erase_ok)
    {
        uint8_t reject_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_ERASE_FAILED,
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, load_addr);
        dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);
        return;
    }

    s_inktag_normal_transfer_active = true;
    dfu_program_start(s_file_size);
    p_frame->data[0] = DFU_ACK_SUCCESS;
    dfu_send_frame(p_frame->data, 1u, p_frame->cmd_type);
    cmd_receive_flag = 0;
}'''
    text = text[:start_at] + start_handler + text[start_end:]

    erase_old = """            bool erase_state = dfu_flash_erase(write_addr, DFU_FLASH_SECTOR_SIZE);
            security_state_recovery();
#if defined(SOC_GR533X) || defined(SOC_GR5405)
            bool erase_ok = !erase_state;
#else
            bool erase_ok = erase_state;
#endif
"""
    erase_new = """            bool erase_ok = inktag_dfu_app_erase(
                write_addr, DFU_FLASH_SECTOR_SIZE);
            security_state_recovery();
"""
    if erase_old not in text:
        raise SystemExit(f"cannot route Goodix PROGRAM_FLASH erase in {source}")
    text = text.replace(erase_old, erase_new, 1)
    write_old = """        uint32_t write_state = dfu_flash_write(
            write_addr, &p_frame->data[7u + input_offset], write_len);
        (void)dfu_flash_read(write_addr, s_p_fast_cache_buffer, write_len);
        security_state_recovery();
#if defined(SOC_GR533X) || defined(SOC_GR5405)
        bool write_ok = (write_state == HAL_OK);
#else
        bool write_ok = (write_state == write_len);
#endif
"""
    write_new = """        bool write_ok = inktag_dfu_app_write(
            write_addr, &p_frame->data[7u + input_offset], write_len);
        (void)dfu_flash_read(write_addr, s_p_fast_cache_buffer, write_len);
        security_state_recovery();
"""
    if write_old not in text:
        raise SystemExit(f"cannot route Goodix PROGRAM_FLASH write in {source}")
    text = text.replace(write_old, write_new, 1)

    pointer_accumulator = """#if defined(SOC_GR533X) || defined(SOC_GR5405)
        *p_all_check_sum += chunk_sum;
#else
        all_check_sum += chunk_sum;
#endif
"""
    if pointer_accumulator not in text:
        raise SystemExit(f"cannot align Goodix single-slot checksum state in {source}")
    text = text.replace(
        pointer_accumulator, "        all_check_sum += chunk_sum;\n", 1
    )

    text = text.replace(
        "if (!s_fast_dfu_mode && bin_check_sum == (*p_all_check_sum))",
        "if (bin_check_sum == (*p_all_check_sum))",
        1,
    )
    pointer_end_check = """#if defined(SOC_GR533X) || defined(SOC_GR5405)
    if (bin_check_sum == (*p_all_check_sum))
    {
        check_result = true;
    }
#endif

"""
    pointer_end_check_sgp = pointer_end_check.replace(
        "defined(SOC_GR5405)\n",
        "defined(SOC_GR5405) || defined(SOC_SGP)\n",
        1,
    )
    if pointer_end_check not in text and pointer_end_check_sgp in text:
        pointer_end_check = pointer_end_check_sgp
    if pointer_end_check not in text:
        raise SystemExit(f"cannot isolate Goodix single-slot checksum check in {source}")
    text = text.replace(pointer_end_check, "", 1)
    text = text.replace(
        "if (!s_fast_dfu_mode &&\n"
        "        (!s_inktag_normal_transfer_active ||",
        "if (!s_inktag_normal_transfer_active ||",
        1,
    )
    text = text.replace(
        "         s_inktag_transfer_next != s_inktag_transfer_end))",
        "        s_inktag_transfer_next != s_inktag_transfer_end)",
        1,
    )
    end_at = text.find("static void program_end_replace(dfu_receive_frame_t *p_frame)")
    response_at = text.find("    uint8_t response_len = 1u;", end_at)
    commit_match = re.search(
        r"\n    if \(check_result == (?:0x01|true)\)\n", text[response_at:]
    )
    if response_at < 0 or commit_match is None:
        raise SystemExit(f"cannot remove Goodix FAST_DFU PROGRAM_END response in {source}")
    commit_at = response_at + commit_match.start()
    response = r'''    uint8_t response_len = 1u;
    if (check_result)
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
        s_inktag_normal_transfer_active = false;
    }
    else
    {
        inktag_ota_reject_reason_t reason =
            INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED;
        uint8_t detail_flags = 0u;
        uint32_t detail = 0u;
        if (!s_inktag_normal_transfer_active ||
            s_inktag_transfer_next != s_inktag_transfer_end)
        {
            reason = INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH;
            detail_flags = INKTAG_OTA_REJECT_FLAG_DETAIL_IS_OFFSET;
            detail = s_inktag_transfer_next - s_inktag_transfer_base;
        }
        response_len = inktag_dfu_build_rejection(
            p_frame->data, reason, INKTAG_OTA_REJECT_STAGE_PROGRAM_END,
            detail_flags, detail);
        s_inktag_normal_transfer_active = false;
    }
    dfu_send_frame(p_frame->data, response_len, p_frame->cmd_type);
'''
    text = text[:response_at] + response + text[commit_at:]

    schedule_start = text.find("static void fast_dfu_program_schedule(void)")
    image_info_start = text.find("uint16_t dfu_fw_image_info_get(", schedule_start)
    if schedule_start < 0 or image_info_start < 0:
        raise SystemExit(f"cannot remove Goodix FAST_DFU scheduler from {source}")
    normal_schedule = """void dfu_schedule(void)
{
    normal_dfu_schedule();
}

"""
    text = text[:schedule_start] + normal_schedule + text[image_info_start:]

    if "uint8_t inktag_image_info[INKTAG_GR_IMAGE_INFO_LEN];" not in text:
        image_info_read = re.compile(
            r"(?P<indent>[ \t]*)uint32_t fw_img_info_addr = "
            r"s_dfu_info\.dfu_fw_save_addr \+ s_now_img_info\.boot_info\.bin_size;\n"
            r"(?P=indent)hal_flash_read\(fw_img_info_addr, "
            r"\(uint8_t \*\)&s_dfu_info\.dfu_img_info, sizeof\(dfu_image_info_t\)\);"
        )
        text, read_count = image_info_read.subn(
            lambda match: (
                f"{match.group('indent')}uint32_t fw_img_info_addr = "
                "s_dfu_info.dfu_fw_save_addr + s_now_img_info.boot_info.bin_size;\n"
                f"{match.group('indent')}uint8_t "
                "inktag_image_info[INKTAG_GR_IMAGE_INFO_LEN];\n"
                f"{match.group('indent')}hal_flash_read("
                "fw_img_info_addr, inktag_image_info, sizeof(inktag_image_info));\n"
                f"{match.group('indent')}memcpy(&s_dfu_info.dfu_img_info, "
                "inktag_image_info, sizeof(s_dfu_info.dfu_img_info));"
            ),
            text,
            count=1,
        )
        if read_count != 1:
            raise SystemExit(f"cannot retain Goodix layout-v3 image-info tail in {source}")
    old_app_info_write = (
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR, "
        "(uint8_t*)&s_dfu_info.dfu_img_info, sizeof(s_dfu_info.dfu_img_info));"
    )
    new_app_info_write = (
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR, inktag_image_info, "
        "sizeof(inktag_image_info));"
    )
    if new_app_info_write not in text:
        if old_app_info_write not in text:
            raise SystemExit(f"cannot commit Goodix layout-v3 image-info tail in {source}")
        text = text.replace(old_app_info_write, new_app_info_write, 1)

    old_info_read = (
        "hal_flash_read_judge_security(APP_INFO_START_ADDR, "
        "(uint8_t *)&p_frame->data[6], sizeof(dfu_image_info_t));"
    )
    new_info_read = (
        "hal_flash_read_judge_security(APP_INFO_START_ADDR, "
        "&p_frame->data[6], INKTAG_GR_IMAGE_INFO_LEN);"
    )
    if new_info_read not in text:
        if old_info_read not in text:
            raise SystemExit(f"cannot report Goodix layout-v3 image-info tail in {source}")
        text = text.replace(old_info_read, new_info_read, 1)

    if "FAST_DFU" in text.replace(marker, "") or "s_fast_dfu_" in text:
        raise SystemExit(f"legacy Goodix FAST_DFU path remains in {source}")
    changed = write_if_changed(source, text)

    header = sdk / "components/libraries/dfu_port/dfu_port.h"
    header_text = header.read_text(encoding="utf-8")
    header_text = header_text.replace("void fast_dfu_state_machine_reset(void);\n", "")
    return write_if_changed(header, header_text) or changed


def patch_goodix_layout_v3_dfu(sdk: Path):
    """Make the signed 48-byte layout-v3 image-info contract idempotent.

    The single-slot transformation predates layout v3 and deliberately returns
    early once its marker is present.  Keep this migration separate so an
    already-materialized SDK cannot retain the legacy 41-byte PROGRAM_START
    wire shape or discard the signed eight-byte layout marker on commit.
    """
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    if '#include "inktag_gr_flash_layout.h"' not in text:
        text = text.replace(
            '#include "inktag_ota_reject.h"',
            '#include "inktag_ota_reject.h"\n#include "inktag_gr_flash_layout.h"',
            1,
        )

    text = text.replace(
        "p_frame->data_len != (sizeof(dfu_image_info_t) + 1u)",
        "p_frame->data_len != (INKTAG_GR_IMAGE_INFO_LEN + 1u)",
        1,
    )

    marker_parse = r'''    uint32_t layout_marker;
    uint32_t layout_inverse;
    memcpy(&layout_marker,
           &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN],
           sizeof(layout_marker));
    memcpy(&layout_inverse,
           &p_frame->data[1u + INKTAG_GR_IMAGE_INFO_STRUCT_LEN + 4u],
           sizeof(layout_inverse));
'''
    if marker_parse not in text:
        anchor = "    memcpy(&s_now_img_info, &p_frame->data[1], sizeof(s_now_img_info));\n"
        if anchor not in text:
            raise SystemExit(f"cannot add Goodix layout-v3 marker parser in {source}")
        text = text.replace(anchor, anchor + marker_parse, 1)

    limit_pattern = re.compile(
        r"(?P<indent>[ \t]*)bin_size > \(0x[0-9A-Fa-f]+u - 48u - 856u\)"
    )
    text, _ = limit_pattern.subn(
        lambda match: (
            f"{match.group('indent')}layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC ||\n"
            f"{match.group('indent')}layout_inverse != INKTAG_GR_LAYOUT_MARKER_INVERSE ||\n"
            f"{match.group('indent')}bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT"
        ),
        text,
        count=1,
    )
    if "layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC" not in text:
        legacy_limit = "        bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT"
        replacement = (
            "        layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC ||\n"
            "        layout_inverse != INKTAG_GR_LAYOUT_MARKER_INVERSE ||\n"
            "        bin_size > INKTAG_GR_APP_PAYLOAD_LIMIT"
        )
        if legacy_limit not in text:
            raise SystemExit(f"cannot bind Goodix layout-v3 marker to PROGRAM_START in {source}")
        text = text.replace(legacy_limit, replacement, 1)

    text = re.sub(
        r"s_file_size = bin_size \+ 48u \+ 856u;",
        "s_file_size = bin_size + INKTAG_GR_IMAGE_INFO_LEN +\n"
        "                  INKTAG_GR_SIGNATURE_EXTENSION_LEN;",
        text,
        count=1,
    )

    old_info_read = (
        "hal_flash_read_judge_security(APP_INFO_START_ADDR, "
        "(uint8_t *)&p_frame->data[6], sizeof(dfu_image_info_t));"
    )
    new_info_read = (
        "hal_flash_read_judge_security(APP_INFO_START_ADDR, "
        "&p_frame->data[6], INKTAG_GR_IMAGE_INFO_LEN);"
    )
    text = text.replace(old_info_read, new_info_read, 1)

    if "uint8_t inktag_image_info[INKTAG_GR_IMAGE_INFO_LEN];" not in text:
        image_info_read = re.compile(
            r"(?P<indent>[ \t]*)uint32_t fw_img_info_addr = "
            r"s_dfu_info\.dfu_fw_save_addr \+ s_now_img_info\.boot_info\.bin_size;\n"
            r"(?P=indent)hal_flash_read\(fw_img_info_addr, "
            r"\(uint8_t \*\)&s_dfu_info\.dfu_img_info, sizeof\(dfu_image_info_t\)\);"
        )
        text, read_count = image_info_read.subn(
            lambda match: (
                f"{match.group('indent')}uint32_t fw_img_info_addr = "
                "s_dfu_info.dfu_fw_save_addr + s_now_img_info.boot_info.bin_size;\n"
                f"{match.group('indent')}uint8_t "
                "inktag_image_info[INKTAG_GR_IMAGE_INFO_LEN];\n"
                f"{match.group('indent')}hal_flash_read("
                "fw_img_info_addr, inktag_image_info, sizeof(inktag_image_info));\n"
                f"{match.group('indent')}memcpy(&s_dfu_info.dfu_img_info, "
                "inktag_image_info, sizeof(s_dfu_info.dfu_img_info));"
            ),
            text,
            count=1,
        )
        if read_count != 1:
            raise SystemExit(f"cannot retain Goodix layout-v3 image-info tail in {source}")

    old_app_info_write = (
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR, "
        "(uint8_t*)&s_dfu_info.dfu_img_info, sizeof(s_dfu_info.dfu_img_info));"
    )
    new_app_info_write = (
        "inktag_dfu_metadata_write(APP_INFO_START_ADDR, inktag_image_info, "
        "sizeof(inktag_image_info));"
    )
    text = text.replace(old_app_info_write, new_app_info_write, 1)

    required = (
        "p_frame->data_len != (INKTAG_GR_IMAGE_INFO_LEN + 1u)",
        "layout_marker != INKTAG_GR_LAYOUT_MARKER_MAGIC",
        "layout_inverse != INKTAG_GR_LAYOUT_MARKER_INVERSE",
        new_info_read,
        "uint8_t inktag_image_info[INKTAG_GR_IMAGE_INFO_LEN];",
        new_app_info_write,
    )
    missing = [needle for needle in required if needle not in text]
    if missing:
        raise SystemExit(
            f"incomplete Goodix layout-v3 DFU migration in {source}: {missing}"
        )
    return write_if_changed(source, text)


def patch_goodix_safe_resume_dfu(sdk: Path):
    """Bind guarded Recovery resume to the native Goodix OTAS transport."""
    source = sdk / "components/libraries/dfu_port/dfu_port.c"
    text = source.read_text(encoding="utf-8")
    if "/* InkTag Recovery return after actual response TX. */" in text:
        return False
    changed = False
    legacy_request_commit = """        inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE);
        inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info));
        security_state_recovery();
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    guarded_request_commit_preflag = """        inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        bool inktag_request_ok =
            inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE) &&
            inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info)) == sizeof(s_dfu_info) &&
            inktag_dfu_recovery_request_mark();
        security_state_recovery();
        if (!inktag_request_ok)
        {
            __set_PRIMASK(inktag_reset_primask);
            return;
        }
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    guarded_request_commit = """        inktag_dfu_before_reset();

        uint32_t inktag_reset_primask = __get_PRIMASK();
        __disable_irq();
        security_disable();
        bool inktag_request_ok =
            inktag_dfu_metadata_erase(DFU_INFO_START_ADDR, DFU_FLASH_SECTOR_SIZE) &&
            inktag_dfu_metadata_write(DFU_INFO_START_ADDR, (uint8_t*)&s_dfu_info, sizeof(s_dfu_info)) == sizeof(s_dfu_info) &&
            inktag_dfu_recovery_request_mark();
        security_state_recovery();
        if (!inktag_request_ok)
        {
            __set_PRIMASK(inktag_reset_primask);
            return;
        }
#if defined(INKTAG_GOODIX_GR5331)
        pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif
        hal_nvic_system_reset();
        __set_PRIMASK(inktag_reset_primask);
"""
    if legacy_request_commit in text:
        text = text.replace(legacy_request_commit, guarded_request_commit, 1)
        changed = True
    elif guarded_request_commit_preflag in text:
        text = text.replace(guarded_request_commit_preflag,
                            guarded_request_commit, 1)
        changed = True
    elif (guarded_request_commit not in text and
          guarded_request_commit.replace(
              "        hal_nvic_system_reset();\n        __set_PRIMASK(inktag_reset_primask);",
              AUTHENTICATED_MODE_HANDOFF_RESET, 1) not in text and
          guarded_request_commit.replace(
              "        hal_nvic_system_reset();\n        __set_PRIMASK(inktag_reset_primask);",
              AUTHENTICATED_MODE_HANDOFF_GR5331_RESET, 1) not in text):
        raise SystemExit(f"cannot persist Goodix controlled Recovery request in {source}")

    marker = "/* InkTag guarded Recovery safe resume. */"
    if marker in text:
        legacy_registration = (
            "    dfu_set_cmd_handler(0x1E, 0X43, "
            "inktag_dfu_safe_resume_app);\n"
        )
        mode_registration = (
            "    dfu_set_cmd_handler(0x1C, 0X41, dfu_mode_set);\n"
        )
        guarded_mode_registration = (
            "    dfu_set_cmd_handler(0x1C, 0X41, "
            "inktag_dfu_mode_or_safe_resume);\n"
        )
        if legacy_registration in text:
            text = text.replace(legacy_registration, "")
            changed = True
        if mode_registration in text:
            text = text.replace(mode_registration, guarded_mode_registration)
            changed = True
        if "static void inktag_dfu_mode_or_safe_resume" not in text:
            info_handler = "static void dfu_fw_info_get(dfu_receive_frame_t *p_frame)\n"
            if info_handler not in text:
                raise SystemExit(
                    f"cannot migrate Goodix safe-resume mode dispatcher in {source}"
                )
            mode_dispatcher = r'''static void inktag_dfu_mode_or_safe_resume(dfu_receive_frame_t *p_frame)
{
#ifdef BOOTLOADER_ENABLE
    if (p_frame->data_len == 0u)
    {
        inktag_dfu_safe_resume_app(p_frame);
        return;
    }
#endif
    dfu_mode_set(p_frame);
}

'''
            text = text.replace(info_handler, mode_dispatcher + info_handler, 1)
            changed = True
        disconnect_without_transfer_clear = """        case OTAS_EVT_DISCONNECT:
            if (s_ota_conn_index == p_evt->conn_idx)
            {
                s_ota_conn_index = BLE_GAP_INVALID_CONN_INDEX;
            }
            if (s_inktag_safe_resume_pending)
"""
        disconnect_with_transfer_clear = """        case OTAS_EVT_DISCONNECT:
            if (s_ota_conn_index == p_evt->conn_idx)
            {
                s_ota_conn_index = BLE_GAP_INVALID_CONN_INDEX;
            }
            /* A disconnected host no longer owns an active transfer. The
             * durable mutation marker still prevents an unsafe app resume. */
            s_inktag_normal_transfer_active = false;
            s_inktag_application_mutated_this_transfer = false;
            if (s_inktag_safe_resume_pending)
"""
        disconnect_final = """        case OTAS_EVT_DISCONNECT:
            if (s_ota_conn_index == p_evt->conn_idx)
            {
                s_ota_conn_index = BLE_GAP_INVALID_CONN_INDEX;
            }
            /* A disconnected host no longer owns an active transfer.  The
             * persistent mutation marker remains authoritative, so a later
             * SAFE_RESUME_APP can report the actual invalid application. */
            s_inktag_normal_transfer_active = false;
            s_inktag_application_mutated_this_transfer = false;
            if (s_inktag_safe_resume_pending)
"""
        if disconnect_with_transfer_clear not in text and disconnect_final not in text:
            if disconnect_without_transfer_clear not in text:
                raise SystemExit(
                    f"cannot migrate Goodix safe-resume disconnect state in {source}"
                )
            text = text.replace(
                disconnect_without_transfer_clear,
                disconnect_with_transfer_clear,
                1,
            )
            changed = True
        return write_if_changed(source, text) if changed else False

    transfer_state = "static bool                 s_inktag_normal_transfer_active = false;\n"
    if transfer_state not in text:
        raise SystemExit(f"cannot track Goodix safe-resume transfer state in {source}")
    text = text.replace(
        transfer_state,
        transfer_state + "static bool                 s_inktag_safe_resume_pending = false;\n",
        1,
    )

    info_handler = "static void dfu_fw_info_get(dfu_receive_frame_t *p_frame)\n"
    if info_handler not in text:
        raise SystemExit(f"cannot add Goodix safe-resume handler in {source}")
    safe_handler = r'''/* InkTag guarded Recovery safe resume. */
static bool inktag_dfu_schedule_safe_resume(void)
{
#ifdef BOOTLOADER_ENABLE
    if (!s_inktag_normal_transfer_active &&
        inktag_dfu_safe_resume_allowed())
    {
        s_inktag_safe_resume_pending = true;
        return true;
    }
#endif
    return false;
}

static void inktag_dfu_safe_resume_app(dfu_receive_frame_t *p_frame)
{
    uint8_t response_len = 1u;
    if (p_frame->data_len == 0u && inktag_dfu_schedule_safe_resume())
    {
        p_frame->data[0] = DFU_ACK_SUCCESS;
    }
    else
    {
        response_len = inktag_dfu_build_rejection(
            p_frame->data, INKTAG_OTA_REJECT_REASON_INTERNAL,
            INKTAG_OTA_REJECT_STAGE_BOOT_CONFIRM, 0u, 0u);
    }
    dfu_send_frame(p_frame->data, response_len, p_frame->cmd_type);
#if defined(SOC_GR5515) || defined(SOC_GR5X25) || defined(SOC_GR5526)
    cmd_receive_flag = 0;
#endif
}

static void inktag_dfu_mode_or_safe_resume(dfu_receive_frame_t *p_frame)
{
#ifdef BOOTLOADER_ENABLE
    if (p_frame->data_len == 0u)
    {
        inktag_dfu_safe_resume_app(p_frame);
        return;
    }
#endif
    dfu_mode_set(p_frame);
}

'''
    text = text.replace(info_handler, safe_handler + info_handler, 1)

    start_at = text.find("static void program_start_replace(dfu_receive_frame_t *p_frame)\n{")
    start_end = text.find("\n\nstatic void program_flash_replace", start_at)
    if start_at < 0 or start_end < 0:
        raise SystemExit(f"cannot bind Goodix PROGRAM_START safe resume in {source}")
    start_handler = text[start_at:start_end]
    rejection_return = (
        "dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);\n"
        "        return;"
    )
    rejection_count = start_handler.count(rejection_return)
    if rejection_count < 5:
        raise SystemExit(
            f"cannot find all Goodix PROGRAM_START rejection paths in {source}"
        )
    start_handler = start_handler.replace(
        rejection_return,
        "dfu_send_frame(p_frame->data, reject_len, p_frame->cmd_type);\n"
        "        (void)inktag_dfu_schedule_safe_resume();\n"
        "        return;",
    )
    start_handler = start_handler.replace(
        "{\n    s_inktag_normal_transfer_active = false;",
        "{\n    s_inktag_normal_transfer_active = false;\n"
        "    s_inktag_application_mutated_this_transfer = false;",
        1,
    )
    text = text[:start_at] + start_handler + text[start_end:]

    disconnect = """            if (s_ota_conn_index == p_evt->conn_idx)
            {
                s_ota_conn_index = BLE_GAP_INVALID_CONN_INDEX;
            }
            break;
"""
    disconnect_safe = """            if (s_ota_conn_index == p_evt->conn_idx)
            {
                s_ota_conn_index = BLE_GAP_INVALID_CONN_INDEX;
            }
            /* A disconnected host no longer owns an active transfer.  The
             * persistent mutation marker remains authoritative, so a later
             * SAFE_RESUME_APP can report the actual invalid application. */
            s_inktag_normal_transfer_active = false;
            s_inktag_application_mutated_this_transfer = false;
            if (s_inktag_safe_resume_pending)
            {
                s_inktag_safe_resume_pending = false;
                inktag_dfu_before_reset();
#if defined(INKTAG_GOODIX_GR5331)
                pwr_mgmt_set_wakeup_flag(COLD_BOOT);
#endif
                hal_nvic_system_reset();
            }
            break;
"""
    if disconnect not in text:
        raise SystemExit(f"cannot bind Goodix safe resume to disconnect in {source}")
    text = text.replace(disconnect, disconnect_safe, 1)

    registration = "    dfu_set_cmd_handler(0x1C, 0X41, dfu_mode_set);\n"
    registration_count = text.count(registration)
    if registration_count < 2:
        raise SystemExit(f"cannot register Goodix safe-resume command in {source}")
    text = text.replace(
        registration,
        "    dfu_set_cmd_handler(0x1C, 0X41, "
        "inktag_dfu_mode_or_safe_resume);\n",
    )
    return write_if_changed(source, text)

def patch_gr533x_att_db_heap(sdk: Path):
    """Give InkTag + OTAS enough retained ATT database RAM."""
    header = sdk / "platform/soc/include/ble_cfg.h"
    text = header.read_text(encoding="utf-8")
    old = "#define ATT_DB_HEAP_SIZE          MEM_CALC_HEAP_LEN_IN_BYTES(1024)"
    new = """#if defined(INKTAG_GOODIX_GR5331)
#define ATT_DB_HEAP_SIZE          MEM_CALC_HEAP_LEN_IN_BYTES(2048)
#else
#define ATT_DB_HEAP_SIZE          MEM_CALC_HEAP_LEN_IN_BYTES(1024)
#endif"""
    if new in text:
        pass
    elif old in text:
        text = text.replace(old, new, 1)
    else:
        raise SystemExit(f"cannot enlarge GR5331 InkTag ATT database heap in {header}")
    return write_if_changed(header, text)

def copy_if_changed(src: Path, dst: Path):
    if src.suffix in ('.c', '.h', '.inc'):
        raw = src.read_bytes()
        if not raw or b'\0' in raw:
            raise ValueError('Invalid empty/NUL compiler source: '+str(src))
    if dst.exists() and src.read_bytes() == dst.read_bytes():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    # Changed bytes need a fresh destination mtime for incremental compilers.
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)
    return True


def platform_profile_source(repo: Path, name: str, profile: str):
    """Exact platform selection; private files are never probed by base."""
    if profile not in ('base', 'pro'):
        raise ValueError('Explicit base/pro platform source profile required')
    manifest = json.loads((repo/'firmware/ports/goodix/source-profiles.json').read_text())
    modules = {item['nativeBasename']: item['path'] for item in manifest['items']
               if item['kind'] == 'pro-module' or (item['kind'] == 'pro-increment' and 'nativeBasename' in item)}
    retired = {item['path'] for item in manifest.get('retiredItems', [])}
    private = {item['path'] for item in manifest['items']
               if item['kind'].startswith('pro-')}
    selected = modules.get(name, name)
    if selected in retired or (selected in private and profile == 'base'):
        return None
    source = repo/'firmware/ports/goodix/common/src'/selected
    if not source.is_file():
        raise ValueError(f'Missing {profile} Goodix source: {selected}')
    return source


def compose_pro_gui_source(repo: Path, relative: str, text: str, sources: list) -> str:
    # Additional GUI includes changed ARM linker veneer ordering in native
    # builds. Reconstruct the original Pro TU using the declared composition.
    manifest = json.loads((repo/'firmware/source-profiles.json').read_text(encoding='utf-8'))
    compositions = manifest['rendererCompositions'][relative]
    if not compositions:
        raise ValueError('Missing Pro GUI provider composition')
    for anchor, logical in compositions.items():
        if text.count(anchor) != 1:
            raise ValueError('Unknown GUI provider composition: ' + logical)
        part = source_path(repo, logical, profile='pro')
        sources.append(dict(logicalPath=logical, source=part.relative_to(repo).as_posix(),
                            sha256=hashlib.sha256(part.read_bytes()).hexdigest()))
        text = text.replace(anchor, part.read_text(encoding='utf-8'), 1)
    return text


def copy_conditional_profile_inputs(selected: Path, dst: Path, *, repo: Path,
                                    relative: str, selections: list):
    """Keep both declared compile-time branches beside a flattened wrapper.

    The preprocessor still selects the provider. Base never calls this helper,
    and no directory-wide private source sweep is needed for Pro.
    """
    manifest = json.loads((repo/'firmware/source-profiles.json').read_text())
    entry = next((e for e in manifest['overrides'] if e['logicalPath'] == relative), {})
    variants = entry.get('conditionalVariants')
    if not variants:
        # Storage also selects this compile-time policy when the board has no
        # NFC backend. Resolve its declared Pro provider through the normal
        # profile path, independently of whether FMSC wrappers were copied.
        name = 'inktag_nfc_r16_profile.h'
        if re.search(r'^\s*#\s*include\s*"'+re.escape(name)+r'"', selected.read_text(), re.M):
            sources = []
            changed = copy_profile_source(repo/'firmware/common/input'/name,
                dst.parent/name, repo=repo, profile='pro', selections=sources, retired=[])
            selections.extend(s for s in sources if not any(
                p['destination'] == s['destination'] for p in selections))
            return changed
        return False
    dependencies = {repo/v['source'] for v in variants['variants'].values()}
    # The wrapper's local policy header is also a real compiler dependency.
    for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', selected.read_text(), re.M):
        path = selected.parent/name
        if path not in dependencies:
            if Path(name).name != name or not path.is_file():
                raise ValueError('Missing conditional profile dependency: ' + name)
            dependencies.add(path)
    changed = False
    for path in sorted(dependencies):
        if path.parent != selected.parent or not path.is_file():
            raise ValueError('Conditional profile inputs must stay beside their wrapper')
        output = dst.parent/path.name
        source = path.relative_to(repo).as_posix()
        prior = next((e for e in selections if e['destination'] == str(output)), None)
        if prior is not None and prior['source'] != source:
            raise ValueError('Conditional profile basename collision: ' + path.name)
        if prior is None:
            selections.append(dict(logicalPath=relative, source=source,
                destination=str(output), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                conditionalDependency=True))
        changed |= copy_if_changed(path, output)
    return changed


def copy_profile_source(src: Path, dst: Path, *, repo: Path, profile: str,
                        selections: list, retired: list):
    relative = src.relative_to(repo).as_posix()
    if relative.startswith('firmware/common/'):
        selected = source_path(repo, relative, profile=profile)
    elif relative.startswith('firmware/ports/goodix/common/src/'):
        selected = platform_profile_source(repo, src.name, profile)
    else:
        selected = src
    if selected is None:
        if dst.is_file():
            # Only a known generated file selected by this explicit producer.
            # Preserve old bytes privately before excluding them from Src.
            digest = hashlib.sha256(dst.read_bytes()).hexdigest()
            backup = dst.parent/'.inktag-retired-pro'/digest/dst.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if backup.exists() and backup.read_bytes() != dst.read_bytes():
                raise ValueError('Retired generated input hash collision')
            shutil.copyfile(dst, backup)
            retired.append(dict(path=str(dst), sha256=digest, backup=str(backup)))
            dst.unlink()
        return False
    selections.append(dict(logicalPath=relative, source=selected.relative_to(repo).as_posix(),
                           destination=str(dst), sha256=hashlib.sha256(selected.read_bytes()).hexdigest()))
    if profile == 'pro' and relative == 'firmware/common/gui/inktag_gui_default.inc':
        parts = selections[-1]['compositionSources'] = []
        return write_if_changed(dst, compose_pro_gui_source(repo, relative,
                                selected.read_text(encoding='utf-8'), parts))
    changed = copy_if_changed(selected, dst)
    if profile == 'pro' and relative.startswith('firmware/common/'):
        changed |= copy_conditional_profile_inputs(selected, dst, repo=repo,
            relative=relative, selections=selections)
    return changed


def copy_gui_relative_power_header(repo: Path, common_out: Path):
    # inktag_gui_default.inc preserves its common-tree ../power include. The SDK's
    # flattened common directory needs that same header beside it as well.
    return copy_if_changed(repo / "firmware/common/power/inktag_power.h",
                           common_out.parent / "power/inktag_power.h")


def retire_private_profile_depfiles(target: Path, private: set[Path], retired: list):
    """Preserve obsolete Pro depfiles before GNU make resolves removed headers.

    Ordinary objects and unrelated depfiles remain intact. The new base flags
    and selected sources cause normal incremental compilation, without an old
    .d rule demanding a private source which this profile intentionally omits.
    """
    gcc = target/'GCC'
    output = (gcc/'out/obj').resolve()
    private_names = {path.name for path in private}
    for dep in sorted(output.glob('*.d')):
        raw = dep.read_bytes()
        rule = raw.decode('utf-8').replace('\\\n', ' ').split('\n', 1)[0].split(':', 1)
        if len(rule) != 2:
            raise ValueError('Unknown Goodix compiler depfile: '+str(dep))
        tokens = [re.sub(r'\\(.)', r'\1', token).replace('$$', '$')
                  for token in re.findall(r'(?:\\.|[^\s\\])+', rule[1])]
        # Resolve only plausible private names: ordinary SDK headers need no
        # filesystem probes during this profile-specific dependency retirement.
        obsolete = sorted({(gcc/token).resolve() for token in tokens
                           if Path(token).name in private_names} & private)
        if not obsolete or any(path.is_file() for path in obsolete):
            continue
        digest = hashlib.sha256(raw).hexdigest()
        backup = target/'.inktag-retired-profile-deps'/digest/dep.name
        assert dep.resolve().is_relative_to(output) and backup.resolve().is_relative_to(target.resolve())
        backup.parent.mkdir(parents=True, exist_ok=True)
        if backup.exists() and backup.read_bytes() != raw:
            raise ValueError('Retired compiler depfile hash collision')
        backup.write_bytes(raw)
        if backup.read_bytes() != raw or dep.read_bytes() != raw:
            raise ValueError('Compiler depfile changed while preserving it')
        dep.unlink()
        retired.append(dict(path=str(dep), backup=str(backup), sha256=digest,
                            kind='obsolete-private-profile-dependency',
                            omittedDependencies=list(map(str, obsolete))))


def invalidate_config_dependent_outputs(target: Path):
    """Relink generated configurations without deleting object/depfile caches.

    Generated objects depend on Makefile, and -MD depfiles track configuration
    headers. Normal input changes therefore rebuild the affected objects; the
    maintained producer additionally validates their content certificate.
    """
    out = target / "GCC/out"
    for path in [
        out / "gcc_linker.lds",
        out / "lst/InkTag-gr5331_zkc21v.elf",
        out / "lst/InkTag-gr551x_zkc42v.elf",
        out / "lst/InkTag-gr551x_zkc42v-ota.elf",
    ]:
        if path.exists():
            path.unlink()

def invalidate_link_outputs(target: Path, target_name: str):
    """Force a relink without discarding unaffected compiled objects."""
    out = target / "GCC/out"
    for path in [
        out / f"{target_name}.bin",
        out / f"{target_name}.hex",
        out / "lst" / f"{target_name}.elf",
        out / "lst" / f"{target_name}.map",
        out / "lst" / f"{target_name}.s",
    ]:
        if path.exists():
            path.unlink()

def invalidate_stale_layout_outputs(target: Path, expected_app_addr: str):
    """Drop cached objects/linker output built for a different flash slot."""
    linker = target / "GCC/out/gcc_linker.lds"
    if not linker.exists():
        return False
    if expected_app_addr in linker.read_text(encoding="utf-8"):
        return False
    invalidate_config_dependent_outputs(target)
    return True

def patch_makefile(
    template_mk: Path,
    mk: Path,
    target_name: str,
    platform_define: str,
    layout: str,
    experimental_gt_mirror: bool | None = None,
    experimental_device_password: bool = True,
    experimental_panel_temperature_sample: bool = True,
    sdk_literal_fault_log: bool = False,
    panel_diagnostic: bool = False,
    clock_partial: bool = False,
    internal_images: bool = False,
    activation_dataflow: bool = False,
    code_fingerprints: bool = False,
    guard_repo: Path = None,
    clock_partial_ssd1683: bool = False,
    nfc_stream_pilot: bool | None = None,
    nfc_stream_diagnostics: bool = False,
    source_profile: str = 'pro',
):
    if source_profile not in ('pro', 'base'):
        raise ValueError('Explicit base/pro source profile required')
    if source_profile == 'base' and (activation_dataflow or code_fingerprints):
        raise ValueError('base has no private protection producers')
    if source_profile == 'base' and (clock_partial or clock_partial_ssd1683 or
                                   nfc_stream_pilot or nfc_stream_diagnostics or experimental_gt_mirror):
        raise ValueError('base cannot compile Pro NFC/clock implementations')
    if source_profile == 'base':
        experimental_gt_mirror = False
    if clock_partial_ssd1683 and (clock_partial or internal_images or
            platform_define != "INKTAG_GOODIX_GR5515" or layout != "ota"):
        raise ValueError("SSD1683 clock requires standard GR551x OTA only")
    if experimental_gt_mirror is None:
        experimental_gt_mirror = source_profile == "pro" and platform_define == "INKTAG_GOODIX_GR5515" and not internal_images
    if nfc_stream_pilot is None:
        nfc_stream_pilot = experimental_gt_mirror and not internal_images
    if nfc_stream_pilot and (platform_define != "INKTAG_GOODIX_GR5515" or internal_images or not experimental_gt_mirror):
        raise ValueError("NFC stream pilot requires the standard GR551x GT mirror")
    text = template_mk.read_text(encoding="utf-8")
    if internal_images:
        if platform_define != "INKTAG_GOODIX_GR5515" or layout != "ota":
            raise ValueError("Internal images require the distinct GR5513 OTA executable layout")
    text = text.replace("MAKE_TARGET_NAME :=  ble_app_template", f"MAKE_TARGET_NAME :=  {target_name}")
    text = text.replace("../Src/user/user_app.c  \\", "../Src/user/inktag_gr_app.c  \\\n../Src/user/inktag_gr_health.c  \\\n../Src/user/inktag_gr_settings_map.c  \\\n../Src/user/inktag_gr_settings_store.c  \\\n../Src/user/inktag_gr_device_info_qr.c  \\\n../Src/user/inktag_gr_activation.c  \\\n../Src/user/inktag_gr_service.c  \\\n../Src/user/inktag_gr_external_flash.c  \\\n../Src/user/inktag_gr_image_store.c  \\\n../Src/user/inktag_gr_swap.c  \\\n../Src/user/epd_driver_goodix.c  \\\n../Src/inktag_common/inktag_recovery.c  \\\n../Src/inktag_common/inktag_epd_config.c  \\\n../Src/inktag_common/inktag_epd_uc.c  \\\n../Src/inktag_common/inktag_epd_jd.c  \\\n../Src/inktag_common/inktag_epd_ssd.c  \\\n../Src/inktag_common/inktag_gui.c  \\\n../Src/inktag_common/inktag_calendar.c  \\\n../Src/inktag_common/inktag_gui_fonts.c  \\\n../Src/inktag_common/u8g2_font.c  \\\n../Src/inktag_common/Adafruit_GFX.c  \\")
    text = text.replace(
        "../Src/user/inktag_gr_image_store.c  " + chr(92),
        "../Src/user/inktag_gr_image_store.c  " + chr(92) + "\n"
        "../Src/user/inktag_gr_image_io.c  " + chr(92) + "\n"
        "../Src/inktag_common/inktag_image_upload_v5.c  " + chr(92),
    )
    text = text.replace(
        "../Src/user/inktag_gr_settings_store.c  " + chr(92),
        "../Src/user/inktag_gr_settings_store.c  " + chr(92) + "\n"
        "../Src/user/inktag_gr_board_identity.c  " + chr(92),
        1,
    )
    text = text.replace(
        "../Src/user/inktag_gr_board_identity.c  " + chr(92),
        "../Src/user/inktag_gr_board_identity.c  " + chr(92) + "\n" +
        "\n".join("../Src/" + name + "  " + chr(92) for name in (
            "user/inktag_gr_pin_config.c", "user/inktag_gr_pin_storage.c",
            "user/inktag_gr_pin_routes.c", "user/inktag_gr_screen.c",
            "inktag_common/inktag_pin_config.c", "inktag_common/inktag_aux_io.c")), 1)
    text = text.replace(
        "../Src/user/inktag_gr_settings_store.c  " + chr(92),
        "../Src/user/inktag_gr_settings_store.c  " + chr(92) + "\n"
        "../Src/user/inktag_gr_nvds_guard.c  " + chr(92), 1,
    )
    text = text.replace(
        "../Src/user/inktag_gr_nvds_guard.c  " + chr(92),
        "../Src/user/inktag_gr_nvds_guard.c  " + chr(92) + "\n" +
        "\n".join("../Src/" + name + "  " + chr(92) for name in (
            "user/inktag_gr_ble_address.c", "inktag_common/inktag_ble_address.c",
            "inktag_common/inktag_ble_address_record.c", "inktag_common/inktag_ble_address_uid_compact.c")), 1)
    text = text.replace(
        "../Src/user/inktag_gr_activation.c  \\",
        "../Src/user/inktag_gr_activation.c  \\\n"
        "../Src/user/inktag_gr_gui_assets.c  \\",
        1,
    )
    text = text.replace(
        "../Src/inktag_common/inktag_gui.c  \\",
        "../Src/inktag_common/inktag_gui.c  \\\n"
        "../Src/inktag_common/inktag_gui_asset_package.c  \\",
        1,
    )
    text = text.replace("../Src/inktag_common/inktag_epd_uc.c", "../Src/inktag_common/inktag_epd_screen.c  \\\n../Src/inktag_common/inktag_epd_runtime.c  \\\n../Src/inktag_common/inktag_epd_commands.c  \\\n../Src/inktag_common/inktag_page_cycle.c  \\\n../Src/inktag_common/inktag_epd_uc.c", 1)
    text = text.replace(
        "../Src/user/inktag_gr_swap.c",
        "../Src/user/inktag_gr_dfu_diagnostics.c  " + chr(92) +
        "\n../Src/user/inktag_gr_swap.c",
        1,
    )
    text = text.replace("../Src/inktag_common/Adafruit_GFX.c",
                        "../Src/inktag_common/Adafruit_GFX.c  \\\n"
                        "../Src/inktag_common/inktag_findmy.c  \\\n"
                        "../Src/inktag_common/inktag_findmy_public.c  \\\n"
                        "../Src/inktag_common/inktag_input.c  \\\n"
                        "../Src/inktag_common/inktag_input_simulation.c  \\\n"
                        "../Src/inktag_common/inktag_nfc_command.c  \\\n"
                        "../Src/inktag_common/inktag_ndef.c  \\\n"
                        "../Src/inktag_common/inktag_status.c  \\\n"
                        "../Src/inktag_common/inktag_power.c  \\\n"
                        "../Src/inktag_common/inktag_soak_telemetry.c  \\\n"
                        "../Src/inktag_common/inktag_access.c  \\\n"
                        "../Src/inktag_common/inktag_brand_overlay.c  \\\n"
                        "../Src/inktag_common/inktag_image_quota.c  \\\n"
                        "../Src/inktag_common/inktag_activation.c  \\\n"
                        "../Src/inktag_common/inktag_sha256.c  \\\n"
                        "../Src/inktag_common/inktag_protocol_core.c  \\\n"
                        "../Src/inktag_common/inktag_packbits.c  \\\n"
                        "../Src/inktag_common/inktag_lzo1x.c  \\\n"
                        "../Src/inktag_common/inktag_display_settings.c  \\\n"
                        "../Src/inktag_common/inktag_settings_records.c  \\\n"
                        "../Src/inktag_common/inktag_settings_journal.c  \\\n"
                        "../Src/inktag_common/inktag_lifecycle.c", 1)
    text = text.replace("../../../../../drivers/src/app_uart_dma.c  \\", "../../../../../drivers/src/app_uart_dma.c  \\\n../../../../../drivers/src/app_aon_wdt.c  \\\n../../../../../drivers/src/app_spi.c  \\")
    text = text.replace(
        "../Src/inktag_common/inktag_ndef.c  " + chr(92),
        "../Src/inktag_common/inktag_ndef.c  " + chr(92) + "\n"
        "../Src/inktag_common/inktag_type2_uri_store.c  " + chr(92) + "\n"
        "../Src/inktag_common/inktag_nfc_link.c  " + chr(92),
        1,
    )
    text = text.replace(
        "../Src/inktag_common/inktag_sha256.c  " + chr(92),
        "../Src/inktag_common/inktag_sha256.c  " + chr(92) + "\n"
        "../Src/inktag_common/inktag_device_password.c  " + chr(92) + "\n"
        "../Src/inktag_common/inktag_device_password_store.c  " + chr(92),
        1,
    )
    text = text.replace(
        "../../../../../drivers/src/app_aon_wdt.c  \\",
        "../../../../../components/libraries/hal_flash/hal_flash.c  \\\n"
        "../../../../../drivers/src/app_aon_wdt.c  \\",
        1,
    )
    text = text.replace(
        "../../../../../drivers/src/app_spi.c  \\",
        "../../../../../drivers/src/app_spi.c  \\\n"
        "../../../../../components/libraries/ble/ble_time/ble_time.c  \\",
        1,
    )
    # Keep the common NFC command/configuration implementation in both Goodix
    # application artifacts.  Exact hardware access remains platform/board
    # gated inside the driver, so the known no-NFC ZKC21V stays disabled while
    # a future qualified ZKC21V NFC assembly does not require a new protocol
    # or application-family split.
    text = text.replace(
        "../Src/user/inktag_gr_app.c  \\",
        "../Src/user/inktag_gr_app.c  \\\n"
        "../Src/inktag_common/inktag_clock_rate.c  \\\n"
        "../Src/user/inktag_gr_findmy.c  \\\n"
        "../Src/user/inktag_gr_find_public_store.c  \\\n"
        "../Src/user/inktag_gr_p224_public.c  \\\n"
        "../Src/user/inktag_gr_nfc.c  \\",
        1,
    )
    if platform_define == "INKTAG_GOODIX_GR5515":
        if experimental_gt_mirror:
            text = text.replace(
                "../Src/user/inktag_gr_nfc.c  " + chr(92),
                "../Src/user/inktag_gr_nfc.c  " + chr(92) + "\n"
                "../Src/inktag_common/inktag_gt_mirror.c  " + chr(92) + "\n"
                "../Src/inktag_common/inktag_nfc_volatile_command.c  " + chr(92),
                1,
            )
    # The button-only adapter is shared by both SDK families. No dedicated
    # app_pwm source or PWM configuration is part of this build.
    text = text.replace(
        "../Src/user/inktag_gr_app.c  " + chr(92),
        "../Src/user/inktag_gr_app.c  " + chr(92) + "\n" +
        "../Src/user/inktag_gr_aux_io.c  " + chr(92), 1)
    include_anchor = "../../../../../components/libraries/hci_uart  \\\n"
    if include_anchor not in text:
        raise SystemExit("cannot add Goodix activation crypto include path")
    text = text.replace(
        include_anchor,
        include_anchor + "../../../../../components/libraries/ble/ble_time  \\\n",
        1,
    )
    crypto_include = "../../../../../components/libraries/app_crypto  \\\n"
    text = text.replace(include_anchor, include_anchor + crypto_include, 1)
    ota_layout = layout == "ota"
    if ota_layout:
        text = text.replace(
            "../../../../../drivers/src/app_spi.c  \\",
            "../../../../../drivers/src/app_spi.c  \\\n"
            "../../../../../components/libraries/dfu_port/dfu_port.c  \\\n"
            "../../../../../components/profiles/otas/otas.c  \\",
        )
        if platform_define == "INKTAG_GOODIX_GR5515":
            text = text.replace(
                "../../../../../components/libraries/dfu_port/dfu_port.c  \\",
                "../../../../../drivers/src/app_qspi.c  \\\n"
                "../../../../../drivers/src/app_qspi_dma.c  \\\n"
                "../../../../../drivers/src/app_spi_dma.c  \\\n"
                "../../../../../drivers/src/app_graphics_qspi.c  \\\n"
                "../../../../../components/libraries/hci_uart/hci_uart.c  \\\n"
                "../../../../../components/libraries/dfu_port/dfu_port.c  \\",
            )
            text = text.replace(
                "../../../../../components/profiles/otas/otas.c  \\",
                "../../../../../components/profiles/otas/otas.c  \\\n"
                "../../../../../components/drivers_ext/gr55xx/gr55xx_spi_flash.c  \\",
            )
    text = text.replace("../Src/user  \\", "../Src/user  \\\n../Src/inktag_common  \\")
    text = text.replace(
        "../Src/inktag_common/inktag_gui.c  \\",
        "../Src/inktag_common/inktag_gui.c  \\\n"
        "../Src/inktag_common/inktag_gui_device_qr_asset.c  \\",
    )
    epd_defines = list(GOODIX_APPLICATION_EPD_DEFINES)
    if source_profile == 'base':
        epd_defines = [define.replace('=1', '=0') if define.split('=')[0] in
            ('INKTAG_EPD_ENABLE_OSPREY_E6', 'INKTAG_EPD_ENABLE_JD79665CA') else define
            for define in epd_defines]
    extra_defines = "".join(f"{define}  \\\n" for define in epd_defines)
    if activation_dataflow:
        if platform_define not in ('INKTAG_GOODIX_GR5515','INKTAG_GOODIX_GR5331'):
            raise ValueError("Activation dataflow requires a supported GR platform")
        extra_defines += "INKTAG_ACTIVATION_RESULT_DATAFLOW=1  " + chr(92) + "\n"
    gui_heap_size = 32768 if platform_define == "INKTAG_GOODIX_GR5515" else 24576
    if platform_define == "INKTAG_GOODIX_GR5515":
        # GR551x links the newlib heap from startup_gr55xx.s.  A C-only
        # definition changes GUI's budget calculation without reserving the
        # corresponding RAM, so the startup assembler must see the same size.
        text = text.replace(
            "PRJ_ASM_MICRO_DEFINES :=   \\",
            f"PRJ_ASM_MICRO_DEFINES :=   \\\n__HEAP_SIZE={gui_heap_size}  \\",
        )
        text = text.replace(
            "ASMFLAGS += $(COMMON_COMPILE_FLAGS)",
            "ASMFLAGS += $(COMMON_COMPILE_FLAGS)\n"
            "ASMFLAGS += -x assembler-with-cpp",
        )
    # Only a compiled NFC image path consumes the temperature option.
    temperature_option = int(source_profile == 'pro' and
                             platform_define == 'INKTAG_GOODIX_GR5515' and not internal_images and nfc_stream_pilot)
    extra_defines += (
        f"INKTAG_EPD_TEMPERATURE_OPTION={temperature_option}  \\\n"
        f"INKTAG_GR_NFC_PACKAGED={int(source_profile == 'pro')}  \\\n"
        "INKTAG_GUI_COOPERATIVE_YIELD=1  \\\n"
        "INKTAG_GUI_ATOMIC_FONT_ASSETS=1  \\\n"
        "INKTAG_GUI_EMBEDDED_FONT_FALLBACK=1  \\\n"
        "INKTAG_GUI_ASSET_READ_FAILURE_AWARE=1  \\\n"
        "INKTAG_GUI_ROLE_FONT_ASSETS=1  \\\n"
        "INKTAG_GUI_NATIVE_FONT_ASSETS=1  \\\n"
        "INKTAG_GUI_FACTORY_FONT_LAYOUT=1  \\\n"
        "INKTAG_DEVICE_INFO_QR_SHARED_ASSET=1  \\\n"
        "INKTAG_ENABLE_SOAK_TELEMETRY=1  \\\n"
        "INKTAG_AUX_RESERVED_ENABLED=0  \\\n"
        "INKTAG_TRANSFER_PACKBITS_ENABLED=0  \\\n"
        "INKTAG_TRANSFER_LZO1X_ENABLED=1  \\\n"
        "INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES=35u  \\\n"
        f"__HEAP_SIZE={gui_heap_size}  \\\n"
    )
    if experimental_gt_mirror:
        extra_defines += "INKTAG_GR_EXPERIMENTAL_NFC_SRAM_COMMANDS=1  " + chr(92) + "\n"
        extra_defines += "INKTAG_NFC_EXPLICIT_TIMEZONE=1  " + chr(92) + "\n"
    if nfc_stream_diagnostics:
        if not nfc_stream_pilot: raise ValueError("GT diagnostics require the isolated NFC stream pilot")
        extra_defines += "INKTAG_GR_NFC_STREAM_DIAGNOSTICS=1  " + chr(92) + "\n"
    if nfc_stream_pilot:
        extra_defines += "INKTAG_GR_NFC_STREAM_PILOT=1  " + chr(92) + "\n"
        extra_defines += "INKTAG_NFC_R16_STREAMING=1  " + chr(92) + "\n"
        extra_defines += "INKTAG_NFC_TIMED_DISPLAY=1  " + chr(92) + "\n"
        anchor = "../Src/user/inktag_gr_nfc.c  " + chr(92)
        names = ("inktag_gt_stream.c", "inktag_gt_pipe.c", "inktag_nfc_fmsc.c")
        text = text.replace(anchor, anchor + "\n" + "".join("../Src/inktag_common/" + name + "  " + chr(92) + "\n" for name in names).rstrip("\n"), 1)
    if internal_images:
        extra_defines += "INKTAG_GR5513_INTERNAL_IMAGES=1  " + chr(92) + "\n"
    if experimental_device_password:
        extra_defines += "INKTAG_GR_EXPERIMENTAL_DEVICE_PASSWORD=1  " + chr(92) + "\n"
    if experimental_panel_temperature_sample:
        extra_defines += "INKTAG_GR_EXPERIMENTAL_PANEL_TEMPERATURE_SAMPLE=1  " + chr(92) + "\n"
    if sdk_literal_fault_log:
        extra_defines += "INKTAG_GR_SDK_LITERAL_FAULT_LOG=1  " + chr(92) + "\n"
    if panel_diagnostic:
        extra_defines += "INKTAG_PANEL_DIAGNOSTIC_ENABLED=1  " + chr(92) + "\n"
        text = text.replace("../Src/inktag_common/inktag_epd_uc.c", "../Src/inktag_common/inktag_panel_diagnostic.c  " + chr(92) + "\n../Src/inktag_common/inktag_epd_uc.c", 1)
    if clock_partial or clock_partial_ssd1683:
        extra_defines += "INKTAG_GR_CLOCK_PARTIAL=1  " + chr(92) + "\n"
    if clock_partial_ssd1683:
        extra_defines += "INKTAG_GR_CLOCK_PARTIAL_SSD1683=1  " + chr(92) + "\n"
    if platform_define == "INKTAG_GOODIX_GR5331":
        # GR5331's linker places newlib's `end` at the bottom of the explicit
        # system heap.  Reserve the same 24 KiB that bounds GUI allocation so
        # the framebuffer can never grow into its 8 KiB stack.
        extra_defines += "SYSTEM_HEAP_SIZE=24576  \\\n"
    text = text.replace("PRJ_C_MICRO_DEFINES :=   \\", f"PRJ_C_MICRO_DEFINES :=   \\\nINKTAG_GOODIX  \\\n{platform_define}  \\\nINKTAG_GUI_RAW_VIEWPORT  \\\nINKTAG_DEVICE_SETTINGS_ENABLED=1  \\\n{extra_defines}FDS_ENABLED=0  \\")
    if ota_layout:
        text = text.replace(
            f"{platform_define}  \\",
            f"{platform_define}  \\\nINKTAG_GOODIX_OTA  \\\nINKTAG_SWAP_EXECUTE_FROM_RAM  \\",
        )
    if platform_define == "INKTAG_GOODIX_GR5331" and layout == "ota":
        text = text.replace(
            "LINKER_SCRIPT_TMPL = ../../../../../platform/soc/linker/gcc/gcc_linker.lds",
            "LINKER_SCRIPT_TMPL = gcc_linker_inktag_ota.lds",
        )
    # Keep all InkTag features while making the board builds size-oriented.
    # The BLE controller implementation remains in the vendor archive/ROM; LTO
    # only sees the application objects and can still link non-LTO SDK objects.
    # Variables remain overridable for isolated regression builds.
    text = text.replace("CFLAGS += -O2", "")
    cflags_anchor = "CFLAGS += $(COMMON_COMPILE_FLAGS)"
    if cflags_anchor not in text:
        raise SystemExit(f"cannot add Goodix size flags: {template_mk}")
    text = text.replace(
        cflags_anchor,
        cflags_anchor
        + "\nINKTAG_OPT_FLAGS ?= -Os"
        + "\nINKTAG_LTO_FLAGS ?= -flto -fuse-linker-plugin"
        + "\nCFLAGS += $(INKTAG_OPT_FLAGS) $(INKTAG_LTO_FLAGS)",
        1,
    )
    if platform_define == "INKTAG_GOODIX_GR5331":
        # All format strings in the linked InkTag/GR5331 sources are
        # integer/string-only. Use full newlib for the SDK/ROM ABI, while
        # routing all three formatting entry points to its integer variants.
        # Redirecting only snprintf is counterproductive because app_log also
        # references sprintf and would retain both formatter engines.
        text = text.replace(
            "CFLAGS += $(INKTAG_OPT_FLAGS) $(INKTAG_LTO_FLAGS)",
            "CFLAGS += $(INKTAG_OPT_FLAGS) $(INKTAG_LTO_FLAGS) "
            "-Dsprintf=siprintf -Dsnprintf=sniprintf "
            "-Dvsnprintf=vsniprintf",
            1,
        )
    ldflags_anchor = "LDFLAGS += -Wl,--gc-sections"
    if ldflags_anchor not in text:
        raise SystemExit(f"cannot add Goodix link size flags: {template_mk}")
    size_link_flags = (
        "\nLDFLAGS += $(INKTAG_LTO_FLAGS)"
        "\nLDFLAGS += -Wl,--undefined=svc_user_handler"
    )
    if platform_define == "INKTAG_GOODIX_GR5515":
        size_link_flags += "\nLDFLAGS += --specs=nano.specs"
    else:
        # GR5331 hardware regression: the controller reports a successful
        # asynchronous ADV_START event with nano.specs, but transmits no
        # observable packets. The same SDK template and image advertise at
        # -52 dBm as soon as the vendor full-newlib link is restored.
        size_link_flags += "\n# GR5331: keep the vendor full-newlib link; nano.specs breaks radio advertising."
    text = text.replace(ldflags_anchor, ldflags_anchor + size_link_flags, 1)
    text = text.replace("IS_WIN_OS :=  true", "IS_WIN_OS :=  false")
    # Vendor projects already compile with -MD but never include the generated
    # dependency files. Without this, changing inktag_version.h, a board
    # header, or a driver header leaves stale objects in an apparently
    # successful incremental build. The OTA manifest can then advertise a new
    # version while the application still reports and runs the old one.
    object_anchor = "OBJ_ADJUST \t= $(patsubst %.o,$(BUILD_OBJ)/%.o,$(notdir $(OBJ)))"
    dependency_include = (
        object_anchor
        + "\n\n.DEFAULT_GOAL := all"
        + "\n-include $(wildcard $(BUILD_OBJ)/*.d)"
        # Compiler/assembler defines live in this generated Makefile. Header
        # .d files cannot track them, and GCC/out invalidation cannot reach a
        # caller's BUILD override. Preserve source-first implicit rules while
        # making every output directory consume changed build configuration.
        + "\n$(OBJ_ADJUST): Makefile"
    )
    if object_anchor not in text:
        raise SystemExit(f"cannot enable generated header dependencies: {template_mk}")
    text = text.replace(object_anchor, dependency_include, 1)
    text = text.replace(
        "$(BUILD_LST)/$(MAKE_TARGET_NAME).elf: $(OBJ_ADJUST)",
        "$(BUILD_LST)/$(MAKE_TARGET_NAME).elf: $(OBJ_ADJUST) $(LINK_SCRIPT) $(PATCH_FILE)",
        1,
    )
    if code_fingerprints:
        from goodix_code_guards import guard_makefile, guard_linker, layout_choice
        family = ('gr5331_zkc21v' if platform_define == 'INKTAG_GOODIX_GR5331' else
                  ('gr5513_zkel_internal' if internal_images else 'gr551x_zkc42v'))
        text = guard_makefile(text, guard_repo, 'app',family)
        if source_profile == 'base':
            text = text.replace('--role app', '--role app --source-profile base')
    profile_root = guard_repo or Path(__file__).resolve().parents[2]
    text += '\n# Explicit source profile; part of the generated cache identity.\n'
    text += 'CFLAGS += ' + ' '.join('-D'+k+'='+str(v) for k,v in
        profile_defines(profile_root, profile=source_profile).items()) + '\n'
    if source_profile == 'base':
        anchor = '../Src/user/inktag_gr_image_store.c  ' + chr(92)
        if anchor not in text:
            raise ValueError('Missing native image-store source anchor')
        text = text.replace(anchor, anchor+'\n../Src/inktag_common/inktag_resource_name.c  '+chr(92), 1)
        for line in tuple(text.splitlines()):
            match = re.fullmatch(r'\s*\.\./Src/(user|inktag_common)/(\w+\.c)\s*\\?\s*', line)
            if not match:
                continue
            if match[1] == 'user':
                # main.c and other SDK template units are generated from the
                # owned SDK, not looked up in the repository's platform tree.
                if match[2] not in COMMON_SOURCES:
                    continue
                selected = platform_profile_source(profile_root, match[2], source_profile)
            else:
                logicals = [f'firmware/common/{directory}/{match[2]}' for directory,names in (
                    ('epd',EPD_SOURCES),('gui',GUI_SOURCES),('status',STATUS_SOURCES),
                    ('power',POWER_SOURCES),('health',HEALTH_SOURCES),('input',INPUT_SOURCES),
                    ('ble',BLE_SOURCES),('protocol',PROTOCOL_SOURCES),('lifecycle',LIFECYCLE_SOURCES),
                    ('storage',STORAGE_SOURCES),('security',SECURITY_SOURCES),('ota',OTA_SOURCES))
                    if match[2] in names]
                selected = source_path(profile_root, logicals[0], profile=source_profile) if logicals else True
            if selected is None:
                text = text.replace(line+'\n', '')
    text = compact_diagnostic_makefile(text,guard_repo or Path(__file__).resolve().parents[2])
    return write_if_changed(mk, text)

def patch_linker(template_lds: Path, linker: Path, platform: str, layout: str,
                 internal_images: bool = False, code_fingerprints: bool = False,
                 guard_layout: int = 0):
    text = template_lds.read_text(encoding="utf-8")
    if platform == "gr5515":
        if layout == "ota":
            text = text.replace(
                "FLASH (rx) : ORIGIN = 0x01002000, LENGTH = (0x00800000 -0x00002000)",
                "FLASH (rx) : ORIGIN = 0x01020000, LENGTH = 0x00052000",
            )
            if internal_images:
                text = text.replace("LENGTH = 0x00052000", "LENGTH = 0x00044000")
        else:
            text = text.replace(
                "FLASH (rx) : ORIGIN = 0x01002000, LENGTH = (0x00800000 -0x00002000)",
                "FLASH (rx) : ORIGIN = 0x0100A000, LENGTH = (0x00800000 -0x0000A000)",
            )
        # The qualified ZKC42VM reports SYS_DEV_SRAM_128K at runtime. Addresses
        # above 0x3001ffff alias while those banks are excluded by the SDK, so
        # both the initial stack and every application allocation must stay in
        # the low 128 KiB physical SRAM window.
        text = text.replace(
            "RAM (rwx)  : ORIGIN = 0x30000000 +0x4000, LENGTH = (0x00040000 -0x4000)",
            "RAM (rwx)  : ORIGIN = 0x30000000 +0x4000, LENGTH = (0x00020000 -0x4000)",
        )
    clean_anchor = '        *(.rodata*)'
    if text.count(clean_anchor) != 1:
        raise SystemExit('Unknown GR551x read-only linker shape')
    text = text.replace(clean_anchor,
        '        KEEP(*(.rodata.inktag_gr_clean_identity_info*))\n'+clean_anchor, 1)
    retained_anchor = "    } > RAM\n    \n    .heap (COPY):"
    retained_section = """    } > RAM

    .inktag_noinit (NOLOAD) :
    {
        . = ALIGN(4);
        __inktag_noinit_start__ = .;
        KEEP(*(.inktag_noinit))
        . = ALIGN(4);
        __inktag_noinit_end__ = .;
    } > RAM

    .heap (COPY):"""
    if retained_anchor not in text:
        raise SystemExit(f"cannot reserve InkTag retained RAM in {template_lds}")
    text = text.replace(retained_anchor, retained_section, 1)
    if code_fingerprints:
        from goodix_code_guards import guard_makefile, guard_linker, layout_choice
        text = guard_linker(text, guard_layout)
    return write_if_changed(linker, text)

def patch_main(template_main_c: Path, main_c: Path):
    text = template_main_c.read_text(encoding="utf-8")
    text = text.replace(
        "int main (void)\n{\n    // Initialize user peripherals.",
        "int main (void)\n{\n    inktag_gr_boot_stage_set(0x47524D30); // GRM0\n\n    // Initialize user peripherals.",
    )
    text = text.replace(
        "    app_periph_init();\n\n    // Initialize ble stack.",
        "    app_periph_init();\n    inktag_gr_hw_info_capture();\n    inktag_gr_boot_stage_set(0x47524D31); // GRM1\n\n    // Initialize ble stack.",
    )
    text = text.replace(
        "    ble_stack_init(ble_evt_handler, &heaps_table);",
        "    inktag_gr_boot_stage_set(0x47524D32); // GRM2\n    ble_stack_init(ble_evt_handler, &heaps_table);\n    inktag_gr_boot_irq_state_capture();\n    if (inktag_gr_boot_stage_get() == 0x47524D32) {\n        inktag_gr_boot_stage_set(0x47524D33); // GRM3\n    }",
    )
    old = "        app_log_flush();\n        pwr_mgmt_schedule();"
    new = (
        "        app_log_flush();\n"
        "        inktag_gr_app_process();\n"
        "        inktag_gr_app_before_sleep();\n"
        "        pwr_mgmt_schedule();\n"
        "        inktag_gr_app_after_sleep();"
    )
    if old not in text and new not in text:
        raise SystemExit(f"cannot patch Goodix main loop: {template_main_c}")
    text = text.replace(old, new)
    return write_if_changed(main_c, text)

def patch_periph(template_periph_c: Path, periph_c: Path, platform: str, layout: str):
    text = template_periph_c.read_text(encoding="utf-8")
    template_addr = (
        "static const uint8_t s_bd_addr[SYS_BD_ADDR_LEN] = "
        "{0x0f, 0x00, 0xcf, 0x3e, 0xcb, 0xea};"
    )
    if template_addr not in text:
        raise SystemExit(f"cannot locate Goodix template Bluetooth address: {template_periph_c}")
    text = text.replace(template_addr, "/* Address is read/applied after stack init; never written at boot. */")
    text = text.replace("    SYS_SET_BD_ADDR(s_bd_addr);\n    board_init();",
                        "    board_init();")
    idle_power_mode = "pwr_mgmt_mode_set(PMR_MGMT_IDLE_MODE);"
    if idle_power_mode not in text:
        raise SystemExit(
            f"Goodix application template no longer selects the qualified "
            f"event-aware idle mode: {template_periph_c}"
        )
    if platform == "gr5515":
        # Keep the vendor template's PMR_MGMT_IDLE_MODE. The non-RTOS main
        # loop drains InkTag work before pwr_mgmt_schedule(), which then owns
        # the interrupt-aware wait. Offline timed sleep is retired separately.
        text = text.replace(
            "    board_init();",
            "    ble_rf_tx_mode_set(BLE_RF_TX_MODE_LP_MODE);\n"
            "    ble_rf_match_circuit_set(BLE_RF_MATCH_CIRCUIT_25OHM);\n"
            "    board_init();",
        )
        if layout == "ota":
            text = text.replace(
                '#include "custom_config.h"',
                '#include "custom_config.h"\n#include "dfu_port.h"\n#include "app_error.h"',
            )
            text = text.replace(
                "    board_init();",
                "    board_init();\n"
                "    /* Application role only requests entry to fixed BLE Recovery. */\n"
                "    extern dfu_pro_callback_t g_inktag_gr_dfu_callbacks;\n"
                "    APP_ERROR_CHECK(dfu_port_init(NULL, FLASH_START_ADDR + 0x20000, &g_inktag_gr_dfu_callbacks));",
                1,
            )
    else:
        # GR5331 keeps the vendor RF/power baseline. Identity is handled by
        # the same read-only typed adapter as GR551x after stack initialization.
        if layout == "ota":
            text = text.replace(
                '#include "custom_config.h"',
                '#include "custom_config.h"\n#include "dfu_port.h"\n#include "app_error.h"',
            )
            text = text.replace(
                "    board_init();",
                "    board_init();\n"
                "    /* Application role only requests entry to fixed BLE Recovery. */\n"
                "    extern dfu_pro_callback_t g_inktag_gr_dfu_callbacks;\n"
                "    APP_ERROR_CHECK(dfu_port_init(NULL, FLASH_START_ADDR + 0x20000, &g_inktag_gr_dfu_callbacks));",
                1,
            )
    # SK/DK demo LEDs and keys are not exact InkTag PCB facts on either family.
    # GR533x's board_init also owns AON3/4 outputs and AON5/7 pulled-up key IRQs;
    # an IRQ can start its unrelated 10 ms polling timer. Product indicators
    # and qualified inputs are initialized by InkTag's board routes instead.
    # This patches App initialization only, never the separate Recovery path.
    if text.count("    board_init();") != 1:
        raise SystemExit(f"unexpected Goodix App board initializer: {template_periph_c}")
    text = text.replace("    board_init();", "    bsp_log_init();", 1)
    return write_if_changed(periph_c, text)

def patch_gr5515_soc(soc_c: Path):
    text = soc_c.read_text(encoding="utf-8")
    if "INKTAG_SOC_STAGE" in text:
        return False

    include_anchor = '#include "app_pwr_mgmt.h"\n'
    if include_anchor not in text:
        raise SystemExit(f"cannot add GR5515 soc diagnostics: {soc_c}")
    text = text.replace(
        include_anchor,
        include_anchor
        + "\n#ifdef INKTAG_GOODIX_GR5515\n"
        + "extern void inktag_gr_boot_stage_set(uint32_t stage);\n"
        + "#define INKTAG_SOC_STAGE(_stage) inktag_gr_boot_stage_set((_stage))\n"
        + "#else\n"
        + "#define INKTAG_SOC_STAGE(_stage) ((void)0)\n"
        + "#endif\n",
        1,
    )

    replacements = [
        ("void soc_init(void)\n{\n    ultra_deep_sleep_wakeup_handle();",
         "void soc_init(void)\n{\n    INKTAG_SOC_STAGE(0x47525130); /* GRQ0: soc_init entry */\n    ultra_deep_sleep_wakeup_handle();\n    INKTAG_SOC_STAGE(0x47525131); /* GRQ1 */"),
        ("    patch_init();\n\n#if defined (__CC_ARM)",
         "    patch_init();\n    INKTAG_SOC_STAGE(0x47525132); /* GRQ2 */\n\n#if defined (__CC_ARM)"),
        ("    if (!hal_flash_init())\n    {",
         "    INKTAG_SOC_STAGE(0x47525133); /* GRQ3: entering hal_flash_init */\n    if (!hal_flash_init())\n    {\n        INKTAG_SOC_STAGE(0x4752513F); /* GRQ?: hal_flash_init failed */"),
        ("    platform_flash_enable_quad();\n\n    platform_init();\n    hal_init();",
         "    INKTAG_SOC_STAGE(0x47525134); /* GRQ4: hal_flash_init returned */\n    platform_flash_enable_quad();\n    INKTAG_SOC_STAGE(0x47525135); /* GRQ5 */\n\n    platform_init();\n    INKTAG_SOC_STAGE(0x47525136); /* GRQ6 */\n    hal_init();\n    INKTAG_SOC_STAGE(0x47525137); /* GRQ7 */"),
    ]
    for old, new in replacements:
        if old not in text:
            raise SystemExit(f"cannot patch GR5515 soc diagnostics at {old!r}: {soc_c}")
        text = text.replace(old, new, 1)
    return write_if_changed(soc_c, text)

def patch_gr5515_flash_capacity(soc_c: Path):
    text = soc_c.read_text(encoding="utf-8")
    if "INKTAG_GR5515_FLASH_SIZE_BYTES" in text:
        old = "#define INKTAG_GR5515_FLASH_SIZE_BYTES 0x00100000u"
        if "#if defined(INKTAG_GR5513_INTERNAL_IMAGES)" not in text:
            if old not in text:
                raise SystemExit(f"Unknown GR551x capacity override: {soc_c}")
            text = text.replace(old,
                "#if defined(INKTAG_GR5513_INTERNAL_IMAGES)\n"
                "#define INKTAG_GR5515_FLASH_SIZE_BYTES 0x00080000u\n"
                "#else\n" + old + "\n#endif",1)
            return write_if_changed(soc_c,text)
        return False

    include_anchor = '#include "app_pwr_mgmt.h"\n'
    if include_anchor not in text:
        raise SystemExit(f"cannot add GR5515 Flash capacity override: {soc_c}")
    text = text.replace(
        include_anchor,
        include_anchor
        + "\n#ifdef INKTAG_GOODIX_GR5515\n"
        + "extern exflash_handle_t g_exflash_handle;\n"
        + "#if defined(INKTAG_GR5513_INTERNAL_IMAGES)\n"
        + "#define INKTAG_GR5515_FLASH_SIZE_BYTES 0x00080000u\n"
        + "#else\n#define INKTAG_GR5515_FLASH_SIZE_BYTES 0x00100000u\n#endif\n"
        + "#endif\n",
        1,
    )

    init_anchor = "    INKTAG_SOC_STAGE(0x47525134); /* GRQ4: hal_flash_init returned */\n"
    if init_anchor not in text:
        raise SystemExit(f"cannot patch GR5515 Flash capacity after initialization: {soc_c}")
    text = text.replace(
        init_anchor,
        init_anchor
        + "#ifdef INKTAG_GOODIX_GR5515\n"
        + "    /* Keep the established 1 MiB logical XIP window for the shared\n"
        + "     * Swap/NVDS address map.  Some ZKC42VM batches physically carry\n"
        + "     * only 512 KiB and alias its upper half; image-store code checks\n"
        + "     * the JEDEC capacity before exposing any record sectors. */\n"
        + "    g_exflash_handle.flash_size = INKTAG_GR5515_FLASH_SIZE_BYTES;\n"
        + "#endif\n",
        1,
    )
    return write_if_changed(soc_c, text)

def patch_gr5515_platform_init(soc_c: Path):
    text = soc_c.read_text(encoding="utf-8")
    if "GRR0: platform_init entry" in text:
        return False

    replacements = [
        ("void platform_init(void)\n{\n    /* if BLE not fully power off",
         "void platform_init(void)\n{\n    INKTAG_SOC_STAGE(0x47525230); /* GRR0: platform_init entry */\n    /* if BLE not fully power off"),
        ("    /* enable protection. */\n    platform_init_push();",
         "    INKTAG_SOC_STAGE(0x47525231); /* GRR1: reset/NVIC cleanup done */\n\n    /* enable protection. */\n    platform_init_push();"),
        ("    /* nvds module init process. */\n    nvds_setup();",
         "    INKTAG_SOC_STAGE(0x47525232); /* GRR2: protection/RAM power done */\n\n    /* nvds module init process. */\n    nvds_setup();\n    INKTAG_SOC_STAGE(0x47525233); /* GRR3: NVDS setup done */"),
        ("    /* platform init process. */\n    platform_sdk_init();",
         "    INKTAG_SOC_STAGE(0x47525234); /* GRR4: clocks initialized */\n\n    /* platform init process. */\n    platform_sdk_init();\n    INKTAG_SOC_STAGE(0x47525235); /* GRR5: platform SDK initialized */"),
        ("    system_pmu_init((mcu_clock_type_t)SYSTEM_CLOCK);",
         "    system_pmu_init((mcu_clock_type_t)SYSTEM_CLOCK);\n    INKTAG_SOC_STAGE(0x47525236); /* GRR6: PMU initialized */"),
        ("    /* rng calibration */\n    rng_calibration();",
         "    /* rng calibration */\n    rng_calibration();\n    INKTAG_SOC_STAGE(0x47525237); /* GRR7: RTC/RNG calibrated */"),
        ("    exflash_io_pull_config();",
         "    INKTAG_SOC_STAGE(0x47525238); /* GRR8: LCP initialized */\n    exflash_io_pull_config();"),
        ("    /* recover the default setting by temperature, should be called in the end */\n    pmu_calibration_handler(NULL);",
         "    INKTAG_SOC_STAGE(0x47525239); /* GRR9: flash IO/auto PMU calibration done */\n\n    /* recover the default setting by temperature, should be called in the end */\n    pmu_calibration_handler(NULL);\n    INKTAG_SOC_STAGE(0x4752523A); /* GRRA: temperature PMU calibration done */"),
        ("    /* Init peripheral sleep management */\n    app_pwr_mgmt_init();",
         "    /* Init peripheral sleep management */\n    app_pwr_mgmt_init();\n    INKTAG_SOC_STAGE(0x4752523B); /* GRRB: peripheral power management done */"),
    ]
    for old, new in replacements:
        if old not in text:
            raise SystemExit(f"cannot patch GR5515 platform diagnostics at {old!r}: {soc_c}")
        text = text.replace(old, new, 1)
    return write_if_changed(soc_c, text)

def patch_custom_config(
    template_config: Path,
    config: Path,
    platform: str,
    layout: str,
    internal_images: bool = False,
):
    text = template_config.read_text(encoding="utf-8")
    if platform == "gr5515":
        # Read the package directly from the ZKC42VM chip with
        # sys_device_package_get(): runtime package id 4 is GR5515I0ND.
        # custom_config.h uses a different project enum where GR5515I0ND is 2.
        text = text.replace("#define CHIP_TYPE  4", "#define CHIP_TYPE  2")
        # InkTag reports operational status over BLE. Keeping the vendor
        # UART/RTT logger in the application costs 3,420 bytes with no product
        # function benefit. Unlike narrowing the SDK resource table, disabling
        # APP_LOG reduces Flash without changing profile/link capacity.
        text = text.replace("#define APP_LOG_ENABLE            1", "#define APP_LOG_ENABLE            0")
        # InkTag is a single-connection peripheral.  It registers the product
        # service plus the SDK's advertising/GAP plumbing, never scans, and
        # never needs the template's ten-profile/five-link resource tables.
        # Keep one spare profile slot so the GR551x and GR5331 applications
        # follow the same conservative capacity policy.
        for old, new in {
            "#define CFG_MAX_PRFS             10": "#define CFG_MAX_PRFS             2",
            "#define CFG_MAX_BOND_DEVS        4": "#define CFG_MAX_BOND_DEVS        1",
            "#define CFG_MAX_CONNECTIONS      5": "#define CFG_MAX_CONNECTIONS      1",
        }.items():
            if old not in text:
                raise SystemExit(
                    f"cannot apply GR551x peripheral-only BLE config at {old!r}: "
                    f"{template_config}"
                )
            text = text.replace(old, new)
        # ZKC42VM does not expose/populate the SDK template's 32.768 kHz RTC
        # crystal path.  The external-clock branch stalls in
        # platform_clock_init(); use the GR551x internal low-power oscillator.
        text = text.replace("#define CFG_LPCLK_INTERNAL_EN   0", "#define CFG_LPCLK_INTERNAL_EN   1")
        # Runtime-configurable Find uses an exact 31-byte manufacturer frame.
        # Packaging support is harmless while the persisted feature remains
        # disabled, so release builds keep one byte-exact application image.
        max_adv_anchor = "#define CFG_MAX_ADV_DATA_LEN_SUPPORT        0"
        if max_adv_anchor not in text:
            raise SystemExit(
                f"cannot enable GR551x 31-byte advertising support at "
                f"{max_adv_anchor!r}: {template_config}"
            )
        text = text.replace(
            max_adv_anchor,
            "#define CFG_MAX_ADV_DATA_LEN_SUPPORT        1",
            1,
        )
        if layout == "ota":
            # Single-app InkTag layout: 112 KiB fixed BLE Recovery, one
            # Board-qualified application, image store, Recovery journal and NVDS.
            text = text.replace("#define APP_CODE_LOAD_ADDR      0x01002000", "#define APP_CODE_LOAD_ADDR      0x01020000")
            text = text.replace("#define APP_CODE_RUN_ADDR       0x01002000", "#define APP_CODE_RUN_ADDR       0x01020000")
            text = text.replace("#define NVDS_START_ADDR         0x010FF000", "#define NVDS_START_ADDR         0x010FC000")
            text = text.replace("#define NVDS_NUM_SECTOR          1", "#define NVDS_NUM_SECTOR          4")
        else:
            # Stock recovery layout keeps the original ZKC42VM second_boot.
            text = text.replace("#define APP_CODE_LOAD_ADDR      0x01002000", "#define APP_CODE_LOAD_ADDR      0x0100A000")
            text = text.replace("#define APP_CODE_RUN_ADDR       0x01002000", "#define APP_CODE_RUN_ADDR       0x0100A000")
    elif platform == "gr5331":
        # Keep the GR533x SDK's previously hardware-qualified project setting
        # while the package-specific controller patch difference is isolated.
        # sys_device_package_get() still reports the physical GR5331CENI
        # package at runtime; do not confuse that package ID with this SDK
        # patch-table selector.
        # InkTag is a single-connection BLE peripheral/GATT server. Pairing is
        # explicitly disabled by the application, and it never scans, acts as
        # a central, opens an LE credit-based channel, or instantiates a GATT
        # client. The vendor template enables all of those generic roles and
        # pulls about 36 KiB of otherwise unreachable GR533x BLE patch code.
        # The isolated OTAS diagnostic registers one application profile.
        # Retain one spare entry, matching the earlier on-air-qualified build.
        max_profiles = 2
        replacements = {
            # GR5331 must keep the vendor full-newlib link: nano.specs makes
            # even the untouched Goodix template stop transmitting
            # advertisements on real ZKC21V hardware. Disable the vendor SDK
            # log frontend instead so its printf family does not consume the
            # OTA-slot headroom. InkTag's BLE status notifications remain.
            "#define APP_LOG_ENABLE                   1": "#define APP_LOG_ENABLE                   0",
            "#define CFG_MAX_PRFS                     10": f"#define CFG_MAX_PRFS                     {max_profiles}",
            "#define CFG_MAX_BOND_DEVS                4": "#define CFG_MAX_BOND_DEVS                1",
            "#define CFG_MAX_CONNECTIONS              5": "#define CFG_MAX_CONNECTIONS              1",
            "#define CFG_MASTER_SUPPORT               1": "#define CFG_MASTER_SUPPORT               0",
            "#define CFG_LEGACY_PAIR_SUPPORT          1": "#define CFG_LEGACY_PAIR_SUPPORT          0",
            "#define CFG_SC_PAIR_SUPPORT              1": "#define CFG_SC_PAIR_SUPPORT              0",
            "#define CFG_COC_SUPPORT                  1": "#define CFG_COC_SUPPORT                  0",
            "#define CFG_GATTC_SUPPORT                1": "#define CFG_GATTC_SUPPORT                0",
        }
        for old, new in replacements.items():
            if old not in text:
                raise SystemExit(f"cannot apply GR533x peripheral-only BLE config at {old!r}: {template_config}")
            text = text.replace(old, new)
        if layout == "stock":
            # ZKC21V/GR5331 stock flash keeps a second_boot layout. The public
            # SDK template links at 0x00202000, but the backed-up stock APP slot is
            # 0x00208000. A 1 MiB debugger read repeats the physical 512 KiB flash;
            # its upper half is not an OTA bank.
            text = text.replace("#define APP_CODE_LOAD_ADDR               0x00202000", "#define APP_CODE_LOAD_ADDR               0x00208000")
            text = text.replace("#define APP_CODE_RUN_ADDR                0x00202000", "#define APP_CODE_RUN_ADDR                0x00208000")
        elif layout == "ota":
            text = text.replace("#define APP_CODE_LOAD_ADDR               0x00202000", "#define APP_CODE_LOAD_ADDR               0x00220000")
            text = text.replace("#define APP_CODE_RUN_ADDR                0x00202000", "#define APP_CODE_RUN_ADDR                0x00220000")
            # GR533x NVDS consumes the requested data sector plus one extra
            # 4 KiB garbage-collection sector. Keep both explicit and outside
            # the image store: 0x0027E000-0x0027FFFF.
            nvds_anchor = "#ifndef NVDS_NUM_SECTOR\n#define NVDS_NUM_SECTOR                  1\n#endif"
            if nvds_anchor not in text:
                raise SystemExit(f"cannot place GR5331 NVDS at {nvds_anchor!r}: {template_config}")
            text = text.replace(
                nvds_anchor,
                "#ifndef NVDS_START_ADDR\n"
                "#define NVDS_START_ADDR                 0x0027E000\n"
                "#endif\n\n"
                + nvds_anchor,
                1,
            )
    if internal_images:
        if platform != "gr5515" or layout != "ota":
            raise ValueError("Internal images require the distinct GR5513 OTA executable layout")
        # Exact D464 package, not the shared GR5515 project enum. Both branches
        # are replaced so no 512-KiB alias of the old 1-MiB tail is used.
        text = text.replace("#define CHIP_TYPE  2", "#define CHIP_TYPE  7")
        text = text.replace("#define NVDS_START_ADDR         0x0107F000", "#define NVDS_START_ADDR         0x0107E000")
        text = text.replace("#define NVDS_START_ADDR         0x010FC000", "#define NVDS_START_ADDR         0x0107E000")
        text = text.replace("#define NVDS_NUM_SECTOR          4", "#define NVDS_NUM_SECTOR          2")
    return write_if_changed(config, text)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("platform", choices=["gr5331", "gr5515"])
    ap.add_argument("--board", required=True)
    ap.add_argument("--repo", default=Path(__file__).resolve().parents[2])
    ap.add_argument("--sdk-root", type=Path, required=True,
                    help="absolute owned SDK workspace under the durable primary SDK root")
    # Qualified product functionality is part of the ordinary family build.
    # Keep former positive flags as compatibility aliases, not shipping opt-ins.
    ap.add_argument("--nfc-stream-diagnostics", action="store_true", help="Private bounded GT terminal diagnosis; default off")
    ap.add_argument("--nfc-stream-pilot", action="store_true", default=None, help="Compatibility alias: normal standard Pro already includes GT/FMSC; no new board qualification")
    ap.add_argument("--experimental-gt-mirror", action="store_true", default=None,
                    help="compatibility alias: GR551x volatile short commands are included by default; no bulk transport")
    ap.add_argument("--experimental-device-password", action="store_true", default=True,
                    help="compatibility alias: ordinary GR builds include device password support")
    ap.add_argument("--experimental-panel-temperature-sample", action="store_true", default=True,
                    help="compatibility alias: ordinary GR builds include bounded panel temperature sampling")
    ap.add_argument("--sdk-literal-fault-log", action="store_true",
                    help="explicit existing fixed-diagnostic optimization; track its compiler define for incremental builds")
    ap.add_argument("--panel-diagnostic", action="store_true",
                    help="default-off bounded panel read pilot; no registered waveform or partial capability")
    ap.add_argument("--clock-partial", action="store_true",
                    help="Pro Goodix small SSD1680 clock-window backend; standard internal GR5513 enables it; standard large GR551x uses the separate SSD1683 switch")
    ap.add_argument("--clock-partial-ssd1683", action="store_true",
                    help="standard GR551x exact SSD1683/400x300 Pro clock candidate; excludes internal and all other screens")
    ap.add_argument("--internal-images", action="store_true",
                    help="maintainer-only distinct GR5513 272-KiB App / internal-image layout; not a runtime fallback")
    ap.add_argument("--activation-dataflow", action="store_true",
                    help="retain verification result in effective feature data; GR internal")
    ap.add_argument("--code-fingerprints", action="store_true")
    ap.add_argument("--guard-layout", choices=('auto','0','1'), default='auto')
    ap.add_argument("--source-profile", choices=("base", "pro"), default="base")
    args = ap.parse_args()
    if args.code_fingerprints and args.source_profile == 'pro':
        from goodix_code_guards import layout_choice
    if args.source_profile == "base" and (args.clock_partial or args.clock_partial_ssd1683 or args.nfc_stream_pilot or args.nfc_stream_diagnostics or args.experimental_gt_mirror):
        ap.error("base excludes Pro clock/NFC implementation flags")
    if args.source_profile == 'base' and (args.code_fingerprints or args.activation_dataflow):
        ap.error('base does not contain private protection producers')
    if args.code_fingerprints and (args.platform not in ('gr5515','gr5331') or not args.activation_dataflow):
        ap.error('Code fingerprints require a GR result-dataflow profile')
    if not args.sdk_root.is_absolute():
        ap.error("--sdk-root must be an absolute owned SDK workspace")
    sdk = args.sdk_root.resolve()
    if not sdk.is_dir():
        ap.error(f"--sdk-root directory is missing: {sdk}")
    if args.internal_images and args.platform != "gr5515":
        ap.error("Internal images require the GR5513/GR551x SDK ABI")
    if args.experimental_gt_mirror is None:
        args.experimental_gt_mirror = args.source_profile == 'pro' and args.platform == "gr5515" and not args.internal_images
    if args.internal_images and args.experimental_gt_mirror:
        ap.error("The internal-image target has no NFC backend")
    if args.experimental_gt_mirror and args.platform != "gr5515":
        ap.error("GT mirror requires the exact ZKC42V backend in the shared GR551x app")
    if args.nfc_stream_pilot is None:
        args.nfc_stream_pilot = args.source_profile == "pro" and args.platform == "gr5515" and not args.internal_images
    if args.nfc_stream_pilot and (args.platform != "gr5515" or args.internal_images or not args.experimental_gt_mirror):
        ap.error("NFC stream requires standard GR551x with GT mirror")
    repo = Path(args.repo).resolve()
    selections, retired = [], []
    def copy_for_target(src, dst):
        return copy_profile_source(src, dst, repo=repo, profile=args.source_profile,
                                   selections=selections, retired=retired)
    registry = json.loads((repo / "firmware/boards/boards.json").read_text(encoding="utf-8"))
    board = registry.get("boards", {}).get(args.board)
    if board is None:
        raise SystemExit(f"unknown board: {args.board}")
    # The SDK/ABI selector keeps its vendor GR5515 name; the owning source
    # directory is GR551x because the same App also supports GR5513.
    expected_port = "goodix/gr551x" if args.platform == "gr5515" else f"goodix/{args.platform}"
    if board.get("port") != expected_port:
        raise SystemExit(
            f"board {args.board} belongs to port {board.get('port')!r}, not {expected_port!r}"
        )
    # One descriptor header per existing compatible executable. Board/OTA
    # identifiers above remain stable; no new target is created by a marking.
    family_header = "gr551x_zkel.h" if args.platform == "gr5515" else "gr5331_zkel.h"
    board_header = repo / "firmware/boards" / family_header
    if not board_header.is_file():
        raise SystemExit(f"missing board compile config: {board_header}")
    artifact = board["artifact"]
    subprocess.run(
        [sys.executable, str(repo / "tools/version/sync-version.py"), "--repo", str(repo)],
        check=True,
    )
    if args.platform == "gr5515":
        platform_define = "INKTAG_GOODIX_GR5515"
    else:
        platform_define = "INKTAG_GOODIX_GR5331"
    sdk_stack_sources_changed = patch_goodix_dfu_service_status(sdk)
    sdk_stack_sources_changed |= patch_goodix_literal_fault_log(sdk)
    sdk_stack_sources_changed |= patch_goodix_dfu_reset_checkpoint(sdk, args.platform)
    sdk_stack_sources_changed |= patch_goodix_dfu_rejection_diagnostics(sdk)
    sdk_stack_sources_changed |= patch_goodix_single_slot_dfu(sdk)
    sdk_stack_sources_changed |= patch_goodix_safe_resume_dfu(sdk)
    sdk_stack_sources_changed |= patch_goodix_layout_v3_dfu(sdk)
    sdk_stack_sources_changed |= patch_goodix_dfu_link_auth(sdk)
    sdk_stack_sources_changed |= patch_goodix_authenticated_mode_handoff(sdk, args.platform)
    sdk_stack_sources_changed |= patch_resume_transport(sdk, repo)
    sdk_stack_sources_changed |= patch_goodix_fault_capture(sdk, args.platform)
    sdk_stack_sources_changed |= patch_gr551x_dwt_delay_resume(sdk, args.platform)
    sdk_stack_sources_changed |= patch_gr551x_app_timer_snapshot(sdk, args.platform)
    sdk_stack_sources_changed |= patch_gr551x_app_timer_epoch(sdk, args.platform)
    sdk_stack_sources_changed |= patch_gr5331_checkpoint_aon_snapshot(sdk, args.platform)
    sdk_stack_sources_changed |= patch_goodix_flash_mutation_guard(sdk, args.platform)
    sdk_stack_sources_changed |= patch_goodix_nvds_guard(sdk)
    if args.platform == "gr5331":
        sdk_stack_sources_changed |= patch_gr533x_att_db_heap(sdk)
    layout = "ota"
    target_name = f"{artifact}-ota"
    template = sdk / "projects/ble/ble_peripheral/ble_app_template"
    target_dir = "ble_app_inktag_ota"
    target = sdk / "projects/ble/ble_peripheral" / target_dir
    if not template.exists():
        raise SystemExit(f"missing Goodix template: {template}")
    if not target.exists():
        shutil.copytree(template, target)
    user = target / "Src/user"
    common_out = target / "Src/inktag_common"
    common_out.mkdir(parents=True, exist_ok=True)
    changed = []
    if sdk_stack_sources_changed:
        changed.append("shared GR533x BLE stack config")
    for name in COMMON_SOURCES:
        if copy_for_target(repo / "firmware/ports/goodix/common/src" / name, user / name):
            changed.append(f"Src/user/{name}")
    platform_manifest = json.loads((repo/'firmware/ports/goodix/source-profiles.json').read_text())
    retired_platform_destinations = set()
    for item in platform_manifest.get('retiredItems', []):
        name = item['path']
        copy_for_target(repo/'firmware/ports/goodix/common/src'/name, user/name)
        retired_platform_destinations.add((user/name).resolve())
    # Preserve obsolete generated fragments/depfiles when either profile moves
    # to the new composition; ordinary compiler objects remain cached.
    retire_private_profile_depfiles(target, retired_platform_destinations, retired)
    increments = [item['path'] for item in platform_manifest['items'] if item['path'].endswith('.inc')]
    for name in ("inktag_gr_panel_diagnostic.inc", "inktag_gr_panel_diagnostic.h", *increments):
        if copy_for_target(repo / "firmware/ports/goodix/common/src" / name, user / name):
            changed.append(f"Src/user/{name}")
    for name in ("inktag_panel_diagnostic.h", "inktag_panel_diagnostic.c"):
        if copy_for_target(repo / "firmware/common/epd" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in ("inktag_epd_window.h", "inktag_ssd1683_clock.h", "inktag_ssd1619_clock.h", "inktag_uc8276_clock.h",
                 "inktag_epd_partial_lut.h", "inktag_epd_partial_lut_data.h", "inktag_epd_clock_flow.inc"):
        if copy_for_target(repo / "firmware/common/epd" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    if args.experimental_gt_mirror:
        for name in ("inktag_gt_mirror.c", "inktag_gt_mirror.h",
                     "inktag_nfc_volatile_command.c", "inktag_nfc_volatile_command.h"):
            if copy_for_target(repo / "firmware/common/input" / name, common_out / name):
                changed.append(f"Src/inktag_common/{name}")
    if args.nfc_stream_pilot:
        for name in ("inktag_gt_stream.c", "inktag_gt_stream.h", "inktag_gt_pipe.c", "inktag_gt_pipe.h", "inktag_nfc_fmsc.c", "inktag_nfc_fmsc.h"):
            if copy_for_target(repo / "firmware/common/input" / name, common_out / name):
                changed.append(f"Src/inktag_common/{name}")
        name = "inktag_image_fixed_stream_pro.inc"
        if copy_for_target(repo / "firmware/common/storage" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in EPD_SOURCES:
        if copy_for_target(repo / "firmware/common/epd" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    # Accepted security sources retain their source-tree relative protocol
    # include. Supply that header verbatim; never patch common authentication
    # code into a GR-local variant to accommodate this flattened SDK project.
    protocol_relative = target / "Src/epd/inktag_protocol_v2.h"
    protocol_relative.parent.mkdir(parents=True, exist_ok=True)
    if copy_for_target(repo / "firmware/common/epd/inktag_protocol_v2.h",
                       protocol_relative):
        changed.append("Src/epd/inktag_protocol_v2.h")
    for name in GUI_SOURCES:
        if copy_for_target(repo / "firmware/common/gui" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in STATUS_SOURCES:
        if copy_for_target(repo / "firmware/common/status" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in POWER_SOURCES:
        if copy_for_target(repo / "firmware/common/power" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    if copy_gui_relative_power_header(repo, common_out):
        changed.append("Src/power/inktag_power.h")
    for name in HEALTH_SOURCES:
        if copy_for_target(repo / "firmware/common/health" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in INPUT_SOURCES:
        if copy_for_target(repo / "firmware/common/input" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in BLE_SOURCES:
        if copy_for_target(repo / "firmware/common/ble" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    # inktag_gui.h keeps its source-relative include verbatim in the flattened SDK.
    display_name_relative = target / "Src/ble/inktag_device_display_name.h"
    display_name_relative.parent.mkdir(parents=True, exist_ok=True)
    if copy_for_target(repo / "firmware/common/ble/inktag_device_display_name.h",
                       display_name_relative):
        changed.append("Src/ble/inktag_device_display_name.h")
    for name in PROTOCOL_SOURCES:
        if copy_for_target(repo / "firmware/common/protocol" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in LIFECYCLE_SOURCES:
        if copy_for_target(repo / "firmware/common/lifecycle" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in STORAGE_SOURCES:
        if copy_for_target(repo / "firmware/common/storage" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in SECURITY_SOURCES:
        source = repo / "firmware/common/security" / name
        if copy_for_target(source, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in PLATFORM_SOURCES:
        if copy_for_target(repo / "firmware/common/platform" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in TIME_SOURCES:
        if copy_for_target(repo / "firmware/common/time" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    for name in OTA_SOURCES:
        if copy_for_target(repo / "firmware/common/ota" / name, common_out / name):
            changed.append(f"Src/inktag_common/{name}")
    if copy_for_target(board_header, common_out / "inktag_board.h"):
        changed.append("Src/inktag_common/inktag_board.h")
    if copy_for_target(repo / "firmware/ports/goodix/common/src/goodix_pin_backend_facts.h",
                       common_out / "goodix_pin_backend_facts.h"):
        changed.append("Src/inktag_common/goodix_pin_backend_facts.h")
    if args.source_profile == 'base':
        common_manifest = json.loads((repo/'firmware/source-profiles.json').read_text())
        private_destinations = set(retired_platform_destinations)
        for entry in common_manifest['overrides']:
            if entry['base'] is None:
                logical = repo/entry['logicalPath']
                copy_for_target(logical, common_out/logical.name)
                private_destinations.add((common_out/logical.name).resolve())
        for item in platform_manifest['items']:
            if item['kind'].startswith('pro-'):
                private_destinations.add((user/item['path']).resolve())
            name = item.get('nativeBasename')
            if name and name != 'inktag_gr_activation.c':
                copy_for_target(repo/'firmware/ports/goodix/common/src'/name, user/name)
                private_destinations.add((user/name).resolve())
        retire_private_profile_depfiles(target, private_destinations, retired)
    if patch_makefile(
        template / "GCC/Makefile",
        target / "GCC/Makefile",
        target_name,
        platform_define,
        layout,
        args.experimental_gt_mirror,
        args.experimental_device_password,
        args.experimental_panel_temperature_sample,
        args.sdk_literal_fault_log,
        args.panel_diagnostic,
        args.clock_partial,
        args.internal_images,
        args.activation_dataflow,
        args.code_fingerprints,
        repo,
        args.clock_partial_ssd1683,
        args.nfc_stream_pilot,
        args.nfc_stream_diagnostics,
        args.source_profile,
    ):
        changed.append("GCC/Makefile")
        invalidate_config_dependent_outputs(target)
        changed.append("GCC/out build-flags cache")
    if args.platform == "gr5331":
        linker_template = (sdk / "platform/soc/linker/gcc/gcc_linker.lds").read_text(encoding="utf-8")
        # The vendor template normally lives beside flash_linker_config.h.  Our
        # board-specific copy lives in the generated project, so retain an
        # explicit SDK-relative include instead of relying on cpp's source-dir
        # lookup.
        linker_template = linker_template.replace(
            '#include "flash_linker_config.h"',
            '#include "../../../../../platform/soc/linker/gcc/flash_linker_config.h"',
        )
        linker_template = linker_template.replace(
            "#define APP_FLASH_END_ADDR (FLASH_START_ADDR + FLASH_SIZE)",
            "#define APP_FLASH_END_ADDR (FLASH_START_ADDR + 0x60000)",
        )
        clean_anchor = '        *(.rodata*)'
        if linker_template.count(clean_anchor) != 1:
            raise SystemExit('Unknown GR5331 read-only linker shape')
        linker_template = linker_template.replace(clean_anchor,
            '        KEEP(*(.rodata.inktag_gr_clean_identity_info*))\n'+clean_anchor, 1)
        retained_anchor = "    } > RAM\n\n    __HeapBase = STACK_END_ADDR"
        retained_section = """    } > RAM

    .inktag_noinit (NOLOAD) :
    {
        . = ALIGN(4);
        __inktag_noinit_start__ = .;
        KEEP(*(.inktag_noinit))
        . = ALIGN(4);
        __inktag_noinit_end__ = .;
    } > RAM

    __HeapBase = STACK_END_ADDR"""
        if retained_anchor not in linker_template:
            raise SystemExit("cannot reserve GR5331 InkTag retained RAM")
        linker_template = linker_template.replace(retained_anchor, retained_section, 1)
        linker_template = linker_template.replace(
            'ASSERT(__HeapBase >= __bss_end__, "region RAM overflowed with HEAP & STACK")',
            'ASSERT(__HeapBase >= __inktag_noinit_end__, "region RAM overflowed with HEAP & STACK")',
        )
        # Fixed signed factory-init ABI, not additional physical SRAM. Fail an
        # App link before a later Full helper could overlap product globals.
        linker_template += '\nASSERT(__inktag_noinit_end__ <= 0x2000AF00, "GR5331 data overlaps fixed factory scratch")\n'
        linker_template += 'ASSERT(__HeapBase >= 0x2000DE80, "GR5331 heap overlaps fixed factory stack")\n'
        if args.code_fingerprints:
            from goodix_code_guards import guard_makefile, guard_linker, layout_choice
            linker_template = guard_linker(linker_template,layout_choice(repo,'app',args.guard_layout))
        if write_if_changed(target / "GCC/gcc_linker_inktag_ota.lds", linker_template):
            changed.append("GCC/gcc_linker_inktag_ota.lds")
            invalidate_link_outputs(target, target_name)
    if args.platform == "gr5515":
        if patch_linker(template / "GCC/gcc_linker_gr5515.lds", target / "GCC/gcc_linker_gr5515.lds", args.platform, layout, args.internal_images,
                        args.code_fingerprints, layout_choice(repo, 'app', args.guard_layout) if args.code_fingerprints else 0):
            changed.append("GCC/gcc_linker_gr5515.lds")
            invalidate_link_outputs(target, target_name)
            changed.append("GCC/out linker cache")
        if patch_gr5515_soc(sdk / "platform/soc/src/gr_soc.c"):
            changed.append("platform/soc/src/gr_soc.c diagnostics")
        if patch_gr5515_flash_capacity(sdk / "platform/soc/src/gr_soc.c"):
            changed.append("platform/soc/src/gr_soc.c Flash capacity")
        if patch_gr5515_platform_init(sdk / "platform/soc/src/gr_soc.c"):
            changed.append("platform/soc/src/gr_soc.c platform diagnostics")
    if patch_main(template / "Src/user/main.c", user / "main.c"):
        changed.append("Src/user/main.c")
    if patch_periph(
        template / "Src/platform/user_periph_setup.c",
        target / "Src/platform/user_periph_setup.c",
        args.platform,
        layout,
    ):
        changed.append("Src/platform/user_periph_setup.c")
    if patch_custom_config(
        template / "Src/config/custom_config.h",
        target / "Src/config/custom_config.h",
        args.platform,
        layout,
        args.internal_images,
    ):
        changed.append("Src/config/custom_config.h")
        invalidate_config_dependent_outputs(target)
        changed.append("GCC/out config cache")
    expected_app_addr = (
        "0x01020000" if args.platform == "gr5515" else "0x00220000"
    )
    if invalidate_stale_layout_outputs(target, expected_app_addr):
        changed.append("GCC/out stale layout cache")
    for item in selections:
        materialized = Path(item['destination'])
        item['materializedSha256'] = hashlib.sha256(materialized.read_bytes()).hexdigest()
        item['transformed'] = item['materializedSha256'] != item['sha256']
    write_if_changed(target/'Src/inktag-source-profile.json', json.dumps(dict(
        schemaVersion=1, sourceProfile=args.source_profile,
        defines={**profile_defines(repo, profile=args.source_profile),
                 'INKTAG_EPD_TEMPERATURE_OPTION': int(args.source_profile == 'pro' and
                     args.platform == 'gr5515' and not args.internal_images and args.nfc_stream_pilot)},
        files=selections), sort_keys=True, indent=2)+'\n')
    if retired:
        record = json.dumps(retired, sort_keys=True, indent=2)+'\n'
        archive = target/'.inktag-source-retirements'/hashlib.sha256(record.encode()).hexdigest()
        archive.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(archive.with_suffix('.json'), record)
    suffix = " (unchanged)" if not changed else " (updated: " + ", ".join(changed) + ")"
    print(f"{target}{suffix}")

if __name__ == "__main__":
    main()
