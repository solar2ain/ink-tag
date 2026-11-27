# nrf52811_aesl 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

使用 Full ZIP 和对应板型配置。BAT 会写入内部镜像、外置设置和配对的 GUI 资源。需要手工烧录时，可用 [generate-hex.bat](tools/generate-hex.bat) 生成整组镜像，按[地址空间和烧录顺序](tools/INITIALIZATION.md)操作。

### EL022HWRL

[配置 / Configuration](tools/configs/EL022HWRL.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-nrf52811_aesl-full.zip" -Config ".\tools\configs\EL022HWRL.toml"
```

### EL022HWRL-A0

[配置 / Configuration](tools/configs/EL022HWRL-A0.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-nrf52811_aesl-full.zip" -Config ".\tools\configs\EL022HWRL-A0.toml"
```
