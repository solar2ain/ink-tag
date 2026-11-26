# Flash om6626_zkel BASIC

[简体中文](INSTALL.md)

Use the [OTA package](../../../../../release/firmware/om6626_zkel/ota/) for wireless updates and the [complete package](../../../../../release/firmware/om6626_zkel/full/) for first installation and wired recovery. Back up what you need first.

On Windows, install [SEGGER J-Link/J-Flash 7.88j](https://www.segger.com/downloads/jlink/). Run the flashing tool with the included OM6626 device pack. See the [device pack source](payload/device-pack/SOURCE.md).

Run [flash.bat](payload/tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

The variants share one family-level App and Recovery pair. Use the Full tool to preserve vendor boot data and apply the correct board settings.

### ZK_EL_OM21BA_V1.1

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM21BA_V1.1/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM21BA_V1.1.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM21BA_V1.1.toml"
```

### ZK_EL_OM42BA_V1.0

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM42BA_V1.0/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM42BA_V1.0.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM42BA_V1.0.toml"
```

### ZK_EL_OM6626_2.13_V1.1

[BIN / HEX](../../../../../release/firmware/om6626_zkel/programmer/ZK_EL_OM6626_2.13_V1.1/) · [配置 / Configuration](payload/tools/configs/ZK_EL_OM6626_2.13_V1.1.toml)
 · [App / Recovery segments](../../../../../release/firmware/om6626_zkel/programmer/segments/)

```powershell
.\payload\tools\flash.bat -Firmware "..\..\..\..\..\release\firmware\om6626_zkel\full\app-full.zip" -Config ".\payload\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml"
```
