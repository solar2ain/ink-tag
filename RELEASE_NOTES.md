# InkTag 0.2.2

基础版（BASIC）的固件源码、WebUI 和 CLI 随本版提供。高级版固件请通过 [InkTag QQ 群 418446983 获取](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/pro.md#获取高级版固件)。

## 基础版与客户端

- **自己编译和安装**。按主控和主板选择 BASIC 固件。发布材料提供[固件构建说明](https://github.com/solar2ain/ink-tag/blob/main/docs/build-firmware.md)、OTA 包、完整烧录包、预转换的 BIN／Intel HEX，以及 Windows 烧录入口。
- **基础版固件体积**。同一主控和主板上，基础版的 App 比高级版小约 30–61 KiB。不同屏幕、布局和存储配置仍需使用配套文件。
- **按实际容量保存图片**。图槽数量随屏幕和可用存储而定。设备信息页用 `BASIC` 和 `PRO` 标识档位。
- **创作字体**。小屏默认用 10 px 缝合像素字体，大屏默认用 12 px Z 工坊像素黑体。新增原生点阵字号，改善课表中加粗汉字的间距。创作的设备效果预览支持窗口适配、1:1 和细步进缩放。[字体指南](https://github.com/solar2ain/ink-tag/blob/main/docs/creation-fonts.md)
- **图片与升级**。WebUI 和 CLI 可调整图片亮度。WebUI 支持下载发布渠道中与设备匹配的 OTA 包，并在页面中升级。
- **使用体验**。改善日历和有线刷机提示。双击 `flash.bat` 或 `flash-zh.bat` 可按提示操作。

## 高级版

- **NFC 传图与校时**。指定 Goodix、OnMicro 和 Telink 主板搭配支持的 NFC 硬件和应用，可用手机传图。D3-Pro InkTag 版在 InkTag QQ 群获取，用法见 [NFC 指南](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/nfc.md)。NFC 校时可同步时区，单独校时保留当前画面。
- **蓝牙广播方式**。支持的设备可在断开连接后延时关闭普通蓝牙广播。用受支持的按键或 NFC 恢复广播。已启用的 Find My 广播会继续工作。
- **Find 身份管理**。验证原身份后可解除设备上的登记，同时保留本机资料。打开地图时保持页面滚动位置。
- **日历和时区**。改进设备当地午夜换日和重启后保留时区的处理。

安装前核对主控、主板丝印、屏幕和存储配置。需要配套屏幕资源的设备请按对应版型的[有线烧录说明](https://github.com/solar2ain/ink-tag/blob/main/docs/zh-CN/flash.md)更新。

---

## BASIC and shared clients

- **Build and install.** BASIC firmware, WebUI and CLI source ship with 0.2.2. Use the [firmware build guide](https://github.com/solar2ain/ink-tag/blob/main/docs/build-firmware.en.md) and choose the OTA, complete installation, BIN or Intel HEX file for your exact board. Windows users can run `flash.bat` for guided setup.
- **Firmware size.** On the seven targets with both editions, the BASIC App uses about 30–61 KiB less flash than the PRO App.
- **Image storage.** Available capacity determines the number of pictures you can store. The device information page shows `BASIC` or `PRO`.
- **Creation fonts.** Small canvases default to 10 px Fusion Pixel and larger canvases to 12 px Z Labs Pixel CN. Native bitmap sizes and better spacing improve small text and timetable headings. The device-effect preview fits the available space and offers smaller zoom steps. [Font guide](https://github.com/solar2ain/ink-tag/blob/main/docs/creation-fonts.en.md)
- **Images and updates.** Adjust brightness in WebUI or CLI. WebUI can download and install a compatible OTA package from the published update channel.

## PRO

Obtain PRO firmware through the [InkTag QQ group 418446983](https://github.com/solar2ain/ink-tag/blob/main/docs/en/pro.md#get-pro-firmware).

- **NFC image transfer and time sync.** Available with supported Goodix, OnMicro and Telink boards and compatible hardware and apps. Get D3-Pro InkTag through the QQ group. Time sync can set the time zone and preserves the current screen. [NFC guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/nfc.md)
- **Bluetooth advertising.** Supported devices can stop ordinary advertising after a configurable delay. An available button or NFC restores it. Enabled Find My advertising continues.
- **Find registration.** Verify the current identity to deregister it from the device while retaining local data. Opening the map preserves the page's scroll position.
- **Calendar and time zone.** Improved local-midnight rollover and time-zone retention across restarts.

Check the controller, board markings, display and storage configuration first. Follow the [wired flashing guide](https://github.com/solar2ain/ink-tag/blob/main/docs/en/flash.md) for devices that require matching display resources.

Nordic 完整包中的 S112 和 S132 随附 [Nordic SoftDevice 许可](https://github.com/solar2ain/ink-tag/blob/main/release/firmware/Nordic-SoftDevice-LICENSE.txt)。 / Nordic Full packages include S112 or S132 under the [Nordic SoftDevice license](https://github.com/solar2ain/ink-tag/blob/main/release/firmware/Nordic-SoftDevice-LICENSE.txt).
