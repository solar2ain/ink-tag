# Flash gr551x_zkel_internal BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Use the Internal board profile and its flash map.

### ZK_EL_GR_2.13_V1.2

[BIN / HEX](programmer/ZK_EL_GR_2.13_V1.2/) · [配置 / Configuration](tools/configs/ZK_EL_GR_2.13_V1.2.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\ZK_EL_GR_2.13_V1.2.toml"
```
