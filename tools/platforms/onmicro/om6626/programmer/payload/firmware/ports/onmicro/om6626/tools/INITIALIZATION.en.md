# OM initialization and preservation flashing

Double-click `flash.bat` for English or `flash-zh.bat` for Chinese. Select Full firmware first and then the board. Confirm Full flashing after checking the firmware and board. The tool then waits for a USB J-Link and an SWD target. Press Esc to cancel the wait or any key to close an error.

Once the USB probe is selected, one J-Link session continuously checks for SWD contact. If needed, power the board on normally after the ready message. The tool checks that the captured board is still halted before handing off to the existing maintenance flow.

The flash.bat entry supports the four maintained PCB presets under `configs/`,
App 0.2.2 and shared Common Recovery 1.0.1, epoch1/layout-v1. This batch qualifies
offline preflight and isolated host flows. Physical Full/Recovery writes
and new Pro signatures have not been performed.

```bat
flash.bat -Firmware "D:\firmware\app-ota-components.json" -Config "configs\ZK_EL_OM21BA_V1.1.toml" -JLinkSerial 20090928
```

Replace the example serial with the exact connected probe. Multiple probes are
never guessed. Paths may contain spaces. Without parameters the entries prompt;
blank input cancels. Exit codes: 0 success, 1 failure, 2 cancellation. `-DryRun`
validates files without resolving native tools or accessing a probe. Programming/readback and optical qualification are separate results.

Runtime dependencies are Windows PowerShell 5.1, system .NET, the existing licensed
J-Link/J-Flash 7.88j and maintained OM device pack. This entry does not run
Python/pip/venv, InkTag CLI, BLE or Find. Development builds/signing/oracles may use
Python. Native configuration accepts the maintained TOML subset or equivalent
JSON. YAML remains on the legacy Python `flash.ps1` path. Keep the repository
directory structure and all internal modules, terminal presentation, read-only
project/callback, `tools/jlink-tools.ps1`, `tools/powershell-cli.ps1` and
`tools/hardware/activation-preservation.ps1`; copying only the BAT file is insufficient.

Basic input is the component JSON, OTA ZIP and Common Recovery BIN produced by the
development `package-onmicro-base-ota.py`. User execution consumes compiled files.
Pro uses the existing normally signed Full ZIP with publisher/context/pairing
checks. No user key override or external authorization-token import exists.
When needed, the installer initializes a legal address for the current unit. Unit labels, links and QR can be configured later in WebUI. Missing, blank, corrupt or unreadable optional
records use defaults.
ZK retains the signed authorization read from the current board. The Pro firmware
checks it against the NFC UID at startup. A base installation can retain the record
for a later Pro installation. Connection/chip/capacity/mandatory-read/write/readback
failures remain errors. Failed or uncertain writes are never automatically replayed.

The plan skips manufacturer boot/CPFT 0x00000–0x03FFF, runtime MBR
0x80000–0x81FFF and ZK CONF 0xFF000–0xFFFFF. The vendor loader still updates
ROM CRC metadata 0x1184–0x118D and its required 0x2000 backup; independent prediction
and readback check those vendor changes. Usable manufacturer ROM formatting is
required (0x1182=0x60, 0x11FE=55AA); the tool cannot invent missing manufacturer
boot data after a whole-chip erase. External Flash is untouched; OM42B external images remain. Full clears
ordinary settings, password/Find, internal images, user fonts and health records.
Fully power-cycle manually after successful readback.
