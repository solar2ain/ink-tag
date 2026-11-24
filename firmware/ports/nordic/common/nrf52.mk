SDK_ROOT ?= .sdk/nordic/nRF5_SDK_17.1.0_ddde560
.DEFAULT_GOAL := all
NRF_CONFIG_DIR ?= firmware/ports/nordic/common/config
INKTAG_NRF_IMAGE_STORE ?= 1
# Exact PriceETag / SSD1675_152 BW backend passed the original-panel pilot.
# E6 exact-profile checked reads are adapted below; their electrical/accuracy
# qualification remains separate from the original-panel pilot.
INKTAG_NRF_PANEL_TEMPERATURE ?= 1
INKTAG_GUI_RENDER_BUFFER_SIZE ?= 7168
# Explicit Base source identity; private recipes are not provided here.
INKTAG_BUILD_PRO ?= 0
# Reserved AUX bytes have no runtime output backend.
# The fitted stock button remains independent of those opaque fields.
INKTAG_AUX_RESERVED_ENABLED ?= 0
ifneq ($(INKTAG_AUX_RESERVED_ENABLED),0)
$(error Nordic reserved AUX backend is disabled; INKTAG_AUX_RESERVED_ENABLED must be 0)
endif
ifneq ($(INKTAG_BUILD_PRO),0)
$(error This source tree provides Base only; INKTAG_BUILD_PRO must be 0)
endif
NRF_SOURCE_PROFILE := base
NRF_PROFILE_SUFFIX := -base
NRF_CHIP ?= nrf52832
NRF_PORT_MAKEFILE ?= firmware/ports/nordic/nrf52832/Makefile

ifneq ($(origin INKTAG_ACTIVATION_TEST_FIXTURE),undefined)
$(error The Nordic activation-test fixture was retired; build the ordinary signed application)
endif

