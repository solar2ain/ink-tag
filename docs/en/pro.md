# InkTag PRO features and activation

[中文](../zh-CN/pro.md) | English

InkTag BASIC already covers most everyday needs, including pictures, calendars, clocks and device management. PRO adds advanced and experimental features for users who enjoy tinkering and exploring. Activation cannot add absent hardware or unsupported functions.

## Feature comparison

| Feature | BASIC | PRO |
| --- | --- | --- |
| Pictures, built-in calendars and clocks | Available | Available |
| Non-E6 panel colours, orientation and ordinary full refresh | Available | Same |
| Advanced screen control | Not available | Partial refresh on selected screens and E6 control, with compatible hardware, wiring and drivers |
| Bluetooth, diagnostics, OTA and Recovery | Available | Available |
| Stored pictures and slideshows | Uses available storage | Same |
| Anti-loss tracker using Apple's Find My network | Not available | After activation and setup, query locations through Find My |
| NFC | — | Open the device link, write commands, or send an image where supported |
| Device passwords | Not available | Optional password; changing or disabling requires fresh old-password verification |
| Device labels | Editable | Editable |
| Bluetooth advertising name | Factory name | Editable within device rules |
| Display mark | Fixed `InkTag` | Adjustable or hideable `InkTag Pro` |
| Custom dynamic device layouts (planned) | — | Planned; not yet available. The current canvas sends static pictures |

Picture capacity depends on screen dimensions, colors, and available storage. nRF52811 and DA14585 offer BASIC only.

## Tracker and NFC requirements

| Feature | Requirements and limits |
| --- | --- |
| Anti-loss tracker | Uses Apple's Find My network, not real-time GPS. Location updates depend on nearby participating devices and network availability. Currently experimental and disabled by default; requires supported firmware, PRO and separate Find identity setup |
| Find queries | Require a suitable client/backend or local service, not a static Web page alone. Keep local private identity data secure |
| NFC interaction | Requires supported NFC hardware. Phone commands for display-by-picture-name and clearing are PRO device actions; supported commands vary by board |
| NFC connection | A supported NFC tag opens the device link. Bluetooth still requires browser permission |

## Using NFC

Supported PRO tags can open a device link, receive a text command, or accept an image over NFC. Get **D3-Pro InkTag** from the InkTag QQ group **418446983**. See the [NFC guide](nfc.md) for detailed steps.

## Get PRO firmware

Join the [InkTag QQ group 418446983](https://qun.qq.com/universal-share/share?ac=1&busi_data=eyJncm91cENvZGUiOiI0MTg0NDY5ODMiLCJ0b2tlbiI6ImRPVW00UnZxcWNKTU41M1JXZTQrRGpDeDRqTzI5UDkrTkdPb2U0Y1JGbE00MlcreFRuNnpkSU9QWkF3d01lTEEifQ%3D%3D&data=vETdxuw2M9SvyY0uJLAPuFrwhYCMPMCkYfiu-HzwB4-vWHpF2Jo9CtUXgTsNHzOrklunriznP8R1fytCHQWXpQ&svctype=4&tempid=h5_group_info) and see its announcements for the 0.2.2 PRO firmware and flashing instructions. Check the controller and the markings on your board before downloading. See [supported devices](supported-devices.md). nRF52811 and DA14585 offer BASIC only.

Follow the group announcements for activation. Read the current device code from device settings. Each activation code is bound to that device.

## Activate in the Web console

1. Connect the InkTag application and verify MCU, board and device identity.
2. Open Settings → Device settings → Activation and read the current device code. If activation is unsupported, do not substitute a code for another model.
3. Obtain the matching code through the official process and submit it on that same device's page. Complete password verification when requested.
4. Wait for device confirmation and read the activation status again. Once it reports PRO, configure the tracker or password as needed. NFC instructions appear under Interaction.

Keep the activation code secure; do not expose credentials in screenshots, logs or QR codes. Ordinary application OTA preserves activation. Save important data before first complete flashing or wired recovery. For failures, check the device/code match and the reported error rather than bypassing validation.

## Related guides

- [Devices and tested panels](devices.md)
- [Web console](webui.md)
- [Command-line client](cli.md)
- [OTA and Recovery](ota.md)

## Partial refresh and E6

Advanced screen control includes partial refresh on selected screens and E6 control. It requires PRO and compatible hardware, wiring and drivers. Clocks still perform daily full refresh/background changes, and frequent updates use more power. Offline E6 image conversion and preview do not require PRO.
