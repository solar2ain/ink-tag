SDK_ROOT ?= .sdk/nordic/nRF5_SDK_17.1.0_ddde560
.DEFAULT_GOAL := all
INKTAG_BUILD_PRO ?= 0
INKTAG_AUX_RESERVED_ENABLED ?= 0
ifneq ($(INKTAG_AUX_RESERVED_ENABLED),0)
$(error Nordic reserved AUX backend is disabled; INKTAG_AUX_RESERVED_ENABLED must be 0)
endif
ifneq ($(INKTAG_BUILD_PRO),0)
$(error This source tree provides Base only; INKTAG_BUILD_PRO must be 0)
endif
# Common integrity-only Recovery; the public cache is Base only.
NRF_RECOVERY_OUTPUT_SUFFIX := -base
override INKTAG_BUILD_PRO := 0
NRF_SOURCE_PROFILE := base
NRF_PROFILE_SUFFIX := $(NRF_RECOVERY_OUTPUT_SUFFIX)
BOARD ?= nrf52832_priceetag
SUPPORTED_BOARDS ?= nrf52832_priceetag
ifeq ($(filter $(BOARD),$(SUPPORTED_BOARDS)),)
$(error Unsupported Nordic bootloader board '$(BOARD)'; expected one of: $(SUPPORTED_BOARDS))
endif
PROJECT_DFU_DIR ?= firmware/ports/nordic/common/bootloader/src
PROJECT_DFU_CONFIG_DIR ?= firmware/ports/nordic/nrf52832/bootloader
BOARD_DIR := firmware/boards
BOARD_FAMILY_HEADER := firmware/boards/nrf52832_priceetag.h
BUILD_DIR ?= .cache/build/nordic/$(BOARD)$(NRF_PROFILE_SUFFIX)/bootloader
OUTPUT_DIR ?= _build/nordic/$(BOARD)$(NRF_PROFILE_SUFFIX)/bootloader
TARGET ?= InkTag-$(BOARD)-bootloader
LINKER_SCRIPT ?= firmware/ports/nordic/nrf52832/bootloader/bootloader.ld
BOOTLOADER_FLASH_LIMIT ?= 24576
DEVICE_DEFINE ?= NRF52832_XXAA
SOFTDEVICE_DEFINE ?= S132
SOFTDEVICE ?= s132
STARTUP_FILE ?= gcc_startup_nrf52.S
# Preserve the existing bench shared MSP budget; no layout change.
NRF_BOOTLOADER_STACK_SIZE := 4096
BOOTLOADER_LAYOUT_DEFINES ?=
ifeq ($(strip $(BOOTLOADER_LAYOUT_DEFINES)),)
$(error Use firmware/ports/nordic/nrf52832/bootloader/Makefile for the exact-board layout)
endif
DFU_FLASH_SRC ?= firmware/ports/nordic/common/src/inktag_nrf_dfu_flash.c
DFU_SETTINGS_SVCI_SRC ?= firmware/ports/nordic/common/src/inktag_nrf_dfu_fixed_name.c
BOOTLOADER_SVCI_HANDLER_SRCS ?=
BOOTLOADER_EXTRA_SRCS ?= firmware/common/power/inktag_power.c
BOOTLOADER_EXTRA_SRCS += firmware/ports/nordic/common/src/inktag_nrf_ble_address.c \
  firmware/common/ble/inktag_ble_address.c
