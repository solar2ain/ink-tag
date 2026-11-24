# InkTag firmware families and board markings

## Current organization

This directory contains the board registry and the shared family headers
used by the firmware build. Nordic nRF52811 and Dialog DA14585 are maintained
here alongside the other InkTag BASIC firmware targets.

| Compatible firmware target | Description |
|---|---|
| TLSR8359 | `tlsr8359_hsel.h` |
| GR5331 | `gr5331_zkel.h` |
| Shared GR5513/GR5515 | `gr551x_zkel.h` |
| GR5513 Internal | `gr551x_zkel.h` with the internal image storage build profile |
| OM6626 ZK | `om6626_zkel.h` |
| nRF52832 PriceETag | `nrf52832_priceetag.h` |
| nRF52811 AESL | `nrf52811_aesl.h` |
| DA14585 AESL | `da14585_aesl.h` |

Goodix-specific pin rules live in [the Goodix port](../ports/goodix/common/src/).
The programmer reads `goodix-pin-backends.json` to check the board preset.
The App and Recovery use `goodix_pin_backend_facts.h` to validate pin records
on the device. See the [firmware build guide](../../docs/build-firmware.en.md)
for the SDK and compiler used by each family.

Select a bundled programmer preset by the exact board marking.
`board_marking` maps to the product name and existing `board_id`. The
programmer checks the family and writes the configuration in its existing
format. Use the matching board directory in the [flashing guide](../../docs/en/flash.md).
Numeric selectors stay numeric where already implemented. A single-backend
target retains its native existing identifier; no synthetic zero ID or new
record is needed. An existing alias without verified silkscreen must be labelled
as such, not presented as an exact marking or newly qualified hardware.
For duplicate physical markings, use controlled configuration aliases instead
of a second user field. GR5513 `ZK_EL_GR42V_V1.2_w_NFC` selects existing
backend2; `ZK_EL_GR42V_V1.2` selects existing backend3 without NFC. Both entries
retain actual silkscreen `ZK_EL_GR42V_V1.2` as evidence. The suffixed preset name
is not new printing, a runtime NFC switch or a new Flash field; a bare observed
marking/probe/backup label does not authorize either backend or Full.

The family descriptor keeps separate Flash/NFC/ADC/clock/power and protected-net
facts for its supported `board_id` values. Supported screen/LED and qualified
optional IO defaults remain configurable data. A shared executable does not
qualify sibling peripherals, turn an unknown selector into a known board or
allow guessed wiring. Preserve current numeric values, factory bytes, record
formats and OTA compatibility identities during consolidation.

The three layers remain product-common semantics, vendor-common SDK/bus
operations and firmware-family hardware descriptions. Product/marking mappings
and `boards.json` remain registry data, not reasons to add more App targets.
Instantiate each descriptor table once and retain existing tested drivers.

## Historical identities and hardware evidence

The following associations retain their recorded names and evidence. Their old
directory paths and artifact labels describe the previous organization, not a
requirement to keep one header or executable per product. Apply the current
contract above when migrating live code; do not rewrite original backup paths
or treat a historical qualification as a pass for new firmware.

2026-10-07 public tools and WebUI labels use lowercase `mcu_model`: for example
`tlsr8359_nowa154r`, `nrf52811_asel22`, `da14585_asel29`. The mapping is in
`webui/src/protocol/boardNames.json`; stable selectors and historical wire IDs
in the records below are not changed. The MCU-owned flashing entry must still
select exact `gr5513` or `gr5515` silicon for its qualified Full container. See
[flashing guide](../../docs/en/flash.md).

Display names are separate from stable board IDs. Exact-board adapters provide
`board_name` (long: MCU + board) and `board_name_short` (board only) to the common
GUI; compile-time boards may use `INKTAG_BOARD_DISPLAY_NAME` and
`INKTAG_BOARD_DISPLAY_NAME_SHORT`. Common chooses the long name if it fits the
actual PCB row's font and available width/height, otherwise the short name.
152x152 is a regression fixture, not a hard-coded short-name condition.
Do not split/truncate an internal ID to invent the short name. Missing short
metadata preserves the supplied long name. This never renames BLE, changes an
OTA compatibility ID or depends on the currently attached panel model.
NRF52832 PriceETag pilots `nrf52832_priceetag` / `priceetag`; other vendor
adapters take the rule in a reviewed batch, with comparable size and optical
evidence. The V1 WebUI native-page preview uses the same supplied metadata.

InkTag firmware is selected by the physical PCB, not by MCU alone. Stable IDs
normally use `<mcu>_<pcb>`, for example `gr5331_zkc21v`; an established product
name may instead lead when it still identifies one exact MCU/PCB combination.

The layers are deliberately separate:

1. `firmware/ports/<mcu>` provides BLE, flash, GPIO, SPI and RTC integration.
2. `firmware/boards/boards.json` identifies the PCB, wiring and output artifact.
3. `firmware/boards/<firmware_family>.h` concentrates the live hardware
   descriptors. Exact clock, fitted-device and protected-net facts remain
   distinct by selector; user-configurable defaults remain in the presets.
