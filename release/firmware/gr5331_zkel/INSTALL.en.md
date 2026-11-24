# Flash gr5331_zkel BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Read the MCU base address from flash-map.json.

### ZK_EL_GR5331_2.13_V1.1

[BIN / HEX](programmer/ZK_EL_GR5331_2.13_V1.1/) · [配置 / Configuration](tools/configs/ZK_EL_GR5331_2.13_V1.1.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\ZK_EL_GR5331_2.13_V1.1.toml"
```
