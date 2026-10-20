# InkTag

[中文](README.md) | English

InkTag is low-power display firmware and management system software for electronic shelf labels across several MCU platforms. It turns e-paper into a calendar, clock or picture display for desk signs, photographs and everyday reminders. InkTag WebUI and the shared Python CLI configure screens, manage images and update firmware. Configured devices do not need a continuous computer connection.

E-paper uses ambient light for a paper-like appearance without a continuously illuminated backlight.

## Features

| Feature | Free | Pro | Notes |
| --- | :---: | :---: | --- |
| Pictures, calendar, clock, QR and device information | ✓ | ✓ | Subject to actual device capabilities |
| Image storage | ✓ | ✓ | Free: up to10 images below300px visible long edge, otherwise4. Pro has no count quota within the available image-store space. Image size and physical capacity also limit availability |
| Slideshows | ✓ | ✓ | Stored images can cycle without a continuous BLE connection |
| Screen and pin configuration | ✓ | ✓ | Profile, orientation and supported display/LED routes |
| Canvas and local weather preview | ✓ | ✓ | Editable controls, image dithering, scene export and host rendering |
| LEDs, buttons and buzzer | ✓ | ✓ | Button and buzzer actions are not yet implemented; configuration does not establish support |
| Updates and Recovery | ✓ | ✓ | Requires an already installed compatible InkTag image |
| Passive NFC connection link | ✓ | ✓ | Requires fitted NFC; initial browser authorization is still required |
| Hide the InkTag mark | — | ✓ | Free has a fixed mark; Pro can adjust or hide it |
| Lost-item locator using Apple's Find My network | — | ✓ | Compatible firmware and identity setup required; not real-time GPS or an Apple-certified client |
| Switch pages through NFC | — | ✓ | Requires supported hardware and firmware |
| NFC image transfer | — | Planned | Not available |
| Custom dynamic layouts (device-side) | — | Planned | Not available; host weather sends ordinary images rather than executing layouts on the device |
| Advertising-name changes, passwords and more | — | ✓ | Subject to device capabilities |
| Scheduled host-weather delivery | — | ✓ | CLI data lookup and delivery; keep the host online, actual targets still require device validation |

The independent nRF52811 and DA14585 projects are Free-only, without these commercial image quotas. Their firmware is not supplied here. Activation cannot add missing hardware. See [Pro features](docs/en/pro.md). Donation-based activation-code distribution is planned; no donation channel or redemption policy is currently announced.

## E-paper characteristics

- **Paper-like viewing:** reflected ambient light, with external illumination needed in the dark. Eye-friendly is not a medical guarantee; use suitable lighting, distance and breaks.
- **Slow, flashing refresh:** a full refresh commonly takes several to around fifteen seconds, and can take longer on colour panels or in the cold. Keep power stable and wait.
- **Image retention without sustaining power:** the finished panel image normally remains without continuous power, even when disconnected. The MCU, Bluetooth and clock still consume energy.
- **Suitable for infrequent updates:** low-power standby and infrequent refreshes can give a coin cell a long service life. Actual life depends on the battery, temperature, refresh rate and radio activity.

Read more: [Good Display introduction](https://www.good-display.com/faq/1.html) | [中文版](https://www.good-display.cn/news/212.html).

## Getting started

1. Prepare a device already running compatible InkTag firmware and stable power.
2. Open WebUI in a Web Bluetooth-capable Chrome or Edge browser, choose Connect and select the actual advertised name.
3. Check the MCU and screen settings, import a picture or choose a template, then send and wait for display completion.
4. Devices with image storage can save images and run slideshows after disconnecting.

Factory devices require matching firmware first. This preview supplies no first-flash files: do not use unknown or cross-MCU images. See the [getting-started guide](docs/en/getting-started.md).

## Installation and use

| Client | Installation | Guide |
| --- | --- | --- |
| Online WebUI | Use [InkTag Web](https://solar2ain.github.io/ink-tag/) when published; inspect the supplied local package for this preview | [WebUI](docs/en/webui.md) |
| Local WebUI / Find service | Python3.11+. Extract the server package; Windows: `install.cmd`; macOS: `install.command`; Linux: `sh install.sh` | Windows Find requires full7-Zip and initial runtime downloads. Native Mac authentication awaits actual-Mac validation; Linux Find login is unqualified |
| Python CLI | Extract the CLI package and install its isolated environment as described inside. One source package serves Windows, Linux and macOS | [CLI](docs/en/cli.md) |

The local service defaults to port8787 and keeps data in the current user's persistent InkTag directory. Restarting does not require re-enrolling tags. Do not delete this directory to fix login errors. Enter Apple credentials only in the local interface and use a separately registered account rather than your everyday personal account.

With an existing Python virtual environment, run `python -m pip install .` inside the extracted CLI package.

## User manuals

[Getting started](docs/en/getting-started.md) · [WebUI](docs/en/webui.md) · [CLI](docs/en/cli.md) · [Adapted devices](docs/en/supported-devices.md) · [Pro features](docs/en/pro.md) · [Client updates](docs/en/updates.md) · [FAQ](docs/en/faq.md)

Read [security and privacy](SECURITY.md). Never include passwords, activation codes, private locations or keys in issue reports.

See [GitHub Pages](docs/en/github-pages.md) for future hosting configuration and limitations. This update changes the private repository only; it does not deploy the site.

## License

InkTag-owned CLI source is licensed under AGPL-3.0-only. WebUI is distributed in compiled form only and is not covered by the CLI license. See [licensing scope](LICENSING.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
