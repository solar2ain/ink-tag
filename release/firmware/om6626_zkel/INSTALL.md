# om6626_zkel 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

## 首次使用

在 Windows 上安装 [SEGGER J-Link/J-Flash 7.88j](https://www.segger.com/downloads/jlink/)。本目录自带 OM6626 设备支持文件，可直接运行烧录工具。见[文件出处](tools/internal/device-pack/SOURCE.md)和[烧录地址表](ADDRESSES.md)。

## 开始烧录

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

使用 Full ZIP 和对应板型配置完整烧录。BAT 会保留原厂启动区，再应用所选配置。

### ZK_EL_OM21BA_V1.1

[配置 / Configuration](tools/configs/ZK_EL_OM21BA_V1.1.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM21BA_V1.1.toml"
```

### ZK_EL_OM42BA_V1.0

[配置 / Configuration](tools/configs/ZK_EL_OM42BA_V1.0.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM42BA_V1.0.toml"
```

### ZK_EL_OM6626_2.13_V1.1

[配置 / Configuration](tools/configs/ZK_EL_OM6626_2.13_V1.1.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml"
```
