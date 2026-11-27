# Flash om6626_zkel BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

## First-time setup

On Windows, install [SEGGER J-Link/J-Flash 7.88j](https://www.segger.com/downloads/jlink/). Run the flashing tool with the included OM6626 device pack. See the [device pack source](tools/internal/device-pack/SOURCE.md) and [flash address map](ADDRESSES.md).

## Flashing

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Use the Full ZIP with the matching board preset. The BAT preserves the factory boot area and applies the selected settings.

### ZK_EL_OM21BA_V1.1

[配置 / Configuration](tools/configs/ZK_EL_OM21BA_V1.1.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM21BA_V1.1.toml"
```

### ZK_EL_OM42BA_V1.0

[配置 / Configuration](tools/configs/ZK_EL_OM42BA_V1.0.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM42BA_V1.0.toml"
```

### ZK_EL_OM6626_2.13_V1.1

[配置 / Configuration](tools/configs/ZK_EL_OM6626_2.13_V1.1.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-om6626_zkel-full.zip" -Config ".\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml"
```
