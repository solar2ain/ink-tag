BOARD := nrf52832_priceetag
SUPPORTED_BOARDS := nrf52832_priceetag
PROJECT_DFU_DIR := firmware/ports/nordic/common/bootloader/src
PROJECT_DFU_CONFIG_DIR := firmware/ports/nordic/nrf52832/bootloader
INKTAG_BUILD_PRO ?= 0
# Common integrity-only Recovery, using the Base cache.
NRF_RECOVERY_OUTPUT_SUFFIX := -base
BUILD_DIR ?= .cache/build/nordic/$(BOARD)$(NRF_RECOVERY_OUTPUT_SUFFIX)/bootloader
OUTPUT_DIR ?= _build/nordic/$(BOARD)$(NRF_RECOVERY_OUTPUT_SUFFIX)/bootloader
TARGET ?= InkTag-$(BOARD)-bootloader
LINKER_SCRIPT := firmware/ports/nordic/nrf52832/bootloader/bootloader.ld
BOOTLOADER_FLASH_LIMIT := 24576
INKTAG_NRF_SAFE_RESUME := 1
DFU_SETTINGS_SVCI_SRC = firmware/ports/nordic/common/src/inktag_nrf_dfu_fixed_name.c
BOOTLOADER_SVCI_HANDLER_SRCS =
DEVICE_DEFINE := NRF52832_XXAA
SOFTDEVICE_DEFINE := S132
SOFTDEVICE := s132
STARTUP_FILE := gcc_startup_nrf52.S
BOOTLOADER_LAYOUT_DEFINES := \
  -DINKTAG_NRF_BOARD_NRF52832_PRICETAG=1 \
  -DNRF_DFU_APP_DATA_AREA_SIZE=40960 \
  -DNRF_DFU_HW_VERSION=56 \
  -DNRF_DFU_FORCE_DUAL_BANK_APP_UPDATES=0 \
  -DNRF_DFU_SINGLE_BANK_APP_UPDATES=1 \
  -DNRF_BL_DFU_ENTER_METHOD_BUTTON=1 \
  -DNRF_BL_DFU_ENTER_METHOD_BUTTON_PIN=2
# New-product default: retain the immutable factory App address in Recovery.
# Exact6775 CLI/OTA evidence is bound in the Recovery handoff audit; browser
# qualification remains separate. Explicit 0 is a legacy diagnostic override,
# not a second product variant. App and rotating Find addresses are unchanged.
INKTAG_NRF_RECOVERY_SAME_MAC ?= 1
ifeq ($(INKTAG_NRF_RECOVERY_SAME_MAC),1)
BOOTLOADER_LAYOUT_DEFINES += -DINKTAG_NRF_RECOVERY_SAME_MAC=1
else ifneq ($(INKTAG_NRF_RECOVERY_SAME_MAC),0)
$(error INKTAG_NRF_RECOVERY_SAME_MAC must be 0 or 1)
endif
include firmware/ports/nordic/common/bootloader/Makefile
# Load the unchanged vendor ABI before applying project-only addresses.
override CFLAGS += -include firmware/ports/nordic/nrf52832/bootloader/inktag_dfu_layout.h
# Fixed board LED pins currently need no settings journal. If Recovery later
# reads configurable pins, build its own journal object with read/NULL/NULL IO:
# validated future records may be skipped without hiding a later known record.
# This rule neither links the journal today nor changes the App's strict reader.
$(BUILD_DIR)/firmware/common/storage/inktag_settings_journal.o: override CFLAGS += \
  -DINKTAG_SETTINGS_JOURNAL_READONLY_FUTURE_RECORDS=1
# Changing a command-line flag does not change MAKEFILE_LIST timestamps.
# Keep either explicit override from leaking through reused incremental objects.
NRF_RECOVERY_MAC_CONFIG := $(BUILD_DIR)/nrf-recovery-mac.config
.PHONY: nrf-recovery-mac-config-check
$(NRF_RECOVERY_MAC_CONFIG): nrf-recovery-mac-config-check
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(INKTAG_NRF_RECOVERY_SAME_MAC)' | cmp -s - "$@" || \
		printf '%s\n' '$(INKTAG_NRF_RECOVERY_SAME_MAC)' > "$@"
$(OBJS): $(NRF_RECOVERY_MAC_CONFIG)

# Packaging may not silently select a pre-guard or stale Recovery artifact.
# This host-side record binds the linked ELF/BIN/HEX and generated inputs;
# it does not add firmware bytes or replace actual-board qualification.
.PHONY: recovery-package-binding
all: recovery-package-binding
recovery-package-binding: $(BOOTLOADER_HEX) $(BOOTLOADER_BIN) $(BUILD_DIR)/$(TARGET).elf
	@python3 tools/sdk/nrf52832-recovery-binding.py record --repo . --sdk-root "$(SDK_ROOT)" \
		--build $(BUILD_DIR) --hex $(BOOTLOADER_HEX) --bin $(BOOTLOADER_BIN) \
		--elf $(BUILD_DIR)/$(TARGET).elf --nm "$(patsubst %gcc,%nm,$(CC))" \
		--same-mac $(INKTAG_NRF_RECOVERY_SAME_MAC) \
		--binding $(OUTPUT_DIR)/recovery-package-binding.json
