# Devices and panels

Check the controller, board marking and screen before installation. The [supported-device list](supported-devices.md) covers products and downloads, and [tested screens](tested-screens.md) lists physical results and partial refresh. This guide explains screen settings, image capacity and replacement-screen limits.

## Choose a board and installation package

- Match the controller and board marking in the [device list](supported-devices.md). A product name can cover different hardware revisions.
- Some boards share a marking with and without NFC. Select the configuration matching the hardware fitted to your board.
- Complete flashing also installs boot/recovery software and initial screen/pin settings. A shared OTA application does not make complete-flashing packages interchangeable.
- If the screen has been replaced, check its controller, colours, resolution and wiring.

## Original panels

BW is black/white, BWR adds red, BWRY adds yellow, and KWRYGB means black/white/red/yellow/green/blue. Match IC, recipe, native geometry, visible area and orientation.

### GR

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| GR5331 ZKC21V | SSD1680 / zkc21v | 128×250 / 122×250 | BWR |
| GR5513 ZKC42V | UC8276 / zkc42v | 400×300 | BWR |
| GR5513/5515 ZKC42VM | SSD1683 / zkc42vm | 400×300 | BWR |

### OM6626

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| OM ZKC21V | SSD1680 / zkc21v | 128×250 / 122×250 | BWR |
| OM ZKC21B | JD79661 / zkc21b | 128×250 / 122×250 | BWRY |
| OM ZKC42B | JD79668AA / zkc42b | 400×300 | BWRY |

### TLSR8359

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| Nowa154R | SSD1675 / nowa154r | 152×152 | BWR |
| Nebular213R | JD79676 / nebular213r | 128×250 / 122×250 | BWRY |
| Nebular420R | JD79668AA / nebular420r | 400×300 | BWRY |

### nRF52832

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| Tested PriceETag | SSD1675 | 152×152 | BW |

### nRF52811

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| nRF52811 Hema | SSD1619 | 128×250 / 122×250 | BWR |

### DA14585

| Board / panel | Driver / screen profile | Resolution / usable display area | Colors |
| --- | --- | --- | --- |
| DA asel29 E029A33 | SSD1619 / asel29 | 128×296 | BWR |
| DA asel42 original | UC8176 | 400×300 | BWR |

For Nowa213R or unlisted panels, use actual device settings and release instructions. Disconnect power before a swap and verify FPC, wiring and colors. A preset in a dropdown does not prove testing on every PCB. DA 128×296 landscape is only 128 pixels high and uses a compact layout. A ZKC42VM replacement on DA requires SSD1683, not the original UC8176 recipe.

## Replacement-screen compatibility

### GR5331 ZKC21V

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| GR5331 ZKC21V | Original clear/calendar/pictures and direction/BWR output | Other 128×250 replacements need separate compatibility checks |

### GR5513 ZKC42V

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| GR5513 ZKC42V | Original clear/calendar; Huawei A0/A1 pages and pictures | GR5515 replacement compatibility must be checked separately |

### GR5515 ZKC42VM

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| GR5515 ZKC42VM | Original panel; clear is confirmed on selected Nebular/Huawei replacements | Replacement pages, colours and repeated refreshes are not all confirmed |

### nRF52832 PriceETag

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| nRF52832 PriceETag | SSD1675 152×152 BW pages and pictures | 200×200 compatibility is not confirmed |

### nRF52811 EL022H2

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| nRF52811 EL022H2 | Hema pages/pictures/orientation/panel temperature; Huawei A0 pages/pictures/orientation/colors; A1 pictures/orientation/colors/slideshow | EL029H2, other 400×300 replacements and A0/A1 temperature readback are not confirmed compatible |

### DA AESL42

| Device | Supported screens and operations | Limits |
| --- | --- | --- |
| DA AESL42 | SES UC8151 104×212 BWR pages/pictures/slots/slideshow | Other SES models and temperature readback are not confirmed compatible |

Equal dimensions do not imply equal voltage, waveforms, BUSY polarity or transfer format. Advanced configuration does not automatically identify an unknown IC.