4. `firmware/common/epd` contains board-independent screen controller drivers.
5. A runtime `EPD_*` screen profile selects panel size, color and controller.

Firmware reports both values independently after connection:

```text
board=gr5331_zkc21v
profile=EPD_213_BWR_SSD1680
```

Changing the panel profile must not change `board_id`. A newly supported
silkscreen uses the maintained mapping and an existing or newly qualified
backend ID, not a new App/OTA merely for a different product. The former
temporary `nrf52811_common` and
EL022H2-only identities have been replaced by `aesl22_nrf`. This ID denotes
the Alibaba Cloud AESL nRF52811 board/application family and covers the
replaceable-panel EL022H2 and EL029H2 variants with one application and OTA
artifact. Public records still identify SOLUM as an EL022H2 supplier; the AESL
name records the product/source family and does not claim that Alibaba designed
or fabricated every PCB. A materially different nRF52811 PCB, ABI or memory
layout must still receive a new board ID.

EL022H2 has three active-low LED channels on P0.16/P0.18/P0.17 and no
physical button. The exact red/green/blue mapping is not yet qualified, so the
registry records neutral channel pins and the core firmware keeps its existing
P0.18 indicator until that mapping is measured.

Similar commercial names are not interchangeable board identities.
`gr5331_zkc21v` and the newly identified `om6626_zkc21b` are different product
models, MCUs and PCBs and must never share a firmware artifact. ZKC21B and
ZKC42B each have a verified 1 MiB stock backup and share the compatible OM ZK
application family. Their factory records provide separate B2/B3 screen defaults even
though the recovered EPD GPIO table is currently the same.
The ZKC42B v1.0 exact-board directory also owns the qualified GT25Q16A record:
SPI0 PAD8/PAD9/PAD10/PAD11, JEDEC `C4 60 15` and 2 MiB capacity.
Those constants are not OM6626-family defaults and must never make ZKC21B
advertise or touch an auxiliary Flash. Two matching complete reads preserved the
factory data; a full typed-record fill/readback/slideshow/clear trial qualified
`0x002000-0x1FFFFF`, while the first two sectors remain permanently excluded.

DA14585 follows the same ownership rule with one extra packaging layer:
`aesl29_da` and `aesl42_da` are separate physical board IDs and stock-derived
first-flash artifacts, while both run the same `da14585_aesl` application and
SUOTA image. The application reads the preserved `0x02`/`0x03` factory selector
and matching geometry to report the exact ID at runtime; an unreadable or
unknown record fails back to the shared application-target identity rather than
guessing. Their confirmed shared panel bus lives in
`firmware/boards/da14585_aesl.h`; the earlier 2.9-inch controller remains
unqualified, so its registry default profile stays `null` until hardware
regression passes.

The former exact IDs `da14585_aesl29` and `da14585_aesl42` are no longer
accepted by 0.2.0 clients, tools or packagers. Checked-in stock dumps keep their
historical paths so recorded hashes do not appear to be new evidence. The
selector bytes are unchanged; new builds and release manifests use only the
canonical `aesl29_da` / `aesl42_da` names.

The optional `lowFrequencyClock` object records the oscillator actually fitted
to an exact PCB. `source: lfxo` means the registered board has an installed
32.768 kHz crystal; it does not qualify startup, accuracy or current by itself.
A shared application may select one SDK clock macro only when every exact-board
member declares the same source. The build must stop on a missing, unknown or
divergent source instead of replacing board evidence with an MCU-family default.

`boards.json` is the source of truth for known board associations. Unknown
panel controllers and unverified GPIOs stay `null`; do not turn guesses into a
screen profile or a production firmware name.

Hanshow Nowa-154R-N and Nebular-420R-N are experimental TLSR8359 board IDs.
They share `InkTag-tlsr8359_hanshow` application bytes and differ only through
the bounded selector in their first-flash images. Their two matching,
non-mirrored 512 KiB reads justify `verifiedFlashWindowSizeBytes`; this field is
not interchangeable with `physicalFlashSizeBytes`. The first-flash wrapper
therefore requires an explicit bench-layout acknowledgement and preserves the
registered factory ranges from the exact stock backup.
Their recorded internal IDs are `hs_nowa154` and `hs_nebular420`; unpublished long
development IDs are not accepted by clients, fixtures, packages or flashing
tools. Numeric selectors 1/2 and the
`HANSHOW_NOWA_154R_N` / `HANSHOW_NEBULAR_420R_N` panel recipes are unchanged.
Both board entries record the recovered FM11NC08-compatible bus. The common
application exposes an experimental qualified-session tap input and persistent
actions, but this does not qualify the Type 4 FIFO/APDU, NDEF, UID or user-area
protocol; clients must honor the firmware's explicit `ndef=0` capability.
The common application also compiles V2 button bindings, input events, IO
configuration and a bounded passive-piezo PWM backend. Neither Hanshow PCB has
an exact UART contact-to-GPIO mapping, so both records keep `auxIo.supported`
false, use GPIO `255`/disabled defaults and advertise no button/buzzer/IO
capability. B85 UART pinmux alternatives are candidates only, not board facts.

