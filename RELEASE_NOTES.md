# InkTag 0.2.2.1

这是 0.2.2 的维护更新。图片处理、网页 NFC 和设备更新流程均有改进。

## 新增功能

- **网页 NFC。** 电脑可通过 Web Serial 和 PN532／PCR532 读卡器，向支持 NFC 的高级版设备发送图片和命令。支持 Web NFC 的安卓浏览器可发送文本命令。网页和小程序的 NFC 日历、时钟快捷命令会同步当前时间和时区。
- **图片暖色模式。** 大面积蓝绿的图片在三色、四色屏上更容易保留色彩，处理时尽量保留人物肤色。
- **Find 本机备份。** 静态版和独立版 WebUI 在开启 Find 前先保存加密备份，之后可在当前浏览器离线导出。微信小程序也可以在本机管理和导出备份。

## 修复与改进

- 修复手机端裁剪时图片可完全拖出画板的问题。
- WebUI 与固件支持更多标准 SSD1680 屏幕尺寸，包括 152 × 296。请按实际屏幕选择颜色模式。
- 修复 WebUI 进入 Recovery 后的 OTA 回连问题。CLI 在 TLSR OTA 后可校验完整的四段固件版本。
- 小程序版本与 WebUI 对齐为 0.2.2.1，保留普通模板和本地固件包升级，天气入口与在线 OTA 已移除。

## 固件与安装

统一了固件发布目录和命名。文件名包含版级、版本、目标和包类型。每个目标的 `ota/` 用于无线更新，`full/` 用于完整安装，`tools/` 放烧录脚本和板型配置。支持直接烧录的目标另有 `flash-images/`。GR 和 TLSR 提供 BIN 和地址表，nRF52832 提供 Intel HEX。OM6626、nRF52811 使用 Full ZIP，DA14585 使用 Full BIN。

解压 `InkTag-BASIC-0.2.2.1-firmware.zip` 后按主板丝印打开目标的 `INSTALL.md`。克隆仓库也能从 `release/firmware/` 使用同一套文件。OM6626 随包提供 J-Link 设备文件，DA14585 的烧录脚本自动加载并校验随包的 SRAM helper。高级版固件可在 InkTag QQ 群 **418446983** 获取。 [高级版说明](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/pro.md#获取高级版固件) · [烧录说明](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/flash.md) · [使用手册](https://solar2ain.github.io/ink-tag-manual/)

## English

### Features

- **NFC in WebUI.** Use a PN532 or PCR532 reader over Web Serial on a computer to send images and commands to compatible PRO devices. Android browsers with Web NFC can send text commands. The WebUI and mini program NFC calendar and clock presets sync the current time and time zone.
- **Warm colors for images.** Improves color on three- and four-color screens for images with large blue or green areas while aiming to preserve skin tones.
- **Local Find backups.** The static and standalone WebUI save an encrypted backup before enabling Find. You can export it later from the same browser while offline. The WeChat mini program can also manage and export backups on the phone.

### Fixes and improvements

- Fixes images being dragged entirely off the canvas when cropping on phones.
- WebUI and firmware support more standard SSD1680 panel sizes, including 152 × 296. Select the palette that matches your panel.
- Fixes OTA reconnection after entering Recovery in WebUI. The CLI verifies the full four-part version after a TLSR OTA update.
- The mini program now reports version 0.2.2.1, matching WebUI. It keeps standard templates and updates from local firmware files. Weather and online OTA are no longer available in the mini program.

### Firmware and installation

Firmware filenames and release directories are standardized. Filenames show the edition, version, target, and package type. Each target has `ota/` for wireless updates, `full/` for complete installation, and `tools/` for flashers and board presets. Where available, `flash-images/` contains ready-to-flash images. GR and TLSR provide BIN images with address maps. nRF52832 provides Intel HEX. OM6626 and nRF52811 use the Full ZIP, and DA14585 uses the Full BIN.

Extract `InkTag-BASIC-0.2.2.1-firmware.zip` and open the target's `INSTALL.en.md` for your PCB marking. A Git clone includes the same files under `release/firmware/`. OM6626 includes the J-Link device files, and the DA14585 flasher automatically loads and verifies the bundled SRAM helper. Obtain PRO firmware from the InkTag QQ group **418446983**. [PRO guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/pro.md#get-pro-firmware) · [Flashing guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/flash.md) · [User manual](https://solar2ain.github.io/ink-tag-manual/en/index.html)
