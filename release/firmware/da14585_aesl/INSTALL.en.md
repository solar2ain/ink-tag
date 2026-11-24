# Flash da14585_aesl BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/programmer/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

The sparse HEX and BIN segments target external SPI flash. Keep the vendor range 0x39000–0x3AFFF intact and obtain the loader per the tool instructions.

### asel29

[BIN / HEX](programmer/asel29/) · [配置 / Configuration](tools/programmer/configs/asel29.toml)

```powershell
.\tools\programmer\flash.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel29.toml"
```

### asel42

[BIN / HEX](programmer/asel42/) · [配置 / Configuration](tools/programmer/configs/asel42.toml)

```powershell
.\tools\programmer\flash.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel42.toml"
```

### asel42-ssd1683

[BIN / HEX](programmer/asel42-ssd1683/) · [配置 / Configuration](tools/programmer/configs/asel42-ssd1683.toml)

```powershell
.\tools\programmer\flash.bat -Firmware ".\full\InkTag-da14585_aesl-full.bin" -Config ".\tools\programmer\configs\asel42-ssd1683.toml"
```