ifeq ($(NRF_CHIP),nrf52832)
INKTAG_NRF_MINIMAL_ADVERTISING := 1
NRF_APP_STACK_SIZE := 8192
BOARD ?= nrf52832_priceetag
TARGET ?= InkTag-$(BOARD)
SOFTDEVICE := s132
SOFTDEVICE_DEFINE := S132
SOFTDEVICE_VERSION := 7.2.0
DEVICE_DEFINE := NRF52832_XXAA
ifeq ($(BOARD),nrf52832_priceetag)
BOARD_DEFINE := INKTAG_NRF_BOARD_NRF52832_PRICETAG
else
BOARD_DEFINE := INKTAG_NRF_BOARD_NRF52832_COMMON
endif
STARTUP_FILE := gcc_startup_nrf52.S
LINKER_SCRIPT ?= firmware/ports/nordic/nrf52832/linker/app_s132.ld
NRF_APP_FLASH_LIMIT ?= 163840
# Exact PriceETag functional evidence is bound in docs/nrf52832-fixed-slots.md.
# Ordinary development builds match the tested full-feature configuration;
# this does not close production, physical interruption or long-power gates.
INKTAG_NRF_GUI_ASSET_QUALIFICATION ?= 1
INKTAG_NRF_PASSWORD_QUALIFICATION ?= 1
INKTAG_NRF_FIND_PUBLIC_QUALIFICATION ?= 1
INKTAG_NRF_IMAGE_INDEX_QUALIFICATION ?= 1
INKTAG_NRF_RLE_QUALIFICATION ?= 1
INKTAG_NRF_HEALTH_QUALIFICATION ?= 1
ifeq ($(BOARD),nrf52832_priceetag)
DEFINES_BOARD_LAYOUT := -DFDS_ENABLED=0 -DINKTAG_NRF_COMPACT_STORE=1 \
  -DINKTAG_NRF_PROTOCOL_V2=1 -DINKTAG_NRF_IMAGE_STORE_ENABLED=$(INKTAG_NRF_IMAGE_STORE) \
  -DINKTAG_NRF_FIND_ENABLED=$(INKTAG_BUILD_PRO) -DINKTAG_NRF_LIFECYCLE_ENABLED=1 \
  -DINKTAG_NRF_ACTIVATION_ENABLED=$(INKTAG_BUILD_PRO) -DINKTAG_NRF_PASSWORD_ENABLED=1 -DINKTAG_NRF_INDICATOR_ENABLED=0 \
  -DINKTAG_NRF_DEVICE_NAME_WRITABLE=$(INKTAG_BUILD_PRO) -DINKTAG_ENABLE_SOAK_TELEMETRY=1 \
  -DINKTAG_GUI_ATOMIC_FONT_ASSETS=1 -DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1 \
  -DINKTAG_GUI_EMBEDDED_FONT_FALLBACK=1 -DINKTAG_GUI_ROLE_FONT_ASSETS=1 \
  -DINKTAG_GUI_NATIVE_FONT_ASSETS=1 \
  -DINKTAG_NRF_GUI_ASSET_QUALIFICATION=$(INKTAG_NRF_GUI_ASSET_QUALIFICATION) \
  -DINKTAG_NRF_PASSWORD_QUALIFICATION=$(INKTAG_NRF_PASSWORD_QUALIFICATION) \
  -DINKTAG_NRF_FIND_PUBLIC_QUALIFICATION=$(INKTAG_NRF_FIND_PUBLIC_QUALIFICATION) \
  -DINKTAG_NRF_IMAGE_INDEX_QUALIFICATION=$(INKTAG_NRF_IMAGE_INDEX_QUALIFICATION) \
  -DINKTAG_NRF_RLE_QUALIFICATION=$(INKTAG_NRF_RLE_QUALIFICATION) \
  -DINKTAG_NRF_HEALTH_QUALIFICATION=$(INKTAG_NRF_HEALTH_QUALIFICATION) \
  -DINKTAG_NRF_PANEL_TEMPERATURE_ENABLED=$(INKTAG_NRF_PANEL_TEMPERATURE) \
  -DINKTAG_EPD_OSPREY_E6_TEMPERATURE_READ=$(INKTAG_NRF_PANEL_TEMPERATURE) \
  -DINKTAG_EPD_OSPREY_E6_PON_NO_WAIT=1 \
  -DINKTAG_NRF_MINIMAL_ADVERTISING=1 -DINKTAG_EPD_ENABLE_JD79665CA=$(INKTAG_BUILD_PRO) \
  -DINKTAG_EPD_ENABLE_OSPREY_E6=$(INKTAG_BUILD_PRO) -DINKTAG_EPD_ENABLE_HANSHOW_SMALL_Q=1 \
  -DINKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154=1 \
  -DINKTAG_EPD_ENABLE_HANSHOW_OLD_200=1 -DINKTAG_EPD_ENABLE_STELLAR154=1
endif
else
$(error Unsupported NRF_CHIP '$(NRF_CHIP)'; expected nrf52832)
endif

# Keep reproducible compiler state out of the firmware artifact tree. `_build`
# is reserved for files that can be flashed, updated or exported in a release.
BUILD_DIR ?= .cache/build/nordic/$(BOARD)$(NRF_PROFILE_SUFFIX)
OUTPUT_DIR ?= _build/nordic/$(BOARD)$(NRF_PROFILE_SUFFIX)

BOARD_DIR := firmware/boards
BOARD_FAMILY_HEADER := firmware/boards/nrf52832_priceetag.h
ifneq ($(BOARD),nrf52832_priceetag)
$(error Unsupported Nordic single-backend family '$(BOARD)')
endif
ifeq ($(wildcard $(BOARD_FAMILY_HEADER)),)
$(error Missing Nordic firmware-family description $(BOARD_FAMILY_HEADER))
endif

