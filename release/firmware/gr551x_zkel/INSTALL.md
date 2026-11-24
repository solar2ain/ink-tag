# gr551x_zkel 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

内置 MCU Flash；不同主控容量不同，使用对应 flash-map.json。

### ZK_EL_GR42V_V1.2

[BIN / HEX](programmer/ZK_EL_GR42V_V1.2/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2.toml"
```

### ZK_EL_GR42V_V1.2_w_NFC

[BIN / HEX](programmer/ZK_EL_GR42V_V1.2_w_NFC/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2_w_NFC.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2_w_NFC.toml"
```

### ZK_EL_GR42V_V1.2_w_NFC-SSD1683

[BIN / HEX](programmer/ZK_EL_GR42V_V1.2_w_NFC-SSD1683/) · [配置 / Configuration](tools/configs/ZK_EL_GR42V_V1.2_w_NFC-SSD1683.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2_w_NFC-SSD1683.toml"
```

### zkc42vm_eailier_version

[BIN / HEX](programmer/zkc42vm_eailier_version/) · [配置 / Configuration](tools/configs/zkc42vm_eailier_version.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\app-full.zip" -Config ".\tools\configs\zkc42vm_eailier_version.toml"
```