Every board record carries the canonical flat `lfclkSource` and
`lfclkQualification` fields. `lfclkSource` is `lfxo`, `internal-rc` or
`unknown`; optional `lfclkAccuracyPpm` is allowed only with supporting evidence.
These are exact-PCB facts, not MCU-family capabilities. Product-common code
owns elapsed-time/fractional-tick semantics, a vendor-common adapter selects the
source and reads an always-running counter, and the exact board header supplies
the source, nominal rate, counter width and qualification. Seeing an empty
crystal footprint does not qualify an LFXO retrofit. Both inspected Hanshow
boards have no fitted 32.768 kHz crystal, remain on internal RC and are marked
non-precision; native clock pages remain available with an accuracy warning.

`panelReplaceable` is mechanical metadata. `true` means the panel is connected
through a serviceable FPC/socket in the known assembly; it does not qualify
every replacement panel electrically. ZKC21V, ZKC21B and ZKC42B use soldered
panel assemblies and are `false`. DA14585 AESL and nRF boards use replaceable
panels and are `true`.

The optional `led` and `button` fields record physical board capability, not a
feature request. Use `led: null` and `button.present: false` when hardware is absent;
unknown RGB color order must remain explicit instead of assigning guessed color
names to GPIOs.

An optional `auxIo` object records exact-board expansion defaults separately
from fitted hardware. `defaultEnabled: false` means firmware must leave every
candidate high-impedance until a client explicitly writes an enabled
configuration. A factory UART label or pinmux alternative is not enough to
release arbitrary pins: the record must name the exact allowed role and keep
SWD, reset, power, boot, display, NFC and production-calibration contacts
reserved. A board may record `auxIo.supported: false` to preserve an analyzed
but not yet qualified mapping. For example, ZKC21V factory `TX=AON6` conflicts
with its panel power enable. Its `RX=AON7` contact exists on the PCB but is
reachable only after shell removal, and its continuity plus held-low-at-boot
and recovery behavior have not been qualified. This gate must not be converted
into a client-visible capability merely because InkTag disables UART logging.
`unpopulatedNfcFootprint` is assembly evidence only; it does not make the
footprint's unresolved MCU nets safe GPIOs.

An optional `experimentalBuzzer` object records a proposed exact-board circuit
or pin-sharing experiment. It is documentation, not a capability declaration:
`status: proposed-unqualified` and `defaultEnabled: false` must not cause the
firmware or WebUI to advertise button, buzzer or custom-IO support. The record
must state the complete ownership transition and physical acceptance gates.
ZKC21V uses this form for the proposed screen-exclusive `AON6` experiment
because that pad is both the exterior TX contact and the panel power enable.

An optional `nfc` boolean records whether the exact board/revision has positive
NFC hardware evidence. `nfc: true` does not by itself qualify an InkTag NFC
driver. Controller evidence, I2C address and unresolved wiring may be recorded
separately, while SCL/SDA/IRQ/power pins must remain unspecified until factory
analysis or physical measurement confirms them. See
[`docs/nfc-esl-research.md`](../../docs/nfc-esl-research.md).

Experimental radio code is also gated separately from production capability.
For example, `INKTAG_BOARD_FIND_EXPERIMENTAL_AVAILABLE=1` only permits the
default-off Find runtime and its configuration API to be packaged for bench
work. It must not set the production `FIND` capability; that remains controlled
by exact-board qualification evidence. A valid public advertisement key and an
explicit user action are required before experimental advertising starts.

The matching firmware-family header must be selected explicitly by every
release build. A qualified preset may provide a safe first-boot default, but
the firmware must not force that
screen after the user selects and persists another runtime profile. A
bring-up-only target with unidentified wiring remains on the existing safe
no-backend path, not a guessed descriptor or a qualified release assembly.
PriceETag is a named physical board and therefore uses
`nrf52832_priceetag`. Its preserved stock firmware establishes P0.08-P0.13 as
the panel bus and P0.02 as the button. The installed panel is user-confirmed as
1.54-inch 200×200 BW, while one reachable stock routine and its recovered LUT
operate on a 152×152 compatibility buffer. The provisional board default keeps
that recovered SSD1675-family path until full-panel hardware testing identifies
the exact controller/window. The PCB has no LED and therefore keeps that
optional pin disabled. It must not fall back to the `_unassigned` release
identity.

Nordic LFCLK is also selected by exact board, never by an nRF52-family default.
A fitted 32.768 kHz crystal uses `INKTAG_BOARD_LFCLK_SOURCE=1`, zero RC
calibration intervals and a board-evidenced or conservative accuracy. Missing,
unpopulated or unknown crystal hardware must use its own board clock header and
remain on RC; it must not inherit the settings of another Nordic board.

An optional `ota` object records board-owned physical boundaries, not generic
MCU capability. `swap-with-scratch-v1` means equal fixed-address A/B slots,
two redundant control sectors and one scratch sector; it must match linker,
Bootloader, package and Web validation constants exactly. `imageStore` may say
that a range is reserved only. A reserved range is not user-visible slideshow
capacity until that port implements and tests its record API.
