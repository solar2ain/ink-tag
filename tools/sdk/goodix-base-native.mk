# base-only native compilation: no Pro producer, signing or clean target.
SOURCE_PROFILE := base
REPO_ROOT ?= $(abspath ../../../..)
SDK_ROOT ?=
TOOLCHAIN_PATH ?=
PORT ?=
BOARD ?=
INTERNAL_IMAGES ?= 0
ifeq ($(strip $(SDK_ROOT)),)
$(error SDK_ROOT must be an explicit owned external SDK workspace)
endif
ifeq ($(strip $(TOOLCHAIN_PATH)),)
$(error TOOLCHAIN_PATH must be an explicit external compiler bin directory)
endif
ifeq ($(filter $(PORT),gr5331 gr551x),)
$(error PORT must be gr5331 or gr551x)
endif
APP_ARGS := --source-profile base
BOOT_ARGS := --source-profile common
ifeq ($(PORT),gr5331)
PLATFORM := gr5331
APP_ARGS += --sdk-literal-fault-log
else
PLATFORM := gr5515
ifeq ($(INTERNAL_IMAGES),1)
APP_ARGS += --internal-images --sdk-literal-fault-log
BOOT_ARGS += --internal-images
else ifneq ($(INTERNAL_IMAGES),0)
$(error INTERNAL_IMAGES must be 0 or 1)
endif
endif
APP_PROJECT := $(SDK_ROOT)/projects/ble/ble_peripheral/ble_app_inktag_ota/GCC
BOOT_PROJECT := $(SDK_ROOT)/projects/ble/dfu/app_bootloader_inktag/GCC
.NOTPARALLEL:
.PHONY: all materialize app bootloader
all: app bootloader
materialize:
	python3 "$(REPO_ROOT)/tools/sdk/materialize-goodix-port.py" $(PLATFORM) --board $(BOARD) --repo "$(REPO_ROOT)" --sdk-root "$(SDK_ROOT)" $(APP_ARGS)
	python3 "$(REPO_ROOT)/tools/sdk/materialize-goodix-$(PORT)-bootloader.py" --board $(BOARD) --repo "$(REPO_ROOT)" --sdk-root "$(SDK_ROOT)" $(BOOT_ARGS)
app: materialize
	PATH="$(TOOLCHAIN_PATH):$$PATH" $(MAKE) -C "$(APP_PROJECT)"
bootloader: materialize
	PATH="$(TOOLCHAIN_PATH):$$PATH" $(MAKE) -C "$(BOOT_PROJECT)"
