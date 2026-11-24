SDK_ROOT ?= $(shell python3 build-support/sdk/primary-checkout.py --sdk)
.DEFAULT_GOAL := all
INKTAG_AUX_RESERVED_ENABLED ?= 0
ifneq ($(strip $(INKTAG_AUX_RESERVED_ENABLED)),0)
$(error This nRF52811 build requires INKTAG_AUX_RESERVED_ENABLED=0)
endif
override INKTAG_AUX_RESERVED_ENABLED := 0
BOARD ?= nrf52811_aesl
SUPPORTED_BOARDS ?= nrf52811_aesl
ifeq ($(filter $(BOARD),$(SUPPORTED_BOARDS)),)
$(error Unsupported Nordic bootloader board '$(BOARD)'; expected one of: $(SUPPORTED_BOARDS))
endif
PROJECT_DFU_DIR ?= app/bootloader/src
PROJECT_DFU_CONFIG_DIR ?= $(PROJECT_DFU_DIR)
BOARD_DIR := ../../../boards
BOARD_FAMILY_HEADER := $(BOARD_DIR)/nrf52811_aesl.h
BUILD_DIR ?= .cache/build/nordic/$(BOARD)/crc-only/bootloader
OUTPUT_DIR ?= _build/nordic/$(BOARD)/crc-only/bootloader
TARGET ?= InkTag-$(BOARD)-bootloader
NRF_DEPENDENCY_FLAGS ?= -MMD -MP
LINKER_SCRIPT ?= app/bootloader/bootloader.ld
override BOOTLOADER_FLASH_LIMIT := 16384
DEVICE_DEFINE ?= NRF52811_XXAA
SOFTDEVICE_DEFINE ?= S112
SOFTDEVICE ?= s112
STARTUP_FILE ?= gcc_startup_nrf52811.S
INKTAG_NRF_COMPACT_BOOT_VECTORS ?= 1
NORDIC_STARTUP_SOURCE := $(SDK_ROOT)/modules/nrfx/mdk/$(STARTUP_FILE)
STARTUP_SRC := $(NORDIC_STARTUP_SOURCE)
ifeq ($(INKTAG_NRF_COMPACT_BOOT_VECTORS),1)
STARTUP_MATERIALIZER := build-support/sdk/materialize-nrf52811-startup.py
STARTUP_SRC := $(BUILD_DIR)/generated/gcc_startup_nrf52811.S
endif
ifneq ($(origin INKTAG_NRF_REQUIRE_SIGNED_DFU),undefined)
$(error INKTAG_NRF_REQUIRE_SIGNED_DFU is retired; current Recovery is CRC-only)
endif
BOOTLOADER_LAYOUT_DEFINES ?=
INKTAG_NRF_SAFE_RESUME ?= 1
# Ordinary Recovery shares the App's factory address. 0 preserves the old
# SDK +1 address behavior for compatibility qualification.
INKTAG_NRF_RECOVERY_SAME_MAC ?= 1
ifeq ($(INKTAG_NRF_RECOVERY_SAME_MAC),1)
ifneq ($(INKTAG_NRF_SAFE_RESUME),1)
$(error Same-address Recovery requires the hash-locked safe-resume projection)
endif
BOOTLOADER_LAYOUT_DEFINES += -DINKTAG_NRF_RECOVERY_SAME_MAC=1
else ifneq ($(INKTAG_NRF_RECOVERY_SAME_MAC),0)
$(error INKTAG_NRF_RECOVERY_SAME_MAC must be 0 or 1)
endif
DFU_FLASH_SRC ?= compat/nordic/src/inktag_nrf_dfu_flash.c
DFU_SETTINGS_SVCI_SRC ?= compat/nordic/src/inktag_nrf_dfu_fixed_name.c
BOOTLOADER_SVCI_HANDLER_SRCS ?=
BOOTLOADER_EXTRA_SRCS ?= compat/common/power/inktag_power.c
DFU_HASH_SRC := compat/nordic/src/inktag_nrf_dfu_hash.c
DFU_VER_VALIDATION_SRC ?= compat/nordic/src/inktag_nrf_dfu_ver_validation.c
DFU_ACTIVATION_SRC ?= compat/nordic/src/inktag_nrf_dfu_activation.c
NORDIC_DFU_VALIDATION_SOURCE := $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_validation.c
DFU_VALIDATION_SRC ?= $(BUILD_DIR)/generated/nrf_dfu_validation.c
DFU_VALIDATION_MATERIALIZER := build-support/sdk/materialize-nordic-dfu-validation.py
NORDIC_DFU_REQUEST_SOURCE := $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_req_handler.c
DFU_REQUEST_SRC ?= $(BUILD_DIR)/generated/nrf_dfu_req_handler.c
DFU_REQUEST_MATERIALIZER := build-support/sdk/materialize-nordic-dfu-request.py
NORDIC_DFU_BLE_SOURCE := $(SDK_ROOT)/components/libraries/bootloader/ble_dfu/nrf_dfu_ble.c
NORDIC_BOOTLOADER_SOURCE := $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader.c
DFU_BLE_SRC := $(NORDIC_DFU_BLE_SOURCE)
BOOTLOADER_SRC := $(NORDIC_BOOTLOADER_SOURCE)
ifeq ($(INKTAG_NRF_SAFE_RESUME),1)
DFU_RESUME_MATERIALIZER := build-support/sdk/materialize-nordic-dfu-resume.py
DFU_REQUEST_MATERIALIZER := $(DFU_RESUME_MATERIALIZER)
DFU_BLE_SRC := $(BUILD_DIR)/generated/nrf_dfu_ble.c
BOOTLOADER_SRC := $(BUILD_DIR)/generated/nrf_bootloader.c
BOOTLOADER_EXTRA_SRCS += compat/nordic/src/inktag_nrf_dfu_resume.c
BOOTLOADER_LAYOUT_DEFINES += -DINKTAG_NRF_SAFE_RESUME=1
endif
BOOTLOADER_EXTRA_INCLUDES ?= \
  -Icompat/common/ota \
  -Icompat/common/power \
  -Icompat/common/status \
  -Icompat/nordic/src

