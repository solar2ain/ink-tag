.DEFAULT_GOAL := all
.PHONY: all build full host-tests
POWERSHELL ?= pwsh
SDK_ROOT ?= $(CURDIR)/.sdk

all: build
build:
	"$(POWERSHELL)" -NoProfile -File build.ps1 -SdkRoot "$(SDK_ROOT)"

full:
	"$(POWERSHELL)" -NoProfile -File build.ps1 -SdkRoot "$(SDK_ROOT)"

host-tests:
	python build-support/build/run_host_tests.py --sanitize
