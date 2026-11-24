#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from onmicro_family import compile_view,section as family_section
from onmicro_source_profiles import SourceSelection
from onmicro_adc_patch import patch_adc_source


def app_startup_without_unwind(source):
    """Reset has no caller to unwind into. Change EH metadata, no instructions.

    The vendor fnstart without cantunwind otherwise pulls libgcc's complete
    C++ personality/exception engine into a C-only application. Recovery keeps
    its original startup object; no blanket discard of SDK sections is used.
    """
    marker="                .fnstart\nReset_Handler:\n"
    if source.count(marker)!=1:
        raise ValueError('Unexpected pinned OM Reset unwind declaration')
    return source.replace(marker,"                .fnstart\n                .cantunwind\nReset_Handler:\n",1)


def normal_system_clock64(source: str) -> str:
    """Use the SDK's calibrated x2 path in both roles, with explicit SF Hz."""
    before = """    drv_pmu_xtal32m_startup();        // TOP clock is 32MHz. If xtal32m is used, this function must be called before other functions that depend on xtal32m.
    //drv_pmu_xtal32m_x2_startup();   // TOP clock is 64MHz. This function depend on xtal32m.

    // Init flash
    drv_sfs_probe(DRV_SFS_IFLASH, 2/*freq*/);
    drv_sfs_config(1/*freq*/, DRV_SF_WIDTH_4LINE, DRV_SF_DELAY_AUTO);"""
    after = """    drv_pmu_xtal32m_startup();
    /* InkTag normal App and Recovery: calibrated 64MHz, no role override. */
    drv_pmu_xtal32m_x2_startup();

    /* Values below 256 mean dividers in this SDK. Keep the qualified Flash
     * targets explicit as Hz when TOP changes from 32MHz to 64MHz. */
    drv_sfs_probe(DRV_SFS_IFLASH, 16000000u);
    drv_sfs_config(32000000u, DRV_SF_WIDTH_4LINE, DRV_SF_DELAY_AUTO);"""
    if source.count(after) == 1 and before not in source:
        return source
    if source.count(before) != 1 or after in source:
        raise SystemExit('unsupported OM SDK system clock/Flash startup block')
    return source.replace(before, after, 1)


COMMON_SOURCES = [
    # Shared product defaults; GPIO/mux/IRQ owners live in App/Recovery, not
    # the vendor EVB sample's LED/UART/button tables.
    "projects/ble_app_inktag/src/user/inktag_om_board_init.c",
    "common/om_libc_retarget.c",
    "common/om_fifo.c",
    "common/om_list.c",
    "common/om_utils.c",
    "components/ble/common/ob_mem_heap.c",
    "components/evt/evt.c",
    "components/evt/evt_timer.c",
    "components/crypto/sha256.c",
    "components/crypto/micro_ecc/uECC.c",
    "components/pm/pm.c",
    "components/mbr/mbr.c",
    "hal/driver/common/drv_isr.c",
    "hal/driver/common/drv_wdt.c",
    "hal/driver/common/drv_pmu_timer.c",
    "hal/driver/common/drv_cortex.c",
    "hal/driver/common/drv_24g.c",
    "hal/driver/common/drv_aes_hw.c",
    "hal/driver/common/drv_gpio.c",
    "hal/driver/common/drv_efuse.c",
    "hal/driver/common/drv_sf_base.c",
    "hal/driver/common/drv_sf_sys.c",
    "hal/driver/common/drv_sf.c",
    "hal/driver/common/drv_dma.c",
    "hal/driver/common/drv_spi.c",
    "hal/driver/common/drv_radio.c",
    "hal/driver/common/drv_tim.c",
    "hal/driver/common/drv_lp_tim.c",
    "hal/driver/common/drv_rng.c",
    "hal/driver/om6626/drv_pinmux.c",
    "hal/driver/om6626/drv_pmu.c",
    "hal/driver/om6626/drv_rcc.c",
    "hal/driver/om6626/drv_calib.c",
    "hal/driver/om6626/drv_calib_repair.c",
    "hal/driver/om6626/drv_adc.c",
    "hal/device/om6626/GCC/system_asm.S",
    "hal/device/om6626/GCC/startup.S",
    "hal/device/om6626/system.c",
]

APP_PLATFORM_SOURCES = [
    # The NFC frontend is application-owned. Keep I2C out of the fixed
    # bootloader so enabling a tag never consumes boot-window space.
    "hal/driver/common/drv_i2c.c",
]

DIAGNOSTIC_SOURCES = [
    "common/om_printf.c",
    "common/om_log.c",
    "components/shell/shell.c",
    "components/shell/shell_cmd.c",
    "components/shell/shell_port.c",
    "components/unity/unity_fixture.c",
    "components/unity/unity.c",
    "components/trace/trc_io.c",
    "hal/driver/common/drv_usart.c",
    "hal/driver/common/drv_usart_ex.c",
    "hal/driver/common/drv_rtc.c",
]

INKTAG_SOURCES = [
    "projects/ble_app_inktag/src/user/inktag_om_address.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_ble_address.c",
    "projects/ble_app_inktag/src/user/inktag_om_app.c",
    "projects/ble_app_inktag/src/user/inktag_om_service.c",
    "projects/ble_app_inktag/src/user/inktag_om_image_store.c",
    "projects/ble_app_inktag/src/user/inktag_om_gui_assets.c",
    "projects/ble_app_inktag/src/user/inktag_om_pins.c",
    "projects/ble_app_inktag/src/user/inktag_om_pin_layout.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_pin_config.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_aux_io.c",
    "projects/ble_app_inktag/src/user/inktag_om_external_flash.c",
    "projects/ble_app_inktag/src/user/inktag_om_nfc.c",
    "projects/ble_app_inktag/src/user/inktag_om_device_settings.c",
    "projects/ble_app_inktag/src/user/inktag_om_qr_store.c",
    "projects/ble_app_inktag/src/user/inktag_om_activation.c",
    "projects/ble_app_inktag/src/user/inktag_om_brand.c",
    "projects/ble_app_inktag/src/user/inktag_om_findmy.c",
    "projects/ble_app_inktag/src/user/inktag_om_p224_uecc.c",
    "projects/ble_app_inktag/src/user/inktag_om_health.c",
    "projects/ble_app_inktag/src/user/inktag_om_nvds.c",
    "projects/ble_app_inktag/src/user/inktag_om_voltage.c",
    "projects/ble_app_inktag/src/user/inktag_om_board_identity.c",
    "projects/ble_app_inktag/src/user/inktag_om_swap.c",
    "projects/ble_app_inktag/src/user/epd_driver_onmicro.c",
    "projects/ble_app_inktag/src/ota/inktag_recovery.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_config.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_input.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_ndef.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_nfc_command.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_nfc_link.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_nfc_volatile_command.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_nfc_binding.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gt_mirror.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_status.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_soak_telemetry.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_power.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_elapsed_clock.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_ble_identity.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_findmy.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_findmy_public.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_settings_records.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_protocol_core.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_image_upload_v5.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_lzo1x.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_type4_apdu.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_nfc_fmsc.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gt_stream.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gt_pipe.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gui_asset_package.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_display_settings.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_lifecycle.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_lifecycle_pending.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_activation.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_device_password.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_device_password_store.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_access.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_image_quota.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_brand_overlay.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_sha256.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_screen.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_runtime.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_commands.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_page_cycle.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_uc.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_jd.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_epd_ssd.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gui.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_calendar.c",
    "projects/ble_app_inktag/src/inktag_common/inktag_gui_fonts.c",
    "projects/ble_app_inktag/src/inktag_common/u8g2_font.c",
    "projects/ble_app_inktag/src/inktag_common/Adafruit_GFX.c",
]

INCLUDES = [
    "bsp/include",
    "bsp/OM662X_EVB",
    "common/",
    "components/shell/",
    "components/unity",
    "components/ble/include/",
    "components/ble/controller/include/",
    "components/ble/host/include/",
    "components/ble/mesh/include/",
    "components/nvds/",
    "components/evt/",
    "components/crypto/",
    "components/crypto/micro_ecc",
    "components/pm",
    "components/mbr",
    "components/eeprom",
    "components/trace",
    "hal/CMSIS/Core/Include",
    "hal/driver/include",
    "hal/device/include",
    "projects/ble_app_inktag/src/user",
    "projects/ble_app_inktag/src/ota",
    "projects/ble_app_inktag/src/dfu",
    "projects/ble_app_inktag/src/inktag_common",
    "include",
]

MAKEFILE = """############################################################################################
# Generated InkTag OM6626 Makefile. Do not edit here; edit tools/sdk/materialize-onmicro-port.py.
############################################################################################

ROOT_DIR = ../../../
CUR_ROMLIB_DIR = $(ROOT_DIR)hal/device/om6626/rom_lib/current/
INKTAG_OM_DIAGNOSTICS ?= 0
PROJECT = {artifact}
ifeq ($(INKTAG_OM_DIAGNOSTICS),1)
PROJECT := {artifact}-diagnostics
endif

DEFINES = CONFIG_OM6626A=1 CONFIG_LIB_PRESET_BLE_1PERIPHERAL CONFIG_AUTOCONF_PRESET _start=main INKTAG_ONMICRO INKTAG_ONMICRO_OM6626 INKTAG_ONMICRO_OTA INKTAG_OM_ENABLE_CPFT INKTAG_OM_SWAP INKTAG_SWAP_EXECUTE_FROM_RAM INKTAG_GUI_RAW_VIEWPORT INKTAG_GUI_COOPERATIVE_YIELD INKTAG_ENABLE_SOAK_TELEMETRY=1 INKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES=35u FDS_ENABLED=0 __HEAP_SIZE=2048{activation_defines}
CONFIG_OPTIMIZATION_LEVEL = 4
CONFIG_LIB_GENERATE = n
POSTBUILDPROG =
LIBRARIES =

SOURCES = \\
{sources}

ifeq ($(INKTAG_OM_DIAGNOSTICS),1)
DEFINES += INKTAG_OM_DIAGNOSTICS
SOURCES += \\
{diagnostic_sources}
else
DEFINES += RTE_DEVICE_HEADER=1
endif

INCLUDES = \\
{includes}

LDSCRIPT = $(ROOT_DIR)projects/ble_app_inktag/gcc/linker_flash_inktag.ld
LDEXTFILE = $(ROOT_DIR)hal/device/om6626/rom_lib/current/GCC/linker_ble_1peripheral.mk

include $(ROOT_DIR)tools/build/rules_prj.mk

# The vendor ELF recipe uses $+ for object inputs. Linker files must invalidate
# the link without becoming extra positional inputs or recompiling SDK objects.
$(TARGET_ELF): .EXTRA_PREREQS = $(LDSCRIPT) $(CUR_ROMLIB_DIR)GCC/rom_symbol.ld $(LDEXTFILE)

INKTAG_VARIANT_CONFIG = $(ROOT_DIR)projects/ble_app_inktag/src/user/inktag_om_variant_config.h
$(FILES_C_OBJ): $(INKTAG_VARIANT_CONFIG)
CPPFLAGS += -include inktag_om_variant_config.h
ifeq ($(INKTAG_OM_DIAGNOSTICS),0)
CPPFLAGS += -include inktag_om_build_config.h -fno-unwind-tables -fno-asynchronous-unwind-tables
endif
{prototype_cppflags}
{layout_ldflags}

"""

BOOT_MAKEFILE = """############################################################################################
# Generated InkTag OM6626 fixed BLE Recovery Makefile.
############################################################################################

ROOT_DIR = ../../../
CUR_ROMLIB_DIR = $(ROOT_DIR)hal/device/om6626/rom_lib/current/
PROJECT = InkTag-om6626-recovery

DEFINES = CONFIG_OM6626A=1 CONFIG_LIB_PRESET_BLE_1PERIPHERAL CONFIG_AUTOCONF_PRESET RTE_DEVICE_HEADER=1 NDEBUG _start=main INKTAG_ONMICRO INKTAG_ONMICRO_OM6626 INKTAG_OM_ENABLE_CPFT INKTAG_OM_FIXED_RECOVERY INKTAG_OM_SWAP INKTAG_SWAP_EXECUTE_FROM_RAM FDS_ENABLED=0
CONFIG_OPTIMIZATION_LEVEL = 4
CONFIG_LIB_GENERATE = n
POSTBUILDPROG =
LIBRARIES =

SOURCES = \\
{sources}

INCLUDES = \\
{includes}

LDSCRIPT = $(ROOT_DIR)projects/inktag_om_swap_boot/gcc/linker_flash_inktag_boot.ld
LDEXTFILE = $(ROOT_DIR)hal/device/om6626/rom_lib/current/GCC/linker_ble_1peripheral.mk

include $(ROOT_DIR)tools/build/rules_prj.mk

$(TARGET_ELF): .EXTRA_PREREQS = $(LDSCRIPT) $(CUR_ROMLIB_DIR)GCC/rom_symbol.ld $(LDEXTFILE)

CPPFLAGS += -include inktag_om_variant_config.h
CPPFLAGS += -include inktag_om_build_config.h -fno-unwind-tables -fno-asynchronous-unwind-tables
INKTAG_BOOT_VARIANT = $(ROOT_DIR)projects/inktag_om_swap_boot/src/inktag_om_variant_config.h
$(FILES_C_OBJ): $(INKTAG_BOOT_VARIANT)
{layout_ldflags}
"""

