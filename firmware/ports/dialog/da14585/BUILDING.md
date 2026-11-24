# DA14585 固件编译 / DA14585 firmware build

盒马 AESL-2.9 和 AESL-4.2 共用 `da14585_aesl` 目标。Windows 需要 PowerShell 7、Python 3.12 和带有效许可的 Keil µVision、Arm Compiler 6.24。Renesas/Dialog DA145xx SDK 6.0.24.1464 需单独取得。普通构建和打包使用 Python 标准库。

AESL-2.9 and AESL-4.2 use the `da14585_aesl` target. On Windows, install PowerShell 7, Python 3.12, and licensed Keil µVision with Arm Compiler 6.24. Obtain Renesas/Dialog DA145xx SDK 6.0.24.1464 separately. The normal build and packager use the Python standard library.

## 构建 / Build

在端口目录内执行。独立源码导出包从解压后的根目录执行。首次用原始 SDK 创建库存，已有库存时从验证步骤开始。

Run from this port directory or the root of an extracted DA14585 source snapshot. Import the vendor SDK once, then verify the existing inventory before subsequent builds.

```powershell
python build-support/sdk/da_sdk_workspace.py preserve-base --source 'D:/SDK/DA145xx_SDK/6.0.24.1464' --sdk-root 'D:/SDK/InkTag-DA'
python build-support/sdk/da_sdk_workspace.py verify --sdk-root 'D:/SDK/InkTag-DA'
pwsh -NoProfile -File .\build.ps1 -Board da14585_aesl -SdkRoot 'D:/SDK/InkTag-DA' -Output '.local/builds/da14585-aesl'
```

`-SdkRoot` 指向已准备的绝对容器路径，其内包含 `dialog/DA145xx_SDK/6.0.24.1464` 和 `da6-base.json`。每次指定一个新的输出目录。Keil 安装在默认的 `%LOCALAPPDATA%\Keil_v5` 以外时，加 `-KeilRoot` 指定安装路径。

`-SdkRoot` points to the prepared absolute container path, which holds `dialog/DA145xx_SDK/6.0.24.1464` and `da6-base.json`. Specify a new output directory for each run. Set `-KeilRoot` if Keil is installed outside `%LOCALAPPDATA%\Keil_v5`.

## 构建产物 / Output

`packages/` 下包含未签名的 App OTA `InkTag-da14585_aesl-ota.img`、Recovery `InkTag-da14585_aesl-recovery.img`、含启动加载器和 GUI 资源的 256 KiB Full 模板 `InkTag-da14585_aesl-full.bin`，以及记录配对和校验值的 `package-manifest.json`。App OTA 只更新应用。首次安装、更新 Recovery 或更新 GUI 资源时，按对应主板的烧录说明选用匹配的完整包。旧设备的 OTA 兼容性取决于设备实际回报的 Recovery 策略。详见 [OTA 格式与兼容性](protocol/da-ota.md)。

The `packages/` directory contains the unsigned App OTA `InkTag-da14585_aesl-ota.img`, the Recovery image `InkTag-da14585_aesl-recovery.img`, the 256 KiB Full template with bootloader and GUI assets `InkTag-da14585_aesl-full.bin`, and `package-manifest.json` with component bindings and hashes. An App OTA updates only the App. Use the matching Full package and board instructions for a first installation or a Recovery or GUI resource update. OTA compatibility on older devices depends on the policy reported by the installed Recovery. See [OTA format and compatibility](protocol/da-ota.md).

DA14585 从外置 SPI Flash 加载应用到 SRAM 运行。构建根目录中的 App HEX 使用 SRAM 地址 `0x07FC0000`，仅对应原生构建输出。完整烧录使用适配主板的 SPI Flash 文件及其地址清单。

The DA14585 loads the App from external SPI flash into SRAM. The build's App HEX uses SRAM address `0x07FC0000`. For full flashing, use the board-specific SPI flash files with their address manifest.

## 离线检查 / Offline checks

C 测试使用 GCC 或 WSL GCC。Python 回归使用 pytest。节气回归在 WSL 中直接运行，使用同一环境的 C 编译器。

C checks use GCC or WSL GCC. The Python regressions use pytest. Run the solar-term check in WSL with its C compiler.

```powershell
python build-support/build/run_host_tests.py --sanitize
python -m pytest tests -q --ignore=tests/test_solar_terms.py
wsl --exec python3 -B tests/test_solar_terms.py
```

[第三方依赖与资源 / Third-party dependencies and resources](THIRD_PARTY_NOTICES.md) · [节气资源生成 / Solar-term resource generation](build-support/calendar/README.md)
