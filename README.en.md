# InkTag Releases

`solar2ain/ink-tag` is the private-first distribution repository. Python CLI source is explicitly allowed; firmware source, uncompiled WebUI source, vendor SDKs, signing keys and factory backups are prohibited. Chinese documentation is the default, with English documentation alongside it. A qualified compiled WeChat mini-program is not yet available.

[简体中文](README.md)

InkTag Releases contains the InkTag CLI, Web UI, optional firmware assets, and user documentation. Firmware source code and vendor SDKs are not included.

> This repository is currently a private preview. It must pass per-board hardware regression, OTA rollback testing, and third-party licensing review before becoming public.

## What this release contains

A release may contain only the CLI, WebUI, or firmware for explicitly selected and qualified platforms. Read
[`firmware/manifest.json`](firmware/manifest.json) first: `components` lists the release components, and an empty
board `artifacts` array means that no firmware for that board is published. Board directories under `firmware/`
may be documentation-only placeholders and are not flashable files.

## Firmware downloads

Published firmware binaries are attached to GitHub Releases instead of being committed to Git history. Always choose the exact matching MCU and board:

- `InkTag-<board>-v<version>-full.bin|hex|zip`: first installation or recovery; replaces the installed firmware. An OM6626 ZIP contains the three HEX inputs used in the fixed qualified order.
- `InkTag-<board>-v<version>-ota.zip`: update package for InkTag Web.
- `InkTag-flash-tools-v<version>.zip`: Windows first-flash entry points; vendor tools are still installed separately as documented.
- `SHA256SUMS.txt`: integrity checks for downloaded files.

Never flash firmware for a similar-looking but different board. See [supported boards](docs/en/supported-devices.md).

## Start here

- `webui/`: content prepared for GitHub Pages. During the private preview, download
  `InkTag-WebUI-standalone-*.html` from the matching GitHub Release; enable the hosted
  site after the repository is public or the account supports private Pages
- `webui/manual/`: bilingual user manuals deployed with the WebUI on GitHub Pages
- [Web console user guide](docs/en/webui.md)
- [Full flashing](docs/en/flashing.md)
- [OTA updates](docs/en/ota.md)
- [Command-line client](docs/en/cli.md)
- [Frequently asked questions](docs/en/faq.md)
- [Versioning and releases](docs/en/releases.md)
- `flash/windows/`: one-command entry points for the Goodix, OM6626, and Telink candidate boards; scripts verify the Release manifest and SHA-256 first

## Safety

Back up the factory firmware before the first flash. Verify the board, supply voltage, SWD/serial wiring, and target firmware. A wrong full image may prevent booting or damage peripherals.

GitHub Pages configuration and activation: [GitHub Pages](docs/en/github-pages.md).
