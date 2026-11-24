# Public independent base build. SDK and compiler are external dependencies.
AUX_RESERVED_ENABLED ?= 0
ifneq ($(AUX_RESERVED_ENABLED),0)
$(error Reserved AUX output fields are not implemented)
endif
REPO_ROOT ?= $(abspath ../../../..)
SDK_ROOT ?=
TOOLCHAIN_ROOT ?=
OUTPUT ?= $(CURDIR)/build-base/gr5331_zkel
JOBS ?= 4
ifeq ($(strip $(SDK_ROOT)),)
$(error SDK_ROOT must name an owned external GR533x SDK workspace)
endif
ifeq ($(strip $(TOOLCHAIN_ROOT)),)
$(error TOOLCHAIN_ROOT must name the external ARM GNU toolchain root)
endif
.PHONY: all
all:
	python3 "$(REPO_ROOT)/tools/sdk/build-goodix-base.py" --build-target gr5331_zkel --sdk-root "$(SDK_ROOT)" --toolchain-root "$(TOOLCHAIN_ROOT)" --output "$(OUTPUT)" --jobs $(JOBS)
