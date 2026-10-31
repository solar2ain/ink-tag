# Tested screens and partial refresh

[中文](../zh-CN/tested-screens.md) | English

This page lists screens with physical InkTag test results. Use it to choose a screen profile alongside [supported boards](supported-devices.md) and the [screen replacement guide](devices.md).

**Matching size, resolution or connector does not establish interchangeability.** Check the panel marking, colours, board wiring and exact driver profile. A driver appearing in Settings does not mean every panel using that controller has been tested.

## Reading the partial-refresh column

- **Clock partial refresh** means updating the clock-digit region on the listed supported board/panel/profile combination. It requires activated Pro and does not provide arbitrary image-region updates.
- **Not offered** means current InkTag does not offer partial refresh for that combination. It does not establish a theoretical controller limitation.
- **Not verified** means existing tests do not establish working partial refresh.
- **Available, visual result not fully checked** means the clock function exists but the exact board/panel's image and ghosting have not been completely checked.

The partial-refresh clock still performs a daily full refresh and changes its background. First display, maintenance and state changes may also need full refresh. Frequent updates use more power than occasional image changes.

## Original ZKC screens

| Panel / product | Size and resolution | Colours | Driver profile | Physically tested use | Partial refresh |
| --- | --- | --- | --- | --- | --- |
| GR5331 ZKC21V original | 2.13 inch, 128×250 | Black/white/red | SSD1680, ZKC21V | Calendar, clock and images; clock-region updates visually confirmed | Clock partial refresh, Pro required; some samples retain faint grey ghosting |
| Small GR5513 ZKC21V, MF213M0001 / MOFANG M001-4 | 2.13 inch, 128×250 | Black/white/red | SSD1680, ZKC21V | Minute updates, recovery after midnight full refresh, images, fonts and slideshow tested | Clock function available with Pro; visual result not fully checked |
| OM ZKC21V-N original | 2.13 inch, 128×250 | Black/white/red | SSD1680 driver, ZKC21V | Readable calendar and device page; complete display edges and stored-image display still need confirmation | Not offered |
| ZKC42V, HE042AD7A2 / HINK-E042A48-A1 | 4.2 inch, 400×300 | Black/white/red | UC8276, ZKC42V | Clear, calendar, clock and device page on GR5513 | Not offered |
| ZKC42VM, HE042A55 / HINK-E042A13-FPC-A0 | 4.2 inch, 400×300 | Black/white/red | SSD1683, ZKC42VM | Original-panel full refresh | Not offered |
| ZKC21B, HINK-E0213A349 | 2.13 inch, 128×250 | Black/white/red/yellow | JD79661 driver, ZKC21B | Clear, calendar, clock and image tests on OM6626; top-edge display still needs complete confirmation | Not offered |
| ZKC42B original | 4.2 inch, 400×300 | Black/white/red/yellow | JD79668AA driver, ZKC42B | Images and stored-image display on OM6626, with calendar Chinese text, dates and digits visually confirmed | Not offered |

Some 128×250 panels have a visible width of 122 pixels. The client handles this automatically; do not choose a different controller because of that difference.

## Original Hanshow screens

The different 200×200 panels below need separate profiles. SSD1681 is the display name for the compatible command path; the exact silicon is not independently identified. “JD family” also leaves the exact IC unconfirmed. Match the specific panel or board.

| Panel / identifying mark | Size and resolution | Colours | Driver profile | Physically tested use | Partial refresh |
| --- | --- | --- | --- | --- | --- |
| Nowa-154R-N, FPC-8002, HS_EL5101_5M_66_05 | 1.54 inch, 152×152 | Black/white/red | SSD1675, Nowa 154R | TLSR8359: clear, native pages, images and stored images | Not offered |
| P154030-MF1-A / P154030-0306, HS_EL5101_5M_60_04 | 1.54 inch, 200×200 | Black/white/red | SSD1681 profile for P154030 | TLSR8359: clear, calendar, images, native pages and orientation; use this panel's separate profile | Not offered |
| StellarPro-154R-N earlier replaceable-panel version, FPC-5301 / DEPG0154RWF653CF, HSEL5_01_54M_00 | 1.54 inch, 200×200 | Black/white/red | JD79653A, Stellar 154 | TLSR8359: orientation, colours, user fonts, clock, calendar and device page | Not offered |
| StellarPro-154R-N later fixed-panel version, HSEL5_01_54M_02 (YW2116 batch) | 1.54 inch, 200×200 | Black/white/red | SSD1681-compatible driver, separate HSEL5_01_54M_02 profile; exact IC unconfirmed | TLSR8359: clock, device page, calendar, user fonts, panel temperature and voltage display; fixed original panel | Not offered |
| NebularPro-154Q-N, HSEL4Q_01_54M_31 | 1.54 inch, 200×200 | Black/white/red/yellow | JD family, separate HSEL4Q_01_54M_31 profile; exact IC unconfirmed | TLSR8359: calendar, clock, device page and four font roles tested; Chinese, English and time digits visually confirmed | Not offered |
| Nowa-213R-N, P213124-MF1-A | 2.13 inch, 128×250 | Black/white/red | UC factory profile, Nowa 213R | TLSR8359: calendar visually confirmed; stored images and page operations tested | Not offered |
| Nebular-213R-N, AN0213A250-122KS682 / HS-E2.13ME-V1.0-M | 2.13 inch, 128×250 | Black/white/red/yellow | JD79676, Nebular 213R | TLSR8359: full refresh, native pages and images | Not offered |
| PolarisPro-260Q-N, HSEL3_02_66M_36 | 2.66 inch, 152×296 | Black/white/red/yellow | JD family, HSEL3_02_66M_36 | TLSR8359: calendar, portrait images, user fonts and orientation | Not offered |
| Nebular-420R-N, YW2520 four-colour assembly | 4.2 inch, 400×300 | Black/white/red/yellow | JD79668AA, Nebular 420R | TLSR8359 original panel; clear, calendar and four-colour images visually confirmed on GR5513 replacements | Not offered |
| Nebular-420R-N, YW2427 three-colour assembly | 4.2 inch, 400×300 | Black/white/red | SSD1683 | Original-panel display on TLSR8359; separate from the four-colour assembly above | Not offered |