DFU_CRYPTO_SRC := firmware/ports/nordic/common/src/inktag_nrf_recovery_hash.c
DFU_VER_VALIDATION_SRC ?= firmware/ports/nordic/common/src/inktag_nrf_dfu_ver_validation.c
DFU_ACTIVATION_SRC ?= firmware/ports/nordic/common/src/inktag_nrf_dfu_activation.c
NORDIC_DFU_VALIDATION_SOURCE := $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_validation.c
DFU_VALIDATION_SRC ?= $(BUILD_DIR)/generated/nrf_dfu_validation.c
DFU_VALIDATION_MATERIALIZER := tools/sdk/materialize-nordic-base-dfu-validation.py
DFU_SETTINGS_INPUT := $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_settings.c
DFU_SETTINGS_SRC := $(BUILD_DIR)/generated/nrf_dfu_settings.c
DFU_SETTINGS_MATERIALIZER := tools/sdk/materialize-nordic-dfu-settings.py
DFU_REQ_HANDLER_SRC := $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_req_handler.c
DFU_BLE_SRC := $(SDK_ROOT)/components/libraries/bootloader/ble_dfu/nrf_dfu_ble.c
BOOTLOADER_SRC := $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader.c
ifneq ($(INKTAG_NRF_SAFE_RESUME),1)
$(error NRF32 mandatory boot verification requires the maintained guarded boot source)
endif
ifeq ($(INKTAG_NRF_SAFE_RESUME),1)
DFU_RESUME_MATERIALIZER := tools/sdk/materialize-nordic-dfu-resume.py
DFU_RESUME_INPUTS := $(DFU_REQ_HANDLER_SRC) $(DFU_BLE_SRC) $(BOOTLOADER_SRC)
DFU_REQ_HANDLER_SRC := $(BUILD_DIR)/generated/nrf_dfu_req_handler.c
DFU_BLE_SRC := $(BUILD_DIR)/generated/nrf_dfu_ble.c
BOOTLOADER_SRC := $(BUILD_DIR)/generated/nrf_bootloader.c
DFU_RESUME_GENERATED := $(DFU_REQ_HANDLER_SRC) $(DFU_BLE_SRC) $(BOOTLOADER_SRC)
BOOTLOADER_EXTRA_SRCS += firmware/ports/nordic/common/src/inktag_nrf_dfu_resume.c
BOOTLOADER_LAYOUT_DEFINES += -DINKTAG_NRF_SAFE_RESUME=1
endif
BOOTLOADER_EXTRA_INCLUDES ?= \
  -Ifirmware/common/power \
  -Ifirmware/ports/nordic/common/src \
  -Ifirmware/ports/nordic/common/config

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
  -DBLE_STACK_SUPPORT_REQD \
  -DFLOAT_ABI_SOFT \
  -D$(DEVICE_DEFINE) \
  -DNRF_DFU_SETTINGS_VERSION=2 \
  -DNRF_DFU_SVCI_ENABLED \
  -DNRF_SD_BLE_API_VERSION=7 \
  -D$(SOFTDEVICE_DEFINE) \
  -DSOFTDEVICE_PRESENT \
  -DSVC_INTERFACE_CALL_AS_NORMAL_FUNCTION \
  -D__STARTUP_CLEAR_BSS \
  -D__START=main \
  -D__HEAP_SIZE=0 \
  -D__STACK_SIZE=$(NRF_BOOTLOADER_STACK_SIZE) \
  $(BOOTLOADER_LAYOUT_DEFINES)

