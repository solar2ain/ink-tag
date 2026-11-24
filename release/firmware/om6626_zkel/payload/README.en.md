# OM Windows native tools-only local candidate

Double-click `flash.bat` for English or `flash-zh.bat` for Chinese. You can place a Full ZIP beside the `payload` folder. The tool uses a single match, lists multiple matches or opens a file dialog if it finds none. After checking the board, confirm Full flashing to start waiting for USB J-Link and SWD. The default answer cancels. Detection also waits when you supply parameters. Press Esc to cancel.

Tools version **0.2.0.16** is independent of firmware. No firmware, SDK, compiler,
portable Python, CLI or BLE/Find service is included. This is not a publication
or physical flashing qualification. ZK/HS are separate directories; together
they contain four PCB presets. Their firmware families are not interchangeable.

Keep `payload/`, sibling `records/` and `delivery.json` together. From `payload/`:

```bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\setup.ps1
tools\flash.bat -Firmware "D:\firmware\app-full.zip" -Config "tools\configs\PCB.toml" -JLinkSerial 20090928
tools\probe.bat -Chip om6626 -JLinkSerial 20090928
tools\backup.bat -Chip om6626 -Board PCB -OutputDir "D:\backups" -JLinkSerial 20090928
```

Replace PCB and probe serial with exact values. Presets are under `tools/configs/`.
The flashing BAT prompts without parameters; blank cancels. Exit codes are
0 success, 1 failure, 2 cancellation. `-Help` displays help; `-DryRun` never opens
a probe. Entries verify the exact tools inventory first, rejecting missing,
extra, modified or linked files. Store outputs/backups outside the tools directory.

Runtime requires Windows PowerShell5.1/system .NET, a licensed SEGGER
J-Link/J-Flash7.88j installation and the fixed OM device pack. These external
files are not bundled/downloaded and there is no source-tree/SDK fallback.
`tools/check.ps1 -NativeInstalled` checks their hashes from sibling records
without contacting hardware. The maintained backend still checks version,
actual chip/capacity and context before writing. Development packaging may use
Python; user execution does not. TOML/JSON are supported; legacy YAML/Python
entries are outside this candidate.

Basic consumes external compiled components; Pro consumes an external normally
signed Full ZIP. Integrity,
family, signature and App/Recovery pairing failures stop before device access.
The tool attempts to retain a valid address and required vendor settings.
ZK retains a signed authorization read from the current board, which the Pro firmware
checks against the NFC UID at startup. OM42B keeps external images. See [ADDRESSES.md](ADDRESSES.md).
Full builds the device QR from the installed address and seeds the same NFC link on Pro.

Writing, independent readback and optical acceptance are separate. Detailed
runtime behavior remains in platform `INITIALIZATION.md` and its English version.