DFU_CONFIG = """#ifndef __ONMICRO_DFU_CONFIG_H__
#define __ONMICRO_DFU_CONFIG_H__

#include "stdlib.h"
#include "onmicro_dfu_nvds.h"
#include "inktag_om_swap.h"

/* Erase the single application one 4 KiB sector at a time while data is received. Leaving
 * this undefined makes the vendor DFU implementation erase the complete
 * application image synchronously in the DATA CREATE GATT callback, which is
 * long enough to drop the BLE connection before payload transfer starts. */
#define FLASH_ERASE_SIZE 0x1000
#define INKTAG_DFU_APPLICATION_ADDRESS INKTAG_OM_SLOT_A
#define INKTAG_DFU_APPLICATION_SIZE    INKTAG_OM_SLOT_SIZE

typedef struct {
    uint16_t type;
    uint32_t base_address1;
    uint32_t base_address2;
    uint32_t max_length;
    const char *describe;
    const dfu_nvds_itf_t *image_ops_itf;
    const dfu_nvds_itf_t *info_ops_itf;
    uint16_t info_id;
} dfu_image_t;

/* Recovery writes the only application in place. The fixed MBR entry remains
 * Recovery; its journal decides whether the application may be trialed. */
const dfu_image_t dfu_image_types[] = {
    {IMAGE_TYPE_CUSTOM, INKTAG_DFU_APPLICATION_ADDRESS,
     INKTAG_DFU_APPLICATION_ADDRESS, INKTAG_DFU_APPLICATION_SIZE, "InkTag application",
     &dfu_nvds_itf[DFU_NVDS_ITF_TYPE_FLASH], NULL, 0x00},
};
const uint8_t dfu_image_types_num = 1u;

#endif
"""

def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and src.read_bytes() == dst.read_bytes():
        return
    # Changed bytes need a fresh destination mtime for incremental compilers.
    shutil.copyfile(src, dst)
    shutil.copymode(src, dst)


def copy_build_dependency(src: Path, dst: Path) -> None:
    """Copy a generated trust input with a fresh mtime when bytes change.

    ``copy2`` preserves an older source timestamp. That can leave a newer
    verifier object falsely up to date after fixture -> production switching.
    """
    copy_file(src, dst)

def write_if_changed(dst: Path, content: str) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists() and dst.read_text(encoding="utf-8") == content:
        return
    dst.write_text(content, encoding="utf-8")


def reserve_retained_mailbox(linker: str) -> str:
    """Reserve first-bank context outside both images AND ROM boot storage.

    The B892 A3 ROM scatter/loader uses 0x20013d40..0x20013ea0. Merely
    excluding the former top-of-RAM mailbox from C startup is insufficient.
    Keep initialized code/data below the first-bank window and ordinary
    BSS/heap/stack above it; the final physical 8KiB bank is vendor-owned.
    """
    ram = "    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = __RAM_SIZE"
    alias = "    RAM_RO (rwx)  : ORIGIN = 0x00200000, LENGTH = __RAM_SIZE"
    bss = "    .ram_bss :\n"
    if any(linker.count(marker) != 1 for marker in (ram, alias, bss)):
        raise ValueError("Unexpected OM6626 retained-mailbox linker marker")
    linker = linker.replace(ram, ram.replace("__RAM_SIZE", "0x00001fc0"), 1)
    linker = linker.replace(alias, alias.replace("__RAM_SIZE", "0x00001fc0") + "\n"
        "    RAM_HIGH (rwx) : ORIGIN = 0x20002000, LENGTH = 0x00010000\n"
        "    INKTAG_RETAINED (rw) : ORIGIN = 0x20001fc0, LENGTH = 0x40", 1)
    prefix, suffix = linker.split(bss, 1)
    linker = prefix + bss + suffix.replace("} > RAM", "} > RAM_HIGH")
    return linker.replace(bss, """    /* Paired App/Recovery ABI; never a ROM or C startup destination. */
    .inktag_noinit (NOLOAD) :
    {
        __inktag_retained_start__ = .;
        KEEP(*(.inktag_noinit))
        KEEP(*(.inktag_noinit.*))
        . = ORIGIN(INKTAG_RETAINED) + LENGTH(INKTAG_RETAINED);
        __inktag_retained_end__ = .;
    } > INKTAG_RETAINED
    ASSERT(__RAM_SIZE == 0x00013ea0,
           "Unexpected OM ROM SRAM layout")
    ASSERT(ORIGIN(INKTAG_RETAINED) == 0x20001fc0,
           "OM retained mailbox ABI changed")
    ASSERT(SIZEOF(.inktag_noinit) == 0x40,
           "OM retained mailbox overflow")
    ASSERT(__ram_data_end__ <= ORIGIN(INKTAG_RETAINED),
           "OM low code/data overlaps retained mailbox")
    ASSERT(__ram_bss_start__ >= ORIGIN(RAM_HIGH),
           "OM startup BSS overlaps retained mailbox")

""" + bss, 1)


def repair_ble_dwt_entry(source: str) -> str:
    """Re-arm DWT before entering the precompiled controller's inline waits."""
    marker = "/* InkTag precompiled BLE DWT entry recovery */"
    replacement = (marker + "\n"
                   "    (void)drv_dwt_read_running_cycle();\n    obc_isr();")
    if marker in source:
        if source.count(marker) != 1 or replacement not in source:
            raise SystemExit("incomplete OM BLE DWT entry recovery")
        return source
    if source.count("    obc_isr();") != 1:
        raise SystemExit("unsupported OM BLE IRQ dispatch anchor")
    return source.replace("    obc_isr();", "    " + replacement, 1)


def repair_dwt_counter_reads(source: str) -> str:
    """Recover a counter stopped during a wait without restarting its epoch."""
    marker = "/* InkTag DWT counter-read recovery */"
    if marker in source:
        if source.count("drv_dwt_read_running_cycle(void)") != 1:
            raise SystemExit("incomplete OM DWT read recovery")
        return source
    anchor = "__STATIC_FORCEINLINE void drv_dwt_delay_cycles(uint32_t cycles)"
    if source.count(anchor) != 1 or source.count("return DWT->CYCCNT;") != 1:
        raise SystemExit("unsupported OM SDK DWT counter reader")
    source = source.replace("DWT->CYCCNT", "drv_dwt_read_running_cycle()")
    helper = marker + "\n__RAM_CODE uint32_t drv_dwt_read_running_cycle(void);\n\n"
    return source.replace(anchor, helper + anchor, 1)