DEFINES += -DINKTAG_CODE_GUARDS=0 -DINKTAG_AUX_RESERVED_ENABLED=0
INCLUDES := \
  -I$(BOARD_DIR) \
  -Ifirmware/common/ble \
  -Ifirmware/common/ota \
  -Ifirmware/common/security \
  -I$(PROJECT_DFU_CONFIG_DIR) \
  -I$(PROJECT_DFU_DIR) \
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
LDFLAGS := $(MCU_FLAGS) -Oz -T$(LINKER_SCRIPT) -L$(BUILD_DIR)/generated -L$(SDK_ROOT)/modules/nrfx/mdk \
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
  $(DFU_REQ_HANDLER_SRC) \
  $(DFU_SETTINGS_SRC) \
  $(DFU_SETTINGS_SVCI_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_svci.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_transport.c \
  $(SDK_ROOT)/components/libraries/bootloader/dfu/nrf_dfu_utils.c \
  $(DFU_VALIDATION_SRC) \
  $(DFU_VER_VALIDATION_SRC) \
  $(BOOTLOADER_SRC) \
  $(BUILD_DIR)/generated/nrf_bootloader_app_start.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_app_start_final.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_dfu_timers.c \
  $(DFU_ACTIVATION_SRC) \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_info.c \
  $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_wdt.c \
  $(SDK_ROOT)/components/libraries/crc32/crc32.c \
  $(DFU_CRYPTO_SRC) \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/mem_manager/mem_manager.c \
  $(SDK_ROOT)/components/libraries/ringbuf/nrf_ringbuf.c \
  $(SDK_ROOT)/components/libraries/scheduler/app_scheduler.c \
  $(SDK_ROOT)/components/libraries/sha256/sha256.c \
  $(SDK_ROOT)/components/libraries/strerror/nrf_strerror.c \
  $(SDK_ROOT)/components/libraries/svc/nrf_svc_handler.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(SDK_ROOT)/external/nano-pb/pb_common.c \
  $(SDK_ROOT)/external/nano-pb/pb_decode.c \
  $(SDK_ROOT)/modules/nrfx/hal/nrf_nvmc.c \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(BOOTLOADER_SVCI_HANDLER_SRCS) \
  $(BOOTLOADER_EXTRA_SRCS)

C_SRCS += firmware/common/security/inktag_code_guard.c
# Common Recovery has no Pro implementation; keep the inert public ABI only.
NRF_PROFILE_SELECTOR := tools/sdk/materialize-nordic-source-profile.py
NRF_PROFILE_SELECTION := $(BUILD_DIR)/generated/source-profile.mk
NRF_COMMON_LOGICAL_SRCS := $(filter firmware/common/%,$(C_SRCS))
NRF_PROFILE_GENERATE := $(shell python3 $(NRF_PROFILE_SELECTOR) --repo . --profile $(NRF_SOURCE_PROFILE) --output $(NRF_PROFILE_SELECTION) $(NRF_COMMON_LOGICAL_SRCS))
ifneq ($(.SHELLSTATUS),0)
$(error Nordic Recovery source-profile selection failed)
endif
include $(NRF_PROFILE_SELECTION)
C_SRCS := $(filter-out $(NRF_PROFILE_OMITTED),$(C_SRCS))
override CFLAGS += $(NRF_PROFILE_DEFINES)
override ASFLAGS += $(NRF_PROFILE_DEFINES)
ASM_SRCS := $(SDK_ROOT)/modules/nrfx/mdk/$(STARTUP_FILE)
OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o)) $(addprefix $(BUILD_DIR)/,$(ASM_SRCS:.S=.o)) $(NRF_PROFILE_NOTE_OBJECT)
NRF_DFU_SETTINGS_OBJ := $(BUILD_DIR)/$(DFU_SETTINGS_SRC:.c=.o)
# LTO can resolve the SDK's definition and caller before GNU ld applies
# --wrap. Rename only the definition object so every normal SDK caller binds
# to InkTag's confirmation adapter while that adapter can still call the
# original implementation explicitly.
$(NRF_DFU_SETTINGS_OBJ): CFLAGS += \
  -Dnrf_dfu_settings_init=inktag_nrf_dfu_settings_init_original
BOOTLOADER_HEX := $(OUTPUT_DIR)/$(TARGET).hex
BOOTLOADER_BIN := $(OUTPUT_DIR)/$(TARGET).bin

.PHONY: all clean check-toolchain size check-size

all: check-toolchain $(BOOTLOADER_HEX) $(BOOTLOADER_BIN) size check-size

check-toolchain:
	@test -f "$(SDK_ROOT)/modules/nrfx/mdk/system_nrf52.c" || { \
		echo "Missing Nordic nRF5 SDK 17.1.0: $(SDK_ROOT)"; \
		echo "Run tools/sdk/setup-sdk.ps1 -Platform nrf or tools/sdk/setup-sdk.sh --platform nrf"; \
		exit 2; \
	}
	@command -v "$(CC)" >/dev/null 2>&1 || { echo "Missing ARM GCC: $(CC)"; exit 127; }
	@command -v python3 >/dev/null 2>&1 || { echo "Missing Python 3"; exit 127; }
	@python3 $(DFU_VALIDATION_MATERIALIZER) --source $(NORDIC_DFU_VALIDATION_SOURCE) --check-only
	@python3 $(DFU_SETTINGS_MATERIALIZER) --source $(DFU_SETTINGS_INPUT) --check-only
