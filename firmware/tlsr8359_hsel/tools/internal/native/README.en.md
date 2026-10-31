# InkTag tlsr8359_hsel complete-flashing guide

[中文](README.md) | English

**Tools 0.2.0.8** for Windows x64 and Windows PowerShell **5.1** (also PowerShell 7). Tools and firmware are separate; InkTag CLI is not required.

## Before starting

1. Keep the complete tools directory and open a terminal **in the directory containing this README**. Each code block below is one independently copyable command; no `&` prefix is used.
2. Download the matching **complete firmware**, keep its original filename and place it in the **parent directory** of this tools directory. Do not pass an OTA package to the complete-flashing tool.
3. Match the product, PCB marking and actual panel before selecting a configuration. A product can have different boards, and a board can use different panels.
4. Connect stable power, ground and programming wires. Keep the default confirmation and complete readback, especially for the first flash.

Complete-firmware filename used below: `InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip`. If you move or rename it, change only the `-Firmware` path. The tool does not bind to, search for or download this firmware version.

Apart from required `-Firmware` and `-Config`, flashing commands add only `-RetryDetect`. It retries initial detection, never programming or an operation with an unknown result. Speed, timeout and confirmation retain their defaults.

**TLSR exception:** complete flashing requires an explicit port. Replace `-Port COM3` below with the programmer port shown in Device Manager. Keep other defaults. Use CH340/SWS wiring, not J-Link SWD wiring.

## Prepare the tools

Install Python **3.11 or newer** and the CH340 driver, then install and check the dependencies:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\setup.ps1"
```

## Select the product and PCB configuration

### Hanshow Nowa-154R-N — HS_EL5101_5M_66_05.toml

- PCB marking: **HS_EL5101_5M_66_05**.
- Panel: 152×152, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HS_EL5101_5M_66_05.toml`. Original 152×152 three-colour panel; not for 200×200 boards.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5101_5M_66_05.toml" -RetryDetect -Port COM3
```

### Hanshow Stellar-S3TN@ (check the product label) — HS_EL5101_5M_60_04.toml

- PCB marking: **HS_EL5101_5M_60_04**.
- Panel: 200×200, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HS_EL5101_5M_60_04.toml`. P154030 200×200 three-colour assembly; E0154A07-A1 panel compatibility is not confirmed. Verify the product name on its label.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5101_5M_60_04.toml" -RetryDetect -Port COM3
```

### Hanshow Nowa-213R-N — HS_EL5102_1M_67_33.toml

- PCB marking: **HS_EL5102_1M_67_33**.
- Panel: 128×250, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HS_EL5102_1M_67_33.toml`. Original 2.13-inch three-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5102_1M_67_33.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-213R-N — HSEL4Q_02_13M_30.toml

- PCB marking: **HSEL4Q_02_13M_30**.
- Panel: 128×250, black/white/red/yellow.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_02_13M_30.toml`. Original 2.13-inch four-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_02_13M_30.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33.toml

- PCB marking: **HSEL4Q_04_20M_33**.
- Panel: 400×300, black/white/red/yellow.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33.toml`. JD79668AA four-colour assembly.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33-SSD1683.toml

- PCB marking: **HSEL4Q_04_20M_33**.
- Panel: 400×300, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33-SSD1683.toml`. SSD1683 three-colour assembly, separate from the four-colour configuration.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33-SSD1683.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33-YW2427.toml

- PCB marking: **HSEL4Q_04_20M_33**.
- Panel: 400×300, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33-YW2427.toml`. YW2427 batch with an SSD1683 three-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33-YW2427.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154R-N (early version) — HSEL5_01_54M_00.toml

- PCB marking: **HSEL5_01_54M_00**.
- Panel: 200×200, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL5_01_54M_00.toml`. Early version with a removable JD79653A 200×200 three-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5_01_54M_00.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154R-N (later version) — HSEL5_01_54M_02.toml

- PCB marking: **HSEL5_01_54M_02**.
- Panel: 200×200, black/white/red.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL5_01_54M_02.toml`. Later YW2116 fixed-panel version; not interchangeable with the _00 configuration.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5_01_54M_02.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154Q-N — HSEL5Q_01_54M_00.toml

- PCB marking: **HSEL5Q_01_54M_00**.
- Panel: 200×200, black/white/red/yellow.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL5Q_01_54M_00.toml`. 200×200 four-colour panel; the complete image and colour output remain unconfirmed. Installation is not currently recommended.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5Q_01_54M_00.toml" -RetryDetect -Port COM3
```

### Hanshow NebularPro-154Q-N — HSEL4Q_01_54M_31.toml

- PCB marking: **HSEL4Q_01_54M_31**.
- Panel: 200×200, black/white/red/yellow.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_01_54M_31.toml`. 200×200 four-colour panel; complete-flashing validation remains incomplete.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_01_54M_31.toml" -RetryDetect -Port COM3
```

### Hanshow PolarisPro-260Q-N — HSEL3_02_66M_36.toml

- PCB marking: **HSEL3_02_66M_36**.
- Panel: 152×296, black/white/red/yellow.
- Configuration: `firmware/ports/telink/tlsr8359/tools/configs/HSEL3_02_66M_36.toml`. 2.66-inch 152×296 four-colour panel; NFC and standby current remain unconfirmed.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL3_02_66M_36.toml" -RetryDetect -Port COM3
```

## Data handling and verification

Complete flashing resets device settings, passwords, device-side Find data, user fonts, old image state and crash records. Images may be invalidated rather than securely erased; unsupported external Flash is retained. This tool does not delete host Apple sessions, Find identities or location history.

Required factory identity, Bluetooth address and calibration are preserved according to the tool rules. Only an existing valid Pro record read from the same device in this operation is retained, never from another device or an old backup. Factory devices and devices without valid authorization initialize as Free.

Enter `y` only after reading the confirmation. Success requires all mandatory readback and verification steps; 100% in one phase is not completion. On error, check wiring, target, firmware and configuration before starting another write.

## Probe, backup and help

`probe.ps1` detects a device without firmware. Where included, `backup.ps1` separately saves factory contents. Both are in the same tools subdirectory used above. Check their help and output locations first; do not probe during timing or power measurements.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Help
```

Tools contain no signing keys, vendor SDK, firmware source or user backups. Keep the whole directory and supplied LICENSE, NOTICE and dependency/font/icon notices; do not copy only one script.
