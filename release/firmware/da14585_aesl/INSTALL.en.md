# Flash da14585_aesl BASIC

[简体中文](INSTALL.md)

Use the [Full BIN](full/) for first installation and wired recovery, and the [OTA IMG](ota/) for wireless updates. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

On Windows, install Python 3.12+ and J-Link 7.88j. The flashing tool uses the included SRAM helper. The BAT uses the Full BIN and board preset to prepare writes to external SPI flash, skipping the factory range 0x39000–0x3AFFF. See the [tool guide](tools/README.md) to generate BIN segments offline.

### asel29

[配置 / Configuration](tools/configs/asel29.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel29.toml"
```

### asel42

[配置 / Configuration](tools/configs/asel42.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel42.toml"
```

### asel42-ssd1683

[配置 / Configuration](tools/configs/asel42-ssd1683.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-da14585_aesl-full.bin" -Config ".\tools\configs\asel42-ssd1683.toml"
```
