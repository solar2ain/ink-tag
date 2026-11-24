.DEFAULT_GOAL := all
INKTAG_AUX_RESERVED_ENABLED ?= 0
ifneq ($(strip $(INKTAG_AUX_RESERVED_ENABLED)),0)
$(error This nRF52811 build requires INKTAG_AUX_RESERVED_ENABLED=0)
endif
override INKTAG_AUX_RESERVED_ENABLED := 0
.PHONY: all full nrf52811-aesl nrf52811-aesl-bootloader nrf52811-aesl-unsigned nrf52811-aesl-bootloader-unsigned nrf52811-aesl-ota print-build-profile
# Current Recovery is integrity-only; signing keys and policy switches are absent.
ifneq ($(origin INKTAG_NRF_REQUIRE_SIGNED_DFU),undefined)
$(error INKTAG_NRF_REQUIRE_SIGNED_DFU is retired; current Recovery is CRC-only)
endif
override NRF_SIGNATURE_PROFILE := crc-only
NRF_BUILD_ROOT ?= $(if $(BUILD_DIR),$(BUILD_DIR),.cache/build/nordic/nrf52811_aesl)
NRF_OUTPUT_ROOT ?= $(if $(OUTPUT_DIR),$(OUTPUT_DIR),_build/nordic/nrf52811_aesl)
override NRF_PROFILE_BUILD_DIR := $(NRF_BUILD_ROOT)/$(NRF_SIGNATURE_PROFILE)
override NRF_PROFILE_OUTPUT_DIR := $(NRF_OUTPUT_ROOT)/$(NRF_SIGNATURE_PROFILE)
all: nrf52811-aesl
full:
	python3 build.py $(if $(SDK_ROOT),--sdk-root "$(SDK_ROOT)") $(if $(TOOLCHAIN_PATH),--toolchain-path "$(TOOLCHAIN_PATH)") --build-root "$(NRF_BUILD_ROOT)" --output-root "$(NRF_OUTPUT_ROOT)"
nrf52811-aesl:
	$(MAKE) -f app/Makefile all BUILD_DIR="$(NRF_PROFILE_BUILD_DIR)" OUTPUT_DIR="$(NRF_PROFILE_OUTPUT_DIR)"
	$(MAKE) -f app/bootloader/Makefile all BUILD_DIR="$(NRF_PROFILE_BUILD_DIR)/bootloader" OUTPUT_DIR="$(NRF_PROFILE_OUTPUT_DIR)/bootloader"
nrf52811-aesl-bootloader:
	$(MAKE) -f app/bootloader/Makefile all BUILD_DIR="$(NRF_PROFILE_BUILD_DIR)/bootloader" OUTPUT_DIR="$(NRF_PROFILE_OUTPUT_DIR)/bootloader"
nrf52811-aesl-unsigned: nrf52811-aesl
nrf52811-aesl-bootloader-unsigned: nrf52811-aesl-bootloader
nrf52811-aesl-ota: nrf52811-aesl
	python3 build-support/ota/package-nrf52811.py --app "$(NRF_PROFILE_OUTPUT_DIR)/InkTag-nrf52811_aesl.bin" --output "$(NRF_PROFILE_OUTPUT_DIR)/InkTag-nrf52811_aesl-ota-unsigned.zip"
print-build-profile:
	@printf '%s\n' 'profile=crc-only' 'verificationPolicy=crc-sha256' 'recoveryVersion=$(shell python3 build-support/version/read-version.py --field recoveryVersion)' 'signatureRequired=0' 'auxReservedEnabled=$(INKTAG_AUX_RESERVED_ENABLED)' 'build=$(NRF_PROFILE_BUILD_DIR)' 'output=$(NRF_PROFILE_OUTPUT_DIR)' 'signing=no'
