# InkTag Pro features and activation

[中文](../zh-CN/pro.md) | English

InkTag Free provides everyday display, Bluetooth connections, built-in calendars and clocks, and safe updates. Pro adds a lost-item locator, more image slots, NFC page switching and customization on activation-capable devices. Activation cannot add absent hardware or features omitted from firmware. This preview supplies clients, not firmware or flashing packages.

## Feature comparison

| Feature | Free | Pro |
| --- | --- | --- |
| Pictures, built-in calendars and clocks | Available | Available |
| Native panel colors, orientation and supported refresh modes | Available | The same |
| Bluetooth, diagnostics, OTA and Recovery | Available | The same; update and recovery safety do not depend on edition |
| Stored pictures and slideshows | Up to10 pictures below300px visible long edge, otherwise4, within actual capacity | No count quota within the available image-store space |
| Anti-loss tracker using Apple's Find My network | Not available | After activation and setup, query locations through Find My |
| NFC | Passive UID/device-link reads | Tap-to-change-page and supported short commands; all NFC device actions require Pro |
| Device passwords | Not available | Optional password; changing or disabling requires fresh old-password verification |
| Device labels | Editable | Editable |
| Bluetooth advertising name | Factory name | Editable within device rules |
| Display mark | Fixed `InkTag` | Adjustable or hideable `InkTag Pro` |
| NFC image transfer | Planned | Planned |
| Custom dynamic layouts | Planned | Planned; the current canvas sends static pictures |

Picture capacity still depends on geometry, color and Flash capacity. Pro cannot enlarge physical storage. nRF52811 and DA14585 are Free-only: their slots use actual storage capacity without the commercial quota of Pro-capable targets, and activation cannot add Pro features.

## Tracker and NFC requirements

| Feature | Requirements and limits |
| --- | --- |
| Anti-loss tracker | Uses Apple's Find My network, not real-time GPS. Location updates depend on nearby participating devices and network availability. Currently experimental and disabled by default; requires supported firmware, Pro and separate Find identity setup |
| Find queries | Require a suitable client/backend or local service, not a static Web page alone. Keep local private identity data secure |
| NFC interaction | Requires supported NFC hardware. Tap-to-change-page, display-by-picture-name and clear commands are Pro device actions; supported commands vary by board |
| Passive NFC | Free can still read UID/device links where supported, without device actions. Passwords and QR credentials are not written to NFC |

## Obtain an activation code

A donation-based Pro activation-code process is planned. Channels, eligibility and delivery will be announced on the [project page](https://github.com/solar2ain/ink-tag). No donation/code-request entry is currently open.

First confirm that the device supports Pro and provide the current device code requested by its activation page. Activation codes are bound to a specific device and cannot activate another. A donation cannot upgrade a Free-only target or guarantee unimplemented hardware/experimental features.

## Activate in the Web console

1. Connect the InkTag application and verify MCU, board and device identity.
2. Open Settings → Device settings → Activation and read the current device code. If activation is unsupported, do not substitute a code for another model.
3. Obtain the matching code through the official process and submit it on that same device's page. Complete password verification when requested.
4. Wait for device confirmation and read the activation status again. Once it reports Pro, configure the tracker, password or NFC interaction separately; activation does not automatically enable them.

Keep the activation code secure; do not expose credentials in screenshots, logs or QR codes. Ordinary application OTA preserves activation. Save important data before first complete flashing or wired recovery. For failures, check the device/code match and the reported error rather than bypassing validation.

## Related guides

- [Devices and tested panels](devices.md)
- [Web console](webui.md)
- [Command-line client](cli.md)
- [OTA and Recovery](ota.md)