ifeq ($(wildcard $(BOARD_FAMILY_HEADER)),)
$(error Missing Nordic firmware-family description $(BOARD_FAMILY_HEADER))
endif

TOOLCHAIN_PATH ?= $(abspath .tools/arm-gnu-toolchain/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin)
TOOLCHAIN_PREFIX := $(if $(TOOLCHAIN_PATH),$(TOOLCHAIN_PATH)/,)
CC := $(TOOLCHAIN_PREFIX)arm-none-eabi-gcc
AS := $(TOOLCHAIN_PREFIX)arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)arm-none-eabi-objcopy
SIZE := $(TOOLCHAIN_PREFIX)arm-none-eabi-size

MCU_FLAGS := -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=soft
DEFINES := \
  -DINKTAG_AUX_RESERVED_ENABLED=$(INKTAG_AUX_RESERVED_ENABLED) \
  -DBLE_STACK_SUPPORT_REQD \
  -DFLOAT_ABI_SOFT \
  -D$(DEVICE_DEFINE) \
  -DNRF_DFU_SETTINGS_VERSION=2 \
  -DINKTAG_NRF_RECOVERY_FLASH_SIZE=$(BOOTLOADER_FLASH_LIMIT) \
  -DNRF_DFU_SVCI_ENABLED \
  -DNRF_SD_BLE_API_VERSION=7 \
  -D$(SOFTDEVICE_DEFINE) \
  -DSOFTDEVICE_PRESENT \
  -DSVC_INTERFACE_CALL_AS_NORMAL_FUNCTION \
  -D__STARTUP_CLEAR_BSS \
  -D__START=main \
  -D__HEAP_SIZE=0 \
  -D__STACK_SIZE=2048 \
  $(BOOTLOADER_LAYOUT_DEFINES)

