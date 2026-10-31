# Supported devices and board identification

[中文](../zh-CN/supported-devices.md) | English

Check the controller, board marking and fitted screen before choosing an installation package. Product names and screen sizes alone do not identify compatible hardware. Installation is not recommended where compatibility is still unconfirmed.

## GR5331

| Board marking | Product |
| --- | --- |
| `ZK_EL_GR5331_2.13_V1.1` | ZKC21V |

## GR5513

| Board marking | Product |
| --- | --- |
| `ZK_EL_GR42V_V1.2` | ZKC42VM |
| `ZK_EL_GR42V_V1.2` | ZKC42V-N (NFC fitted) |

## GR551x

| Board marking | Product |
| --- | --- |
| `ZK_EL_GR_2.13_V1.2` | ZKC21V (GR551x batch) |

## GR5515

| Board marking | Product |
| --- | --- |
| Confirm before installation | Early ZKC42VM |

## OM6626

| Board marking | Product |
| --- | --- |
| `ZK_EL_OM6626_2.13_V1.1` | ZKC21V-N (OM variant) |
| `ZK_EL_OM21BA_V1.1` | ZKC21B-NAE4 |
| `ZK_EL_OM42BA_V1.0` | ZKC42B-NE4 |

## TLSR8359 (HS9118)

Updated 2026-10-28. Identify the complete board marking and fitted panel; completed tests and remaining checks are listed separately.

| Board marking | Product | Original screen | Tested use and installation notes |
| --- | --- | --- | --- |
| HS_EL5101_5M_66_05 | Hanshow Nowa-154R-N | 1.54 inch, 152×152, three colours | Native pages, images, stored images and orientation tested; do not use a 200×200 board configuration |
| HS_EL5101_5M_60_04 | Hanshow Stellar-S3TN@ (product label not yet confirmed) | 1.54 inch, 200×200, three colours | P154030 assembly tested; the E0154A07-A1 assembly still needs confirmation |
| HS_EL5102_1M_67_33 | Hanshow Nowa-213R-N | 2.13 inch, 128×250, three colours | Calendar, native pages, images, storage and slideshow tested |
| HSEL4Q_02_13M_30 | Hanshow Nebular-213R-N | 2.13 inch, 128×250, four colours | Complete refresh, native pages and images tested; use a separate configuration from Nowa-213R-N |
| HSEL4Q_04_20M_33 | Hanshow Nebular-420R-N | 4.2 inch, 400×300, three or four colours | Use SSD1683 or JD79668AA according to the fitted panel |
| HSEL5_01_54M_00 | Hanshow StellarPro-154R-N, earlier version | 1.54 inch, 200×200, three colours, replaceable panel | JD79653A; orientation, colours, native pages, fonts, RGB and NFC tested |
| HSEL5_01_54M_02 | Hanshow StellarPro-154R-N, later version (YW2116 batch) | 1.54 inch, 200×200, three colours, fixed panel | Clock, calendar, device page, fonts, panel temperature, voltage display and phone NFC reading tested; use this board's separate screen profile |
| HSEL5Q_01_54M_00 | Hanshow StellarPro-154Q-N | 1.54 inch, 200×200, four colours | Basic communication, OTA and configuration tested; complete-panel image and colours still unconfirmed, so installation is not yet recommended |
| HSEL4Q_01_54M_31 | Hanshow NebularPro-154Q-N | 1.54 inch, 200×200, four colours | Original-panel native pages, Chinese fonts and digits visually confirmed; NFC tested; complete-flashing verification still needs completion |
| HSEL3_02_66M_36 | Hanshow PolarisPro-260Q-N | 2.66 inch, 152×296, four colours | Calendar, portrait images, user fonts and orientation tested; NFC and standby current need separate confirmation |

The earlier and later StellarPro-154R-N share a product name but use different boards: `_00` has a replaceable panel and `_02` has a fixed panel. Their panel drivers differ. Matching pins do not make their complete board or screen configurations interchangeable.

`E0154A07-A1` is not an established interchangeable replacement for the tested `P154030-MF1-A / P154030-0306` assembly. Check the product, board marking and FPC separately rather than choosing by 200×200 resolution alone.

The new TLSR firmware with startup signature verification is still being tested and is not yet a recommended installation. Existing display/function tests above do not replace startup and complete-flashing checks for that new version. See [tested screens](tested-screens.md#original-hanshow-screens) for panel coverage and partial refresh.

## nRF52832

| Board marking | Product |
| --- | --- |
| `PriceETag V1.7` | PriceETag |

## nRF52811

| Board marking | Product |
| --- | --- |
| `EL022HWRL` | AESL / Hema EL022H2 |

## DA14585

| Board marking | Product |
| --- | --- |
| Confirm before installation | AESL / Hema AESL-2.9 and AESL-4.2 |

The [detailed device table](../zh-CN/supported-devices.md) includes screen sizes and installation notes. Early GR5515 and some DA14585 boards need further identification before selecting an installation package.

ZKC21V and ZKC42VM have different hardware variants, and Nebular-420R-N can have different screens. Some boards share a marking with and without NFC; select the configuration matching the hardware fitted to your board.

This release includes WebUI, CLI, user guides and firmware for supported devices, including nRF52832, nRF52811 and DA14585. Firmware and complete-flashing tools are provided by controller and board. Use only actual [Release attachments](https://github.com/solar2ain/ink-tag/releases). Installation on TLSR boards with unconfirmed screen compatibility is not recommended.

See [tested screens and partial refresh](tested-screens.md), [device/screen details](devices.md), [complete flashing](flash.md), and [OTA/Recovery](ota.md). Hardware capability and the connected device's declarations determine which functions are available.
