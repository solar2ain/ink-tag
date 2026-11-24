# nRF52811 固件编译 / nRF52811 firmware build

目标为 nRF52811 AESL（EL022HWRL）。在 Linux 或 WSL 中准备 GNU Make、Python 3、nRF5 SDK 17.1.0（S112 7.3.0）和 Arm GNU Toolchain 15.2.Rel1。完整仓库与独立源码包使用同一构建入口。请在源码根目录运行以下命令。

The nRF52811 AESL target (EL022HWRL) uses GNU Make, Python 3, nRF5 SDK 17.1.0 (S112 7.3.0), and Arm GNU Toolchain 15.2.Rel1 on Linux or WSL. Run this command at the root of the repository or the exported source tree.

~~~sh
make full BUILD_TARGET=nrf52811_aesl \
  SDK_ROOT=/path/to/nRF5_SDK_17.1.0_ddde560 \
  TOOLCHAIN_ROOT=/path/to/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi \
  OUTPUT=.local/builds/nrf52811-aesl JOBS=4
~~~

- 输出位于 `OUTPUT/firmware/crc-only/`，包括 App、Recovery、固定 GUI 资源以及配对的 Full 和 App OTA 包。仅编译固件时可省略 `full`。详见[固件编译](../../../../docs/build-firmware.md#nordic-nrf52811)。
- 无签名 App OTA 要求设备的 Recovery 报告协议版本 0xE1（CRC/SHA-256 完整性校验策略），并且设备已安装与 App 配对的 GUI 资源。首次安装或更新 GUI 资源时，使用配对的 Full 包和[烧录工具](../../../../tools/platforms/nordic/nrf52811/programmer/INITIALIZATION.md)。
- App, Recovery, fixed GUI resources, and the matched Full and App OTA packages are written under `OUTPUT/firmware/crc-only/`. Omit `full` to compile the firmware. See the [firmware build guide](../../../../docs/build-firmware.en.md#nordic-nrf52811).
- Unsigned App OTA requires an installed Recovery reporting protocol 0xE1 (CRC/SHA-256 integrity checks) and GUI resources matching the App. For first-time installation or a GUI resource update, use the matched Full package and the [programming tools](../../../../tools/platforms/nordic/nrf52811/programmer/INITIALIZATION.md).

使用以下命令将独立源码导出到尚不存在的目录。 / Run this command to export a standalone source tree to a new directory.

~~~sh
python3 firmware/ports/nordic/nrf52811/build-support/source/export-source.py /path/to/source-export
~~~

[依赖版本 / Dependency lock](build-support/sdk/sdk-lock.json) · [第三方许可 / Third-party notices](THIRD_PARTY_NOTICES.md)
