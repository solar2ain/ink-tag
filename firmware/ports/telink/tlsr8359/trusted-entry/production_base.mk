# Explicit internal native producer profile; not a user-flashing mode.
TRUSTED_PATH := $(REPO_ROOT)/firmware/ports/telink/tlsr8359/trusted-entry
LS_FLAGS := $(TRUSTED_PATH)/role.link
TLSR_ENTRY_PACKAGE_ARGS := --layout layout-v1 --stage0 "$(BUILD_DIR)/stage0.bin"
GCC_FLAGS += -DINKTAG_CODE_GUARDS=0 -DINKTAG_SHA256_ENABLE_STREAMING=1
RECOVERY_GCC_FLAGS += -DINKTAG_CODE_GUARDS=0 -DINKTAG_CODE_RECOVERY=1
GCC_FLAGS += -include $(TRUSTED_PATH)/profile.h
RECOVERY_GCC_FLAGS += -include $(TRUSTED_PATH)/profile.h
TRUSTED_DIR := $(OUT_PATH)/trusted
STAGE0_BIN_FILE := $(BUILD_DIR)/stage0.bin
STAGE0_ELF_FILE := $(BUILD_DIR)/stage0.elf
STAGE0_OBJS := $(TRUSTED_DIR)/stage0.o $(TRUSTED_DIR)/native_load.o
APP_OBJS := $(filter-out $(OUT_PATH)/cstartup_825x.o,$(APP_OBJS)) $(TRUSTED_DIR)/app_startup.o $(TRUSTED_DIR)/address_binding.o
RECOVERY_OBJS := $(filter-out $(OUT_PATH)/cstartup_825x.o,$(RECOVERY_OBJS)) \
 $(TRUSTED_DIR)/recovery_startup.o $(TRUSTED_DIR)/boot.o \
 $(TRUSTED_DIR)/image_verify.o $(TRUSTED_DIR)/native_blob.o $(TRUSTED_DIR)/address_binding.o
PROJECT_OBJS += $(STAGE0_OBJS) $(TRUSTED_DIR)/boot.o $(TRUSTED_DIR)/image_verify.o
OUT_DIR += /trusted

$(TRUSTED_DIR)/%.o: $(TRUSTED_PATH)/%.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc $(RECOVERY_GCC_FLAGS) $(INCLUDE_PATHS) -c -o '$@' '$<'
$(TRUSTED_DIR)/native_load.o: $(TRUSTED_PATH)/native_load.c
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -Os -fno-jump-tables -ffunction-sections -fdata-sections -fno-builtin -std=gnu99 -c -o '$@' '$<'
$(TRUSTED_DIR)/stage0.o: $(TRUSTED_PATH)/stage0.S $(TRUSTED_PATH)/native_memory.h
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -c -o '$@' '$<'
$(STAGE0_ELF_FILE): $(STAGE0_OBJS) $(TRUSTED_PATH)/stage0.link
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections -T$(TRUSTED_PATH)/stage0.link $(STAGE0_OBJS) -o '$@'
$(STAGE0_BIN_FILE): $(STAGE0_ELF_FILE)
	@$(TC32_COMPILER_PATH)tc32-elf-objcopy -O binary '$<' '$@'
	@$(HOST_PYTHON) $(TELINK_SDK)/make/tl_firmware_tools.py add_crc '$@'
$(BUILD_DIR)/native_load.elf: $(TRUSTED_DIR)/native_load.o $(TRUSTED_PATH)/native_load.link
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections -T$(TRUSTED_PATH)/native_load.link '$<' -o '$@'
$(BUILD_DIR)/native_load.bin: $(BUILD_DIR)/native_load.elf
	@$(TC32_COMPILER_PATH)tc32-elf-objcopy -O binary '$<' '$@'
$(TRUSTED_DIR)/native_blob.o: $(TRUSTED_PATH)/native_blob.S $(BUILD_DIR)/native_load.bin
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -DNATIVE_LOAD_BINARY='"$(BUILD_DIR)/native_load.bin"' -c -o '$@' '$<'
$(BUILD_DIR)/generated/trusted-%-startup.S: $(TELINK_SDK)/static_src/cstartup_825x.S $(REPO_ROOT)/tools/sdk/generate-tlsr-trusted-startup.py
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/generate-tlsr-trusted-startup.py --input '$<' --output '$@' --role '$*'
$(TRUSTED_DIR)/app_startup.o: $(BUILD_DIR)/generated/trusted-app-startup.S
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -DMCU_STARTUP_825X -c -o '$@' '$<'
$(TRUSTED_DIR)/recovery_startup.o: $(BUILD_DIR)/generated/trusted-recovery-startup.S
	@$(TC32_COMPILER_PATH)tc32-elf-gcc -DMCU_STARTUP_825X -c -o '$@' '$<'

# These recipes replace SDK alternate mapping with bounded physical linking.
$(ELF_FILE): $(APP_OBJS) $(BUILD_DIR)/generated/native-app.link
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections -L$(TELINK_SDK)/components/proj_lib --defsym=ROLE_BASE=0x1000 --defsym=ROLE_CAPACITY=0x3f000 --defsym=ROLE_SIGNATURE_BYTES=96 --defsym=ROLE_STACK_END=0x850000 -T$(BUILD_DIR)/generated/native-app.link -o '$@' $(APP_OBJS) $(LIBS)
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role app --elf '$@' --source-profile $(TLSR_SOURCE_PROFILE)
$(RECOVERY_ELF_FILE): $(RECOVERY_OBJS) $(BUILD_DIR)/generated/native-recovery.link
	@$(TC32_COMPILER_PATH)tc32-elf-ld --gc-sections --undefined=inktag_tlsr_recovery_version_info -L$(TELINK_SDK)/components/proj_lib --defsym=ROLE_BASE=0x40000 --defsym=ROLE_CAPACITY=0x11000 --defsym=ROLE_SIGNATURE_BYTES=0 --defsym=ROLE_STACK_END=0x84f000 -T$(BUILD_DIR)/generated/native-recovery.link -o '$@' $(RECOVERY_OBJS) $(LIBS)
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role recovery --elf '$@' --source-profile $(TLSR_SOURCE_PROFILE)
all: $(STAGE0_BIN_FILE)
compile-only: $(STAGE0_BIN_FILE)
package: $(STAGE0_BIN_FILE)
-include $(TRUSTED_DIR)/boot.d $(TRUSTED_DIR)/image_verify.d $(TRUSTED_DIR)/address_binding.d
$(APP_OBJS) $(RECOVERY_OBJS) $(STAGE0_OBJS): $(TRUSTED_PATH)/profile.h $(TRUSTED_PATH)/production.mk $(TRUSTED_PATH)/production_base.mk

$(BUILD_DIR)/generated/native-%.link: $(TRUSTED_PATH)/role.link $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role $* --linker-input '$<' --output '$@'
.PHONY: inktag-native-verify
all: inktag-native-verify
compile-only: inktag-native-verify
inktag-native-verify: $(BIN_FILE) $(RECOVERY_BIN_FILE)
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role app --elf $(ELF_FILE) --bin $(BIN_FILE) --source-profile base
	@$(HOST_PYTHON) $(REPO_ROOT)/tools/sdk/tlsr_native_integrity.py --role recovery --elf $(RECOVERY_ELF_FILE) --bin $(RECOVERY_BIN_FILE) --source-profile base