INCLUDES := \
  -I$(BOARD_DIR) \
  -I$(PROJECT_DFU_CONFIG_DIR) \
  -I$(PROJECT_DFU_DIR) \
  -Icompat/nordic/config \
  -I$(SDK_ROOT)/components/ble/common \
  -I$(SDK_ROOT)/components/libraries/atomic \
  -I$(SDK_ROOT)/components/libraries/atomic_fifo \
  -I$(SDK_ROOT)/components/libraries/balloc \
  -I$(SDK_ROOT)/components/libraries/bootloader \
  -I$(SDK_ROOT)/components/libraries/bootloader/ble_dfu \
  -I$(SDK_ROOT)/components/libraries/bootloader/dfu \
  -I$(SDK_ROOT)/components/libraries/crc32 \
  -I$(SDK_ROOT)/components/libraries/delay \
  -I$(SDK_ROOT)/components/libraries/experimental_section_vars \
  -I$(SDK_ROOT)/components/libraries/fstorage \
  -I$(SDK_ROOT)/components/libraries/log \
  -I$(SDK_ROOT)/components/libraries/log/src \
  -I$(SDK_ROOT)/components/libraries/mem_manager \
  -I$(SDK_ROOT)/components/libraries/memobj \
  -I$(SDK_ROOT)/components/libraries/queue \
  -I$(SDK_ROOT)/components/libraries/ringbuf \
  -I$(SDK_ROOT)/components/libraries/scheduler \
  -I$(SDK_ROOT)/components/libraries/sha256 \
  -I$(SDK_ROOT)/components/libraries/stack_info \
  -I$(SDK_ROOT)/components/libraries/strerror \
  -I$(SDK_ROOT)/components/libraries/svc \
  -I$(SDK_ROOT)/components/libraries/util \
  -I$(SDK_ROOT)/components/softdevice/common \
  -I$(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/headers \
  -I$(SDK_ROOT)/components/softdevice/$(SOFTDEVICE)/headers/nrf52 \
  -I$(SDK_ROOT)/components/toolchain/cmsis/include \
  -I$(SDK_ROOT)/external/nano-pb \
  -I$(SDK_ROOT)/integration/nrfx \
  -I$(SDK_ROOT)/modules/nrfx \
  -I$(SDK_ROOT)/modules/nrfx/hal \
  -I$(SDK_ROOT)/modules/nrfx/mdk

INCLUDES += $(BOOTLOADER_EXTRA_INCLUDES)

CFLAGS := $(MCU_FLAGS) $(DEFINES) $(INCLUDES) -Oz -g3 -Wall -Wno-unused-function \
  -ffunction-sections -fdata-sections -fno-strict-aliasing -fshort-enums -std=gnu99 \
  -flto=1 -flto-partition=one -fuse-linker-plugin -fmerge-all-constants \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-jump-tables -fipa-pta
ASFLAGS := $(MCU_FLAGS) $(DEFINES) $(INCLUDES) -x assembler-with-cpp
LDFLAGS := $(MCU_FLAGS) -Oz -T$(LINKER_SCRIPT) -L$(SDK_ROOT)/modules/nrfx/mdk \
  -Wl,--gc-sections -Wl,--relax -Wl,-Map=$(BUILD_DIR)/$(TARGET).map \
  -flto=1 -flto-partition=one -fuse-linker-plugin -nostartfiles \
  --specs=nano.specs --specs=nosys.specs

C_SRCS := \
  $(PROJECT_DFU_DIR)/main.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/balloc/nrf_balloc.c \
  $(DFU_BLE_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/dfu-cc.pb.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu.c \
  $(DFU_FLASH_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_handling_error.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_mbr.c \
  $(DFU_REQUEST_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_settings.c \
  $(DFU_SETTINGS_SVCI_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_svci.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_transport.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_utils.c \
  $(DFU_VALIDATION_SRC) \
  $(DFU_VER_VALIDATION_SRC) \
  $(BOOTLOADER_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_app_start.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_app_start_final.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_dfu_timers.c \
  $(DFU_ACTIVATION_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_info.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_wdt.c \
  $(SDK_ROOT)/components/libraries/crc32/crc32.c \
  $(DFU_HASH_SRC) \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/mem_manager/mem_manager.c \
  $(SDK_ROOT)/components/libraries/ringbuf/nrf_ringbuf.c \
  $(SDK_ROOT)/components/libraries/scheduler/app_scheduler.c \
  $(SDK_ROOT)/components/libraries/sha256/sha256.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(BUILD_DIR)/generated/nrf_svc_handler.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(BUILD_DIR)/generated/nrf_sdh.c \
  $(BUILD_DIR)/generated/nrf_sdh_ble.c \
  $(BUILD_DIR)/generated/nrf_sdh_soc.c \
  $(SDK_ROOT)/external/nano-pb/pb_common.c \
  $(SDK_ROOT)/external/nano-pb/pb_decode.c \
  $(SDK_ROOT)/modules/nrfx/hal/nrf_nvmc.c \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(BOOTLOADER_SVCI_HANDLER_SRCS) \
  $(BOOTLOADER_EXTRA_SRCS)

ASM_SRCS := $(STARTUP_SRC)
OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o)) $(addprefix $(BUILD_DIR)/,$(ASM_SRCS:.S=.o))
NRF_DFU_SETTINGS_OBJ := \
  $(BUILD_DIR)/$(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_settings.o
# LTO can resolve the SDK's definition and caller before GNU ld applies
# --wrap. Rename only the definition object so every normal SDK caller binds
# to InkTag's confirmation adapter while that adapter can still call the
# original implementation explicitly.
$(NRF_DFU_SETTINGS_OBJ): CFLAGS += \
  -Dnrf_dfu_settings_init=inktag_nrf_dfu_settings_init_original
NRF_DFU_TRANSPORT_OBJ := \
  $(BUILD_DIR)/$(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_transport.o
# Bind ordinary callers to the role-readiness adapter before LTO resolution.
$(NRF_DFU_TRANSPORT_OBJ): CFLAGS += \
  -Dnrf_dfu_transports_init=inktag_nrf_dfu_transports_init_original
BOOTLOADER_HEX := $(OUTPUT_DIR)/$(TARGET).hex
BOOTLOADER_BIN := $(OUTPUT_DIR)/$(TARGET).bin

.PHONY: all clean check-toolchain size check-size print-delivery-inputs print-delivery-config

print-delivery-config:
	@printf '%s\n' 'CC=$(CC)' 'AS=$(AS)' 'OBJCOPY=$(OBJCOPY)' 'CFLAGS=$(CFLAGS)' 'ASFLAGS=$(ASFLAGS)' 'LDFLAGS=$(LDFLAGS)' 'NRF_DEPENDENCY_FLAGS=$(NRF_DEPENDENCY_FLAGS)' 'SOURCES=$(C_SRCS) $(ASM_SRCS)' 'LINKER_SCRIPT=$(LINKER_SCRIPT)' 'TARGET=$(TARGET)' 'MAKEFILES=$(filter-out %.d,$(MAKEFILE_LIST))'

print-delivery-inputs:
	@printf '%s\n' $(sort $(C_SRCS) $(ASM_SRCS) $(LINKER_SCRIPT) $(NORDIC_STARTUP_SOURCE) $(NORDIC_DFU_VALIDATION_SOURCE) $(NORDIC_DFU_REQUEST_SOURCE) $(NORDIC_DFU_BLE_SOURCE) $(NORDIC_BOOTLOADER_SOURCE) $(foreach n,nrf_sdh.c nrf_sdh_ble.c nrf_sdh_soc.c,$(SDK_ROOT)/components/softdevice/common/$(n)) $(SDK_ROOT)/components/libraries/svc/nrf_svc_handler.c $(wildcard $(SDK_ROOT)/modules/nrfx/mdk/*.ld))

all: check-toolchain $(BOOTLOADER_HEX) $(BOOTLOADER_BIN) size check-size

check-toolchain:
	@test -f "$(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c" || { \
		echo "Missing Nordic nRF5 SDK 17.1.0: $(SDK_ROOT)"; \
		echo "Run build-support/sdk/setup-sdk.ps1 -Platform nrf, or pass the read-only SDK_ROOT."; \
		exit 2; \
	}
	@command -v "$(CC)" >/dev/null 2>&1 || { echo "Missing ARM GCC: $(CC)"; exit 127; }
	@command -v python3 >/dev/null 2>&1 || { echo "Missing Python 3"; exit 127; }
	@python3 build-support/sdk/check-nordic-inputs.py --sdk-root "$(SDK_ROOT)"
	@for name in nrf_sdh.c nrf_sdh_ble.c nrf_sdh_soc.c; do python3 build-support/sdk/materialize-nordic-recovery-events.py --source "$(SDK_ROOT)/components/softdevice/common/$$name" --check-only || exit; done
	@python3 build-support/sdk/materialize-nordic-recovery-events.py --source "$(SDK_ROOT)/components/libraries/svc/nrf_svc_handler.c" --check-only
	@python3 $(DFU_VALIDATION_MATERIALIZER) --source $(NORDIC_DFU_VALIDATION_SOURCE) --check-only
	@python3 $(DFU_REQUEST_MATERIALIZER) --source $(NORDIC_DFU_REQUEST_SOURCE) --check-only
ifeq ($(INKTAG_NRF_COMPACT_BOOT_VECTORS),1)
	@python3 $(STARTUP_MATERIALIZER) --source $(NORDIC_STARTUP_SOURCE) --check-only
endif
ifeq ($(INKTAG_NRF_SAFE_RESUME),1)
	@python3 $(DFU_RESUME_MATERIALIZER) --source $(NORDIC_DFU_BLE_SOURCE) --check-only
	@python3 $(DFU_RESUME_MATERIALIZER) --source $(NORDIC_BOOTLOADER_SOURCE) --check-only
endif

$(DFU_VALIDATION_SRC): $(NORDIC_DFU_VALIDATION_SOURCE) $(DFU_VALIDATION_MATERIALIZER)
	@python3 $(DFU_VALIDATION_MATERIALIZER) --source $< --output $@

$(DFU_REQUEST_SRC): $(NORDIC_DFU_REQUEST_SOURCE) $(DFU_REQUEST_MATERIALIZER)
	@python3 $(DFU_REQUEST_MATERIALIZER) --source $< --output $@

ifeq ($(INKTAG_NRF_COMPACT_BOOT_VECTORS),1)
$(STARTUP_SRC): $(NORDIC_STARTUP_SOURCE) $(STARTUP_MATERIALIZER)
	@python3 $(STARTUP_MATERIALIZER) --source $< --output $@
endif

ifeq ($(INKTAG_NRF_SAFE_RESUME),1)
$(DFU_REQUEST_SRC): build-support/sdk/materialize-nordic-dfu-request.py

$(DFU_BLE_SRC): $(NORDIC_DFU_BLE_SOURCE) $(DFU_RESUME_MATERIALIZER) $(DFU_VALIDATION_MATERIALIZER)
	@python3 $(DFU_RESUME_MATERIALIZER) --source $< --output $@

$(BOOTLOADER_SRC): $(NORDIC_BOOTLOADER_SOURCE) $(DFU_RESUME_MATERIALIZER)
	@python3 $(DFU_RESUME_MATERIALIZER) --source $< --output $@
endif

$(addprefix $(BUILD_DIR)/generated/,nrf_sdh.c nrf_sdh_ble.c nrf_sdh_soc.c): $(BUILD_DIR)/generated/%.c: $(SDK_ROOT)/components/softdevice/common/%.c build-support/sdk/materialize-nordic-recovery-events.py build-support/sdk/materialize-nordic-dfu-validation.py
	@python3 build-support/sdk/materialize-nordic-recovery-events.py --source $< --output $@

$(BUILD_DIR)/generated/nrf_svc_handler.c: $(SDK_ROOT)/components/libraries/svc/nrf_svc_handler.c build-support/sdk/materialize-nordic-recovery-events.py build-support/sdk/materialize-nordic-dfu-validation.py
	@python3 build-support/sdk/materialize-nordic-recovery-events.py --source $< --output $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

$(BOOTLOADER_HEX): $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O ihex $< $@

$(BOOTLOADER_BIN): $(BUILD_DIR)/$(TARGET).elf
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary \
		--remove-section=.uicr_bootloader_start_address \
		--remove-section=.uicr_mbr_params_page \
		$< $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $(NRF_DEPENDENCY_FLAGS) -c $< -o $@

# Board wrappers supply device, SoftDevice and layout defines before including
# this Makefile. Rebuild objects whenever either Makefile changes so a changed
# DFU boundary or entry method cannot silently reuse stale objects.
$(OBJS): $(MAKEFILE_LIST)

# Version generation must finish before parallel object compilation. The App
# header remains generated from its separate, unchanged version fields.
RECOVERY_VERSION_HEADER := compat/common/ota/inktag_recovery_version.h
$(RECOVERY_VERSION_HEADER): version.json build-support/version/sync-version.py
	@python3 build-support/version/sync-version.py --repo .
$(OBJS): $(RECOVERY_VERSION_HEADER)

# Command-line flag changes do not alter Makefile timestamps. Bind every
# object to the effective flag so a reused 1 -> 0 cache cannot leak opt-in code.
NRF_RECOVERY_MAC_CONFIG := $(BUILD_DIR)/nrf-recovery-mac.config
.PHONY: nrf-recovery-mac-config-check
$(NRF_RECOVERY_MAC_CONFIG): nrf-recovery-mac-config-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(INKTAG_NRF_RECOVERY_SAME_MAC)' | cmp -s - "$@" || \
		printf '%s\n' '$(INKTAG_NRF_RECOVERY_SAME_MAC)' > "$@"
$(OBJS): $(NRF_RECOVERY_MAC_CONFIG)

# Bind the full effective toolchain/SDK/flags, not only the same-MAC switch.
# Keep the existing cache; only a changed stamp rebuilds its dependent objects.
NRF_RECOVERY_BUILD_CONFIG := $(BUILD_DIR)/.build-config
.PHONY: nrf-recovery-build-config-check
$(NRF_RECOVERY_BUILD_CONFIG): nrf-recovery-build-config-check
	@mkdir -p $(dir $@)
	@printf '%s\n' 'CC=$(CC)' 'CFLAGS=$(CFLAGS)' 'ASFLAGS=$(ASFLAGS)' 'LDFLAGS=$(LDFLAGS)' 'NRF_DEPENDENCY_FLAGS=$(NRF_DEPENDENCY_FLAGS)' > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm -f $@.tmp; fi
$(OBJS): $(NRF_RECOVERY_BUILD_CONFIG)

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

check-size: $(BOOTLOADER_BIN)
	@actual=$$(wc -c < "$<"); \
	if [ "$$actual" -gt "$(BOOTLOADER_FLASH_LIMIT)" ]; then \
		echo "Nordic Bootloader is $$actual bytes; limit is $(BOOTLOADER_FLASH_LIMIT) bytes" >&2; \
		exit 1; \
	fi; \
	echo "Nordic Bootloader size: $$actual / $(BOOTLOADER_FLASH_LIMIT) bytes ($$(( $(BOOTLOADER_FLASH_LIMIT) - actual )) bytes free)"

clean:
	rm -rf $(BUILD_DIR) $(OUTPUT_DIR)

-include $(OBJS:.o=.d)
