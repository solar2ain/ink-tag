REPO_ROOT ?= ../../../..
TELINK_SDK ?= .
override INKTAG_BUILD_PRO := 0
ifeq ($(INKTAG_BUILD_PRO),0)
BUILD_DIR ?= $(REPO_ROOT)/.local/build/telink/tlsr8359/base/app
endif
BUILD_DIR ?= $(REPO_ROOT)/.local/build/telink/tlsr8359/app
ifneq ($(filter-out 0,$(INKTAG_AUX_RESERVED_ENABLED)),)
$(error TLSR maintained firmware requires INKTAG_AUX_RESERVED_ENABLED=0)
endif
override INKTAG_AUX_RESERVED_ENABLED := 0
TLSR_SOURCE_PROFILE := $(if $(filter 1,$(INKTAG_BUILD_PRO)),pro,base)
TLSR_PROFILE_DIR := $(BUILD_DIR)/generated/source-profile
TLSR_PROFILE_COMMON := $(TLSR_PROFILE_DIR)/common
# One public-only Find application/OTA artifact for all three Hanshow boards.
TLSR_OTA_PRODUCTION_KEY ?= 1
# Human-selected TLSR default for the three E6 recipes;0 restores PON waiting.
TLSR_OSPREY_E6_PON_NO_WAIT ?= 1
ifeq ($(filter $(TLSR_OSPREY_E6_PON_NO_WAIT),0 1),)
$(error TLSR_OSPREY_E6_PON_NO_WAIT must be 0 or 1)
endif
TLSR_OSPREY_E6_TEMPERATURE_READ ?= 1
ifeq ($(filter $(TLSR_OSPREY_E6_TEMPERATURE_READ),0 1),)
$(error TLSR_OSPREY_E6_TEMPERATURE_READ must be 0 or 1)
endif
# Human-selected universal default after the A0 trial, including production
# signing. Other panels still require their own evidence; explicit 2 rolls back.
TLSR_EXPERIMENTAL_SW_SPI_HALF_US ?= 1
ifeq ($(filter $(TLSR_EXPERIMENTAL_SW_SPI_HALF_US),1 2),)
$(error TLSR_EXPERIMENTAL_SW_SPI_HALF_US must be 1 or 2)
endif
# SSD1683 implementation is normal; optical diagnostic logging remains isolated.
TLSR_CLOCK_SSD1683 ?= 1
ifeq ($(filter $(TLSR_CLOCK_SSD1683),0 1),)
$(error TLSR_CLOCK_SSD1683 must be 0 or 1)
endif
TLSR_CLOCK_SSD1683_PILOT ?= 0
ifeq ($(filter $(TLSR_CLOCK_SSD1683_PILOT),0 1),)
$(error TLSR_CLOCK_SSD1683_PILOT must be 0 or 1)
endif
# Internal unsigned size oracle only; the maintained producer keeps default1.
TLSR_CLOCK_PARTIAL ?= 1
# Keep the historical native macro name; NFC image transfer is a normal Pro
# feature now. InkTag uses BLE LZO without any NFC implementation.
ifeq ($(INKTAG_BUILD_PRO),1)
TLSR_NFC_FMSC_PILOT ?= 1
TLSR_IMAGE_CODEC_PROFILE ?= 2
else
TLSR_NFC_FMSC_PILOT ?= 0
TLSR_IMAGE_CODEC_PROFILE ?= 2
endif
ifeq ($(filter $(TLSR_IMAGE_CODEC_PROFILE),0 1 2),)
$(error TLSR_IMAGE_CODEC_PROFILE must be 0 (RLE), 1 (both) or 2 (LZO))
endif
ifeq ($(filter $(TLSR_NFC_FMSC_PILOT),0 1),)
$(error TLSR_NFC_FMSC_PILOT must be 0 or 1)
endif
ifeq ($(filter $(TLSR_CLOCK_PARTIAL),0 1),)
$(error TLSR_CLOCK_PARTIAL must be 0 or 1)
endif
.DEFAULT_GOAL := compile-only

ifeq ($(OS),Windows_NT)
TC32_BIN := $(TELINK_SDK)/tc32_windows/bin
HOST_PYTHON ?= python
else
TC32_BIN := $(TELINK_SDK)/tc32_linux/bin
HOST_PYTHON ?= python3
endif

