# InkTag nrf52811_aesl complete-flashing guide

Shared layout: `ota/`, `full/`, `tools/`; configurations are in `tools/configs/`. Run the commands from this board directory. The original backend is retained under `tools/internal/native/`; tools still require an explicit firmware argument.


[中文](README.md) | English

**Tools 0.2.0.4** for Windows x64 and Windows PowerShell **5.1** (also PowerShell 7). Tools and firmware are separate; InkTag CLI is not required.

## Before starting

1. Keep the complete tools directory and open a terminal **in the directory containing this README**. Each code block below is one independently copyable command; no `&` prefix is used.
2. Download the matching **complete firmware**, keep its original filename and place it in the **`full/` subdirectory** of this tools directory. Do not pass an OTA package to the complete-flashing tool.
3. Match the product, PCB marking and actual panel before selecting a configuration. A product can have different boards, and a board can use different panels.
4. Connect stable power, ground and programming wires. Keep the default confirmation and complete readback, especially for the first flash.

Complete-firmware filename used below: `InkTag-nrf52811_aesl-full-v0.2.0.0-b6fb6ecc78cd.zip`. If you move or rename it, change only the `-Firmware` path. The tool does not bind to, search for or download this firmware version.

Apart from required `-Firmware` and `-Config`, flashing commands add only `-RetryDetect`. It retries initial detection, never programming or an operation with an unknown result. Speed, timeout and confirmation retain their defaults.

## Prepare the tools

Install official J-Link **7.88j** and its USB driver. Portable Python and dependencies are included; no SDK, compiler or system Python is required. One connected probe uses the default selection. With multiple probes, explicitly add `-JLinkSerial` to select the correct one.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## Select the product and PCB configuration

### AESL / Hema EL022H2 — EL022HWRL.toml

- PCB marking: **EL022HWRL (CAL_VER06)**.
- Panel: 128×250, black/white/red.
- Configuration: `tools/configs/EL022HWRL.toml`. Original 2.13-inch three-colour panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware ".\full\InkTag-nrf52811_aesl-full-v0.2.0.0-b6fb6ecc78cd.zip" -Config ".\tools\configs\EL022HWRL.toml" -RetryDetect
```

### AESL / Hema EL022H2 (replacement Huawei A0 panel) — EL022HWRL-A0.toml

- PCB marking: **EL022HWRL (CAL_VER06)**.
- Panel: 768×552, black/white/red/yellow.
- Configuration: `tools/configs/EL022HWRL-A0.toml`. Replacement 768×552 four-colour A0 panel; not for the original small panel.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware ".\full\InkTag-nrf52811_aesl-full-v0.2.0.0-b6fb6ecc78cd.zip" -Config ".\tools\configs\EL022HWRL-A0.toml" -RetryDetect
```

## Data handling and verification

Complete flashing resets device settings, passwords, device-side Find data, user fonts, old image state and crash records. Images may be invalidated rather than securely erased; unsupported external Flash is retained. This tool does not delete host Apple sessions, Find identities or location history.

Required factory identity, Bluetooth address and calibration are preserved according to the tool rules. This controller is Free-only and cannot activate Pro.

Enter `y` only after reading the confirmation. Success requires all mandatory readback and verification steps; 100% in one phase is not completion. On error, check wiring, target, firmware and configuration before starting another write.

The normal Bluetooth name is `IKT_NRF_XXXX`; Recovery adds `_OTA`. Use WebUI to set the time and check the display.

## Probe, backup and help

`probe.ps1` detects a device without firmware. Where included, `backup.ps1` separately saves factory contents. Both are in the same tools subdirectory used above. Check their help and output locations first; do not probe during timing or power measurements.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

Tools contain no signing keys, vendor SDK, firmware source or user backups. Keep the whole directory and supplied LICENSE, NOTICE and dependency/font/icon notices; do not copy only one script.
