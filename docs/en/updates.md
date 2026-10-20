# Client updates

[中文](../zh-CN/updates.md) | English

View the version and check for updates under Settings → Client settings → About. WebUI, CLI and device firmware versions are independent; updating a client does not update a device.

## Download and install

1. Read the release notes and download the required attachment from [GitHub Releases](https://github.com/solar2ain/ink-tag/releases). If none is listed, that version is not available for download.
2. Follow [downloads and checksums](releases.md) and choose your client.
3. Hosted WebUI: reopen the page for the deployed version. Standalone HTML: download and open the new file. Local service: stop the old service, update as described in [WebUI](webui.md), then restart using the same data directory.
4. CLI: update in its existing isolated environment using the [CLI guide](cli.md), then check `inktag --version`.

This release supplies no firmware or wired flashing packages. When a matching OTA package becomes available, follow [wireless updates](ota.md); never substitute another MCU or board's file. Checksums check file integrity, not publisher signatures.

## If checking fails

A timeout or network failure does not prevent connections or ordinary use. Retry later or check Releases directly; use the local client if the hosted site is unavailable. No newer version does not mean every device supports identical features.

Update checks send no device address, password, identity or Find data. Keep your local data and error details when troubleshooting; do not delete the data directory or reflash a device to fix a client update.
