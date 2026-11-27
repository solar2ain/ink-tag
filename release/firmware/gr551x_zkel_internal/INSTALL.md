# gr551x_zkel_internal 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

内置 MCU Flash；使用 Internal 主板配置。

### ZK_EL_GR_2.13_V1.2

[BIN](flash-images/ZK_EL_GR_2.13_V1.2/) · [配置 / Configuration](tools/configs/ZK_EL_GR_2.13_V1.2.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr551x_zkel_internal-full.zip" -Config ".\tools\configs\ZK_EL_GR_2.13_V1.2.toml"
```