def repair_dwt_counter_implementation(source: str) -> str:
    """The shared reader must remain executable while Flash is unavailable."""
    marker = "/* InkTag DWT counter-read recovery */"
    if marker in source:
        if source.count("drv_dwt_read_running_cycle(void)") != 1:
            raise SystemExit("incomplete OM DWT reader implementation")
        return source
    helper = marker + "\n#if (RTE_CORTEX_DWT)\n" + """__RAM_CODE uint32_t drv_dwt_read_running_cycle(void)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk))
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return DWT->CYCCNT;
}
""" + "#endif\n"
    if source.count('/** @} */') != 1:
        raise SystemExit("unsupported OM SDK cortex implementation")
    return source.replace('/** @} */', helper + '\n/** @} */', 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", required=True)
    parser.add_argument("--profile", choices=("base",), default="base")
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument('--nfc-gt-stream-pilot',action='store_true')
    parser.add_argument("--next-layout", action="store_true",
                        help="Unqualified Full-only 152/312 KiB layout pilot; never an OTA migration")
    parser.add_argument(
        "--ota-verifier-fixture-public-header",
        type=Path,
        help=(
            "explicit non-production OTA verifier public-key header; intended "
            "only for incremental trust-root dependency qualification"
        ),
    )
    args = parser.parse_args()
    repo = args.repo.resolve()
    from onmicro_code_guards import (
        guard_linker as common_recovery_linker,
        guard_makefile as common_recovery_makefile,
        layout_choice as common_recovery_layout,
    )
    from onmicro_code_guards import guard_linker, guard_makefile, install_link_hook, layout_choice
    selection = SourceSelection(repo, args.profile)
    copy_file = selection.copy_file
    if args.profile == "base" and args.nfc_gt_stream_pilot:
        raise SystemExit("NFC pilot requires the private Pro profile")
    if args.ota_verifier_fixture_public_header is not None:
        raise SystemExit("CRC-only Recovery has no publisher verifier or fixture key")
    product_key_header = (
        repo / "firmware" / "common" / "security" /
        "inktag_activation_product_key.h"
    )
    registry = json.loads((repo / "firmware/boards/boards.json").read_text(encoding="utf-8"))
    board = registry.get("boards", {}).get(args.board)
    if args.board in ("om6626_zkcxxb", "om6626_zkc"):
        board = {"artifact": f"InkTag-{args.board}", "port": "onmicro/om6626"}
    if board is None or board.get("port") != "onmicro/om6626":
        raise SystemExit(f"board {args.board!r} is not an OM6626 board")
    header_name,board_view=compile_view(args.board)
    family_header = repo / "firmware/boards" / header_name
    board_header = family_header
    if not board_header.is_file():
        raise SystemExit(f"missing board compile config: {board_header}")
    subprocess.run(
        [sys.executable, str(repo / "tools/version/sync-version.py"), "--repo", str(repo)],
        check=True,
    )
    sdk = repo / ".sdk" / "onmicro" / "OM6626.SDK"
    if not sdk.exists():
        raise SystemExit(f"OM6626 SDK not found: {sdk}")

    # J-Link normally clears DEMCR.TRCENA on detach. The vendor delay helper
    # only checked DWT.CTRL.CYCCNTENA, so a retained CTRL bit could leave a
    # subsequent microsecond delay spinning forever on a stopped cycle counter.
    # Keep this SDK-only adaptation idempotent and shared by App and Recovery.
    cortex_path = sdk / "hal" / "driver" / "common" / "drv_cortex.h"
    cortex_source = cortex_path.read_text(encoding="utf-8")
    old_dwt_guard = """    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        CoreDebug->DEMCR |= (1U << CoreDebug_DEMCR_TRCENA_Pos);
        // enable DWT peripheral, enable Cortex-M DWT Cycle counter
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
"""
    new_dwt_guard = """    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) ||
        !(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        // enable DWT peripheral, enable Cortex-M DWT Cycle counter
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
"""
    if old_dwt_guard in cortex_source:
        write_if_changed(cortex_path,
                         cortex_source.replace(old_dwt_guard, new_dwt_guard, 1))
    elif new_dwt_guard not in cortex_source:
        raise SystemExit("unsupported OM SDK DWT delay guard; refusing to materialize")
    # The archive's already compiled inline delays do not consume this header.
    # Restore the separately qualified entry fix as well as mid-wait recovery.
    write_if_changed(cortex_path, repair_dwt_counter_reads(
        cortex_path.read_text(encoding="utf-8")))
    cortex_impl_path = sdk / "hal" / "driver" / "common" / "drv_cortex.c"
    write_if_changed(cortex_impl_path, repair_dwt_counter_implementation(
        cortex_impl_path.read_text(encoding="utf-8")))
    irq_path = sdk / "hal" / "driver" / "common" / "drv_isr.c"
    write_if_changed(irq_path, repair_ble_dwt_entry(
        irq_path.read_text(encoding="utf-8")))

    # The vendor blocking I2C read waits forever when the controller raises
    # TIME_OUT while INTR_MASK differs from a magic all-events value. InkTag
    # uses this API for a possibly absent external NFC tag, so preserve the
    # documented timeout contract and return the observed bus error instead.
    i2c_path = sdk / "hal" / "driver" / "common" / "drv_i2c.c"
    i2c_source = i2c_path.read_text(encoding="utf-8")
    old_i2c_timeout = """\twhile(!((drv_i2c_get_timeout_rawstate(om_i2c) && (om_i2c->INTR_MASK == 2303)) || (drv_i2c_get_timeout_rawstate(om_i2c) == 0)));
\tom_i2c->INTR_MASK = 0;
    om_i2c->INTR_MASK |= I2C_INTR_TIME_OUT_MASK;
    NVIC_ClearPendingIRQ(resource->irq_num);
    NVIC_SetPriority(resource->irq_num, resource->irq_prio);
    NVIC_EnableIRQ(resource->irq_num);
\tif (om_i2c->RAW_INTR_STAT & I2C_INTR_TIME_OUT_MASK) {
\t    return OM_ERROR_TIMEOUT;
\t}

    return OM_ERROR_OK;
"""
    new_i2c_timeout = """    bool timed_out = drv_i2c_get_timeout_rawstate(om_i2c) != 0;
    bool transfer_failed = drv_i2c_get_tx_abrt(om_i2c) != 0 ||
                           drv_i2c_get_rxunder_rawstate(om_i2c) != 0;
    om_i2c->INTR_MASK = 0;
    om_i2c->INTR_MASK |= I2C_INTR_TIME_OUT_MASK;
    NVIC_ClearPendingIRQ(resource->irq_num);
    NVIC_SetPriority(resource->irq_num, resource->irq_prio);
    NVIC_EnableIRQ(resource->irq_num);
    if (timed_out) return OM_ERROR_TIMEOUT;
    if (transfer_failed) return OM_ERROR_FAIL;
    return OM_ERROR_OK;
"""
    if old_i2c_timeout in i2c_source:
        write_if_changed(i2c_path, i2c_source.replace(old_i2c_timeout, new_i2c_timeout, 1))
    elif new_i2c_timeout not in i2c_source:
        raise SystemExit("Unexpected OM6626 I2C source: blocking-read timeout marker not found")

    # The ROM helper imports the per-chip CP/FT calibration record before ADC
    # users start. The vendor source documents this call but ships it commented
    # out, leaving the ADC gain/offset table zeroed and VBAT reads unusable.
    system_path = sdk / "hal" / "device" / "om6626" / "system.c"
    system_source = system_path.read_text(encoding="utf-8")
    old_system_post = "    //SystemInitPostLib();\n"
    direct_system_post = "    SystemInitPostLib();\n"
    new_system_post = """#if defined(INKTAG_OM_ENABLE_CPFT)
    SystemInitPostLib();
#endif
"""
    nested_system_post = re.compile(
        r"(?:#if defined\(INKTAG_OM_ENABLE_CPFT\)\n)+"
        r"    SystemInitPostLib\(\);\n"
        r"(?:#endif\n)+"
    )
    if old_system_post in system_source:
        system_source = system_source.replace(old_system_post, new_system_post, 1)
    elif nested_system_post.search(system_source):
        # Normalize earlier generated output as well as the already-correct
        # single guard.  Looking for the bare call first would wrap our own
        # generated block once more on every materialization.
        system_source = nested_system_post.sub(new_system_post, system_source, count=1)
    elif direct_system_post in system_source:
        system_source = system_source.replace(direct_system_post, new_system_post, 1)
    else:
        raise SystemExit(
            "Unexpected OM6626 system source: CP/FT import marker not found"
        )
    write_if_changed(system_path, normal_system_clock64(system_source))

    app_project_name = "ble_app_inktag" + ("_base" if args.profile == "base" else "")
    boot_project_name = "inktag_om_swap_boot" + ("_base" if args.profile == "base" else "")
    project = sdk / "projects" / app_project_name
    (project / "gcc").mkdir(parents=True, exist_ok=True)
    (project / "src" / "user").mkdir(parents=True, exist_ok=True)
    (project / "src" / "inktag_common").mkdir(parents=True, exist_ok=True)
    (project / "src" / "ota").mkdir(parents=True, exist_ok=True)
    (project / "src" / "dfu").mkdir(parents=True, exist_ok=True)

    user_src = repo / "firmware" / "ports" / "onmicro" / "common" / "src"
    variant = ("#ifndef INKTAG_OM_VARIANT_CONFIG_H\n"
               "#define INKTAG_OM_VARIANT_CONFIG_H\n" +
               "".join(f"#define {name} {value}\n" for name, value in selection.defines.items()) +
               # Maintained OM profiles have no private PACKED LUT/validate consumer.
               "#define INKTAG_LZO1X_VALIDATE_ENABLED 0\n" +
               ("#define INKTAG_OM_NEXT_LAYOUT 1\n"
                "#define INKTAG_RECOVERY_SESSION_RECORDS 1\n"
                "" if args.next_layout else "") +
               "#define INKTAG_GUI_ASSET_READ_FAILURE_AWARE 1\n"
               "#define INKTAG_GUI_ROLE_FONT_ASSETS 1\n"
               "#define INKTAG_GUI_NATIVE_FONT_ASSETS 1\n" +
               "#define INKTAG_EPD_ENABLE_HANSHOW_SMALL_Q 1\n"
               "#define INKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154 1\n"
               "#define INKTAG_EPD_ENABLE_HANSHOW_OLD_200 1\n"
               "#define INKTAG_EPD_ENABLE_STELLAR154 1\n"
               f"#define INKTAG_EPD_ENABLE_OSPREY_E6 {int(args.profile == 'pro')}\n"
               f"#define INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ {int(args.profile == 'pro')}\n"
               f"#define INKTAG_EPD_OSPREY_E6_PON_NO_WAIT {int(args.profile == 'pro')}\n" +
               "#endif\n")
    for name in ["inktag_om_profile.h", "inktag_om_board_init.c", "inktag_om_app.c", "inktag_om_app.h", "inktag_om_service.c", "inktag_om_service.h",
                 "inktag_om_image_store.c", "inktag_om_image_store.h",
                 "inktag_om_image_io.inc",
                 "inktag_om_external_flash.c", "inktag_om_external_flash.h",
                 "inktag_om_nfc.c", "inktag_om_nfc.h", "inktag_om_nfc_field.h",
                 "inktag_om_nfc_gt.inc", "inktag_om_nfc_boot_url.inc", "inktag_om_nfc_binding.inc", "inktag_om_nfc_event.inc",
                 "inktag_om_nfc_stream.inc", "inktag_om_nfc_image.inc",
                 "inktag_om_display_feedback.inc",
                 "inktag_om_status_pwm.inc",
                 "inktag_om_recovery_preflight.h",
                 "inktag_om_device_settings.c", "inktag_om_device_settings.h",
                 "inktag_om_qr_store.c",
                 "inktag_om_activation.c", "inktag_om_activation.h",
                  "inktag_om_brand.c", "inktag_om_brand.h",
                  "inktag_om_features.h", "inktag_om_findmy.c", "inktag_om_findmy.h",
                   "inktag_om_findmy_storage.h",
                  "inktag_om_p224_uecc.c", "inktag_om_p224_uecc.h",
                  "inktag_om_flash_layout.h",
                 "inktag_om_health.c", "inktag_om_health.h",
                 "inktag_om_nvds.c", "inktag_om_nvds.h",
                 "inktag_om_voltage.c", "inktag_om_voltage.h",
                 "inktag_om_dfu_diagnostics.c", "inktag_om_dfu_diagnostics.h",
                 "inktag_om_protocol_v2.inc",
                 "inktag_om_gui_assets.c", "inktag_om_gui_assets.h",
                 "inktag_om_pins.c", "inktag_om_pins.h", "inktag_om_pin_layout.h", "inktag_om_pin_layout.c",
                 "inktag_om_address.c", "inktag_om_address.h",
                 "inktag_om_board_identity.c", "inktag_om_board_identity.h",
                 "inktag_om_panel_idle.h",
                 "inktag_om_ble_reset.h",
                 "inktag_om_debug_retention.h",
                  "inktag_om_swap.c", "inktag_om_swap.h", "inktag_om_app_integrity.h",
                  "inktag_om_build_config.h", "epd_driver_onmicro.c", "sdk_config.h"]:
        copy_file(user_src / name, project / "src" / "user" / name)
    selection.include_assets(project / "src/user")
    # App-only image transport policy. Recovery keeps its original variant.
    variant = variant.replace('#endif\n','#define INKTAG_AUX_RESERVED_ENABLED 0\n#endif\n')
    app_variant = variant.replace('#endif\n',
        '#define INKTAG_TRANSFER_LZO1X_ENABLED 1\n' +
        ('#define INKTAG_EPD_TEMPERATURE_OPTION 1\n#define INKTAG_OM_NFC_STREAM_PILOT 1\n#define INKTAG_NFC_R16_STREAMING 1\n' if args.profile == "pro" else '') + '#endif\n')
    write_if_changed(
        project / "src" / "user" / "inktag_om_variant_config.h",
        app_variant,
    )
    copy_file(repo / "firmware/common/ble/inktag_ble_address_uid_compact.c",
              project / "src/inktag_common/inktag_ble_address_uid_compact.c")
    write_if_changed(project / "src/user/inktag_om_ota_policy.h",
                     family_section(family_header, "policy"))
    write_if_changed(project / "src" / "inktag_common" / "inktag_board.h",
                     family_section(board_header, board_view))
    # This descriptor is still usable only by the existing runtime 42B gate.
    write_if_changed(project / "src/user/inktag_om_external_flash_board.h",
                     family_section(repo / "firmware/boards/om6626_zkel.h", "flash"))

    ota_src = repo / "firmware" / "common" / "ota"
    for name in ["inktag_recovery.c", "inktag_recovery.h", "inktag_swap.h",
                 "inktag_ota_reject.h"]:
        copy_file(ota_src / name, project / "src" / "ota" / name)

    input_src = repo / "firmware" / "common" / "input"
    for name in ["inktag_input.c", "inktag_input.h", "inktag_ndef.c",
                 "inktag_ndef.h", "inktag_nfc_link.c", "inktag_nfc_link.h",
                 "inktag_nfc_command.c",
                 "inktag_nfc_command.h", "inktag_nfc_volatile_command.c",
                 "inktag_nfc_volatile_command.h", "inktag_gt_mirror.c",
                 "inktag_gt_mirror.h", "inktag_nfc_binding.c",
                 "inktag_nfc_binding.h", "inktag_type4_ndef.c", "inktag_type4_ndef.h",
                 "inktag_type4_transport.c", "inktag_type4_transport.h",
                 "inktag_type4_apdu.c", "inktag_type4_apdu.h",
                 "inktag_nfc_fmsc.c", "inktag_nfc_fmsc.h",
                 "inktag_gt_stream.c", "inktag_gt_stream.h",
                 "inktag_gt_pipe.c", "inktag_gt_pipe.h"]:
        copy_file(input_src / name, project / "src" / "inktag_common" / name)

    if args.profile == "pro":
        # Frozen dual providers are private include closures, not new objects.
        for name in ("inktag_nfc_r16_profile.h",
                     "inktag_nfc_fmsc_c_base_pro.inc", "inktag_nfc_fmsc_h_base_pro.inc",
                     "inktag_nfc_fmsc_c_r16_pro.inc", "inktag_nfc_fmsc_h_r16_pro.inc",
                     "inktag_type4_apdu_c_base_pro.inc", "inktag_type4_apdu_h_base_pro.inc",
                     "inktag_type4_apdu_c_r16_pro.inc", "inktag_type4_apdu_h_r16_pro.inc"):
            copy_file(repo / "firmware/common_pro/input" / name,
                      project / "src/inktag_common" / name)
        copy_file(repo / "firmware/common_pro/storage/inktag_image_fixed_stream_pro.inc",
                  project / "src/inktag_common/inktag_image_fixed_stream_pro.inc")

    status_src = repo / "firmware" / "common" / "status"
    for name in ["inktag_status.c", "inktag_status.h", "inktag_temperature_wire.h"]:
        copy_file(status_src / name, project / "src" / "inktag_common" / name)

    health_src = repo / "firmware" / "common" / "health"
    for name in ["inktag_soak_telemetry.c", "inktag_soak_telemetry.h"]:
        copy_file(health_src / name, project / "src" / "inktag_common" / name)

    common_groups = [
        (repo / "firmware" / "common" / "time",
         ["inktag_wall_clock.h", "inktag_time_checkpoint.h"]),
        (repo / "firmware" / "common" / "platform",
         ["inktag_elapsed_clock.c", "inktag_elapsed_clock.h", "inktag_timebase.h"]),
        (repo / "firmware" / "common" / "power",
         ["inktag_power.c", "inktag_power.h"]),
        (repo / "firmware" / "common" / "ble",
         ["inktag_findmy.c", "inktag_findmy.h",
           "inktag_findmy_public.c", "inktag_findmy_public.h",
           "inktag_ble_identity.c", "inktag_ble_identity.h", "inktag_ble_power.h",
           "inktag_ble_address.c", "inktag_ble_address.h",
           "inktag_device_display_name.h"]),
        (repo / "firmware" / "common" / "storage",
         ["inktag_settings_records.c", "inktag_settings_records.h",
          "inktag_settings_journal.h", "inktag_resource_name.h",
          "inktag_image_fixed_store.h", "inktag_image_fixed_store.inc"]),
        (repo / "firmware" / "common" / "protocol",
         ["inktag_protocol_core.c", "inktag_protocol_core.h",
          "inktag_pin_config.c", "inktag_pin_config.h", "inktag_release_v1.h",
          "inktag_aux_io.c", "inktag_aux_io.h",
          "inktag_image_upload_v5.c", "inktag_image_upload_v5.h",
          "inktag_packbits.c", "inktag_packbits.h", "inktag_lzo1x.c", "inktag_lzo1x.h",
          "inktag_display_settings.c", "inktag_display_settings.h"]),
        (repo / "firmware" / "common" / "lifecycle",
         ["inktag_lifecycle.c", "inktag_lifecycle.h",
          "inktag_lifecycle_pending.c", "inktag_lifecycle_pending.h"]),
        (repo / "firmware" / "common" / "security",
         ["inktag_code_guard.c", "inktag_code_guard.h", "inktag_activation.c", "inktag_activation.h",
          "inktag_device_identity.inc", "inktag_device_password.c", "inktag_device_password.h",
          "inktag_device_password_store.c", "inktag_device_password_store.h",
          "inktag_access.c", "inktag_access.h",
          "inktag_image_quota.c", "inktag_image_quota.h",
          "inktag_brand_overlay.c", "inktag_brand_overlay.h",
          "inktag_sha256.c", "inktag_sha256.h",
          "inktag_firmware_signature.h"]),
    ]
    for source_dir, names in common_groups:
        for name in names:
            copy_file(source_dir / name,
                      project / "src" / "inktag_common" / name)
    if args.profile == "base":
        copy_file(repo / "firmware/common/storage/inktag_resource_name.c",
                  project / "src/inktag_common/inktag_resource_name.c")
    # Password's source-relative common include also works in the SDK's flat
    # materialization. Same authoritative header; no protocol fork.
    copy_file(repo / "firmware/common/epd/inktag_protocol_v2.h",
              project / "src/epd/inktag_protocol_v2.h")
    copy_file(
        product_key_header,
        project / "src" / "inktag_common" /
        "inktag_activation_product_key.h",
    )

    vendor_dfu = sdk / "projects" / "common" / "om_dfu"
    for name in ["onmicro_dfu.h", "onmicro_dfu_nvds.h",
                 "service_onmicro_dfu.h"]:
        copy_file(vendor_dfu / name, project / "src" / "dfu" / name)
    dfu_header_path = project / "src" / "dfu" / "onmicro_dfu.h"
    dfu_header = dfu_header_path.read_text(encoding="utf-8")
    old_response_union = """union dfu_rsp_data
{
    struct dfu_rsp_checksum checksum;
    struct dfu_rsp_select select_data;
};
"""
    new_response_union = """union dfu_rsp_data
{
    struct dfu_rsp_checksum checksum;
    struct dfu_rsp_select select_data;
    /* The response starts two bytes before result. Result is the first byte of
     * the common 15-byte rejection record; keep room for its remaining 14. */
    uint8_t inktag_reject_tail[14];
};
"""
    if old_response_union not in dfu_header:
        raise SystemExit("Unexpected OM6626 DFU response union")
    dfu_header = dfu_header.replace(old_response_union, new_response_union, 1)
    select_opcode = "#define DFU_CTRL_SELECT              0x06\n"
    if select_opcode not in dfu_header:
        raise SystemExit("Unexpected OM6626 DFU header: SELECT opcode marker")
    dfu_header = dfu_header.replace(
        select_opcode,
        select_opcode + "#define DFU_CTRL_SAFE_RESUME_APP     0x07\n",
        1,
    )
    write_if_changed(dfu_header_path, dfu_header)
    service_header_path = project / "src" / "dfu" / "service_onmicro_dfu.h"
    service_header = service_header_path.read_text(encoding="utf-8")
    service_end_decl = (
        "extern void app_onmicro_dfu_update_end_ind_handler(uint8_t status, void *p);\n"
    )
    if service_end_decl not in service_header:
        raise SystemExit("Unexpected OM6626 DFU service header: end callback marker")
    service_header = service_header.replace(
        service_end_decl,
        service_end_decl
        + "/* PROGRAM_START rejected before any application mutation. */\n"
        + "extern bool app_onmicro_dfu_safe_resume_handler(bool rejected);\n"
        + "extern void app_onmicro_dfu_safe_resume_tx_complete(void);\n",
        1,
    )
    write_if_changed(service_header_path, service_header)
    write_if_changed(project / "src" / "dfu" / "onmicro_dfu_config.h", DFU_CONFIG)

    # The vendor data path advances its received offset and CRC even when its
    # storage backend rejects an erase or program operation.  That turns a
    # real low-voltage/storage failure into a misleading 100%-then-CRC-error.
    # Propagate the backend result so the next PRN/checksum exposes that no
    # durable progress was made.
    dfu_path = project / "src" / "dfu" / "onmicro_dfu.c"
    dfu = (vendor_dfu / "onmicro_dfu.c").read_text(encoding="utf-8")
    old_address_selector = """static uint32_t get_new_img_address(dfu_cmd_img_t *img, const dfu_image_t *cmd_img_info)
{
\t// base_address2 is used if no info saved or read info failed
\tuint32_t new_addr = cmd_img_info->base_address2;
\tif(img->type < IMAGE_TYPE_MBR_MAX){ // app,patch,cfg info should be read from MBR
\t\tconst dfu_nvds_itf_t *nvds_itf = (const dfu_nvds_itf_t*)&dfu_nvds_itf[DFU_NVDS_ITF_TYPE_MBR];
\t\tuint32_t len = sizeof(dfu_image_mbr_info);
\t\tdfu_image_mbr_info info;
\t\tnvds_itf->enable();
\t\tuint8_t res = nvds_itf->get(img->type, &len, &info);
\t\tnvds_itf->disable();
\t\tdfu_assert(res != ONMICRO_DFU_NVDS_ST_SUCCESS || len == sizeof(dfu_image_mbr_info));
\t\tif(res == ONMICRO_DFU_NVDS_ST_SUCCESS){
\t\t\tif(cmd_img_info->base_address2 <= info.address &&
\t\t\t\t\tinfo.address <= cmd_img_info->base_address2 + cmd_img_info->max_length){
\t\t\t\tnew_addr = cmd_img_info->base_address1;
\t\t\t}
\t\t}
\t}else if(img->type < IMAGE_TYPE_RAW){ // IMAGE_TYPE_CUSTOM images info read from it's own nvds itf
\t\tconst dfu_nvds_itf_t *nvds_itf = cmd_img_info->info_ops_itf;
\t\tif(nvds_itf){
\t\t\tuint32_t len = sizeof(dfu_image_info);
\t\t\tdfu_image_info info;
\t\t\tnvds_itf->enable();
\t\t\tuint8_t res = nvds_itf->get(img->type, &len, &info);
\t\t\tnvds_itf->disable();
\t\t\tdfu_assert(res != ONMICRO_DFU_NVDS_ST_SUCCESS || len == sizeof(dfu_image_info));
\t\t\tif(res == ONMICRO_DFU_NVDS_ST_SUCCESS){
\t\t\t\tif(cmd_img_info->base_address2 <= info.address &&
\t\t\t\t\t\tinfo.address <= cmd_img_info->base_address2 + cmd_img_info->max_length){
\t\t\t\t\tnew_addr = cmd_img_info->base_address1;
\t\t\t\t}
\t\t\t}
\t\t}
\t}else if(img->type == IMAGE_TYPE_RAW){
        new_addr = cmd_img_info->base_address1;
    }else{
        dfu_assert(0);
    }
    return new_addr;
}
"""
    fixed_address_selector = """/* InkTag fixed Recovery accepts one application
 * type at INKTAG_DFU_APPLICATION_ADDRESS. The vendor dual-slot/MBR selector
 * and RAW arbitrary-address route are intentionally absent. */
"""
    if old_address_selector not in dfu:
        raise SystemExit("Unexpected OM6626 DFU engine: address selector marker not found")
    dfu = dfu.replace(old_address_selector, fixed_address_selector, 1)

    # The public fixed Recovery supports exactly one application at one
    # address. Specialize the vendor's multi-image/RAW cursor bookkeeping;
    # the shared DFU framing, CRC/PRN and reconnect offset remain unchanged.
    image_cursor_start = dfu.find("static void update_env_img_info_by_idx(void)")
    image_cursor_end = dfu.find(
        "static void write_image_data(const uint8_t *data, uint32_t len)",
        image_cursor_start,
    )
    if image_cursor_start < 0 or image_cursor_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: image cursor marker not found")
    fixed_image_cursor = """static void update_env_img_info_by_idx(void)
{
    dfu_cmd_img_t *img = (dfu_cmd_img_t*)&p_env->cmd_obj_buffer[DFU_CMDPKG_OFFSET_PKG];
    p_env->cmd_img_size = img->size;
    p_env->cmd_img_recv_len = 0;
    p_env->cmd_img_info = &dfu_image_types[0];
    p_env->image_ops_itf = dfu_image_types[0].image_ops_itf;
    p_env->cmd_img_new_addr = INKTAG_DFU_APPLICATION_ADDRESS;
    img_operation_notify(img->type, 0, INKTAG_DFU_APPLICATION_ADDRESS, img->size);
}
"""
    dfu = (dfu[:image_cursor_start] + fixed_image_cursor +
           dfu[image_cursor_end:])
    function_start = dfu.find("static void write_image_data(const uint8_t *data, uint32_t len)")
    function_end = dfu.find("static void update_env_img_info_by_cache(void)", function_start)
    if function_start < 0 or function_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: image writer marker not found")
    writer = dfu[function_start:function_end]
    writer = writer.replace(
        "static void write_image_data(const uint8_t *data, uint32_t len)",
        "static bool write_image_data(const uint8_t *data, uint32_t len)",
        1,
    )
    old_writer_io = """            write_itf->enable();
            uint32_t write_addr = p_env->cmd_img_new_addr+p_env->cmd_img_recv_len-p_env->cache_recv_len;
#if FLASH_ERASE_SIZE
            uint32_t erase_addr = (write_addr + FLASH_ERASE_SIZE - 1) & ~(FLASH_ERASE_SIZE - 1);
            for(;erase_addr < write_addr + p_env->cache_recv_len;erase_addr += FLASH_ERASE_SIZE){
                dfu_debug("Erasing 0x%08X-0x%08X (%s)\\n", erase_addr, erase_addr+FLASH_ERASE_SIZE, p_env->cmd_img_info->describe);
                write_itf->del(erase_addr, FLASH_ERASE_SIZE);
            }
#endif
            int res = write_itf->put(write_addr, p_env->cache_recv_len, m_cache);
            if(res != ONMICRO_DFU_NVDS_ST_SUCCESS){
                dfu_debug("Write image failed.\\n");
                break;
            }
            write_itf->disable();
"""
    new_writer_io = """            if(write_itf->enable() != ONMICRO_DFU_NVDS_ST_SUCCESS){
                return false;
            }
            uint32_t write_addr = p_env->cmd_img_new_addr+p_env->cmd_img_recv_len-p_env->cache_recv_len;
#if FLASH_ERASE_SIZE
            uint32_t erase_addr = (write_addr + FLASH_ERASE_SIZE - 1) & ~(FLASH_ERASE_SIZE - 1);
            for(;erase_addr < write_addr + p_env->cache_recv_len;erase_addr += FLASH_ERASE_SIZE){
                dfu_debug("Erasing 0x%08X-0x%08X (%s)\\n", erase_addr, erase_addr+FLASH_ERASE_SIZE, p_env->cmd_img_info->describe);
                if(write_itf->del(erase_addr, FLASH_ERASE_SIZE) != ONMICRO_DFU_NVDS_ST_SUCCESS){
                    write_itf->disable();
                    return false;
                }
            }
#endif
            int res = write_itf->put(write_addr, p_env->cache_recv_len, m_cache);
            if(res != ONMICRO_DFU_NVDS_ST_SUCCESS){
                dfu_debug("Write image failed.\\n");
                write_itf->disable();
                return false;
            }
            if(write_itf->disable() != ONMICRO_DFU_NVDS_ST_SUCCESS){
                return false;
            }
"""
    if old_writer_io not in writer:
        raise SystemExit("Unexpected OM6626 DFU engine: image storage marker not found")
    writer = writer.replace(old_writer_io, new_writer_io, 1)
    writer_tail = """        }
    }
}
"""
    if not writer.endswith(writer_tail):
        raise SystemExit("Unexpected OM6626 DFU engine: image writer tail not found")
    writer = writer[:-len(writer_tail)] + """        }
    }
    return true;
}
"""
    dfu = dfu[:function_start] + writer + dfu[function_end:]

    cached_cursor_start = dfu.find("static void update_env_img_info_by_cache(void)")
    cached_cursor_end = dfu.find(
        "static uint32_t cal_cmd_obj_size(uint8_t *cmd)", cached_cursor_start
    )
    if cached_cursor_start < 0 or cached_cursor_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: cached cursor marker not found")
    fixed_cached_cursor = """static void update_env_img_info_by_cache(void)
{
    dfu_cmd_img_t *img = (dfu_cmd_img_t*)&p_env->cmd_obj_buffer[DFU_CMDPKG_OFFSET_PKG];
    p_env->obj_crc32 = m_data_crc;
    p_env->obj_recv_len = m_data_offset;
    p_env->obj_size = img->size;
    p_env->cmd_img_idx = 0;
    p_env->cmd_img_size = img->size;
    p_env->cmd_img_recv_len = m_data_offset;
    p_env->cmd_img_info = &dfu_image_types[0];
    p_env->image_ops_itf = dfu_image_types[0].image_ops_itf;
    p_env->cmd_img_new_addr = INKTAG_DFU_APPLICATION_ADDRESS;
    img_operation_notify(img->type, 0, INKTAG_DFU_APPLICATION_ADDRESS, img->size);
}
"""
    dfu = (dfu[:cached_cursor_start] + fixed_cached_cursor +
           dfu[cached_cursor_end:])
    old_write_call = """    }else if(p_env->obj_actived == DFU_PKG_ACTIVED_DATA){
        write_image_data(data, len);
        struct {
"""
    new_write_call = """    }else if(p_env->obj_actived == DFU_PKG_ACTIVED_DATA){
        if(!write_image_data(data, len)){
            return;
        }
        struct {
"""
    if old_write_call not in dfu:
        raise SystemExit("Unexpected OM6626 DFU engine: data progress marker not found")
    dfu = dfu.replace(old_write_call, new_write_call, 1)

    create_start = dfu.find(
        "            }else if(type == DFU_PKG_ACTIVED_DATA){\n"
    )
    create_end_marker = (
        "            }else{\n"
        "                response->result = DFU_INVALID_PARAMETER;\n"
        "            }\n"
        "        }   break;"
    )
    create_end = dfu.find(create_end_marker, create_start)
    if create_start < 0 or create_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: DATA CREATE marker not found")
    fixed_data_create = """            }else if(type == DFU_PKG_ACTIVED_DATA){
                uint16_t img_cnt = *(uint16_t*)&p_env->cmd_obj_buffer[DFU_CMDPKG_OFFSET_IMGCNT];
                dfu_cmd_img_t *img = (dfu_cmd_img_t*)&p_env->cmd_obj_buffer[DFU_CMDPKG_OFFSET_PKG];
                p_env->cmd_img_idx = -1;
                p_env->cmd_img_size = p_env->cmd_img_recv_len = p_env->cache_recv_len = 0;
                m_data_offset = m_data_crc = 0;
                if(!p_env->cmd_obj_buffer_valid || img_cnt != 1u ||
                   img->type != IMAGE_TYPE_CUSTOM || img->size != length){
                    response->result = DFU_INVALID_OBJECT;
                }
"""
    dfu = dfu[:create_start] + fixed_data_create + dfu[create_end:]

    validate_start = dfu.find(
        "                    uint32_t exp_length = cal_cmd_obj_size(cmd);\n"
    )
    validate_end = dfu.find(
        "                }else if(p_env->obj_actived == DFU_PKG_ACTIVED_DATA){\n",
        validate_start,
    )
    if validate_start < 0 or validate_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: command validation marker not found")
    fixed_validation = """                    uint32_t exp_length = cal_cmd_obj_size(cmd);
                    uint16_t img_cnt = *(uint16_t*)&cmd[DFU_CMDPKG_OFFSET_IMGCNT];
                    dfu_cmd_img_t *img = (dfu_cmd_img_t*)&cmd[DFU_CMDPKG_OFFSET_PKG];
                    if(exp_length != p_env->obj_recv_len || img_cnt != 1u){
                        response->result = DFU_INVALID_PARAMETER;
                    }else if(img->type != IMAGE_TYPE_CUSTOM){
                        response->result = DFU_UNSUPPORTED_TYPE;
                    }else{
                        response->result = dfu_cb_itf.dfu_user_check_cb(
                            img->type, img->size, img->version);
                        if(response->result == DFU_SUCCESS){
                            p_env->cmd_obj_buffer_valid = true;
                        }
                    }
"""
    dfu = dfu[:validate_start] + fixed_validation + dfu[validate_end:]

    verify_start = dfu.find(
        "                    m_data_offset = 0; // m_cache used for cal crc32, so reset offest.\n",
        validate_start,
    )
    verify_end = dfu.find(
        "                }else{\n"
        "                    response->result = DFU_OPERATION_FAILED;\n",
        verify_start,
    )
    if verify_start < 0 or verify_end < 0:
        raise SystemExit("Unexpected OM6626 DFU engine: DATA EXECUTE marker not found")
    fixed_data_verify = """                    m_data_offset = 0;
                    if(!p_env->cmd_obj_buffer_valid){
                        response->result = DFU_INVALID_PARAMETER;
                    }else{
                        dfu_cmd_img_t *img = (dfu_cmd_img_t*)&p_env->cmd_obj_buffer[DFU_CMDPKG_OFFSET_PKG];
                        const dfu_nvds_itf_t *flash_itf =
                            dfu_image_types[0].image_ops_itf;
                        uint32_t address = INKTAG_DFU_APPLICATION_ADDRESS;
                        uint32_t end = address + img->size;
                        uint32_t obj_crc32 = 0;
                        flash_itf->enable();
                        while(address < end){
                            uint32_t length = MIN(DFU_CACHE_BUF_SIZE, end - address);
                            flash_itf->get(address, &length, m_cache);
                            if(length == 0u) break;
                            obj_crc32 = dfu_crc32(m_cache, length, &obj_crc32);
                            address += length;
                        }
                        flash_itf->disable();
                        if(address != end || obj_crc32 != p_env->obj_crc32){
                            response->result = DFU_INSUFFICIENT_RESOURCES;
                        }else{
                            dfu_reset(DFU_UPDATE_ST_SUCCESS);
                        }
                    }
"""
    dfu = dfu[:verify_start] + fixed_data_verify + dfu[verify_end:]
    write_if_changed(dfu_path, dfu)

    # OM6626 executes the application and its vector table directly from the
    # same serial Flash used for slot B.  If a BLE interrupt arrives while the
    # controller is doing an explicit read, erase or program operation, the
    # vector fetch can return invalid data and the core enters HardFault.  Keep
    # each vendor ROM Flash operation in a short critical section; a 4 KiB
    # erase remains far below the BLE supervision timeout, and interrupts
    # resume as soon as XIP is readable.
    nvds_path = project / "src" / "dfu" / "onmicro_dfu_nvds.c"
    nvds = (vendor_dfu / "onmicro_dfu_nvds.c").read_text(encoding="utf-8")
    nvds_include = '#include "onmicro_dfu_nvds.h"\n'
    if nvds_include not in nvds:
        raise SystemExit("Unexpected OM6626 DFU NVDS source: include marker not found")
    nvds = nvds.replace(
        nvds_include,
        nvds_include + '#include "om.h"\n'
        '#include "inktag_om_dfu_diagnostics.h"\n'
        '#include "inktag_om_swap.h"\n'
        '#include "inktag_om_voltage.h"\n',
        1,
    )
    nvds_helpers_marker = "/*********************************************************************\n * LOCAL VARIABLES\n */\n"
    nvds_helpers = """static bool inktag_dfu_flash_allowed(
    inktag_ota_reject_stage_t stage, uint8_t detail_flags, uint32_t detail)
{
    uint16_t sampled_mv = 0u;
    bool sampled_mv_valid = false;
    if (inktag_om_voltage_operation_check(INKTAG_POWER_OPERATION_FLASH,
                                           &sampled_mv,
                                           &sampled_mv_valid)) {
        return true;
    }
    inktag_om_dfu_reject_set_power(
        stage, detail_flags, detail, sampled_mv, sampled_mv_valid,
        INKTAG_POWER_POLICY_FLASH_MIN_MV);
    return false;
}

""" + nvds_helpers_marker
    if nvds_helpers_marker not in nvds:
        raise SystemExit("Unexpected OM6626 DFU NVDS local marker")
    nvds = nvds.replace(nvds_helpers_marker, nvds_helpers, 1)
    old_get = """static uint8_t onmicro_dfu_nvds_get_flash(uint32_t addr, uint32_t *lengthPtr, void *buf)
{
    drv_sfs_read(addr, buf, *lengthPtr);
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    new_get = """static uint8_t onmicro_dfu_nvds_get_flash(uint32_t addr, uint32_t *lengthPtr, void *buf)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_read(addr, buf, *lengthPtr);
    __DSB();
    __ISB();
    if (primask == 0) __enable_irq();
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    old_put = """static uint8_t onmicro_dfu_nvds_put_flash(uint32_t addr, uint32_t length, void *buf)
{
    drv_sfs_write(addr, buf, length);
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    new_put = """static uint8_t onmicro_dfu_nvds_put_flash(uint32_t addr, uint32_t length, void *buf)
{
    if (!inktag_dfu_flash_allowed(
            INKTAG_OTA_REJECT_STAGE_PROGRAM_WRITE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, addr) ||
        !inktag_om_swap_mark_application_mutated()) {
        return ONMICRO_DFU_NVDS_ST_FAILED;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_write(addr, buf, length);
    __DSB();
    __ISB();
    if (primask == 0) __enable_irq();
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    old_erase = """static uint8_t onmicro_dfu_nvds_erase_flash(uint32_t addr, uint32_t length)
{
    drv_sfs_erase(addr, length);
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    new_erase = """static uint8_t onmicro_dfu_nvds_erase_flash(uint32_t addr, uint32_t length)
{
    if (!inktag_dfu_flash_allowed(
            INKTAG_OTA_REJECT_STAGE_ERASE,
            INKTAG_OTA_REJECT_FLAG_DETAIL_IS_ADDRESS, addr) ||
        !inktag_om_swap_mark_application_mutated()) {
        return ONMICRO_DFU_NVDS_ST_FAILED;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    __DSB();
    __ISB();
    drv_sfs_erase(addr, length);
    __DSB();
    __ISB();
    if (primask == 0) __enable_irq();
    return ONMICRO_DFU_NVDS_ST_SUCCESS;
}
"""
    if old_get not in nvds or old_put not in nvds or old_erase not in nvds:
        raise SystemExit("Unexpected OM6626 DFU NVDS source: Flash operation marker not found")
    nvds = (nvds.replace(old_get, new_get, 1)
            .replace(old_put, new_put, 1)
            .replace(old_erase, new_erase, 1))
    old_put_mbr = """static uint8_t onmicro_dfu_nvds_put_mbr(uint32_t id, uint32_t length, void *buf)
{
    dfu_image_mbr_info *info = (dfu_image_mbr_info*)buf;
    if(id < sizeof(mbr_types)/sizeof(mbr_types[0])){
        mbr_write_part(mbr_types[id], info->address, info->length, info->crc16);
        return ONMICRO_DFU_NVDS_ST_SUCCESS;
    }else{
        return ONMICRO_DFU_NVDS_ST_FAILED;
    }
}
"""
    new_put_mbr = """static uint8_t onmicro_dfu_nvds_put_mbr(uint32_t id, uint32_t length, void *buf)
{
    dfu_image_mbr_info *info = (dfu_image_mbr_info*)buf;
    if(id < sizeof(mbr_types)/sizeof(mbr_types[0]) &&
       inktag_dfu_flash_allowed(
           INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u, id)){
        mbr_write_part(mbr_types[id], info->address, info->length, info->crc16);
        return ONMICRO_DFU_NVDS_ST_SUCCESS;
    }else{
        return ONMICRO_DFU_NVDS_ST_FAILED;
    }
}
"""
    if old_put_mbr not in nvds:
        raise SystemExit("Unexpected OM6626 DFU NVDS source: MBR put marker not found")
    nvds = nvds.replace(old_put_mbr, new_put_mbr, 1)
    write_if_changed(nvds_path, nvds)

    service_path = project / "src" / "dfu" / "service_onmicro_dfu.c"
    # Patch from the immutable vendor source in memory. Copying the vendor file
    # over the already-patched generated file on every materialization would
    # change its mtime and needlessly rebuild this translation unit.
    service = (vendor_dfu / "service_onmicro_dfu.c").read_text(encoding="utf-8")
    include_marker = '#include "service_onmicro_dfu.h"\n'
    if include_marker not in service:
        raise SystemExit("Unexpected OM6626 DFU service: include marker not found")
    service = service.replace(
        include_marker,
        include_marker + '#include "inktag_om_dfu_diagnostics.h"\n'
        '#include "inktag_om_swap.h"\n'
        '#include "inktag_om_recovery_preflight.h"\n'
        '#include "inktag_om_voltage.h"\n',
        1,
    )
    # Append Recovery-only standard Software Revision attributes. Existing
    # service indices, UUIDs and permissions remain byte-for-byte unchanged.
    for marker, addition in (
        ("    IDX_DFU_VER_DESC,\n", "#if defined(INKTAG_OM_FIXED_RECOVERY)\n"
         "    IDX_RECOVERY_SOFTWARE_CHAR,\n    IDX_RECOVERY_SOFTWARE_VAL,\n#endif\n"),
        ("    static const ob_gatt_item_t atts_dfu[] = {\n",
         "#if defined(INKTAG_OM_FIXED_RECOVERY)\n"
         "    static const uint8_t software_revision[2] = {0x28, 0x2a};\n#endif\n"),
    ):
        if service.count(marker) != 1:
            raise SystemExit("Unexpected OM6626 DFU software-version declaration marker")
        service = service.replace(marker, marker + addition if "IDX_" in marker
                                  else addition + marker, 1)
    attribute_tail = "    };\n    static const ob_gatt_serv_t att_serv_dfu = {\n"
    if service.count(attribute_tail) != 1:
        raise SystemExit("Unexpected OM6626 DFU software-version attribute tail")
    service = service.replace(attribute_tail,
        "#if defined(INKTAG_OM_FIXED_RECOVERY)\n"
        "        { ob_att_char_def, OB_UUID_16BIT, OB_ATT_PROP_READ },\n"
        "        { software_revision, OB_UUID_16BIT, OB_ATT_PROP_READ },\n"
        "#endif\n" + attribute_tail, 1)
    preflight_marker = "void service_onmicro_dfu_evt_cb(uint16_t evt_id, const omble_evt_t *evt)\n{\n"
    if preflight_marker not in service:
        raise SystemExit("Unexpected OM6626 DFU service: event callback marker not found")
    service = service.replace(
        preflight_marker,
        preflight_marker + "#if defined(INKTAG_OM_FIXED_RECOVERY)\n"
        "    if (inktag_om_recovery_preflight_read(evt_id, evt,\n"
        "            m_start_handle + IDX_DFU_VER_VAL)) return;\n"
        "    if (inktag_om_recovery_software_read(evt_id, evt,\n"
        "            m_start_handle + IDX_RECOVERY_SOFTWARE_VAL)) return;\n"
        "#endif\n", 1,
    )
    service_state_marker = "static uint8_t m_end_state = NOT_END;\n"
    if service_state_marker not in service:
        raise SystemExit("Unexpected OM6626 DFU service: end-state marker not found")
    service = service.replace(
        service_state_marker,
        service_state_marker
        + "static bool m_safe_resume_after_response;\n"
        + "static bool m_dfu_transfer_active;\n"
        + "static uint8_t m_return_kind; /* 1: untouched resume, 2: committed OTA */\n"
        + "static uint16_t m_return_tx_id;\n"
        + "static uint16_t m_tx_sequence;\n",
        1,
    )
    response_marker = "    ob_gatts_send_hvx(0, &hvx);\n"
    # The original response helper ignored queue failure and omitted the SDK
    # TX_COMPLETE correlation id. Version notifications remain non-terminal.
    if service.count(response_marker) != 2:
        raise SystemExit("Unexpected OM6626 DFU response submission markers")
    service = service.replace("        len,\n    };", "        len,\n        0u,\n    };")
    service = service.replace(response_marker, """    if (++m_tx_sequence == 0u) ++m_tx_sequence;
    hvx.id = m_tx_sequence;
    if (m_return_kind != 0u) m_return_tx_id = hvx.id;
    if (ob_gatts_send_hvx(0, &hvx) != 0u && m_return_kind != 0u) {
        m_return_tx_id = 0u;
        m_return_kind = 0u;
        m_end_state = END_RESET; /* Failed submission: stay, never infer ACK. */
    }
""", 1)
    service = service.replace(
        "    if (evt_id == OB_GATTS_EVT_WRITE_REQ) {\n",
        "    if (evt_id == OB_GATTS_EVT_WRITE_REQ) {\n"
        "        if (m_return_kind != 0u || m_end_state == END_RESET) return;\n",
        1)
    service = service.replace(
        "        uint16_t len = evt->gatt.write_req.len;\n",
        "        uint16_t len = evt->gatt.write_req.len;\n"
        "        if (data == NULL || len == 0u) return;\n", 1)
    old_control_response = """            case IDX_DFU_CTRL_VAL:
                dfu_write_cmd((uint8_t *)data, len, &dfu_rsp_data);
                dfu_response(&dfu_rsp_data);
                break;
"""
    new_control_response = """            case IDX_DFU_CTRL_VAL:
                if (len == 1u && data[0] == DFU_CTRL_SAFE_RESUME_APP) {
                    dfu_rsp_data.rsp_code = DFU_CTRL_RESPONSE;
                    dfu_rsp_data.opcode = DFU_CTRL_SAFE_RESUME_APP;
                    dfu_rsp_data.result = DFU_OPERATION_NOT_PERMITTED;
                    dfu_rsp_data.length = DFU_RESP_SIZE_NO_EXT_DATA;
                    if (!m_dfu_transfer_active &&
                        app_onmicro_dfu_safe_resume_handler(false)) {
                        dfu_rsp_data.result = DFU_SUCCESS;
                        m_return_kind = 1u;
                    }
                    dfu_response(&dfu_rsp_data);
                    break;
                }
                dfu_write_cmd((uint8_t *)data, len, &dfu_rsp_data);
                inktag_ota_reject_info_t reject_info;
                if (inktag_om_dfu_reject_take(&reject_info)) {
                    uint8_t reject_len = inktag_ota_reject_encode(
                        &dfu_rsp_data.result,
                        INKTAG_OTA_REJECT_EXTENSION_WIRE_SIZE,
                        DFU_OPERATION_FAILED, &reject_info);
                    if (reject_len != 0u) {
                        /* rsp_code + opcode precede the encoded result byte. */
                        dfu_rsp_data.length = (uint8_t)(2u + reject_len);
                    } else {
                        dfu_rsp_data.result = DFU_OPERATION_FAILED;
                        dfu_rsp_data.length = DFU_RESP_SIZE_NO_EXT_DATA;
                    }
                }
                /* The vendor engine builds its response before dfu_reset()
                 * invokes our candidate-prepare callback. Surface a failed
                 * Recovery journal commit instead of reporting false success. */
                if (m_end_state == DFU_UPDATE_ST_FAILED &&
                    dfu_rsp_data.result == DFU_SUCCESS) {
                    dfu_rsp_data.result = DFU_OPERATION_FAILED;
                }
                if (m_safe_resume_after_response) {
                    m_safe_resume_after_response = false;
                    if (app_onmicro_dfu_safe_resume_handler(true))
                        m_return_kind = 1u;
                } else if (m_end_state == DFU_UPDATE_ST_SUCCESS) {
                    m_return_kind = 2u;
                } else if (m_end_state != NOT_END &&
                           m_end_state != END_RESET) {
                    uint8_t end_state = m_end_state;
                    m_end_state = END_RESET;
                    app_onmicro_dfu_update_end_ind_handler(end_state, NULL);
                }
                dfu_response(&dfu_rsp_data);
                break;
"""
    if old_control_response not in service:
        raise SystemExit("Unexpected OM6626 DFU service: control response marker not found")
    service = service.replace(old_control_response, new_control_response, 1)
    old_begin = """static void dfu_begin_ind_handler(uint8_t status, void *p)
{
    app_onmicro_dfu_update_start_ind_handler(status, p);
}
"""
    new_begin = """static void dfu_begin_ind_handler(uint8_t status, void *p)
{
    inktag_om_dfu_reject_clear();
    m_safe_resume_after_response = false;
    m_dfu_transfer_active = true;
    app_onmicro_dfu_update_start_ind_handler(status, p);
}
"""
    if old_begin not in service:
        raise SystemExit("Unexpected OM6626 DFU begin callback marker")
    service = service.replace(old_begin, new_begin, 1)
    old_success_reboot = """        if (m_end_state == DFU_UPDATE_ST_SUCCESS) {
            drv_pmu_force_reboot();
        }
"""
    new_success_reboot = """        uint8_t end_state = m_end_state;
        m_return_kind = 0u;
        m_return_tx_id = 0u;
        m_dfu_transfer_active = false;
        dfu_reset(DFU_UPDATE_ST_DISCONNECT);
        if (end_state != END_RESET &&
                   m_end_state == DFU_UPDATE_ST_DISCONNECT) {
            /* An interrupted object (including a Flash write rejected by the
             * voltage guard) is still a terminal transfer failure.  The
             * vendor disconnect path only frees its state, so explicitly
             * leave the perpetual blue transfer scene and report the bounded
             * Recovery error scene. */
            m_end_state = END_RESET;
            app_onmicro_dfu_update_end_ind_handler(
                DFU_UPDATE_ST_DISCONNECT, NULL);
        }
        inktag_om_dfu_reject_clear();
"""
    if old_success_reboot not in service:
        raise SystemExit("Unexpected OM6626 DFU service: success reboot marker not found")
    service = service.replace(
        "        dfu_reset(DFU_UPDATE_ST_DISCONNECT);\n" + old_success_reboot,
        new_success_reboot,
        1,
    )
    service = service.replace("        m_end_state = NOT_END;\n",
        "        m_end_state = NOT_END;\n"
        "        m_return_kind = 0u;\n"
        "        m_return_tx_id = 0u;\n"
        "        m_dfu_transfer_active = false;\n", 1)
    old_tx_complete = """        if (m_end_state != NOT_END) {
            app_onmicro_dfu_update_end_ind_handler(m_end_state, NULL);
        }
"""
    new_tx_complete = """        if (evt->gatt.conn_idx == 0u && m_return_tx_id != 0u &&
            evt->gatt.tx_complete.id == m_return_tx_id) {
            uint8_t kind = m_return_kind;
            m_return_tx_id = 0u;
            m_return_kind = 0u;
            m_end_state = END_RESET;
            if (kind == 1u) app_onmicro_dfu_safe_resume_tx_complete();
            else if (kind == 2u) app_onmicro_dfu_update_end_ind_handler(
                DFU_UPDATE_ST_SUCCESS, NULL);
        }
"""
    if old_tx_complete not in service:
        raise SystemExit("Unexpected OM6626 DFU service: TX completion marker not found")
    service = service.replace(old_tx_complete, new_tx_complete, 1)
    old_end = """static void dfu_end_ind_handler(uint8_t status, void *p)
{
    m_end_state = status;
}

const dfu_cb_itf_t dfu_cb_itf = {
    dfu_begin_ind_handler,
    dfu_prog_ind_handler,
    dfu_end_ind_handler,
};
"""
    new_end = """static uint32_t m_candidate_size;
static uint32_t m_candidate_version;

static uint8_t dfu_user_check_handler(uint8_t img_type, uint32_t img_size,
                                       uint32_t img_version)
{
    if (img_type != IMAGE_TYPE_CUSTOM || img_size == 0 ||
        img_size > INKTAG_OM_SLOT_SIZE
#if !defined(INKTAG_OM_FIXED_RECOVERY)
        || !inktag_om_swap_supported()
#endif
        ) {
        inktag_om_dfu_reject_set(
            INKTAG_OTA_REJECT_REASON_IMAGE_INFO_INVALID,
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u, 0u, 0u);
        m_safe_resume_after_response = true;
        return DFU_INSUFFICIENT_RESOURCES;
    }
    uint16_t sampled_mv = 0u;
    bool sampled_mv_valid = false;
    if (!inktag_om_voltage_operation_check(INKTAG_POWER_OPERATION_OTA,
                                            &sampled_mv,
                                            &sampled_mv_valid)) {
        inktag_om_dfu_reject_set_power(
            INKTAG_OTA_REJECT_STAGE_PROGRAM_START, 0u, 0u,
            sampled_mv, sampled_mv_valid, INKTAG_POWER_POLICY_OTA_MIN_MV);
        m_safe_resume_after_response = true;
        return DFU_OPERATION_NOT_PERMITTED;
    }
    m_candidate_size = img_size;
    m_candidate_version = img_version;
    return DFU_SUCCESS;
}

static void dfu_end_ind_handler(uint8_t status, void *p)
{
    (void)p;
    m_dfu_transfer_active = false;
    if (status == DFU_UPDATE_ST_SUCCESS) {
        inktag_swap_result_t result = inktag_om_swap_prepare_candidate(
            m_candidate_size, m_candidate_version, 0u);
        if (result != INKTAG_SWAP_RESET_REQUIRED) {
            if (result == INKTAG_SWAP_VERIFY_ERROR) {
                inktag_om_dfu_reject_set(
                    INKTAG_OTA_REJECT_REASON_CHECKSUM_OR_SIGNATURE_FAILED,
                    INKTAG_OTA_REJECT_STAGE_SIGNATURE_VERIFY, 0u, 0u,
                    0u, 0u);
            } else if (result == INKTAG_SWAP_BAD_LAYOUT) {
                inktag_om_dfu_reject_set(
                    INKTAG_OTA_REJECT_REASON_ADDRESS_OR_LAYOUT_MISMATCH,
                    INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u, 0u,
                    0u, 0u);
            } else {
                uint16_t sampled_mv = 0u;
                bool sampled_mv_valid = false;
                inktag_om_voltage_last_operation_sample(
                    &sampled_mv, &sampled_mv_valid);
                if (sampled_mv_valid &&
                    sampled_mv < INKTAG_POWER_POLICY_FLASH_MIN_MV) {
                    inktag_om_dfu_reject_set_power(
                        INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u, 0u,
                        sampled_mv, sampled_mv_valid,
                        INKTAG_POWER_POLICY_FLASH_MIN_MV);
                } else {
                    inktag_om_dfu_reject_set(
                        INKTAG_OTA_REJECT_REASON_INTERNAL,
                        INKTAG_OTA_REJECT_STAGE_METADATA_COMMIT, 0u, 0u,
                        0u, 0u);
                }
            }
            status = DFU_UPDATE_ST_FAILED;
        }
    }
    m_end_state = status;
}

const dfu_cb_itf_t dfu_cb_itf = {
    dfu_begin_ind_handler,
    dfu_prog_ind_handler,
    dfu_end_ind_handler,
    dfu_user_check_handler,
    NULL,
};
"""
    if old_end not in service:
        raise SystemExit("Unexpected OM6626 DFU service: callback marker not found")
    write_if_changed(service_path, service.replace(old_end, new_end, 1))

    epd_src = repo / "firmware" / "common" / "epd"
    copy_file(user_src / "EPD_config_onmicro.c", project / "src" / "inktag_common" / "inktag_epd_config.c")
    for name in ["inktag_epd_config.h", "inktag_epd_driver.h", "inktag_epd_screen.c", "inktag_epd_screen.h", "inktag_screen_busy.h", "inktag_busy_defaults.inc", "inktag_epd_runtime.c", "inktag_epd_commands.c", "inktag_page_cycle.c", "inktag_page_cycle.h", "inktag_epd_wait_policy.h", "inktag_epd_temperature.h", "inktag_protocol_v2.h", "inktag_version.h", "inktag_epd_uc.c", "inktag_epd_jd.c", "inktag_epd_jd_shared.inc", "inktag_epd_ssd.c"]:
        copy_file(epd_src / name, project / "src" / "inktag_common" / name)

    gui_src = repo / "firmware" / "common" / "gui"
    copy_file(epd_src / "inktag_epd_e6.inc", project / "src/inktag_common/inktag_epd_e6.inc")
    for name in ["inktag_solar_terms.inc", "inktag_gui.c", "inktag_gui.h", "inktag_gui_default.inc", "inktag_gui_edition_badge.inc", "inktag_gui_edition_name.inc", "inktag_gui_calendar.inc", "inktag_gui_assets.h",
                 "inktag_gui_font_cache.h",
                 "inktag_gui_asset_package.c", "inktag_gui_asset_package.h",
                 "inktag_gui_font_roles.h", "inktag_gui_large_digits.h", "inktag_gui_fixed_layout.h",
                 "inktag_gui_device_qr.h", "inktag_calendar.c", "inktag_calendar.h", "inktag_gui_fonts.c",
                 "inktag_gui_fonts.h", "u8g2_font.c", "u8g2_font.h", "Adafruit_GFX.c",
                 "Adafruit_GFX.h"]:
        copy_file(gui_src / name, project / "src" / "inktag_common" / name)
    gui_header=project / "src/inktag_common/inktag_gui.h"
    write_if_changed(gui_header,gui_header.read_text(encoding="utf-8").replace(
        '#include "../ble/inktag_device_display_name.h"',
        '#include "inktag_device_display_name.h"'))
    # The shared source tree keeps its natural gui/../protocol relationship.
    # OM's vendor SDK project intentionally flattens common headers into one
    # include directory, so adapt only this materialized copy instead of
    # changing the public header for every platform.
    gui_asset_header_path = (
        project / "src" / "inktag_common" / "inktag_gui_asset_package.h"
    )
    gui_asset_header = gui_asset_header_path.read_text(encoding="utf-8")
    gui_asset_relative_include = '#include "../protocol/inktag_protocol_core.h"'
    gui_asset_flat_include = '#include "inktag_protocol_core.h"'
    if gui_asset_relative_include in gui_asset_header:
        write_if_changed(
            gui_asset_header_path,
            gui_asset_header.replace(
                gui_asset_relative_include,
                gui_asset_flat_include,
                1,
            ),
        )
    elif gui_asset_flat_include not in gui_asset_header:
        raise SystemExit(
            "Unexpected ITFA header: protocol include marker not found"
        )

    linker_dir = sdk / "hal" / "device" / "om6626" / "rom_lib" / "current" / "GCC"
    # Lifecycle owns a natural sibling storage include in the source tree.
    # The SDK projection is flat, just like the ITFA header above.
    pending_header = project / "src/inktag_common/inktag_lifecycle_pending.h"
    pending_content = pending_header.read_text(encoding="utf-8")
    write_if_changed(pending_header, pending_content.replace(
        '#include "../storage/inktag_settings_records.h"',
        '#include "inktag_settings_records.h"'))
    linker = (linker_dir / "linker_flash.ld").read_text(encoding="utf-8")
    # Same SDK retarget serves App and Recovery. The vendor's _sbrk starts
    # at StackLimit, and its instantaneous MSP test lets later stack frames
    # overwrite a boot-time allocation. Use the existing reserved arena.
    heap_header = repo / "firmware/ports/onmicro/common/src/inktag_om_libc_heap.h"
    copy_file(heap_header, sdk / "common/inktag_om_libc_heap.h")
    retarget_path = sdk / "common/om_libc_retarget.c"
    retarget = retarget_path.read_text(encoding="utf-8")
    start = retarget.index("caddr_t _sbrk(int incr)")
    end = retarget.index("\nint _write(", start)
    if "__StackLimit" not in retarget[start:end] and "inktag_om_heap_advance" not in retarget[start:end]:
        raise SystemExit("Unexpected OM newlib retarget arena")
    safe_sbrk = '''caddr_t _sbrk(int incr)
{
    extern unsigned char __general_heap_start__, __general_heap_end__;
    static size_t used;
    size_t previous;
    size_t capacity = (uintptr_t)&__general_heap_end__ - (uintptr_t)&__general_heap_start__;
    if (!inktag_om_heap_advance(capacity, &used, incr, &previous)) {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    return (caddr_t)((uintptr_t)&__general_heap_start__ + previous);
}
'''
    retarget = retarget[:start] + safe_sbrk + retarget[end:]
    if '#include "inktag_om_libc_heap.h"' not in retarget:
        retarget = retarget.replace('#include <assert.h>', '#include <assert.h>\n#include <errno.h>\n#include "inktag_om_libc_heap.h"', 1)
    write_if_changed(retarget_path, retarget)
    old_heap = "__GENERAL_HEAP_SIZE  = 0x400;"
    if old_heap not in linker:
        raise SystemExit("Unexpected OM6626 linker script: general heap marker not found")
    # Static GUI frames do not need a32KiB allocator. Newlib now uses the
    # existing1KiB arena; reserve4KiB stack for GUI plus radio/fault frames.
    linker = linker.replace(old_heap, "__GENERAL_HEAP_SIZE  = 0x400;", 1)
    if "__STACK_SIZE = 0x800;" not in linker:
        raise SystemExit("Unexpected OM App stack marker")
    linker = linker.replace("__STACK_SIZE = 0x800;", "__STACK_SIZE = 0x1000;", 1)
    old_flash = "FLASH (rx)    : ORIGIN = 0x00404000, LENGTH = 1012K"
    if old_flash not in linker:
        raise SystemExit("Unexpected OM6626 linker script: flash region marker not found")
    linker = linker.replace(
        old_flash,
        ("FLASH (rx)    : ORIGIN = 0x0042A000, LENGTH = 312K" if args.next_layout else
         "FLASH (rx)    : ORIGIN = 0x0042B000, LENGTH = 296K"),
        1,
    )
    vectors_marker = "    } > FLASH\n\n    /* RAM */"
    if vectors_marker not in linker:
        raise SystemExit("Unexpected OM6626 app linker: vectors marker not found")
    linker = linker.replace(
        vectors_marker,
        "    } > FLASH\n\n" +
        ("    .inktag_launch 0x0042A100 :\n" if args.next_layout else
         "    .inktag_launch 0x0042B100 :\n") +
        "    {\n"
        "        KEEP(*(.inktag_launch))\n"
        "    } > FLASH\n\n"
        "    /* RAM */",
        1,
    )
    rodata_marker = "        *(.rodata*)\n"
    if rodata_marker not in linker:
        raise SystemExit("Unexpected OM6626 app linker: rodata marker not found")
    linker = linker.replace(
        rodata_marker,
        "        KEEP(*(.inktag_factory_selector))\n"
        "        KEEP(*(.inktag_layout))\n" + "        KEEP(*(.inktag_identity_init))\n" + rodata_marker,
        1,
    )
    app_ram_marker = "        *(.ram_code)\n"
    if app_ram_marker not in linker:
        raise SystemExit("Unexpected OM6626 app linker: RAM-code marker not found")
    linker = linker.replace(
        app_ram_marker,
        app_ram_marker
        + "        /* OTA stages and arms swap metadata in this same XIP serial\n"
        + "         * Flash; keep the mutation driver in SRAM. */\n"
        + "        *drv_sf.o(.text*)\n",
        1,
    )
    image_end_marker = "    .ram_bss :\n"
    if image_end_marker not in linker:
        raise SystemExit("Unexpected OM6626 app linker: RAM-BSS marker not found")
    linker = linker.replace(
        image_end_marker,
        "    __inktag_image_end__ = LOADADDR(.ram_data) + SIZEOF(.ram_data);\n\n"
        + image_end_marker,
        1,
    )
    linker = reserve_retained_mailbox(linker)
    linker = guard_linker(linker, layout_choice(repo, 'app'), profile=args.profile)
    write_if_changed(project / "gcc" / "linker_flash_inktag.ld", linker)
    copy_file(linker_dir / "rom_symbol.ld", project / "gcc" / "rom_symbol.ld")

    rte_path = sdk / "hal" / "device" / "om6626" / "rom_lib" / "current" / "config" / "RTE_om6626.h"
    rte = rte_path.read_text(encoding="utf-8")
    for option in [
        "RTE_USART_REGISTER_CALLBACK", "RTE_USART0", "RTE_USART1",
        "RTE_I2C_REGISTER_CALLBACK", "RTE_RTC",
    ]:
        rte, count = re.subn(rf"(#define\s+{re.escape(option)}\s+)1\b", rf"\g<1>0", rte, count=1)
        if count != 1:
            raise SystemExit(f"Unexpected OM6626 RTE config: {option} marker not found")
    write_if_changed(project / "src" / "user" / "RTE_om6626.h", rte)

    # Preserve the installed/pristine vendor tree. Each role compiles a
    # project-local, hash-admitted copy with bounded public ADC reads.
    adc_source = patch_adc_source((sdk / "hal/driver/om6626/drv_adc.c").read_text(encoding="utf-8"))
    write_if_changed(project / "src/vendor/drv_adc.c", adc_source)
    rng_source = (sdk / "hal/driver/common/drv_rng.c").read_text(encoding="utf-8")
    if rng_source.count("uint32_t random_val;") != 1:
        raise SystemExit("Unexpected SDK RNG accumulator declaration")
    rng_source = rng_source.replace("uint32_t random_val;", "uint32_t random_val = 0;")
    write_if_changed(project / "src/vendor/drv_rng.c", rng_source)
    app_sources = ["projects/ble_app_inktag/src/vendor/drv_adc.c"
                   if p == "hal/driver/om6626/drv_adc.c" else p
                   for p in COMMON_SOURCES + APP_PLATFORM_SOURCES + INKTAG_SOURCES]
    app_sources = ["projects/ble_app_inktag/src/vendor/drv_rng.c" if p == "hal/driver/common/drv_rng.c" else p for p in app_sources]
    if args.profile == "pro":
        app_sources.append("projects/ble_app_inktag/src/inktag_common/inktag_code_guard.c")
    # The App's P-224 adapter owns one combined pinned SDK core, preserving
    # native P-256 exports. Do not link a duplicate SDK arithmetic instance.
    app_sources.remove("components/crypto/micro_ecc/uECC.c")
    app_startup="projects/" + app_project_name + "/src/user/startup_inktag_app.S"
    write_if_changed(sdk/app_startup,app_startup_without_unwind(
        (sdk/"hal/device/om6626/GCC/startup.S").read_text(encoding="utf-8")))
    app_sources[app_sources.index("hal/device/om6626/GCC/startup.S")]=app_startup
    app_sources.append("projects/ble_app_inktag/src/inktag_common/inktag_ble_address_uid_compact.c")
    # CRC reduction is App-only. Recovery's CPFT verifier
    # must retain the original SDK CRC implementation and link independently.
    app_ldflags = "\nLDFLAGS += -Wl,--wrap=om_crc16_ccitt"
    if args.profile == "base":
        app_sources.append("projects/ble_app_inktag/src/inktag_common/inktag_resource_name.c")
        # The base protocol has no font-upload path, but the offline Full
        # configurator still calls this ordinary read-only native metadata ABI.
        app_ldflags += "\nLDFLAGS += -Wl,--undefined=inktag_om_gui_asset_info"
    app_sources = selection.source_list(sdk, app_sources)
    sources = " \\\n".join(
        f"    $(ROOT_DIR){p}"
        for p in app_sources
    )
    diagnostic_sources = " \\\n".join(f"    $(ROOT_DIR){p}" for p in DIAGNOSTIC_SOURCES)
    includes = " \\\n".join(f"    $(ROOT_DIR){p}" for p in INCLUDES)
    write_if_changed(
        project / "gcc" / "Makefile",
        guard_makefile(MAKEFILE.format(
            artifact=board["artifact"] + ("-base" if args.profile == "base" else ""),
            activation_defines=(
                " INKTAG_GUI_ATOMIC_FONT_ASSETS=1"
                " INKTAG_GUI_EMBEDDED_FONT_FALLBACK=1"
            ),
            prototype_cppflags="",
            layout_ldflags=app_ldflags,
            sources=sources,
            diagnostic_sources=diagnostic_sources,
            includes=includes,
        ), repo, 'app', profile=args.profile),
    )

    # Recovery is common to both App editions. Select its actual source and
    # ordinary placement provider independently, rather than disabling a Pro
    # measurement macro after private sources have already been selected.
    recovery_selection = SourceSelection(repo, "base")
    copy_file = recovery_selection.copy_file
    boot_variant = variant.replace(
        "".join(f"#define {name} {value}\n" for name, value in selection.defines.items()),
        "".join(f"#define {name} {value}\n" for name, value in recovery_selection.defines.items()),
    )
    for name in ("INKTAG_EPD_ENABLE_OSPREY_E6", "INKTAG_EPD_OSPREY_E6_TEMPERATURE_READ",
                 "INKTAG_EPD_OSPREY_E6_PON_NO_WAIT"):
        boot_variant = boot_variant.replace(f"#define {name} 1\n", f"#define {name} 0\n")
    boot_project = sdk / "projects" / boot_project_name
    (boot_project / "gcc").mkdir(parents=True, exist_ok=True)
    (boot_project / "src").mkdir(parents=True, exist_ok=True)
    write_if_changed(boot_project / "src/vendor/drv_adc.c", adc_source)
    write_if_changed(boot_project / "src/vendor/drv_rng.c", rng_source)
    copy_file(user_src / "inktag_om_profile.h", boot_project / "src/inktag_om_profile.h")
    for name in ["inktag_om_boot.c", "inktag_om_flash_layout.h",
                  "inktag_om_address.c", "inktag_om_address.h",
                  "inktag_om_pins.c", "inktag_om_pins.h", "inktag_om_pin_layout.h", "inktag_om_pin_layout.c",
                  "inktag_om_ble_reset.h",
                  "inktag_om_debug_retention.h",
                  "inktag_om_dfu_diagnostics.c",
                  "inktag_om_dfu_diagnostics.h",
                  "inktag_om_swap.c", "inktag_om_swap.h", "inktag_om_app_integrity.h",
                  "inktag_om_voltage.c", "inktag_om_voltage.h",
                  "inktag_om_build_config.h",
                  "inktag_om_recovery_preflight.h"]:
        copy_file(user_src / name, boot_project / "src" / name)
    write_if_changed(boot_project / "src/inktag_om_variant_config.h", boot_variant)
    write_if_changed(boot_project / "src/inktag_om_ota_policy.h",
                     family_section(family_header, "policy"))
    copy_file(epd_src / "inktag_version.h", boot_project / "src" / "inktag_version.h")
    for name in ('inktag_code_guard.c', 'inktag_code_guard.h'):
        copy_file(repo / 'firmware/common/security' / name, boot_project / 'src' / name)
    for name in ("inktag_ble_address.c", "inktag_ble_address.h"):
        copy_file(repo / "firmware/common/ble" / name, boot_project / "src" / name)
    copy_file(repo / "firmware/common/ble/inktag_ble_address_uid_compact.c",
              boot_project / "src/inktag_ble_address_uid_compact.c")
    copy_file(repo / "firmware" / "common" / "security" /
              "inktag_firmware_signature.h",
              boot_project / "src" / "inktag_firmware_signature.h")
    for name in ["inktag_power.c", "inktag_power.h"]:
        copy_file(repo / "firmware" / "common" / "power" / name,
                  boot_project / "src" / name)
    copy_file(repo / "firmware" / "common" / "status" / "inktag_status.h",
              boot_project / "src" / "inktag_status.h")
    for name in ("inktag_pin_config.c", "inktag_pin_config.h", "inktag_aux_io.c", "inktag_aux_io.h", "inktag_release_v1.h"):
        copy_file(repo / "firmware/common/protocol" / name, boot_project / "src" / name)
    for name in ("inktag_epd_screen.h", "inktag_protocol_v2.h"):
        copy_file(repo / "firmware/common/epd" / name, boot_project / "src" / name)
    for name in ["inktag_recovery.c", "inktag_recovery.h", "inktag_swap.h",
                 "inktag_ota_reject.h", "inktag_recovery_version.h",
                 "inktag_recovery_integrity_policy.h"]:
        copy_file(ota_src / name, boot_project / "src" / name)
    write_if_changed(boot_project / "src/inktag_recovery_led.h",
                     family_section(family_header, "recovery"))
    boot_rte = re.sub(
        r"(#define\s+RTE_I2C0\s+)1\b",
        r"\g<1>0",
        rte,
        count=1,
    )
    if boot_rte == rte:
        raise SystemExit("Unexpected OM6626 RTE config: RTE_I2C0 marker not found")
    write_if_changed(boot_project / "src" / "RTE_om6626.h", boot_rte)

    boot_linker = (linker_dir / "linker_flash.ld").read_text(encoding="utf-8")
    boot_flash = "FLASH (rx)    : ORIGIN = 0x00404000, LENGTH = 1012K"
    if boot_flash not in boot_linker:
        raise SystemExit("Unexpected OM6626 linker script: boot flash marker not found")
    boot_linker = boot_linker.replace(
        boot_flash,
        ("FLASH (rx)    : ORIGIN = 0x00404000, LENGTH = 152K" if args.next_layout else
         "FLASH (rx)    : ORIGIN = 0x00404000, LENGTH = 156K"),
        1,
    )
    boot_vectors_marker = "    } > FLASH\n\n    /* RAM */"
    if boot_vectors_marker not in boot_linker:
        raise SystemExit("Unexpected OM6626 boot linker: vectors marker not found")
    boot_linker = boot_linker.replace(
        boot_vectors_marker,
        "    } > FLASH\n\n"
        "    .inktag_boot_entry 0x00404100 :\n"
        "    {\n"
        "        KEEP(*(.inktag_boot_entry))\n"
        "    } > FLASH\n\n"
        "    /* RAM */",
        1,
    )
    boot_ram_marker = "        *(.ram_code)\n"
    if boot_ram_marker not in boot_linker:
        raise SystemExit("Unexpected OM6626 boot linker: RAM-code marker not found")
    boot_linker = boot_linker.replace(
        boot_ram_marker,
        boot_ram_marker
        + "        /* Recovery overwrites the application in its own XIP serial\n"
        + "         * Flash. Every drv_sf instruction that can execute after a\n"
        + "         * program/erase command starts must therefore run from SRAM. */\n"
        + "        *drv_sf.o(.text*)\n",
        1,
    )
    if rodata_marker not in boot_linker:
        raise SystemExit("Unexpected OM6626 boot linker: rodata marker not found")
    boot_linker = boot_linker.replace(
        rodata_marker, "        KEEP(*(.inktag_recovery_info))\n        KEEP(*(.inktag_identity_init))\n" + rodata_marker, 1)
    boot_linker = reserve_retained_mailbox(boot_linker)
    boot_linker = common_recovery_linker(
        boot_linker, common_recovery_layout(repo, 'recovery'), recovery=True)
    write_if_changed(boot_project / "gcc" / "linker_flash_inktag_boot.ld", boot_linker)
    copy_file(linker_dir / "rom_symbol.ld", boot_project / "gcc" / "rom_symbol.ld")

    boot_sources_list = [p for p in COMMON_SOURCES
                         if p != "components/crypto/micro_ecc/uECC.c"] + [
        "projects/inktag_om_swap_boot/src/inktag_code_guard.c",
        "projects/inktag_om_swap_boot/src/inktag_om_address.c",
        "projects/inktag_om_swap_boot/src/inktag_ble_address.c",
        "projects/inktag_om_swap_boot/src/inktag_om_pins.c",
        "projects/inktag_om_swap_boot/src/inktag_om_pin_layout.c",
        "projects/inktag_om_swap_boot/src/inktag_pin_config.c",
        "projects/inktag_om_swap_boot/src/inktag_aux_io.c",
        "projects/inktag_om_swap_boot/src/inktag_om_boot.c",
        "projects/inktag_om_swap_boot/src/inktag_om_swap.c",
        "projects/inktag_om_swap_boot/src/inktag_om_voltage.c",
        "projects/inktag_om_swap_boot/src/inktag_om_dfu_diagnostics.c",
        "projects/inktag_om_swap_boot/src/inktag_power.c",
        "projects/inktag_om_swap_boot/src/inktag_recovery.c",
        "projects/ble_app_inktag/src/dfu/onmicro_dfu.c",
        "projects/ble_app_inktag/src/dfu/onmicro_dfu_nvds.c",
        "projects/ble_app_inktag/src/dfu/service_onmicro_dfu.c",
    ]
    boot_sources_list = ["projects/inktag_om_swap_boot/src/vendor/drv_rng.c" if p == "hal/driver/common/drv_rng.c" else p for p in boot_sources_list]
    boot_includes_list = [
        p for p in INCLUDES
        if not p.startswith("projects/ble_app_inktag/") and p != "components/crypto/micro_ecc"
    ] + ["projects/inktag_om_swap_boot/src", "projects/ble_app_inktag/src/dfu"]
    boot_sources_list.append("projects/inktag_om_swap_boot/src/inktag_ble_address_uid_compact.c")
    boot_sources_list = [path for path in boot_sources_list if not path.endswith("/inktag_code_guard.c")]
    boot_sources_list = ["projects/inktag_om_swap_boot/src/vendor/drv_adc.c"
                         if p == "hal/driver/om6626/drv_adc.c" else p for p in boot_sources_list]
    boot_sources = " \\\n".join(f"    $(ROOT_DIR){p}" for p in boot_sources_list)
    boot_includes = " \\\n".join(f"    $(ROOT_DIR){p}" for p in boot_includes_list)
    write_if_changed(
        boot_project / "gcc" / "Makefile",
        common_recovery_makefile(BOOT_MAKEFILE.format(sources=boot_sources, includes=boot_includes,
                             layout_ldflags=""), repo, 'recovery'),
    )
    if args.profile == "base":
        for path in (project / "gcc/Makefile", boot_project / "gcc/Makefile"):
            text = path.read_text().replace("projects/ble_app_inktag/", "projects/" + app_project_name + "/").replace("projects/inktag_om_swap_boot/", "projects/" + boot_project_name + "/")
            if path.parent.parent == boot_project:
                text = text.replace("PROJECT = InkTag-om6626-recovery", "PROJECT = InkTag-om6626-recovery-base")
            write_if_changed(path, text)
    write_if_changed(project / "source-selection.json", json.dumps(selection.receipt(), sort_keys=True, indent=2) + "\n")
    write_if_changed(boot_project / "source-selection.json", json.dumps(
        dict(**recovery_selection.receipt(), role="common-recovery"), sort_keys=True, indent=2) + "\n")
    rules = sdk / 'tools/build/rules_prj.mk'
    write_if_changed(rules, install_link_hook(rules.read_text(encoding='utf-8')))
    print(project)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
