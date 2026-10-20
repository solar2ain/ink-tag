# Devices and panels

Identify the exact MCU and board before selecting an installation package. The [support table](supported-devices.md) lists compatibility and available downloads. A single successful screen update does not mean every function is supported or tested.

## Board selection

| MCU manufacturer / model | Original label series / model | Public IDs | Difference |
| --- | --- | --- | --- |
| Goodix GR5331 | ZKC21V | `gr5331_zkc21v` | Not interchangeable with OM ZKC21V |
| Goodix GR5513 | ZKC42V / ZKC42VM (GR5513 revision) | `gr5513_zkc42v`, `gr5513_zkc42vm` | Different original-panel recipes |
| Goodix GR5515 | ZKC42VM (GR5515 revision) | `gr5515_zkc42vm` | Different complete-flashing container from GR5513; compatible App can be shared |
| OnMicro OM6626 | ZKC21V / ZKC21B-NAE4 / ZKC42B-NE4 | `om6626_zkc21v`, `om6626_zkc21b`, `om6626_zkc42b` | 21V BWR, 21B/42B BWRY; storage differs |
| Telink TLSR8359 (Nebular marking: HS9118) | Hanshow Nowa-154R-N / Nowa-213R-N / Nebular-213R-N / Nebular-420R-N | `tlsr8359_nowa154r`, `tlsr8359_nowa213r`, `tlsr8359_nebular213r`, `tlsr8359_nebular420r` | HS9118 identifies the marked Nebular package; geometry, selector and Flash capacity differ |
| Nordic Semiconductor nRF52832 | PriceETag | `nrf52832_priceetag` | Not the nRF52811 firmware target |
| Nordic Semiconductor nRF52811 | Alibaba Cloud AESL / Hema EL022H2 | `nrf52811_asel22` | Tested PCB is EL022H2, not EL029H2 |
| Renesas (formerly Dialog) DA14585 | Alibaba Cloud AESL / Hema AESL-2.9 / AESL-4.2 | `da14585_asel29`, `da14585_asel42` | Shared App; different complete-flashing panel defaults |

A complete-flashing image can contain boot, Recovery, fonts and board configuration. Shared OTA bytes do not make these images interchangeable. Historical `aesl22_nrf` / `aesl29_da` names are not current public download IDs.

## Original panels

BW is black/white, BWR adds red, BWRY adds yellow, and KWRYGB means black/white/red/yellow/green/blue. Match IC, recipe, native geometry, visible area and orientation.

| PCB / panel | Driver / adaptation | Native / visible geometry | Colors |
| --- | --- | --- | --- |
| GR5331 / OM ZKC21V | SSD1680 / zkc21v | 128×250 / 122×250 | BWR |
| GR5513 ZKC42V | UC8276 / zkc42v | 400×300 | BWR |
| GR5513/5515 ZKC42VM | SSD1683 / zkc42vm | 400×300 | BWR |
| OM ZKC21B | JD79661 / zkc21b | 128×250 / 122×250 | BWRY |
| OM ZKC42B | JD79668AA / zkc42b | 400×300 | BWRY |
| Nowa154R | SSD1675 / nowa154r | 152×152 | BWR |
| Nebular213R | JD79676 / nebular213r | 128×250 / 122×250 | BWRY |
| Nebular420R | JD79668AA / nebular420r | 400×300 | BWRY |
| Tested PriceETag | SSD1675 | 152×152 | BW |
| nRF52811 Hema | SSD1619 | 128×250 / 122×250 | BWR |
| DA asel29 E029A33 | SSD1619 / asel29 | 128×296 | BWR |
| DA asel42 original | UC8176 | 400×300 | BWR |

For Nowa213R or unlisted panels, use actual device settings and release instructions. Disconnect power before a swap and verify FPC, wiring and colors. A preset in a dropdown does not prove testing on every PCB. DA 128×296 landscape is only 128 pixels high and uses a compact layout. A ZKC42VM replacement on DA requires SSD1683, not the original UC8176 recipe.

## Actual test scope

| Board | Tested operations | Not established |
| --- | --- | --- |
| GR5331 ZKC21V | Original clear/calendar/pictures and direction/BWR output | Arbitrary 128×250 replacements |
| GR5513 ZKC42V | Original clear/calendar; Huawei A0/A1 pages and pictures | Equivalent coverage on GR5515 |
| GR5515 ZKC42VM | Original panel and selected replacement clear results | All replacement pages/colors/repeated refreshes |
| nRF52832 PriceETag | SSD1675 152×152 BW pages and pictures | Equivalent 200×200 testing |
| nRF52811 EL022H2 | Hema pages/pictures/orientation/panel temperature; Huawei A0 pages/pictures/orientation/colors; A1 pictures/orientation/colors/slideshow | EL029H2, 400×300 candidates or A0/A1 temperature readback |
| DA AESL42 | SES UC8151 104×212 BWR pages/pictures/slots/slideshow | All SES models or temperature readback |

Equal dimensions do not imply equal voltage, waveforms, BUSY polarity or transfer format. Advanced configuration does not automatically identify an unknown IC.

## Image capacity

Storage includes the decoded full frame, header and sector alignment, not the JPEG/PNG size. RLE saves transfer bytes, not Flash capacity. Device-reported IDs and capacity are authoritative.

| Panel | nRF52811 total 472 KiB | DA total 68 KiB |
| --- | --- | --- |
| 128×250 BWR | 8 KiB/slot, about 59 | 8 KiB/slot, about 8 |
| 128×296 BWR | Follow actual settings | 12 KiB/slot, about 5 |
| 400×300 BWR/BWRY | 32 KiB/slot, about 14; capacity reference, not a tested-panel claim | 32 KiB/slot, about 2 |
| Huawei A0/A1 768×552 BWRY | 104 KiB/slot, about 4; replacement tests exist | Frame exceeds storage: no saved slots or autonomous slideshow |

DA large panels can use the corresponding direct-transfer path. Image Flash cannot enlarge executable SRAM. Other MCUs have board-specific storage; changing geometry/colors can invalidate old slots. Download needed content first, then clear and re-upload as prompted.

## Temperature and capabilities

Panel and MCU temperatures have different sources. Missing, stale, busy or unsupported samples appear as `--`, not 0 °C. A0/A1 panel temperature on nRF52811 is not physically tested; DA availability also depends on controller and sampling state.

nRF52811 and DA14585 are permanently Free without activation, Find, passwords, NFC actions or Bluetooth renaming. LEDs/buttons/buzzers on other families depend on fitted hardware and actual capabilities. Labels use printable English/ASCII characters; default `InkTag XXXX` is separate from advertising.
