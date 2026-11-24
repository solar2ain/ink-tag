# nrf52811_aesl 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

内置镜像、外置设置和 GUI 分开写入。App 与 GUI 必须配对。

### EL022HWRL

[BIN / HEX](programmer/EL022HWRL/) · [配置 / Configuration](tools/native-configs/EL022HWRL.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-nrf52811_aesl-v0.2.2.0-self-built-full.zip" -Config ".\tools\native-configs\EL022HWRL.toml"
```

### EL022HWRL-A0

[BIN / HEX](programmer/EL022HWRL-A0/) · [配置 / Configuration](tools/native-configs/EL022HWRL-A0.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-nrf52811_aesl-v0.2.2.0-self-built-full.zip" -Config ".\tools\native-configs\EL022HWRL-A0.toml"
```