SOFTDEVICE_HEX := $(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/hex/$(SOFTDEVICE)_nrf52_$(SOFTDEVICE_VERSION)_softdevice.hex
APP_HEX := $(OUTPUT_DIR)/$(TARGET).hex
APP_BIN := $(OUTPUT_DIR)/$(TARGET).bin
MERGED_HEX := $(OUTPUT_DIR)/$(TARGET)-$(SOFTDEVICE).hex
OPTIMIZED_BUILD_DIR ?= .cache/build/nordic/$(BOARD)/optimized
OPTIMIZED_OUTPUT_DIR ?= _build/nordic/$(BOARD)/optimized
OPTIMIZED_TARGET ?= InkTag-$(BOARD)-optimized
ifeq ($(BOARD),nrf52832_priceetag)
INKTAG_NRF_PROFILE ?= epd_154_bw_ssd1675
else
INKTAG_NRF_PROFILE ?= legacy_420_uc8176
endif
NRF_OPTIMIZATION ?= -Oz
NRF_LTO ?= 1
NRF_USE_BUILTINS ?= 1
NRF_52840_DEVELOPMENT_COMPAT ?= 0
INKTAG_NRF_MINIMAL_ADVERTISING ?= 0

TOOLCHAIN_PATH ?= $(abspath .tools/arm-gnu-toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin)
TOOLCHAIN_PREFIX := $(if $(TOOLCHAIN_PATH),$(TOOLCHAIN_PATH)/,)

CC := $(TOOLCHAIN_PREFIX)arm-none-eabi-gcc
AS := $(TOOLCHAIN_PREFIX)arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)arm-none-eabi-objcopy
SIZE := $(TOOLCHAIN_PREFIX)arm-none-eabi-size
NM := $(TOOLCHAIN_PREFIX)arm-none-eabi-nm

ifeq ($(NRF_LTO),1)
LTO_FLAGS := -flto=1 -flto-partition=one -fuse-linker-plugin
else
LTO_FLAGS :=
endif

ifeq ($(NRF_USE_BUILTINS),1)
BUILTIN_FLAGS :=
else
BUILTIN_FLAGS := -fno-builtin
endif

MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=soft
DEFINES := \
  -DAPP_TIMER_V2 \
  -DAPP_TIMER_V2_RTC1_ENABLED \
  -DFLOAT_ABI_SOFT \
  -D$(DEVICE_DEFINE) \
  -D$(BOARD_DEFINE) \
  -DINKTAG_NRF_SDK17 \
  -DINKTAG_GUI_RAW_VIEWPORT \
  -DNRF_DFU_SVCI_ENABLED \
  -DNRF_DFU_TRANSPORT_BLE=1 \
  -DNRF_FSTORAGE_ENABLED=1 \
  -DNRF_SD_BLE_API_VERSION=7 \
  -D$(SOFTDEVICE_DEFINE) \
  -DSOFTDEVICE_PRESENT \
  -DINKTAG_GUI_STATIC_BUFFER_ONLY \
  -DINKTAG_GUI_RENDER_BUFFER_SIZE=$(INKTAG_GUI_RENDER_BUFFER_SIZE) \
  -D__HEAP_SIZE=0 \
  -D__STARTUP_CLEAR_BSS \
  -D__START=main \
  -D__STACK_SIZE=$(NRF_APP_STACK_SIZE) \
  -DuECC_ENABLE_VLI_API=0 \
  -DuECC_OPTIMIZATION_LEVEL=1 \
  -DuECC_SQUARE_FUNC=0 \
  -DuECC_SUPPORT_COMPRESSED_POINT=0 \
  -DuECC_SUPPORTS_secp160r1=0 \
  -DuECC_SUPPORTS_secp192r1=0 \
  -DuECC_SUPPORTS_secp224r1=$(if $(filter nrf52832,$(NRF_CHIP)),1,0) \
  -DuECC_SUPPORTS_secp256k1=0 \
  -DuECC_SUPPORTS_secp256r1=1 \
  -DuECC_VLI_NATIVE_LITTLE_ENDIAN=1

DEFINES += $(DEFINES_BOARD_LAYOUT)
DEFINES += -DINKTAG_AUX_RESERVED_ENABLED=0
DEFINES += -DINKTAG_TRANSFER_LZO1X_ENABLED=1
DEFINES += -DINKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES=35u
DEFINES += -DINKTAG_NRF_CLOCK_PARTIAL_400X300=$(INKTAG_BUILD_PRO)
DEFINES += -DINKTAG_CODE_GUARDS=0

ifeq ($(NRF_52840_DEVELOPMENT_COMPAT),1)
DEFINES += -DDEVELOP_IN_NRF52840
endif

