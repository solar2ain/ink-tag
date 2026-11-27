# da14585_aesl 基础版烧录

[English](INSTALL.en.md)

首次安装或有线恢复使用 [Full BIN](full/)，无线升级使用 [OTA IMG](ota/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

需要 Windows、Python 3.12+、J-Link 7.88j。烧录工具自动使用随包的 SRAM helper。BAT 根据 Full BIN 和板型配置准备外置 SPI Flash 写入数据，跳过原厂区域 0x39000–0x3AFFF。需要离线生成分段 BIN 时，见[工具说明](tools/README.md)。

### asel29

[配置 / Configuration](tools/configs/asel29.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel29.toml"
```

### asel42

[配置 / Configuration](tools/configs/asel42.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel42.toml"
```

### asel42-ssd1683

[配置 / Configuration](tools/configs/asel42-ssd1683.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel42-ssd1683.toml"
```
