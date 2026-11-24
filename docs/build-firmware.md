# 构建 InkTag BASIC 固件

共用固件在 `firmware/common/`，各主控在 `firmware/ports/<厂商>/<主控>/`。nRF52811 和 DA14585 的 `compat/` 保存已验证的容量适配，`build-support/` 提供各自原生构建脚本。烧录工具源码统一在 `tools/platforms/<厂商>/<主控>/programmer/`，可直接使用的烧录包见 `release/firmware/`。

[English](build-firmware.en.md)

从完整的 `ink-tag` 源码根目录执行。准备 Git、Python 3、GNU Make，以及目标 MCU 对应的 SDK 和编译器。下方 `make` 示例按 Linux／WSL 环境编写，DA14585 使用其 Windows 步骤。示例路径请替换为本机路径。`TOOLCHAIN_ROOT` 指包含 `bin/` 的工具链根目录。每个目标使用自己的构建输出目录。仅有 nRF52811 源码包时，直接使用 [nRF52811](#nordic-nrf52811) 一节。

| 主控 | 基础版目标 |
| --- | --- |
| Goodix GR5331 | `gr5331_zkel` |
| Goodix GR5513／GR5515 | `gr551x_zkel`、`gr551x_zkel_internal` |
| OnMicro OM6626 | `om6626_zkel` |
| Telink TLSR8359 | `tlsr8359_hsel` |
| Nordic nRF52832 | `nrf52832_priceetag` |
| Nordic nRF52811 | `nrf52811_aesl` |
| Dialog DA14585 | `da14585_aesl` |

[本次交付使用的实际编译参数](../release/firmware/build-parameters.json) · [已转换的固件和主板配置](../release/firmware/)

交付参数保留原始构建命令。用当前源码重新编译时，按下方对应平台的步骤运行。

## Goodix GR5331、GR5513／GR5515

准备 [GR533x SDK](https://gitee.com/goodix-ble/GR533x.SDK) 的 `v1.0.7_patch_1` 或 [GR551x SDK](https://gitee.com/goodix-ble/GR551x.SDK) 的 `v2.1.1`，以及 Arm GNU Toolchain 15.2.Rel1。以 GR551x 标准版为例：

```sh
make BUILD_TARGET=gr551x_zkel \
  SDK_ROOT=/path/to/GR551x.SDK \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain-15.2.rel1 \
  OUTPUT=.local/builds/gr551x-zkel JOBS=4
```

Internal 版使用同一个 GR551x SDK，改用 `gr551x_zkel_internal` 和独立输出目录。GR5331 使用 `gr5331_zkel` 和 GR533x SDK。按目标输出及配套工具分别选择 App、Recovery 和对应板型的有线材料。

## OnMicro OM6626

从 [Gitee 的 OM6626 SDK 镜像](https://gitee.com/jtcom/OM6626)获取 SDK，并准备 Arm GNU Embedded **10.3-2021.10**。根 Makefile 从 `.sdk/onmicro/OM6626.SDK/` 读取 SDK，例如：

```sh
git clone https://gitee.com/jtcom/OM6626.git .sdk/onmicro/OM6626.SDK

make BUILD_TARGET=om6626_zkel \
  TOOLCHAIN_ROOT=/path/to/gcc-arm-none-eabi-10.3-2021.10 \
  OUTPUT=.local/builds/om6626-zkel JOBS=4
```

Gitee 的 Git 下载若提示登录，需使用有仓库访问权限的账号。也可按[昂瑞微官方 SDK 获取说明](https://bbs.onmicro.com.cn/OM6626/381.html)申请，将取得的 SDK 放到同一目录。

App 和 Recovery 分别输出到 `app/` 和 `recovery/`。

## Telink TLSR8359

准备 [ATC_TLSR_Paper](https://github.com/atc1441/ATC_TLSR_Paper) 的 Telink 工程及配套 TC32 工具链。将 `SDK_ROOT` 指向准备好的 `Firmware/` 工程根目录，使工具链可用，然后运行：

```sh
make BUILD_TARGET=tlsr8359_hsel \
  SDK_ROOT=/path/to/ATC_TLSR_Paper/Firmware \
  OUTPUT=.local/builds/tlsr8359-hsel JOBS=4
```

此入口执行本地编译。需要原生 OTA 或 Recovery 时，使用[交付参数](../release/firmware/build-parameters.json)中的对应目标。

## Nordic nRF52832

nRF52832 使用 nRF5 SDK 17.1.0、S132 和 Arm GNU Toolchain。应用和 Recovery 分别输出到 `app/`、`recovery/`。

```sh
make BUILD_TARGET=nrf52832_priceetag \
  SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560 \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain \
  OUTPUT=.local/builds/nrf52832-priceetag JOBS=4
```

## Nordic nRF52811

使用 **nRF5 SDK 17.1.0、S112 7.3.0 和 Arm GNU Toolchain 15.2.Rel1**。从源码根目录执行：

```sh
make BUILD_TARGET=nrf52811_aesl \
  SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560 \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi \
  OUTPUT=.local/builds/nrf52811-aesl JOBS=4
```

- App 和字体资源输出到 `firmware/crc-only/`，Recovery 在其 `bootloader/` 子目录。以上路径均相对于 `OUTPUT`。
- 需要生成完整烧录包时，在上面的 `make` 后加上 `full`，其余参数相同，`OUTPUT` 必须在源码目录内。
- App OTA 要求设备已装有配对的 GUI 资源。首次安装或 GUI 也需更新时，使用对应的完整包。有线全刷会重建设备设置并使旧图片失效，先备份所需内容。
- 屏幕缓冲、字体缓存和存储适配由端口维护。资源配对和独立源码包的构建步骤见 `firmware/ports/nordic/nrf52811/BUILDING.md`。

## Dialog DA14585

在 Windows 准备 DA145xx SDK 6.0.24.1464、Keil Arm Compiler 6.24 和 PowerShell 7。从源码根目录运行：

```powershell
pwsh -NoProfile -File .\firmware\ports\dialog\da14585\build.ps1 -Board da14585_aesl -SdkRoot "D:\InkTagSDK" -Output ".local\builds\da14585-aesl"
```

`-SdkRoot` 指向准备好的 SDK 工作区根目录，其下应有 `dialog/DA145xx_SDK/6.0.24.1464/`。SDK 工作区准备、构建产物和地址说明见 `firmware/ports/dialog/da14585/BUILDING.md`。Keil 使用非默认安装位置时可指定 `-KeilRoot`。

## 使用构建结果

基础版构建无需签名密钥。OTA 只更新 App；首次安装、更换 Recovery 或更新配对 GUI 资源，使用对应版型的完整包和[有线烧录说明](zh-CN/flash.md)。同一 App 可能对应多份板型或屏幕配置，烧录前按丝印和存储地址选择。