ifeq ($(INKTAG_NRF_PROFILE),epd_154_bw_ssd1675)
DEFINES += -DINKTAG_NRF_PROFILE_EPD_154_BW_SSD1675
else ifeq ($(INKTAG_NRF_PROFILE),epd_420_bwr_ssd1683)
DEFINES += -DINKTAG_NRF_PROFILE_EPD_420_BWR_SSD1683
else ifeq ($(INKTAG_NRF_PROFILE),legacy_420_uc8176)
DEFINES += -DINKTAG_NRF_PROFILE_LEGACY_420_UC8176
else ifeq ($(INKTAG_NRF_PROFILE),small_213_probe)
DEFINES += -DINKTAG_NRF_PROFILE_SMALL_213_PROBE
else ifeq ($(INKTAG_NRF_PROFILE),hema_213_bwr)
DEFINES += -DINKTAG_NRF_PROFILE_HEMA_213_BWR
else ifeq ($(INKTAG_NRF_PROFILE),epd_426_gray4_ssd1677)
DEFINES += -DINKTAG_NRF_PROFILE_EPD_426_GRAY4_SSD1677
else
$(error Unsupported INKTAG_NRF_PROFILE '$(INKTAG_NRF_PROFILE)')
endif

INCLUDES := \
  -I$(BOARD_DIR) \
  -Ifirmware/ports/nordic/common/src \
  -I$(NRF_CONFIG_DIR) \
  -Ifirmware/common/epd \
  -Ifirmware/common/ble \
  -Ifirmware/common/input \
  -Ifirmware/common/lifecycle \
  -Ifirmware/common/health \
  -Ifirmware/common/power \
  -Ifirmware/common/protocol \
  -Ifirmware/common/security \
  -Ifirmware/common/status \
  -Ifirmware/common/storage \
  -Ifirmware/common/time \
  -Ifirmware/common/gui \
  -I$(SDK_ROOT) \
  -I$(SDK_ROOT)/components/ble/common \
  -I$(SDK_ROOT)/components/ble/ble_advertising \
  -I$(SDK_ROOT)/components/ble/nrf_ble_gatt \
  -I$(SDK_ROOT)/components/ble/ble_services/ble_dfu \
  -I$(SDK_ROOT)/components/libraries/atomic \
  -I$(SDK_ROOT)/components/libraries/atomic_fifo \
  -I$(SDK_ROOT)/components/libraries/atomic_flags \
  -I$(SDK_ROOT)/components/libraries/balloc \
  -I$(SDK_ROOT)/components/libraries/bootloader \
  -I$(SDK_ROOT)/components/libraries/bootloader/ble_dfu \
  -I$(SDK_ROOT)/components/libraries/bootloader/dfu \
  -I$(SDK_ROOT)/components/libraries/delay \
  -I$(SDK_ROOT)/components/libraries/experimental_section_vars \
  -I$(SDK_ROOT)/components/libraries/fds \
  -I$(SDK_ROOT)/components/libraries/fstorage \
  -I$(SDK_ROOT)/components/libraries/log \
  -I$(SDK_ROOT)/components/libraries/log/src \
  -I$(SDK_ROOT)/components/libraries/memobj \
  -I$(SDK_ROOT)/components/libraries/mutex \
  -I$(SDK_ROOT)/components/libraries/pwr_mgmt \
  -I$(SDK_ROOT)/components/libraries/ringbuf \
  -I$(SDK_ROOT)/components/libraries/scheduler \
  -I$(SDK_ROOT)/components/libraries/sortlist \
  -I$(SDK_ROOT)/components/libraries/strerror \
  -I$(SDK_ROOT)/components/libraries/svc \
  -I$(SDK_ROOT)/components/libraries/timer \
  -I$(SDK_ROOT)/components/libraries/util \
  -I$(SDK_ROOT)/components/softdevice/common \
  -I$(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/headers \
  -I$(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/headers/nrf52 \
  -I$(SDK_ROOT)/components/toolchain/cmsis/include \
  -I$(SDK_ROOT)/external/fprintf \
  -I$(SDK_ROOT)/external/segger_rtt \
  -I$(SDK_ROOT)/integration/nrfx \
  -I$(SDK_ROOT)/integration/nrfx/legacy \
  -I$(SDK_ROOT)/modules/nrfx \
  -I$(SDK_ROOT)/modules/nrfx/drivers/include \
  -I$(SDK_ROOT)/modules/nrfx/hal \
  -I$(SDK_ROOT)/modules/nrfx/mdk

INCLUDES += -I$(SDK_ROOT)/external/micro-ecc/micro-ecc
NRF_EXTRA_LIBS :=

NRF_EXTRA_SIZE_FLAGS ?=
SIZE_FLAGS := -fmerge-all-constants -fno-unwind-tables -fno-asynchronous-unwind-tables $(NRF_EXTRA_SIZE_FLAGS)
# Base uses the ordinary public security ABI.
COMMON_FLAGS := $(MCU_FLAGS) $(DEFINES) $(INCLUDES) $(NRF_OPTIMIZATION) -g3 -Wall -ffunction-sections -fdata-sections -fno-strict-aliasing $(BUILTIN_FLAGS) -fshort-enums $(SIZE_FLAGS) $(LTO_FLAGS)
CFLAGS := $(COMMON_FLAGS) -std=gnu99
ASFLAGS := $(COMMON_FLAGS) -x assembler-with-cpp
LDFLAGS := $(MCU_FLAGS) $(NRF_OPTIMIZATION) $(SIZE_FLAGS) $(LTO_FLAGS) \
  -nostartfiles -T$(LINKER_SCRIPT) -L$(BUILD_DIR)/generated -L$(SDK_ROOT)/modules/nrfx/mdk \
  -Wl,--gc-sections -Wl,--relax -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
  --specs=nano.specs --specs=nosys.specs

ifeq ($(INKTAG_NRF_MINIMAL_ADVERTISING),1)
NRF_ADVERTISING_SRCS :=
else
NRF_ADVERTISING_SRCS := \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c
endif

C_SRCS := \
  firmware/ports/nordic/common/src/main.c \
  firmware/ports/nordic/common/src/inktag_nrf_ble_address.c \
  firmware/common/ble/inktag_ble_address.c \
  firmware/common/epd/inktag_epd_config.c \
  firmware/common/epd/inktag_epd_driver.c \
  firmware/common/epd/inktag_epd_screen.c \
  firmware/common/epd/inktag_epd_runtime.c \
  firmware/common/epd/inktag_epd_commands.c \
  firmware/common/epd/inktag_epd_service.c \
  firmware/common/epd/inktag_page_cycle.c \
  firmware/common/epd/inktag_epd_uc.c \
  firmware/common/epd/inktag_epd_jd.c \
  firmware/common/epd/inktag_epd_ssd.c \
  firmware/common/input/inktag_input.c \
  firmware/common/gui/inktag_calendar.c \
  firmware/common/gui/inktag_gui_fonts.c \
  firmware/common/gui/u8g2_font.c \
  firmware/common/gui/Adafruit_GFX.c \
  firmware/common/gui/inktag_gui.c \
  $(NRF_ADVERTISING_SRCS) \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_dfu/ble_dfu.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_clock.c \
  $(SDK_ROOT)/integration/nrfx/legacy/nrf_drv_spi.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_clock.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_spi.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_wdt.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/scheduler/app_scheduler.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_svci.c \
  $(SDK_ROOT)/components/libraries/pwr_mgmt/nrf_pwr_mgmt.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  firmware/ports/nordic/common/src/inktag_nrf_ble_events.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  firmware/ports/nordic/common/src/inktag_nrf_config_store.c \
  firmware/ports/nordic/common/src/inktag_nrf_factory_defaults.c \
  firmware/ports/nordic/common/src/inktag_nrf_io_config.c \
  firmware/common/lifecycle/inktag_lifecycle.c \
  firmware/common/power/inktag_power.c \
  firmware/common/protocol/inktag_protocol_core.c \
  firmware/common/protocol/inktag_lzo1x.c \
  firmware/common/storage/inktag_settings_journal.c \
  firmware/common/protocol/inktag_display_settings.c \
  firmware/common/protocol/inktag_pin_config.c \
  firmware/common/protocol/inktag_aux_io.c \
  firmware/common/security/inktag_brand_overlay.c \
  firmware/common/status/inktag_status.c \
  firmware/ports/nordic/common/src/inktag_nrf_record_store.c \
  firmware/ports/nordic/common/src/inktag_nrf_health.c \
  firmware/ports/nordic/common/src/inktag_nrf_epd_io.c \
  firmware/ports/nordic/common/src/inktag_nrf_panel_temperature.c \
  firmware/ports/nordic/common/src/inktag_nrf_buttonless_guard.c

ifeq ($(NRF_CHIP),nrf52832)
C_SRCS += \
  $(SDK_ROOT)/components/ble/common/ble_conn_params.c \
  $(SDK_ROOT)/components/ble/nrf_ble_gatt/nrf_ble_gatt.c \
  firmware/ports/nordic/common/src/inktag_nrf_buttonless_unbonded.c \
  firmware/common/ble/inktag_findmy.c \
  firmware/common/ble/inktag_findmy_public.c \
  firmware/common/health/inktag_soak_telemetry.c \
  firmware/common/security/inktag_image_quota.c \
  firmware/common/security/inktag_activation.c \
  firmware/common/security/inktag_access.c \
  firmware/common/security/inktag_sha256.c \
  firmware/common/security/inktag_device_password.c \
  firmware/common/security/inktag_device_password_store.c \
  firmware/ports/nordic/common/src/inktag_nrf_password.c \
  firmware/ports/nordic/common/src/inktag_nrf_public_ecc.c \
  firmware/ports/nordic/common/src/inktag_nrf_activation.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage_sd.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_gpiote.c \
  firmware/common/storage/inktag_settings_records.c \
  firmware/common/storage/inktag_findmy_public_store.c \
  firmware/common/gui/inktag_gui_asset_package.c \
  firmware/ports/nordic/common/src/inktag_nrf_internal_assets.c \
  firmware/ports/nordic/common/src/inktag_nrf_image_fixed.c \
  firmware/common/protocol/inktag_image_upload_v5.c \
  firmware/ports/nordic/common/src/inktag_nrf_findmy.c

endif
NRF_DFU_UNBONDED_OBJ := $(BUILD_DIR)/firmware/ports/nordic/common/src/inktag_nrf_buttonless_unbonded.o
$(NRF_DFU_UNBONDED_OBJ): CFLAGS += \
  -Dble_dfu_buttonless_on_ctrl_pt_write=inktag_nrf_sdk_buttonless_on_ctrl_pt_write

# The factory-font policy is consumed only by the shared GUI translation unit.
# Keep the ordinary Nordic renderer/cache/stack configuration intact.
$(BUILD_DIR)/firmware/common/gui/inktag_gui.o: CFLAGS += -DINKTAG_GUI_FACTORY_FONT_LAYOUT=1
# Nordic uses GENERATED LUTs; image transfer is a decode-only LZO consumer.
# Keep this local to the codec TU and make flag edits invalidate that object.
$(BUILD_DIR)/firmware/common/protocol/inktag_lzo1x.o: CFLAGS += -DINKTAG_LZO1X_VALIDATE_ENABLED=0
$(BUILD_DIR)/firmware/common/protocol/inktag_lzo1x.o: firmware/ports/nordic/common/nrf52.mk

C_SRCS += firmware/common/time/inktag_monotonic_clock.c \
  firmware/common/time/inktag_clock_rate.c \
  firmware/common/input/inktag_nfc_command.c


C_SRCS += firmware/common/security/inktag_code_guard.c
C_SRCS += firmware/common/storage/inktag_resource_name.c

# Shared selection is exact and profile-bound; the adapter emits explicit rules
# using stable logical object names and Base input order.
NRF_PROFILE_SELECTOR := tools/sdk/materialize-nordic-source-profile.py
NRF_PROFILE_SELECTION := $(BUILD_DIR)/generated/source-profile.mk
NRF_COMMON_LOGICAL_SRCS := $(filter firmware/common/%,$(C_SRCS))
NRF_PROFILE_GENERATE := $(shell python3 $(NRF_PROFILE_SELECTOR) --repo . --profile $(NRF_SOURCE_PROFILE) --output $(NRF_PROFILE_SELECTION) $(NRF_COMMON_LOGICAL_SRCS))
ifneq ($(.SHELLSTATUS),0)
$(error Nordic $(NRF_SOURCE_PROFILE) source selection failed; no private fallback)
endif
include $(NRF_PROFILE_SELECTION)
C_SRCS := $(filter-out $(NRF_PROFILE_OMITTED),$(C_SRCS))
C_SRCS := $(filter-out firmware/ports/nordic/common/src/inktag_nrf_public_ecc.c,$(C_SRCS))
DEFINES += $(NRF_PROFILE_DEFINES)
NRF_PROFILE_INCLUDES :=
override CFLAGS += $(NRF_PROFILE_DEFINES) $(NRF_PROFILE_INCLUDES)
override ASFLAGS += $(NRF_PROFILE_DEFINES)
ASM_SRCS := \
  $(SDK_ROOT)/modules/nrfx/mdk/$(STARTUP_FILE)

OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o)) $(addprefix $(BUILD_DIR)/,$(ASM_SRCS:.S=.o)) $(NRF_PROFILE_NOTE_OBJECT)
CONFIG_STAMP := $(BUILD_DIR)/.build-config

