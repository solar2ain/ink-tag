SDK_ROOT ?= $(shell python3 build-support/sdk/primary-checkout.py --sdk)
.DEFAULT_GOAL := all
INKTAG_AUX_RESERVED_ENABLED ?= 0
ifneq ($(strip $(INKTAG_AUX_RESERVED_ENABLED)),0)
$(error This nRF52811 build requires INKTAG_AUX_RESERVED_ENABLED=0)
endif
override INKTAG_AUX_RESERVED_ENABLED := 0
NRF_CONFIG_DIR ?= compat/nordic/config
INKTAG_NRF_IMAGE_STORE ?= 1
INKTAG_GUI_RENDER_BUFFER_SIZE ?= 7168
NRF_CHIP ?= nrf52811
ifneq ($(NRF_CHIP),nrf52811)
$(error This InkTag source port only builds nrf52811)
endif
NRF_PORT_MAKEFILE ?= app/Makefile

BOARD ?= nrf52811_aesl
ifneq ($(BOARD),nrf52811_aesl)
$(error This InkTag source port requires the nrf52811_aesl board)
endif
INKTAG_NRF_DEVICE_NAME_WRITABLE ?= 0
TARGET ?= InkTag-$(BOARD)
SOFTDEVICE := s112
SOFTDEVICE_DEFINE := S112
SOFTDEVICE_VERSION := 7.3.0
DEVICE_DEFINE := NRF52811_XXAA
BOARD_DEFINE := INKTAG_NRF_BOARD_NRF52811_AESL
INKTAG_NRF_MINIMAL_ADVERTISING := 1
STARTUP_FILE := gcc_startup_nrf52811.S
LINKER_SCRIPT ?= app/linker/app_s112.ld
NRF_APP_FLASH_LIMIT ?= 69632
DEFINES_BOARD_LAYOUT := -DFDS_ENABLED=0 -DINKTAG_NRF_COMPACT_STORE=1 \
  -DINKTAG_AUX_RESERVED_ENABLED=$(INKTAG_AUX_RESERVED_ENABLED) \
  -DINKTAG_COMPACT_BUSY_DIAG=1 -DINKTAG_DEVICE_SETTINGS_ENABLED=1 \
  -DINKTAG_TRANSFER_PACKBITS_ENABLED=1 -DINKTAG_TRANSFER_LZO1X_ENABLED=0 \
  -DINKTAG_NRF_PROTOCOL_V2=1 -DINKTAG_NRF_IMAGE_STORE_ENABLED=$(INKTAG_NRF_IMAGE_STORE) \
  -DINKTAG_NRF_FIND_ENABLED=0 -DINKTAG_NRF_LIFECYCLE_ENABLED=1 \
  -DINKTAG_NRF_ACTIVATION_ENABLED=0 -DINKTAG_NRF_INDICATOR_ENABLED=1 \
  -DINKTAG_NRF_DEVICE_NAME_WRITABLE=$(INKTAG_NRF_DEVICE_NAME_WRITABLE) -DINKTAG_NRF_TEST_ENABLED=1 \
  -DINKTAG_EPD_REQUIRE_CONFIGURED_IO=1 \
  -DINKTAG_NRF_MINIMAL_ADVERTISING=1 -DINKTAG_EPD_ENABLE_JD79665CA=0 \
  -DINKTAG_EPD_ENABLE_UC8151=0 -DINKTAG_EPD_ENABLE_UC8159=0 \
  -DINKTAG_EPD_ENABLE_SSD1677_GRAY4=0 \
  -DINKTAG_STATUS_PRESET_ONLY_RUNTIME=1 \
  -DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1 \
  -DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1 \
  -DINKTAG_NRF_IMAGE_WRITE_BLOCK_SIZE=256 -DINKTAG_NRF_IMAGE_WRITE_BLOCK_COUNT=2
ifeq ($(INKTAG_NRF_EXCLUDE_154_DRIVER),1)
DEFINES_BOARD_LAYOUT += -DINKTAG_EXCLUDE_SSD1675_154=1 -DINKTAG_EPD_ENABLE_SSD1675=0
endif
ifeq ($(INKTAG_NRF_LOW_BATTERY_PAGE),0)
DEFINES_BOARD_LAYOUT += -DINKTAG_GUI_DISABLE_LOW_BATTERY_PAGE=1
endif

