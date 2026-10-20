# Downloads and checksums

[中文](../zh-CN/releases.md) | English

Download the required files from [GitHub Releases](https://github.com/solar2ain/ink-tag/releases) and read that version's notes. No listed attachment means no download is available yet. This release contains WebUI, the Python CLI and user guides, with no firmware or wired flashing packages.

WebUI, CLI and device firmware may have different versions and need not be updated together. Open standalone HTML directly, use the static ZIP for your own hosting, or install the local-service package. See the [CLI guide](cli.md) for CLI installation.

## Check file integrity

Compare each download's SHA256 with `SHA256SUMS.txt` from the same release:

```powershell
Get-FileHash -Algorithm SHA256 FILE
```

On Linux use `sha256sum FILE`; on macOS use `shasum -a 256 FILE`. Download again rather than installing incomplete, changed or mismatching files.

Checksums do not replace publisher signatures. When firmware becomes available, match the exact MCU, board and compatibility requirements, and retain both client and device signature checks. Never substitute another similarly named board's file.