.PHONY: all clean merged optimized optimized-merged size check-size check-runtime check-toolchain sync-version FORCE

all: sync-version check-toolchain $(APP_HEX) $(APP_BIN) $(AUXILIARY_ARTIFACTS) size check-size check-runtime

sync-version:
	python3 tools/version/sync-version.py --repo . --firmware-only

merged: sync-version check-toolchain $(MERGED_HEX)

# Preserve the nRF52832 feature set and the configured payload limit.
optimized:
	$(MAKE) -f $(NRF_PORT_MAKEFILE) TOOLCHAIN_PATH="$(TOOLCHAIN_PATH)" \
		BUILD_DIR="$(OPTIMIZED_BUILD_DIR)" OUTPUT_DIR="$(OPTIMIZED_OUTPUT_DIR)" TARGET="$(OPTIMIZED_TARGET)" \
		NRF_LTO=1 NRF_USE_BUILTINS=1 NRF_52840_DEVELOPMENT_COMPAT=0 \
		NRF_APP_FLASH_LIMIT=$(NRF_APP_FLASH_LIMIT) all

optimized-merged:
	$(MAKE) -f $(NRF_PORT_MAKEFILE) TOOLCHAIN_PATH="$(TOOLCHAIN_PATH)" \
		BUILD_DIR="$(OPTIMIZED_BUILD_DIR)" OUTPUT_DIR="$(OPTIMIZED_OUTPUT_DIR)" TARGET="$(OPTIMIZED_TARGET)" \
		NRF_LTO=1 NRF_USE_BUILTINS=1 NRF_52840_DEVELOPMENT_COMPAT=0 \
		NRF_APP_FLASH_LIMIT=$(NRF_APP_FLASH_LIMIT) all merged

