# nRF52832 PriceETag tools

Double-click `flash.bat` for English or `flash-zh.bat` for Chinese. The tool uses a single valid local Full ZIP, lists multiple matches and opens a picker if it finds none. Choose the board and confirm Full flashing before detection begins.

This directory contains tools only: no firmware, Python runtime, SDK, compiler,
or private key. The BAT entries use Windows x64 PowerShell 5.1/.NET.
Install the original SEGGER J-Link 7.88j separately for hardware operations;
the tools verify its pinned EXE/DLL hashes.

Double-click an entry for simple path prompts; an empty response cancels.
Automation can supply all arguments. Quote paths containing spaces or Unicode.
Success returns 0; validation, programming and readback failures return nonzero.
A failed or unconfirmed write is never replayed automatically.

`flash.bat` is the recommended normal flashing entry. It attempts to preserve
usable same-device information and uses defaults when optional information is
absent. The board menu lists each supported configuration by number, PCB marking
(PriceETag V1.7) and config name. Before detection, review the board, firmware and
the data that will be kept or cleared, then confirm the write. Empty input cancels.
`-Config` and `-Firmware` skip their respective prompts. Unattended automation
uses complete arguments with `-Yes`; missing required arguments fail.
The existing J-Link selection/serial-pinning flow remains; use `-JLinkSerial`
when an explicit probe selection is needed.

Generate clean initialization material offline, without reading an old device:

```bat
generate-hex.bat -Firmware "D:\firmware\app-full.zip" -Config "configs\PriceEtag_V1.7.toml" -Output "D:\initialization material"
```

An explicit external firmware package is required. Native App/Recovery pairing
and layout are checked. Base uses CRC integrity; Pro verifies the fixed publisher
signature. An invalid present signature never falls back to unsigned admission.
These entries accept TOML/JSON configuration, not YAML.

Output includes `main-flash.hex` covering the 512 KiB main Flash, an address map,
hashes and a sanitized source package. The HEX starts at internal Flash address 0
and excludes FICR/UICR and external Flash. Material contains no previous device MAC,
name, QR, Pro or user data. Startup uses the chip FICR/SDK address; import a QR
later through WebUI.

Install the exact clean material:

```bat
flash-hex.bat -ImageDirectory "D:\initialization material" -JLinkSerial 20090928
```

Clean installation clears old activation and ordinary user data. For normal
flashing, use the recommended entry:

```bat
flash.bat -Firmware "D:\firmware\app-full.zip" -Config "configs\PriceEtag_V1.7.toml" -JLinkSerial 20090928
```

Preserving installation first reads chip identity and necessary old information.
Factory/calibration/address areas and publicly verified same-UID Pro are kept.
The Pro record is retained even for a Base target; Base gains no Pro features.
Missing, blank, corrupt, unreadable or foreign optional information defaults to
absent and does not block a base installation. Ordinary settings, password, Find,
images, fonts, QR and health records are cleared. Connection, chip/capacity,
programming, readback and cleanup failures remain errors.

Without arguments, `generate-hex.bat` asks for firmware, board configuration and
output directory and remains offline. `flash-hex.bat` asks only for the generated
material directory and uses its bound configuration.

Both installation entries confirm before one programming operation, verify the
complete internal Flash and FICR/UICR, then clean up and reset. `-DryRun` validates
and prints the plan offline without hardware access. `-Yes` explicitly skips the
interactive confirmation. `-JLinkExe` selects a pinned original executable.
Receipts, including failed operations, remain in unique directories under
`%LOCALAPPDATA%\InkTag\programmer\nrf52832\`, avoiding PS5.1 deep-path limits.

This BAT batch has offline and simulated-device evidence. Complete hardware
flashing qualification is separate; DryRun and App OTA do not establish it.
Legacy probe/backup, pyOCD and YAML workflows remain in the existing Python-based
tools package and are outside this Python-free directory.
