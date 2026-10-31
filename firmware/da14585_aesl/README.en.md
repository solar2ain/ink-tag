# InkTag da14585_aesl complete-flashing guide

Shared layout: `ota/`, `full/`, `tools/`; configurations are in `tools/configs/`. Run the commands from this board directory. The original backend is retained under `tools/internal/native/`; tools still require an explicit firmware argument.


[中文](README.md) | English

**Tools 0.2.0.12** for Windows x64 and Windows PowerShell **5.1** (also PowerShell 7). Tools and firmware are separate; InkTag CLI is not required.

## Before starting

1. Keep the complete tools directory and open a terminal **in the directory containing this README**. Each code block below is one independently copyable command; no `&` prefix is used.
2. Download the matching **complete firmware**, keep its original filename and place it in the **`full/` subdirectory** of this tools directory. Do not pass an OTA package to the complete-flashing tool.
3. Match the product, PCB marking and actual panel before selecting a configuration. A product can have different boards, and a board can use different panels.
4. Connect stable power, ground and programming wires. Keep the default confirmation and complete readback, especially for the first flash.

Complete-firmware filename used below: `InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin`. If you move or rename it, change only the `-Firmware` path. The tool does not bind to, search for or download this firmware version.

Apart from required `-Firmware` and `-Config`, flashing commands add only `-RetryDetect`. It retries initial detection, never programming or an operation with an unknown result. Speed, timeout and confirmation retain their defaults.

> [!WARNING]
> **Actual complete flashing is not enabled in the current portable tools.** Complete invocations are listed for every configuration below, but execution is rejected before writing. Append `-DryRun` for offline preflight; a successful preflight does not enable writing. Do not use `-Yes` or modify scripts to bypass this restriction.
>
> The DA RAM helper is not bundled and has no public download location. This version has no separate backup command.

## Prepare the tools

Install official J-Link **7.88j** and its USB driver. Portable Python and dependencies are included; no SDK, compiler or system Python is required. One connected probe uses the default selection. With multiple probes, explicitly add `-JLinkSerial` to select the correct one.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## Select the product and PCB configuration

### AESL / Hema AESL-2.9 — asel29.toml

- PCB marking: **No visible marking**.
- Panel: 128×296, black/white/red.
- Configuration: `tools/configs/asel29.toml`. Original 2.9-inch panel; actual complete flashing is not enabled in the current portable tools.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware ".\full\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel29.toml" -RetryDetect
```

### AESL / Hema AESL-4.2 — asel42.toml

- PCB marking: **No visible marking**.
- Panel: 400×300, black/white/red.
- Configuration: `tools/configs/asel42.toml`. Original 4.2-inch UC8176 panel; actual complete flashing is not enabled in the current portable tools.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware ".\full\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel42.toml" -RetryDetect
```

### AESL / Hema AESL-4.2 (replacement SSD1683 panel) — asel42-ssd1683.toml

- PCB marking: **No visible marking**.
- Panel: 400×300, black/white/red.
- Configuration: `tools/configs/asel42-ssd1683.toml`. Same 4.2-inch board with a replacement HINK-E042A13-FPC-A0 / SSD1683 400×300 panel; actual complete flashing is not enabled in the current portable tools.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware ".\full\InkTag-da14585_aesl-full-v0.2.0.34462-e3c0352c6f9c.bin" -Config ".\tools\configs\asel42-ssd1683.toml" -RetryDetect
```

## Data handling and verification

Complete flashing resets device settings, passwords, device-side Find data, user fonts, old image state and crash records. Images may be invalidated rather than securely erased; unsupported external Flash is retained. This tool does not delete host Apple sessions, Find identities or location history.

Required factory identity, Bluetooth address and calibration are preserved according to the tool rules. This controller is Free-only and cannot activate Pro.

Enter `y` only after reading the confirmation. Success requires all mandatory readback and verification steps; 100% in one phase is not completion. On error, check wiring, target, firmware and configuration before starting another write.

## Probe, backup and help

`probe.ps1` detects a device without firmware. Where included, `backup.ps1` separately saves factory contents. Both are in the same tools subdirectory used above. Check their help and output locations first; do not probe during timing or power measurements.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

Tools contain no signing keys, vendor SDK, firmware source or user backups. Keep the whole directory and supplied LICENSE, NOTICE and dependency/font/icon notices; do not copy only one script.