check-toolchain:
	@if [ ! -f "$(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c" ]; then \
		echo "Missing Nordic nRF5 SDK 17.1.0: $(SDK_ROOT)"; \
		echo "Run: pwsh tools/sdk/setup-sdk.ps1 -Platform nrf"; \
		echo " or: sh tools/sdk/setup-sdk.sh --platform nrf"; \
		exit 2; \
	fi
	@if ! command -v "$(CC)" >/dev/null 2>&1; then \
		echo "Missing nRF5 ARM GCC toolchain: $(CC)"; \
		echo "Install arm-none-eabi-gcc or pass TOOLCHAIN_PATH=/path/to/bin"; \
		exit 127; \
	fi
$(APP_HEX): $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O ihex $< $@

$(APP_BIN): $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	$(OBJCOPY) --gap-fill 0xFF -O binary $< $@


$(MERGED_HEX): $(SOFTDEVICE_HEX) $(APP_HEX) tools/platforms/nordic/common/programmer/merge_hex.py
	@mkdir -p $(dir $@)
	python3 tools/platforms/nordic/common/programmer/merge_hex.py $@ $(SOFTDEVICE_HEX) $(APP_HEX)

NRF_GUARD_TOOL := tools/sdk/nordic_base_identity.py
NRF_GUARD_MATERIALIZER := tools/sdk/materialize-nordic-base-sdk.py
NRF_VERIFY_MODULE := tools/sdk/nordic_base_identity.py
NRF_LINK_FINALIZE :=
NRF_GUARD_LINKER := $(BUILD_DIR)/generated/nrf_common.ld
$(NRF_GUARD_LINKER): $(SDK_ROOT)/modules/nrfx/mdk/nrf_common.ld $(NRF_GUARD_MATERIALIZER) version.json
	python3 $(NRF_GUARD_MATERIALIZER) --source $< --output $@ --role app

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT) $(NRF_GUARD_LINKER) $(NRF_GUARD_TOOL) $(NRF_VERIFY_MODULE) tools/sdk/goodix_code_elf.py
	$(CC) $(LDFLAGS) $(OBJS) $(NRF_EXTRA_LIBS) -o $@.tmp
	python3 $(NRF_GUARD_TOOL) --role app --source-profile $(NRF_SOURCE_PROFILE) --elf $@.tmp $(NRF_LINK_FINALIZE)
	mv $@.tmp $@