The P154030 client profile is named SSD1681; it does not cover every similar 200×200 connector. HSEL5_01_54M_00 and _02 are the replaceable- and fixed-panel versions and require different profiles. The E0154A07-A1 assembly remains unconfirmed and is not merged into the tested P154030 entry. GR5515 replacement tests with the Nebular four-colour panel currently establish clear-only operation.

## Huawei 3.98-inch four-colour screens

| Panel | Resolution | Colours | Driver profile | Physically tested use | Partial refresh |
| --- | --- | --- | --- | --- | --- |
| Huawei A0 | 768×552 | Black/white/red/yellow | JD79665AA, Huawei A0 | GR5513: clear, calendar and four-colour images; TLSR8359: calendar and images; nRF52811: pages, images and orientation | Not offered |
| Huawei A1 | 768×552 | Black/white/red/yellow | JD79665AA, Huawei A1 | GR5513: clear, calendar and four-colour images; GR5331: clear and calendar; TLSR8359: calendar, device page and images; nRF52811: images, orientation and slideshow | Not offered |

Choose the separate A0 or A1 profile. Related GR5515 replacements currently have clear-only results, not the complete display coverage above. Direct display does not guarantee that a board can store the image; DA14585, for example, has insufficient image storage for a complete four-colour frame at this resolution.

## E6 six-colour screens

None of these profiles offers clock partial refresh. Physical control requires Pro; six-colour conversion and offline preview remain free.

| Panel / identifying mark | Size and resolution | Driver profile | Physically tested use and limits | Partial refresh |
| --- | --- | --- | --- | --- |
| Osprey HT0154A10 / HINK-T0154A10-A1 | 1.54 inch, 240×240 | IST7513, HT0154A10 | TLSR8359: image transfer, calendar and panel temperature reported working | Not offered |
| Osprey HT0169A06 / HINK-T0169A06-A0 | 1.69 inch, 400×400, circular visible area | IST7601, HT0169A06 | TLSR8359: Pro clear, calendar and direct image uploads tested, with panel temperature readings; Free screen control is refused. Overall visual result, ghosting and long-term power still need confirmation | Not offered |
| Osprey HT037A02 / HINK-T037A02-A1 | 3.7 inch, 720×480 | IST7601 / JD7601, HT037A02 | Display reported working; orientation and calendar had earlier issues, and cold-start/standby consumption still needs confirmation | Not offered |
| H364F067-2601A | Approximately 3.64 inch, 760×568 | JD79665CA, H364F067 E6 | TLSR8359: calendar, device page, image transfer and stored-image playback tested; visible edges, complete colour result and power still need confirmation | Not offered |

**When using an Osprey E6 screen on an ordinary price-tag board, insulate connector pin16 with tape so it stays disconnected.** Otherwise the screen may heat up and draw excessive current. Power off first and verify connector orientation. Do not copy this wiring change to other screens with different connectors.

## Other tested screens

| Panel / product | Size and resolution | Colours | Driver profile | Physically tested use | Partial refresh |
| --- | --- | --- | --- | --- | --- |
| PriceETag original | 1.54 inch, 152×152 | Black/white | SSD1675 | Pages and images on nRF52832 | Not offered |
| Hema EL022H2 original | 2.13 inch, 128×250 | Black/white/red | SSD1619 | Pages, images, orientation and panel temperature on nRF52811 | Not offered |
| SES SE2213FS093 / A1360067-00 | 2.13 inch, 104×212 | Black/white/red | UC8151, SES 2.13 | Clear, calendar, clock, images and slideshow on a DA14585 replacement setup | Not offered |
| AESL-4.2 original | 4.2 inch, 400×300 | Black/white/red | UC8176 | Complete-page refresh; a replacement ZKC42VM panel needs SSD1683 instead | Not offered |
| SSD1677 four-grey screen | 4.26 inch, 800×480 | Four grey levels | SSD1677 | Clear, calendar and four-grey images visually confirmed on GR5513 and GR5515 | Not offered |

## Screens not yet confirmed fully working

- **StellarPro-154Q-N, HSEL5Q_01_54M_00, 1.54 inch 200×200 four-colour**: basic communication, OTA, configuration and screen commands were tested, but complete-panel images and colours are not yet confirmed. NebularPro-154Q results do not establish this panel's behaviour.
- **SES SE2266FS093, 2.66 inch, 152×296 black/white/red:** a calendar was displayed, but BUSY subsequently timed out. It is not listed as normally supported.
- **AESL-2.9, E029A33, 128×296:** an SSD1619 adaptation exists, but this page does not claim a verified complete display result.
- **Other Hanshow 200×200 four-colour originals:** HSEL3 or same-size three-colour results do not establish support; confirm the specific screen/profile.

## Before using a replacement

- Results apply to the listed board/panel combinations, not every production batch or replacement board.
- Follow the connected device's actual partial-refresh support. Changing the panel, orientation, wiring or fonts may invalidate it.
- Physical E6 six-colour control requires Pro. Offline image conversion and preview do not.
- Power off before changing screens and check the connector and supply. Stop if a panel heats up, draws unusual current or fails to refresh.

See [WebUI](webui.md) for profile and image controls, and [supported devices](supported-devices.md) for boards and installation packages.
