# Adapted devices

## MCUs and devices

| Manufacturer | MCU | Researched / adapted label families |
| --- | --- | --- |
| Shenzhen Goodix Technology Co., Ltd. | GR5331, GR5513, GR5515 | ZKC21V, ZKC42V, ZKC42VM |
| OnMicro | OM6626 | ZKC21B-NAE4, ZKC42B-NE4, some ZKC21V and Hanshow devices |
| Telink | TLSR8359 | Hanshow Nowa-154R-N, Nowa-213R-N, Nebular-213R-N and Nebular-420R-N |
| Nordic Semiconductor | nRF52832 | PriceETag; no firmware in this preview |
| Nordic Semiconductor | nRF52811 | AESL / Hema EL022H2; independent project, no firmware here |
| Renesas / formerly Dialog | DA14585 | AESL-2.9 and AESL-4.2 / Hema; independent project, no firmware here |

HS9118 is a package marking found on some Hanshow chips, not a unique silicon identity. Different actual MCUs have been found under it. Neither a label model nor a board marking alone establishes the installed screen. Identify the actual MCU, board and panel before selecting a tool or image.

Manufacturer reference: [Goodix company information](https://www.goodix.com/en/about_goodix/profile/overview). See [devices and panels](devices.md) for geometry, colour, controller and storage differences.

## Board and attachment status

Select firmware by the exact MCU and PCB model. Firmware columns list included files, not an official release or hardware qualification. Experimental targets are not intended for production; never substitute firmware for a similar board.

| Board | MCU | Release scope | Status | Complete flashing in this release | OTA in this release | Windows | Linux | macOS |
|---|---|---|---|---:|---:|---|---|---|
| gr5331_zkc21v (`gr5331_zkc21v`) | Goodix GR5331 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| gr5513_zkc42v (`gr5513_zkc42v`) | Goodix GR5513 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| gr5513_zkc42vm (`gr5513_zkc42vm`) | Goodix GR5513 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| gr5515_zkc42vm (`gr5515_zkc42vm`) | Goodix GR5515 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| om6626_zkc21b (`om6626_zkc21b`) | OnMicro OM6626 | Current candidate | Beta | — | — | Qualified | Pending | Pending |
| om6626_zkc42b (`om6626_zkc42b`) | OnMicro OM6626 | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| tlsr8359_nowa154r (`tlsr8359_nowa154r`) | Telink TLSR8359 | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| tlsr8359_nebular420r (`tlsr8359_nebular420r`) | Telink HS9118 / TLSR8359-compatible | Current candidate | Experimental | — | — | Qualified | Pending | Pending |
| om6626_hsel (`om6626_hsel`) | OnMicro OM6626 | Deferred | Experimental | — | — | Pending | Pending | Pending |
| nrf52811_asel22 (`nrf52811_asel22`) | Nordic nRF52811 | Deferred | Beta | — | — | Qualified | Qualified | Pending |
| nrf52832_priceetag (`nrf52832_priceetag`) | Nordic nRF52832 | Deferred | Beta | — | — | Qualified | Qualified | Pending |
| da14585_asel29 (`da14585_asel29`) | Renesas/Dialog DA14585 | Deferred | Experimental | — | — | Qualified | Pending | Pending |
| da14585_asel42 (`da14585_asel42`) | Renesas/Dialog DA14585 | Deferred | Beta | — | — | Qualified | Pending | Pending |

## Notes

- `gr5331_zkc21v`: This package does not include current firmware for this board.
- `gr5513_zkc42v`: This package does not include current firmware for this board.
- `gr5513_zkc42vm`: This package does not include current firmware for this board.
- `gr5515_zkc42vm`: This package does not include current firmware for this board.
- `om6626_zkc21b`: This package does not include current firmware for this board.
- `om6626_zkc42b`: This package does not include current firmware for this board.
- `tlsr8359_nowa154r`: This package does not include current firmware for this board.
- `tlsr8359_nebular420r`: This package does not include current firmware for this board.
- `om6626_hsel`: This package does not include current firmware for this board.
- `nrf52811_asel22`: This package does not include current firmware for this board.
- `nrf52832_priceetag`: This package does not include current firmware for this board.
- `da14585_asel29`: This package does not include current firmware for this board.
- `da14585_asel42`: This package does not include current firmware for this board.
