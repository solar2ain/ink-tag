# Flash nrf52811_aesl BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Use the Full ZIP with the matching board preset. The BAT installs the internal image, external settings and matching GUI resources. For a manual workflow, use [generate-hex.bat](tools/generate-hex.bat) to produce all three images. See the [address spaces and flashing order](tools/INITIALIZATION.en.md).

### EL022HWRL

[配置 / Configuration](tools/configs/EL022HWRL.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-nrf52811_aesl-full.zip" -Config ".\tools\configs\EL022HWRL.toml"
```

### EL022HWRL-A0

[配置 / Configuration](tools/configs/EL022HWRL-A0.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-nrf52811_aesl-full.zip" -Config ".\tools\configs\EL022HWRL-A0.toml"
```
