# InkTag om6626_zkel complete-flashing guide

[中文](README.md) | English

**Tools 0.2.0.7** for Windows x64 and Windows PowerShell **5.1** (also PowerShell 7). Tools and firmware are separate; InkTag CLI is not required.

## Before starting

1. Keep the complete tools directory and open a terminal **in the directory containing this README**. Each code block below is one independently copyable command; no `&` prefix is used.
2. Download the matching **complete firmware**, keep its original filename and place it in the **parent directory** of this tools directory. Do not pass an OTA package to the complete-flashing tool.
3. Match the product, PCB marking and actual panel before selecting a configuration. A product can have different boards, and a board can use different panels.
4. Connect stable power, ground and programming wires. Keep the default confirmation and complete readback, especially for the first flash.

Complete-firmware filename used below: `InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip`. If you move or rename it, change only the `-Firmware` path. The tool does not bind to, search for or download this firmware version.

Apart from required `-Firmware` and `-Config`, flashing commands add only `-RetryDetect`. It retries initial detection, never programming or an operation with an unknown result. Speed, timeout and confirmation retain their defaults.

## Prepare the tools

Install Python **3.12.0** x64, official J-Link **7.88j** and the OM6626 J-Link support files as described in `DEPENDENCIES.txt`. Put that Python on PATH before running. For other locations, use `setup.ps1 -Help` to view path options.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## Select the product and PCB configuration

### ZKC21V-N (OM variant) — ZK_EL_OM6626_2.13_V1.1.toml

- PCB marking: **ZK_EL_OM6626_2.13_V1.1**.
- Panel: 128×250, black/white/red.
- Configuration: `tools/configs/ZK_EL_OM6626_2.13_V1.1.toml`. Original three-colour panel, with NFC.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml" -RetryDetect
```

### ZKC21B-NAE4 — ZK_EL_OM21BA_V1.1.toml

- PCB marking: **ZK_EL_OM21BA_V1.1**.
- Panel: 128×250, black/white/red/yellow.
- Configuration: `tools/configs/ZK_EL_OM21BA_V1.1.toml`. Original four-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM21BA_V1.1.toml" -RetryDetect
```

### ZKC42B-NE4 — ZK_EL_OM42BA_V1.0.toml

- PCB marking: **ZK_EL_OM42BA_V1.0**.
- Panel: 400×300, black/white/red/yellow.
- Configuration: `tools/configs/ZK_EL_OM42BA_V1.0.toml`. Original four-colour panel; separate external image Flash is retained.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM42BA_V1.0.toml" -RetryDetect
```

## Data handling and verification

Complete flashing resets device settings, passwords, device-side Find data, user fonts, old image state and crash records. Images may be invalidated rather than securely erased; unsupported external Flash is retained. This tool does not delete host Apple sessions, Find identities or location history.

Required factory identity, Bluetooth address and calibration are preserved according to the tool rules. Only an existing valid Pro record read from the same device in this operation is retained, never from another device or an old backup. Factory devices and devices without valid authorization initialize as Free.

Enter `y` only after reading the confirmation. Success requires all mandatory readback and verification steps; 100% in one phase is not completion. On error, check wiring, target, firmware and configuration before starting another write.

After completion, fully remove and restore power. The separate ZKC42B external image Flash is not cleared.

## Probe, backup and help

`probe.ps1` detects a device without firmware. Where included, `backup.ps1` separately saves factory contents. Both are in the same tools subdirectory used above. Check their help and output locations first; do not probe during timing or power measurements.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

Tools contain no signing keys, vendor SDK, firmware source or user backups. Keep the whole directory and supplied LICENSE, NOTICE and dependency/font/icon notices; do not copy only one script.