# Keep reproducible compiler state out of the firmware artifact tree. `_build`
# is reserved for files that can be flashed, updated or exported in a release.
BUILD_DIR ?= .cache/build/nordic/$(BOARD)
OUTPUT_DIR ?= _build/nordic/$(BOARD)

BOARD_DIR := ../../../boards
BOARD_FAMILY_HEADER := $(BOARD_DIR)/nrf52811_aesl.h
ifeq ($(wildcard $(BOARD_FAMILY_HEADER)),)
$(error Missing Nordic firmware-family description $(BOARD_FAMILY_HEADER))
endif

SOFTDEVICE_HEX := $(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/hex/$(SOFTDEVICE)_nrf52_$(SOFTDEVICE_VERSION)_softdevice.hex
APP_HEX := $(OUTPUT_DIR)/$(TARGET).hex
APP_BIN := $(OUTPUT_DIR)/$(TARGET).bin
MERGED_HEX := $(OUTPUT_DIR)/$(TARGET)-$(SOFTDEVICE).hex
ifeq ($(NRF_CHIP),nrf52811)
FONT_ASSET_FONT_OBJECT := $(BUILD_DIR)/external-fonts/fonts.o
FONT_ASSET_QR_OBJECT := $(BUILD_DIR)/external-fonts/device-info-qr.o
FONT_ASSET_LUNAR_OBJECT := $(BUILD_DIR)/external-fonts/lunar.o
FONT_ASSET_OBJECTS := $(FONT_ASSET_FONT_OBJECT) $(FONT_ASSET_QR_OBJECT) \
	$(FONT_ASSET_LUNAR_OBJECT)
FONT_ASSET_ELF := $(BUILD_DIR)/external-fonts/fonts.elf
FONT_ASSET_PAYLOAD := $(BUILD_DIR)/external-fonts/fonts.bin
FONT_ASSET_IMAGE := $(OUTPUT_DIR)/$(TARGET)-font-assets.bin
AUXILIARY_ARTIFACTS := $(FONT_ASSET_IMAGE)
endif
OPTIMIZED_BUILD_DIR ?= .cache/build/nordic/$(BOARD)/optimized
OPTIMIZED_OUTPUT_DIR ?= _build/nordic/$(BOARD)/optimized
OPTIMIZED_TARGET ?= InkTag-$(BOARD)-optimized
ifeq ($(BOARD),nrf52811_aesl)
INKTAG_NRF_PROFILE ?= hema_213_bwr
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
  -D__STACK_SIZE=2048

DEFINES += $(DEFINES_BOARD_LAYOUT)


ifeq ($(NRF_52840_DEVELOPMENT_COMPAT),1)
DEFINES += -DDEVELOP_IN_NRF52840
endif

ifeq ($(INKTAG_NRF_PROFILE),epd_420_bwr_ssd1683)
DEFINES += -DINKTAG_NRF_PROFILE_EPD_420_BWR_SSD1683
else ifeq ($(INKTAG_NRF_PROFILE),legacy_420_uc8176)
DEFINES += -DINKTAG_NRF_PROFILE_LEGACY_420_UC8176
else ifeq ($(INKTAG_NRF_PROFILE),small_213_probe)
DEFINES += -DINKTAG_NRF_PROFILE_SMALL_213_PROBE
else ifeq ($(INKTAG_NRF_PROFILE),hema_213_bwr)
DEFINES += -DINKTAG_NRF_PROFILE_HEMA_213_BWR
else
$(error Unsupported INKTAG_NRF_PROFILE '$(INKTAG_NRF_PROFILE)')
endif

INCLUDES := \
  -I$(BOARD_DIR) \
  -Icompat/nordic/src \
  -I$(NRF_CONFIG_DIR) \
  -Icompat/common/epd \
  -Icompat/common/ble \
  -Icompat/common/input \
  -Icompat/common/lifecycle \
  -Icompat/common/health \
  -Icompat/common/power \
  -Icompat/common/protocol \
  -Icompat/common/security \
  -Icompat/common/status \
  -Icompat/common/storage \
  -Icompat/common/time \
  -Icompat/common/gui \
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

NRF_EXTRA_LIBS :=

NRF_EXTRA_SIZE_FLAGS ?=
SIZE_FLAGS := -fmerge-all-constants -fno-unwind-tables -fno-asynchronous-unwind-tables $(NRF_EXTRA_SIZE_FLAGS)
COMMON_FLAGS := $(MCU_FLAGS) $(DEFINES) $(INCLUDES) $(NRF_OPTIMIZATION) -g3 -Wall -ffunction-sections -fdata-sections -fno-strict-aliasing $(BUILTIN_FLAGS) -fshort-enums $(SIZE_FLAGS) $(LTO_FLAGS)
NRF_DEPENDENCY_FLAGS ?= -MMD -MP
CFLAGS := $(COMMON_FLAGS) -std=gnu99 -Werror=return-type
ASFLAGS := $(COMMON_FLAGS) -x assembler-with-cpp
LDFLAGS := $(MCU_FLAGS) $(NRF_OPTIMIZATION) $(SIZE_FLAGS) $(LTO_FLAGS) \
  -nostartfiles -T$(LINKER_SCRIPT) -L$(SDK_ROOT)/modules/nrfx/mdk \
  -Wl,--gc-sections -Wl,--relax -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
  --specs=nano.specs --specs=nosys.specs

ifeq ($(INKTAG_NRF_MINIMAL_ADVERTISING),1)
NRF_ADVERTISING_SRCS :=
else
NRF_ADVERTISING_SRCS := \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c
endif


# Retired index/slot-alias prototype is archived at fixed Git revision 53a5007.
# Keep the default define stable for existing build stamps; no opt-in can link it.
INKTAG_NRF_LOGICAL_IMAGE_SLOTS ?= 0
ifneq ($(INKTAG_NRF_LOGICAL_IMAGE_SLOTS),0)
$(error Logical-index prototype has been removed; use sparse physical resource IDs. Compare Git revision 53a5007 offline)
endif
CFLAGS += -DINKTAG_NRF_LOGICAL_IMAGE_SLOTS=0
INKTAG_NRF_STREAMING_CONFIG_STORE ?= 1
CFLAGS += -DINKTAG_NRF_STREAMING_CONFIG_STORE=$(INKTAG_NRF_STREAMING_CONFIG_STORE)
INKTAG_NRF_NATIVE_IMAGE_METADATA ?= 0
ifneq ($(INKTAG_NRF_NATIVE_IMAGE_METADATA),0)
$(error Native image COW/metadata prototype is retired; compare Git revision cfff3e4 offline)
endif
INKTAG_NRF_FIXED_IMAGE_SLOTS ?= 0
ifeq ($(INKTAG_NRF_FIXED_IMAGE_SLOTS),1)
INKTAG_NRF_SIMPLE_IMAGE_METADATA := 1
endif
CFLAGS += -DINKTAG_NRF_FIXED_IMAGE_SLOTS=$(INKTAG_NRF_FIXED_IMAGE_SLOTS)
INKTAG_NRF_IMAGE_METADATA_EDIT ?= 0
ifneq ($(INKTAG_NRF_IMAGE_METADATA_EDIT),0)
$(error Standalone image metadata editing is retired; names/types are set during upload or replacement)
endif
CFLAGS += -DINKTAG_NRF_IMAGE_METADATA_EDIT=$(INKTAG_NRF_IMAGE_METADATA_EDIT)
INKTAG_NRF_SIMPLE_IMAGE_METADATA ?= 0
CFLAGS += -DINKTAG_NRF_SIMPLE_IMAGE_METADATA=$(INKTAG_NRF_SIMPLE_IMAGE_METADATA)
CFLAGS += -DINKTAG_NRF_NATIVE_IMAGE_METADATA=$(INKTAG_NRF_NATIVE_IMAGE_METADATA)
ifeq ($(NRF_CHIP),nrf52811)
INKTAG_EPD_COMPACT_INIT ?= 1
else
INKTAG_EPD_COMPACT_INIT ?= 0
endif

C_SRCS := \
  compat/nordic/src/main.c \
  compat/nordic/src/inktag_nrf_pins.c \
  compat/nordic/src/inktag_nrf_pin_epoch.c \
  compat/common/protocol/inktag_pin_config.c \
  compat/common/epd/inktag_epd_config.c \
  compat/common/epd/inktag_epd_driver.c \
  compat/common/epd/inktag_epd_screen.c \
  compat/common/epd/inktag_epd_runtime.c \
  compat/common/epd/inktag_epd_commands.c \
  compat/common/epd/inktag_epd_service.c \
  compat/common/epd/inktag_page_cycle.c \
  compat/common/epd/inktag_epd_uc.c \
  compat/common/epd/inktag_epd_jd.c \
  compat/common/epd/inktag_epd_ssd.c \
  compat/common/input/inktag_input.c \
  compat/common/gui/inktag_calendar.c \
  compat/common/gui/inktag_gui_fonts.c \
  compat/common/gui/u8g2_font.c \
  compat/common/gui/Adafruit_GFX.c \
  compat/common/gui/inktag_gui.c \
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
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  compat/nordic/src/inktag_nrf_config_store.c \
  compat/common/lifecycle/inktag_lifecycle.c \
  compat/common/power/inktag_power.c \
  compat/common/protocol/inktag_protocol_core.c \
  compat/common/protocol/inktag_text.c \
  compat/common/protocol/inktag_packbits.c \
  compat/common/protocol/inktag_display_settings.c \
  compat/common/security/inktag_brand_overlay.c \
  compat/common/status/inktag_status.c \
  compat/nordic/src/inktag_nrf_record_store.c \
  compat/nordic/src/inktag_nrf_health.c \
  compat/nordic/src/inktag_nrf_buttonless_guard.c

C_SRCS += \
  compat/nordic/src/inktag_nrf_external_flash.c \
  compat/nordic/src/inktag_nrf_external_assets.c \
  compat/nordic/src/inktag_nrf_led.c \
  compat/nordic/src/inktag_nrf_image_store.c \
  compat/nordic/src/inktag_nrf_buttonless_unbonded.c


NRF_DFU_UNBONDED_OBJ := $(BUILD_DIR)/compat/nordic/src/inktag_nrf_buttonless_unbonded.o
$(NRF_DFU_UNBONDED_OBJ): CFLAGS += \
  -Dble_dfu_buttonless_on_ctrl_pt_write=inktag_nrf_sdk_buttonless_on_ctrl_pt_write

C_SRCS += compat/common/time/inktag_monotonic_clock.c
INKTAG_NRF_GUI_RAM_TABLES ?= 0
ifeq ($(INKTAG_NRF_GUI_RAM_TABLES),1)
CFLAGS += -DINKTAG_NRF_GUI_RAM_TABLES=1 -I$(BUILD_DIR)/gui-projection
C_SRCS := $(filter-out compat/common/gui/inktag_gui.c compat/common/gui/inktag_calendar.c,$(C_SRCS))
NRF_GUI_PROJECTIONS := $(BUILD_DIR)/gui-projection/inktag_gui.c $(BUILD_DIR)/gui-projection/inktag_calendar.c
$(BUILD_DIR)/compat/nordic/src/inktag_nrf_external_assets.o: $(BUILD_DIR)/gui-projection/inktag_gui.c
endif
INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES ?= 0
ifneq ($(INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES),0)
ifneq ($(NRF_CHIP),nrf52811)
$(error Glyph-address projection is qualified only for the fixed nRF52811 assets)
endif
CFLAGS += -DINKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES=$(INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES)
C_SRCS := $(filter-out compat/common/gui/u8g2_font.c,$(C_SRCS))
NRF_FONT_PROJECTION := $(BUILD_DIR)/font-projection/u8g2_font.c
endif
ifeq ($(INKTAG_EPD_COMPACT_INIT),1)
# Generate driver projections in the cache; never rewrite the common snapshot.
NRF_EPD_COMMON_SRCS := $(addprefix compat/common/epd/,inktag_epd_jd.c inktag_epd_ssd.c inktag_epd_uc.c inktag_epd_driver.c)
NRF_EPD_PROJECTIONS := $(addprefix $(BUILD_DIR)/epd-projection/,$(notdir $(NRF_EPD_COMMON_SRCS)))
.SECONDARY: $(NRF_EPD_PROJECTIONS)
C_SRCS := $(filter-out $(NRF_EPD_COMMON_SRCS),$(C_SRCS))
# Shared command tables and the Nordic projection use the same common helper.
# Retain the old port helper as an uncompiled reference for size comparisons.
endif

ifeq ($(INKTAG_NRF_SIMPLE_IMAGE_METADATA),1)
ifeq ($(INKTAG_NRF_FIXED_IMAGE_SLOTS),1)
C_SRCS := $(filter-out compat/nordic/src/inktag_nrf_image_store.c,$(C_SRCS))
C_SRCS += compat/nordic/src/inktag_nrf_image_fixed.c
endif
C_SRCS += compat/common/protocol/inktag_resource_name.c
$(BUILD_DIR)/compat/common/protocol/inktag_resource_name.o: CFLAGS += -DINKTAG_RESOURCE_NAME_MAX_BYTES=35u
endif

ASM_SRCS := \
  $(SDK_ROOT)/modules/nrfx/mdk/$(STARTUP_FILE)

OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o)) $(addprefix $(BUILD_DIR)/,$(ASM_SRCS:.S=.o))
OBJS += $(NRF_EPD_PROJECTIONS:.c=.o)
OBJS += $(NRF_GUI_PROJECTIONS:.c=.o)
OBJS += $(NRF_FONT_PROJECTION:.c=.o)
CONFIG_STAMP := $(BUILD_DIR)/.build-config

