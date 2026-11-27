# 更新说明 / Changelog

## 0.2.2.1

### 新增 / Features

- WebUI 新增网页 NFC 和图片暖色模式，NFC 日历和时钟同步时间及本地时区。静态版和独立版支持 Find 加密备份离线导出。
- WebUI adds browser NFC and warm image colors. The NFC calendar and clock presets sync the time and local time zone. The static and standalone builds support offline export of encrypted Find backups.

### 修复与改进 / Fixes and improvements

- 改进手机端传图和图片裁剪，修复图片可完全拖出画板的问题。
- Improves image transfer and cropping on phones and fixes images being dragged entirely off the canvas.
- 改进 WebUI 的 Recovery 回连和 OTA 衔接，CLI 可校验 TLSR 四段固件版本。WebUI 和固件支持更多标准 SSD1680 尺寸，包括 152 × 296。
- Improves Recovery reconnection and OTA handoff in WebUI. The CLI verifies four-part TLSR firmware versions. WebUI and firmware support more standard SSD1680 sizes, including 152 × 296.
- 统一了固件文件名及发布目录。
- Standardized firmware filenames and release directories.

[本期发布说明 / Release notes](RELEASE_NOTES.md)

## 0.2.2

- 各平台固件的 BASIC 部分源码开源。新增创作点阵字体和图片亮度调节，改进设备更新流程。高级版新增 NFC 传图与蓝牙广播延时关闭功能。
- The BASIC firmware source is now open source for all supported platforms. This release adds bitmap fonts for creation and image brightness adjustment, and improves device updates. PRO adds NFC image transfer and delayed shutoff for ordinary Bluetooth advertising.
- [0.2.2 发布说明 / Release notes](https://github.com/solar2ain/ink-tag/releases/tag/v0.2.2-20261124.8)

## 0.2.1

- 高级版新增部分 4.2 寸三色屏的时钟局刷，支持 SSD1683、SSD1619／SSD1619A 和 UC8276，适用于兼容的 GR5513／GR5515、TLSR8359 和 nRF52832 主板。改进 Find 标签与轨迹管理，更新节气日期，修复日历排版、OTA 重连和本地客户端安装启动问题。
- PRO adds clock partial refresh on selected 4.2-inch three-color SSD1683, SSD1619/SSD1619A, and UC8276 panels on compatible GR5513/GR5515, TLSR8359, and nRF52832 boards. This release improves Find tag and track management, updates solar-term dates, and fixes calendar layout, OTA reconnection, and local client setup and startup.
- [0.2.1 发布说明 / Release notes](https://github.com/solar2ain/ink-tag/releases/tag/v0.2.1-20261107.1)

## 0.2.0

- 首版发布。
- Initial release.
