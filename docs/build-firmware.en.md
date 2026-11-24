# Build InkTag BASIC firmware

Shared firmware lives in `firmware/common/`. Each controller lives under `firmware/ports/<vendor>/<controller>/`. The constrained nRF52811 and DA14585 ports retain their verified adaptations in `compat/` and native build helpers in `build-support/`. Programmer sources live under `tools/platforms/<vendor>/<controller>/programmer/`; ready-to-use tools are under `release/firmware/`.

[简体中文](build-firmware.md)

Run the commands from the complete `ink-tag` source checkout. Prepare Git, Python 3, GNU Make, and the SDK and compiler for your target. The `make` examples use Linux or WSL. For DA14585, follow its Windows instructions. Substitute your own paths below. `TOOLCHAIN_ROOT` is the toolchain directory containing `bin/`. Use separate output directories for different targets. If you have the nRF52811 source archive, go directly to [nRF52811](#nordic-nrf52811).

[Exact release build parameters](../release/firmware/build-parameters.json) · [Preconverted firmware and board presets](../release/firmware/)

The release record preserves the commands used for the packaged artifacts. To build the current checkout, follow the steps for your controller below.

## Goodix GR5331 and GR5513/GR5515

Use [GR533x SDK](https://gitee.com/goodix-ble/GR533x.SDK) `v1.0.7_patch_1` or [GR551x SDK](https://gitee.com/goodix-ble/GR551x.SDK) `v2.1.1` with Arm GNU Toolchain 15.2.Rel1. For the standard GR551x target:

```sh
make BUILD_TARGET=gr551x_zkel \
  SDK_ROOT=/path/to/GR551x.SDK \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain-15.2.rel1 \
  OUTPUT=.local/builds/gr551x-zkel JOBS=4
```

Use `gr551x_zkel_internal` and a separate output directory for the Internal board. Use `gr5331_zkel` with the GR533x SDK for GR5331.

## OnMicro OM6626

Get the [OM6626 SDK mirror on Gitee](https://gitee.com/jtcom/OM6626) and Arm GNU Embedded **10.3-2021.10**. The root Makefile expects the SDK in `.sdk/onmicro/OM6626.SDK/`:

```sh
git clone https://gitee.com/jtcom/OM6626.git .sdk/onmicro/OM6626.SDK

make BUILD_TARGET=om6626_zkel \
  TOOLCHAIN_ROOT=/path/to/gcc-arm-none-eabi-10.3-2021.10 \
  OUTPUT=.local/builds/om6626-zkel JOBS=4
```

If Git download from Gitee prompts for authentication, use an account with access to the repository. You can also request the SDK using the [official OnMicro instructions](https://bbs.onmicro.com.cn/OM6626/381.html) and place it in the same directory.

The root command copies the built App and Recovery into `app/` and `recovery/`.

## Telink TLSR8359

Prepare the Telink workspace from [ATC_TLSR_Paper](https://github.com/atc1441/ATC_TLSR_Paper) and its TC32 toolchain. Point `SDK_ROOT` at the prepared `Firmware/` directory:

```sh
make BUILD_TARGET=tlsr8359_hsel \
  SDK_ROOT=/path/to/ATC_TLSR_Paper/Firmware \
  OUTPUT=.local/builds/tlsr8359-hsel JOBS=4
```

This entry compiles the native application. The [release build parameters](../release/firmware/build-parameters.json) record native packaging and Recovery targets.

## Nordic nRF52832

Use nRF5 SDK 17.1.0, S132, and Arm GNU for nRF52832. The root entry writes App and Recovery to separate `app/` and `recovery/` directories.

```sh
make BUILD_TARGET=nrf52832_priceetag \
  SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560 \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain \
  OUTPUT=.local/builds/nrf52832-priceetag JOBS=4
```

## Nordic nRF52811

Use **nRF5 SDK 17.1.0, S112 7.3.0, and Arm GNU Toolchain 15.2.Rel1**. Run from the source root:

```sh
make BUILD_TARGET=nrf52811_aesl \
  SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560 \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi \
  OUTPUT=.local/builds/nrf52811-aesl JOBS=4
```

- The App and font resource go into `firmware/crc-only/`, and Recovery into its `bootloader/` subdirectory, all under `OUTPUT`.
- For a complete flashing package, add the `full` target immediately after `make` and set `OUTPUT` to a location inside this checkout.
- App OTA requires a matching GUI resource already installed. For the first installation or to replace the GUI resource too, use the matching complete package. A complete flash rebuilds settings and invalidates the old images. Back up anything you need to keep.
- The port maintains its framebuffer, font cache, and storage adaptations. See `firmware/ports/nordic/nrf52811/BUILDING.md` for resource pairing and source archive instructions.

## Dialog DA14585

On Windows, prepare DA145xx SDK 6.0.24.1464, Keil Arm Compiler 6.24, and PowerShell 7. From the source root:

```powershell
pwsh -NoProfile -File .\firmware\ports\dialog\da14585\build.ps1 -Board da14585_aesl -SdkRoot "D:\InkTagSDK" -Output ".local\builds\da14585-aesl"
```

Point `-SdkRoot` at the prepared SDK workspace containing `dialog/DA145xx_SDK/6.0.24.1464/`. See `firmware/ports/dialog/da14585/BUILDING.md` for SDK preparation, build outputs, and addresses. Set `-KeilRoot` if Keil is installed outside its default location.

## Use the built firmware

BASIC builds need no signing key. OTA updates the App. For a first install, Recovery change, or paired GUI resource update, follow the [wired flashing guide](en/flash.md) for your exact board. Several board and screen presets can share a single App.