$(BUILD_DIR)/font-projection/u8g2_font.c: compat/common/gui/u8g2_font.c build-support/gui/cache-font-glyphs.py $(CONFIG_STAMP)
	python3 build-support/gui/cache-font-glyphs.py $< $@ $(INKTAG_NRF_GUI_GLYPH_CACHE_ENTRIES)

$(BUILD_DIR)/font-projection/%.o: $(BUILD_DIR)/font-projection/%.c
	$(CC) $(CFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(BUILD_DIR)/gui-projection/inktag_gui.c: compat/common/gui/inktag_gui.c compat/common/gui/inktag_gui_default.inc compat/common/gui/inktag_gui_calendar.inc compat/common/gui/inktag_calendar.c build-support/gui/ram-gui-tables.py build-support/gui/gui-pool-v1.json
	python3 build-support/gui/ram-gui-tables.py compat/common/gui $(dir $@)

$(BUILD_DIR)/gui-projection/inktag_calendar.c: $(BUILD_DIR)/gui-projection/inktag_gui.c
	@test -f $@

$(BUILD_DIR)/gui-projection/%.o: $(BUILD_DIR)/gui-projection/%.c
	$(CC) $(CFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(BUILD_DIR)/epd-projection/%.c: compat/common/epd/%.c build-support/gui/compact-epd-commands.py
	python3 build-support/gui/compact-epd-commands.py $< $@

$(BUILD_DIR)/epd-projection/%.o: $(BUILD_DIR)/epd-projection/%.c
	$(CC) $(CFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

.PHONY: all clean merged optimized optimized-merged size check-size check-runtime check-toolchain sync-version FORCE print-delivery-inputs print-delivery-config

print-delivery-config:
	@printf '%s\n' 'CC=$(CC)' 'AS=$(AS)' 'OBJCOPY=$(OBJCOPY)' 'CFLAGS=$(CFLAGS)' 'ASFLAGS=$(ASFLAGS)' 'LDFLAGS=$(LDFLAGS)' 'NRF_EXTRA_LIBS=$(NRF_EXTRA_LIBS)' 'NRF_DEPENDENCY_FLAGS=$(NRF_DEPENDENCY_FLAGS)' 'SOURCES=$(C_SRCS) $(ASM_SRCS)' 'LINKER_SCRIPT=$(LINKER_SCRIPT)' 'TARGET=$(TARGET)' 'MAKEFILES=$(filter-out %.d,$(MAKEFILE_LIST))'

print-delivery-inputs:
	@printf '%s\n' $(sort $(C_SRCS) $(ASM_SRCS) $(LINKER_SCRIPT) $(SOFTDEVICE_HEX) $(wildcard $(SDK_ROOT)/modules/nrfx/mdk/*.ld))

all: sync-version check-toolchain $(APP_HEX) $(APP_BIN) $(AUXILIARY_ARTIFACTS) size check-size check-runtime

sync-version:
	python3 build-support/version/sync-version.py --repo .

merged: sync-version check-toolchain $(MERGED_HEX)

# The optimized image keeps S112, the two-page config journal, Buttonless DFU, every EPD driver and all
# GUI layouts/fonts. It enables whole-program optimization, uses the real
# nRF52811 errata selection and permits GCC's semantics-preserving builtins.
optimized:
	$(MAKE) -f $(NRF_PORT_MAKEFILE) TOOLCHAIN_PATH="$(TOOLCHAIN_PATH)" \
		BUILD_DIR="$(OPTIMIZED_BUILD_DIR)" OUTPUT_DIR="$(OPTIMIZED_OUTPUT_DIR)" TARGET="$(OPTIMIZED_TARGET)" \
		NRF_LTO=1 NRF_USE_BUILTINS=1 NRF_52840_DEVELOPMENT_COMPAT=0 \
		NRF_APP_FLASH_LIMIT=69632 all

optimized-merged:
	$(MAKE) -f $(NRF_PORT_MAKEFILE) TOOLCHAIN_PATH="$(TOOLCHAIN_PATH)" \
		BUILD_DIR="$(OPTIMIZED_BUILD_DIR)" OUTPUT_DIR="$(OPTIMIZED_OUTPUT_DIR)" TARGET="$(OPTIMIZED_TARGET)" \
		NRF_LTO=1 NRF_USE_BUILTINS=1 NRF_52840_DEVELOPMENT_COMPAT=0 \
		NRF_APP_FLASH_LIMIT=69632 all merged

check-toolchain:
	@if [ ! -f "$(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c" ]; then \
		echo "Missing Nordic nRF5 SDK 17.1.0: $(SDK_ROOT)"; \
		echo "Run: pwsh build-support/sdk/setup-sdk.ps1 -Platform nrf"; \
		echo "For offline builds pass SDK_ROOT and TOOLCHAIN_PATH explicitly."; \
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
	$(OBJCOPY) -O binary $< $@

ifeq ($(NRF_CHIP),nrf52811)
$(FONT_ASSET_FONT_OBJECT): compat/common/gui/inktag_gui_fonts.c compat/common/gui/inktag_gui_fonts.h compat/common/gui/u8g2_font.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1 $(LTO_FLAGS),$(CFLAGS)) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(FONT_ASSET_QR_OBJECT): compat/common/gui/inktag_gui_device_qr_asset.c compat/common/gui/inktag_gui_device_qr.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1 $(LTO_FLAGS),$(CFLAGS)) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(FONT_ASSET_LUNAR_OBJECT): compat/common/gui/inktag_calendar.c compat/common/gui/inktag_calendar.h compat/common/gui/inktag_gui_fonts.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -DINKTAG_GUI_EXTERNAL_FONT_ASSETS=1 $(LTO_FLAGS),$(CFLAGS)) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(FONT_ASSET_ELF): $(FONT_ASSET_OBJECTS) app/external-fonts.ld
	$(CC) $(MCU_FLAGS) -nostdlib -Wl,--build-id=none -Wl,--gc-sections \
		-Wl,-T,app/external-fonts.ld $(FONT_ASSET_OBJECTS) -o $@

$(FONT_ASSET_PAYLOAD): $(FONT_ASSET_ELF)
	$(OBJCOPY) -O binary $< $@

$(FONT_ASSET_IMAGE): $(FONT_ASSET_PAYLOAD) build-support/gui/package-font-assets.py $(NRF_GUI_PROJECTIONS)
	@mkdir -p $(dir $@)
	python3 build-support/gui/package-font-assets.py $< $@ $(if $(NRF_GUI_PROJECTIONS),--tables $(BUILD_DIR)/gui-projection/tables.bin,)

$(BUILD_DIR)/gui-projection/inktag_nrf_gui_asset_binding.h: $(FONT_ASSET_IMAGE) build-support/gui/package-font-assets.py
	python3 build-support/gui/package-font-assets.py --binding-only $< $@

$(BUILD_DIR)/compat/nordic/src/inktag_nrf_external_assets.o: $(BUILD_DIR)/gui-projection/inktag_nrf_gui_asset_binding.h
endif

$(MERGED_HEX): $(SOFTDEVICE_HEX) $(APP_HEX) build-support/hex/merge_hex.py
	@mkdir -p $(dir $@)
	python3 build-support/hex/merge_hex.py $@ $(SOFTDEVICE_HEX) $(APP_HEX)

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(NRF_EXTRA_LIBS) -o $@.tmp
	mv $@.tmp $@

# Make does not normally notice command-line flag changes. Keep one compact
# stamp so switching LTO/builtin/chip-selection options cannot reuse stale
# objects from a previous build directory invocation.
$(CONFIG_STAMP): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' 'BOARD=$(BOARD)' 'NRF_CHIP=$(NRF_CHIP)' 'SOFTDEVICE=$(SOFTDEVICE)' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'ASFLAGS=$(ASFLAGS)' 'LDFLAGS=$(LDFLAGS)' 'NRF_DEPENDENCY_FLAGS=$(NRF_DEPENDENCY_FLAGS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi

$(OBJS): $(CONFIG_STAMP)
ifneq ($(strip $(FONT_ASSET_OBJECTS)),)
$(FONT_ASSET_OBJECTS): $(CONFIG_STAMP)
endif

FORCE:

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

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

-include $(OBJS:.o=.d) $(FONT_ASSET_OBJECTS:.o=.d)
