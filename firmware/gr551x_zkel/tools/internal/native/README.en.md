# InkTag gr551x_zkel complete-flashing guide

[中文](README.md) | English

**Tools 0.2.0.0** for Windows x64 and Windows PowerShell **5.1** (also PowerShell 7). Tools and firmware are separate; InkTag CLI is not required.

## Before starting

1. Keep the complete tools directory and open a terminal **in the directory containing this README**. Each code block below is one independently copyable command; no `&` prefix is used.
2. Download the matching **complete firmware**, keep its original filename and place it in the **parent directory** of this tools directory. Do not pass an OTA package to the complete-flashing tool.
3. Match the product, PCB marking and actual panel before selecting a configuration. A product can have different boards, and a board can use different panels.
4. Connect stable power, ground and programming wires. Keep the default confirmation and complete readback, especially for the first flash.

Complete-firmware filename used below: `InkTag-gr551x_zkel-full-v0.2.0.0-354a7910f2c0.zip`. If you move or rename it, change only the `-Firmware` path. The tool does not bind to, search for or download this firmware version.

Apart from required `-Firmware` and `-Config`, flashing commands add only `-RetryDetect`. It retries initial detection, never programming or an operation with an unknown result. Speed, timeout and confirmation retain their defaults.

## Prepare the tools

Install Python 3.12 x64, official SEGGER J-Link 7.88j and Goodix GProgrammer console 2.0.3.0, retaining its loaders and DLL. Adjust both installation directories below; exact dependencies are specified in `tools/internal/dependencies-windows-x64.lock.json`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1" -Offline -JLinkHome "C:\Program Files\SEGGER\JLink_V788j" -GProgrammerHome "C:\Goodix\GProgrammer"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## Select the product and PCB configuration

### ZKC42VM — ZK_EL_GR42V_V1.2.toml

- PCB marking: **ZK_EL_GR42V_V1.2**.
- Panel: 400×300, black/white/red.
- Configuration: `tools/configs/ZK_EL_GR42V_V1.2.toml`. Assembly without NFC.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-gr551x_zkel-full-v0.2.0.0-354a7910f2c0.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2.toml" -RetryDetect
```

### ZKC42V-N — ZK_EL_GR42V_V1.2_w_NFC.toml

- PCB marking: **ZK_EL_GR42V_V1.2**.
- Panel: 400×300, black/white/red.
- Configuration: `tools/configs/ZK_EL_GR42V_V1.2_w_NFC.toml`. Assembly with NFC; boards share this marking, so also check that the NFC chip is fitted.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-gr551x_zkel-full-v0.2.0.0-354a7910f2c0.zip" -Config ".\tools\configs\ZK_EL_GR42V_V1.2_w_NFC.toml" -RetryDetect
```

### ZKC42VM (early version) — zkc42vm_eailier_version.toml

- PCB marking: **Full marking not recorded**.
- Panel: 400×300, black/white/red.
- Configuration: `tools/configs/zkc42vm_eailier_version.toml`. Early GR5515 board; verify the controller and wiring first. Not for later boards with the same product name.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-gr551x_zkel-full-v0.2.0.0-354a7910f2c0.zip" -Config ".\tools\configs\zkc42vm_eailier_version.toml" -RetryDetect
```

## Data handling and verification

Complete flashing resets device settings, passwords, device-side Find data, user fonts, old image state and crash records. Images may be invalidated rather than securely erased; unsupported external Flash is retained. This tool does not delete host Apple sessions, Find identities or location history.

Required factory identity, Bluetooth address and calibration are preserved according to the tool rules. Only an existing valid Pro record read from the same device in this operation is retained, never from another device or an old backup. Factory devices and devices without valid authorization initialize as Free.

Enter `y` only after reading the confirmation. Success requires all mandatory readback and verification steps; 100% in one phase is not completion. On error, check wiring, target, firmware and configuration before starting another write.

## Probe, backup and help

`probe.ps1` detects a device without firmware. Where included, `backup.ps1` separately saves factory contents. Both are in the same tools subdirectory used above. Check their help and output locations first; do not probe during timing or power measurements.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

Tools contain no signing keys, vendor SDK, firmware source or user backups. Keep the whole directory and supplied LICENSE, NOTICE and dependency/font/icon notices; do not copy only one script.