TLSR_PROFILE_READY := $(shell $(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/materialize-tlsr-source-profile.py --repo $(REPO_ROOT) --output $(TLSR_PROFILE_DIR) --profile $(TLSR_SOURCE_PROFILE) --make-result)
ifneq ($(TLSR_PROFILE_READY),INKTAG_PROFILE_OK)
$(error Explicit TLSR source profile materialization failed; no compiler or signing step admitted)
endif

PROJECT_NAME := InkTag-tlsr8359_hanshow
OUT_PATH := $(BUILD_DIR)/obj
BIN_FILE := $(BUILD_DIR)/$(PROJECT_NAME).bin
ELF_FILE := $(BUILD_DIR)/$(PROJECT_NAME).elf
LST_FILE := $(BUILD_DIR)/$(PROJECT_NAME).lst
RECOVERY_NAME := $(PROJECT_NAME)-recovery
RECOVERY_BIN_FILE := $(BUILD_DIR)/recovery/$(RECOVERY_NAME).bin
RECOVERY_ELF_FILE := $(BUILD_DIR)/recovery/$(RECOVERY_NAME).elf
RECOVERY_LST_FILE := $(BUILD_DIR)/recovery/$(RECOVERY_NAME).lst
GUI_ASSET_PACKAGE_FILE := $(BUILD_DIR)/$(PROJECT_NAME)-gui-assets.itfa

TEL_CHIP := -DCHIP_TYPE=CHIP_TYPE_8258
# The pinned TC32 compiler emits malformed indirect jump tables for some
# sparse/large switches (observed in both profile resolution and the v2 command
# dispatcher). Keep every project-owned object on explicit compare branches;
# this is a compiler correctness constraint, not a position-independence rule.
GCC_FLAGS := -include $(TLSR_PROFILE_DIR)/source-profile.h \
	-ffunction-sections -fdata-sections -fno-jump-tables -MMD -MP -Wall -Os -fpack-struct \
	-fshort-enums -std=gnu99 -funsigned-char \
	-fshort-wchar -fms-extensions -Wno-unused $(TEL_CHIP) \
	-DINKTAG_TELINK -DINKTAG_TELINK_TYPES_READY -DINKTAG_GUI_RAW_VIEWPORT \
	-DINKTAG_BUILD_PRO=$(INKTAG_BUILD_PRO) -DINKTAG_AUX_RESERVED_ENABLED=0 \
	-DINKTAG_CRC32_NIBBLE_TABLE=1 \
	-DINKTAG_NFC_RESOURCE_ALIAS_MAX_BYTES=35u \
	-DINKTAG_GUI_ATOMIC_FONT_ASSETS=1 \
	-DINKTAG_GUI_ROLE_FONT_ASSETS=1 -DINKTAG_GUI_NATIVE_FONT_ASSETS=1 \
	-DINKTAG_GUI_EMBEDDED_FONT_FALLBACK=1 \
	-DINKTAG_GUI_ASSET_READ_FAILURE_AWARE=1 \
	-DINKTAG_GUI_STATIC_BUFFER_ONLY -DINKTAG_GUI_COOPERATIVE_YIELD \
	-DINKTAG_EPD_ENABLE_UC8XXX=1 -DINKTAG_EPD_ENABLE_UC8159=1 \
	-DINKTAG_EPD_ENABLE_UC8179=1 -DINKTAG_EPD_ENABLE_SSD1XXX=1 \
	-DINKTAG_EPD_ENABLE_SSD1675=1 -DINKTAG_EPD_ENABLE_SSD1677=1 \
	-DINKTAG_EPD_ENABLE_SSD1683=1 -DINKTAG_EPD_ENABLE_JD79XXX=1 \
	-DINKTAG_EPD_ENABLE_JD79665CA=1 \
	-DINKTAG_EPD_ENABLE_OSPREY_E6=1 \
	-DINKTAG_EPD_ENABLE_HANSHOW_SMALL_Q=1 \
	-DINKTAG_EPD_ENABLE_HANSHOW_STELLAR_Q154=1 \
	-DINKTAG_EPD_ENABLE_HANSHOW_OLD_200=1 \
	-DINKTAG_EPD_ENABLE_STELLAR154=1 \
	-DINKTAG_ENABLE_SOAK_TELEMETRY=1 \
	-DINKTAG_SOAK_TELEMETRY_STORAGE=_attribute_data_retention_ \
	-D__HEAP_SIZE=4096 -include $(TELINK_SDK)/components/tl_common.h
GCC_FLAGS += -DINKTAG_TLSR_SW_SPI_HALF_US=$(TLSR_EXPERIMENTAL_SW_SPI_HALF_US)
RECOVERY_GCC_FLAGS := $(filter-out -DINKTAG_GUI_ROLE_FONT_ASSETS=1 -DINKTAG_GUI_NATIVE_FONT_ASSETS=1,$(GCC_FLAGS)) -DINKTAG_SHA256_ENABLE_STREAMING=1
GCC_FLAGS += -DINKTAG_GUI_FACTORY_FONT_LAYOUT=1
GCC_FLAGS += -DINKTAG_DEVICE_INFO_QR_SHARED_ASSET=1
GCC_FLAGS += -DINKTAG_TLSR_CLOCK_SSD1683=$(TLSR_CLOCK_SSD1683)
GCC_FLAGS += -DINKTAG_TLSR_CLOCK_SSD1683_PILOT=$(TLSR_CLOCK_SSD1683_PILOT)
GCC_FLAGS += -DINKTAG_TLSR_CLOCK_PARTIAL=$(TLSR_CLOCK_PARTIAL)
GCC_FLAGS += -DINKTAG_TLSR_NFC_FMSC_PILOT=$(TLSR_NFC_FMSC_PILOT)
ifeq ($(INKTAG_BUILD_PRO),1)
GCC_FLAGS += -DINKTAG_NFC_TIMED_DISPLAY=1 -DINKTAG_NFC_EXPLICIT_TIMEZONE=1
GCC_FLAGS += -DINKTAG_FIND_OWNER_UNREGISTER=1
endif
ifeq ($(INKTAG_BUILD_PRO),1)
GCC_FLAGS += -DINKTAG_NFC_R16_STREAMING=1
ifeq ($(TLSR_NFC_FMSC_PILOT),1)
GCC_FLAGS += -DINKTAG_EPD_TEMPERATURE_OPTION=1
endif
endif
GCC_FLAGS += -DINKTAG_TLSR_IMAGE_CODEC_PROFILE=$(TLSR_IMAGE_CODEC_PROFILE)
# All three TLSR clock LUT providers are GENERATED. Select the shared
# decode-only preimage; an accidental validate consumer must fail to link.
GCC_FLAGS += -DINKTAG_LZO1X_VALIDATE_ENABLED=0
GCC_FLAGS += -DINKTAG_TRANSFER_PACKBITS_ENABLED=$(if $(filter 2,$(TLSR_IMAGE_CODEC_PROFILE)),0,1)
ifneq ($(TLSR_IMAGE_CODEC_PROFILE),0)
GCC_FLAGS += -DINKTAG_TRANSFER_LZO1X_ENABLED=1
endif
GCC_FLAGS += -DINKTAG_EPD_OSPREY_E6_TEMPERATURE_READ=$(TLSR_OSPREY_E6_TEMPERATURE_READ)
# Exact TLSR round-panel pilot; common defaults must not change other ports.
GCC_FLAGS += -DINKTAG_EPD_OSPREY_E6_ROUND_SLEEP_RESET=1
ifeq ($(TLSR_OSPREY_E6_PON_NO_WAIT),1)
GCC_FLAGS += -DINKTAG_EPD_OSPREY_E6_PON_NO_WAIT=1
endif
ifeq ($(TLSR_OTA_PRODUCTION_KEY),1)
RECOVERY_GCC_FLAGS += -DINKTAG_TLSR_OTA_PRODUCTION_KEY=1 \
	-I$(BUILD_DIR)/generated
endif
INCLUDE_PATHS := \
	-I$(TLSR_PROFILE_DIR) \
	-I$(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/compat \
	-I$(TELINK_SDK)/components \
	-I$(REPO_ROOT)/firmware/boards \
	-I$(REPO_ROOT)/firmware/ports/telink/common/src \
	-I$(REPO_ROOT)/firmware/ports/telink/tlsr8359/src \
	-I$(TLSR_PROFILE_COMMON)/ble \
	-I$(TLSR_PROFILE_COMMON)/epd \
	-I$(TLSR_PROFILE_COMMON)/health \
	-I$(TLSR_PROFILE_COMMON)/input \
	-I$(TLSR_PROFILE_COMMON)/lifecycle \
	-I$(TLSR_PROFILE_COMMON)/ota \
	-I$(TLSR_PROFILE_COMMON)/power \
	-I$(TLSR_PROFILE_COMMON)/platform \
	-I$(TLSR_PROFILE_COMMON)/time \
	-I$(TLSR_PROFILE_COMMON)/protocol \
	-I$(TLSR_PROFILE_COMMON)/security \
	-I$(TLSR_PROFILE_COMMON)/storage \
	-I$(TLSR_PROFILE_COMMON)/status \
	-I$(TLSR_PROFILE_COMMON)/gui \
	-I$(TELINK_SDK)/components/vendor/common/mijia_ble/libs/third_party/micro-ecc \
	-I$(TELINK_SDK)/components/vendor/common/mijia_ble/mijia_ble_api
LIBS := -llt_8258
LS_FLAGS := $(TELINK_SDK)/static_src/boot.link
TC32_COMPILER_PATH := $(TC32_BIN)/
TEL_PATH := $(TELINK_SDK)

OBJ_SRCS :=
S_SRCS :=
ASM_SRCS :=
C_SRCS :=
S_UPPER_SRCS :=
O_SRCS :=
FLASH_IMAGE :=
ELFS :=
OBJS :=
LST :=
SIZEDUMMY :=
OUT_DIR :=

include $(TELINK_SDK)/make/application.mk
include $(TELINK_SDK)/make/common.mk
include $(TELINK_SDK)/make/vendor_common.mk
include $(TELINK_SDK)/make/tinyFlash.mk
include $(TELINK_SDK)/make/drivers_8258.mk
include $(TELINK_SDK)/static_src/div_mod.mk

# The pinned Windows Make/Cygwin pair merges double-quoted output/source
# arguments when the source contains an absolute drive path. Keep the SDK
# immutable and override its generated recipes here with POSIX single quotes.
# This permits a physical central SDK without worktree junctions.
define INKTAG_SDK_C_RULE
$(OUT_PATH)/$(1)/%.o: $(TEL_PATH)/components/$(2)/%.c
	@echo 'Building file: $$<'
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$$@' '$$<'
endef
$(eval $(call INKTAG_SDK_C_RULE,application,application))
$(eval $(call INKTAG_SDK_C_RULE,common,common))
$(eval $(call INKTAG_SDK_C_RULE,vendor/common,vendor/common))
$(eval $(call INKTAG_SDK_C_RULE,tinyFlash,tinyFlash))
$(eval $(call INKTAG_SDK_C_RULE,drivers/8258,drivers/8258))
$(OUT_PATH)/%.o: $(TEL_PATH)/components/boot/%.S
	@echo 'Building file: $<'
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(BOOT_FLAG) -c -o '$@' '$<'

SDK_OBJS := $(filter-out $(OUT_PATH)/drivers/8258/gpio_8258.o,$(OBJS))
PORT_SRCS := main.c compat.c app.c app_att.c inktag_ble.c inktag_app.c inktag_epd.c \
	inktag_storage.c inktag_settings.c inktag_tlsr_settings_backend.c inktag_tlsr_pin_routes.c \
	inktag_nfc.c inktag_nfc_link.c inktag_led.c inktag_tlsr_identity.c \
	inktag_tlsr_recovery.c inktag_tlsr_adc.c inktag_tlsr_board.c \
	inktag_tlsr_power.c inktag_feature_store.c inktag_tlsr_gui_assets.c inktag_tlsr_user_font_storage.c inktag_tlsr_gpio_driver.c
RECOVERY_SRCS := recovery_main.c recovery_ble.c recovery_att.c inktag_tlsr_recovery_pins.c
COMMON_SRCS := inktag_epd_screen.c inktag_epd_runtime.c inktag_epd_commands.c inktag_epd_uc.c inktag_epd_ssd.c inktag_epd_jd.c \
	inktag_page_cycle.c \
	Adafruit_GFX.c u8g2_font.c inktag_gui_fonts.c inktag_gui.c inktag_calendar.c \
	inktag_gui_asset_package.c inktag_gui_device_qr_asset.c
HEALTH_SRCS := inktag_soak_telemetry.c
INPUT_SRCS := inktag_input.c inktag_ndef.c inktag_nfc_command.c \
	inktag_type4_ndef.c inktag_nfc_binding.c inktag_input_simulation.c
STATUS_SRCS := inktag_status.c
STORAGE_SRCS := inktag_settings_records.c inktag_settings_journal.c
OTA_SRCS := inktag_recovery.c
LIFECYCLE_SRCS := inktag_lifecycle.c
POWER_SRCS := inktag_power.c
PROTOCOL_SRCS := inktag_protocol_core.c inktag_display_settings.c \
	inktag_image_upload_v5.c inktag_pin_config.c inktag_aux_io.c
ifneq ($(TLSR_IMAGE_CODEC_PROFILE),2)
PROTOCOL_SRCS += inktag_packbits.c
endif
ifeq ($(TLSR_NFC_FMSC_PILOT),1)
INPUT_SRCS += inktag_nfc_fmsc.c inktag_type4_apdu.c
endif
ifneq ($(TLSR_IMAGE_CODEC_PROFILE),0)
PROTOCOL_SRCS += inktag_lzo1x.c
endif
SECURITY_SRCS := inktag_activation.c inktag_sha256.c inktag_access.c \
                 inktag_device_password.c inktag_device_password_store.c \
	inktag_image_quota.c inktag_brand_overlay.c
BLE_SRCS := inktag_findmy.c inktag_findmy_public.c \
    inktag_ble_address.c inktag_ble_address_record.c inktag_ble_address_uid_compact.c
TELINK_COMMON_SRCS := inktag_tlsr_health.c inktag_tlsr_activation.c \
	inktag_tlsr_nfc_storage55.c \
	inktag_tlsr_int64.c inktag_tlsr_aux_io.c inktag_tlsr_clock.c \
	inktag_tlsr_p224_uecc.c inktag_tlsr_no_security.c
PORT_SRCS += inktag_tlsr_code_guard.c
ifeq ($(INKTAG_BUILD_PRO),0)
INPUT_SRCS := inktag_input.c
STORAGE_SRCS += inktag_resource_name.c
BLE_SRCS := inktag_ble_address.c inktag_ble_address_record.c inktag_ble_address_uid_compact.c
GCC_FLAGS := $(filter-out -DINKTAG_EPD_ENABLE_JD79665CA=1 -DINKTAG_EPD_ENABLE_OSPREY_E6=1,$(GCC_FLAGS)) -DINKTAG_EPD_ENABLE_JD79665CA=0 -DINKTAG_EPD_ENABLE_OSPREY_E6=0 -DINKTAG_PROTOCOL_BASE_ONLY_TARGET=1
RECOVERY_GCC_FLAGS += -DINKTAG_PROTOCOL_BASE_ONLY_TARGET=1
ifneq ($(TLSR_NFC_FMSC_PILOT),0)
$(error NFC pilot requires INKTAG_BUILD_PRO=1)
endif
endif

PORT_OBJS := $(addprefix $(OUT_PATH)/port/,$(PORT_SRCS:.c=.o))
TIME_OBJS := $(OUT_PATH)/common_time/inktag_clock_rate.o \
    $(OUT_PATH)/common_time/inktag_elapsed_clock.o
COMMON_OBJS := $(addprefix $(OUT_PATH)/common_inktag/,$(COMMON_SRCS:.c=.o))
HEALTH_OBJS := $(addprefix $(OUT_PATH)/common_health/,$(HEALTH_SRCS:.c=.o))
INPUT_OBJS := $(addprefix $(OUT_PATH)/common_input/,$(INPUT_SRCS:.c=.o))
STATUS_OBJS := $(addprefix $(OUT_PATH)/common_status/,$(STATUS_SRCS:.c=.o))
STORAGE_OBJS := $(addprefix $(OUT_PATH)/common_storage/,$(STORAGE_SRCS:.c=.o))
OTA_OBJS := $(addprefix $(OUT_PATH)/common_ota/,$(OTA_SRCS:.c=.o))
LIFECYCLE_OBJS := $(addprefix $(OUT_PATH)/common_lifecycle/,$(LIFECYCLE_SRCS:.c=.o))
POWER_OBJS := $(addprefix $(OUT_PATH)/common_power/,$(POWER_SRCS:.c=.o))
PROTOCOL_OBJS := $(addprefix $(OUT_PATH)/common_protocol/,$(PROTOCOL_SRCS:.c=.o))
SECURITY_OBJS := $(addprefix $(OUT_PATH)/common_security/,$(SECURITY_SRCS:.c=.o))
BLE_OBJS := $(addprefix $(OUT_PATH)/common_ble/,$(BLE_SRCS:.c=.o))
TELINK_COMMON_OBJS := $(addprefix $(OUT_PATH)/telink_common/,$(TELINK_COMMON_SRCS:.c=.o))
APP_OBJS := $(SDK_OBJS) $(PORT_OBJS) $(COMMON_OBJS) $(HEALTH_OBJS) $(INPUT_OBJS) \
	$(STATUS_OBJS) $(OTA_OBJS) $(LIFECYCLE_OBJS) $(POWER_OBJS) \
	$(PROTOCOL_OBJS) $(SECURITY_OBJS) $(STORAGE_OBJS) $(BLE_OBJS) $(TELINK_COMMON_OBJS) \
	$(TIME_OBJS) $(OUT_PATH)/cstartup_825x.o
RECOVERY_OTA_OBJ := $(OUT_PATH)/recovery/recovery_ota.o
RECOVERY_STORAGE_OBJS := $(filter-out $(OUT_PATH)/common_storage/inktag_settings_journal.o,$(STORAGE_OBJS)) \
    $(OUT_PATH)/recovery/inktag_settings_journal.o
RECOVERY_SRCS += inktag_tlsr_code_guard.c
RECOVERY_PORT_OBJS := $(addprefix $(OUT_PATH)/recovery/,$(RECOVERY_SRCS:.c=.o)) \
    $(RECOVERY_OTA_OBJ) $(OUT_PATH)/recovery/inktag_tlsr_power.o \
    $(OUT_PATH)/recovery/inktag_tlsr_recovery_adc_driver.o
RECOVERY_OBJS := $(filter-out $(OUT_PATH)/drivers/8258/adc.o,$(SDK_OBJS)) $(RECOVERY_PORT_OBJS) \
	$(OUT_PATH)/port/inktag_tlsr_gpio_driver.o \
	$(OUT_PATH)/port/inktag_tlsr_identity.o \
    $(OUT_PATH)/common_ble/inktag_ble_address.o \
    $(OUT_PATH)/common_ble/inktag_ble_address_record.o \
    $(OUT_PATH)/common_ble/inktag_ble_address_uid_compact.o \
    $(OUT_PATH)/common_protocol/inktag_protocol_core.o \
	$(OUT_PATH)/port/inktag_tlsr_recovery.o \
	$(OUT_PATH)/port/inktag_tlsr_adc.o \
	$(OUT_PATH)/port/inktag_tlsr_board.o \
    $(RECOVERY_STORAGE_OBJS) $(OUT_PATH)/common_protocol/inktag_pin_config.o \
    $(OTA_OBJS) $(POWER_OBJS) \
	$(OUT_PATH)/recovery/inktag_sha256.o \
	$(OUT_PATH)/telink_common/inktag_tlsr_clock.o \
	$(OUT_PATH)/telink_common/inktag_tlsr_no_security.o $(OUT_PATH)/cstartup_825x.o
OUT_DIR += /port /recovery /common_inktag /common_input /common_status /common_ota
PROJECT_OBJS := $(PORT_OBJS) $(COMMON_OBJS) $(HEALTH_OBJS) $(INPUT_OBJS) $(STATUS_OBJS) \
	$(OTA_OBJS) $(LIFECYCLE_OBJS) $(POWER_OBJS) $(PROTOCOL_OBJS) \
	$(SECURITY_OBJS) $(STORAGE_OBJS) $(BLE_OBJS) $(TELINK_COMMON_OBJS) $(RECOVERY_PORT_OBJS)
PROJECT_OBJS += $(TIME_OBJS)
OUT_DIR += /common_lifecycle /common_power /common_protocol /common_security \
	/common_ble /common_health /telink_common

OUT_DIR += /common_storage
OUT_DIR += /common_time

-include $(PROJECT_OBJS:.o=.d)
-include $(RECOVERY_STORAGE_OBJS:.o=.d)

# The imported SDK make fragments do not emit dependency files for their
# driver objects.  gpio_init() and several radio/power helpers compile the
# board defaults from app_config.h, so reusing an older SDK object here can
# silently restore stale pins after deep-retention wake.  Treat the project
# configuration and this Makefile as explicit inputs without requiring a
# clean build.
$(SDK_OBJS): $(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/app_config.h \
	$(REPO_ROOT)/firmware/ports/telink/tlsr8359/Makefile \
	$(REPO_ROOT)/firmware/ports/telink/common/src/inktag_tlsr_flash_layout.h

.PHONY: all application check-sdk pre-build clean size package
compile-only: check-sdk pre-build $(ELF_FILE) $(BIN_FILE) $(LST_FILE) \
	$(RECOVERY_ELF_FILE) $(RECOVERY_BIN_FILE) $(RECOVERY_LST_FILE) $(GUI_ASSET_PACKAGE_FILE)
.PHONY: compile-only
recovery-only: check-sdk pre-build $(RECOVERY_ELF_FILE) $(RECOVERY_BIN_FILE) $(RECOVERY_LST_FILE)
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role recovery --elf $(RECOVERY_ELF_FILE) --bin $(RECOVERY_BIN_FILE) --source-profile base
.PHONY: recovery-only
all: check-sdk pre-build $(ELF_FILE) $(BIN_FILE) $(LST_FILE) \
	$(RECOVERY_ELF_FILE) $(RECOVERY_BIN_FILE) $(RECOVERY_LST_FILE) size package

application: check-sdk pre-build $(ELF_FILE) $(BIN_FILE) $(LST_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-size -t '$(ELF_FILE)'

check-sdk:
	@test -f "$(TELINK_SDK)/components/proj_lib/liblt_8258.a" || \
		(echo "TLSR SDK missing: run tools/sdk/setup-sdk.ps1 -Platform telink" && exit 2)
	@test -f "$(TC32_BIN)/tc32-elf-gcc$(if $(filter Windows_NT,$(OS)),.exe,)" || \
		(echo "Pinned TC32 compiler missing under $(TC32_BIN)" && exit 2)

pre-build:
	@$(HOST_PYTHON) -c "from pathlib import Path; [Path(p).mkdir(parents=True,exist_ok=True) for p in r'$(sort $(dir $(APP_OBJS) $(RECOVERY_OBJS) $(STAGE0_OBJS)) $(BUILD_DIR)/recovery)'.split()]"

$(OUT_PATH)/port/%.o: $(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/%.c
	@echo "Building $<"
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/port/inktag_app.o: \
	$(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/inktag_protocol_v2.inc

$(OUT_PATH)/common_time/%.o: $(TLSR_PROFILE_COMMON)/time/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_time/inktag_elapsed_clock.o: $(TLSR_PROFILE_COMMON)/platform/inktag_elapsed_clock.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/recovery/%.o: $(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/%.c
	@mkdir -p "$(@D)"
	@echo "Building Recovery $<"
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(RECOVERY_GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/recovery/inktag_tlsr_power.o: $(REPO_ROOT)/firmware/ports/telink/tlsr8359/src/inktag_tlsr_power.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(RECOVERY_GCC_FLAGS) -DINKTAG_TLSR_RECOVERY_POWER_PROFILE=1 $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/recovery/inktag_sha256.o: \
	$(TLSR_PROFILE_COMMON)/security/inktag_sha256.c
	@echo "Building Recovery $<"
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(RECOVERY_GCC_FLAGS) $(INCLUDE_PATHS) \
		-c -o '$@' '$<'

# Recovery only reads the App journal. Validate and skip future opaque record
# types without enabling them in the App's writable/compacting backend.
$(OUT_PATH)/recovery/inktag_settings_journal.o: $(TLSR_PROFILE_COMMON)/storage/inktag_settings_journal.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(RECOVERY_GCC_FLAGS) \
		-DINKTAG_SETTINGS_JOURNAL_READONLY_FUTURE_RECORDS=1 $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_epd_screen.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_screen.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_epd_runtime.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_runtime.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_epd_commands.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_commands.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_page_cycle.o: $(TLSR_PROFILE_COMMON)/epd/inktag_page_cycle.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_epd_uc.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_uc.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

# The E6 PON option changes inktag_epd_uc.o; bind its value for both directions.
.PHONY: force-osprey-e6-config
force-osprey-e6-config:
$(BUILD_DIR)/generated/osprey-e6-pon.cfg: force-osprey-e6-config
	@mkdir -p "$(BUILD_DIR)/generated"
	@$(HOST_PYTHON) -c "from pathlib import Path; p=Path(r'$@'); v='$(TLSR_OSPREY_E6_PON_NO_WAIT)/$(TLSR_OSPREY_E6_TEMPERATURE_READ)/round-reset1'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
$(OUT_PATH)/common_inktag/inktag_epd_uc.o: $(BUILD_DIR)/generated/osprey-e6-pon.cfg
$(OUT_PATH)/port/inktag_epd.o: $(BUILD_DIR)/generated/osprey-e6-pon.cfg

$(OUT_PATH)/common_inktag/inktag_epd_ssd.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_ssd.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/inktag_epd_jd.o: $(TLSR_PROFILE_COMMON)/epd/inktag_epd_jd.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_inktag/%.o: $(TLSR_PROFILE_COMMON)/gui/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_input/%.o: $(TLSR_PROFILE_COMMON)/input/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_status/%.o: $(TLSR_PROFILE_COMMON)/status/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_ota/%.o: $(TLSR_PROFILE_COMMON)/ota/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_lifecycle/%.o: $(TLSR_PROFILE_COMMON)/lifecycle/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_power/%.o: $(TLSR_PROFILE_COMMON)/power/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_protocol/%.o: $(TLSR_PROFILE_COMMON)/protocol/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_security/%.o: $(TLSR_PROFILE_COMMON)/security/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_storage/%.o: $(TLSR_PROFILE_COMMON)/storage/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_ble/%.o: $(TLSR_PROFILE_COMMON)/ble/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/common_health/%.o: $(TLSR_PROFILE_COMMON)/health/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) \
		-include $(TELINK_SDK)/components/drivers/8258/compiler.h \
		$(INCLUDE_PATHS) -c -o '$@' '$<'

$(OUT_PATH)/telink_common/%.o: $(REPO_ROOT)/firmware/ports/telink/common/src/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'

# Existing SDK-derived build trees predate generated dependency files. A
# Makefile change rebuilds project-owned objects once; subsequent header
# changes are tracked by the included .d files above.
$(PROJECT_OBJS) $(RECOVERY_OBJS): $(REPO_ROOT)/firmware/ports/telink/tlsr8359/Makefile

$(OUT_PATH)/cstartup_825x.o: $(TELINK_SDK)/static_src/cstartup_825x.S
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -DMCU_STARTUP_825X -c -o '$@' '$<'

$(ELF_FILE): $(APP_OBJS) $(LS_FLAGS) $(REPO_ROOT)/firmware/ports/telink/tlsr8359/Makefile
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections -L$(TELINK_SDK)/components/proj_lib \
		-T$(LS_FLAGS) -o '$@' $(APP_OBJS) $(LIBS)

$(BIN_FILE): $(ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-objcopy -O binary '$<' '$@'
	@$(HOST_PYTHON) $(TELINK_SDK)/make/tl_firmware_tools.py add_crc '$@'

$(RECOVERY_ELF_FILE): $(RECOVERY_OBJS) $(LS_FLAGS) $(REPO_ROOT)/firmware/ports/telink/tlsr8359/Makefile
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections --undefined=inktag_tlsr_recovery_version_info -L$(TELINK_SDK)/components/proj_lib \
		-T$(LS_FLAGS) -o '$@' $(RECOVERY_OBJS) $(LIBS)

$(RECOVERY_BIN_FILE): $(RECOVERY_ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-objcopy -O binary '$<' '$@'
	@$(HOST_PYTHON) $(TELINK_SDK)/make/tl_firmware_tools.py add_crc '$@'

$(LST_FILE): $(ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-objdump -x -D -l -S '$<' > '$@'

$(RECOVERY_LST_FILE): $(RECOVERY_ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-objdump -x -D -l -S '$<' > '$@'

size: $(ELF_FILE) $(RECOVERY_ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-size -t '$<'
	@$(TC32_COMPILER_PATH)tc32-elf-size -t '$(RECOVERY_ELF_FILE)'

$(GUI_ASSET_PACKAGE_FILE): \
	$(TLSR_PROFILE_COMMON)/gui/inktag_gui_fonts.c \
	$(REPO_ROOT)/tools/sdk/make-gui-asset-package.py \
	$(REPO_ROOT)/firmware/factory/native-font-template.json
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/make-gui-asset-package.py \
		--fonts "$(TLSR_PROFILE_COMMON)/gui/inktag_gui_fonts.c" \
		--output '$@' --generation 1

package: package-base

clean:
	@/bin/rm -rf "$(BUILD_DIR)"

# Paired trusted-entry profile is explicit until its device qualification.
TLSR_TRUSTED_ENTRY ?= 1
ifeq ($(TLSR_TRUSTED_ENTRY),1)
include $(REPO_ROOT)/firmware/ports/telink/tlsr8359/trusted-entry/production.mk
else ifneq ($(TLSR_TRUSTED_ENTRY),0)
$(error TLSR_TRUSTED_ENTRY must be0 or1)
endif

# A direct Make invocation may reuse a directory. Never reuse objects compiled
# for the other entry/link profile merely because their source timestamp fits.
.PHONY: force-entry-profile
force-entry-profile:
$(BUILD_DIR)/generated/entry-layout.cfg: force-entry-profile
	@mkdir -p "$(BUILD_DIR)/generated"
	@$(HOST_PYTHON) -c "from pathlib import Path; p=Path(r'$@'); v='$(TLSR_TRUSTED_ENTRY)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
$(APP_OBJS) $(RECOVERY_OBJS) $(STAGE0_OBJS): $(BUILD_DIR)/generated/entry-layout.cfg

# Switching the optical profile must rebuild App objects in either direction;
# a compiler-flag change alone is not a Make timestamp dependency.
.PHONY: force-clock-ssd1683-profile
force-clock-ssd1683-profile:
$(BUILD_DIR)/generated/clock-ssd1683-profile.cfg: force-clock-ssd1683-profile
	@mkdir -p "$(BUILD_DIR)/generated"
	@$(HOST_PYTHON) -c "from pathlib import Path; p=Path(r'$@'); v='$(TLSR_CLOCK_SSD1683),$(TLSR_CLOCK_SSD1683_PILOT),$(TLSR_CLOCK_PARTIAL)'; p.write_text(v) if not p.exists() or p.read_text()!=v else None"
$(APP_OBJS): $(BUILD_DIR)/generated/clock-ssd1683-profile.cfg

# Materialization happens before Make scans depfiles. Its profile/defines
# stamp invalidates a reused directory, while changed source bytes rebuild
# only their actual dependents.
# Old depfiles name the former common C path as their primary prerequisite.
# Admit that stale name only when the explicit profile materialized its real
# replacement. Recompilation emits the new dependency; no placeholder source,
# clean, deleted evidence, or probing for a private directory is needed.
TLSR_LEGACY_COMMON_C := $(shell $(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/materialize-tlsr-source-profile.py --repo $(REPO_ROOT) --profile $(TLSR_SOURCE_PROFILE) --legacy-c-targets)
$(addprefix $(REPO_ROOT)/,$(TLSR_LEGACY_COMMON_C)):
	@test -f '$(TLSR_PROFILE_COMMON)/$(patsubst firmware/common/%,%,$(patsubst $(REPO_ROOT)/firmware/common/%,%,$@))'
$(APP_OBJS) $(RECOVERY_OBJS) $(STAGE0_OBJS): $(TLSR_PROFILE_DIR)/source-profile.cfg

# Direct compile-only targets and parallel fresh builds need their directories
# before any compiler runs. Order-only prerequisites preserve warm objects.
$(APP_OBJS) $(RECOVERY_OBJS) $(STAGE0_OBJS) $(GUI_ASSET_PACKAGE_FILE): | pre-build check-sdk



ifeq ($(INKTAG_BUILD_PRO),0)
BASE_PACKAGE_OUTPUT ?= $(REPO_ROOT)/_build/telink
.PHONY: package-base
package-base: compile-only
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/make-tlsr-base-images.py --application '$(BIN_FILE)' --recovery '$(RECOVERY_BIN_FILE)' --stage0 '$(STAGE0_BIN_FILE)' --output '$(BASE_PACKAGE_OUTPUT)'
endif
