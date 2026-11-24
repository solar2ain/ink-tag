# InkTag 固件与烧录工具

中文 | [English](README.en.md)

请从 [固件交付目录](../release/firmware/) 按主控和主板家族选择目录。各目录的 `ota/` 是无线更新文件，`full/` 是首次安装和有线恢复文件；不要混用。

- `gr5331_zkel`
- `gr551x_zkel`
- `gr551x_zkel_internal`
- `om6626_zkel`
- `tlsr8359_hsel`
- `nrf52832_priceetag`
- `nrf52811_aesl`
- `da14585_aesl`

本仓库同时保存已归集的固件，GitHub Releases 提供相同字节的下载附件。文件大小、SHA256与准确主板以 `manifest.json` 为准；目录存在不代表兼容任意同主控设备。

每个目录的中英文 README 列出设备名、主板丝印、配置及完整的一行烧录命令。统一目录与使用方法见 [完整烧录说明](../docs/zh-CN/flash.md)。工具不捆绑某一版固件，仍通过 `-Firmware` 显式传入。

## 源码目录

板级定义在 `firmware/boards/`，跨平台实现位于 `firmware/common/`，主控移植位于 `firmware/ports/`。nRF52811 和 DA14585 的端口各有一份 `BUILDING.md`，各自的容量适配和原生构建脚本保存在 `compat/` 和 `build-support/`。用户安装与使用说明统一由仓库主文档维护。[固件编译指南](../docs/build-firmware.md)
