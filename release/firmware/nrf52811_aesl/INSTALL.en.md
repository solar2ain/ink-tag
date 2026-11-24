# Flash nrf52811_aesl BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Internal flash, external settings and GUI use separate address spaces. Keep the App and GUI versions paired.

### EL022HWRL

[BIN / HEX](programmer/EL022HWRL/) · [配置 / Configuration](tools/native-configs/EL022HWRL.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-nrf52811_aesl-v0.2.2.0-self-built-full.zip" -Config ".\tools\native-configs\EL022HWRL.toml"
```

### EL022HWRL-A0

[BIN / HEX](programmer/EL022HWRL-A0/) · [配置 / Configuration](tools/native-configs/EL022HWRL-A0.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-nrf52811_aesl-v0.2.2.0-self-built-full.zip" -Config ".\tools\native-configs\EL022HWRL-A0.toml"
```
