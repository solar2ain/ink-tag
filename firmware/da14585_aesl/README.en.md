# DA14585 tools

[中文](README.md) | English

Tools version **0.2.0.12**. Device probing and offline firmware/board configuration checks for Windows x64. These tools run independently of InkTag CLI and contain no firmware, SDK, compiler or vendor debugger.

> [!NOTE]
> Probing and offline validation are available. **Actual complete flashing is not available in this portable directory.** The required RAM helper is not included and has no public download location. Supplying it does not enable the portable writer, and `-Yes` cannot bypass this restriction. A separate backup command is not provided in this version.

## Prepare the environment

Keep the entire directory together. In a 64-bit PowerShell 5.1 or 7 terminal opened here, run:

```powershell
& .\tools\setup.ps1
& .\tools\check.ps1
```

Python 3.13.16 and pinned dependencies are included. Setup creates only the local `tools/.runtime`; it does not access the network or change system Python, drivers, services or execution policy. Checking does not connect to devices.

For probing, install [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/) separately. The default directory is `C:/Program Files/SEGGER/JLink_V788j`; set `INKTAG_JLINK_HOME` in your current PowerShell session for a different location. Required files and hashes are listed in `tools/vendor-dependencies.json`. If checking fails, correct the version rather than substituting an unchecked program.

## Probe a device

Connect stable power, ground and SWD, then run:

```powershell
& .\tools\probe.ps1 -Chip DA14585
& .\tools\probe.ps1 -Chip DA14585 -JLinkSerial 20090928 -FrequencyHz 1000000
```

Replace the example serial with your probe's serial. Select an exact probe when several are connected. Probing defaults to 1 MHz. `-FrequencyHz` is in Hz and must be a whole number of kHz. `-SpeedKHz` is also accepted, but cannot be combined with `-FrequencyHz`. `-JLink` and `-Snr` alias `-JLinkSerial`.

By default there is one detection attempt. Add `-RetryDetect -TimeoutSeconds 60` to wait for a stable connection; this retries detection only, never a write or an operation with an unknown result. Probing affects debug power state; do not use it during power or clock measurements.

## Validate complete firmware and configuration

After environment preparation, use this offline command. It does not connect to a device and requires neither J-Link nor the RAM helper:

```powershell
& .\tools\validate.ps1 -Chip DA14585 -Firmware 'D:\Firmware\InkTag-da14585_aesl-full.bin' -Config .\tools\configs\asel29.toml
```

Choose the configuration for your actual hardware. These configurations share one complete firmware image:

| Configuration | Screen |
| --- | --- |
| `asel29.toml` | Adapted AESL 2.9-inch original screen |
| `asel42.toml` | Adapted AESL 4.2-inch original UC8176 screen |
| `asel42-ssd1683.toml` | SSD1683 400×300 replacement on the same board |

Screen dimensions alone do not identify a compatible configuration. The configuration specifies display pins and board EN polarity; other screen-signal polarities belong to the driver.

Validation checks the App signature, native formats, CRC, layout, capacity and configuration. Format/CRC checks for Recovery, the loader and GUI do not authenticate their publisher. Passing offline validation does not establish that a connected device can be flashed safely.

`flash.ps1 -DryRun` provides complete-flashing preflight with the same required `-Firmware` and `-Config` parameters. Its defaults are 500000 Hz SWD and 256-byte SRAM blocks; actual programming remains unavailable here. Firmware must be supplied explicitly and is never automatically selected.

## Help, logs and licensing

Entry points accept `-Help`. Probe supports `-DryRun` without opening a debugger. Normal output shows stages and elapsed time; `-Verbose` expands details. Failures retain their records without automatically restarting the operation.

Keep `tools/`, `dependencies/`, `licenses/` and the manifests together. `tool-manifest.json` records file sizes and SHA256 hashes. See the directory's `LICENSE` and `licenses/`; third-party terms remain unchanged. No keys, device backups or user data are included.
