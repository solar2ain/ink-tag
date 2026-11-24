# Flash nrf52832_priceetag BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

The 512 KiB internal-flash image starts at 0.

### PriceEtag_V1.7

[BIN / HEX](programmer/PriceEtag_V1.7/) · [配置 / Configuration](tools/configs/PriceEtag_V1.7.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\PriceEtag_V1.7.toml"
```