.PHONY: nrf-code-verify
all: nrf-code-verify
nrf-code-verify: $(BUILD_DIR)/$(TARGET).elf $(APP_HEX) $(APP_BIN)
	python3 $(NRF_GUARD_TOOL) --role app --source-profile $(NRF_SOURCE_PROFILE) --elf $(BUILD_DIR)/$(TARGET).elf --hex $(APP_HEX) --bin $(APP_BIN)

# Make does not normally notice command-line flag changes. Keep one compact
# stamp so switching LTO/builtin/chip-selection options cannot reuse stale
# objects from a previous build directory invocation.
$(CONFIG_STAMP): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' 'BOARD=$(BOARD)' 'NRF_CHIP=$(NRF_CHIP)' 'SOFTDEVICE=$(SOFTDEVICE)' 'SOURCE_PROFILE=$(NRF_SOURCE_PROFILE)' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'ASFLAGS=$(ASFLAGS)' 'LDFLAGS=$(LDFLAGS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

$(OBJS): $(CONFIG_STAMP)

FORCE:

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Keep the shared/materialized SDK read-only. Only this bounded adaptation
# object is regenerated when its pinned input or transformer changes.
NRF_BLE_SDK_ADAPTER := $(BUILD_DIR)/generated/nrf_sdh_ble.c
$(NRF_BLE_SDK_ADAPTER): $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c tools/sdk/materialize-nordic-ble-event-buffer.py
	python3 tools/sdk/materialize-nordic-ble-event-buffer.py --input $< --output $@

$(BUILD_DIR)/firmware/ports/nordic/common/src/inktag_nrf_ble_events.o: firmware/ports/nordic/common/src/inktag_nrf_ble_events.c $(NRF_BLE_SDK_ADAPTER)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DINKTAG_NRF_BLE_SDK_ADAPTER='"$(abspath $(NRF_BLE_SDK_ADAPTER))"' -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -MMD -MP -c $< -o $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

check-size: $(APP_BIN)
	@if [ "$(NRF_APP_FLASH_LIMIT)" -gt 0 ]; then \
		actual=$$(wc -c < "$(APP_BIN)"); \
		if [ "$$actual" -gt "$(NRF_APP_FLASH_LIMIT)" ]; then \
			echo "nRF application is $$actual bytes; limit is $(NRF_APP_FLASH_LIMIT) bytes" >&2; \
			exit 1; \
		fi; \
		echo "nRF application size: $$actual / $(NRF_APP_FLASH_LIMIT) bytes ($$(( $(NRF_APP_FLASH_LIMIT) - actual )) bytes free)"; \
	fi

check-runtime: $(BUILD_DIR)/$(TARGET).elf
	@if $(NM) --defined-only "$<" | grep -Eq ' [TtWw] (malloc|free|_malloc_r|_free_r|_mainCRTStartup|_start|exit|__libc_init_array)$$'; then \
		echo "nRF application unexpectedly links the general-purpose heap or CRT startup:" >&2; \
		$(NM) --defined-only "$<" | grep -E ' [TtWw] (malloc|free|_malloc_r|_free_r|_mainCRTStartup|_start|exit|__libc_init_array)$$' >&2; \
		exit 1; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)

-include $(OBJS:.o=.d)
