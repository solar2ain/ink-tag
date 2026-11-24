# Windows DA native producer, also callable from GNU Make under WSL.
ifeq ($(strip $(SDK_ROOT)),)
$(error SDK_ROOT must name the verified DA SDK root)
endif
ifeq ($(strip $(OUTPUT)),)
$(error OUTPUT must name a new explicit output directory)
endif

DA_BUILD_SCRIPT := $(FREE_REPO_ROOT)/firmware/ports/dialog/da14585/build.ps1
ifdef WSL_DISTRO_NAME
POWERSHELL ?= pwsh.exe
define da_windows_path
$$(wslpath -a -w "$(1)")
endef
else
POWERSHELL ?= pwsh
da_windows_path = $(1)
endif

.PHONY: all full
all full:
	"$(POWERSHELL)" -NoProfile -File "$(call da_windows_path,$(DA_BUILD_SCRIPT))" -Board "$(BUILD_TARGET)" -SdkRoot "$(call da_windows_path,$(SDK_ROOT))" -Output "$(call da_windows_path,$(OUTPUT))" $(if $(strip $(TOOLCHAIN_ROOT)),-KeilRoot "$(call da_windows_path,$(TOOLCHAIN_ROOT))")
