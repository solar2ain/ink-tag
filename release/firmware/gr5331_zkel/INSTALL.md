# gr5331_zkel 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

内置 MCU Flash；从 flash-map.json 读取 BIN 起始地址。

### ZK_EL_GR5331_2.13_V1.1

[BIN](flash-images/ZK_EL_GR5331_2.13_V1.1/) · [配置 / Configuration](tools/configs/ZK_EL_GR5331_2.13_V1.1.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr5331_zkel-full.zip" -Config ".\tools\configs\ZK_EL_GR5331_2.13_V1.1.toml"
```