ifeq ($(INKTAG_NRF_SAFE_RESUME),1)
	@$(foreach source,$(DFU_RESUME_INPUTS),python3 $(DFU_RESUME_MATERIALIZER) --source $(source) --check-only &&) true

$(DFU_RESUME_GENERATED): $(DFU_RESUME_INPUTS) $(DFU_RESUME_MATERIALIZER)
	@python3 $(DFU_RESUME_MATERIALIZER) --source $(filter %/$(notdir $@),$(DFU_RESUME_INPUTS)) --output $@
endif

$(DFU_VALIDATION_SRC): $(NORDIC_DFU_VALIDATION_SOURCE) $(DFU_VALIDATION_MATERIALIZER)
	@python3 $(DFU_VALIDATION_MATERIALIZER) --source $< --output $@

$(DFU_SETTINGS_SRC): $(DFU_SETTINGS_INPUT) $(DFU_SETTINGS_MATERIALIZER)
	@python3 $(DFU_SETTINGS_MATERIALIZER) --source $< --output $@

NRF_GUARD_TOOL := tools/sdk/nordic_base_identity.py
NRF_GUARD_MATERIALIZER := tools/sdk/materialize-nordic-base-sdk.py
NRF_VERIFY_MODULE := tools/sdk/nordic_base_identity.py
NRF_LINK_FINALIZE :=
NRF_GUARD_LINKER := $(BUILD_DIR)/generated/nrf_common.ld
$(NRF_GUARD_LINKER): $(SDK_ROOT)/modules/nrfx/mdk/nrf_common.ld $(NRF_GUARD_MATERIALIZER) version.json
	python3 $(NRF_GUARD_MATERIALIZER) --source $< --output $@ --role recovery

NRF_CRC_INPUT := $(SDK_ROOT)/components/libraries/crc32/crc32.c
NRF_CRC_GENERATED := $(BUILD_DIR)/generated/crc32.c
$(NRF_CRC_GENERATED): $(NRF_CRC_INPUT) $(NRF_GUARD_MATERIALIZER)
	python3 $(NRF_GUARD_MATERIALIZER) --source $< --output $@ --role recovery
$(BUILD_DIR)/$(NRF_CRC_INPUT:.c=.o): $(NRF_CRC_GENERATED)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/generated/nrf_bootloader_app_start.c: $(SDK_ROOT)/components/libraries/bootloader/nrf_bootloader_app_start.c $(NRF_GUARD_MATERIALIZER)
	python3 $(NRF_GUARD_MATERIALIZER) --source $< --output $@ --role recovery

$(BUILD_DIR)/$(TARGET).elf: $(OBJS) $(LINKER_SCRIPT) $(NRF_GUARD_LINKER) $(NRF_GUARD_TOOL) $(NRF_VERIFY_MODULE) tools/sdk/goodix_code_elf.py
	$(CC) $(LDFLAGS) $(OBJS) -o $@.tmp
	python3 $(NRF_GUARD_TOOL) --role recovery --source-profile $(NRF_SOURCE_PROFILE) --elf $@.tmp $(NRF_LINK_FINALIZE)
	mv $@.tmp $@

.PHONY: nrf-code-verify
all: nrf-code-verify
nrf-code-verify: $(BUILD_DIR)/$(TARGET).elf $(BOOTLOADER_HEX) $(BOOTLOADER_BIN)
	python3 $(NRF_GUARD_TOOL) --role recovery --source-profile $(NRF_SOURCE_PROFILE) --elf $(BUILD_DIR)/$(TARGET).elf --hex $(BOOTLOADER_HEX) --bin $(BOOTLOADER_BIN)

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
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -MMD -MP -c $< -o $@

# Board wrappers supply device, SoftDevice and layout defines before including
# this Makefile. Rebuild objects whenever either Makefile changes so a changed
# DFU boundary or entry method cannot silently reuse stale objects.
$(OBJS): $(MAKEFILE_LIST)

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