## Image capacity

The table below estimates storage for **250×128 (also 128×250), 400×300 and 3.98-inch 768×552 images at 2 bits per pixel (2bpp)**. E6 uses two 2bpp stages, totaling **4bpp**; both larger E6 sizes, 720×480 and 760×568, are listed separately.

Saved images use a full decoded frame, a 96-byte record header and 4 KiB alignment. The approximate space per image is:

| Image size | Color data | Space per image, including header and alignment |
| --- | --- | ---: |
| 250×128 / 128×250 | 2bpp | 8 KiB |
| 400×300 | 2bpp | 32 KiB |
| 3.98-inch 768×552 | 2bpp | 104 KiB |
| E6 3.7-inch 720×480 | 4bpp | 172 KiB |
| E6 3.64-inch 760×568 | 4bpp | 212 KiB |

The counts below show **storage capacity before Free-edition quotas**. On Pro-capable controllers, Free normally allows 10 pictures when the visible long edge is below 300 pixels, or 4 otherwise. **DA14585 and nRF52811 are Free-only due to firmware-size limits, cannot activate Pro, and have no such count quota.** Storage still has a physical limit. E6 control requires Pro and is unavailable on these two controllers.

1 KiB = 1,024 bytes. `0` means that a full image cannot be saved; `—` means E6 is not supported. This is a capacity comparison, not confirmation that every listed screen works on every board; see [tested screens](tested-screens.md).

### GR5331

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standard | 80 KiB | 10 | 2 | 0 | 0 | 0 |

### GR5513 / GR5515

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GR5513 internal image storage | 64 KiB | 8 | 2 | 0 | 0 | 0 |
| GR5513 external image storage / larger GR5515 configuration | 512 KiB | 64 | 16 | 4 | 2 | 2 |

### OM6626

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Without external image Flash | 484 KiB | 60 | 15 | 4 | 2 | 2 |
| ZKC42B with its 2 MiB external image Flash available | 2,532 KiB (484 + 2048) | 255 | 79 | 23 | 13 | 11 |

### TLSR8359

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Base image storage | 136 KiB | 17 | 4 | 1 | 0 | 0 |
| Nebular-420R-N with extended image storage enabled | 648 KiB (136 + 512) | 81 | 20 | 5 | 2 | 2 |

### nRF52832

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standard | 128 KiB | 16 | 4 | 1 | 0 | 0 |

### nRF52811

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| EL022H2 | 472 KiB | 59 | 14 | 4 | — | — |

### DA14585

| Configuration | Available image storage | 250×128 | 400×300 | 768×552 | E6 720×480 | E6 760×568 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| AESL family | 68 KiB | 8 | 2 | 0 | — | — |

Each separate storage area is rounded down independently; an image cannot span two Flash areas. The current image-ID limit is 255, so the ZKC42B estimate for 250×128 stops at 255 even if space remains.

These sizes refer to image storage only, not total Flash or App/OTA space. Available space can differ with the board, installed firmware and whether external Flash is available. Follow the capacity reported by the connected device. The estimates assume all saved images use the selected size; row padding, orientation and color format can change the actual count. RLE reduces transfer bytes, not stored full-frame size; the original JPEG/PNG file size does not determine slot use.

Some large screens can be displayed directly without enough room for saved slots or an autonomous slideshow. When changing screen settings, download pictures you want to keep, then clear incompatible images and upload again as prompted.

## Temperature and capabilities

Panel and MCU temperatures have different sources. Missing, stale, busy or unsupported samples appear as `--`, not 0 °C. A0/A1 panel temperature on nRF52811 is not physically tested; DA availability also depends on controller and sampling state.

nRF52811 and DA14585 are permanently Free without activation, Find, passwords, NFC actions or Bluetooth renaming. LEDs/buttons/buzzers on other families depend on fitted hardware and actual capabilities. Labels use printable English/ASCII characters; default `InkTag XXXX` is separate from advertising.
