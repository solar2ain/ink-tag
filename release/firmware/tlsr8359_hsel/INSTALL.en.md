# Flash tlsr8359_hsel BASIC

[简体中文](INSTALL.md)

Use the [OTA package](ota/) for wireless updates and the [complete package](full/) for first installation and wired recovery. Back up what you need first.

Run [flash.bat](firmware/ports/telink/tlsr8359/tools/flash.bat) for the guided setup. Run the commands below from this directory. Device detection waits by default.

The 512 KiB internal image starts at 0. Replace COM13 with your actual port.

### HS_EL5101_5M_60_04

[BIN / HEX](programmer/HS_EL5101_5M_60_04/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HS_EL5101_5M_60_04.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HS_EL5101_5M_60_04.toml" -Port COM13
```

### HS_EL5101_5M_66_05

[BIN / HEX](programmer/HS_EL5101_5M_66_05/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HS_EL5101_5M_66_05.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HS_EL5101_5M_66_05.toml" -Port COM13
```

### HS_EL5102_1M_67_33

[BIN / HEX](programmer/HS_EL5102_1M_67_33/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HS_EL5102_1M_67_33.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HS_EL5102_1M_67_33.toml" -Port COM13
```

### HSEL3_02_66M_36

[BIN / HEX](programmer/HSEL3_02_66M_36/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL3_02_66M_36.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL3_02_66M_36.toml" -Port COM13
```

### HSEL4Q_01_54M_31

[BIN / HEX](programmer/HSEL4Q_01_54M_31/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL4Q_01_54M_31.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL4Q_01_54M_31.toml" -Port COM13
```

### HSEL4Q_02_13M_30

[BIN / HEX](programmer/HSEL4Q_02_13M_30/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL4Q_02_13M_30.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL4Q_02_13M_30.toml" -Port COM13
```

### HSEL4Q_04_20M_33-JD79668

[BIN / HEX](programmer/HSEL4Q_04_20M_33-JD79668/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL4Q_04_20M_33-JD79668.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL4Q_04_20M_33-JD79668.toml" -Port COM13
```

### HSEL4Q_04_20M_33-SSD1683

[BIN / HEX](programmer/HSEL4Q_04_20M_33-SSD1683/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL4Q_04_20M_33-SSD1683.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL4Q_04_20M_33-SSD1683.toml" -Port COM13
```

### HSEL5_01_54M_00

[BIN / HEX](programmer/HSEL5_01_54M_00/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL5_01_54M_00.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL5_01_54M_00.toml" -Port COM13
```

### HSEL5_01_54M_02

[BIN / HEX](programmer/HSEL5_01_54M_02/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL5_01_54M_02.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL5_01_54M_02.toml" -Port COM13
```

### HSEL5Q_01_54M_00

[BIN / HEX](programmer/HSEL5Q_01_54M_00/) · [配置 / Configuration](firmware/ports/telink/tlsr8359/tools/HSEL5Q_01_54M_00.toml)

```powershell
.\firmware\ports\telink\tlsr8359\tools\flash.bat -Firmware ".\full\InkTag-tlsr8359_hsel-base-full.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\HSEL5Q_01_54M_00.toml" -Port COM13
```
