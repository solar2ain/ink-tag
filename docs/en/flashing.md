# First flashing and wired recovery

Complete flashing installs InkTag on factory firmware, updates fixed Recovery or repairs a device that cannot be recovered wirelessly. Ordinary application updates use [OTA](ota.md). Verify the exact MCU, PCB, supply and file checksum first.

## Flashing environment

No Keil, Arm compiler or frontend build setup is required. Install separately licensed vendor tools/drivers and loaders, not a firmware compilation environment.

| Family | Windows runtime | Public launcher |
| --- | --- | --- |
| GR5331/5513/5515 | Native PowerShell, GProgrammer; J-Link identification for GR551x | `flash-goodix.ps1`, matching provided complete-flashing image/runtime |
| OM6626 | Native PowerShell, J-Link and OM6626 device pack | `flash-om6626.ps1`; `-OmSdkRoot` locates the device pack only |
| TLSR8359 | Python, pyserial, qualified UART-to-SWS/tool | `flash-telink.ps1` for included boards |
| nRF52811/52832 | J-Link/SWD; external fonts for nRF52811 | No qualified public Nordic complete-flashing launcher yet |
| DA14585 | J-Link/J-Flash and external Flash loader; no Keil | No qualified public Dialog complete-flashing launcher yet |

A complete-flashing file in a preview does not mean its runtime is ready. Do not substitute generic erase/loadfile commands or partially copy source-repository scripts.

## Wiring

SWD targets need supply, common ground, SWDIO, SWCLK and probe voltage reference. VTref is not reliable power output. TLSR uses SWS, not generic SWD. Match the exact PCB, keep contacts short and stable, and never apply 5 V or guess unmarked pads.

EL022H2 wiring does not automatically qualify EL029H2. DA asel29 has TP5=SWCLK, TP6=SWDIO; use actual signal labels for the 4.2-inch large-pad PCB rather than copying TP positions. Do not connect DA VPP/TP3 or program OTP. DA reset is active-high, not ordinary active-low nRESET.

The default Windows entry requires a factory backup. Explicit `-SkipStockBackupCheck` skips that requirement without requiring a backup file; chip, capacity, layout, voltage and image checks still apply. Keep recoverable factory copies separately whenever possible.

Use `flash/windows/flash-goodix.ps1`, `flash-om6626.ps1` or `flash-telink.ps1` with `-Board` set to the lowercase public MCU/PCB ID, `-Firmware` for the matching complete flashing and `-StockBackup` or the explicit skip option. Use `-h` for exact dependencies and options. Goodix requires GProgrammer; GR551x also uses J-Link identification. OM requires J-Link and the vendor device pack, preserving the fixed state/code/device-information order. Telink requires the Python UART-to-SWS tool and explicit experimental-layout confirmation where applicable. Do not interchange board-specific complete-flashing containers.

Invoke Windows launchers with `powershell -NoProfile -ExecutionPolicy Bypass -File .\flash\windows\flash-goodix.ps1 -h`, replacing the script for OM/TLSR. Commands require the exact matching complete-flashing image/runtime in your package.

Linux entry points report support and preparation. A Windows EXE or PowerShell launch is not qualified native Linux flashing. Unsupported targets refuse hardware writes; `bash flash/linux/flash.sh --help` lists the current scope. macOS has no one-command complete flashing support. Nordic and Dialog require their independently qualified targets and packages; check the [support table](supported-devices.md).

Wait for programming and readback verification, follow the power/BOOT instructions, then check the App identity, firmware and panel. For failures, check supply, ground, wiring and tool ownership before a bounded retry. Do not blindly erase unknown regions.

## Nordic and Dialog differences

nRF52811 keeps App internally and fonts externally. First installation needs the same-version internal complete-flashing image and font assets; OTA does not supply missing fonts. A preparation-only writer test is not physical qualification or a complete public entry.

The DA complete-flashing image includes boot, App, Recovery and fonts, unlike Nordic's separate font process. asel29 defaults to SSD1619 128×296 BWR; asel42 to UC8176 400×300 BWR, not SSD1683. Verify the fitted screen after installation.

Probe/backup may halt the MCU; do not use during clock/power observation. A backup may be old InkTag rather than factory firmware, so retain provenance and checksum. Backup skip does not bypass current identity, voltage or required UICR preservation.
