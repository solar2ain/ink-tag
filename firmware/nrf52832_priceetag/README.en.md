# nRF52832 Windows offline tools

Shared layout: `ota/`, `full/`, `tools/`; configurations are in `tools/configs/`. Run the commands from this board directory. The original backend is retained under `tools/internal/native/`; tools still require an explicit firmware argument.


[中文](README.md) | English

Tools and firmware are separate deliveries. This complete tools-only directory includes probe,
backup, flash, offline checks and pinned dependencies. It never includes, downloads,
searches for or defaults to firmware. Tool and firmware versions are independent. The current tools version is 0.2.0.3; buildId is recorded separately.
The exported HW56 complete writer remains guarded: flash currently permits offline
DryRun validation only. This candidate is not approved for public release.

Copy the complete directory into a writable location; Chinese characters and spaces are supported.
Use Windows x64 PowerShell 5.1 or 7:

```powershell
./tools/setup.ps1
./tools/check.ps1 -JLinkExe 'C:/Program Files/SEGGER/JLink_V788j/JLink.exe'
./tools/probe.ps1 -Chip nrf52832 -DryRun
./tools/backup.ps1 -Chip nrf52832 -Board 'PriceETag V1.7' -DryRun
```

Setup extracts bundled CPython 3.13.16 and dependencies without system installation
or network access. Check only reads and verifies files; it never starts the probe,
serial or BLE. J-Link is not redistributed. Obtain the complete 7.88j pack from
[SEGGER](https://www.segger.com/downloads/jlink/) under its terms, for genuine SEGGER
or authorized OEM probes. Driver installation may require administrator rights.

Probe and backup need no firmware. Removing DryRun performs SWD work only within
separately authorized exact-device scope. Board is an operator label, not silicon
identification. Backup uses two independent Flash/FICR/UICR captures and verification;
failed/partial captures are retained and never reported as success.

Flash requires an explicit, separately obtained signed native Full package:

```powershell
./tools/flash.ps1 -Firmware 'D:/firmware/app-full.zip' -Config ./tools/configs/PriceEtag_V1.7.toml -DryRun
```

Missing Firmware is refused before hardware access; bundle metadata cannot supply it.
DryRun verifies the native App signature, chip, layout, capacity, configuration and
firmware-internal App/Recovery pairing. Removing DryRun still refuses complete writing
pending HW56 qualification. Full clears passwords, Find, user fonts, internal images,
settings and crash/health/reset history, with default y/N. Only the complete native
Pro token freshly captured from this exact unit and verified against its immutable
UID is retained. Both physical pages erased or clean current-journal absence stays
Free. Successfully read stock/unknown old layouts and damaged or foreign old
authorizations warn and initialize defaults as Free; invalid tokens are never restored. No old backup or
manual token is accepted. No whole-Flash backup is taken; the additional native
authority read is 8 KiB. FICR/UICR, typed BLE identity and calibration are retained.
Separate external Flash is not accessed or claimed erased. Once all Flash workers
and readback finish, the CPU stays halted while direct SWD clears physical 64 KiB
SRAM and RESETREAS, then verifies RAM, identity and both native boot pages before
the sole native reset. Missing shared verification, unknown writes or mismatched
readback stop without replay. Recovery has member-hash integrity only, not an
independent signature.

RetryDetect is opt-in and only retries initial detection, never unknown writes or
failed readback. This bundle provides its J-Link dependency closure only; source
DAPLink/Linux/macOS paths gain no qualification. No SDK, compiler, Git, full CLI or
BLE runtime is required. No firmware is downloaded automatically.

Maintain documents first in the main repository under release/platform-tools/nrf52832/,
then copy approved bytes one way. Integration archives version materials under
release/versions/<version>/; the user tools tree is release/tools/nrf52832_priceetag/, with tools and firmware recorded separately. Historical
combined archives remain evidence, not the recommended tools delivery. See LICENSE,
LICENSING.md, THIRD_PARTY_NOTICES.md and dependency licenses. This document grants no
device or publication authority.

## Tool-private format closure

Probe/backup/flash neither start InkTag CLI nor import its app, transport or BLE
modules. The tool-private nordic_tool_format snapshot owns the screen/default QR,
native records and bounded font validation; provenance.json binds its inputs.
No installed or mutable CLI is required. qrcode, cryptography and other offline
dependencies remain independently locked. powershell-cli.ps1 is only the local
PowerShell help/parameter guard. Delivery validation executes real prepare,
bound-plan reload and fake readback for erased-stock/Free/same-UID Pro, foreign
authorization warning/Free fallback and pre-write missing-dependency failure with a poisoned client importer.
These host checks do not qualify physical Full or remove its existing export guard.
