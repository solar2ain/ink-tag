# nrf52832_priceetag 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

内置 Flash 512 KiB，地址 0x00000。

### PriceEtag_V1.7

[BIN / HEX](programmer/PriceEtag_V1.7/) · [配置 / Configuration](tools/configs/PriceEtag_V1.7.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\PriceEtag_V1.7.toml"
```
