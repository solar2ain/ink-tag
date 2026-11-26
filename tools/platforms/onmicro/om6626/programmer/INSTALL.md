# om6626_zkel 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](../../../../../release/firmware/om6626_zkel/ota/)，首次安装或有线恢复使用[完整包](../../../../../release/firmware/om6626_zkel/full/)。完整烧录前请备份所需资料。

在 Windows 上安装 [SEGGER J-Link/J-Flash 7.88j](https://www.segger.com/downloads/jlink/)。本目录自带 OM6626 设备支持文件，可直接运行烧录工具。见[文件出处](payload/device-pack/SOURCE.md)。

双击 [flash-zh.bat](payload/tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

各版型共用同家族 App 与 Recovery 分段。完整烧录须使用 BAT 保留原厂启动区，并应用选择的板型配置。

### ZK_EL_OM21BA_V1.1

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM21BA_V1.1/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM21BA_V1.1.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash-zh.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM21BA_V1.1.toml"
```

### ZK_EL_OM42BA_V1.0

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM42BA_V1.0/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM42BA_V1.0.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash-zh.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM42BA_V1.0.toml"
```

### ZK_EL_OM6626_2.13_V1.1

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM6626_2.13_V1.1/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM6626_2.13_V1.1.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash-zh.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml"
```
