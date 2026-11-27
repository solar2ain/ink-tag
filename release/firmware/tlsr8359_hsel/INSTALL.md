# tlsr8359_hsel 基础版烧录

[English](INSTALL.en.md)

先选 [OTA](ota/)，首次安装或有线恢复使用[完整包](full/)。完整烧录前请备份所需资料。

双击 [flash-zh.bat](tools/flash-zh.bat) 可按中文引导操作。以下命令从本目录执行，脚本默认持续检测设备。

512 KiB 内置 Flash，从 0x00000 开始。使用 CH340 或与对应镜像兼容的烧录器。COM13 是示例，请替换为本机串口。

### HS_EL5101_5M_60_04

[BIN](flash-images/HS_EL5101_5M_60_04/) · [配置 / Configuration](tools/configs/HS_EL5101_5M_60_04.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HS_EL5101_5M_60_04.toml" -Port COM13
```

### HS_EL5101_5M_66_05

[BIN](flash-images/HS_EL5101_5M_66_05/) · [配置 / Configuration](tools/configs/HS_EL5101_5M_66_05.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HS_EL5101_5M_66_05.toml" -Port COM13
```

### HS_EL5102_1M_67_33

[BIN](flash-images/HS_EL5102_1M_67_33/) · [配置 / Configuration](tools/configs/HS_EL5102_1M_67_33.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HS_EL5102_1M_67_33.toml" -Port COM13
```

### HSEL3_02_66M_36

[BIN](flash-images/HSEL3_02_66M_36/) · [配置 / Configuration](tools/configs/HSEL3_02_66M_36.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL3_02_66M_36.toml" -Port COM13
```

### HSEL4Q_01_54M_31

[BIN](flash-images/HSEL4Q_01_54M_31/) · [配置 / Configuration](tools/configs/HSEL4Q_01_54M_31.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL4Q_01_54M_31.toml" -Port COM13
```

### HSEL4Q_02_13M_30

[BIN](flash-images/HSEL4Q_02_13M_30/) · [配置 / Configuration](tools/configs/HSEL4Q_02_13M_30.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL4Q_02_13M_30.toml" -Port COM13
```

### HSEL4Q_04_20M_33-JD79668

[BIN](flash-images/HSEL4Q_04_20M_33-JD79668/) · [配置 / Configuration](tools/configs/HSEL4Q_04_20M_33-JD79668.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL4Q_04_20M_33-JD79668.toml" -Port COM13
```

### HSEL4Q_04_20M_33-SSD1683

[BIN](flash-images/HSEL4Q_04_20M_33-SSD1683/) · [配置 / Configuration](tools/configs/HSEL4Q_04_20M_33-SSD1683.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL4Q_04_20M_33-SSD1683.toml" -Port COM13
```

### HSEL5_01_54M_00

[BIN](flash-images/HSEL5_01_54M_00/) · [配置 / Configuration](tools/configs/HSEL5_01_54M_00.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL5_01_54M_00.toml" -Port COM13
```

### HSEL5_01_54M_02

[BIN](flash-images/HSEL5_01_54M_02/) · [配置 / Configuration](tools/configs/HSEL5_01_54M_02.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL5_01_54M_02.toml" -Port COM13
```

### HSEL5Q_01_54M_00

[BIN](flash-images/HSEL5Q_01_54M_00/) · [配置 / Configuration](tools/configs/HSEL5Q_01_54M_00.toml)

```powershell
.\tools\flash-zh.bat -Firmware ".\full\InkTag-BASIC-0.2.2.1-tlsr8359_hsel-full.zip" -Config ".\tools\configs\HSEL5Q_01_54M_00.toml" -Port COM13
```
