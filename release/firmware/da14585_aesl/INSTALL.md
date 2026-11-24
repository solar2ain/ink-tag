# da14585_aesl 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/programmer/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

外置 SPI Flash。使用稀疏 HEX 或分段 BIN，跳过 0x39000–0x3AFFF 的厂商区域。工具所需的外部加载器按随附说明自行准备。

### asel29

[BIN / HEX](programmer/asel29/) · [配置 / Configuration](tools/programmer/configs/asel29.toml)

```powershell
.\tools\programmer\flash-zh.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel29.toml"
```

### asel42

[BIN / HEX](programmer/asel42/) · [配置 / Configuration](tools/programmer/configs/asel42.toml)

```powershell
.\tools\programmer\flash-zh.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel42.toml"
```

### asel42-ssd1683

[BIN / HEX](programmer/asel42-ssd1683/) · [配置 / Configuration](tools/programmer/configs/asel42-ssd1683.toml)

```powershell
.\tools\programmer\flash-zh.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel42-ssd1683.toml"
```
