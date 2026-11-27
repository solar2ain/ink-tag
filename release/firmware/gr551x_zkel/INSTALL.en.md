# Flash gr551x_zkel BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

Use the flash-map.json for your MCU and flash capacity.

### ZK_EL_GR42V_V1.2

[BIN](flash-images/ZK_EL_GR42V_V1.2/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr551x_zkel-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2.toml"
```

### ZK_EL_GR42V_V1.2_w_NFC

[BIN](flash-images/ZK_EL_GR42V_V1.2_w_NFC/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2_w_NFC.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr551x_zkel-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2_w_NFC.toml"
```

### ZK_EL_GR42V_V1.2_w_NFC-SSD1683

[BIN](flash-images/ZK_EL_GR42V_V1.2_w_NFC-SSD1683/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2_w_NFC-SSD1683.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr551x_zkel-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2_w_NFC-SSD1683.toml"
```

### zkc42vm_eailier_version

[BIN](flash-images/zkc42vm_eailier_version/) · [配置 / Configuration](tools/configs/zkc42vm_eailier_version.toml)

```powershell
.\tools\flash.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-gr551x_zkel-full.zip" -Config ".\tools\configs\zkc42vm_eailier_version.toml"
```
