# Public independent base build. The internal layout is an explicit target.
AUX_RESERVED_ENABLED ?= 0
ifneq ($(AUX_RESERVED_ENABLED),0)
$(error Reserved AUX output fields are not implemented)
endif
REPO_ROOT ?= $(abspath ../../../..)
SDK_ROOT ?=
TOOLCHAIN_ROOT ?=
BUILD_TARGET ?= gr551x_zkel
OUTPUT ?= $(CURDIR)/build-base/$(BUILD_TARGET)
JOBS ?= 4
ifeq ($(strip $(SDK_ROOT)),)
$(error SDK_ROOT must name an owned external GR551x SDK workspace)
endif
ifeq ($(strip $(TOOLCHAIN_ROOT)),)
$(error TOOLCHAIN_ROOT must name the external ARM GNU toolchain root)
endif
ifeq ($(filter $(BUILD_TARGET),gr551x_zkel gr551x_zkel_internal),)
$(error BUILD_TARGET must be gr551x_zkel or gr551x_zkel_internal)
endif
.PHONY: all
all:
	python3 "$(REPO_ROOT)/tools/sdk/build-goodix-base.py" --build-target $(BUILD_TARGET) --sdk-root "$(SDK_ROOT)" --toolchain-root "$(TOOLCHAIN_ROOT)" --output "$(OUTPUT)" --jobs $(JOBS)
