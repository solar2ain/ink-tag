# InkTag GR standalone tools

This is a firmware-free **tools-only plaintext directory**. The target, tool
version, dependencies and file hashes are bound by `inktag-goodix-tools.json`.
Keep the complete directory structure. Git, firmware source, an SDK, a compiler
and an installed InkTag CLI are not required.

Directory guidance, configuration selection and complete-flashing instructions
are maintained in the [flashing guide](../../docs/en/flash.md), also available
[online](https://github.com/solar2ain/ink-tag/blob/main/docs/en/flash.md).
Use `-h` or `--help` on `tools/setup.ps1`, `check.ps1`, `probe.ps1`, `backup.ps1`
and `flash.ps1` for their actual parameters.

## Offline dependencies

Supported hosts are Windows x64, CPython 3.12 x64, Windows PowerShell 5.1 and
PowerShell 7. Locked Python wheels are included. Setup creates a bundle-local
environment without changing system Python. Setup/check do not start device
operations, install USB drivers or change the global execution policy.

Native programs are not redistributed. Obtain the following from their official
providers and accept their licenses:

- [SEGGER J-Link 7.88j](https://www.segger.com/downloads/jlink/).
- [Goodix GProgrammer console 2.0.3.0](https://www.goodix.com/en/software_tool/gprogrammer_ble),
  retaining its supplied JLinkARM.dll 6.18c and loader/configuration files.

Exact versions and hashes are recorded in
`tools/internal/dependencies-windows-x64.lock.json`; see setup help for import
parameters. Other files with the same names or different versions are not
implicitly qualified. Install drivers manually using vendor instructions.

## Operation boundaries

Probe/backup do not require firmware. Flash requires an explicit `-Config`
matching the physical PCB and screen, and an external compatible signed
complete-firmware package supplied with `-Firmware`. The tools do not select,
download, embed or pin a firmware version. Missing required arguments fail
before device access even with `-Yes`. Signature, target, capacity, layout and
App/Recovery pairing checks remain mandatory. Cleanup and retention scope are
described in the flashing guide and the actual operation warning.

DryRun and offline validation are not physical qualification. Full retains the
existing guarded backend; a tools update does not qualify additional boards or
stock restore. `restore.ps1` remains guarded. Inspect an unknown write outcome
before attempting another operation.

Third-party licenses and copyrights remain intact. See `LICENSE`,
`THIRD_PARTY_NOTICES.md` and the dependency files.
