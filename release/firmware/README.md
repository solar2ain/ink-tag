# InkTag 0.2.2.1 基础版固件 / BASIC firmware

[编译说明](https://github.com/solar2ain/ink-tag/blob/main/docs/build-firmware.md) · [Build guide](https://github.com/solar2ain/ink-tag/blob/main/docs/build-firmware.en.md) · [SHA-256](SHA256SUMS.txt) · [Firmware manifest](manifest.json) · [InkTag license](LICENSE) · [Goodix tools license](Goodix-tools-GPL-3.0.txt)

解压固件全包后，按主控和主板丝印打开下表对应的烧录说明。从 Git 检出的 `release/firmware/` 目录也可使用同一套材料。安装板型说明要求的烧录软件和驱动。DA14585 的 SRAM helper 随对应目标的烧录工具提供。

After extracting the firmware bundle, use the guide for your controller and PCB marking. The same files are under `release/firmware/` in a Git clone. Install the programmer software and drivers listed in your board guide. The DA14585 flashing tools include the matching SRAM helper.

每个目标目录的 OTA 在 `ota/`，完整固件在 `full/`，文件名为 `InkTag-<版级>-<版本>-<目标>-<类型>`。双击 `tools/flash-zh.bat` 开始，板型配置位于 `tools/configs/`。`tools/internal/` 由 BAT 自动调用。GR、TLSR 和 nRF52832 的 `flash-images/` 提供直接烧录用的镜像和地址表。OM6626 和 nRF52811 使用 Full ZIP，DA14585 使用 Full BIN，均搭配 `tools/configs/` 中的板型配置。

Each target directory contains OTA packages in `ota/` and Full firmware in `full/`. The filename includes the edition, version, target, and package type. Open `tools/flash.bat` to start. Board presets are in `tools/configs/`. The BAT calls the files in `tools/internal/` automatically. For GR, TLSR and nRF52832, `flash-images/` contains converted images and address maps for direct flashing. OM6626 and nRF52811 use the Full ZIP and DA14585 uses the Full BIN, each with a matching preset.

| 主控／主板 | 中文 | English |
| --- | --- | --- |
| `gr5331_zkel` | [烧录与版型](gr5331_zkel/INSTALL.md) | [Board guide](gr5331_zkel/INSTALL.en.md) |
| `gr551x_zkel` | [烧录与版型](gr551x_zkel/INSTALL.md) | [Board guide](gr551x_zkel/INSTALL.en.md) |
| `gr551x_zkel_internal` | [烧录与版型](gr551x_zkel_internal/INSTALL.md) | [Board guide](gr551x_zkel_internal/INSTALL.en.md) |
| `om6626_zkel` | [烧录与版型](om6626_zkel/INSTALL.md) | [Board guide](om6626_zkel/INSTALL.en.md) |
| `tlsr8359_hsel` | [烧录与版型](tlsr8359_hsel/INSTALL.md) | [Board guide](tlsr8359_hsel/INSTALL.en.md) |
| `nrf52832_priceetag` | [烧录与版型](nrf52832_priceetag/INSTALL.md) | [Board guide](nrf52832_priceetag/INSTALL.en.md) |
| `nrf52811_aesl` | [烧录与版型](nrf52811_aesl/INSTALL.md) | [Board guide](nrf52811_aesl/INSTALL.en.md) |
| `da14585_aesl` | [烧录与版型](da14585_aesl/INSTALL.md) | [Board guide](da14585_aesl/INSTALL.en.md) |
