# Downloads and checksums

[中文](../zh-CN/releases.md) | English

Download the required files from [GitHub Releases](https://github.com/solar2ain/ink-tag/releases) and read that version's notes. No listed attachment means no download is available yet. This version provides WebUI, the Python CLI, user guides, OTA and complete firmware for adapted boards, and independent plaintext programmer tools. Tools are under `firmware/<controller_board>/` and require an explicitly supplied compatible complete-firmware file.

WebUI, CLI and device firmware may have different versions and need not be updated together. Extract the standalone ZIP and open its HTML, use the static ZIP for your own hosting, or install the CLI bundle with local WebUI. See the [CLI guide](cli.md) for CLI installation.

## Check file integrity

Compare each download's SHA256 with `SHA256SUMS.txt` from the same release:

```powershell
Get-FileHash -Algorithm SHA256 FILE
```

On Linux use `sha256sum FILE`; on macOS use `shasum -a 256 FILE`. Download again rather than installing incomplete, changed or mismatching files.

Checksums do not replace publisher signatures. When installing firmware, match the exact MCU, board and compatibility requirements, and retain both client and device signature checks. Never substitute another similarly named board's file.
